// #include "config.h"
// #include "logger.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <dirent.h>
// #include <errno.h>
// #include <ctype.h>
// #include <stdbool.h>
// #include <inttypes.h>

// #define INITIAL_TABLE_SIZE 16
// #define MAX_KEY_LENGTH 121
// #define MAX_LINE_LENGTH 1024
// #define MAX_ARRAY_ELEMENTS 128

// static ConfigVariable* config_table = NULL;
// static size_t table_size = 0;
// static size_t table_capacity = 0;
// static int is_config_initialized = 0;

// // Вспомогательные функции
// static void trim_whitespace(char* str) {
//     if (!str) return;
    
//     char* end;
//     // Trim leading space
//     while (isspace((unsigned char)*str)) str++;
    
//     if (*str == 0) { // All spaces?
//         *str = 0;
//         return;
//     }
    
//     // Trim trailing space
//     end = str + strlen(str) - 1;
//     while (end > str && isspace((unsigned char)*end)) end--;
    
//     // Write new null terminator
//     *(end + 1) = 0;
// }

// static int is_valid_key(const char* key) {
//     if (!key || strlen(key) > MAX_KEY_LENGTH) return 0;
    
//     for (const char* p = key; *p; p++) {
//         if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
//             return 0;
//         }
//     }
//     return 1;
// }

// static int expand_table() {
//     size_t new_capacity = table_capacity == 0 ? INITIAL_TABLE_SIZE : table_capacity * 2;
//     ConfigVariable* new_table = realloc(config_table, new_capacity * sizeof(ConfigVariable));
//     if (!new_table) {
//         LOG_ERROR_MSG("Failed to expand config table");
//         return 0;
//     }
    
//     config_table = new_table;
//     table_capacity = new_capacity;
//     return 1;
// }

// static void free_variable_data(ConfigVariable* var) {
//     if (!var) return;
    
//     switch(var->type) {
//         case INTEGER:
//             free(var->data.integer);
//             break;
//         case REAL:
//             free(var->data.real);
//             break;
//         case STRING:
//             if (var->count == 1) {
//                 free(var->data.string[0]);
//             } else {
//                 for (int i = 0; i < var->count; i++) {
//                     free(var->data.string[i]);
//                 }
//             }
//             free(var->data.string);
//             break;
//         default:
//             break;
//     }
// }

// static int parse_array(const char* value, ConfigVariable* var) {
//     char buffer[MAX_LINE_LENGTH];
//     strncpy(buffer, value, sizeof(buffer));
//     buffer[sizeof(buffer)-1] = '\0';
    
//     // Remove brackets
//     char* start = strchr(buffer, '[');
//     char* end = strrchr(buffer, ']');
//     if (!start || !end) {
//         LOG_ERROR_MSG("Invalid array syntax: %s", value);
//         return 0;
//     }
//     *end = '\0';
//     start++;
    
//     // Count elements
//     int count = 1;
//     for (char* p = start; *p; p++) {
//         if (*p == ',') count++;
//     }
    
//     // Parse elements
//     char* elements[MAX_ARRAY_ELEMENTS];
//     char* token = strtok(start, ",");
//     int i = 0;
    
//     while (token && i < MAX_ARRAY_ELEMENTS) {
//         trim_whitespace(token);
//         elements[i++] = strdup(token);
//         token = strtok(NULL, ",");
//     }
    
//     // Determine array type
//     ConfigVarType array_type = UNDEFINED;
//     for (int j = 0; j < i; j++) {
//         char* elem = elements[j];
        
//         // Check for string in quotes
//         if (elem[0] == '"' && elem[strlen(elem)-1] == '"') {
//             if (array_type == UNDEFINED) array_type = STRING;
//             if (array_type != STRING) {
//                 LOG_ERROR_MSG("Mixed array types not allowed");
//                 for (int k = 0; k < i; k++) free(elements[k]);
//                 return 0;
//             }
//             continue;
//         }
        
//         // Check for numbers
//         char* endptr;
//         strtod(elem, &endptr);
//         if (*endptr == '\0') {
//             if (array_type == UNDEFINED) array_type = REAL;
//             if (array_type != REAL && array_type != INTEGER) {
//                 LOG_ERROR_MSG("Mixed array types not allowed");
//                 for (int k = 0; k < i; k++) free(elements[k]);
//                 return 0;
//             }
//             continue;
//         }
        
