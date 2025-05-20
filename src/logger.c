// #include "logger.h"
// #include "my_time.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <time.h>
// #include <unistd.h>
// #include <stdarg.h>
// #include <errno.h>

// typedef struct {
//     FILE *log_file;
//     int file_size;            // Текущий размер файла (в байтах)
//     int file_size_limit;      // Максимальный размер файла (в байтах), 0 — нет ограничения
//     int is_initialized;
// } Logger;

// static Logger logger = {
//     NULL, 0, 0, 0
// };

// int init_logger(char *path, int file_size_limit) {
//     if (logger.is_initialized) {
//         return 1;  // Логгер уже инициализирован
//     }

//     if (path != NULL) {
//         logger.log_file = fopen(path, "a");
//         if (!logger.log_file) {
//             fprintf(stderr, "Have no permissions for file %s\n", path);
//             return 1;
//         }

//         fseek(logger.log_file, 0, SEEK_END);
//         logger.file_size = ftell(logger.log_file);
//         logger.file_size_limit = file_size_limit * 1024; // KB to bytes
//     } else {
//         logger.file_size_limit = 0;
//         logger.file_size = 0;
//         logger.log_file = NULL;
//     }

//     logger.is_initialized = 1;
//     return 0;
// }

// int fini_logger(void) {
//     if (!logger.is_initialized) {
//         return 1;
//     }

//     if (logger.log_file) {
//         fclose(logger.log_file);
//         logger.log_file = NULL;
//     }

//     logger.is_initialized = 0;
//     return 0;
// }

// int write_log(OutputStream stream, LogLevel level,
//               char *filename, int line_number, char *format, ...) {

//     if (!logger.is_initialized) {
//         return 1;
//     }

//     time_t now = get_time();
//     struct tm *tm_info = gmtime(&now);
//     char time_buf[64];
//     strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S(UTC)", tm_info);

//     const char *level_str;
//     switch (level) {
//         case LOG_DEBUG:   level_str = "DEBUG";   break;
//         case LOG_INFO:    level_str = "INFO";    break;
//         case LOG_WARNING: level_str = "WARNING"; break;
//         case LOG_ERROR:   level_str = "ERROR";   break;
//         case LOG_FATAL:   level_str = "FATAL";   break;
//         default:          level_str = "UNKNOWN";
//     }

//     va_list args;
//     va_start(args, format);
//     char message[1024];
//     vsnprintf(message, sizeof(message), format, args);
//     va_end(args);

//     char log_line[2048];
//     int len = snprintf(log_line, sizeof(log_line),
//                        "%s %s:%d [%d] | %s: %s\n",
//                        time_buf, filename, line_number, getpid(), level_str, message);

//     int written = 0;
//     if (stream == FILESTREAM && logger.log_file) {
//         if ((logger.file_size_limit > 0 &&
//              logger.file_size + len > logger.file_size_limit)) {
//             // Файл переполнен — очищаем его
//             ftruncate(fileno(logger.log_file), 0);
//             fseek(logger.log_file, 0, SEEK_SET);
//             logger.file_size = 0;
//         }

//         written = fprintf(logger.log_file, "%s", log_line);
//         fflush(logger.log_file);
//         logger.file_size += len;
//     } else {
//         FILE *output = (stream == STDOUT) ? stdout : stderr;
//         written = fprintf(output, "%s", log_line);
//         fflush(output);
//     }

//     if (written < 0) {
//         // Ошибка записи, выводим в stderr
//         fprintf(stderr, "Failed to write log entry: %s\n", strerror(errno));
//         return 1;
//     }

//     return 0;
// }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/types.h>
#include "logger.h"
#include "my_time.h"

typedef struct {
    FILE *log_file;
    char *log_path;
    int file_size_limit; // в KB
    int is_initialized;
} Logger;

static Logger logger = {NULL, NULL, 0, 0};

static const char* level_to_string(LogLevel level) {
    switch(level) {
        case LOG_DEBUG:   return "DEBUG";
        case LOG_INFO:    return "INFO";
        case LOG_WARNING: return "WARNING";
        case LOG_ERROR:   return "ERROR";
        case LOG_FATAL:   return "FATAL";
        default:          return "UNKNOWN";
    }
}

static void get_utc_time(char *time_buf, size_t buf_size) {
    time_t raw_time;
    struct tm *time_info;
    
    time(&raw_time);
    time_info = gmtime(&raw_time);
    
    strftime(time_buf, buf_size, "%Y-%m-%dT%H:%M:%S(UTC)", time_info);
}

static int check_file_size() {
    if (!logger.log_file || logger.file_size_limit <= 0) {
        return 0;
    }

    long current_size = ftell(logger.log_file);
    if (current_size == -1) {
        return -1;
    }

    // Проверяем, не превысили ли лимит (переводим KB в байты)
    if (current_size > logger.file_size_limit * 1024) {
        // Переоткрываем файл, очищая его
        fclose(logger.log_file);
        logger.log_file = fopen(logger.log_path, "w");
        if (!logger.log_file) {
            return -1;
        }
    }
    return 0;
}

int init_logger(char *path, int file_size_limit) {
    if (logger.is_initialized) {
        return 1;
    }

    logger.file_size_limit = file_size_limit;
    logger.is_initialized = 1;

    if (path) {
        // Проверяем права на запись
        if (access(path, W_OK) == -1) {
            // Пытаемся создать файл, если его нет
            FILE *test = fopen(path, "a");
            if (!test) {
                fprintf(stderr, "Have no permissions for file %s\n", path);
                return 1;
            }
            fclose(test);
        }

        logger.log_path = strdup(path);
        logger.log_file = fopen(path, "a");
        if (!logger.log_file) {
            free(logger.log_path);
            logger.log_path = NULL;
            return 1;
        }
    }

    return 0;
}

int fini_logger(void) {
    if (!logger.is_initialized) {
        return 1;
    }

    if (logger.log_file) {
        fclose(logger.log_file);
        logger.log_file = NULL;
    }

    if (logger.log_path) {
        free(logger.log_path);
        logger.log_path = NULL;
    }

    logger.is_initialized = 0;
    return 0;
}

int write_log(OutputStream stream, LogLevel level, const char *filename, int line_number, const char *format, ...) {
    if (!logger.is_initialized) {
        return 1;
    }

    FILE *output = NULL;

    if (debug_mode) {
        output = stdout;
    }
    else {
        switch(stream) {
            case STDOUT:    output = stdout; break;
            case STDERR:    output = stderr; break;
            case FILESTREAM: 
                if (!logger.log_file) {
                    output = stderr;
                } else {
                    if (check_file_size() == -1) {
                        output = stderr;
                    } else {
                        output = logger.log_file;
                    }
                }
                break;
            default:    output = stderr;
        }
    }

    // Получаем базовое имя файла (без пути)
    const char *base_filename = strrchr(filename, '/');
    base_filename = base_filename ? base_filename + 1 : filename;

    // Формируем временную метку
    char time_buf[64];
    get_utc_time(time_buf, sizeof(time_buf));

    // Выводим заголовок сообщения
    fprintf(output, "%s %s:%d [%d] | %s: ", 
            time_buf, base_filename, line_number, getpid(), level_to_string(level));

    // Выводим само сообщение
    va_list args;
    va_start(args, format);
    int result = vfprintf(output, format, args);
    va_end(args);

    // Добавляем перевод строки
    fprintf(output, "\n");
    fflush(output);

    return (result < 0) ? 1 : 0;
}