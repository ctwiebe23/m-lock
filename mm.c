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
    ((x) < (y) ? (x) : (y))

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
    (GET_NEXT_FREE(fp) = (val))

/**
 * @param fp Pointer to the start of a free block's data.
 * @param val The value to put as the pointer to the previous free block.
 * @returns val.
 */
#define PUT_PREV_FREE(fp, val) \
    (GET_PREV_FREE(fp) = (val))

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

static Word *freeList;  // Pointer to the data of the first block of the free list
static Byte *heapList;  // Pointer to the data of the first block of the heap

// ---[ HELPER FUNCTION PROTOTYPES ]-------------------------------------------

/**
 * Removes a free block from the free list and adjusts its neighbor's next and
 * prev pointers.
 * @param fp Pointer to the start of a free block's data.
 */
static void removeFreeBlock(Word* fp);

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
 * @returns 0 on a success, -1 on failure.
 */
static int place(Byte *fp, Word size);

/**
 * Finds a free block that can fit an allocated block of the given size.
 * @param size The size of the block that must be allocated.
 * @returns Pointer to the start of a free block's data, if one exists of the
 * needed size.  Else returns null.
 */
static Byte *findFit(Word size);

/**
 * Prints the given block to the STDOUT.
 * @param bp Pointer to the start of a block's data.
 */
static void printBlock(Byte *bp);

// ---[ FUNCTIONS ]------------------------------------------------------------

/**
 * Initialize the memory manager.
 * @returns Pointer to the start of the heap on a success, else -1.
 */
int mm_init(void) {
    // Allocate initial heap
    heapList = mem_sbrk(BOUNDARY_SIZE + HEADER_SIZE);

    if (heapList == NULL) {
        return -1;
    }

    PUT_WORD(heapList, PACK_HEADER(0, 1));  // Prologue boundary tag
    heapList += WORD_SIZE;
    PUT_WORD(heapList, PACK_HEADER(0, 1));  // Epilogue header

    // Epilogue becomes header for the initial free block, extendHeap creates
    // new epilogue
    freeList = extendHeap(CHUNK_SIZE / WORD_SIZE);

    if (freeList == NULL) {
        return -1;
    }

    // Initialize empty free list
    PUT_NEXT_FREE(freeList, NULL);
    PUT_PREV_FREE(freeList, NULL);

    heapList = freeList;

    return (int)heapList;
}

/*
 * mm_malloc - Allocate a block with at least size bytes of payload
 */
/* $begin mmmalloc */
void *mm_malloc(size_t size)
{
    size_t asize;      /* adjusted block size */
    size_t extendsize; /* amount to extend heap if no fit */
    char *bp;
    //    printf("call mm_malloc\n");

    /* Ignore spurious requests */
    if (size <= 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= WSIZE)
        asize = WSIZE + OVERHEAD;
    else
        asize = DSIZE * ((size + (OVERHEAD) + (DSIZE-1)) / DSIZE);
    //printf("asize = %d\n", asize);

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
     //printf("extendsize = %d\n", extendsize);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {
        printf("mm_malloc = NULL\n");
        return NULL;
    }
    //printf("return address = %p\n", bp);
    place(bp, asize);
    //mm_checkheap(1);
    return bp;
}
/* $end mmmalloc */

/**
 * Frees the given block by adding it to the free list.
 * @param bp Pointer to the start of a block's data.
 */
void mm_free(void *bp) {
    Word size = GET_SIZE(bp);

    if (GET_PREV_ALLOC(bp) == 0) {
        bp = GET_PREV_BLOCK(bp);
        size += GET_SIZE(bp) + BOUNDARY_SIZE + HEADER_SIZE;
        REDO_HEADERS(bp, size, 0);
        removeFreeBlock(bp);
        // LINK_FREE(GET_PREV_FREE(bp), GET_NEXT_FREE(bp));  TODO: adapt to remove with caution for removing head
    }

    Byte* nextHeader = GET_NEXT_HEADER(bp);

    if (GET_ALLOC_FROM_HEADER(nextHeader) == 0) {
        size += GET_SIZE_FROM_HEADER(nextHeader) + BOUNDARY_SIZE + HEADER_SIZE;
        REDO_HEADERS(bp, size, 0);
        removeFreeBlock(nextHeader + HEADER_SIZE);
    }

    // Insert bp before the current free list head
    LINK_FREE(bp, freeList);
    PUT_PREV_FREE(bp, NULL);
    freeList = bp;
}

