#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

#include <execinfo.h>

#include "mm.h"
#include "memlib.h"

/* ---------------- Configuration ---------------- */

#define ALIGNMENT 16
#define ALIGN(sz) (((sz) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define HDRSIZE ALIGN(sizeof(header_t))   // header aligned to 16 bytes
#define FDRSIZE ALIGN(sizeof(footer_t))   // footer aligned to 16 bytes
#define MIN_BLOCK_SIZE 16                 // minimum block size for free block header and prev/next pointer

/* ---------------- Block Header ---------------- */

typedef struct header {
    size_t size;       // total size of the block including header
    int allocated;     // 0 = free, 1 = allocated
    int padding;
} header_t;

typedef struct footer {
    size_t size;       // block size (header + payload + footer)
    size_t padding;
} footer_t;

typedef struct page_chunk {
    struct page_chunk *prev_chunk;
    struct page_chunk *next_chunk;
    size_t page_size;
    void *page_end;
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
static header_t *get_prev_block(header_t *h);
static header_t *get_next_block(header_t *h);



static void dump_page_list(void) {
    page_chunk_t *pc = page_list_head;
    fprintf(stderr, "DUMP page_list:\n");
    int i = 0;
    while (pc) {
        fprintf(stderr, "  [%02d] pc=%p prev=%p next=%p page_size=%zu page_end=%p\n",
                i++, pc, pc->prev_chunk, pc->next_chunk, pc->page_size, pc->page_end);
        pc = pc->next_chunk;
    }
}
static void dump_free_list(void) {
    void *bp = free_list_head;
    fprintf(stderr, "DUMP free_list:\n");
    int i = 0;
    while (bp) {
        void *prev = FREE_PREV_PTR(bp);
        void *next = FREE_NEXT_PTR(bp);
        header_t *h = (header_t *)((char *)bp - HDRSIZE);
        fprintf(stderr, "  [%02d] bp=%p header=%p size=%zu alloc=%d prev=%p next=%p\n",
                i++, bp, h, (size_t)h->size, h->allocated, prev, next);
        bp = next;
        if (i > 200) { fprintf(stderr, "  ... free_list too long, stopping dump\n"); break; }
    }
}



static void check_and_unmap_full_pages() {
    page_chunk_t *pc = page_list_head;

    while (pc) {
        // The first block after page header
        page_chunk_t *next = pc->next_chunk;
        header_t *h = (header_t *)((char *)pc + sizeof(page_chunk_t));
        size_t page_size = pc->page_size;

        // Only unmap if the block is free and fills the remaining page
        size_t remaining_size = page_size - sizeof(page_chunk_t);
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
            // printf("[DEBUG] UNMAP This happen: %p for size: %lu\n\n", pc, pc->size);

            mem_unmap(pc, page_size);
        }

        pc = next;
    }
}

static page_chunk_t *find_page_chunk_for_addr(void *addr) {
    page_chunk_t *pc = page_list_head;
    while (pc) {
        void *page_start = (char *)pc + sizeof(page_chunk_t);
        void *page_end = pc->page_end;
        if (addr >= page_start && addr < page_end) return pc;
        pc = pc->next_chunk;
    }
    return NULL;
}

