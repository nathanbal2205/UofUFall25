#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

/* ---------------- Configuration ---------------- */

#define ALIGNMENT 16
#define ALIGN(sz) (((sz) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define HDRSIZE ALIGN(sizeof(header_t))   // header aligned to 16 bytes
#define FDRSIZE ALIGN(sizeof(footer_t))   // footer aligned to 16 bytes
#define MIN_BLOCK_SIZE 32                 // minimum block size for free block header and prev/next pointer

/* ---------------- Block Header ---------------- */

typedef struct header {
    size_t size;       // total size of the block including header
    int allocated;     // 0 = free, 1 = allocated
    int padding;
} header_t;

typedef struct footer {
    uint32_t size;       // block size (header + payload + footer)
    uint32_t magic;      // FOOTER_MAGIC
    void *page_end;      // pointer to end of mapped page
} footer_t;

typedef struct page_chunk {
    struct page_chunk *prev_chunk;
    struct page_chunk *next_chunk;
    size_t size;       // total size of this mapped region
    size_t page_end;
} page_chunk_t;

static page_chunk_t *page_list_head = NULL;

/* Free block prev/next pointers stored in payload */
#define FREE_PREV_PTR(bp) (*(void **)(bp))
#define FREE_NEXT_PTR(bp) (*(void **)((char *)(bp) + sizeof(void *)))

/* macros */
#define GET_ALLOC(h) ((h)->allocated)
#define SET_ALLOC(h) ((h)->allocated = 1)
#define SET_FREE(h)  ((h)->allocated = 0)
#define BLOCK_SIZE(h) ((h)->size)            // total block size including header and footer
#define PAYLOAD_SIZE(h) ((h)->size - HDRSIZE - FDRSIZE)
#define FOOTER_MAGIC 0xF00DF00DUL

/* ---------------- Global Free List ---------------- */
static void *free_list_head = NULL;

/* ---------------- Forward Declarations ---------------- */
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);
static void *find_fit(size_t asize);
static void coalesce(void *bp);
static void split_block(header_t *h, size_t asize);


static inline void write_footer(header_t *h, void *page_end) {
    footer_t *f = (footer_t *)((char *)h + BLOCK_SIZE(h) - FDRSIZE);
    f->size = BLOCK_SIZE(h);
    f->magic = FOOTER_MAGIC;
    f->page_end = page_end;
}

/* validate footer structure in mapped region */
static inline int footer_is_valid(footer_t *f) {
    return f && f->magic == FOOTER_MAGIC;
}

static void check_and_unmap_full_pages() {
    page_chunk_t *pc = page_list_head;

    while (pc) {
        // The first block after page header
        page_chunk_t *next = pc->next_chunk;
        header_t *h = (header_t *)((char *)pc + sizeof(page_chunk_t));

        // Only unmap if the block is free and fills the remaining page
        size_t remaining_size = pc->size - sizeof(page_chunk_t);
        if (!GET_ALLOC(h) && BLOCK_SIZE(h) == remaining_size) {
            // Remove from free list
            remove_free_block((char *)h + HDRSIZE);

            // Update page list
            if (pc->prev_chunk)
                pc->prev_chunk->next_chunk = pc->next_chunk;
            else
                page_list_head = pc->next_chunk;

            if (pc->next_chunk)
                pc->next_chunk->prev_chunk = pc->prev_chunk;

            // Unmap the page
            printf("[DEBUG] UNMAP This happen: %p for size: %lu\n\n", pc, pc->size);

            mem_unmap(pc, pc->size);
        }
        
        pc = next;
    }
}

/* Return the previous block's header if one exists and is valid.
   Otherwise return NULL.
*/
static header_t *get_prev_block(header_t *h) {
    footer_t *prev_f = (footer_t *)((char*)h - FDRSIZE);

    // Don't dereference prev_f until we've validated it
    printf("[DEBUG] get_prev_block: h=%p, prev_f=%p\n",
           h, prev_f);

    if (!footer_is_valid(prev_f)) {
        printf("[DEBUG] prev_f invalid or magic mismatch\n\n");
        return NULL;
    }

    size_t prev_size = prev_f->size;
    header_t *prev_h = (header_t *)((char*)h - prev_size);
    printf("[DEBUG] prev_h candidate: %p\n\n", prev_h);

    return prev_h;
}

