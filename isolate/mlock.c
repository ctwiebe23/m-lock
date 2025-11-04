/*
 * PROJECT  : M-LOCK
 * FILE     : mm.c
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
 *                        word   contents
 *                      +------+--------------------------+
 *                      |    1 | padding (key)            |
 *                      |    2 | prologue header          |
 *                      |    3 | prologue boundary tag    |
 *                      |    . | ...                      |
 *                      |    . | zero or more user blocks |
 *                      |    . | ...                      |
 *                      |    n | epilogue header          |
 *                      +------+--------------------------+
 */

#include <string.h>  // For memcpy
#include <unistd.h>  // For sbrk

// ---[ DEBUG ]----------------------------------------------------------------

#ifdef MLOCK_ENABLE_DEBUG
#include <stdio.h>
/**
 * Prints the given debug message.
 * @param __VA_ARGS__ Arguments passed to printf.
 */
#define DEBUG(...)                                                            \
    do {                                                                      \
        printf("%s %3d ### ", __FILE__, __LINE__);                            \
        printf(__VA_ARGS__);                                                  \
        printf("\n");                                                         \
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
static byte_t* freeList = NULL;

// ---[ HELPER FUNCTION PROTOTYPES ]-------------------------------------------

/**
 * Removes a free block from the free list and adjusts its neighbor's next and
 * prev pointers.
 * @param fp Pointer to the start of a free block's data.
 */
static void removeFreeBlock(byte_t* fp);

/**
 * Extends the heap with a new free block and inserts it into the free list.
 * @param size The number of bytes that need to be in the block's data.
 * @returns 0 on success, -1 on failure.
 */
static int extendHeap(size_t size);

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
static byte_t* findFit(word_t size);

// ---[ FUNCTION DEFINITIONS ]-------------------------------------------------

/**
 * Initialize the memory manager.
 * @returns Pointer to the start of the heap on a success, else NULL.
 */
void* initlock(void)
{
    DEBUG("Initializing memory");

    // Allocate initial heap
    word_t* heapList = sbrk(WORD_SIZE * 4);
    word_t* heapStart = heapList + 2;

    if (heapList == NULL) {
        DEBUG("Failed initial sbrk");
        return NULL;
    }

    PUT_WORD(heapList++, 0x00DECADE);
    PUT_WORD(heapList++, PACK_HEADER(0, 1));  // Prologue header
    PUT_WORD(heapList++, PACK_HEADER(0, 1));  // Prologue boundary tag
    PUT_WORD(heapList++, PACK_HEADER(0, 1));  // Epilogue header

    // extendHeap inserts the free block into freeList
    if (extendHeap(CHUNK_SIZE) == -1) {
        DEBUG("Failed to create the first free block");
        return NULL;
    }

    DEBUG("Finished initializing memory");
    return (void*)heapStart;
}

/**
 * Allocate a block of at least the given size.
 * @param size The minimum size of the block's data in bytes.
 * @returns A pointer to the start of the block's data.
 */
void* mlock(size_t size)
{
    DEBUG("Starting malloc of size %ld", size);

    if (size <= 0) {
        DEBUG("Can't malloc of size %ld", size);
        return NULL;
    }

    size = ALIGN_BYTES(size);
    byte_t* fp = findFit(size);

    if (fp != NULL) {
        place(fp, size);
        DEBUG("Placed block at %p", fp);
        return fp;
    }

    // No available blocks; extend heap to get more
    if (extendHeap(MAX(size, CHUNK_SIZE)) == -1) {
        DEBUG("Failed to extend memory by %ld bytes", size);
        return NULL;
    }

    // extendHeap put the result in freeList
    fp = freeList;

    place(fp, size);
    DEBUG("Malloc-ed extended block of size %ld at pointer %p", size, fp);
    return fp;
}

/**
 * Frees the given block by adding it to the free list.
 * @param bp Pointer to the start of a block's data.
 */
void unlock(void* bp)
{
    DEBUG("Freeing pointer %p", bp);

    word_t size = GET_SIZE(bp);
    REDO_HEADERS(bp, size, 0);

    if (GET_PREV_ALLOC(bp) == 0) {
        // Coalesce with previous
        DEBUG("Coalescing with prev");
        bp = GET_PREV_BLOCK(bp);
        size += GET_SIZE(bp) + BOUNDARY_SIZE + HEADER_SIZE;
        REDO_HEADERS(bp, size, 0);
        removeFreeBlock(bp);
    }

    byte_t* nextHeader = GET_NEXT_HEADER(bp);

    if (GET_ALLOC_FROM_HEADER(nextHeader) == 0) {
        // Coalesce with next
        DEBUG("Coalescing with next");
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
void* relock(void* ptr, size_t size)
{
    DEBUG("Reallocating pointer %p to size %ld", ptr, size);

    if (ptr == NULL) {
        DEBUG("Making new pointer");
        return mlock(size);
    }

    if (size == 0) {
        DEBUG("Freeing pointer %p", ptr);
        unlock(ptr);
        return NULL;
    }

    size = ALIGN_BYTES(size);
    word_t currentSize = GET_SIZE(ptr);

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
        byte_t* newFp = GET_NEXT_BLOCK(ptr);
        REDO_HEADERS(newFp, leftover - HEADER_SIZE - BOUNDARY_SIZE, 0);
        unlock(newFp);

        DEBUG("Shrunk and created new free block");
        return ptr;
    }

    size_t needed = size - currentSize;
    byte_t* nextBp = GET_NEXT_BLOCK(ptr);
    size_t gainedInMerge = BOUNDARY_SIZE + HEADER_SIZE + GET_SIZE(nextBp);

    if (GET_ALLOC(nextBp) == 1 || gainedInMerge < needed) {
        // Next block is not free or next block is not large enough
        byte_t* newPtr = mlock(size);

        // Copy old data over
        memcpy(newPtr, ptr, currentSize);

        unlock(ptr);
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
    byte_t* newFp = GET_NEXT_BLOCK(ptr);
    REDO_HEADERS(newFp, leftover - HEADER_SIZE - BOUNDARY_SIZE, 0);
    unlock(newFp);

    DEBUG("Absorbed part of next block and created new free block");
    return ptr;
}

static void removeFreeBlock(byte_t* fp)
{
    DEBUG("Removing free block %p", fp);
    byte_t* next = GET_NEXT_FREE(fp);
    byte_t* prev = GET_PREV_FREE(fp);

    if (fp == freeList) {
        freeList = next;
    }

    LINK_FREE(prev, next);
    DEBUG("Removed free block %p", fp);
}

static int extendHeap(size_t size)
{
    DEBUG("Extending heap with %ld bytes", size);

    size = ALIGN_BYTES(size);
    byte_t* fp = sbrk(size + BOUNDARY_SIZE + HEADER_SIZE);

    if (fp == (void*)-1) {
        DEBUG("sbrk failed to extend heap");
        return -1;
    }

    REDO_HEADERS(fp, size, 0);  // Override old epilogue with new header
    PUT_WORD(GET_NEXT_HEADER(fp), PACK_HEADER(0, 1));  // New epilogue

    // Inserts fp into freeList
    unlock(fp);
    DEBUG("Extended heap to make new block and inserted into freeList");
    return 0;
}

static void place(byte_t* fp, word_t size)
{
    DEBUG("Placing a block of size %ld at pointer %p", size, fp);

    removeFreeBlock(fp);
    size = ALIGN_BYTES(size);

    word_t availableSize = GET_SIZE(fp);
    word_t difference = availableSize - size;

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
    byte_t* newFp = GET_NEXT_BLOCK(fp);
    REDO_HEADERS(newFp, difference - HEADER_SIZE - BOUNDARY_SIZE, 0);
    unlock(newFp);
    DEBUG("Placed block and made new free block from leftovers");
}

static byte_t* findFit(word_t size)
{
    DEBUG("Searching for free block of size %ld", size);

    if (freeList == NULL) {
        DEBUG("Free list is empty");
        return NULL;
    }

    size = ALIGN_BYTES(size);

    byte_t* fp = freeList;
    for (; fp != NULL; fp = GET_NEXT_FREE(fp)) {
        if (GET_SIZE(fp) >= size) {
            DEBUG("Found pointer %p", fp);
            return fp;
        }
    }

    DEBUG("Found no block large enough");
    return NULL;
}