static inline void write_footer(header_t *h) {
    footer_t *f = (footer_t *)((char *)h + BLOCK_SIZE(h) - FDRSIZE);

    /* optional sanity check: ensure f lies inside a mapped page */
    page_chunk_t *pc = find_page_chunk_for_addr((void *)h);
    if (pc) {
        void *page_start = (char *)pc + sizeof(page_chunk_t);
        void *page_end = pc->page_end;
        if ((void *)f < page_start || (void *)f + sizeof(footer_t) > page_end) {
            fprintf(stderr, "[ERROR] write_footer would write outside page: f=%p page_start=%p page_end=%p\n",
                    (void*)f, page_start, page_end);
            abort();
        }
    }

    f->size = BLOCK_SIZE(h);
}
static inline void write_footer_debug(header_t *h) {
    footer_t *f = (footer_t *)((char *)h + BLOCK_SIZE(h) - FDRSIZE);

    /* Find page chunk that contains header h */
    page_chunk_t *pc;
    // fprintf(stderr, "[DEBUG] write_footer: header=%p BLOCK_SIZE=%zu\n", (void*)h, (size_t)BLOCK_SIZE(h));

    /* iterate known pages and print ranges (use char* for comparisons) */
    for (pc = page_list_head; pc != NULL; pc = pc->next_chunk) {
        char *page_start = (char *)pc;
        char *page_payload_start = (char *)pc + sizeof(page_chunk_t);
        char *page_end = (char *)pc->page_end;
    }

    /* then print a more specific local check to find which page contains h */
    for (pc = page_list_head; pc != NULL; pc = pc->next_chunk) {
        char *payload_start = (char *)pc + sizeof(page_chunk_t);
        char *page_end = (char *)pc->page_end;
        if ((char *)h >= payload_start && (char *)h < page_end) {
            // fprintf(stderr, "[DEBUG] header %p is inside page %p (payload_start=%p page_end=%p)\n",
            //         (void*)h, (void*)pc, (void*)payload_start, (void*)page_end);
            break;
        }
    }
    if (!pc) {
        fprintf(stderr, "[ERROR] write_footer: could not find page_chunk for header %p\n", (void*)h);
        /* dump globals for more context */
        // dump_page_list();
        // dump_free_list();
        abort();
    }

    char *page_payload_start = (char *)pc + sizeof(page_chunk_t);
    char *page_end = (char *)pc->page_end;

    /* If footer would be outside page bounds, print diagnostics and abort */
    if ((char *)f < page_payload_start || ((char *)f + sizeof(footer_t)) > page_end) {
        fprintf(stderr, "[ERROR] write_footer would write outside page: f=%p page_start=%p page_end=%p\n",
                (void*)f, (void*)page_payload_start, (void*)page_end);
        fprintf(stderr, "  header h=%p BLOCK_SIZE(h)=%zu HDRSIZE=%zu FDRSIZE=%zu\n",
                (void*)h, (size_t)BLOCK_SIZE(h), (size_t)HDRSIZE, (size_t)FDRSIZE);
        fprintf(stderr, "  page_chunk pc=%p page_size=%zu page_end=%p\n", (void*)pc, pc->page_size, (void*)pc->page_end);

        header_t *prev_h = get_prev_block(h);
        header_t *next_h = get_next_block(h);
        fprintf(stderr, "  prev_h=%p next_h=%p\n", (void*)prev_h, (void*)next_h);

        // dump_page_list();
        // dump_free_list();

        void *bt[32];
        int n = backtrace(bt, 32);
        fprintf(stderr, "Backtrace (%d frames):\n", n);
        backtrace_symbols_fd(bt, n, fileno(stderr));

        abort();
    }

    /* Normal case: write footer */
    f->size = BLOCK_SIZE(h);
}

/* Return the previous block's header if one exists and is valid.
   Otherwise return NULL.  This version uses page_chunk bounds checks
   (via find_page_chunk_for_addr) and does not dereference footers that
   lie outside the containing mapped page. */
static header_t *get_prev_block(header_t *h) {
    /* Find the page chunk containing this header */
    page_chunk_t *pc = find_page_chunk_for_addr((void *)h);
    if (!pc) return NULL;

    void *page_start = (char *)pc + sizeof(page_chunk_t);
    void *page_end = pc->page_end;

    /* location of the previous footer (use aligned footer size) */
    footer_t *prev_f = (footer_t *)((char *)h - FDRSIZE);

    /* Ensure the footer pointer lies within the mapped page region */
    if ((void *)prev_f < page_start || (void *)prev_f + sizeof(footer_t) > page_end) {
        /* previous footer would be outside this page -> no prev block in this page */
        return NULL;
    }

    /* now safe to read prev_f->size */
    size_t prev_size = prev_f->size;

    /* Basic sanity checks on prev_size */
    if (prev_size < (HDRSIZE + FDRSIZE) || prev_size > (size_t)(page_end - page_start)) {
        return NULL;
    }

    header_t *prev_h = (header_t *)((char *)h - prev_size);

    /* Ensure prev_h lies inside this page */
    if ((void *)prev_h < page_start || (void *)prev_h + sizeof(header_t) > page_end) {
        return NULL;
    }

    return prev_h;
}