static header_t *get_next_block(header_t *h) {
    // Use aligned footer size (FDRSIZE) consistently when locating footers
    footer_t *f = (footer_t *)((char *)h + h->size - FDRSIZE);

    // verify footer exists
    if (!footer_is_valid(f)) return NULL;

    header_t *next_h = (header_t *)((char *)h + h->size);

    printf("[DEBUG] get_next_block: h=%p, next_h=%p\n", h, next_h);

    // DO NOT WALK PAST PAGE
    if ((void*)next_h >= f->page_end) {
        printf("[DEBUG] next_h invalid\n");
        return NULL;
    }

    printf("[DEBUG] next_h candidate: %p\n\n", next_h);

    return next_h;
}



/* ---------------- Helper: Insert into free list ---------------- */
static void insert_free_block(void *bp) {
    FREE_PREV_PTR(bp) = NULL;
    FREE_NEXT_PTR(bp) = free_list_head;
    if (free_list_head)
        FREE_PREV_PTR(free_list_head) = bp;
    free_list_head = bp;
}

/* ---------------- Helper: Remove from free list ---------------- */
static void remove_free_block(void *bp) {
    void *prev = FREE_PREV_PTR(bp);
    void *next = FREE_NEXT_PTR(bp);
    if (prev)
        FREE_NEXT_PTR(prev) = next;
    else
        free_list_head = next;
    if (next)
        FREE_PREV_PTR(next) = prev;
    FREE_PREV_PTR(bp) = NULL;
    FREE_NEXT_PTR(bp) = NULL;
}

/* ---------------- Helper: Find first-fit free block ---------------- */
static void *find_fit(size_t asize) {
    void *bp = free_list_head;
    while (bp) {
        header_t *h = (header_t *)((char *)bp - HDRSIZE); // get the header
        size_t total_size = HDRSIZE + asize + FDRSIZE;    // total block size needed
        if (!GET_ALLOC(h) && h->size >= total_size) {
            return bp; // return payload pointer
        }
        bp = FREE_NEXT_PTR(bp);
    }
    return NULL;
}

/* ---------------- Helper: Split block if too large ---------------- */
static void split_block(header_t *h, size_t asize) {
    size_t block_size = BLOCK_SIZE(h);                 // total size of the block
    size_t alloc_size = HDRSIZE + asize + FDRSIZE;     // allocated block includes header + payload + footer
    size_t remaining = block_size - alloc_size;
    footer_t *f = (footer_t *)((char*)h + BLOCK_SIZE(h) - FDRSIZE);

    if (remaining >= HDRSIZE + MIN_BLOCK_SIZE + FDRSIZE) {
        // Shrink the current block to allocated size
        h->size = alloc_size;
        SET_ALLOC(h);
        write_footer(h, f->page_end);

        // Create a new free block with remaining space
        header_t *next_h = (header_t *)((char *)h + alloc_size);
        next_h->size = remaining;
        SET_FREE(next_h);
        write_footer(next_h, f->page_end);

        printf("[DEBUG] split_block: new free block created at %p | block size=%zu | payload=%zu\n",
               next_h,
               BLOCK_SIZE(next_h),
               PAYLOAD_SIZE(next_h));

        printf("[DEBUG] split_block: | allocated block header %p size: %lu | free block %p size: %lu |\n\n",
            h, h->size, next_h, next_h->size);

        insert_free_block((char *)next_h + HDRSIZE);
    } else {
        // Not enough space to split; allocate the whole block
        SET_ALLOC(h);
    }
}

