/* malloc.c - MyOS C library: memory allocator.
 *
 * The heap is the free part of the program's 2 MB area: from the end of
 * the program (__bss_end) to below the stack (which grows down from the
 * command line at 0x5FFF00; 256 KB are kept for it), plus on MyOS Pi
 * 32 MB at 0x02000000.
 *
 * First-fit over a list of free blocks kept in address order, so freed
 * neighbours can be joined again. Every block starts with a 16-byte header.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../myos.h"

#define STACK_TOP   0x5FFF00UL
#define STACK_SIZE  (256 * 1024UL)
#define ALIGN       16
#define HEAP2_START 0x02000000UL    /* arm/user.h USER_HEAP .. USER_HEAP_END */
#define HEAP2_END   0x04000000UL

typedef struct block {
    size_t size;                /* whole block, header included */
    struct block *next;         /* next free block (only while free) */
} block_t;

extern char __bss_end[];
static block_t *free_list;
static int ready;

static void init(void) {
    uintptr_t start = ((uintptr_t)__bss_end + ALIGN - 1) & ~(uintptr_t)(ALIGN - 1);
    uintptr_t end = (STACK_TOP - STACK_SIZE) & ~(uintptr_t)(ALIGN - 1);
    free_list = (block_t *)start;
    free_list->size = end > start ? end - start : 0;
    free_list->next = NULL;
#if defined(__aarch64__)
    /* MyOS Pi gives programs 32 MB more at 0x02000000 (data only) */
    block_t *extra = (block_t *)HEAP2_START;
    extra->size = HEAP2_END - HEAP2_START;
    extra->next = NULL;
    free_list->next = extra;
#endif
    ready = 1;
}

size_t myos_heap_free(void) {
    if (!ready) init();
    size_t n = 0;
    for (block_t *b = free_list; b; b = b->next) n += b->size;
    return n;
}

void *malloc(size_t n) {
    if (!ready) init();
    if (n == 0) n = 1;
    if (n > ((size_t)1 << 30)) return NULL;
    size_t need = (n + sizeof(block_t) + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    block_t **link = &free_list;
    for (block_t *b = free_list; b; link = &b->next, b = b->next) {
        if (b->size < need) continue;
        if (b->size - need >= 64) {             /* split: keep the rest free */
            block_t *rest = (block_t *)((char *)b + need);
            rest->size = b->size - need;
            rest->next = b->next;
            *link = rest;
            b->size = need;
        } else {
            *link = b->next;
        }
        return (char *)b + sizeof(block_t);
    }
    return NULL;
}

void free(void *p) {
    if (!p) return;
    block_t *b = (block_t *)((char *)p - sizeof(block_t));
    block_t *prev = NULL, *cur = free_list;
    while (cur && cur < b) {                    /* find its place by address */
        prev = cur;
        cur = cur->next;
    }
    b->next = cur;
    if (prev) prev->next = b;
    else free_list = b;
    if (cur && (char *)b + b->size == (char *)cur) {        /* join the next one */
        b->size += cur->size;
        b->next = cur->next;
    }
    if (prev && (char *)prev + prev->size == (char *)b) {   /* join the previous one */
        prev->size += b->size;
        prev->next = b->next;
    }
}

void *calloc(size_t n, size_t size) {
    if (size && n > ((size_t)1 << 30) / size) return NULL;
    void *p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) {
        free(p);
        return NULL;
    }
    block_t *b = (block_t *)((char *)p - sizeof(block_t));
    size_t need = (n + sizeof(block_t) + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    if (need <= b->size) return p;              /* already big enough */
    /* grow in place if the block right after is free and big enough */
    block_t *prev = NULL, *cur = free_list;
    char *after = (char *)b + b->size;
    while (cur && (char *)cur < after) {
        prev = cur;
        cur = cur->next;
    }
    if (cur && (char *)cur == after && b->size + cur->size >= need) {
        size_t total = b->size + cur->size;
        block_t *next = cur->next;
        if (total - need >= 64) {
            block_t *rest = (block_t *)((char *)b + need);
            rest->size = total - need;
            rest->next = next;
            next = rest;
            b->size = need;
        } else {
            b->size = total;
        }
        if (prev) prev->next = next;
        else free_list = next;
        return p;
    }
    void *q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, b->size - sizeof(block_t));
    free(p);
    return q;
}

/* how much the block really holds (QuickJS uses it to count its memory) */
size_t malloc_usable_size(void *p) {
    if (!p) return 0;
    return ((block_t *)((char *)p - sizeof(block_t)))->size - sizeof(block_t);
}