//         // Default to string
//         if (array_type == UNDEFINED) array_type = STRING;
//         if (array_type != STRING) {
//             LOG_ERROR_MSG("Mixed array types not allowed");
//             for (int k = 0; k < i; k++) free(elements[k]);
//             return 0;
//         }
//     }
    
//     // Store array
//     var->type = array_type;
//     var->count = i;
    
//     switch(array_type) {
//         case STRING: {
//             var->data.string = malloc(i * sizeof(char*));
//             for (int j = 0; j < i; j++) {
//                 // Remove quotes if present
//                 if (elements[j][0] == '"' && elements[j][strlen(elements[j])-1] == '"') {
//                     elements[j][strlen(elements[j])-1] = '\0';
//                     var->data.string[j] = strdup(elements[j]+1);
//                 } else {
//                     var->data.string[j] = strdup(elements[j]);
//                 }
//                 free(elements[j]);
//             }
//             break;
//         }
//         case REAL: {
//             var->data.real = malloc(i * sizeof(double));
//             for (int j = 0; j < i; j++) {
//                 var->data.real[j] = atof(elements[j]);
//                 free(elements[j]);
//             }
//             break;
//         }
//         case INTEGER: {
//             var->data.integer = malloc(i * sizeof(int64_t));
//             for (int j = 0; j < i; j++) {
//                 var->data.integer[j] = atoll(elements[j]);
//                 free(elements[j]);
//             }
//             break;
//         }
//         default:
//             break;
//     }
    
//     return 1;
// }

// // Основные функции
// int create_config_table(void) {
//     if (is_config_initialized) {
//         LOG_ERROR_MSG("Config already initialized");
//         return 1;
//     }
    
//     config_table = malloc(INITIAL_TABLE_SIZE * sizeof(ConfigVariable));
//     if (!config_table) {
//         LOG_ERROR_MSG("Memory allocation failed");
//         return 1;
//     }
    
//     table_capacity = INITIAL_TABLE_SIZE;
//     table_size = 0;
//     is_config_initialized = 1;
//     LOG_INFO_MSG("Config table created");
//     return 0;
// }

// int destroy_config_table(void) {
//     if (!is_config_initialized) {
//         LOG_ERROR_MSG("Config not initialized");
//         return 1;
//     }
    
//     for (size_t i = 0; i < table_size; i++) {
//         free(config_table[i].name);
//         free(config_table[i].description);
//         free_variable_data(&config_table[i]);
//     }
    
//     free(config_table);
//     config_table = NULL;
//     table_size = 0;
//     table_capacity = 0;
//     is_config_initialized = 0;
//     LOG_INFO_MSG("Config table destroyed");
//     return 0;
// }

// int parse_config(const char* path) {
//     if (!is_config_initialized) {
//         LOG_ERROR_MSG("Config not initialized");
//         return 1;
//     }
    
//     if (!path) {
//         LOG_ERROR_MSG("Null path provided");
//         return 1;
//     }
    
//     FILE* file = fopen(path, "r");
//     if (!file) {
//         LOG_ERROR_MSG("File %s not found: %s", path, strerror(errno));
//         return 1;
//     }
    
//     LOG_INFO_MSG("Start reading config file %s", path);
    
//     char line[MAX_LINE_LENGTH];
//     int line_num = 0;
    
//     while (fgets(line, sizeof(line), file)) {
//         line_num++;
//         char* comment = strchr(line, '#');
//         if (comment) *comment = '\0';
        
//         trim_whitespace(line);
//         if (strlen(line) == 0) continue;
        
//         // Handle include_dir directive
//         if (strncmp(line, "include_dir", 11) == 0) {
//             char* dir_path = line + 11;
//             trim_whitespace(dir_path);
            
//             DIR* dir = opendir(dir_path);
//             if (!dir) {
//                 LOG_ERROR_MSG("Cannot open directory %s: %s", dir_path, strerror(errno));
//                 continue;
//             }
            
