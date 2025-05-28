#include "config.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>

#define MAX_KEY_LENGTH 128
#define MAX_LINE_LENGTH 1024
#define DEFAULT_CONFIG_FILE "proxy.conf"

static ConfigVariable* config_table = NULL;
static size_t config_table_size = 0;
static bool is_initialized = false;

// Вспомогательные функции
static void trim_whitespace(char* str) {
    if (!str) return;
    
    char* end;
    // Удаляем пробелы в начале
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return;
    
    // Удаляем пробелы в конце
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    // Завершаем строку
    *(end + 1) = '\0';
}

static int is_valid_key(const char* key) {
    if (!key || strlen(key) > MAX_KEY_LENGTH - 1) return 0;
    
    for (const char* p = key; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
            return 0;
        }
    }
    return 1;
}

static ConfigVarType detect_value_type(const char* value) {
    if (!value) return UNDEFINED;
    
    char* endptr;
    
    // Проверяем на целое число
    strtoll(value, &endptr, 10);
    if (*endptr == '\0') return INTEGER;
    
    // Проверяем на вещественное число
    strtod(value, &endptr);
    if (*endptr == '\0') return REAL;
    
    // Если не число, то строка
    return STRING;
}

static int parse_array(const char* value, ConfigVariable* var) {
    if (!value || !var) return -1;
    
    char* copy = strdup(value);
    if (!copy) return -1;
    
    // Удаляем квадратные скобки
    char* p = copy;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '[') p++;
    
    char* end = copy + strlen(copy) - 1;
    while (end > p && isspace((unsigned char)*end)) end--;
    if (*end == ']') *end = '\0';
    
    // Подсчитываем количество элементов
    int count = 1;
    for (char* c = p; *c; c++) {
        if (*c == ',') count++;
    }
    
    // Определяем тип элементов
    char* first_element = p;
    char* comma = strchr(p, ',');
    if (comma) *comma = '\0';
    
    trim_whitespace(first_element);
    ConfigVarType element_type = detect_value_type(first_element);
    
    if (comma) *comma = ',';
    
    if (element_type == UNDEFINED) {
        free(copy);
        return -1;
    }
    
    // Выделяем память под массив
    switch (element_type) {
        case INTEGER:
            var->data.integer = malloc(count * sizeof(int64_t));
            if (!var->data.integer) {
                free(copy);
                return -1;
            }
            break;
        case REAL:
            var->data.real = malloc(count * sizeof(double));
            if (!var->data.real) {
                free(copy);
                return -1;
            }
            break;
        case STRING:
            var->data.string = malloc(count * sizeof(char*));
            if (!var->data.string) {
                free(copy);
                return -1;
            }
            break;
        default:
            free(copy);
            return -1;
    }
    
    // Парсим элементы
    char* token = strtok(p, ",");
    int i = 0;
    while (token != NULL) {
        trim_whitespace(token);
        
        switch (element_type) {
            case INTEGER:
                var->data.integer[i] = strtoll(token, NULL, 10);
                break;
            case REAL:
                var->data.real[i] = strtod(token, NULL);
                break;
            case STRING:
                var->data.string[i] = strdup(token);
                if (!var->data.string[i]) {
                    // Освобождаем уже выделенную память
                    for (int j = 0; j < i; j++) free(var->data.string[j]);
                    free(var->data.string);
                    free(copy);
                    return -1;
                }
                break;
            default:
                free(copy);
                return -1;
        }
        
        i++;
        token = strtok(NULL, ",");
    }
    
    var->type = element_type;
    var->count = count;
    free(copy);
    return 0;
}

static int parse_single_value(const char* value, ConfigVariable* var) {
    if (!value || !var) return -1;
    
    ConfigVarType type = detect_value_type(value);
    if (type == UNDEFINED) return -1;
    
    switch (type) {
        case INTEGER:
            var->data.integer = malloc(sizeof(int64_t));
            if (!var->data.integer) return -1;
            *var->data.integer = strtoll(value, NULL, 10);
            break;
        case REAL:
            var->data.real = malloc(sizeof(double));
            if (!var->data.real) return -1;
            *var->data.real = strtod(value, NULL);
            break;
        case STRING:
            {
                char* str = strdup(value);
                if (!str) return -1;
                var->data.string = malloc(sizeof(char*));
                if (!var->data.string) {
                    free(str);
                    return -1;
                }
                *var->data.string = str;
                break;
            }
        default:
            return -1;
    }
    
    var->type = type;
    var->count = 1;
    return 0;
}

static void free_config_variable(ConfigVariable* var) {
    if (!var) return;
    
    if (var->count > 0) {
        switch (var->type) {
            case INTEGER:
                free(var->data.integer);
                break;
            case REAL:
                free(var->data.real);
                break;
            case STRING:
                for (int i = 0; i < var->count; i++) {
                    free(var->data.string[i]);
                }
                free(var->data.string);
                break;
            default:
                break;
        }
    }
    
    free(var->name);
    free(var->description);
}

