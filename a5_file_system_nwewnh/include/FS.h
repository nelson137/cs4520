#ifndef _FS_H__
#define _FS_H__

#include <sys/types.h>

#include <stdio.h>
#include <time.h>
#include <stdlib.h>		// for size_t
#include <inttypes.h>	// for uint16_t
#include <string.h>

#include "dyn_array.h"
#include "block_store.h"

// components of FS
typedef struct inode inode_t;
typedef struct fileDescriptor fileDescriptor_t;
typedef struct directoryFile directoryFile_t;

typedef struct FS FS_t;

extern const size_t INODE_SIZE;
extern const size_t FD_SIZE;

// seek_t is for fs_seek
typedef enum { FS_SEEK_SET, FS_SEEK_CUR, FS_SEEK_END } seek_t;

typedef enum { FS_REGULAR, FS_DIRECTORY } file_t;

#define FS_FNAME_MAX (32) // INCLUDING null terminator
#define FS_MAX_OPEN_FILES 256

typedef struct {
    // You can add more if you want
    // vvv just don't remove or rename these vvv
    char name[FS_FNAME_MAX];
    file_t type;
} file_record_t;

///
/// Formats (and mounts) an FS file for use
/// \param fname The file to format
/// \return Mounted FS object, NULL on error
///
FS_t *fs_format(const char *path);

///
/// Mounts an FS object and prepares it for use
/// \param fname The file to mount
/// \return Mounted FS object, NULL on error
///
FS_t *fs_mount(const char *path);

///
/// Unmounts the given object and frees all related resources
/// \param fs The FS object to unmount
/// \return 0 on success, < 0 on failure
///
int fs_unmount(FS_t *fs);

///
/// Creates a new file at the specified location
///   Directories along the path that do not exist are not created
/// \param fs The FS containing the file
/// \param path Absolute path to file to create
/// \param type Type of file to create (regular/directory)
/// \return 0 on success, < 0 on failure
///
int fs_create(FS_t *fs, const char *path, file_t type);

///
/// Opens the specified file for use
///   R/W position is set to the beginning of the file (BOF)
///   Directories cannot be opened
/// \param fs The FS containing the file
/// \param path path to the requested file
/// \return file descriptor to the requested file, < 0 on error
///
int fs_open(FS_t *fs, const char *path);

///
/// Closes the given file descriptor
/// \param fs The FS containing the file
/// \param fd The file to close
/// \return 0 on success, < 0 on failure
///
int fs_close(FS_t *fs, int fd);

///
/// Moves the R/W position of the given descriptor to the given location
///   Files cannot be seeked past EOF or before BOF (beginning of file)
///   Seeking past EOF will seek to EOF, seeking before BOF will seek to BOF
/// \param fs The FS containing the file
/// \param fd The descriptor to seek
/// \param offset Desired offset relative to whence
/// \param whence Position from which offset is applied
/// \return offset from BOF, < 0 on error
///
off_t fs_seek(FS_t *fs, int fd, off_t offset, seek_t whence);

///
/// Reads data from the file linked to the given descriptor
///   Reading past EOF returns data up to EOF
///   R/W position in incremented by the number of bytes read
/// \param fs The FS containing the file
/// \param fd The file to read from
/// \param dst The buffer to write to
/// \param nbyte The number of bytes to read
/// \return number of bytes read (< nbyte IFF read passes EOF), < 0 on error
///
ssize_t fs_read(FS_t *fs, int fd, void *dst, size_t nbyte);

///
/// Writes data from given buffer to the file linked to the descriptor
///   Writing past EOF extends the file
///   Writing inside a file overwrites existing data
///   R/W position in incremented by the number of bytes written
/// \param fs The FS containing the file
/// \param fd The file to write to
/// \param dst The buffer to read from
/// \param nbyte The number of bytes to write
/// \return number of bytes written (< nbyte IFF out of space), < 0 on error
///
ssize_t fs_write(FS_t *fs, int fd, const void *src, size_t nbyte);

///
/// Deletes the specified file and closes all open descriptors to the file
///   Directories can only be removed when empty
/// \param fs The FS containing the file
/// \param path Absolute path to file to remove
/// \return 0 on success, < 0 on error
///
int fs_remove(FS_t *fs, const char *path);

///
/// Populates a dyn_array with information about the files in a directory
///   Array contains up to 15 file_record_t structures
/// \param fs The FS containing the file
/// \param path Absolute path to the directory to inspect
/// \return dyn_array of file records, NULL on error
///
dyn_array_t *fs_get_dir(FS_t *fs, const char *path);

/// Moves the file from one location to the other
///   Moving files does not affect open descriptors
/// \param fs The FS containing the file
/// \param src Absolute path of the file to move
/// \param dst Absolute path to move the file to
/// \return 0 on success, < 0 on error
///
int fs_move(FS_t *fs, const char *src, const char *dst);

/// Link the dst with the src
/// dst and src should be in the same File type, say, both are files or both are directories
/// \param fs The F18FS containing the file
/// \param src Absolute path of the source file
/// \param dst Absolute path to link the source to
/// \return 0 on success, < 0 on error
///
int fs_link(FS_t *fs, const char *src, const char *dst);

#endif
