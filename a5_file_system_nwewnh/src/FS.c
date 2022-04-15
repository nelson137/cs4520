#include <fcntl.h>
#include <libgen.h>
#include <math.h>
#include <stdint.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "FS.h"
#include "bitmap.h"
#include "block_store.h"
#include "consts.h"
#include "dyn_array.h"

struct inode {

    // A bitmap denoting which entries in the directory entry block (see
    //   .data_direct) are in-use
    uint32_t dir_entry_map;

    // For alignment only
    char _alignment1[18];

    // A character denoting the file type:
    //   - 'r' for a regular file
    //   - 'd' for a directory
    char file_type;

    // The inode number of the file, in the range of [0,256)
    size_t inum;

    // The size of the file in bytes
    size_t file_size;

    // The number of hard-links to this inode
    size_t link_count;

    // Pointers (block numbers) to data blocks for this file
    uint16_t data_direct[FD_DIRECT_N_PTRS];

    // A pointer (block number) to a block containing direct pointers (see
    //   .data_direct)
    uint16_t data_indirect[1];

    // A pointer (block number) to a block containing indirect pointers (see
    //   .data_indirect)
    uint16_t data_double_indirect;

};


/**
 * Fields usage, locate_order, and locate_offset together identify the exact
 * byte at which the cursor points.
 */
struct fileDescriptor {

    // The inode number of the fd
    uint8_t inum;

    // Whether the cursor is currently in a direct, indirect, or double
    //   indirect block
    uint8_t usage;

    // The index of the block in which the cursor points (see .usage)
    // If usage is direct, locate_order is in the range of [0,6)
    // If usage is indirect, locate_order is in the range of [0,512)
    // If usage is double indirect, locate_order is in the range of [0,512**2)
    uint32_t locate_order;

    // The byte offset in the block (see .locate_order) of the cursor
    // The value is in the range of [0,BLOCK_SIZE_BYTES)
    uint32_t locate_offset;

};

const size_t INODE_SIZE = sizeof(struct inode);
const size_t FD_SIZE = sizeof(struct fileDescriptor);

typedef enum {
    FD_DIRECT          = 1 << 0,
    FD_INDIRECT        = 1 << 1,
    FD_DOUBLE_INDIRECT = 1 << 2,
} fileDescriptorUsage_t;

struct directoryFile {
    char filename[32];
    uint8_t inum;
};

struct FS {
    block_store_t *BlockStore_whole;
    block_store_t *BlockStore_inode;
    block_store_t *BlockStore_fd;
};

typedef uint8_t block_t[BLOCK_SIZE_BYTES];
typedef uint16_t ind_block_t[BLOCK_PTRS_PER_BLOCK];

#define MIN(a, b) ((a) <= (b) ? (a) : (b))
#define MAX(a, b) ((a) >= (b) ? (a) : (b))

#define _BS_READ_OK(fs, block_num, dest) (block_store_read((fs)->BlockStore_whole, (block_num), (dest)) == BLOCK_SIZE_BYTES)
#define _BS_WRITE_OK(fs, block_num, src) (block_store_write((fs)->BlockStore_whole, (block_num), (src)) == BLOCK_SIZE_BYTES)
#define _BS_INODE_READ_OK(fs, inum, dest) (block_store_inode_read((fs)->BlockStore_inode, (inum), (dest)) == INODE_SIZE)
#define _BS_INODE_WRITE_OK(fs, inum, src) (block_store_inode_write((fs)->BlockStore_inode, (inum), (src)) == INODE_SIZE)
#define _BS_FD_READ_OK(fs, fd_index, dest) (block_store_fd_read((fs)->BlockStore_fd, (fd_index), (dest)) == FD_SIZE)
#define _BS_FD_WRITE_OK(fs, fd_index, src) (block_store_fd_write((fs)->BlockStore_fd, (fd_index), (src)) == FD_SIZE)



/**
 * Add two numbers and clamp the sum to the given min and max
 * \param a The first addend
 * \param b The second addend
 * \param min The minimum bound of the result (inclusive)
 * \param max The maximum bound of the result (inclusive)
 * \return The clamped sum of a and b
 */
static ssize_t _clamped_add(ssize_t a, ssize_t b, ssize_t min, ssize_t max) {
    ssize_t res = a + b;
    if (res < min) return min;
    if (res > max) return max;
    return res;
}



/**
 * Isolate the directory part of a path (cut off the last component)
 * \param path The path to dirname (is left unchanged)
 * \return A dynamically allocated string with the dirname of path
 * Note: Memory is dynamically allocated and must be freed by user
 */