static int add_variable_to_table(const ConfigVariable var) {
    if (!is_initialized) return -1;
    
    // Проверяем, существует ли уже переменная
    for (size_t i = 0; i < config_table_size; i++) {
        if (strcmp(config_table[i].name, var.name) == 0) {
            // Освобождаем старые данные
            free_config_variable(&config_table[i]);
            
            // Копируем новые данные
            config_table[i].type = var.type;
            config_table[i].count = var.count;
            config_table[i].data = var.data;
            
            // Копируем описание, если оно есть
            if (var.description) {
                config_table[i].description = strdup(var.description);
                if (!config_table[i].description) return -1;
            } else {
                config_table[i].description = NULL;
            }
            
            return 0;
        }
    }
    
    // Добавляем новую переменную
    ConfigVariable* new_table = realloc(config_table, (config_table_size + 1) * sizeof(ConfigVariable));
    if (!new_table) return -1;
    
    config_table = new_table;
    config_table[config_table_size].name = strdup(var.name);
    if (!config_table[config_table_size].name) return -1;
    
    if (var.description) {
        config_table[config_table_size].description = strdup(var.description);
        if (!config_table[config_table_size].description) {
            free(config_table[config_table_size].name);
            return -1;
        }
    } else {
        config_table[config_table_size].description = NULL;
    }
    
    config_table[config_table_size].type = var.type;
    config_table[config_table_size].count = var.count;
    config_table[config_table_size].data = var.data;
    
    config_table_size++;
    return 0;
}

// Основные функции
int create_config_table(void) {
    if (is_initialized) return -1;
    
    config_table = NULL;
    config_table_size = 0;
    is_initialized = true;
    return 0;
}

int destroy_config_table(void) {
    if (!is_initialized) return -1;
    
    for (size_t i = 0; i < config_table_size; i++) {
        free_config_variable(&config_table[i]);
    }
    
    free(config_table);
    config_table = NULL;
    config_table_size = 0;
    is_initialized = false;
    return 0;
}

int parse_config(const char* path) {
    if (!is_initialized || !path) return -1;
    
    FILE* file = fopen(path, "r");
    if (!file) {
        write_log(STDERR, LOG_ERROR, __FILE__, __LINE__, "File %s not found", path);
        return -1;
    }
    
    write_log(STDERR, LOG_INFO, __FILE__, __LINE__, "Start read config file %s", path);
    
    char line[MAX_LINE_LENGTH];
    int line_number = 0;
    
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        
        // Удаляем комментарии
        char* comment = strchr(line, '#');
        if (comment) *comment = '\0';
        
        // Пропускаем пустые строки
        trim_whitespace(line);
        if (line[0] == '\0') continue;
        
        // Разделяем ключ и значение
        char* separator = strchr(line, '=');
        if (!separator) {
            write_log(STDERR, LOG_ERROR, __FILE__, __LINE__, 
                      "Configuration file %s has syntax error at %d: %s", path, line_number, line);
            continue;
        }
        
        *separator = '\0';
        char* key = line;
        char* value = separator + 1;
        
        trim_whitespace(key);
        trim_whitespace(value);
        
        if (!is_valid_key(key)) {
            write_log(STDERR, LOG_ERROR, __FILE__, __LINE__, 
                      "Configuration file %s has syntax error at %d: invalid key", path, line_number);
            continue;
        }
        
        // Создаем переменную конфигурации
        ConfigVariable var;
        var.name = strdup(key);
        var.description = NULL;
        var.type = UNDEFINED;
        var.count = 0;
        
        // Проверяем, является ли значение массивом
        if (value[0] == '[' && value[strlen(value)-1] == ']') {
            if (parse_array(value, &var) != 0) {
                write_log(STDERR, LOG_ERROR, __FILE__, __LINE__, 
                          "Configuration file %s has syntax error at %d: invalid array", path, line_number);
                free(var.name);
                continue;
            }
        } else {
            if (parse_single_value(value, &var) != 0) {
                write_log(STDERR, LOG_ERROR, __FILE__, __LINE__, 
                          "Configuration file %s has syntax error at %d: invalid value", path, line_number);
                free(var.name);
                continue;
            }
        }
        
        // Добавляем переменную в таблицу
        if (add_variable_to_table(var) != 0) {
            write_log(STDERR, LOG_ERROR, __FILE__, __LINE__, 
                      "Failed to add variable %s to config table", key);
            free_config_variable(&var);
        }
    }
    
    fclose(file);
    write_log(STDERR, LOG_INFO, __FILE__, __LINE__, "Finish read config file %s", path);
    return 0;
}

