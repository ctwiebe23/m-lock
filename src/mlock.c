/*
 * PROJECT  : M-LOCK
 * FILE     : mlock.c
 * AUTHOR   : C Wiebe <ctwiebe23@gmail.com>
 * DATE     : OCT 12 2025
 * 
 * Source file for M-LOCK, a C memory allocator.  Documentation in header file.
 */

// ---[ INCLUDES ]-------------------------------------------------------------

#include "mlock.h"

// ---[ DEBUG ]----------------------------------------------------------------

#ifdef MLOCK_ENABLE_DEBUG
#include <stdio.h>
/**
 * Prints the given debug message.
 * @param __VA_ARGS__ Arguments passed to printf.
 */
#define DEBUG(...)                                                            \
    do {                                                                      \
        fprintf(stderr, "%s %3d ### ", __FILE__, __LINE__);                   \
        fprintf(stderr, __VA_ARGS__);                                         \
        fprintf(stderr, "\n");                                                \
    } while (0);
#else
#define DEBUG(...)
#endif

// ---[ TYPES ]----------------------------------------------------------------

typedef size_t word_t;  // A word; 64 bits in a 64-bit system
typedef char byte_t;    // A byte; 8 bits

// ---[ CONSTANTS ]------------------------------------------------------------

#ifdef MLOCK_WORD_SIZE
#define WORD_SIZE MLOCK_WORD_SIZE /* Word size in bytes */
#else
#define WORD_SIZE 8 /* Word size in bytes */
#endif

#define FREE      0  // The block is free
#define ALLOCATED 1  // The block is allocated

#define MIN_DATA_SIZE  (WORD_SIZE * 2)  // Min size of block data in bytes
#define MIN_BLOCK_SIZE (WORD_SIZE * 4)  // Min size of a total block in bytes
#define CHUNK_SIZE     (1 << 12)        // Initial heap size in bytes
#define HEADER_SIZE    WORD_SIZE        // Header size in bytes
#define BOUNDARY_SIZE  WORD_SIZE        // Boundary tag size in bytes

// ---[ MACROS ]---------------------------------------------------------------

/**
 * @returns The larger of x and y.
 */
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/**
 * @param size The aligned size of the block's data in bytes.
 * @param alloc 1 if the block is allocated, else 0.
 * @returns The header/boundary tag.
 */
#define PACK_HEADER(size, alloc) ((word_t)(size) | (word_t)(alloc))

/**
 * @param p Pointer to a word.
 * @returns The value at p.
 */
#define GET_WORD(p) (*(word_t*)(p))

/**
 * @param p Pointer to a word.
 * @param val The value to put at p.
 * @returns val.
 */
#define PUT_WORD(p, val) (GET_WORD(p) = (val))

/**
 * @param p Pointer to a header/boundary tag.
 * @returns The size of the block's data in bytes.
 */
#define GET_SIZE_FROM_HEADER(p) (GET_WORD(p) & ~0x7)

/**
 * @param p Pointer to a header/boundary tag.
 * @returns Whether or not the block is allocated.
 */
#define GET_ALLOC_FROM_HEADER(p) (GET_WORD(p) & 0x1)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the block's header.
 */
#define GET_HEADER(bp) ((byte_t*)(bp) - HEADER_SIZE)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the previous block's boundary tag.
 */
#define GET_PREV_BOUNDARY(bp) ((byte_t*)(bp) - HEADER_SIZE - BOUNDARY_SIZE)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns The size of the block's data in bytes.
 */
#define GET_SIZE(bp) GET_SIZE_FROM_HEADER(GET_HEADER(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Whether or not the block is allocated.
 */
#define GET_ALLOC(bp) GET_ALLOC_FROM_HEADER(GET_HEADER(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the block's boundary tag.
 */
#define GET_BOUNDARY(bp) ((byte_t*)(bp) + GET_SIZE(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns The size of the previous block's data in bytes.
 */
#define GET_PREV_SIZE(bp) GET_SIZE_FROM_HEADER(GET_PREV_BOUNDARY(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Whether or not the previous block is allocated.
 */
#define GET_PREV_ALLOC(bp) GET_ALLOC_FROM_HEADER(GET_PREV_BOUNDARY(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the next block's header.
 */
#define GET_NEXT_HEADER(bp) ((byte_t*)(bp) + GET_SIZE(bp) + BOUNDARY_SIZE)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the next block's data.
 */
