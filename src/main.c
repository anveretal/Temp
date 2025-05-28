#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <limits.h>
#include "master.h"
#include "logger.h"
#include "config.h"

// Глобальный указатель на хук
Hook executor_start_hook = NULL;

// Структура для хранения информации о плагине
typedef struct {
    void* handle;
    char* name;
    int (*init)(void);
    int (*fini)(void);
    const char* (*name_func)(void);
} Plugin;

// Союз для безопасного приведения типов при dlsym
union Caster_to_function_pointer {
    void *ptr;
    void (*function_pointer)(void);
};

union Caster_to_function_pointer2 {
    void *ptr;
    int (*function_pointer)(void);
};

union Caster_to_function_pointer3 {
    void *ptr;
    const char *(*function_pointer)(void);
};

// Функция для выполнения цепочки хуков
void execute_hook(void) {
    if (executor_start_hook) {
        executor_start_hook();
    } else {
        LOG_SET(STDERR, LOG_WARNING, "No hook registered");
    }
}

// Функция для загрузки одного плагина
int load_plugin(const char* plugin_name, Plugin* plugin) {
    char plugin_path[PATH_MAX];
    snprintf(plugin_path, sizeof(plugin_path), "install/plugins/%s.so", plugin_name);

    plugin->handle = dlopen(plugin_path, RTLD_NOW);
    if (!plugin->handle) {
        LOG_SET(STDERR, LOG_ERROR, 
            "Library couldn't be opened.\n\tLibrary's path is %s\n\tdlopen: %s\n\tcheck plugins folder or rename library", 
            plugin_path, dlerror());
        return 1;
    }

    // Получаем функцию name
    union Caster_to_function_pointer3 name_plugin_pointer;
    name_plugin_pointer.ptr = dlsym(plugin->handle, "name");

    plugin->name_func = name_plugin_pointer.function_pointer;
    if (!plugin->name_func) {
        LOG_SET(STDERR, LOG_ERROR,
            "Library couldn't execute name.\n\tLibrary's name is %s. Dlsym message: %s\n\tcheck plugins folder or rename library",
            plugin_name, dlerror());
        dlclose(plugin->handle);
        return 1;
    }

    plugin->name = strdup(plugin_name);

    // Получаем функцию init
    union Caster_to_function_pointer2 init_plugin_pointer;
    init_plugin_pointer.ptr = dlsym(plugin->handle, "init");

    plugin->init = init_plugin_pointer.function_pointer;
    if (!plugin->init) {
        LOG_SET(STDERR, LOG_ERROR,
            "Library couldn't execute init.\n\tLibrary's name is %s. Dlsym message: %s\n\tcheck plugins folder or rename library",
            plugin_name, dlerror());
        dlclose(plugin->handle);
        free(plugin->name);
        return 1;
    }

    // Получаем функцию fini
    union Caster_to_function_pointer2 fini_plugin_pointer;
    fini_plugin_pointer.ptr = dlsym(plugin->handle, "fini");

    plugin->fini = fini_plugin_pointer.function_pointer;
    if (!plugin->fini) {
        LOG_SET(STDERR, LOG_ERROR,
            "Library couldn't execute fini.\n\tLibrary's name is %s. Dlsym message: %s\n\tcheck plugins folder or rename library",
            plugin_name, dlerror());
        dlclose(plugin->handle);
        free(plugin->name);
        return 1;
    }

    // Инициализируем плагин
    if (plugin->init()) {
        LOG_SET(STDERR, LOG_ERROR, "Plugin %s initialization failed", plugin_name);
        dlclose(plugin->handle);
        free(plugin->name);
        return 1;
    }

    LOG_SET(STDOUT, LOG_INFO, "Plugin %s loaded", plugin_name);
    return 0;
}

// Функция для выгрузки плагина
void unload_plugin(Plugin* plugin) {
    if (plugin->fini()) {
        LOG_SET(STDERR, LOG_ERROR, "Plugin %s finalization failed", plugin->name);
    } else {
        LOG_SET(STDOUT, LOG_INFO, "Plugin %s unloaded", plugin->name);
    }

    dlclose(plugin->handle);
    free(plugin->name);
}

