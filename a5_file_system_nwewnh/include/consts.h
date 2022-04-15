#ifndef CONSTS_H__
#define CONSTS_H__

#define BLOCK_STORE_NUM_BLOCKS 65536 // 2^16
#define BLOCK_STORE_AVAIL_BLOCKS 65528 // Last 8 blocks consumed by the FBM
#define BLOCK_SIZE_BYTES 1024 // 2^10
#define BLOCK_SIZE_BITS (8 * BLOCK_SIZE_BYTES)
#define BLOCK_STORE_NUM_BYTES (BLOCK_STORE_NUM_BLOCKS * BLOCK_SIZE_BYTES)

#define NUM_INODES 256
#define NUM_FDS 256

#define DIR_ENTRIES_PER_BLOCK 31
#define BLOCK_PTRS_PER_BLOCK 512

#define INUM_OK(inum) ((inum) < NUM_INODES)
#define PATH_OK(path) ((path) != NULL && (path)[0] == '/' && strlen(path) > 0)
#define FD_OK(fd) (0 <= (fd) && (fd) < NUM_FDS)
#define WHENCE_OK(whence) ((whence)==FS_SEEK_SET || (whence)==FS_SEEK_CUR || (whence)==FS_SEEK_END)

#define FD_DIRECT_N_PTRS 6
#define FD_INDIRECT_N_PTRS BLOCK_PTRS_PER_BLOCK
#define FD_DOUBLE_INDIRECT_N_PTRS (BLOCK_PTRS_PER_BLOCK * BLOCK_PTRS_PER_BLOCK)

#define FD_DIRECT_SIZE (FD_DIRECT_N_PTRS * BLOCK_SIZE_BYTES)
#define FD_INDIRECT_SIZE (FD_INDIRECT_N_PTRS * BLOCK_SIZE_BYTES)
#define FD_DOUBLE_INDIRECT_SIZE (FD_DOUBLE_INDIRECT_N_PTRS * BLOCK_SIZE_BYTES)

#define FD_DIRECT_MAX_PTRS FD_DIRECT_N_PTRS
#define FD_INDIRECT_MAX_PTRS (FD_DIRECT_MAX_PTRS + FD_INDIRECT_N_PTRS)
#define FD_DOUBLE_INDIRECT_MAX_PTRS (FD_INDIRECT_MAX_PTRS + FD_DOUBLE_INDIRECT_N_PTRS)

#define FD_DIRECT_MAX_OFF FD_DIRECT_SIZE
#define FD_INDIRECT_MAX_OFF (FD_DIRECT_MAX_OFF + FD_INDIRECT_SIZE)
#define FD_DOUBLE_INDIRECT_MAX_OFF (FD_INDIRECT_MAX_OFF + FD_DOUBLE_INDIRECT_SIZE)

#ifdef __has_attribute
    #if __has_attribute(__fallthrough__)
        #define NO_BREAK __attribute__((__fallthrough__))
    #else
        #define NO_BREAK
    #endif
#else
    #define NO_BREAK
#endif

#endif
