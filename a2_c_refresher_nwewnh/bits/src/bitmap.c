#include <string.h>

#include "../include/bitmap.h"

// data is an array of uint8_t and needs to be allocated in bitmap_create
//      and used in the remaining bitmap functions. You will use data for any bit operations and bit logic
// bit_count the number of requested bits, set in bitmap_create from n_bits
// byte_count the total number of bytes the data contains, set in bitmap_create


bitmap_t *bitmap_create(size_t n_bits)
{
    if (n_bits <= 0)
        return NULL;

    bitmap_t *map = malloc(sizeof(bitmap_t));
    if (map == NULL)
        return map;

    map->bit_count = n_bits;

    map->byte_count = n_bits / 8;
    if (n_bits % 8)
        map->byte_count++;

    map->data = malloc(sizeof(uint8_t) * map->byte_count);
    if (map->data) {
        memset(map->data, 0, sizeof(uint8_t) * map->byte_count);
    } else {
        free(map);
        map = NULL;
    }

    return map;
}

///
/// Get the byte that contains bit
/// \param bitmap The bitmap
/// \param bit The index of the bit needed
/// \return A pointer to the requested byte
///
static uint8_t *_get_byte(bitmap_t *const bitmap, const size_t bit) {
    return bitmap->data + (bit / 8);
}

///
/// The same as _get_byte except that it returns a const pointer
///
static const uint8_t *_get_const_byte(const bitmap_t *const bitmap, const size_t bit) {
    return _get_byte((bitmap_t *const)bitmap, bit);
}

bool bitmap_set(bitmap_t *const bitmap, const size_t bit)
{
    if (bitmap == NULL || bit < 0 || bit >= bitmap->bit_count)
        return false;

    const uint8_t mask = 0x1 << (bit % 8);
    uint8_t *byte = _get_byte(bitmap, bit);
    *byte |= mask;

    return true;
}

bool bitmap_reset(bitmap_t *const bitmap, const size_t bit)
{
    if (bitmap == NULL || bit >= bitmap->bit_count)
        return false;

    const uint8_t mask = ~(0x1 << (bit % 8));
    uint8_t *byte = _get_byte(bitmap, bit);
    *byte &= mask;

    return true;
}

// Does no error checking as there is no way to signal an error
bool bitmap_test(const bitmap_t *const bitmap, const size_t bit)
{
    const uint8_t mask = 0x1 << (bit % 8);
    const uint8_t *byte = _get_const_byte(bitmap, bit);
    return *byte & mask;
}

///
/// Find the first set bit
/// \param byte The byte to query
/// \param max_index The maximum number of bits to query (8 being all of them)
/// \return The index of the first enabled bit, otherwise UINT8_MAX if none found
///
static uint8_t _uint8_ffs_n(uint8_t byte, uint8_t max_index) {
    for (int i=0; i<8 && i<max_index; i++) {
        if (byte & 0x00000001)
            return i;
        byte >>= 1;
    }
    return UINT8_MAX;
}

size_t bitmap_ffs(const bitmap_t *const bitmap)
{
    if (bitmap == NULL)
        return SIZE_MAX;

    // Number of bits in the last byte
    uint8_t last_byte_count = bitmap->bit_count % 8;
    if (last_byte_count == 0) last_byte_count = 8;
    // Index of the last byte
    size_t last_byte_i = bitmap->byte_count - 1;

    uint8_t byte;
    size_t i, j;
    for (i=0; i<bitmap->byte_count; i++) {
        byte = *(bitmap->data + i);
        j = _uint8_ffs_n(byte, i == last_byte_i ? last_byte_count : 8);
        if (j != UINT8_MAX)
            return (i*8) + j;
    }

    return SIZE_MAX;
}

size_t bitmap_ffz(const bitmap_t *const bitmap)
{
    if (bitmap == NULL)
        return SIZE_MAX;

    // Number of bits in the last byte
    uint8_t last_byte_count = bitmap->bit_count % 8;
    if (last_byte_count == 0) last_byte_count = 8;
    // Index of the last byte
    size_t last_byte_i = bitmap->byte_count - 1;

    uint8_t byte;
    size_t i, j;
    for (i=0; i<bitmap->byte_count; i++) {
        // Find the first enabled bit of the inverted byte
        byte = ~*(bitmap->data + i);
        j = _uint8_ffs_n(byte, i == last_byte_i ? last_byte_count : 8);
        if (j != UINT8_MAX)
            return (i*8) + j;
    }

    return SIZE_MAX;
}

bool bitmap_destroy(bitmap_t *bitmap)
{
    if (bitmap == NULL)
        return false;
    if (bitmap->data) {
        free(bitmap->data);
        bitmap->data = NULL;
    }
    free(bitmap);
    return true;
}
