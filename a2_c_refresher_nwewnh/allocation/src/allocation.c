#define _POSIX_C_SOURCE 200809L
#include "../include/allocation.h"
#include <stdlib.h>
#include <stdio.h>


void* allocate_array(size_t member_size, size_t nmember,bool clear)
{
    if (member_size <= 0 || nmember <= 0)
        return NULL;
    if (clear)
        return calloc(nmember, member_size);
    else
        return malloc(nmember * member_size);
}

void* reallocate_array(void* ptr, size_t size)
{
    if (ptr == NULL || size == 0)
        return NULL;
    return realloc(ptr, size);
}

void deallocate_array(void** ptr)
{
    if (ptr != NULL && *ptr != NULL) {
        free(*ptr);
        *ptr = NULL;
    }
}

char* read_line_to_buffer(char* filename)
{
    if (filename == NULL)
        return NULL;

    FILE *f = fopen(filename, "r");
    if (f == NULL)
        return NULL;

    char *buff = NULL;
    size_t size = 0;
    if (getline(&buff, &size, f) < 0) {
        fclose(f);
        return NULL;
    }

    return buff;
}