/* Return the next block's header if one exists and is valid.
   Uses page_chunk bounds checks to avoid reading footers outside mapped pages. */
static header_t *get_next_block(header_t *h) {
    /* Find the page chunk containing this header */
    page_chunk_t *pc = find_page_chunk_for_addr((void *)h);
    if (!pc) return NULL;

    void *page_start = (char *)pc + sizeof(page_chunk_t);
    void *page_end = pc->page_end;

    /* Compute footer position for this block (aligned FDRSIZE) */
    footer_t *f = (footer_t *)((char *)h + h->size - FDRSIZE);

    /* Ensure the footer lies inside the page so it's safe to read its contents */
    if ((void *)f < page_start || (void *)f + sizeof(footer_t) > page_end) {
        return NULL;
    }

    /* compute the candidate next header */
    header_t *next_h = (header_t *)((char *)h + h->size);

    /* Ensure next_h does not walk past the page end */
    if ((void *)next_h >= page_end) {
        return NULL;
    }

    /* Optionally, do a lightweight sanity check: next_h must be at least HDRSIZE away
       from page_start and its header structure should fit in page bounds. */
    if ((void *)next_h < page_start || (void *)next_h + sizeof(header_t) > page_end) {
        return NULL;
    }

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

    if (remaining >= HDRSIZE + MIN_BLOCK_SIZE + FDRSIZE) {
        // Shrink the current block to allocated size
        h->size = alloc_size;
        SET_ALLOC(h);
        write_footer_debug(h);

        // Create a new free block with remaining space
        header_t *next_h = (header_t *)((char *)h + alloc_size);
        next_h->size = remaining;
        SET_FREE(next_h);
        write_footer_debug(next_h);

        // printf("[DEBUG] split_block: new free block created at %p | block size=%zu | payload=%zu\n",
        //        next_h,
        //        BLOCK_SIZE(next_h),
        //        PAYLOAD_SIZE(next_h));

        // printf("[DEBUG] split_block: | allocated block header %p size: %lu | free block %p size: %lu |\n\n",
        //     h, h->size, next_h, next_h->size);

        insert_free_block((char *)next_h + HDRSIZE);
    } else {
        // Not enough space to split; allocate the whole block
        SET_ALLOC(h);
    }
}

/* ---------------- Helper: Coalesce adjacent free blocks ---------------- */
static void coalesce(void *bp) {
    header_t *h = (header_t *)((char *)bp - HDRSIZE);

    header_t *prev_h = get_prev_block(h);
    header_t *next_h = get_next_block(h);

    int prev_free = (prev_h && !GET_ALLOC(prev_h));
    int next_free = (next_h && !GET_ALLOC(next_h));

    // printf("[DEBUG] Coalesce: GET_ALLOC prev_free: %d next_free %d\n",
    //     (prev_h && GET_ALLOC(prev_h)), (next_h && GET_ALLOC(next_h)));

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
    write_footer_debug(h);

    insert_free_block((char *)h + HDRSIZE);

    // printf("[DEBUG] Coalesce: Final coalesced block: %p | payload=%zu | size=%zu\n",
    //     (char *)h, PAYLOAD_SIZE(h), BLOCK_SIZE(h));

    /* ---- Check if entire page is free ---- */
    check_and_unmap_full_pages();
}

