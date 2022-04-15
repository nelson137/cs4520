#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../include/sys_prog.h"

// LOOK INTO OPEN, READ, WRITE, CLOSE, FSTAT/STAT, LSEEK
// GOOGLE FOR ENDIANESS HELP

bool bulk_read(const char *input_filename, void *dst, const size_t offset, const size_t dst_size)
{
    if (input_filename == NULL || dst == NULL || dst_size == 0)
        return false;

    int fd = open(input_filename, O_RDONLY);
    if (fd < 0)
        return false;

    bool ret = true;

    if (lseek(fd, offset, SEEK_SET) != offset) {
        ret = false;
        goto end;
    }

    if (read(fd, dst, dst_size) <= 0) {
        ret = false;
        goto end;
    }

end:
    close(fd);
    return ret;
}

bool bulk_write(const void *src, const char *output_filename, const size_t offset, const size_t src_size)
{
    if (src == NULL || output_filename == NULL || src_size == 0)
        return false;

    int fd = open(output_filename, O_WRONLY | O_TRUNC);
    if (fd < 0)
        return false;

    bool ret = true;

    if (lseek(fd, offset, SEEK_SET) != offset) {
        ret = false;
        goto end;
    }

    if (write(fd, src, src_size) < 0) {
        ret = false;
        goto end;
    }

end:
    close(fd);
    return ret;
}


bool file_stat(const char *query_filename, struct stat *metadata)
{
    if (query_filename == NULL || metadata == NULL)
        return false;

    int fd = open(query_filename, O_RDONLY);
    if (fd < 0)
        return false;

    bool ret = fstat(fd, metadata) == 0;

    close(fd);
    return ret;
}

bool endianess_converter(uint32_t *src_data, uint32_t *dst_data, const size_t src_count)
{
    #define DO_4X(_x) do { _x; _x; _x; _x; } while (0)

    if (src_data == NULL || dst_data == NULL || src_count == 0)
        return false;

    uint32_t *src_end = src_data + src_count;
    uint8_t *src_i, *dst_i;
    for (; src_data!=src_end; src_data++, dst_data++) {
        // Copy each byte of the current src element (left to right)
        // into the current dst element (right to left)
        src_i = (uint8_t*)src_data;
        dst_i = ((uint8_t*)dst_data) + 3;
        DO_4X(*(dst_i--) = *(src_i++));
    }

    return true;

    #undef DO_4X
}