//             struct dirent* entry;
//             while ((entry = readdir(dir)) != NULL) {
//                 if (entry->d_type == DT_REG) {
//                     char filepath[MAX_LINE_LENGTH];
//                     snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);
                    
//                     // Skip current config file to avoid loops
//                     if (strcmp(filepath, path) == 0) continue;
                    
//                     if (parse_config(filepath) != 0) {
//                         LOG_WARNING_MSG("Failed to parse included config %s", filepath);
//                     }
//                 }
//             }
//             closedir(dir);
//             continue;
//         }
        
//         // Parse key-value pair
//         char* equal = strchr(line, '=');
//         if (!equal) {
//             LOG_ERROR_MSG("Syntax error at line %d: missing '=' in '%s'", line_num, line);
//             continue;
//         }
        
//         *equal = '\0';
//         char* key = line;
//         char* value = equal + 1;
//         trim_whitespace(key);
//         trim_whitespace(value);
        
//         if (!is_valid_key(key)) {
//             LOG_ERROR_MSG("Invalid key at line %d: '%s'", line_num, key);
//             continue;
//         }
        
//         if (strlen(value) == 0) {
//             LOG_ERROR_MSG("Empty value at line %d", line_num);
//             continue;
//         }
        
//         ConfigVariable var = {0};
//         var.name = strdup(key);
        
//         // Check for array
//         if (strchr(value, '[') && strchr(value, ']')) {
//             if (!parse_array(value, &var)) {
//                 free(var.name);
//                 continue;
//             }
//         } 
//         // Check for quoted string
//         else if (value[0] == '"' && value[strlen(value)-1] == '"') {
//             var.type = STRING;
//             var.count = 1;
//             var.data.string = malloc(sizeof(char*));
//             value[strlen(value)-1] = '\0';
//             var.data.string[0] = strdup(value+1);
//         }
//         // Check for numbers
//         else {
//             char* endptr;
//             double dval = strtod(value, &endptr);
            
//             if (*endptr == '\0') {
//                 // It's a number
//                 if (strchr(value, '.')) {
//                     var.type = REAL;
//                     var.count = 1;
//                     var.data.real = malloc(sizeof(double));
//                     *var.data.real = dval;
//                 } else {
//                     var.type = INTEGER;
//                     var.count = 1;
//                     var.data.integer = malloc(sizeof(int64_t));
//                     *var.data.integer = (int64_t)dval;
//                 }
//             } else {
//                 // Default to string
//                 var.type = STRING;
//                 var.count = 1;
//                 var.data.string = malloc(sizeof(char*));
//                 var.data.string[0] = strdup(value);
//             }
//         }
        
//         if (set_variable(var) != 0) {
//             LOG_WARNING_MSG("Failed to set variable %s", key);
//             free_variable_data(&var);
//             free(var.name);
//         }
//     }
    
//     fclose(file);
//     LOG_INFO_MSG("Finished reading config file %s", path);
//     return 0;
// }

// int define_variable(const ConfigVariable variable) {
//     if (!is_config_initialized) {
//         LOG_ERROR_MSG("Config not initialized");
//         return 1;
//     }
    
//     if (!is_valid_key(variable.name)) {
//         LOG_ERROR_MSG("Invalid variable name: %s", variable.name);
//         return 1;
//     }
    
//     if (table_size >= table_capacity && !expand_table()) {
//         return 1;
//     }
    
//     // Make a deep copy
//     ConfigVariable new_var = {0};
//     new_var.name = strdup(variable.name);
//     new_var.description = variable.description ? strdup(variable.description) : NULL;
//     new_var.type = variable.type;
//     new_var.count = variable.count;
    
//     switch(variable.type) {
//         case INTEGER:
//             new_var.data.integer = malloc(variable.count * sizeof(int64_t));
//             memcpy(new_var.data.integer, variable.data.integer, variable.count * sizeof(int64_t));
//             break;
//         case REAL:
//             new_var.data.real = malloc(variable.count * sizeof(double));
//             memcpy(new_var.data.real, variable.data.real, variable.count * sizeof(double));
//             break;
//         case STRING:
//             new_var.data.string = malloc(variable.count * sizeof(char*));
//             for (int i = 0; i < variable.count; i++) {
//                 new_var.data.string[i] = strdup(variable.data.string[i]);
//             }
//             break;
//         default:
//             break;
//     }
    
