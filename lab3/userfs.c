#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#include "userfs.h"

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

typedef struct block Block;

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	const char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

	/* PUT HERE OTHER MEMBERS */
	size_t size;
	bool to_be_deleted;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;

	int flags;
	size_t offset;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int ufs_open(const char *filename, int flags) {
    struct file *file_ptr = NULL;

    // Find the file in the list
    for (struct file *f = file_list; f != NULL; f = f->next) {
        if (!f->to_be_deleted && !strcmp(f->name, filename))
            file_ptr = f;
    }

    // Return an error if the file was not found and cannot be created
    if (!file_ptr && !(flags & UFS_CREATE)) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    // Create the file if it does not exist
    if (!file_ptr) {
        file_ptr = (struct file *) malloc(sizeof(struct file));
        if (!file_ptr) {
            // handle out of memory error
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }

        // Initialize the file
        file_ptr->block_list = NULL;
        file_ptr->last_block = NULL;
        file_ptr->name = strdup(filename);
        file_ptr->next = NULL;
        file_ptr->prev = NULL;
        file_ptr->refs = 0;
        file_ptr->size = 0;
        file_ptr->to_be_deleted = false;

        // Add the file to the list
        if (!file_list) {
            file_list = file_ptr;
        } else {
            struct file *last_file = NULL;
            for (struct file *f = file_list; f != NULL; f = f->next) {
                last_file = f;
            }
            last_file->next = file_ptr;
            file_ptr->prev = last_file;
        }
    }

    // Increment the reference count
    ++(file_ptr->refs);

    // Create a file descriptor for the file
    struct filedesc *file_desc_ptr = (struct filedesc*) malloc(sizeof(struct filedesc));
    if (!file_desc_ptr) {
        // handle out of memory error
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    // Initialize the file descriptor
    file_desc_ptr->file = file_ptr;
    file_desc_ptr->flags = flags;
    file_desc_ptr->offset = 0;

    // Push the descriptor onto the stack
    int available_fd = -1;
    if (!file_descriptor_capacity) {
        file_descriptors = realloc(file_descriptors, sizeof(struct file_descriptor *) * (++file_descriptor_capacity));
        file_descriptors[0] = NULL;
    }

    for (int i = file_descriptor_capacity - 1; i >= 0; --i) {
        if (!file_descriptors[i]) {
            available_fd = i;
            break;
        }
    }

    if (available_fd == -1) {
        // Create a new descriptor
        file_descriptors = realloc(file_descriptors, sizeof(struct file_descriptor *) * (++file_descriptor_capacity));
        available_fd = file_descriptor_capacity - 1;
    }

    file_descriptors[available_fd] = file_desc_ptr;

    // Return the file descriptor handle
    return available_fd;
}

ssize_t ufs_write(int _fd, const char *buf, size_t size) {
    int fd_error_code = 0;

    if (_fd < 0 || _fd >= file_descriptor_capacity)
        fd_error_code =  UFS_ERR_NO_FILE;
    else if (!file_descriptors[_fd])
        fd_error_code = UFS_ERR_NO_FILE;
    else
        fd_error_code = UFS_ERR_NO_ERR;

    if (fd_error_code) {
        ufs_error_code = fd_error_code;
        return -1; 		
    }

    struct filedesc *file_desc = file_descriptors[_fd];

    if (file_desc->flags & UFS_READ_ONLY) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1; 
    }

	if (file_desc->offset > file_desc->file->size)
		file_desc->offset = file_desc->file->size;

    struct file *file = file_desc->file;

    if (!file->last_block) {
        file->last_block = (Block *) malloc(sizeof(Block));
        file->block_list = file->last_block;
        file->last_block->memory = (char *) malloc(sizeof(char) * BLOCK_SIZE);
        file->last_block->prev = NULL;
        file->last_block->next = NULL;
        file->last_block->occupied = 0;
    }

    int skip = 0;
    Block *block = file->block_list;

    while (file_desc->offset > (skip + 1) * BLOCK_SIZE) {
        block = block->next;
        ++skip;

        if (!block) {
            block = (Block *) malloc(sizeof(Block));
            block->memory = (char *) malloc(sizeof(char) * BLOCK_SIZE);
            block->prev = file->last_block;
            block->next = NULL;
            block->occupied = 0;
            file->last_block->next = block;
            file->last_block = block;
        }
    }

    size_t block_offset = file_desc->offset % BLOCK_SIZE;
    size_t bytes_written = 0;

    while (bytes_written < size) {
        if (block->occupied == BLOCK_SIZE) {
            Block *new_block = (Block *) malloc(sizeof(Block));
            new_block->memory = (char *) malloc(sizeof(char) * BLOCK_SIZE);
            new_block->prev = file->last_block;
            new_block->next = NULL;
            new_block->occupied = 0;
            file->last_block->next = new_block;
            file->last_block = new_block;
            block = new_block;
            block_offset = 0;
        }

        size_t bytes_to_write = BLOCK_SIZE - block_offset;

        // check if data to write is less than block size
        if (bytes_to_write > size - bytes_written)
            bytes_to_write = size - bytes_written;

        if (file->size + bytes_to_write > MAX_FILE_SIZE)
            break;

        memcpy(block->memory + block_offset, buf + bytes_written, bytes_to_write);

        file_desc->offset += bytes_to_write;
        block_offset += bytes_to_write;
        block->occupied = block->occupied > block_offset ? block->occupied : block_offset;
        file->size = file_desc->offset > file->size ? file_desc->offset : file->size;
        bytes_written += bytes_to_write;
    }

    if (bytes_written < size && file->size == MAX_FILE_SIZE) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1; 
    }

    return bytes_written;
}

