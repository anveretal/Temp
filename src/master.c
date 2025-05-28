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

Hook executor_start_hook = NULL;

static Plugin* plugins_list = NULL;

static void trim_whitespace(char* str);
static int is_valid_plugin_name(const char* name);
static int add_plugin_to_list(Plugin* plugin);
static void remove_plugin_from_list(const char* plugin_name);
static char** parse_plugins_string(const char* plugins_str, int* count);
static void free_plugins_string(char** plugins, int count);

/* Основные функции для работы с плагинами */

int load_plugin(const char* plugin_name) {
    if (!plugin_name || !is_valid_plugin_name(plugin_name)) {
        LOG_SET(STDERR, LOG_ERROR, "Invalid plugin name: %s", plugin_name ? plugin_name : "NULL");
        return 1;
    }

    // Проверяем, не загружен ли уже плагин
    if (find_plugin(plugin_name)) {
        LOG_SET(STDERR, LOG_WARNING, "Plugin %s is already loaded", plugin_name);
        return 1;
    }

    // Формируем путь к плагину
    char plugin_path[PATH_MAX];
    snprintf(plugin_path, sizeof(plugin_path), "install/plugins/%s.so", plugin_name);

    // Загружаем shared library
    void* handle = dlopen(plugin_path, RTLD_NOW);
    if (!handle) {
        LOG_SET(STDERR, LOG_ERROR, "Failed to load plugin %s: %s", plugin_name, dlerror());
        return 1;
    }

    // Создаем структуру плагина
    Plugin* plugin = malloc(sizeof(Plugin));
    if (!plugin) {
        dlclose(handle);
        LOG_SET(STDERR, LOG_ERROR, "Memory allocation failed for plugin %s", plugin_name);
        return 1;
    }

    // Загружаем обязательные функции плагина
    *(void**)(&plugin->init) = dlsym(handle, "init");
    *(void**)(&plugin->fini) = dlsym(handle, "fini");
    *(void**)(&plugin->get_name) = dlsym(handle, "name");

    if (!plugin->init || !plugin->fini || !plugin->get_name) {
        dlclose(handle);
        free(plugin);
        LOG_SET(STDERR, LOG_ERROR, "Plugin %s is missing required functions", plugin_name);
        return 1;
    }

    // Инициализируем плагин
    if (plugin->init()) {
        dlclose(handle);
        free(plugin);
        LOG_SET(STDERR, LOG_ERROR, "Plugin %s initialization failed", plugin_name);
        return 1;
    }

    // Сохраняем информацию о плагине
    plugin->handle = handle;
    plugin->name = strdup(plugin_name);
    plugin->next = NULL;

    // Добавляем в список загруженных плагинов
    if (add_plugin_to_list(plugin)) {
        dlclose(handle);
        free(plugin->name);
        free(plugin);
        return 1;
    }

    if (is_logger_has_path()) {
        LOG_SET(FILESTREAM, LOG_INFO, "Plugin %s loaded successfully", plugin_name);
    }
    else {
        LOG_SET(STDOUT, LOG_INFO, "Plugin %s loaded successfully", plugin_name);
    }
    return 0;
}

int unload_plugin(const char* plugin_name) {
    Plugin* plugin = find_plugin(plugin_name);
    if (!plugin) {
        LOG_SET(STDERR, LOG_ERROR, "Plugin %s not found", plugin_name);
        return 1;
    }

    // Сначала логируем, потом освобождаем
    if (is_logger_has_path()) {
        LOG_SET(FILESTREAM, LOG_INFO, "Unloading plugin %s", plugin->name);
    }
    else {
        LOG_SET(STDOUT, LOG_INFO, "Unloading plugin %s", plugin->name);
    }

    // Финализируем плагин
    if (plugin->fini()) {
        LOG_SET(STDERR, LOG_ERROR, "Plugin %s finalization failed", plugin->name);
        return 1;
    }

    // Закрываем библиотеку
    dlclose(plugin->handle);

    // Удаляем из списка (освобождает память)
    remove_plugin_from_list(plugin_name);

    return 0;
}

void unload_all_plugins(void) {
    Plugin* current = plugins_list;
    while (current) {
        Plugin* next = current->next;
        unload_plugin(current->name);
        current = next;
    }
}

