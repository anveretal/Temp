#ifndef MASTER_H
#define MASTER_H

#include <stdbool.h>

typedef void (*Hook)(void);

extern Hook executor_start_hook;

typedef struct {
    void* handle;
    char* name;
    void (*init)(void);
    void (*fini)(void);
    const char* (*get_name)(void);
} Plugin;

#endif // MASTER_H