ssize_t ufs_read(int fd, char *buf, size_t size)
{
    int fd_error_code = 0;

    if (fd < 0 || fd >= file_descriptor_capacity)
        fd_error_code = UFS_ERR_NO_FILE;
    else if (!file_descriptors[fd])
        fd_error_code = UFS_ERR_NO_FILE;
    else
        fd_error_code = UFS_ERR_NO_ERR;

    if (fd_error_code) {
        ufs_error_code = fd_error_code;
        return -1;
    }

    struct filedesc *file_desc = file_descriptors[fd];

    if (file_desc->flags & UFS_WRITE_ONLY) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

	if (file_desc->offset > file_desc->file->size)
		file_desc->offset = file_desc->file->size;

    Block *current_block = file_desc->file->block_list;

    if (!current_block)
        return 0;

    // Move to the offset of the pointer
    for (int i = 0; i < file_desc->offset / BLOCK_SIZE; ++i, current_block = current_block->next)
        if (!current_block) return 0;

    size_t block_offset = file_desc->offset % BLOCK_SIZE;
    size_t bytes_read = 0;

    while (bytes_read < size) {
        if (block_offset == BLOCK_SIZE) {
            current_block = current_block->next;
            block_offset = 0;

            if (!current_block) break;
        }

        if (current_block->occupied - block_offset == 0)
            break;

        size_t to_read = BLOCK_SIZE - block_offset;

        if (to_read > size - bytes_read)
            to_read = size - bytes_read;

        if (to_read > current_block->occupied - block_offset)
            to_read = current_block->occupied - block_offset;

        memcpy(buf + bytes_read, current_block->memory + block_offset, to_read);

        file_desc->offset += to_read;
        bytes_read += to_read;
        block_offset += to_read;
    }

    return bytes_read;
}

