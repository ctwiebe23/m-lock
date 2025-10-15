/*
 * PROJECT  : M-LOCK
 * FILE     : mm.c
 * AUTHOR   : Carston Wiebe
 * DATE     : OCT 12 2025
 * 
 * Custom allocator using explicit doubly-linked lists, boundary tags,
 * coalescing, and LIFO insertion.
 * 
 * Each block has a one-word header and boundary tag of the form:
 * 
 *    31 30 29  .  .  .  3  2  1  0
 *   +------------------------------+
 *   | s  s  s  .  .  .  s  0  0  a |
 *   +------------------------------+
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
 * This program expects to be ran on a 32-bit machine, and will not work
 * properly otherwise.
 * 
 * // TODO: heap
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "mm.h"
#include "memlib.h"

team_t team = {
    "M-LOCK",  // Team name
    "Carston Wiebe",  // My name
    "cwiebe3@huskers.unl.edu",  // My email address
    "",  // No teamate
    ""  // No teamate
};

// ---[ CONFIG ]---------------------------------------------------------------

/**
 * Disable to hide debug messages.
 */
#define ENABLE_DEBUG

#ifdef ENABLE_DEBUG

/**
 * Prints the given debug message.
 * @param __VA_ARGS__ Arguments passed to printf.
 */
