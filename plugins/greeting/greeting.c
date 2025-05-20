#include <stdio.h>
#include "master.h"

static Hook previous_hook = NULL;

int is_greeting_initialized = 0;

void greeting(void) {
    if (previous_hook) {
        previous_hook();
    }
    printf("Hello, world!\n");
}

int init(void) {
    if (is_greeting_initialized) {
	    return 1;
    }
    is_greeting_initialized = 1;

    previous_hook = executor_start_hook;
    executor_start_hook = greeting;
    printf("greeting initialized\n");
    return 0;
}

int fini(void) {
    if (!is_greeting_initialized) {
	    return 1;
    }
    is_greeting_initialized = 0;

    executor_start_hook = previous_hook;
    printf("greeting finished\n");
    return 0;
}

const char *name(void) {
    return "greeting";
}