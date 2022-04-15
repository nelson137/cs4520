#include "../include/debug.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// protected function, that only this .c can use
int comparator_func(const void *a, const void *b) {
    return *(uint8_t*)a - *(uint8_t*)b;
}

bool terrible_sort(uint16_t *data_array, const size_t value_count) {
    if (data_array == NULL || value_count == 0) {
        return false;
    }

    uint16_t *sorting_array = malloc(value_count * sizeof(uint16_t));
    if (sorting_array == NULL) {
        return false;
    }
    memcpy(sorting_array, data_array, sizeof(uint16_t) * value_count);

    qsort(sorting_array, value_count, sizeof(uint16_t), comparator_func);

    bool sorted = true;
    for (size_t i = 0; i < value_count - 1; ++i) {
        if ((sorted = sorting_array[i] <= sorting_array[i+1]) == false) {
            break;
        }
    }

    if (sorted) {
        memcpy(data_array, sorting_array, sizeof(uint16_t) * value_count);
    }
    free(sorting_array);
    return sorted;
}
