/* 
 * mm-handout.c -  Simple allocator based on implicit free lists and 
 *                 first fit placement (similar to lecture4.pptx). 
 *                 It does not use boundary tags and does not perform
 *                 coalescing. Thus, it tends to run out of memory 
 *                 when used to allocate objects in large traces  
 *                 due to external fragmentation.
 *
 * Each block has a header of the form:
 * 
 *      31                     3  2  1  0 
 *      -----------------------------------
 *     | s  s  s  s  ... s  s  s  0  0  a/f
 *      ----------------------------------- 
 * 
 * where s are the meaningful size bits and a/f is set 
 * iff the block is allocated. The list has the following form:
 *
 * begin                                                         end
 * heap                                                          heap  
 *  -----------------------------------------------------------------   
 * |  pad   | hdr(8:a) |   pad   | zero or more usr blks | hdr(8:a) |
 *  -----------------------------------------------------------------
 *    four  | prologue |  four   |                       | epilogue |
 *    bytes | block    |  bytes  |                       | block    |
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "mm.h"
#include "memlib.h"


/* Team structure */
team_t team = {
	/* Team name */
	"ateam",
	/* note that we will add a 10% bonus for
	* working alone */
	/* the maximum number of members per team
	 * is three */
	/* First member's full name */
	"Motassem Al-Tarazi",
	/* First member's email address */
	"altarazi@unl.edu",
	/* Second member's full name (leave
	* blank if none) */
	"",
	/* Second member's email address
	* (leave blank if none) */
	""
};


/* $begin mallocmacros */
/* Basic constants and macros */

/* You can add more macros and constants in this section */
#define WSIZE       4       /* word size (bytes) */  
#define DSIZE       8       /* doubleword size (bytes) */
#define CHUNKSIZE  (1<<12)  /* initial heap size (bytes) */
#define OVERHEAD    4       /* overhead of header (bytes) */

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)       (*(size_t *)(p))
#define PUT(p, val)  (*(size_t *)(p) = (val))  

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header*/
#define HDRP(bp)       ((char *)(bp) - WSIZE)  

/* Given block ptr bp, compute address of next block */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char*)(bp) - WSIZE)))

/* $end mallocmacros */


/* Global variables */
static char *heap_listp;  /* pointer to first block */  

/* function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void printblock(void *bp);

/* 
 * mm_init - Initialize the memory manager 
 */
/* $begin mminit */
int mm_init(void) 
{
   /* create the initial empty heap */
   if ((heap_listp = mem_sbrk(4*WSIZE)) == NULL)
		return -1;
   PUT(heap_listp, KEY);               /* alignment padding */
   PUT(heap_listp+WSIZE, PACK(DSIZE, 0));  /* prologue header */ 
   PUT(heap_listp+DSIZE, PACK(0, 0));  /* empty word*/ 
   PUT(heap_listp+DSIZE+WSIZE, PACK(0, 0));   /* epilogue header */
   heap_listp += (DSIZE);


   /* Extend the empty heap with a free block of CHUNKSIZE bytes */
   if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
		return -1;
   return (int) heap_listp;
}
/* $end mminit */

/* 
 * mm_malloc - Allocate a block with at least size bytes of payload 
 */
/* $begin mmmalloc */
void *mm_malloc(size_t size) 
{
    size_t asize;      /* adjusted block size */
    size_t extendsize; /* amount to extend heap if no fit */
    char *bp;      
	//	printf("call mm_malloc\n");

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
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
	 {
	 	printf("mm_malloc = NULL\n");
		return NULL;
	 }
	 //printf("return address = %p\n", bp);
    place(bp, asize);
	 //mm_checkheap(1);
    return bp;
} 
/* $end mmmalloc */

/* 
 * mm_free - Free a block 
 */
/* $begin mmfree */
void mm_free(void *bp)
{
	//printf("call mm_free\n");

	/* You need to implement this function */
}

/* $end mmfree */

/*
 * mm_realloc - naive implementation of mm_realloc
 */
void *mm_realloc(void *ptr, size_t size)
{
	/* You need to implement this function. */
	return NULL;
}

/* 
 * mm_checkheap - Check the heap for consistency 
 */
void mm_checkheap(int verbose) 
{
	char *bp = heap_listp;

	if (verbose)
		printf("Heap (%p):\n", heap_listp);
	if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || GET_ALLOC(HDRP(heap_listp)))
		printf("Bad prologue header\n");

	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		if (verbose)
			printblock(bp);
	}

	if (verbose)
		printblock(bp);
	if ((GET_SIZE(HDRP(bp)) != 0) || (GET_ALLOC(HDRP(bp))))
		printf("Bad epilogue header\n");
}

/* The remaining routines are internal helper routines */

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
/* $begin mmextendheap */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;
	
    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk(size)) == (void *)-1) 
		return NULL;

    /* Initialize free block header and the epilogue header */
    PUT(HDRP(bp), PACK(size, 1));         /* free block header */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0)); /* new epilogue header */
    return bp;
}
/* $end mmextendheap */

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

   if ((csize - asize) >= (DSIZE)) { 
	PUT(HDRP(bp), PACK(asize, 0));
	bp = NEXT_BLKP(bp);
	PUT(HDRP(bp), PACK(csize-asize, 1));
    }
    else { 
	PUT(HDRP(bp), PACK(csize, 0));
    }
}
/* $end mmplace */

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void *find_fit(size_t asize)
{
    /* first fit search */
    void *bp;

    for (bp = heap_listp; GET_SIZE(bp-WSIZE) > 0; bp = NEXT_BLKP(bp)) {
		if (GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
	    	return bp;
		}
    }
    return NULL; /* no fit */
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
