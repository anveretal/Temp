#include "my_time.h"

time_t get_time(void) {
    time_t result;
    asm volatile (
        "mov $201, %%rax\n"  // Номер системного вызова time
        "xor %%rdi, %%rdi\n" // NULL в качестве аргумента
        "syscall\n"
        "mov %%rax, %0"
        : "=r" (result)
        : 
        : "%rax", "%rdi"
    );
    return result;
}