int ufs_close(int fd) {
    int fd_error_code = 0;

    if (fd < 0 || fd >= file_descriptor_capacity) {
        fd_error_code = UFS_ERR_NO_FILE;
    } else if (!file_descriptors[fd]) {
        fd_error_code = UFS_ERR_NO_FILE;
    } else {
        fd_error_code = UFS_ERR_NO_ERR;
    }

    if (fd_error_code) {
        ufs_error_code = fd_error_code;
        return -1;
    }

    // decrement reference counter
    struct filedesc *file_desc = file_descriptors[fd];
    struct file *f = file_desc->file;
    --(f->refs);

    // if there are no more reference,
    // perform delayed deletion if needed
    if (!f->refs && f->to_be_deleted) {
        // free file blocks
        Block *last_block = NULL;

        for (Block *b = f->block_list; b != NULL; b = b->next) {
            free(b->memory);
            if (b->prev) {
                free(b->prev);
            }

            last_block = b;
        }

        free(last_block);
        free((void *)f->name);

        // fix file list connections
        if (f->next) {
            f->next->prev = f->prev;
        }

        if (f->prev) {
            f->prev->next = f->next;
        }

        if (file_list == f) {
            file_list = f->next;
        }

        // free file instance itself
        free(f);
    }

    free(file_descriptors[fd]);
    file_descriptors[fd] = NULL;

    // if file descriptor was at the end of the list,
    // we can decrease list size
    while (file_descriptors && file_descriptor_capacity > 0 && !file_descriptors[file_descriptor_capacity - 1]) {
        free(file_descriptors[--file_descriptor_capacity]);
    }

    file_descriptors = realloc(file_descriptors, sizeof(struct file_desc *) * file_descriptor_capacity);

    return 0;
}


int ufs_delete(const char *filename)
{
	/* IMPLEMENT THIS FUNCTION */
	struct file *f = NULL;

	for (struct file *i = file_list; i != NULL; i = i->next){
		if (!i->to_be_deleted && !strcmp(i->name, filename))
			f = i;
	}

	if (!f){
		ufs_error_code = UFS_ERR_NO_FILE;   
		return -1; 
	}
	
	if (f->refs) {
		// file becomes invisible for opening, but
		// it still exists in memory until last reference is closed
		f->to_be_deleted = true;
	} else {
		// free file blocks
		Block *last_block = NULL;

		for (Block *b = f->block_list; b != NULL; b = b->next){
			free(b->memory);
			if (b->prev)
				free(b->prev);

			last_block = b;
		}

		free(last_block);
		free((void *)f->name);

		// fix file list connections
		if (f->next)
			f->next->prev = f->prev;
		
		if (f->prev)
			f->prev->next = f->next;
		

		if (file_list == f)
			file_list = f->next;
		
		// free file instance itself
		free(f);
	}
	
	return 0;
}

int ufs_resize(int _fd, size_t new_size) {
    // Check if file descriptor is valid
    if (_fd < 0 || _fd >= file_descriptor_capacity || !file_descriptors[_fd]) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    // Check if new size is valid
    if (new_size >= MAX_FILE_SIZE) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return -1;
    }

    struct file *f = file_descriptors[_fd]->file;
    int num_blocks = 0;

    // Count the number of blocks in the file
    for (Block *block = f->block_list; block != NULL; block = block->next) {
        ++num_blocks;
    }

    // Add new blocks until the size is reached or exceeded
    while (num_blocks * BLOCK_SIZE < new_size) {
        Block *new_block = (Block *) malloc(sizeof(Block));
        char *memory = (char *) malloc(sizeof(char) * BLOCK_SIZE);

        *new_block = (Block) {
            .memory = memory,
            .prev = f->last_block,
            .next = NULL,
            .occupied = 0,
        };

        if (!f->block_list) {
            f->block_list = new_block;
            f->last_block = new_block;
        } else {
            f->last_block->next = new_block;
            f->last_block = new_block;
        }

        ++num_blocks;
    }

    // Remove blocks until the size is less than or equal to the new size
    if (num_blocks) {
        while ((num_blocks - 1) * BLOCK_SIZE > new_size) {
            if (f->last_block->prev) {
                Block *block = f->last_block->prev;

                free(block->next->memory);
                free(block->next);
                block->next = NULL;

                f->last_block = block;
            }
            --num_blocks;
        }
    }

    f->size = new_size;

    if (f->last_block)
		f->last_block->occupied = new_size % BLOCK_SIZE;
        
    return 0;
}