/* ------------------ mm.c API ------------------ */
int mm_init(void) {
    // printf("==== mm_init has been CALLED! The it BEGIN!!!!!!!! ====\n\n");
    free_list_head = NULL;
    page_list_head = NULL;
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

        // printf("[DEBUG] mm_malloc: Found Space at %p, block size=%zu for SIZE=%lu\n\n",
        //    bp - HDRSIZE, BLOCK_SIZE(h), size);

        // Split the block if large enough to remain free
        if (h->size >= total_size + MIN_BLOCK_SIZE) {
            split_block(h, asize);           // split off remaining free block
        }

        SET_ALLOC(h);

        return bp;
    }

    // Need to map a new page
    size_t pagesize = mem_pagesize();
    size_t need = total_size + sizeof(page_chunk_t);
    size_t mapsize = ((need + pagesize - 1) / pagesize) * pagesize;

    void *region = mem_map(mapsize);
    if (!region) return NULL;

    // printf("[DEBUG] mm_malloc: mapped region at %p, mapsize=%zu for SIZE=%lu\n",
    //        region, mapsize, size);

    // Insert page_chunk at start of mapped region
    page_chunk_t *pc = (page_chunk_t *)region;
    pc->prev_chunk = NULL;
    pc->next_chunk = page_list_head;
    pc->page_size = mapsize;
    pc->page_end = (char *)region + mapsize;
    if (page_list_head)
        page_list_head->prev_chunk = pc;
    page_list_head = pc;

    void *payload_start = (char*)region + sizeof(page_chunk_t);
    void *page_end = (char*)region + mapsize;

    /* Sanity: ensure there's at least room for header/footer/payload for the requested total_size */
    if ((size_t)(page_end - payload_start) < total_size) {
        fprintf(stderr, "BUG: usable page bytes %zu < requested total_size %zu\n",
                (size_t)(page_end - payload_start), total_size);
        abort();
    }

    // printf("[DEBUG] mm_malloc: page_insert: pc=%p prev=%p next=%p page_end=%p size=%zu\n",
    //        pc, pc->prev_chunk, pc->next_chunk, (void*)pc->page_end, pc->size);

    // Place header right after page_chunk
    header_t *h = (header_t *)((char *)region + sizeof(page_chunk_t));
    if ((char *)h < payload_start || ((char *)h + HDRSIZE) > page_end) {
        fprintf(stderr, "BUG: initial header out of payload range: h=%p payload_start=%p page_end=%p\n",
                (void *)h, (void *)payload_start, (void *)page_end);
        abort();
    }
    h->size = total_size;
    h->allocated = 1;
    write_footer_debug(h);  // set page_end

    // If leftover space in the page, create a free block
    size_t remaining = mapsize - sizeof(page_chunk_t) - total_size;

    if (remaining >= HDRSIZE + MIN_BLOCK_SIZE + FDRSIZE) {
        header_t *free_h = (header_t *)((char *)h + total_size); // <-- no 'header_t *' here
        if ((char *)free_h < payload_start || ((char *)free_h + FDRSIZE) > page_end) {
        fprintf(stderr, "BUG: free_h out of page bounds: free_h=%p free_h+FDRSIZE=%p page_end=%p remaining=%zu\n",
                (void *)free_h, (void *)((char *)free_h + FDRSIZE), (void *)page_end, remaining);
        abort();
    }
        if ((char*)free_h < payload_start || (char*)free_h + FDRSIZE > page_end) {
        fprintf(stderr, "BUG: free_h out of page bounds: free_h=%p free_h+FDRSIZE=%p page_end=%p remaining=%zu\n",
                free_h, (char*)free_h + FDRSIZE, page_end, remaining);
        abort();
}
        free_h->size = remaining;
        SET_FREE(free_h);
        write_footer_debug(free_h);
        insert_free_block((char *)free_h + HDRSIZE);

        // printf("[DEBUG] mm_malloc: | page chunk header %p | allocated block header %p size: %lu | free block %p size: %lu |\n\n",
        //     pc, h, h->size, free_h, free_h->size);
    }
    else {
        // printf("[DEBUG] mm_malloc: | page chunk header %p | allocated block header %p size: %lu |\n\n",
        //     pc, h, h->size);
    }

    return (char *)h + HDRSIZE; // return payload pointer
}

void mm_free(void *ptr) {
    if (!ptr) return;
    // printf("[DEBUG] mm_free called with %p\n", (char *)ptr - HDRSIZE);
    header_t *h = (header_t *)((char *)ptr - HDRSIZE);
    SET_FREE(h);
    coalesce(ptr);
}