Plugin* find_plugin(const char* plugin_name) {
    Plugin* current = plugins_list;
    while (current) {
        if (strcmp(current->name, plugin_name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void execute_hook(void) {
    if (executor_start_hook) {
        executor_start_hook();
    } else {
        LOG_SET(STDERR, LOG_WARNING, "No hook registered");
    }
}

/* Вспомогательные функции */

static int is_valid_plugin_name(const char* name) {
    if (!name || strlen(name) == 0) return 0;
    
    for (size_t i = 0; i < strlen(name); i++) {
        if (!isalnum(name[i]) && name[i] != '_' && name[i] != '-') {
            return 0;
        }
    }
    return 1;
}

static int add_plugin_to_list(Plugin* plugin) {
    if (!plugin) return 1;
    
    if (!plugins_list) {
        plugins_list = plugin;
    } else {
        Plugin* current = plugins_list;
        while (current->next) {
            current = current->next;
        }
        current->next = plugin;
    }
    return 0;
}

static void remove_plugin_from_list(const char* plugin_name) {
    if (!plugins_list) return;
    
    Plugin *current = plugins_list, *prev = NULL;
    
    while (current) {
        if (strcmp(current->name, plugin_name) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                plugins_list = current->next;
            }
            
            // Логирование уже выполнено, можно освобождать
            free(current->name);
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

static char** parse_plugins_string(const char* plugins_str, int* count) {
    *count = 0;
    if (!plugins_str) return NULL;
    
    char* str = strdup(plugins_str);
    if (!str) return NULL;
    
    // Удаляем квадратные скобки и кавычки если они есть
    char* p = str;
    while (*p == ' ' || *p == '[' || *p == ']' || *p == '"' || *p == '\'') p++;
    char* end = str + strlen(str) - 1;
    while (end > p && (*end == ' ' || *end == '[' || *end == ']' || *end == '"' || *end == '\'')) end--;
    *(end + 1) = '\0';
    
    // Разделяем строку по запятым
    int capacity = 2;
    char** plugins = malloc(capacity * sizeof(char*));
    if (!plugins) {
        free(str);
        return NULL;
    }
    
    char* token = strtok(p, ",");
    while (token) {
        trim_whitespace(token);
        // Удаляем кавычки если они есть
        if (token[0] == '"' || token[0] == '\'') {
            memmove(token, token + 1, strlen(token));
            token[strlen(token) - 1] = '\0';
        }
        trim_whitespace(token);
        
        if (strlen(token) > 0) {
            if (*count >= capacity) {
                capacity *= 2;
                char** tmp = realloc(plugins, capacity * sizeof(char*));
                if (!tmp) {
                    free_plugins_string(plugins, *count);
                    free(str);
                    return NULL;
                }
                plugins = tmp;
            }
            plugins[*count] = strdup(token);
            if (!plugins[*count]) {
                free_plugins_string(plugins, *count);
                free(str);
                return NULL;
            }
            (*count)++;
        }
        token = strtok(NULL, ",");
    }
    
    free(str);
    return plugins;
}

static void free_plugins_string(char** plugins, int count) {
    if (!plugins) return;
    
    for (int i = 0; i < count; i++) {
        free(plugins[i]);
    }
    free(plugins);
}

static void trim_whitespace(char* str) {
    if (!str) return;
    
    char* end;
    
    // Удаляем пробелы в начале
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return;
    
    // Удаляем пробелы в конце
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    *(end + 1) = '\0';
}



int main(int argc, char* argv[]) {
    // Определяем режим отладки
    char program_name[PATH_MAX] = {0};
    if (readlink("/proc/self/exe", program_name, sizeof(program_name)) != -1) {
        char* base_name = strrchr(program_name, '/');
        if (base_name && strcmp(base_name + 1, "debug_proxy") == 0) {
            logger_debug_mode = 1;
            fprintf(stdout, "Starting in debug mode");
        }
    }

    // Инициализация логгера
    if (init_logger(NULL, -1)) {
        fprintf(stderr, "Failed to initialize the logger\n");
        return 1;
    }

    // Инициализация системы конфигурации
    if (create_config_table()) {
        LOG_SET(STDERR, LOG_ERROR, "Failed to initialize config system");
        fini_logger();
        return 1;
    }

    // Парсинг конфигурационного файла
    if (parse_config("proxy.conf")) {
        LOG_SET(STDERR, LOG_ERROR, "Failed to parse config file");
        destroy_config_table();
        fini_logger();
        return 1;
    }

    // Проверяем, есть ли в конфиге настройки логгера
    ConfigVariable log_path = get_variable("logs");
    if (log_path.type == STRING) {
        fini_logger();
        if (init_logger(*log_path.data.string, -1)) {
            fprintf(stderr, "Failed to reinitialize the logger\n");
            return 1;
        }
    }

    // Загрузка плагинов из конфига
    ConfigVariable plugins_var = get_variable("plugins");
    if (plugins_var.type != UNDEFINED && plugins_var.type == STRING) {
        int plugin_count = 0;
        char** plugins = parse_plugins_string(*plugins_var.data.string, &plugin_count);
        
        if (plugins) {
            for (int i = 0; i < plugin_count; i++) {
                if (load_plugin(plugins[i])) {
                    LOG_SET(STDERR, LOG_ERROR, "Failed to load plugin: %s", plugins[i]);
                }
            }
            free_plugins_string(plugins, plugin_count);
        }
    }

    // Альтернативно: загрузка из переменной окружения
    const char* env_plugins = getenv("PROXY_MASTER_PLUGINS");
    if (env_plugins) {
        int plugin_count = 0;
        char** plugins = parse_plugins_string(env_plugins, &plugin_count);
        
        if (plugins) {
            for (int i = 0; i < plugin_count; i++) {
                if (load_plugin(plugins[i])) {
                    LOG_SET(STDERR, LOG_ERROR, "Failed to load plugin: %s", plugins[i]);
                }
            }
            free_plugins_string(plugins, plugin_count);
        }
    }

    execute_hook();

    unload_all_plugins();

    // Освобождение ресурсов
    if (destroy_config_table()) {
        LOG_SET(STDERR, LOG_ERROR, "Failed to destroy config system");
    }

    if (fini_logger()) {
        fprintf(stderr, "Couldn't shut down logger\n");
        return 1;
    }

    return 0;
}