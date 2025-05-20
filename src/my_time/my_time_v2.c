#include "my_time.h"
#include <unistd.h>
#include <sys/syscall.h>

time_t get_time(void) {
    return syscall(SYS_time, NULL);
}