// Функция для получения списка плагинов из конфига
char** get_plugins_from_config(int* count) {
    ConfigVariable plugins_var = get_variable("plugins");
    *count = 0;

    if (plugins_var.type == UNDEFINED || plugins_var.type != STRING || plugins_var.count == 0) {
        // Проверяем переменную окружения
        const char* env_plugins = getenv("PROXY_MASTER_PLUGINS");
        if (env_plugins) {
            // Разделяем строку по запятым
            int capacity = 2;
            char** plugins = malloc(capacity * sizeof(char*));
            char* token = strtok((char*)env_plugins, ",");
            
            while (token) {
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
            
            return plugins;
        }
        return NULL;
    }

    // Выделяем память под список плагинов
    char** plugins = malloc(plugins_var.count * sizeof(char*));
    for (int i = 0; i < plugins_var.count; i++) {
        plugins[i] = strdup(plugins_var.data.string[i]);
        (*count)++;
    }

    return plugins;
}

int main(int argc, char *argv[]) {
    // Определяем режим работы (обычный/отладка)
    int debug_mode = 0;
    char program_name[PATH_MAX] = {0};
    if (readlink("/proc/self/exe", program_name, sizeof(program_name)) != -1) {
        char* base_name = strrchr(program_name, '/');
        if (base_name && strcmp(base_name + 1, "debug_proxy") == 0) {
            debug_mode = 1;
        }
    }

    // Инициализация логгера
    char* log_path = NULL;
    int log_size_limit = 1024; // Значение по умолчанию 1MB

    // Парсим аргументы командной строки
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                i++; // Пропускаем значение пути
            }
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--logs") == 0) {
            if (i + 1 < argc) {
                log_path = argv[++i];
            }
        }
    }

    // Если путь к логам не задан в аргументах, проверяем переменную окружения
    if (!log_path) {
        log_path = getenv("PROXY_LOG_PATH");
    }

    // В режиме отладки все логи идут в stdout
    if (debug_mode) {
        log_path = NULL;
        LOG_SET(STDOUT, LOG_INFO, "Starting in debug mode");
    }

    // Инициализируем логгер
    if (init_logger(log_path, log_size_limit)) {
        fprintf(stderr, "Failed to initialize the logger\n");
        return 1;
    }

    // Инициализируем систему конфигурации
    if (create_config_table()) {
        LOG_SET(STDERR, LOG_ERROR, "Failed to initialize config system");
        fini_logger();
        return 1;
    }

    // Парсим конфиг
    char* config_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_path = argv[++i];
            }
        }
    }

    // Если путь не задан в аргументах, проверяем переменную окружения
    if (!config_path) {
        config_path = getenv("PROXY_CONFIG_PATH");
    }

    // Если путь все еще не задан, используем значение по умолчанию
    if (!config_path) {
        config_path = "proxy.conf";
    }

    if (parse_config(config_path)) {
        LOG_SET(STDERR, LOG_ERROR, "Failed to parse config file");
        destroy_config_table();
        fini_logger();
        return 1;
    }

    // Получаем список плагинов для загрузки
    int plugin_count = 0;
    char** plugins = get_plugins_from_config(&plugin_count);
    if (!plugins || plugin_count == 0) {
        LOG_SET(STDERR, LOG_ERROR, "No plugins configured");
        destroy_config_table();
        fini_logger();
        return 1;
    }

    // Загружаем плагины
    Plugin* loaded_plugins = malloc(plugin_count * sizeof(Plugin));
    int loaded_count = 0;

    for (int i = 0; i < plugin_count; i++) {
        if (load_plugin(plugins[i], &loaded_plugins[loaded_count]) == 0) {
            loaded_count++;
        } else {
            free(plugins[i]);
        }
    }

    free(plugins);

    if (loaded_count == 0) {
        LOG_SET(STDERR, LOG_ERROR, "No plugins were loaded successfully");
        free(loaded_plugins);
        destroy_config_table();
        fini_logger();
        return 1;
    }

    // Выполняем основной хук
    execute_hook();

    // Выгружаем плагины
    for (int i = 0; i < loaded_count; i++) {
        unload_plugin(&loaded_plugins[i]);
    }
    free(loaded_plugins);

    // Освобождаем ресурсы
    if (destroy_config_table()) {
        LOG_SET(STDERR, LOG_ERROR, "Failed to destroy config system");
    }

    if (fini_logger()) {
        fprintf(stderr, "Couldn't shut down logger\n");
        return 1;
    }

    return 0;
}