#define GET_NEXT_BLOCK(bp)                                                    \
    ((byte_t*)(bp) + GET_SIZE(bp) + BOUNDARY_SIZE + HEADER_SIZE)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the previous block's data.
 */
#define GET_PREV_BLOCK(bp)                                                    \
    ((byte_t*)(bp) - HEADER_SIZE - BOUNDARY_SIZE - GET_PREV_SIZE(bp))

/**
 * @param fp Pointer to the start of a free block's data.
 * @returns Pointer to the start of the next free block's data.
 */
#define GET_NEXT_FREE(fp) (byte_t*)GET_WORD(fp)

/**
 * @param fp Pointer to the start of a free block's data.
 * @returns Pointer to the start of the previous free block's data.
 */
#define GET_PREV_FREE(fp) (byte_t*)GET_WORD((word_t*)(fp) + 1)

/**
 * @param fp Pointer to the start of a free block's data.
 * @param val The value to put as the pointer to the next free block.
 * @returns val.
 */
#define PUT_NEXT_FREE(fp, val) PUT_WORD((fp), (word_t)(val))

/**
 * @param fp Pointer to the start of a free block's data.
 * @param val The value to put as the pointer to the previous free block.
 * @returns val.
 */
#define PUT_PREV_FREE(fp, val) PUT_WORD((word_t*)(fp) + 1, (word_t)(val))

/**
 * @param bytes The original number of bytes.
 * @returns The adjusted number of bytes that is aligned.
 */
#define ALIGN_BYTES(bytes)                                                    \
    (((bytes) % 8 == 0) ? (bytes) : 8 * ((bytes) / 8 + 1))

/**
 * Redoes the header and boundary tag of the given block.
 * @param bp Pointer to the start of a block's data.
 * @param size The aligned size of the block's data in bytes.
 * @param alloc 1 if the block is allocated, else 0.
 */
#define REDO_HEADERS(bp, size, alloc)                                         \
    do {                                                                      \
        PUT_WORD(GET_HEADER(bp), PACK_HEADER(size, alloc));                   \
        PUT_WORD(GET_BOUNDARY(bp), PACK_HEADER(size, alloc));                 \
    } while (0);

/**
 * Makes two free blocks point to each other.
 * @param fp1 Pointer to the start of a free block's data.
 * @param fp2 Pointer to the start of a free block's data.
 */
#define LINK_FREE(fp1, fp2)                                                   \
    do {                                                                      \
        if (fp1 != fp2) {                                                     \
            if (fp1) {                                                        \
                PUT_NEXT_FREE(fp1, fp2);                                      \
            }                                                                 \
            if (fp2) {                                                        \
                PUT_PREV_FREE(fp2, fp1);                                      \
            }                                                                 \
        }                                                                     \
    } while (0);

// ---[ GLOBALS ]--------------------------------------------------------------

/**
 * Pointer to the data of the first block of the free list
 */
static byte_t* free_list = NULL;

// ---[ HELPER FUNCTION PROTOTYPES ]-------------------------------------------

/**
 * Removes a free block from the free list and adjusts its neighbor's next and
 * prev pointers.
 * @param fp Pointer to the start of a free block's data.
 */
static void remove_free_block(byte_t* fp);

/**
 * Extends the heap with a new free block and inserts it into the free list.
 * @param size The number of bytes that need to be in the block's data.
 * @returns 0 on success, -1 on failure.
 */
static int extend_heap(size_t size);

/**
 * Place an allocated block of at least the given size at the given free block.
 * Properly re-points the free list and adjusts the given size to abide by the
 * byte alignment and minimum block size.
 * @param fp Pointer to the start of a free block's data.
 * @param size The size of the block that must be allocated.
 */
static void place(byte_t* fp, word_t size);

/**
 * Finds a free block that can fit an allocated block of the given size.
 * @param size The size of the block's data in bytes that must be allocated.
 * @returns Pointer to the start of a free block's data, if one exists of the
 * needed size.  Else returns null.
 */
static byte_t* find_fit(word_t size);

// ---[ FUNCTION DEFINITIONS ]-------------------------------------------------