//     config_table[table_size++] = new_var;
//     LOG_DEBUG_MSG("Defined variable %s", new_var.name);
//     return 0;
// }

// ConfigVariable get_variable(const char* name) {
//     ConfigVariable result = {0};
//     result.type = UNDEFINED;
    
//     if (!is_config_initialized || !name) {
//         LOG_ERROR_MSG("Invalid arguments to get_variable");
//         return result;
//     }
    
//     for (size_t i = 0; i < table_size; i++) {
//         if (strcmp(config_table[i].name, name) == 0) {
//             // Return a deep copy
//             ConfigVariable var = config_table[i];
//             ConfigVariable copy = {0};
//             copy.name = strdup(var.name);
//             copy.description = var.description ? strdup(var.description) : NULL;
//             copy.type = var.type;
//             copy.count = var.count;
            
//             switch(var.type) {
//                 case INTEGER:
//                     copy.data.integer = malloc(var.count * sizeof(int64_t));
//                     memcpy(copy.data.integer, var.data.integer, var.count * sizeof(int64_t));
//                     break;
//                 case REAL:
//                     copy.data.real = malloc(var.count * sizeof(double));
//                     memcpy(copy.data.real, var.data.real, var.count * sizeof(double));
//                     break;
//                 case STRING:
//                     copy.data.string = malloc(var.count * sizeof(char*));
//                     for (int i = 0; i < var.count; i++) {
//                         copy.data.string[i] = strdup(var.data.string[i]);
//                     }
//                     break;
//                 default:
//                     break;
//             }
            
//             return copy;
//         }
//     }
    
//     LOG_DEBUG_MSG("Variable %s not found", name);
//     return result;
// }

// int set_variable(const ConfigVariable variable) {
//     if (!is_config_initialized) {
//         LOG_ERROR_MSG("Config not initialized");
//         return 1;
//     }
    
//     if (!is_valid_key(variable.name)) {
//         LOG_ERROR_MSG("Invalid variable name: %s", variable.name);
//         return 1;
//     }
    
//     for (size_t i = 0; i < table_size; i++) {
//         if (strcmp(config_table[i].name, variable.name) == 0) {
//             // Free old data
//             free(config_table[i].description);
//             free_variable_data(&config_table[i]);
            
//             // Copy new data
//             config_table[i].description = variable.description ? strdup(variable.description) : NULL;
//             config_table[i].type = variable.type;
//             config_table[i].count = variable.count;
            
//             switch(variable.type) {
//                 case INTEGER:
//                     config_table[i].data.integer = malloc(variable.count * sizeof(int64_t));
//                     memcpy(config_table[i].data.integer, variable.data.integer, 
//                           variable.count * sizeof(int64_t));
//                     break;
//                 case REAL:
//                     config_table[i].data.real = malloc(variable.count * sizeof(double));
//                     memcpy(config_table[i].data.real, variable.data.real, 
//                           variable.count * sizeof(double));
//                     break;
//                 case STRING:
//                     config_table[i].data.string = malloc(variable.count * sizeof(char*));
//                     for (int j = 0; j < variable.count; j++) {
//                         config_table[i].data.string[j] = strdup(variable.data.string[j]);
//                     }
//                     break;
//                 default:
//                     break;
//             }
            
//             LOG_DEBUG_MSG("Updated variable %s", variable.name);
//             return 0;
//         }
//     }
    
//     // Variable doesn't exist, create new
//     return define_variable(variable);
// }

// bool does_variable_exist(const char* name) {
//     if (!is_config_initialized || !name) {
//         LOG_DEBUG_MSG("Invalid arguments to does_variable_exist");
//         return false;
//     }
    
//     for (size_t i = 0; i < table_size; i++) {
//         if (strcmp(config_table[i].name, name) == 0) {
//             return true;
//         }
//     }
    
//     return false;
// }


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "config.h"
#include "logger.h"

typedef struct {
    ConfigVariable* variables;
    size_t count;
    size_t capacity;
    int is_initialized;
} ConfigSystem;

