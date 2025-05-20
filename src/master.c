#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>
#include <ctype.h>
#include "master.h"
#include "logger.h"
#include "config.h"

// Глобальный указатель на хук
Hook executor_start_hook = NULL;

// Прототипы вспомогательных функций
static int load_plugin(const char* plugin_name, Plugin* plugin);
static void unload_plugin(Plugin* plugin);
static char** get_plugins_from_config(int* count);
static void free_plugins_list(char** plugins, int count);
static void trim_whitespace(char* str);
static int is_debug_mode(void);
static void parse_command_line(int argc, char* argv[], char** config_path, char** log_path);

// Функция для выполнения цепочки хуков
void execute_hook(void) {
    if (executor_start_hook) {
        executor_start_hook();
    } else {
        LOG(STDERR, LOG_WARNING, "No hook registered");
    }
}

int main(int argc, char* argv[]) {
    // Парсим аргументы командной строки
    char* config_path = NULL;
    char* log_path = NULL;
    parse_command_line(argc, argv, &config_path, &log_path);

    // Определяем режим работы
    int debug_mode = is_debug_mode();
    if (debug_mode) {
        log_path = NULL; // В режиме отладки логи идут в stdout
        LOG(STDOUT, LOG_INFO, "Starting in debug mode");
    }

    // Инициализация логгера
    if (init_logger(log_path, 1024)) {
        fprintf(stderr, "Failed to initialize the logger\n");
        return 1;
    }

    // Инициализация системы конфигурации
    if (create_config_table()) {
        LOG(STDERR, LOG_ERROR, "Failed to initialize config system");
        fini_logger();
        return 1;
    }

    // Парсинг конфигурационного файла
    if (parse_config(config_path ? config_path : "proxy.conf")) {
        LOG(STDERR, LOG_ERROR, "Failed to parse config file");
        destroy_config_table();
        fini_logger();
        return 1;
    }

    // Загрузка плагинов
    int plugin_count = 0;
    char** plugins = get_plugins_from_config(&plugin_count);
    if (!plugins || plugin_count == 0) {
        LOG(STDERR, LOG_ERROR, "No plugins configured");
        destroy_config_table();
        fini_logger();
        return 1;
    }

    Plugin* loaded_plugins = malloc(plugin_count * sizeof(Plugin));
    int loaded_count = 0;

    for (int i = 0; i < plugin_count; i++) {
        if (load_plugin(plugins[i], &loaded_plugins[loaded_count]) == 0) {
            loaded_count++;
        } else {
            free(plugins[i]);
        }
    }

    free_plugins_list(plugins, plugin_count);

    if (loaded_count == 0) {
        LOG(STDERR, LOG_ERROR, "No plugins were loaded successfully");
        free(loaded_plugins);
        destroy_config_table();
        fini_logger();
        return 1;
    }

    // Выполнение основного хука
    execute_hook();

    // Выгрузка плагинов
    for (int i = 0; i < loaded_count; i++) {
        unload_plugin(&loaded_plugins[i]);
    }
    free(loaded_plugins);

    // Освобождение ресурсов
    if (destroy_config_table()) {
        LOG(STDERR, LOG_ERROR, "Failed to destroy config system");
    }

    if (fini_logger()) {
        fprintf(stderr, "Couldn't shut down logger\n");
        return 1;
    }

    return 0;
}

// Загрузка одного плагина
static int load_plugin(const char* plugin_name, Plugin* plugin) {
    // Убедимся, что имя плагина не содержит недопустимых символов
    char clean_name[256];
    size_t j = 0;
    for (size_t i = 0; plugin_name[i] && j < sizeof(clean_name)-1; i++) {
        if (isalnum(plugin_name[i]) || plugin_name[i] == '_' || plugin_name[i] == '-') {
            clean_name[j++] = plugin_name[i];
        }
    }
    clean_name[j] = '\0';
    
    if (j == 0) {
        LOG(STDERR, LOG_ERROR, "Invalid plugin name: %s", plugin_name);
        return 1;
    }

    char plugin_path[PATH_MAX];
    snprintf(plugin_path, sizeof(plugin_path), "install/plugins/%s.so", clean_name);

    // Дальше оригинальная реализация load_plugin...
    plugin->handle = dlopen(plugin_path, RTLD_NOW);
    if (!plugin->handle) {
        LOG(STDERR, LOG_ERROR, "Failed to load plugin %s: %s", clean_name, dlerror());
        return 1;
    }

    // Безопасное приведение через промежуточный void*
    void* sym;
    
    // Загружаем функцию name()
    *(void**)(&sym) = dlsym(plugin->handle, "name");
    plugin->name_func = (const char* (*)(void))sym;
    if (!plugin->name_func) {
        LOG(STDERR, LOG_ERROR, "Failed to load 'name' function from %s: %s", plugin_name, dlerror());
        dlclose(plugin->handle);
        return 1;
    }

    // Загружаем функцию init()
    *(void**)(&sym) = dlsym(plugin->handle, "init");
    plugin->init = (int (*)(void))sym;
    if (!plugin->init) {
        LOG(STDERR, LOG_ERROR, "Failed to load 'init' function from %s: %s", plugin_name, dlerror());
        dlclose(plugin->handle);
        return 1;
    }

    // Загружаем функцию fini()
    *(void**)(&sym) = dlsym(plugin->handle, "fini");
    plugin->fini = (int (*)(void))sym;
    if (!plugin->fini) {
        LOG(STDERR, LOG_ERROR, "Failed to load 'fini' function from %s: %s", plugin_name, dlerror());
        dlclose(plugin->handle);
        return 1;
    }

    // Инициализируем плагин
    if (plugin->init()) {
        LOG(STDERR, LOG_ERROR, "Plugin %s initialization failed", plugin_name);
        dlclose(plugin->handle);
        return 1;
    }

    plugin->name = strdup(plugin_name);
    LOG(STDOUT, LOG_INFO, "Plugin %s loaded successfully", plugin_name);
    return 0;
}

