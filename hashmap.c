#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "crc32hash.h"
#include "hashmap.h"

#define BUCKET_BITS 3
#define BUCKET_COUNT 8

#define LOAD_FACTOR_NUM 13
#define LOAD_FACTOR_DEN 2

// this cell is empty, no more non-empty cells at higher indexes
// or overflows.
#define TOPHASH_EMPTY_REST 0
#define TOPHASH_EMPTY 1
#define TOPHASH_MIN 5

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

bool tophash_is_empty(uint8_t top) { return top <= TOPHASH_EMPTY; }

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
    if (h->B > 4) {
        // About extra 1/16 of normal buckets is allocated for overflow buckets.
        nbuckets += bucket_shift(h->B - 4);
        // TODO: Memory allocation round up.
    }

    h->buckets = calloc(nbuckets, h->bucket_size);

    if (base != nbuckets) {
        h->next_overflow = h->buckets + base * h->bucket_size;
        // When overflow is NULL, there are still available overflow buckets.
        // So here we need to manually mark the LAST overflow bucket.
        ((bmap *)(h->buckets + (nbuckets - 1) * h->bucket_size))->overflow =
            h->buckets;
    }
}

map_t hashmap_new(uint8_t value_size, size_t hint) {
    const size_t bucket_size = sizeof(bmap) + value_size * BUCKET_COUNT;
    if (bucket_size > (uint16_t)(-1)) {
        printf("Bucket_size(%zu) exceeds limit(%d), consider using pointer\n",
               bucket_size, (uint16_t)(-1));
        exit(EXIT_FAILURE);
    }

    hmap *h = malloc(sizeof(hmap));

    h->bucket_size = bucket_size;
    h->count = 0;
    h->value_size = value_size;

    uint8_t B = 0;
    while (over_load_factor(hint, B)) {
        B++;
    }
    h->B = B;

    if (h->B > 0) {
        make_bucket_array(h);
    }

    return h;
}

int hashmap_get(map_t m, const char *key, void *value_ref) {
    hmap *h = m;
    if (h == NULL || h->count == 0) {
        return MAP_MISSING;
    }
    size_t hash = hash_str(key);
    size_t mask = bucket_mask(h->B);
    bmap *b = h->buckets + h->bucket_size * (hash & mask);
    uint8_t top = tophash(hash);

    for (; b != NULL; b = b->overflow) {
        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            if (top != b->tophash[i]) {
                if (b->tophash[i] == TOPHASH_EMPTY_REST) {
                    return MAP_MISSING;
                }
                continue;
            }
            if (key == b->keys[i]) {
                if (h->value_size == 0) {
                    return MAP_OK;
                }
                void *value = (void *)(b + 1) + h->value_size * i;
                memcpy(value_ref, value, h->value_size);
                return MAP_OK;
            }
        }
    }

    return MAP_MISSING;
}

int hashmap_insert(map_t m, const char *key, const void *value_ref) {
    if (!m) {
        printf("Map uninitialized");
        exit(EXIT_FAILURE);
    }

    hmap *h = m;
    size_t hash = hash_str(key);
    if (!h->buckets) {
        h->buckets = calloc(1, h->bucket_size);
    }
    size_t bucket = hash & bucket_mask(h->B);
    bmap *b = h->buckets + h->bucket_size * bucket;
    uint8_t top = tophash(hash);
    for (;;) {
        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            if (b->tophash[i] != top) {
                if (tophash_is_empty(b->tophash[i])) {
                    b->tophash[i] = top;
                    b->keys[i] = key;
                    void *value = (void *)(b + 1) + h->bucket_size * i;
                    memcpy(value, value_ref, h->value_size);
                    h->count++;
                    return MAP_OK;
                }
            }
        }
    }

    return MAP_OK;
}
