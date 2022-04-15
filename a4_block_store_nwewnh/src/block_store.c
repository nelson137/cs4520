#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "bitmap.h"
#include "block_store.h"

typedef uint8_t block_t[BLOCK_SIZE_BYTES];

struct block_store
{
    bitmap_t *fbm;
    block_t *blocks;
};

block_store_t *block_store_create()
{
    block_store_t *bs = malloc(sizeof(block_store_t));
    if (bs == NULL)
        return NULL;

    bs->blocks = malloc(sizeof(block_t) * BLOCK_STORE_NUM_BLOCKS);
    // Suppress valgrind errors about uninitialized bytes
    memset(bs->blocks, 0, sizeof(block_t) * BLOCK_STORE_NUM_BLOCKS);

    // Initialize FBM storing data in blocks
    void *fbm_data = bs->blocks + BITMAP_START_BLOCK;
    bs->fbm = bitmap_overlay(BITMAP_SIZE_BITS, fbm_data);
    // bitmap_format(bs->fbm, 0); // Superseded by above memset on all blocks

    // Mark blocks used by FBM as in-use
    for (size_t i=BITMAP_START_BLOCK; i<BITMAP_START_BLOCK+BITMAP_NUM_BLOCKS; i++)
        bitmap_set(bs->fbm, i);

    return bs;
}

void block_store_destroy(block_store_t *const bs)
{
    if (bs) {
        bitmap_destroy(bs->fbm);
        free(bs->blocks);
        bs->blocks = NULL;
        bs->fbm = NULL;
        free(bs);
    }
}

size_t block_store_allocate(block_store_t *const bs)
{
    if (bs == NULL)
        return SIZE_MAX;
    size_t block_id = bitmap_ffz(bs->fbm);
    return block_store_request(bs, block_id) ? block_id : SIZE_MAX;
}

bool block_store_request(block_store_t *const bs, const size_t block_id)
{
    if (bs == NULL || block_id >= BLOCK_STORE_NUM_BLOCKS)
        return false;

    if (bitmap_test(bs->fbm, block_id))
        return false;

    bitmap_set(bs->fbm, block_id);
    return true;
}

void block_store_release(block_store_t *const bs, const size_t block_id)
{
    if (bs == NULL || block_id >= BLOCK_STORE_NUM_BLOCKS)
        return;
    bitmap_reset(bs->fbm, block_id);
}

size_t block_store_get_used_blocks(const block_store_t *const bs)
{
    if (bs == NULL)
        return SIZE_MAX;
    return bitmap_total_set(bs->fbm);
}

size_t block_store_get_free_blocks(const block_store_t *const bs)
{
    if (bs == NULL)
        return SIZE_MAX;
    return bitmap_get_bits(bs->fbm) - bitmap_total_set(bs->fbm);
}

size_t block_store_get_total_blocks()
{
    return BLOCK_STORE_NUM_BLOCKS;
}

size_t block_store_read(const block_store_t *const bs, const size_t block_id, void *buffer)
{
    if (bs == NULL || buffer == NULL || block_id >= BLOCK_STORE_NUM_BLOCKS)
        return 0;

    memcpy(buffer, bs->blocks[block_id], BLOCK_SIZE_BYTES);
    return BLOCK_SIZE_BYTES;
}

size_t block_store_write(block_store_t *const bs, const size_t block_id, const void *buffer)
{
    if (bs == NULL || buffer == NULL || block_id >= BLOCK_STORE_NUM_BLOCKS)
        return 0;

    memcpy(bs->blocks[block_id], buffer, BLOCK_SIZE_BYTES);
    return BLOCK_SIZE_BYTES;
}

block_store_t *block_store_deserialize(const char *const filename)
{
    if (filename == NULL)
        return NULL;

    block_store_t *bs = block_store_create();
    if (bs == NULL)
        return NULL;

    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        perror("failed to open file for deserialization");
        perror(filename);
        block_store_destroy(bs);
        return NULL;
    }

    ssize_t size = sizeof(block_t) * BLOCK_STORE_NUM_BLOCKS;
    ssize_t bytes_read = read(fd, bs->blocks, size);
    if (bytes_read != size)
    {
        perror("failed to read all blocks from file for deserialization");
        perror(filename);
        block_store_destroy(bs);
        close(fd);
        return NULL;
    }

    close(fd);
    return bs;
}

size_t block_store_serialize(const block_store_t *const bs, const char *const filename)
{
    if (bs == NULL || filename == NULL)
        return 0;

    int fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0)
    {
        perror("failed to open file for serialization");
        perror(filename);
        return 0;
    }

    ssize_t size = sizeof(block_t) * BLOCK_STORE_NUM_BLOCKS;
    ssize_t bytes_written = write(fd, bs->blocks, size);
    if (bytes_written != size)
    {
        perror("failed to write all blocks to file for serialization");
        perror(filename);
        close(fd);
        return 0;
    }

    close(fd);
    return bytes_written;
}