/* ---------------- Helper: Coalesce adjacent free blocks ---------------- */
static void coalesce(void *bp) {
    header_t *h = (header_t *)((char *)bp - HDRSIZE);
    footer_t *f = (footer_t *)((char *)h + BLOCK_SIZE(h) - FDRSIZE);

    header_t *prev_h = get_prev_block(h);
    header_t *next_h = get_next_block(h);

    int prev_free = (prev_h && !GET_ALLOC(prev_h));
    int next_free = (next_h && !GET_ALLOC(next_h));

    printf("[DEBUG] Coalesce: GET_ALLOC prev_free: %d next_free %d\n",
        (prev_h && GET_ALLOC(prev_h)), (next_h && GET_ALLOC(next_h)));

    /* ---- Perform merges ---- */
    if (prev_free) {
        remove_free_block((char *)prev_h + HDRSIZE);
        prev_h->size += BLOCK_SIZE(h);
        h = prev_h;
    }

    if (next_free) {
        remove_free_block((char *)next_h + HDRSIZE);
        h->size += BLOCK_SIZE(next_h);
    }
    write_footer(h, f->page_end);

    insert_free_block((char *)h + HDRSIZE);

    printf("[DEBUG] Coalesce: Final coalesced block: %p | payload=%zu | size=%zu\n",
        (char *)h, PAYLOAD_SIZE(h), BLOCK_SIZE(h));

    /* ---- Check if entire page is free ---- */
    check_and_unmap_full_pages();

    printf("WHERE IS THE PROBLEM!!!\n");
}

/* ------------------ mm.c API ------------------ */
int mm_init(void) {
    free_list_head = NULL;
    return 0;
}

void *mm_malloc(size_t size) {
    if (size == 0) return NULL;

    size_t asize = ALIGN(size);               // aligned payload size
    size_t total_size = HDRSIZE + asize + FDRSIZE;
    void *bp = find_fit(asize);

    if (bp) {
        // Found a free block
        header_t *h = (header_t *)((char *)bp - HDRSIZE);

        remove_free_block(bp);                // remove from free list

        printf("[DEBUG] mm_malloc: Found Space at %p, block size=%zu for SIZE=%lu\n\n",
           bp - HDRSIZE, BLOCK_SIZE(h), size);

        // Split the block if large enough to remain free
        if (h->size >= total_size + MIN_BLOCK_SIZE) {
            split_block(h, asize);           // split off remaining free block
        }

        SET_ALLOC(h);

        return bp;
    }

    // Need to map a new page
    size_t pagesize = mem_pagesize();
    size_t mapsize = ((total_size + pagesize - 1) / pagesize) * pagesize;

    void *region = mem_map(mapsize);
    if (!region) return NULL;

    printf("[DEBUG] mm_malloc: mapped region at %p, mapsize=%zu for SIZE=%lu\n",
           region, mapsize, size);

    // Insert page_chunk at start of mapped region
    page_chunk_t *pc = (page_chunk_t *)region;
    pc->prev_chunk = NULL;
    pc->next_chunk = page_list_head;
    pc->size = mapsize;
    if (page_list_head)
        page_list_head->prev_chunk = pc;
    page_list_head = pc;

    // Place header right after page_chunk
    header_t *h = (header_t *)((char *)region + sizeof(page_chunk_t));
    h->size = total_size;
    h->allocated = 1;
    write_footer(h, (char*)region + mapsize);  // set page_end

    // If leftover space in the page, create a free block
    size_t remaining = mapsize - sizeof(page_chunk_t) - total_size;

    if (remaining >= HDRSIZE + MIN_BLOCK_SIZE + FDRSIZE) {
        header_t *free_h = (header_t *)((char *)h + total_size); // <-- no 'header_t *' here
        free_h->size = remaining;
        SET_FREE(free_h);
        write_footer(free_h, (char*)region + mapsize);
        insert_free_block((char *)free_h + HDRSIZE);

        printf("[DEBUG] mm_malloc: | page chunk header %p | allocated block header %p size: %lu | free block %p size: %lu |\n\n",
            pc, h, h->size, free_h, free_h->size);
    }
    else {
        printf("[DEBUG] mm_malloc: | page chunk header %p | allocated block header %p size: %lu |\n\n",
            pc, h, h->size);
    }

    return (char *)h + HDRSIZE; // return payload pointer
}

void mm_free(void *ptr) {
    if (!ptr) return;
    printf("[DEBUG] mm_free called with %p\n", (char *)ptr - HDRSIZE);
    header_t *h = (header_t *)((char *)ptr - HDRSIZE);
    SET_FREE(h);
    coalesce(ptr);
}