void* init_lock(void)
{
    DEBUG("Initializing memory");

    // Allocate initial heap
    word_t* heap_list = sbrk(WORD_SIZE * 4);
    word_t* heap_start = heap_list + 2;

    if (heap_list == NULL) {
        DEBUG("Failed initial sbrk");
        return NULL;
    }

    PUT_WORD(heap_list++, 0x00DECADE);
    PUT_WORD(heap_list++, PACK_HEADER(0, ALLOCATED));  // Prologue header
    PUT_WORD(heap_list++, PACK_HEADER(0, ALLOCATED));  // Prologue boundary tag
    PUT_WORD(heap_list++, PACK_HEADER(0, ALLOCATED));  // Epilogue header

    // extend_heap inserts the free block into free_list
    if (extend_heap(CHUNK_SIZE) == -1) {
        DEBUG("Failed to create the first free block");
        return NULL;
    }

    DEBUG("Finished initializing memory");
    return (void*)heap_start;
}

void* mlock(size_t size)
{
    DEBUG("Starting malloc of size %ld", size);

    if (size <= 0) {
        DEBUG("Can't malloc of size %ld", size);
        return NULL;
    }

    size = ALIGN_BYTES(size);
    size = MAX(size, MIN_DATA_SIZE);
    byte_t* fp = find_fit(size);

    if (fp != NULL) {
        place(fp, size);
        DEBUG("Placed block at %p", fp);
        return fp;
    }

    // No available blocks; extend heap to get more
    if (extend_heap(MAX(size, CHUNK_SIZE)) == -1) {
        DEBUG("Failed to extend memory by %ld bytes", size);
        return NULL;
    }

    // extend_heap put the result in free_list
    fp = free_list;

    place(fp, size);
    DEBUG("Malloc-ed extended block of size %ld at pointer %p", size, fp);
    return fp;
}

void unlock(void* ptr)
{
    DEBUG("Freeing pointer %p", ptr);

    word_t size = GET_SIZE(ptr);
    REDO_HEADERS(ptr, size, FREE);

    if (GET_PREV_ALLOC(ptr) == FREE) {
        // Coalesce with previous
        DEBUG("Coalescing with prev");
        ptr = GET_PREV_BLOCK(ptr);
        DEBUG("Prev pointer %p", ptr);
        size += GET_SIZE(ptr) + BOUNDARY_SIZE + HEADER_SIZE;
        REDO_HEADERS(ptr, size, FREE);
        remove_free_block(ptr);
    }

    byte_t* next_header = GET_NEXT_HEADER(ptr);

    if (GET_ALLOC_FROM_HEADER(next_header) == FREE) {
        // Coalesce with next
        DEBUG("Coalescing with next");
        DEBUG("Next header %p", next_header);
        size
            += GET_SIZE_FROM_HEADER(next_header) + BOUNDARY_SIZE + HEADER_SIZE;
        REDO_HEADERS(ptr, size, FREE);
        remove_free_block(next_header + HEADER_SIZE);
    }

    // Insert ptr before the current free list head
    LINK_FREE(ptr, free_list);
    PUT_PREV_FREE(ptr, NULL);
    free_list = ptr;

    DEBUG("Finished freeing pointer %p", ptr);
}