/**
 * Re-allocates the given pointer to a block of the given size.
 * @param ptr Pointer to the start of a block's data.
 * @param size The new size of the block in bytes.
 * @returns The new pointer.
 */
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return mm_malloc(size);
    }

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    size = ALIGN_BYTES(size);
    Word currentSize = GET_SIZE(ptr);

    if (size == currentSize) {
        return ptr;
    }

    if (size > currentSize) {
        size_t difference = size - currentSize;
        Byte* nextBp = GET_NEXT_BLOCK(ptr);
        size_t gainedInMerge = BOUNDARY_SIZE + HEADER_SIZE + GET_SIZE(nextBp);

        if (GET_ALLOC(nextBp) == 1) {
            // Next block is not free
            goto returnNewPointer;
        }

        if (gainedInMerge < difference) {
            // Next block is not large enough
            goto returnNewPointer;
        }

        if (gainedInMerge == difference) {
            // Next block is exactly large enough
            removeFreeBlock(nextBp);
            REDO_HEADERS(ptr, size, 1);
            return ptr;
        }

        return NULL;  // TODO: merge or returnUnchangedPointer
    }

    if (size < currentSize) {
        // TODO
        return NULL;
    }

returnNewPointer:
    Byte* newPtr = mm_malloc(size);

    // Copy old data over
    Byte* newPtrIterator = newPtr;
    Byte* oldPtrIterator = ptr;
    Byte* stoppingPoint = oldPtrIterator + MIN(currentSize, size);

    while (oldPtrIterator != stoppingPoint) {
        (*newPtrIterator) = (*oldPtrIterator);
        newPtrIterator++;
        oldPtrIterator++;
    }

    mm_free(ptr);
    return newPtr;
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

static Byte *extendHeap(size_t numNeededWords) {
    if (numNeededWords % 2 == 1) {
        numNeededWords += 1;
    }

    // Assuming numNeedWords was originally > 0, we know it now has a minimum
    // value of two, so we don't need to make an additional check to confirm
    // it reaches the minimum block size.

    Word size = numNeededWords * WORD_SIZE;
    Byte *fp = mem_sbrk(size + BOUNDARY_SIZE + HEADER_SIZE);

    if (fp == (void *)-1) {
        return NULL;
    }

    REDO_HEADERS(fp, size, 0);                          // Override old epilogue with new header
    PUT_WORD(GET_NEXT_HEADER(fp), PACK_HEADER(0, 1));   // New epilogue

    LINK_FREE(fp, freeList);
    PUT_PREV_FREE(fp, NULL);
    freeList = fp;

    return fp;
}

/*
 * place - Place block of asize bytes at start of free block bp
 *         and split if remainder would be at least minimum block size
 */
/* $begin mmplace */
/* $begin mmplace-proto */
static void place(void *bp, size_t asize)
/* $end mmplace-proto */
{
    size_t csize = GET_SIZE(HDRP(bp));
    // printf("csize = %d\n", csize);

    if ((csize - asize) >= (DSIZE)) {  // TODO: new min
        PUT(HDRP(bp), PACK(asize, 0));  // TODO: swap 0 1
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 1));  // TODO: swap 0 1
    } else {
        PUT(HDRP(bp), PACK(csize, 0));  // TODO: swap 0 1
    }
}
/* $end mmplace */

static Byte *findFit(Word size) {
    if (freeList == NULL) {
        return NULL;
    }

    size = ALIGN_BYTES(size);

    for (Word* fp = freeList; fp != NULL; fp = GET_NEXT_FREE(fp)) {
        if (GET_SIZE(fp) >= size) {
            return fp;
        }
    }

    return NULL;
}

static void printblock(void *bp)
{
        size_t hsize, halloc;

        hsize = GET_SIZE(HDRP(bp));
        halloc = GET_ALLOC(HDRP(bp));

        if (hsize == 0) {
            printf("%p: EOL\n", bp);
            return;
        }

        printf("%p: header: [%zu:%c]\n", bp, hsize, (halloc ? 'f' : 'a'));
}
