#ifndef MASTER_H
#define MASTER_H

typedef void (*Hook)(void);
extern Hook executor_start_hook;

typedef struct Plugin {
    void* handle;
    char* name;
    int (*init)(void);
    int (*fini)(void);
    const char* (*name_func)(void);
} Plugin;

#endif // MASTER_H