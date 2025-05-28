// #ifndef LOGGER_H
// #define LOGGER_H

// typedef enum {
//     LOG_DEBUG = 1,
//     LOG_INFO,
//     LOG_WARNING,
//     LOG_ERROR,
//     LOG_FATAL
// } LogLevel;

// typedef enum {
//     STDOUT = 1,
//     STDERR,
//     FILESTREAM
// } OutputStream;

// int init_logger(char *path, int file_size_limit);
// int fini_logger(void);

// int write_log(OutputStream output, LogLevel level,
//     char *filename, int line_number, char *format, ...);

// #define LOG_DEBUG_MSG(...)   write_log(STDOUT, LOG_DEBUG,   __FILE__, __LINE__, __VA_ARGS__)
// #define LOG_INFO_MSG(...)    write_log(STDOUT, LOG_INFO,    __FILE__, __LINE__, __VA_ARGS__)
// #define LOG_WARNING_MSG(...) write_log(STDERR, LOG_WARNING, __FILE__, __LINE__, __VA_ARGS__)
// #define LOG_ERROR_MSG(...)   write_log(STDERR, LOG_ERROR,   __FILE__, __LINE__, __VA_ARGS__)
// #define LOG_FATAL_MSG(...)   write_log(STDERR, LOG_FATAL,   __FILE__, __LINE__, __VA_ARGS__)

// #endif // LOGGER_H

#ifndef LOGGER_H
#define LOGGER_H

extern int logger_debug_mode;

typedef enum {
    LOG_DEBUG = 1,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL
} LogLevel;

typedef enum {
    STDOUT = 1,
    STDERR,
    FILESTREAM
} OutputStream;

int init_logger(char *path, int file_size_limit);
int fini_logger(void);
int is_logger_has_path(void);
int write_log(OutputStream stream, LogLevel level,
              const char *filename, int line_number,
              const char *format, ...)
    __attribute__((format(gnu_printf, 5, 6)));

// Макрос для удобного вызова логгера
#define LOG_SET(stream, level, format, ...) \
    write_log(stream, level, __FILE__, __LINE__, format, ##__VA_ARGS__)

#endif // LOGGER_H