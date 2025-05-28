#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef union {
    int64_t* integer;
    double* real;
    char** string;
} ConfigData;

typedef enum {
    UNDEFINED = 0,
    INTEGER = 1,
    REAL,
    STRING
} ConfigVarType;

typedef struct {
    char* name;
    char* description;
    ConfigData data;
    ConfigVarType type;
    int count;
} ConfigVariable;

int create_config_table(void);
int destroy_config_table(void);
int parse_config(const char* path);
int define_variable(const ConfigVariable variable);
ConfigVariable get_variable(const char* name);
int set_variable(const ConfigVariable variable);
bool does_variable_exist(const char* name);

// Макрос для проверки инициализации конфига
#define CONFIG_CHECK() \
    if (!config.is_initialized) { \
        LOG_SET(STDERR, LOG_ERROR, "Config system not initialized"); \
        return 1; \
    }

#endif // CONFIG_H