#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Статическая таблица конфигурации (синглтон)
static ConfigVariable* config_table = NULL;
static int config_size = 0;
static int config_capacity = 0;

// Удаление лишних пробелов
char* trim(char* str) {
    char* end;

    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return str;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    end[1] = '\0';

    return str;
}

// Парсит строку как ключ=значение
int parse_line(const char* line, char** key_out, char** value_out) {
    char* line_copy = strdup(line);
    if (!line_copy) return -1;

    // Удаляем комментарии
    char* comment = strchr(line_copy, '#');
    if (comment) *comment = '\0';

    char* eq = strchr(line_copy, '=');
    if (!eq || eq == line_copy) {
        free(line_copy);
        return -1; // Нет ключа или равно
    }

    *eq = '\0';
    char* key = trim(line_copy);
    char* value = trim(eq + 1);

    if (key[0] == '\0' || value[0] == '\0') {
        free(line_copy);
        return -1;
    }

    *key_out = strdup(key);
    *value_out = strdup(value);

    free(line_copy);
    return 0;
}

// Определяет тип значения
ConfigVarType get_value_type(const char* value) {
    if (strstr(value, "[") != NULL && strstr(value, "]") != NULL) {
        return STRING; // Массивы пока обрабатываем как строки
    } else if (strchr(value, '"') != NULL) {
        return STRING;
    } else if (strchr(value, '.') != NULL) {
        return REAL;
    } else {
        return INTEGER;
    }
}

// Парсит значение
ConfigData parse_value(const char* value, ConfigVarType type, int* count) {
    ConfigData data = {0};
    *count = 1;

    switch (type) {
        case INTEGER: {
            int64_t* val = malloc(sizeof(int64_t));
            *val = atoll(value);
            data.integer = val;
            break;
        }
        case REAL: {
            double* val = malloc(sizeof(double));
            *val = atof(value);
            data.real = val;
            break;
        }
        case STRING: {
            if (value[0] == '"' && value[strlen(value)-1] == '"') {
                char* val = strdup(value + 1);
                val[strlen(val) - 1] = '\0';
                data.string = &val;
            } else {
                data.string = &value; // массивы временно
            }
            break;
        }
        default:
            break;
    }

    return data;
}

// Инициализирует систему конфигурации
int create_config_table() {
    if (config_table != NULL) return -1;

    config_capacity = 10;
    config_size = 0;
    config_table = calloc(config_capacity, sizeof(ConfigVariable));
    if (!config_table) return -1;

    return 0;
}

// Освобождает ресурсы системы конфигурации
int destroy_config_table() {
    if (!config_table) return -1;

    for (int i = 0; i < config_size; ++i) {
        free(config_table[i].name);
        free(config_table[i].description);
        if (config_table[i].type == INTEGER) free(config_table[i].data.integer);
        else if (config_table[i].type == REAL) free(config_table[i].data.real);
        else if (config_table[i].type == STRING) free(*config_table[i].data.string);
    }

    free(config_table);
    config_table = NULL;
    config_size = 0;
    config_capacity = 0;

    return 0;
}

// Парсит файл конфигурации
int parse_config(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        LOG_SET(FILESTREAM, LOG_ERROR, "File %s not found", path);
        return -1;
    }

    LOG_SET(FILESTREAM, LOG_INFO, "Start read config file %s", path);

    char line[1024];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        char* key = NULL;
        char* value = NULL;

        if (parse_line(line, &key, &value) != 0) {
            LOG_SET(FILESTREAM, LOG_ERROR, "Configuration file %s has syntax error at %d: %s", path, line_num, line);
            continue;
        }

        ConfigVarType type = get_value_type(value);
        int count = 1;
        ConfigData data = parse_value(value, type, &count);

        ConfigVariable var = {
            .name = key,
            .description = "",
            .data = data,
            .type = type,
            .count = count
        };

        if (define_variable(var) != 0) {
            free(key);
            free(value);
            fclose(fp);
            return -1;
        }

        free(value);
    }

    fclose(fp);
    LOG_SET(FILESTREAM, LOG_INFO, "Finish read config file %s", path);
    return 0;
}

// Регистрирует новую переменную
int define_variable(const ConfigVariable variable) {
    if (config_size >= config_capacity) {
        config_capacity *= 2;
        ConfigVariable* new_table = realloc(config_table, config_capacity * sizeof(ConfigVariable));
        if (!new_table) return -1;
        config_table = new_table;
    }

    config_table[config_size++] = variable;
    return 0;
}

// Получает значение переменной по имени
ConfigVariable get_variable(const char* name) {
    for (int i = 0; i < config_size; ++i) {
        if (strcmp(config_table[i].name, name) == 0) {
            return config_table[i];
        }
    }

    ConfigVariable undefined = {.type = UNDEFINED};
    return undefined;
}