#include "my_time.h"
#include <time.h>

time_t get_time(void) {
    return time(NULL);
}