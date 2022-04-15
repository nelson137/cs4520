#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "../include/arrays.h"

// LOOK INTO MEMCPY, MEMCMP, FREAD, and FWRITE

bool array_copy(const void *src, void *dst, const size_t elem_size, const size_t elem_count)
{
    if (src == NULL || dst == NULL || elem_size == 0 || elem_count == 0)
        return false;

    memcpy(dst, src, elem_size*elem_count);

    return array_is_equal(src, dst, elem_size, elem_count);
}

bool array_is_equal(const void *data_one, void *data_two, const size_t elem_size, const size_t elem_count)
{
    if (data_one == NULL || data_two == NULL || elem_size == 0 || elem_count == 0)
        return false;

    return memcmp(data_one, data_two, elem_count*elem_size) == 0;
}

ssize_t array_locate(const void *data, const void *target, const size_t elem_size, const size_t elem_count)
{
    if (data == NULL || target == NULL || elem_size == 0 || elem_count == 0)
        return -1;

    char *ptr = (char*) data;
    for (int i=0; i<elem_count; i++, ptr+=elem_size)
        if (memcmp(ptr, target, elem_size) == 0)
            return i;

    return -1;
}

bool array_serialize(const void *src_data, const char *dst_file, const size_t elem_size, const size_t elem_count)
{
    if (src_data == NULL || dst_file == NULL || elem_size == 0 || elem_count == 0)
        return false;
    if (strcmp(dst_file, "\n") == 0)
        return false;

    FILE *f = fopen(dst_file, "w");
    if (f == NULL)
        return false;
    struct stat st;
    if (stat(dst_file, &st) < 0)
        return false;

    bool ret = elem_count == fwrite(src_data, elem_size, elem_count, f);
    fclose(f);
    return ret;
}

bool array_deserialize(const char *src_file, void *dst_data, const size_t elem_size, const size_t elem_count)
{
    if (src_file == NULL || dst_data == NULL || elem_size == 0 || elem_count == 0)
        return false;

    FILE *f = fopen(src_file, "r");
    if (f == NULL)
        return false;

    bool ret = elem_count == fread(dst_data, elem_size, elem_count, f);
    fclose(f);
    return ret;
}