static ConfigSystem config = {NULL, 0, 0, 0};

static void trim_whitespace(char* str) {
    char *end;
    
    // Trim leading space
    while(isspace((unsigned char)*str)) str++;
    
    if(*str == 0) return;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    
    // Write new null terminator
    *(end+1) = 0;
}

static ConfigVarType detect_value_type(const char* value) {
    char* endptr;
    
    // Проверка на целое число
    strtoll(value, &endptr, 10);
    if(*endptr == '\0') return INTEGER;
    
    // Проверка на вещественное число
    strtod(value, &endptr);
    if(*endptr == '\0') return REAL;
    
    // Проверка на строку (в кавычках или без)
    return STRING;
}

static int process_config_line(char* line, int line_num, const char* file_path) {
    // Удаляем комментарии
    char* comment = strchr(line, '#');
    if(comment) *comment = '\0';
    
    trim_whitespace(line);
    if(strlen(line) == 0) return 0;
    
    // Проверяем директиву include_dir
    if(strncmp(line, "include_dir", 11) == 0) {
        char* dir_path = line + 11;
        trim_whitespace(dir_path);
        
        DIR *dir;
        struct dirent *ent;
        
        if((dir = opendir(dir_path)) != NULL) {
            while((ent = readdir(dir)) != NULL) {
                struct stat path_stat;
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);
                
                stat(full_path, &path_stat);
                if(S_ISREG(path_stat.st_mode)) {
                    // Игнорируем текущий файл конфигурации
                    if(strcmp(full_path, file_path) == 0) continue;
                    
                    LOG_SET(STDERR, LOG_INFO, "Including config file: %s", full_path);
                    if(parse_config(full_path)) {
                        closedir(dir);
                        return 1;
                    }
                }
            }
            closedir(dir);
            return 0;
        } else {
            LOG_SET(STDERR, LOG_ERROR, "Could not open directory: %s", dir_path);
            return 1;
        }
    }
    
    // Разбираем строку на ключ и значение
    char* separator = strchr(line, '=');
    if(!separator) {
        LOG_SET(STDERR, LOG_ERROR, "Syntax error in config file at line %d: missing '='", line_num);
        return 1;
    }
    
    *separator = '\0';
    char* key = line;
    char* value = separator + 1;
    
    trim_whitespace(key);
    trim_whitespace(value);
    
    // Проверяем валидность ключа
    if(strlen(key) > 121) {
        LOG_SET(STDERR, LOG_ERROR, "Config key too long at line %d", line_num);
        return 1;
    }
    
    for(char* p = key; *p; p++) {
        if(!isalnum(*p) && *p != '_' && *p != '-') {
            LOG_SET(STDERR, LOG_ERROR, "Invalid character in key at line %d", line_num);
            return 1;
        }
    }
    
    // Создаем переменную конфигурации
    ConfigVariable var;
    var.name = strdup(key);
    var.description = NULL;
    var.count = 1;
    var.type = detect_value_type(value);
    
    // Выделяем память под значение
    switch(var.type) {
        case INTEGER:
            var.data.integer = malloc(sizeof(int64_t));
            *var.data.integer = strtoll(value, NULL, 10);
            break;
        case REAL:
            var.data.real = malloc(sizeof(double));
            *var.data.real = strtod(value, NULL);
            break;
        case STRING:
            var.data.string = malloc(sizeof(char*));
            *var.data.string = strdup(value);
            break;
        default:
            var.type = UNDEFINED;
            break;
    }
    
    // Устанавливаем переменную
    if(set_variable(var)) {
        LOG_SET(STDERR, LOG_ERROR, "Failed to set variable at line %d", line_num);
        return 1;
    }
    
    return 0;
}

int create_config_table(void) {
    if(config.is_initialized) {
        return 1;
    }
    
    config.count = 0;
    config.capacity = 10;
    config.variables = malloc(config.capacity * sizeof(ConfigVariable));
    if(!config.variables) {
        return 1;
    }
    config.is_initialized = 1;
    
    return 0;
}