// Выгрузка плагина
static void unload_plugin(Plugin* plugin) {
    if (plugin->fini()) {
        LOG(STDERR, LOG_ERROR, "Plugin %s finalization failed", plugin->name);
    } else {
        LOG(STDOUT, LOG_INFO, "Plugin %s unloaded", plugin->name);
    }

    dlclose(plugin->handle);
    free(plugin->name);
}

// Получение списка плагинов из конфига
static char** get_plugins_from_config(int* count) {
    *count = 0;
    char** plugins = NULL;
    
    // 1. Проверяем переменную окружения
    const char* env_plugins = getenv("PROXY_MASTER_PLUGINS");
    if (env_plugins) {
        char* env_copy = strdup(env_plugins);
        char* token = strtok(env_copy, ",");
        int capacity = 2;
        plugins = malloc(capacity * sizeof(char*));
        
        while (token) {
            trim_whitespace(token);
            // Удаляем кавычки если они есть
            if (token[0] == '"' || token[0] == '\'') {
                memmove(token, token+1, strlen(token));
                token[strlen(token)-1] = '\0';
            }
            // Удаляем квадратные скобки если они есть
            if (token[0] == '[') {
                memmove(token, token+1, strlen(token));
            }
            if (token[strlen(token)-1] == ']') {
                token[strlen(token)-1] = '\0';
            }
            trim_whitespace(token);
            
            if (strlen(token) > 0) {
                if (*count >= capacity) {
                    capacity *= 2;
                    plugins = realloc(plugins, capacity * sizeof(char*));
                }
                plugins[*count] = strdup(token);
                (*count)++;
            }
            token = strtok(NULL, ",");
        }
        free(env_copy);
        return plugins;
    }
    
    // 2. Проверяем конфигурационный файл
    ConfigVariable plugins_var = get_variable("plugins");
    if (plugins_var.type == UNDEFINED || plugins_var.type != STRING || plugins_var.count == 0) {
        return NULL;
    }

    plugins = malloc(plugins_var.count * sizeof(char*));
    for (int i = 0; i < plugins_var.count; i++) {
        char* name = plugins_var.data.string[i];
        // Удаляем кавычки если они есть
        if (name[0] == '"' || name[0] == '\'') {
            memmove(name, name+1, strlen(name));
            name[strlen(name)-1] = '\0';
        }
        plugins[i] = strdup(name);
        (*count)++;
    }

    return plugins;
}

// Освобождение списка плагинов
static void free_plugins_list(char** plugins, int count) {
    if (!plugins) return;
    
    for (int i = 0; i < count; i++) {
        if (plugins[i]) {
            free(plugins[i]);
            plugins[i] = NULL;
        }
    }
    free(plugins);
}

// Удаление пробелов в начале и конце строки
static void trim_whitespace(char* str) {
    char* end;
    
    // Удаляем пробелы в начале
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return;
    
    // Удаляем пробелы в конце
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    *(end + 1) = 0;
}

// Проверка режима отладки
static int is_debug_mode(void) {
    char program_name[PATH_MAX] = {0};
    if (readlink("/proc/self/exe", program_name, sizeof(program_name)) != -1) {
        char* base_name = strrchr(program_name, '/');
        return base_name && strcmp(base_name + 1, "debug_proxy") == 0;
    }
    return 0;
}

// Парсинг аргументов командной строки
static void parse_command_line(int argc, char* argv[], char** config_path, char** log_path) {
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            *config_path = argv[++i];
        } else if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--logs") == 0) && i + 1 < argc) {
            *log_path = argv[++i];
        }
    }
}