void* relock(void* ptr, size_t size)
{
    DEBUG("Reallocating pointer %p to size %ld", ptr, size);

    if (ptr == NULL) {
        DEBUG("Making new pointer");
        return mlock(size);
    }

    if (size <= 0) {
        DEBUG("Freeing pointer %p", ptr);
        unlock(ptr);
        return NULL;
    }

    size = ALIGN_BYTES(size);
    size = MAX(size, MIN_DATA_SIZE);
    word_t current_size = GET_SIZE(ptr);

    if (size == current_size) {
        DEBUG("No change needed");
        return ptr;
    }

    if (size < current_size) {
        size_t leftover = current_size - size;

        if (leftover < MIN_BLOCK_SIZE) {
            // Not enough leftovers to make a new free block
            DEBUG("Too few leftovers, no change needed");
            return ptr;
        }

        // Create new free block from leftovers
        REDO_HEADERS(ptr, size, ALLOCATED);
        byte_t* new_fp = GET_NEXT_BLOCK(ptr);
        REDO_HEADERS(new_fp, leftover - HEADER_SIZE - BOUNDARY_SIZE, FREE);
        unlock(new_fp);

        DEBUG("Shrunk and created new free block");
        return ptr;
    }

    size_t needed = size - current_size;
    byte_t* next_bp = GET_NEXT_BLOCK(ptr);
    size_t gained_in_merge = BOUNDARY_SIZE + HEADER_SIZE + GET_SIZE(next_bp);

    if (GET_ALLOC(next_bp) == ALLOCATED || gained_in_merge < needed) {
        // Next block is not free or next block is not large enough
        byte_t* new_ptr = mlock(size);

        // Copy old data over
        memcpy(new_ptr, ptr, current_size);

        unlock(ptr);
        DEBUG("Made new pointer entirely");
        return new_ptr;
    }

    // Next block can be merged into
    remove_free_block(next_bp);
    size_t leftover = gained_in_merge - needed;

    if (leftover == 0) {
        // Next block was exactly large enough
        REDO_HEADERS(ptr, size, ALLOCATED);
        DEBUG("Absorb next block");
        return ptr;
    }

    if (leftover < MIN_BLOCK_SIZE) {
        // Not enough leftovers to create a new free block; absorb it entirely
        size = current_size + gained_in_merge;
        REDO_HEADERS(ptr, size, ALLOCATED);
        DEBUG("Expand and absorb next block");
        return ptr;
    }

    // Create new free block from leftovers
    REDO_HEADERS(ptr, size, ALLOCATED);
    byte_t* new_fp = GET_NEXT_BLOCK(ptr);
    REDO_HEADERS(new_fp, leftover - HEADER_SIZE - BOUNDARY_SIZE, FREE);
    unlock(new_fp);

    DEBUG("Absorbed part of next block and created new free block");
    return ptr;
}

static void remove_free_block(byte_t* fp)
{
    DEBUG("Removing free block %p", fp);
    byte_t* next = GET_NEXT_FREE(fp);
    byte_t* prev = GET_PREV_FREE(fp);

    if (fp == free_list) {
        free_list = next;
    }

    LINK_FREE(prev, next);
    DEBUG("Removed free block %p", fp);
}

static int extend_heap(size_t size)
{
    DEBUG("Extending heap with %ld bytes", size);

    size = ALIGN_BYTES(size);
    byte_t* fp = sbrk(size + BOUNDARY_SIZE + HEADER_SIZE);

    if (fp == (void*)-1) {
        DEBUG("sbrk failed to extend heap");
        return -1;
    }

    REDO_HEADERS(fp, size, FREE);  // Override old epilogue with new header
    PUT_WORD(GET_NEXT_HEADER(fp), PACK_HEADER(0, ALLOCATED));  // New epilogue

    // Inserts fp into free_list
    unlock(fp);
    DEBUG("Extended heap to make new block and inserted into free_list");
    return 0;
}

static void place(byte_t* fp, word_t size)
{
    DEBUG("Placing a block of size %ld at pointer %p", size, fp);

    remove_free_block(fp);
    size = ALIGN_BYTES(size);

    word_t available_size = GET_SIZE(fp);
    word_t difference = available_size - size;

    if (difference == 0) {
        // No adjustment needed
        REDO_HEADERS(fp, size, ALLOCATED);
        DEBUG("Placed block");
        return;
    }

    if (difference < MIN_BLOCK_SIZE) {
        // Not enough space to make a new block; grow to absorb leftovers
        size = available_size;
        REDO_HEADERS(fp, size, ALLOCATED);
        DEBUG("Expanded and placed block");
        return;
    }

    // Create new free block
    REDO_HEADERS(fp, size, ALLOCATED);
    byte_t* new_fp = GET_NEXT_BLOCK(fp);
    REDO_HEADERS(new_fp, difference - HEADER_SIZE - BOUNDARY_SIZE, FREE);
    unlock(new_fp);
    DEBUG("Placed block and made new free block from leftovers");
}

static byte_t* find_fit(word_t size)
{
    DEBUG("Searching for free block of size %ld", size);

    if (free_list == NULL) {
        DEBUG("Free list is empty");
        return NULL;
    }

    size = ALIGN_BYTES(size);

    byte_t* fp = free_list;
    for (; fp != NULL; fp = GET_NEXT_FREE(fp)) {
        if (GET_SIZE(fp) >= size) {
            DEBUG("Found pointer %p", fp);
            return fp;
        }
    }

    DEBUG("Found no block large enough");
    return NULL;
}

/*
 * MIT License
 *
 * Copyright (c) 2025 C Wiebe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