#define DEBUG(...) \
    do { \
        printf("%s %3d ### ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } while (0);

#else

#define DEBUG(...)

#endif

// ---[ TYPES ]----------------------------------------------------------------

typedef size_t  Word;   // A word.  32 bits in a 32-bit system.
typedef char    Byte;   // A byte.  8 bits.

// ---[ CONSTANTS ]------------------------------------------------------------

#define WORD_SIZE       4           // Word size in bytes
#define MIN_DATA_SIZE   8           // Minimum size of block data in bytes
#define MIN_BLOCK_SIZE  16          // Minimum size of a total block in bytes
#define CHUNK_SIZE      (1 << 12)   // Initial heap size in bytes
#define HEADER_SIZE     WORD_SIZE   // Header size in bytes
#define BOUNDARY_SIZE   WORD_SIZE   // Boundary tag size in bytes

// ---[ MACROS ]---------------------------------------------------------------

/**
 * @returns The larger of x and y.
 */
#define MAX(x, y) \
    ((x) > (y) ? (x) : (y))

/**
 * @returns The smaller of x and y.
 */
#define MIN(x, y) \
    ((x) < (y) ? (x) : (y)) // TODO: remove

/**
 * @param size The aligned size of the block's data in bytes.
 * @param alloc 1 if the block is allocated, else 0.
 * @returns The header/boundary tag.
 */
#define PACK_HEADER(size, alloc) \
    ((Word)(size) | (Word)(alloc))

/**
 * @param p Pointer to a word.
 * @returns The value at p.
 */
#define GET_WORD(p) \
    (*(Word *)(p))

/**
 * @param p Pointer to a word.
 * @param val The value to put at p.
 * @returns val.
 */
#define PUT_WORD(p, val) \
    (GET_WORD(p) = (val))

/**
 * @param p Pointer to a header/boundary tag.
 * @returns The size of the block's data in bytes.
 */
#define GET_SIZE_FROM_HEADER(p) \
    (GET_WORD(p) & ~0x7)

/**
 * @param p Pointer to a header/boundary tag.
 * @returns Whether or not the block is allocated.
 */
#define GET_ALLOC_FROM_HEADER(p) \
    (GET_WORD(p) & 0x1)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the block's header.
 */
#define GET_HEADER(bp) \
    ((Byte *)(bp) - HEADER_SIZE)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the previous block's boundary tag.
 */
#define GET_PREV_BOUNDARY(bp) \
    ((Byte *)(bp) - HEADER_SIZE - BOUNDARY_SIZE)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns The size of the block's data in bytes.
 */
#define GET_SIZE(bp) \
    GET_SIZE_FROM_HEADER(GET_HEADER(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Whether or not the block is allocated.
 */
#define GET_ALLOC(bp) \
    GET_ALLOC_FROM_HEADER(GET_HEADER(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the block's boundary tag.
 */
#define GET_BOUNDARY(bp) \
    ((Byte *)(bp) + GET_SIZE(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns The size of the previous block's data in bytes.
 */
#define GET_PREV_SIZE(bp) \
    GET_SIZE_FROM_HEADER(GET_PREV_BOUNDARY(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Whether or not the previous block is allocated.
 */
#define GET_PREV_ALLOC(bp) \
    GET_ALLOC_FROM_HEADER(GET_PREV_BOUNDARY(bp))

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the next block's header.
 */
#define GET_NEXT_HEADER(bp) \
    ((Byte *)(bp) + GET_SIZE(bp) + BOUNDARY_SIZE)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the next block's data.
 */
#define GET_NEXT_BLOCK(bp) \
    ((Byte *)(bp) + GET_SIZE(bp) + BOUNDARY_SIZE + HEADER_SIZE)

/**
 * @param bp Pointer to the start of a block's data.
 * @returns Pointer to the previous block's data.
 */
#define GET_PREV_BLOCK(bp) \
    ((Byte *)(bp) - HEADER_SIZE - BOUNDARY_SIZE - GET_PREV_SIZE(bp))

/**
 * @param fp Pointer to the start of a free block's data.
 * @returns Pointer to the start of the next free block's data.
 */
#define GET_NEXT_FREE(fp) \
    GET_WORD(fp)

/**
 * @param fp Pointer to the start of a free block's data.
 * @returns Pointer to the start of the previous free block's data.
 */
#define GET_PREV_FREE(fp) \
    GET_WORD((Word *)fp + 1)

/**
 * @param fp Pointer to the start of a free block's data.
 * @param val The value to put as the pointer to the next free block.
 * @returns val.
 */
#define PUT_NEXT_FREE(fp, val) \
    (GET_NEXT_FREE(fp) = (Byte*)(val))

/**
 * @param fp Pointer to the start of a free block's data.
 * @param val The value to put as the pointer to the previous free block.
 * @returns val.
 */
#define PUT_PREV_FREE(fp, val) \
    (GET_PREV_FREE(fp) = (Byte*)(val))

/**
 * @param bytes The original number of bytes.
 * @returns The adjusted number of bytes that is aligned.
 */
#define ALIGN_BYTES(bytes) \
    ((bytes % 8 == 0) ? bytes : 8 * (bytes / 8 + 1))

/**
 * Redoes the header and boundary tag of the given block.
 * @param bp Pointer to the start of a block's data.
 * @param size The aligned size of the block's data in bytes.
 * @param alloc 1 if the block is allocated, else 0.
 */
#define REDO_HEADERS(bp, size, alloc) \
    do { \
        PUT_WORD(GET_HEADER(bp), PACK_HEADER(size, alloc)); \
        PUT_WORD(GET_BOUNDARY(bp), PACK_HEADER(size, alloc)); \
    } while (0);

/**
 * Makes two free blocks point to each other.
 * @param fp1 Pointer to the start of a free block's data.
 * @param fp2 Pointer to the start of a free block's data.
 */
#define LINK_FREE(fp1, fp2) \
    do { \
        PUT_NEXT_FREE(fp1, fp2); \
        PUT_PREV_FREE(fp2, fp1); \
    } while (0);

// ---[ GLOBALS ]--------------------------------------------------------------

static Byte *freeList;  // Pointer to the data of the first block of the free list

// ---[ HELPER FUNCTION PROTOTYPES ]-------------------------------------------

/**
 * Removes a free block from the free list and adjusts its neighbor's next and
 * prev pointers.
 * @param fp Pointer to the start of a free block's data.
 */
static void removeFreeBlock(Byte* fp);

/**
 * Extends the heap with a new free block and returns a pointer to its data.
 * @param numNeededWords The number of words that need to be in the block's data.
 * @returns Pointer to a new block's data on success, null on failure.
 */
static Byte *extendHeap(size_t numNeededWords);

/**
 * Place an allocated block of at least the given size at the given free block.
 * Properly re-points the free list and adjusts the given size to abide by the
 * byte alignment and minimum block size.
 * @param fp Pointer to the start of a free block's data.
 * @param size The size of the block that must be allocated.
 */
static void place(Byte *fp, Word size);

/**
 * Finds a free block that can fit an allocated block of the given size.
 * @param size The size of the block's data in bytes that must be allocated.
 * @returns Pointer to the start of a free block's data, if one exists of the
 * needed size.  Else returns null.
 */
static Byte *findFit(Word size);

/**
 * Prints the given block to the STDOUT.
 * @param bp Pointer to the start of a block's data.
 */
static void printBlock(Byte *bp);  // TODO: remove?

// ---[ FUNCTIONS ]------------------------------------------------------------

/**
 * Initialize the memory manager.
 * @returns Pointer to the start of the heap on a success, else -1.
 */
int mm_init(void) {
    DEBUG("Initializing memory");
    
    // Allocate initial heap
    Byte* heapList = mem_sbrk(BOUNDARY_SIZE + HEADER_SIZE);

    if (heapList == NULL) {
        DEBUG("Failed initial mem_sbrk");
        return -1;
    }

    PUT_WORD(heapList, PACK_HEADER(0, 1));  // Prologue boundary tag
    heapList += WORD_SIZE;
    PUT_WORD(heapList, PACK_HEADER(0, 1));  // Epilogue header

    // Epilogue becomes header for the initial free block, extendHeap creates
    // new epilogue
    freeList = extendHeap(CHUNK_SIZE / WORD_SIZE);

    if (freeList == NULL) {
        DEBUG("Failed to create the first free block");
        return -1;
    }

    // Initialize empty free list
    PUT_NEXT_FREE(freeList, NULL);
    PUT_PREV_FREE(freeList, NULL);

    heapList = freeList;
    DEBUG("Finished initializing memory");
    return (int)heapList;
}

/**
 * Allocate a block of at least the given size.
 * @param size The minimum size of the block's data in bytes.
 * @returns A pointer to the start of the block's data.
 */
void* mm_malloc(size_t size) {
    DEBUG("Starting malloc of size %d", size);

    if (size <= 0) {
        DEBUG("Can't malloc of size %d", size);
        return NULL;
    }

    size = ALIGN_BYTES(size);
    Byte* fp = findFit(size);

    if (fp != NULL) {
        place(fp, size);
        DEBUG("Placed block at %p", fp);
        return fp;
    }

    // No available blocks; extend heap to get more
    fp = extendHeap(MAX(size, CHUNK_SIZE) / WORD_SIZE);

    if (fp == NULL) {
        DEBUG("Failed to extend memory by %d bytes", size);
        return NULL;
    }

    place(fp, size);
    DEBUG("Malloced extended block of size %d at pointer %p", size, fp);
    return fp;
}

/**
 * Frees the given block by adding it to the free list.
 * @param bp Pointer to the start of a block's data.
 */
void mm_free(void *bp) {
    DEBUG("Freeing pointer %p", bp);

    Word size = GET_SIZE(bp);
    REDO_HEADERS(bp, size, 0);

    if (GET_PREV_ALLOC(bp) == 0) {
        // Coalesce with previous
        DEBUG("Coelescing with prev");
        bp = GET_PREV_BLOCK(bp);
        size += GET_SIZE(bp) + BOUNDARY_SIZE + HEADER_SIZE;
        REDO_HEADERS(bp, size, 0);
        removeFreeBlock(bp);
    }

    Byte* nextHeader = GET_NEXT_HEADER(bp);

    if (GET_ALLOC_FROM_HEADER(nextHeader) == 0) {
        // Coalesce with next
        DEBUG("Coelescing with next");
        size += GET_SIZE_FROM_HEADER(nextHeader) + BOUNDARY_SIZE + HEADER_SIZE;
        REDO_HEADERS(bp, size, 0);
        removeFreeBlock(nextHeader + HEADER_SIZE);
    }

    // Insert bp before the current free list head
    LINK_FREE(bp, freeList);
    PUT_PREV_FREE(bp, NULL);
    freeList = bp;

    DEBUG("Finished freeing pointer %p", bp);
}

/**
 * Re-allocates the given pointer to a block of the given size.
 * @param ptr Pointer to the start of a block's data.
 * @param size The new size of the block in bytes.
 * @returns The new pointer.
 */
void *mm_realloc(void *ptr, size_t size) {
    DEBUG("Reallocating pointer %p to size %d", ptr, size);

    if (ptr == NULL) {
        DEBUG("Making new pointer");
        return mm_malloc(size);
    }

    if (size == 0) {
        DEBUG("Freeing pointer %p", ptr);
        mm_free(ptr);
        return NULL;
    }

    size = ALIGN_BYTES(size);
    Word currentSize = GET_SIZE(ptr);

    if (size == currentSize) {
        DEBUG("No change needed");
        return ptr;
    }

    if (size < currentSize) {
        size_t leftover = currentSize - size;

        if (leftover < MIN_BLOCK_SIZE) {
            // Not enough leftovers to make a new free block
            DEBUG("Too few leftovers, no change needed");
            return ptr;
        }

        // Create new free block from leftovers
        REDO_HEADERS(ptr, size, 1);
        Byte* newFp = GET_NEXT_BLOCK(ptr);
        REDO_HEADERS(newFp, leftover - HEADER_SIZE - BOUNDARY_SIZE, 0);
        mm_free(newFp);

        DEBUG("Shrunk and created new free block");
        return ptr;
    }

    size_t needed = size - currentSize;
    Byte* nextBp = GET_NEXT_BLOCK(ptr);
    size_t gainedInMerge = BOUNDARY_SIZE + HEADER_SIZE + GET_SIZE(nextBp);

    if (GET_ALLOC(nextBp) == 1 || gainedInMerge < needed) {
        // Next block is not free or next block is not large enough
        Byte* newPtr = mm_malloc(size);

        // Copy old data over
        Byte* newPtrIterator = newPtr;
        Byte* oldPtrIterator = ptr;
        Byte* stoppingPoint = oldPtrIterator + currentSize;

        while (oldPtrIterator != stoppingPoint) {
            (*newPtrIterator) = (*oldPtrIterator);
            newPtrIterator++;
            oldPtrIterator++;
        }

        mm_free(ptr);
        DEBUG("Made new pointer entirely");
        return newPtr;
    }

    // Next block can be merged into
    removeFreeBlock(nextBp);
    size_t leftover = gainedInMerge - needed;

    if (leftover == 0) {
        // Next block was exactly large enough
        REDO_HEADERS(ptr, size, 1);
        DEBUG("Absorb next block");
        return ptr;
    }

    if (leftover < MIN_BLOCK_SIZE) {
        // Not enough leftovers to create a new free block; absorb it entirely
        size = currentSize + gainedInMerge;
        REDO_HEADERS(ptr, size, 1);
        DEBUG("Expand and absorb next block");
        return ptr;
    }

    // Create new free block from leftovers
    REDO_HEADERS(ptr, size, 1);
    Byte* newFp = GET_NEXT_BLOCK(ptr);
    REDO_HEADERS(newFp, leftover - HEADER_SIZE - BOUNDARY_SIZE, 0);
    mm_free(newFp);

    DEBUG("Absorbed part of next block and created new free block");
    return ptr;
}

// /*
//  * mm_checkheap - Check the heap for consistency
//  */
// void mm_checkheap(int verbose)
// {
//     char *bp = heap_listp;

//     if (verbose)
//         printf("Heap (%p):\n", heap_listp);
//     if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || GET_ALLOC(HDRP(heap_listp)))
//         printf("Bad prologue header\n");

//     for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
//         if (verbose)
//             printblock(bp);
//     }

//     if (verbose)
//         printblock(bp);
//     if ((GET_SIZE(HDRP(bp)) != 0) || (GET_ALLOC(HDRP(bp))))
//         printf("Bad epilogue header\n");
// }

static void removeFreeBlock(Byte* fp) {
    DEBUG("Removing free block %p", fp);
    Byte* next = GET_NEXT_FREE(fp);
    Byte* prev = GET_PREV_FREE(fp);

    if (fp == freeList) {
        freeList = next;
    }

    if (prev && next) {
        LINK_FREE(prev, next);
    } else if (prev) {
        PUT_NEXT_FREE(prev, NULL);
    } else if (next) {
        PUT_PREV_FREE(next, NULL);
    }

    DEBUG("Removed free block %p", fp);
}

static Byte *extendHeap(size_t numNeededWords) {
    DEBUG("Extending heap with %d words", numNeededWords);

    if (numNeededWords % 2 == 1) {
        numNeededWords += 1;
    }

    // Assuming numNeedWords was originally > 0, we know it now has a minimum
    // value of two, so we don't need to make an additional check to confirm
    // it reaches the minimum block size.

    Word size = numNeededWords * WORD_SIZE;
    Byte *fp = mem_sbrk(size + BOUNDARY_SIZE + HEADER_SIZE);

    if (fp == (void *)-1) {
        DEBUG("Failed mem_sbrk to extend heap");
        return NULL;
    }

    REDO_HEADERS(fp, size, 0);                          // Override old epilogue with new header
    PUT_WORD(GET_NEXT_HEADER(fp), PACK_HEADER(0, 1));   // New epilogue

    LINK_FREE(fp, freeList);
    PUT_PREV_FREE(fp, NULL);
    freeList = fp;

    DEBUG("Extended heap to make new block");
    return fp;
}

static void place(Byte *fp, Word size) {
    DEBUG("Placing a block of size %d at pointer %p", size, fp);

    removeFreeBlock(fp);
    size = ALIGN_BYTES(size);

    size_t availableSize = GET_SIZE(fp);
    size_t difference = availableSize - size;

    if (difference == 0) {
        // No adjustment needed
        REDO_HEADERS(fp, size, 1);
        DEBUG("Placed block");
        return;
    }
    
    if (difference < MIN_BLOCK_SIZE) {
        // Not enough space to make a new block; grow to absorb leftovers
        size = availableSize;
        REDO_HEADERS(fp, size, 1);
        DEBUG("Expanded and placed block");
        return;
    }

    // Create new free block
    REDO_HEADERS(fp, size, 1);
    Word* newFp = GET_NEXT_BLOCK(fp);
    REDO_HEADERS(newFp, difference - HEADER_SIZE - BOUNDARY_SIZE, 0);
    mm_free(newFp);
    DEBUG("Placed block and made new free block from leftovers");
}

static Byte *findFit(Word size) {
    DEBUG("Searching for free block of size %d", size);

    if (freeList == NULL) {
        DEBUG("Free list is empty");
        return NULL;
    }

    size = ALIGN_BYTES(size);

    Word* fp = freeList;
    for (; fp != NULL; fp = GET_NEXT_FREE(fp)) {
        if (GET_SIZE(fp) >= size) {
            DEBUG("Found pointer %p", fp);
            return fp;
        }
    }

    DEBUG("Found no block large enough");
    return NULL;
}

static void printBlock(Byte *bp) {
    Word size = GET_SIZE(bp);
    Word alloc = GET_ALLOC(bp);

    if (size == 0 && alloc) {
        printf("%p is a prologue/epilogue", bp);
        return;
    }

    if (size == 0) {
        printf("%p is malformed (free w/ size = 0)", bp);
        return;
    }

    if (alloc) {
        printf("%p is allocated with size %d", bp, size);
        return;
    }

    printf(
        "%p is free with size %d.  The next free block is %p and the previous %p",
        bp,
        size,
        GET_NEXT_FREE(bp),
        GET_PREV_FREE(bp)
    );
}