int define_variable(const ConfigVariable variable) {
    if (!is_initialized || !variable.name) return -1;
    
    // Создаем копию переменной
    ConfigVariable var;
    var.name = strdup(variable.name);
    if (!var.name) return -1;
    
    if (variable.description) {
        var.description = strdup(variable.description);
        if (!var.description) {
            free(var.name);
            return -1;
        }
    } else {
        var.description = NULL;
    }
    
    var.type = variable.type;
    var.count = variable.count;
    
    // Копируем данные в зависимости от типа
    switch (variable.type) {
        case INTEGER:
            var.data.integer = malloc(var.count * sizeof(int64_t));
            if (!var.data.integer) {
                free(var.name);
                free(var.description);
                return -1;
            }
            memcpy(var.data.integer, variable.data.integer, var.count * sizeof(int64_t));
            break;
        case REAL:
            var.data.real = malloc(var.count * sizeof(double));
            if (!var.data.real) {
                free(var.name);
                free(var.description);
                return -1;
            }
            memcpy(var.data.real, variable.data.real, var.count * sizeof(double));
            break;
        case STRING:
            var.data.string = malloc(var.count * sizeof(char*));
            if (!var.data.string) {
                free(var.name);
                free(var.description);
                return -1;
            }
            for (int i = 0; i < var.count; i++) {
                var.data.string[i] = strdup(variable.data.string[i]);
                if (!var.data.string[i]) {
                    for (int j = 0; j < i; j++) free(var.data.string[j]);
                    free(var.data.string);
                    free(var.name);
                    free(var.description);
                    return -1;
                }
            }
            break;
        default:
            free(var.name);
            free(var.description);
            return -1;
    }
    
    // Добавляем в таблицу
    int result = add_variable_to_table(var);
    if (result != 0) {
        free_config_variable(&var);
    }
    
    return result;
}

ConfigVariable get_variable(const char* name) {
    ConfigVariable result = {0};
    
    if (!is_initialized || !name) {
        result.type = UNDEFINED;
        return result;
    }
    
    for (size_t i = 0; i < config_table_size; i++) {
        if (strcmp(config_table[i].name, name) == 0) {
            // Создаем копию переменной
            result.name = strdup(config_table[i].name);
            if (!result.name) {
                result.type = UNDEFINED;
                return result;
            }
            
            if (config_table[i].description) {
                result.description = strdup(config_table[i].description);
                if (!result.description) {
                    free(result.name);
                    result.type = UNDEFINED;
                    return result;
                }
            } else {
                result.description = NULL;
            }
            
            result.type = config_table[i].type;
            result.count = config_table[i].count;
            
            // Копируем данные в зависимости от типа
            switch (result.type) {
                case INTEGER:
                    result.data.integer = malloc(result.count * sizeof(int64_t));
                    if (!result.data.integer) {
                        free(result.name);
                        free(result.description);
                        result.type = UNDEFINED;
                        return result;
                    }
                    memcpy(result.data.integer, config_table[i].data.integer, 
                           result.count * sizeof(int64_t));
                    break;
                case REAL:
                    result.data.real = malloc(result.count * sizeof(double));
                    if (!result.data.real) {
                        free(result.name);
                        free(result.description);
                        result.type = UNDEFINED;
                        return result;
                    }
                    memcpy(result.data.real, config_table[i].data.real, 
                           result.count * sizeof(double));
                    break;
                case STRING:
                    result.data.string = malloc(result.count * sizeof(char*));
                    if (!result.data.string) {
                        free(result.name);
                        free(result.description);
                        result.type = UNDEFINED;
                        return result;
                    }
                    for (int j = 0; j < result.count; j++) {
                        result.data.string[j] = strdup(config_table[i].data.string[j]);
                        if (!result.data.string[j]) {
                            for (int k = 0; k < j; k++) free(result.data.string[k]);
                            free(result.data.string);
                            free(result.name);
                            free(result.description);
                            result.type = UNDEFINED;
                            return result;
                        }
                    }
                    break;
                default:
                    free(result.name);
                    free(result.description);
                    result.type = UNDEFINED;
                    return result;
            }
            
            return result;
        }
    }
    
    result.type = UNDEFINED;
    return result;
}

int set_variable(const ConfigVariable variable) {
    if (!is_initialized || !variable.name) return -1;
    
    // Проверяем, существует ли переменная
    for (size_t i = 0; i < config_table_size; i++) {
        if (strcmp(config_table[i].name, variable.name) == 0) {
            // Освобождаем старые данные
            free_config_variable(&config_table[i]);
            
            // Копируем новые данные
            return define_variable(variable);
        }
    }
    
    // Если переменная не существует, создаем новую
    return define_variable(variable);
}

bool does_variable_exist(const char* name) {
    if (!is_initialized || !name) return false;
    
    for (size_t i = 0; i < config_table_size; i++) {
        if (strcmp(config_table[i].name, name) == 0) {
            return true;
        }
    }
    
    return false;
}