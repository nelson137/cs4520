#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "../include/error_handling.h"

int create_blank_records(Record_t **records, const size_t num_records)
{
    if (records == NULL || *records != NULL || num_records == 0) {
        return -1;
    }
    if ((*records = malloc(sizeof(Record_t) * num_records)) == NULL) {
        return -2;
    }
    memset(*records, 0, sizeof(Record_t) * num_records);
    return 0;
}

int read_records(const char *input_filename, Record_t *records, const size_t num_records)
{
    if (input_filename == NULL || records == NULL || num_records == 0) {
        return -1;
    }

    int fd = open(input_filename, O_RDONLY);
    if (fd < 0) {
        return -2;
    }

    ssize_t data_read = 0;
    for (size_t i = 0; i < num_records; ++i) {
        data_read = read(fd, &records[i], sizeof(Record_t));
        if (data_read != sizeof(Record_t)) {
            close(fd);
            return -3;
        }
    }

    close(fd);
    return 0;
}

int create_record(Record_t **new_record, const char* name, int age)
{
    if (new_record == NULL)
        return -1;
    if (name == NULL || strcmp(name, "\n") == 0 || strnlen(name, 51) > 50)
        return -1;
    if (age < 1 || age > 200)
        return -1;

    Record_t *r = malloc(sizeof(Record_t));
    if (r == NULL) {
        return -2;
    }

    memcpy(r->name, name, sizeof(char) * strlen(name));
    r->name[MAX_NAME_LEN-1] = '\0';
    r->age = age;

    *new_record = r;
    return 0;
}