int destroy_config_table(void) {
    if(!config.is_initialized) {
        return 1;
    }
    
    for(size_t i = 0; i < config.count; i++) {
        free(config.variables[i].name);
        if(config.variables[i].description) {
            free(config.variables[i].description);
        }
        
        switch(config.variables[i].type) {
            case INTEGER:
                free(config.variables[i].data.integer);
                break;
            case REAL:
                free(config.variables[i].data.real);
                break;
            case STRING:
                free(*config.variables[i].data.string);
                free(config.variables[i].data.string);
                break;
            default:
                break;
        }
    }
    
    free(config.variables);
    config.variables = NULL;
    config.count = 0;
    config.capacity = 0;
    config.is_initialized = 0;
    
    return 0;
}

int parse_config(const char* path) {
    CONFIG_CHECK();
    
    if(!path) {
        LOG_SET(STDERR, LOG_ERROR, "Config file path is NULL");
        return 1;
    }
    
    FILE* file = fopen(path, "r");
    if(!file) {
        LOG_SET(STDERR, LOG_ERROR, "File %s not found", path);
        return 1;
    }
    
    if (is_logger_has_path()) {
        LOG_SET(FILESTREAM, LOG_INFO, "Start read config file %s", path);
    }
    else {
        LOG_SET(STDOUT, LOG_INFO, "Start read config file %s", path);
    }
    
    char line[1024];
    int line_num = 0;
    int error = 0;
    
    while(fgets(line, sizeof(line), file)) {
        line_num++;
        if(process_config_line(line, line_num, path)) {
            error = 1;
            break;
        }
    }
    
    fclose(file);
    
    if(!error) {
        if (is_logger_has_path()) {
            LOG_SET(FILESTREAM, LOG_INFO, "Finish read config file %s", path);
        }
        else {
            LOG_SET(STDERR, LOG_INFO, "Finish read config file %s", path);
        }
    }
    
    return error;
}

int define_variable(const ConfigVariable variable) {
    CONFIG_CHECK();
    
    if(!variable.name || strlen(variable.name) == 0) {
        return 1;
    }
    
    // Проверяем, существует ли уже переменная
    for(size_t i = 0; i < config.count; i++) {
        if(strcmp(config.variables[i].name, variable.name) == 0) {
            return 1;
        }
    }
    
    // Увеличиваем массив при необходимости
    if(config.count >= config.capacity) {
        size_t new_capacity = config.capacity * 2;
        ConfigVariable* new_vars = realloc(config.variables, new_capacity * sizeof(ConfigVariable));
        if(!new_vars) {
            return 1;
        }
        config.variables = new_vars;
        config.capacity = new_capacity;
    }
    
    // Копируем переменную
    config.variables[config.count] = variable;
    config.count++;
    
    return 0;
}

ConfigVariable get_variable(const char* name) {
    ConfigVariable result = {NULL, NULL, {NULL}, UNDEFINED, 0};
    
    if(!config.is_initialized || !name) {
        return result;
    }
    
    for(size_t i = 0; i < config.count; i++) {
        if(strcmp(config.variables[i].name, name) == 0) {
            return config.variables[i];
        }
    }
    
    return result;
}

int set_variable(const ConfigVariable variable) {
    CONFIG_CHECK();
    
    if(!variable.name || strlen(variable.name) == 0) {
        return 1;
    }
    
    // Ищем существующую переменную
    for(size_t i = 0; i < config.count; i++) {
        if(strcmp(config.variables[i].name, variable.name) == 0) {
            // Освобождаем старые данные
            switch(config.variables[i].type) {
                case INTEGER:
                    free(config.variables[i].data.integer);
                    break;
                case REAL:
                    free(config.variables[i].data.real);
                    break;
                case STRING:
                    free(*config.variables[i].data.string);
                    free(config.variables[i].data.string);
                    break;
                default:
                    break;
            }
            
            // Копируем новые данные
            config.variables[i] = variable;
            return 0;
        }
    }
    
    // Если переменная не найдена, создаем новую
    return define_variable(variable);
}

bool does_variable_exist(const char* name) {
    if(!config.is_initialized || !name) {
        return false;
    }
    
    for(size_t i = 0; i < config.count; i++) {
        if(strcmp(config.variables[i].name, name) == 0) {
            return true;
        }
    }
    
    return false;
}