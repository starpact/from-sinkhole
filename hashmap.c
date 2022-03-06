#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32hash.h"
#include "hashmap.h"

#define DEBUG // debug enabled

#define BUCKET_BITS 3
#define BUCKET_COUNT 8

#define LOAD_FACTOR_NUM 13
#define LOAD_FACTOR_DEN 2

// This cell is empty, and there is no more non-empty cells at higher indexes or
// overflows.
#define TOPHASH_EMPTY_REST 0
// This cell is empty.
#define TOPHASH_EMPTY_ONE 1
#define TOPHASH_MIN 5

#define panicf(...)                                                            \
    do {                                                                       \
        fprintf(stderr, "[PANIC] %s: %d | ", __FILE__, __LINE__);              \
        fprintf(stderr, __VA_ARGS__);                                          \
        fflush(stderr);                                                        \
        exit(EXIT_FAILURE);                                                    \
    } while (0);

#ifdef DEBUG
#define debugf(...)                                                            \
    do {                                                                       \
        fprintf(stdout, "[DEBUG] %s: %d | ", __FILE__, __LINE__);              \
        fprintf(stdout, __VA_ARGS__);                                          \
        fflush(stdout);                                                        \
    } while (0);
#else
#define debugf(...)                                                            \
    do {                                                                       \
    } while (0);
#endif

typedef struct _bmap {
    uint8_t tophash[BUCKET_COUNT];
    struct _bmap *overflow;
    // NOTE: Here we directly use string as keys rather than dynamic type to
    // simplify the problem because keys need extra function pointers such as
    // `hash` and `equal`.
    const char *keys[BUCKET_COUNT];
    // Followed by values[BUCKET_COUNT], size of which depends on the type of
    // values stored in the map instance.
} bmap;

typedef struct _hmap {
    size_t count;
    uint8_t B; // log2(len(buckets))
    uint8_t value_size;
    uint16_t bucket_size;
    void *buckets;
    void *old_buckets;
    void *next_overflow;
} hmap;

// Convert B to actual length of *NORMAL* buckets.
size_t bucket_shift(uint8_t b) {
    return (size_t)1 << (b & (sizeof(size_t) * BUCKET_COUNT - 1));
}

size_t bucket_mask(uint8_t b) { return bucket_shift(b) - 1; }

uint8_t tophash(size_t hash) {
    uint8_t top = hash >> (sizeof(size_t) * 8 - 8);
    return top < TOPHASH_MIN ? top + TOPHASH_MIN : top;
}

bool tophash_is_empty(uint8_t top) { return top <= TOPHASH_EMPTY_ONE; }

bool over_load_factor(size_t count, uint8_t B) {
    // When there are less then 8 elements, there should be 1 bucket and B
    // should be 0 so first judge is needed.
    return count > BUCKET_COUNT &&
           // Division is calculated before multiplication first to avoid
           // unnecessary overflow.
           count > LOAD_FACTOR_NUM * (bucket_shift(B) / LOAD_FACTOR_DEN);
}

void make_bucket_array(hmap *h) {
    size_t base = bucket_shift(h->B);
    size_t nbuckets = base;

    // For small b, overflow is almost impossible so do not allocate extra
    // memory for overflow buckets.
    if (h->B > 4)
        // About extra 1/16 of normal buckets is allocated for overflow buckets.
        nbuckets += bucket_shift(h->B - 4);

    h->buckets = calloc(nbuckets, h->bucket_size);

    if (base != nbuckets) {
        h->next_overflow = h->buckets + base * h->bucket_size;
        // When overflow is NULL, there are still available overflow buckets.
        // So here we need to manually mark the LAST overflow bucket.
        ((bmap *)(h->buckets + (nbuckets - 1) * h->bucket_size))->overflow =
            h->buckets;
    }
}

map_t _hashmap_new(uint8_t value_size, size_t hint) {
    const size_t bucket_size = sizeof(bmap) + value_size * BUCKET_COUNT;
    if (bucket_size > (uint16_t)(-1)) {
        panicf("Bucket_size(%zu) exceeds limit(%d), use pointer instead\n",
               bucket_size, (uint16_t)(-1));
    }

    hmap *h = malloc(sizeof(hmap));

    h->bucket_size = bucket_size;
    h->count = 0;
    h->value_size = value_size;

    uint8_t B = 0;
    while (over_load_factor(hint, B))
        B++;
    h->B = B;

    if (h->B > 0)
        make_bucket_array(h);

    return h;
}

int hashmap_get(map_t m, const char *key, void *value_ref) {
    hmap *h = m;
    if (h == NULL || h->count == 0)
        return MAP_MISSING;

    size_t hash = hash_str(key);
    size_t mask = bucket_mask(h->B);
    bmap *b = h->buckets + h->bucket_size * (hash & mask);
    uint8_t top = tophash(hash);

    for (; b != NULL; b = b->overflow) {
        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            if (top != b->tophash[i]) {
                if (b->tophash[i] == TOPHASH_EMPTY_REST)
                    return MAP_MISSING;
                continue;
            }
            if (strcmp(b->keys[i], key) == 0) {
                void *value = (void *)(b + 1) + h->value_size * i;
                memcpy(value_ref, value, h->value_size);
                return MAP_OK;
            }
        }
    }
    return MAP_MISSING;
}

int hashmap_insert(map_t m, const char *key, const void *value_ref) {
    if (!m)
        panicf("Map uninitialized\n");
    hmap *h = m;
    if (!value_ref && h->value_size != 0)
        panicf("Value pointer must not be NULL if value size is not zero\n");

    if (!h->buckets)
        h->buckets = calloc(1, h->bucket_size);

    size_t hash = hash_str(key);
    size_t bucket = hash & bucket_mask(h->B);
    bmap *b = h->buckets + h->bucket_size * bucket;
    uint8_t top = tophash(hash);

    uint8_t *top_write = NULL;
    const char **key_write = NULL;
    void *value_write = NULL;

    for (;;) {
        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            if (b->tophash[i] != top) {
                if (tophash_is_empty(b->tophash[i]) && !top_write) {
                    top_write = &(b->tophash[i]);
                    key_write = &b->keys[i];
                    value_write = (void *)(b + 1) + h->value_size * i;
                }
                if (b->tophash[i] == TOPHASH_EMPTY_REST)
                    goto writekey;
                continue;
            }
            if (strcmp(b->keys[i], key))
                continue;
            // Already have a mapping for key. Update it.
            value_write = (void *)(b + 1) + h->value_size * i;
            goto writevalue;
        }
        bmap *ovf = b->overflow;
        if (!ovf)
            break;
        b = ovf;
    }

writekey:
    if (!h->old_buckets && (over_load_factor(h->count, h->B)))
        panicf("TODO: hashgrow");

    if (!top_write)
        panicf("TODO: newoverflow");

    *top_write = top;
    *key_write = key;
    h->count++;

writevalue:
    memcpy(value_write, value_ref, h->value_size);

    return MAP_OK;
}
