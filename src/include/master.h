#ifndef MASTER_H
#define MASTER_H

#include <stdbool.h>

typedef void (*Hook)(void);

// Глобальные переменные
extern Hook executor_start_hook;

// Структура для хранения информации о плагине
typedef struct Plugin {
    void* handle;
    char* name;
    int (*init)(void);
    int (*fini)(void);
    const char* (*get_name)(void);
    struct Plugin* next;
} Plugin;

// Функции для работы с плагинами
int load_plugin(const char* plugin_name);
int unload_plugin(const char* plugin_name);
void unload_all_plugins(void);
Plugin* find_plugin(const char* plugin_name);
void execute_hook(void);

#endif // MASTER_H