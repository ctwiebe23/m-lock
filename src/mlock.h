/*
 * PROJECT  : M-LOCK
 * FILE     : mlock.h
 * AUTHOR   : C Wiebe <ctwiebe23@gmail.com>
 * DATE     : OCT 12 2025
 *
 * Custom allocator using explicit doubly-linked lists, boundary tags,
 * coalescing, and LIFO insertion.
 *
 * IF YOUR SYSTEM IS NOT 64-BIT, DEFINE `MLOCK_WORD_SIZE` TO BE EQUAL TO YOUR
 * SYSTEM'S WORD SIZE IN BYTES.  FOR 32-BIT SYSTEMS, THAT WOULD BE 4.  YOU WILL
 * GET SEGMENTATION FAULTS OTHERWISE.
 *
 * ----------------------------------------------------------------------------
 *
 * Each block has a one-word header and boundary tag of the form:
 *
 *                        63 62 61  .  .  .  3  2  1  0
 *                      +-------------------------------+
 *                      |  s  s  s  .  .  .  s  0  0  a |
 *                      +-------------------------------+
 *
 * Where s is the size of the block's data in bytes and a is set if the block
 * is allocated.  Blocks are aligned to eight-byte boundaries, which is why the
 * first three bits of the size are inconsequential.
 *
 * For free blocks, the first two words of the payload will be pointers to the
 * data of the next free block and the previous free block.  Thus, the smallest
 * possible total block size is four words (two words of data/pointers, two
 * words of header/boundary tag).  Data-wise, the smallest possible block is
 * two words.
 *
 * The free list is LIFO --- only the "first" node of the list is tracked using
 * a global variable, and new frees will be inserted at the start to become the
 * new head.
 *
 * The heap has the following form:
 *
 *                       word   contents
 *                     +------+--------------------------+
 *                     |    1 | padding (key)            |
 *                     |    2 | prologue header          |
 *                     |    3 | prologue boundary tag    |
 *                     |    . | ...                      |
 *                     |    . | zero or more user blocks |
 *                     |    . | ...                      |
 *                     |    n | epilogue header          |
 *                     +------+--------------------------+
 */

#ifndef MLOCK
#define MLOCK

// ---[ INCLUDES ]-------------------------------------------------------------

#include <string.h>  // For memcpy
#include <unistd.h>  // For sbrk

// ---[ FUNCTION PROTOTYPES ]--------------------------------------------------

/**
 * Initialize the memory manager.
 * @returns Pointer to the start of the heap on a success, else NULL.
 */
void* init_lock(void);

/**
 * Allocate a block of at least the given size.
 * @param size The minimum size of the block's data in bytes.
 * @returns A pointer to the start of the block's data.
 */
void* mlock(size_t size);

/**
 * Frees the given block by adding it to the free list.
 * @param bp Pointer to the start of a block's data.
 */
void unlock(void* ptr);

/**
 * Re-allocates the given pointer to a block of the given size.
 * @param ptr Pointer to the start of a block's data.
 * @param size The new size of the block in bytes.
 * @returns The new pointer.
 */
void* relock(void* ptr, size_t size);

#endif

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