static char *_dirname(const char *path) {
    if (!PATH_OK(path) || strcmp(path, "/") == 0)
        goto err1;

    char *copy = strdup(path);
    if (copy == NULL)
        goto err1;

    char *dir = dirname(copy);
    if (dir == NULL || strcmp(dir, ".") == 0)
        goto err2;

    char *ret = strdup(dir);
    free(copy);
    return ret;
err2:
    free(copy);
err1:
    return NULL;
}



/**
 * Isolate the file name part of a path (cut off the directory name component)
 * \param path The path to basename (is left unchanged)
 * \return A dynamically allocated string with the basename of path
 * Note: Memory is dynamically allocated and must be freed by user
 */
static char *_basename(const char *path) {
    if (!PATH_OK(path) || strcmp(path, "/") == 0)
        goto err1;

    char *copy = strdup(path);
    if (copy == NULL)
        goto err1;

    char *base = basename(copy);
    if (base == NULL || strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
        goto err2;

    char *ret = strdup(base);
    free(copy);

    return ret;
err2:
    free(copy);
err1:
    return NULL;
}



/**
 * Split a path into its components (delimiter: '/')
 * \param path The path to split
 * \return A dynamic array containing the path components
 */
static dyn_array_t *_split_path(const char *path) {
    if (!PATH_OK(path))
        return NULL;

    char *copy = strdup(path);
    if (copy == NULL)
        return NULL;

    dyn_array_t *components = dyn_array_create(8, FS_FNAME_MAX, NULL);
    if (components == NULL) {
        free(copy);
        return NULL;
    }

    char *tok, *saveptr, *str = copy;
    while ((tok = strtok_r(str, "/", &saveptr)) != NULL) {
        dyn_array_push_back(components, tok);
        str = NULL;
    }

    free(copy);
    return components;
}



/**
 * Load an inode
 * \param fs The file system from which to load
 * \param inum The number of the inode to load
 * \param dest The buffer for the inode
 * \return Whether the read was successful
 */
static bool _inode_read(FS_t *fs, size_t inum, inode_t *dest) {
    if (fs == NULL || !INUM_OK(inum) || dest == NULL)
        return false;
    if (!block_store_sub_test(fs->BlockStore_inode, inum))
        return false;
    return _BS_INODE_READ_OK(fs, inum, dest);
}



/**
 * Load the directory entry block and directory entry map of an inode number
 * \param fs The file system from which to load inum
 * \param inum The number of the inode from which to load
 * \param block A buffer for the inode's directory entry block
 * \param map A pointer to the directory entry bitmap
 * \return 0 on success, -1 on error
 */
static int _inode_dir_load(
    FS_t *fs,
    inode_t *inode,
    void *block,
    bitmap_t **map
) {
    if (fs == NULL || inode == NULL || block == NULL || map == NULL)
        return -1;

    size_t block_num = inode->data_direct[0];

    if (inode->file_type != 'd')
        return -1;

    // Load dir_entry_map
    *map = bitmap_overlay(16, &inode->dir_entry_map);
    if (*map == NULL)
        return -1;

    // Load the directory entries block
    if (!_BS_READ_OK(fs, block_num, block)) {
        bitmap_destroy(*map);
        *map = NULL;
        return -1;
    }

    return 0;
}



/**
 * Find a child file in directory entries
 * \param entries The directory entries to search
 * \param map The directory entries bitmap
 * \param child The name of the child file for which to search
 * \return The inode number of the child if found, -1 if not found or error
 */
static int _inode_find_child(
    block_t *entries,
    bitmap_t *map,
    const char *child
) {
    if (entries == NULL || map == NULL || child == NULL)
        return -1;

    int child_inum = -1;
    for (size_t i=0; i<DIR_ENTRIES_PER_BLOCK; i++) {
        directoryFile_t *entry = (directoryFile_t*)entries + i;
        if (strncmp(entry->filename, child, FS_FNAME_MAX) == 0) {
            child_inum = entry->inum;
            break;
        }
    }

    return child_inum;
}



/**
 * Find a child file in an inode
 * \param fs The file system in which to search
 * \param parent_inum The inode number to search
 * \param child The name of the child file for which to search
 * \return The inode number of the child if found, -1 if not found or error
 */
static int _inum_find_child(FS_t *fs, size_t parent_inum, const char *child) {
    if (fs == NULL || !INUM_OK(parent_inum) || child == NULL)
        return -1;

    inode_t parent_inode;
    if (!_inode_read(fs, parent_inum, &parent_inode))
        return -1;

    bitmap_t *map = NULL;
    block_t block;
    if (_inode_dir_load(fs, &parent_inode, block, &map) < 0)
        return -1;

    int child_inum = _inode_find_child(&block, map, child);
    bitmap_destroy(map);
    return child_inum;
}



/**
 * Get the inode number of a path
 * \param fs The file system from which to search
 * \param path The path of the target file
 * \return The inode number of path
*/
static int _get_inum(FS_t *fs, const char *path) {
    if (fs == NULL || !PATH_OK(path))
        return -1;

    dyn_array_t *path_components = _split_path(path);
    size_t n_components = dyn_array_size(path_components);

    int component_inum = 0;
    for (size_t i=0; i<n_components; i++) {
        char *component_name = dyn_array_at(path_components, i);
        component_inum = _inum_find_child(fs, component_inum, component_name);
        if (component_inum < 0) {
            dyn_array_destroy(path_components);
            return -1;
        }
    }

    dyn_array_destroy(path_components);
    return component_inum;
}



/**
 * Load a file descriptor
 * \param fs The file system from which to load
 * \param fd_index The index of the file descriptor to load
 * \param dest The buffer for the file descriptor
 * \return Whether the read was successful
 */
static bool _fd_read(FS_t *fs, int fd_index, fileDescriptor_t *dest) {
    if (fs == NULL || !FD_OK(fd_index) || dest == NULL)
        return false;
    if (!block_store_sub_test(fs->BlockStore_fd, fd_index))
        return false;
    return _BS_FD_READ_OK(fs, fd_index, dest);
}



/**
 * Calculate the data block index in a file of a file descriptor cursor
 * \param fd An open file descriptor for the file
 * \return The data block index
 */
static size_t _fd_cursor_get_block_index(fileDescriptor_t *fd) {
    size_t block_index = 0;

    switch (fd->usage) {
        case FD_DOUBLE_INDIRECT: block_index += FD_INDIRECT_N_PTRS; NO_BREAK;
        case FD_INDIRECT:        block_index += FD_DIRECT_N_PTRS;   NO_BREAK;
        case FD_DIRECT:          block_index += fd->locate_order;   break;
        default:                 return SIZE_MAX;
    }

    return block_index;
}



/**
 * Calculate the offset (from BOF) of a file descriptor cursor
 * \param fd An open file descriptor for the file
 * \return The calculated offset
 */
static size_t _fd_cursor_get(fileDescriptor_t *fd) {
    return _fd_cursor_get_block_index(fd)*BLOCK_SIZE_BYTES + fd->locate_offset;
}



/**
 * Set a file descriptor cursor to an offset (from BOF)
 * \param fd An open file descriptor for the file
 * \param offset The offset
 * \return Whether the offset was valid and the cursor was set successfully
 */
static bool _fd_cursor_set(fileDescriptor_t *fd, size_t offset) {
    if (fd == NULL)
        return false;

    fileDescriptorUsage_t usage;
    if (offset < FD_DIRECT_MAX_OFF)
        usage = FD_DIRECT;
    else if (offset < FD_INDIRECT_MAX_OFF)
        usage = FD_INDIRECT;
    else if (offset < FD_DOUBLE_INDIRECT_MAX_OFF)
        usage = FD_DOUBLE_INDIRECT;
    else
        return false;

    fd->usage = usage;
    fd->locate_order = offset / BLOCK_SIZE_BYTES;
    fd->locate_offset = offset % BLOCK_SIZE_BYTES;

    switch (usage) {
        case FD_DOUBLE_INDIRECT:
            fd->locate_order -= FD_INDIRECT_N_PTRS;  NO_BREAK;
        case FD_INDIRECT:
            fd->locate_order -= FD_DIRECT_N_PTRS;    NO_BREAK;
        default:
            break; // Ignore other usage values
    }

    return true;
}



/**
 * Determine if a file descriptor cursor points in a data block of the file
 * \param inode The inode of the file
 * \param fd An open file descriptor for the file
 * \return Whether the file owns the block in which fd's cursor points
 */
static bool _cursor_in_owned_block(inode_t *inode, fileDescriptor_t *fd) {
    size_t cursor_block_index = _fd_cursor_get_block_index(fd);
    size_t n_file_blocks = ceil((double)inode->file_size / BLOCK_SIZE_BYTES);
    return cursor_block_index < n_file_blocks;
}



/**
 * Allocate and add a new data block to a file
 * \param fs The file system from which to allocate
 * \param inode The file to which to add the new data block
 * \return The block number of the new data block if successful, -1 if there is
 *   an error allocating or adding the block, -2 if fs is out of space
 */
static ssize_t _inode_add_owned_block(FS_t *fs, inode_t *inode) {
    if (fs == NULL || inode == NULL)
        goto err1;

    block_store_t *bs_whole = fs->BlockStore_whole;

    size_t err_ret = SIZE_MAX;
    size_t new_db_ind_ptr = SIZE_MAX;
    size_t new_ind_ptr = SIZE_MAX;
    size_t new_ptr = block_store_allocate(bs_whole);
    if (new_ptr == SIZE_MAX)
        goto err2_no_space;

    ind_block_t ind_block1, ind_block2;
    size_t index = ceil((double)inode->file_size / BLOCK_SIZE_BYTES);

    if (index < FD_DIRECT_MAX_PTRS) {
        inode->data_direct[index] = new_ptr;
        if (!_BS_INODE_WRITE_OK(fs, inode->inum, inode))
            goto err2;
    }

    else if (index < FD_INDIRECT_MAX_PTRS) {
        index -= FD_DIRECT_MAX_PTRS;
        if (index == 0) {
            if ((new_ind_ptr = block_store_allocate(bs_whole)) == SIZE_MAX)
                goto err2_no_space;
            *inode->data_indirect = new_ind_ptr;
            if (!_BS_INODE_WRITE_OK(fs, inode->inum, inode))
                goto err2;
        }

        if (!_BS_READ_OK(fs, *inode->data_indirect, ind_block1))
            goto err2;
        ind_block1[index] = new_ptr;
        if (!_BS_WRITE_OK(fs, *inode->data_indirect, ind_block1))
            goto err2;
    }

    else if (index < FD_DOUBLE_INDIRECT_MAX_PTRS) {
        index -= FD_INDIRECT_MAX_PTRS;
        if (index == 0) {
            if ((new_db_ind_ptr = block_store_allocate(bs_whole)) == SIZE_MAX)
                goto err2_no_space;
            inode->data_double_indirect = new_db_ind_ptr;
            if (!_BS_INODE_WRITE_OK(fs, inode->inum, inode))
                goto err2;
        }

        if (!_BS_READ_OK(fs, inode->data_double_indirect, ind_block1))
            goto err2;

        size_t ind_index1 = index / BLOCK_PTRS_PER_BLOCK;
        size_t ind_index2 = index % BLOCK_PTRS_PER_BLOCK;

        if (ind_index2 == 0) {
            if ((new_ind_ptr = block_store_allocate(bs_whole)) == SIZE_MAX)
                goto err2_no_space;
            ind_block1[ind_index1] = new_ind_ptr;
            if (!_BS_WRITE_OK(fs, inode->data_double_indirect, ind_block1))
                goto err2;
        }

        if (!_BS_READ_OK(fs, ind_block1[ind_index1], ind_block2))
            goto err2;
        ind_block2[ind_index2] = new_ptr;
        if (!_BS_WRITE_OK(fs, ind_block1[ind_index1], ind_block2))
            goto err2;
    }

    else {
        goto err2;
    }

    return new_ptr;
err2_no_space:
    if (err_ret == SIZE_MAX)
        err_ret = -2;
err2:
    if (err_ret == SIZE_MAX)
        err_ret = -1;
    if (new_db_ind_ptr != SIZE_MAX)
        block_store_release(bs_whole, new_db_ind_ptr);
    if (new_ind_ptr != SIZE_MAX)
        block_store_release(bs_whole, new_ind_ptr);
    if (new_ptr != SIZE_MAX)
        block_store_release(bs_whole, new_ptr);
err1:
    return err_ret;
}



/**
 * Load the data block in which a file descriptor cursor points
 * \param fs The file system from which to read
 * \param inode The inode of the file
 * \param fd An open file descriptor for the file
 * \param dest A buffer for the data block
 * \return Whether the read was successful
 */
static bool _fd_data_block_read(
    FS_t *fs,
    inode_t *inode,
    fileDescriptor_t *fd,
    void *dest
) {
    if (fs == NULL || inode == NULL || fd == NULL || dest == NULL)
        return false;

    uint16_t *ind_block = dest;

    if (fd->usage == FD_DIRECT) {
        uint32_t block_num = fd->locate_order;
        return _BS_READ_OK(fs, inode->data_direct[block_num], dest);
    }

    else if (fd->usage == FD_INDIRECT) {
        if (!_BS_READ_OK(fs, *inode->data_indirect, ind_block))
            return false;
        return _BS_READ_OK(fs, ind_block[fd->locate_order], dest);
    }

    else if (fd->usage == FD_DOUBLE_INDIRECT) {
        if (!_BS_READ_OK(fs, inode->data_double_indirect, ind_block))
            return false;
        size_t ind_index = fd->locate_order / BLOCK_PTRS_PER_BLOCK;
        if (!_BS_READ_OK(fs, ind_block[ind_index], ind_block))
            return false;
        uint16_t db_ind_index = fd->locate_order % BLOCK_PTRS_PER_BLOCK;
        return _BS_READ_OK(fs, ind_block[db_ind_index], dest);
    }

    // Catch invalid fd usage and any fallthrough
    return false;
}



/**
 * Write the data block in which a file descriptor cursor points
 * \param fs The file system in which to write
 * \param inode The inode of the file
 * \param fd An open file descriptor for the file
 * \param src The block to write
 * \return Whether the write was successful
 */
static bool _fd_data_block_write(
    FS_t *fs,
    inode_t *inode,
    fileDescriptor_t *fd,
    const void *src
) {
    if (fs == NULL || inode == NULL || fd == NULL || src == NULL)
        return false;

    ind_block_t ind_block;

    if (fd->usage == FD_DIRECT) {
        uint32_t block_num = fd->locate_order;
        return _BS_WRITE_OK(fs, inode->data_direct[block_num], src);
    }

    else if (fd->usage == FD_INDIRECT) {
        if (!_BS_READ_OK(fs, *inode->data_indirect, ind_block))
            return -1;
        return _BS_WRITE_OK(fs, ind_block[fd->locate_order], src);
    }

    else if (fd->usage == FD_DOUBLE_INDIRECT) {
        if (!_BS_READ_OK(fs, inode->data_double_indirect, ind_block))
            return -1;
        size_t ind_index = fd->locate_order / BLOCK_PTRS_PER_BLOCK;
        if (!_BS_READ_OK(fs, ind_block[ind_index], ind_block))
            return -1;
        uint16_t db_ind_index = fd->locate_order % BLOCK_PTRS_PER_BLOCK;
        return _BS_WRITE_OK(fs, ind_block[db_ind_index], src);
    }

    // Catch invalid fd usage and any fallthrough
    return false;
}



FS_t *fs_format(const char *path)
{
    if(path != NULL && strlen(path) != 0)
    {
        FS_t * ptr_FS = (FS_t*) calloc(1, sizeof(FS_t));
        ptr_FS->BlockStore_whole = block_store_create(path);

        // reserve the 1st block for bitmap of inode
        size_t bitmap_ID = block_store_allocate(ptr_FS->BlockStore_whole);

        // 2rd - 17th block for inodes, 16 blocks in total
        size_t inode_start_block = block_store_allocate(ptr_FS->BlockStore_whole);
        for(int i = 0; i < 15; i++)
            block_store_allocate(ptr_FS->BlockStore_whole);

        // install inode block store inside the whole block store
        ptr_FS->BlockStore_inode = block_store_inode_create(
            block_store_Data_location(ptr_FS->BlockStore_whole) + bitmap_ID*BLOCK_SIZE_BYTES,
            block_store_Data_location(ptr_FS->BlockStore_whole) + inode_start_block*BLOCK_SIZE_BYTES
        );

        // the first inode is reserved for root dir
        block_store_sub_allocate(ptr_FS->BlockStore_inode);

        // update the root inode info.
        uint8_t root_inum = 0;	// root inode is the first one in the inode table
        inode_t root_inode = {
            .dir_entry_map = 0,
            .file_type = 'd',
            .inum = root_inum,
            .link_count = 0,
        };
        block_store_inode_write(ptr_FS->BlockStore_inode, root_inum, &root_inode);

        // now allocate space for the file descriptors
        ptr_FS->BlockStore_fd = block_store_fd_create();

        return ptr_FS;
    }

    return NULL;
}



FS_t *fs_mount(const char *path)
{
    if(path != NULL && strlen(path) != 0)
    {
        FS_t * ptr_FS = (FS_t *)calloc(1, sizeof(FS_t));	// get started
        ptr_FS->BlockStore_whole = block_store_open(path);	// get the chunck of data

        // the bitmap block should be the 1st one
        size_t bitmap_ID = 0;

        // the inode blocks start with the 2nd block, and goes around until the 17th block, 16 in total
        size_t inode_start_block = 1;

        // attach the bitmaps to their designated place
        ptr_FS->BlockStore_inode = block_store_inode_create(block_store_Data_location(ptr_FS->BlockStore_whole) + bitmap_ID * BLOCK_SIZE_BYTES, block_store_Data_location(ptr_FS->BlockStore_whole) + inode_start_block * BLOCK_SIZE_BYTES);

        // since file descriptors are allocated outside of the whole blocks, we can simply reallocate space for it.
        ptr_FS->BlockStore_fd = block_store_fd_create();

        return ptr_FS;
    }

    return NULL;
}



int fs_unmount(FS_t *fs)
{
    if(fs != NULL)
    {
        block_store_inode_destroy(fs->BlockStore_inode);

        block_store_destroy(fs->BlockStore_whole);
        block_store_fd_destroy(fs->BlockStore_fd);

        free(fs);
        return 0;
    }
    return -1;
}



int fs_create(FS_t *fs, const char *path, file_t type) {
    if (fs == NULL || !PATH_OK(path))
        goto err1;
    if (strlen(path) && path[strlen(path)-1] == '/')
        goto err1;

    block_store_t *bs_whole = fs->BlockStore_whole;
    block_store_t *bs_inode = fs->BlockStore_inode;

    char new_file_type;
    switch (type) {
        case FS_REGULAR:   new_file_type = 'r'; break;
        case FS_DIRECTORY: new_file_type = 'd'; break;
        default:           goto err1;
    }

    /**
     * Load the inode of the file's parent
     */

    // Get the parent directory's path
    char *parent_path = _dirname(path);
    if (parent_path == NULL)
        goto err1;

    // Get the parent inode number
    int parent_inum = _get_inum(fs, parent_path);
    free(parent_path); // parent_path no longer needed
    parent_path = NULL;
    if (parent_inum < 0)
        goto err1;

    // Load the parent inode
    inode_t parent_inode;
    if (!_inode_read(fs, parent_inum, &parent_inode))
        goto err1;

    // Load the parent inode directory entries
    bitmap_t *parent_dentry_map = NULL;
    block_t parent_dentry_block;
    if (_inode_dir_load(fs, &parent_inode, parent_dentry_block, &parent_dentry_map) < 0)
        goto err1;

    // Get parent inode entry block number
    // Use a pointer so that any changes update parent_inode
    uint16_t *dentry_block_num = &parent_inode.data_direct[0];
    bool dentry_block_num_is_new = (parent_inode.file_size == 0);
    if (dentry_block_num_is_new) {
        // Directory has no dir entry block so allocate one
        size_t block_num = block_store_allocate(bs_whole);
        if (block_num == SIZE_MAX)
            goto err2;
        *dentry_block_num = block_num;
    }

    /**
     * Verify that the file can/should be created
     */

    // Error if parent directory is full
    if (bitmap_total_set(parent_dentry_map) >= DIR_ENTRIES_PER_BLOCK)
        goto err3;

    // Get the basename of the file to be created
    char *filename = _basename(path);
    if (filename == NULL)
        goto err3;
    // Error if the basename is too long
    if (strnlen(path, FS_FNAME_MAX) == FS_FNAME_MAX)
        goto err4;

    // Error if the file already exists
    if (_inode_find_child(&parent_dentry_block, parent_dentry_map, filename) >= 0)
        goto err4;

    /**
     * Create the new inode
     */

    // Get new inode number
    size_t new_inum = block_store_sub_allocate(bs_inode);
    if (new_inum == SIZE_MAX)
        goto err4;

    // Create the new inode
    inode_t node = {
        .dir_entry_map = 0,
        .file_type = new_file_type,
        .inum = new_inum,
        .file_size = 0,
        .link_count = 1,
    };

    /**
     * Write the new inode to the store
     */

    // Write the new inode to the store
    if (!_BS_INODE_WRITE_OK(fs, new_inum, &node))
        goto err5;

    // Add the file entry to the parent directory entry block
    size_t index = bitmap_ffz(parent_dentry_map);
    if (index == SIZE_MAX || index >= DIR_ENTRIES_PER_BLOCK)
        // Should never happen, caught when parent_dentry_map is loaded
        goto err5;
    bitmap_set(parent_dentry_map, index);
    parent_inode.file_size++;
    directoryFile_t *entry = ((directoryFile_t*)parent_dentry_block) + index;
    strncpy(entry->filename, filename, 32);
    entry->inum = new_inum;

    /**
     * Update the parent directory's inode
     */

    // Write the parent directory's entry block to the store
    if (!_BS_WRITE_OK(fs, *dentry_block_num, parent_dentry_block))
        goto err5;

    // Write the parent directory's inode to the store
    // Should have updated values:
    //   - A new bit set on the dir_entry_map
    //   - If this is the first child of the parent, the first direct block
    //       pointer is now valid
    //   - An incremented file_size
    if (!_BS_INODE_WRITE_OK(fs, parent_inum, &parent_inode))
        // Would be err5, but in case some of the new data got written to the
        // parent inode try to re-write the old data to undo the changes
        goto err6;

    bitmap_destroy(parent_dentry_map);
    free(filename);

    return 0;
err6:
    // Undo parent inode update
    bitmap_reset(parent_dentry_map, index);
    block_store_write(fs->BlockStore_whole, parent_inum, &parent_inode);
err5:
    block_store_sub_release(bs_inode, new_inum);
err4:
    free(filename);
err3:
    if (dentry_block_num_is_new)
        block_store_release(bs_whole, *dentry_block_num);
err2:
    bitmap_destroy(parent_dentry_map);
err1:
    return -1;
}



int fs_open(FS_t *fs, const char *path) {
    if (fs == NULL || path == NULL)
        return -1;

    int inum = _get_inum(fs, path);
    if (inum < 0)
        return -1;

    inode_t inode;
    if (!_inode_read(fs, inum, &inode))
        return -1;

    if (inode.file_type == 'd')
        return -1;

    size_t fd_index = block_store_sub_allocate(fs->BlockStore_fd);
    if (fd_index == SIZE_MAX)
        return -1;

    fileDescriptor_t fd = {
        .inum = inum,
        .usage = FD_DIRECT,
        .locate_order = 0,
        .locate_offset = 0,
    };

    if (!_BS_FD_WRITE_OK(fs, fd_index, &fd)) {
        block_store_sub_release(fs->BlockStore_fd, fd_index);
        return -1;
    }

    return fd_index;
}



int fs_close(FS_t *fs, int fd) {
    if (fs == NULL || fd < 0 || fd > 255)
        return -1;

    if (!block_store_sub_test(fs->BlockStore_fd, fd))
        return -1;

    block_store_sub_release(fs->BlockStore_fd, fd);

    return 0;
}



dyn_array_t *fs_get_dir(FS_t *fs, const char *path) {
    if (fs == NULL || !PATH_OK(path))
        return NULL;

    int inum = _get_inum(fs, path);
    if (inum < 0)
        return NULL;

    inode_t inode;
    if (!_inode_read(fs, inum, &inode))
        return NULL;

    bitmap_t *map = NULL;
    block_t block;
    if (_inode_dir_load(fs, &inode, block, &map) < 0)
        return NULL;

    // Create the dyn_array for the dentries
    dyn_array_t *entries_arr = dyn_array_create(
        DIR_ENTRIES_PER_BLOCK, sizeof(directoryFile_t), NULL);
    if (entries_arr == NULL) {
        bitmap_destroy(map);
        return NULL;
    }

    // Load all in-use dentries (according to the bitmap) into the dyn_array
    for (size_t i=0; i<DIR_ENTRIES_PER_BLOCK; i++)
        if (bitmap_test(map, i))
            dyn_array_push_back(entries_arr, (directoryFile_t*)block+i);

    bitmap_destroy(map);
    return entries_arr;
}



off_t fs_seek(FS_t *fs, int fd_index, off_t offset, seek_t whence) {
    if (fs == NULL || !FD_OK(fd_index) || !WHENCE_OK(whence))
        return -1;

    // Load the file descriptor
    fileDescriptor_t fd;
    if (!_fd_read(fs, fd_index, &fd))
        return -1;

    // Load the inode
    inode_t inode;
    if (!_inode_read(fs, fd.inum, &inode))
        return -1;

    // Calculate the global file offset
    size_t new_cursor = 0;
    if      (whence == FS_SEEK_CUR) new_cursor = _fd_cursor_get(&fd);
    else if (whence == FS_SEEK_END) new_cursor = inode.file_size;
    new_cursor = _clamped_add(new_cursor, offset, 0, inode.file_size);

    // Update the file descriptor cursor to be new_cursor
    if (_fd_cursor_set(&fd, new_cursor) == false)
        return -1;

    // Update the file descriptor
    if (!_BS_FD_WRITE_OK(fs, fd_index, &fd))
        return -1;

    return new_cursor;
}



ssize_t fs_read(FS_t *fs, int fd_index, void *dest, size_t nbyte) {
    if (fs == NULL || !FD_OK(fd_index) || dest == NULL)
        return -1;

    // Load the file descriptor
    fileDescriptor_t fd;
    if (!_fd_read(fs, fd_index, &fd))
        return -1;

    // Load the inode
    inode_t inode;
    if (!_inode_read(fs, fd.inum, &inode))
        return -1;

    size_t cursor = _fd_cursor_get(&fd);
    if (cursor == SIZE_MAX)
        return -1;
    if (cursor >= inode.file_size)
        return 0;

    block_t data_block;
    size_t n_to_read, n_to_read_remaining, n_read;
    n_to_read = n_to_read_remaining = MIN(
        nbyte,                    // Requested
        inode.file_size - cursor  // Remaining in file from cursor
    );

    while (n_to_read_remaining > 0) {
        n_read = MIN(
            n_to_read_remaining,                 // Remaining requested
            BLOCK_SIZE_BYTES - fd.locate_offset  // Remaining in block
        );

        if (n_read == BLOCK_SIZE_BYTES) {
            if (_fd_data_block_read(fs, &inode, &fd, dest) == false)
                return -1;
        } else {
            if (_fd_data_block_read(fs, &inode, &fd, data_block) == false)
                return -1;
            memcpy(dest, data_block + fd.locate_offset, n_read);
        }

        cursor += n_read;
        dest += n_read;
        n_to_read_remaining -= n_read;

        if (_fd_cursor_set(&fd, cursor) == false)
            return -1;
    }

    // Update the file descriptor
    if (!_BS_FD_WRITE_OK(fs, fd_index, &fd))
        return -1;

    return n_to_read - n_to_read_remaining;
}



ssize_t fs_write(FS_t *fs, int fd_index, const void *src, size_t nbyte) {
    if (fs == NULL || !FD_OK(fd_index) || src == NULL)
        goto err1;

    // Load the file descriptor
    fileDescriptor_t fd;
    if (!_fd_read(fs, fd_index, &fd))
        goto err1;

    // Load the inode
    inode_t inode;
    if (!_inode_read(fs, fd.inum, &inode))
        goto err1;

    size_t cursor = _fd_cursor_get(&fd);
    if (cursor == SIZE_MAX)
        goto err1;

    size_t max_new_ptrs = ceil((double)nbyte / BLOCK_SIZE_BYTES);
    ssize_t *new_ptrs, *new_ptrs_it;
    new_ptrs = new_ptrs_it = calloc(max_new_ptrs, sizeof(ssize_t));

    block_t data_block;
    size_t n_write, nbyte_orig = nbyte;
    const void *chunk_src;

    while (nbyte > 0) {
        // Allocate a new data block for the file if needed
        if (_cursor_in_owned_block(&inode, &fd) == false) {
            if ((*new_ptrs_it = _inode_add_owned_block(fs, &inode)) == -1)
                goto err2;
            if (*new_ptrs_it == -2)
                break; // No more space available
            new_ptrs_it++;
        }

        // Calculate the number of bytes to write next
        n_write = MIN(nbyte, BLOCK_SIZE_BYTES - fd.locate_offset);

        if (n_write < BLOCK_SIZE_BYTES) {
            // Read data block from store into local temp storage
            if (_fd_data_block_read(fs, &inode, &fd, data_block) == false)
                goto err2;
            // Copy partial data into local temp storage
            memcpy(data_block + fd.locate_offset, src, n_write);
            // Next block written to store will be local temp storage
            chunk_src = data_block;
        } else {
            // Next block written to store will be user buffer
            chunk_src = src;
        }

        if (_fd_data_block_write(fs, &inode, &fd, chunk_src) == false)
            goto err2;

        cursor += n_write;
        src += n_write;
        nbyte -= n_write;

        if (cursor > inode.file_size)
            inode.file_size = cursor;
        if (_fd_cursor_set(&fd, cursor) == false)
            goto err2;
    }

    // Update the file descriptor
    if (!_BS_FD_WRITE_OK(fs, fd_index, &fd))
        goto err2;

    // Update the inode in case the file size has increased
    if (!_BS_INODE_WRITE_OK(fs, inode.inum, &inode))
        goto err2;

    return nbyte_orig - nbyte;
err2:
    for (ssize_t *it=new_ptrs; it!=new_ptrs_it; it++)
        block_store_release(fs->BlockStore_whole, *it);
    free(new_ptrs);
err1:
    return -1;
}
