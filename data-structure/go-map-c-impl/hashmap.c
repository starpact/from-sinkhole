#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "crc32hash.h"
#include "hashmap.h"

const size_t BUCKET_BITS = 3;
const size_t BUCKET_COUNT = 1 << BUCKET_BITS;

const size_t LOAD_FACTOR_NUM = 13;
const size_t LOAD_FACTOR_DEN = 2;

typedef struct _bmap {
    uint8_t tophashes[8];
    struct _bmap *overflow;
    char *keys[8];
    // followed by values[8] size of which cannot be determined at compile time.
} bmap;

typedef struct _hmap {
    size_t count;
    uint8_t B; // log2(len(buckets))
    uint8_t element_size;
    void *buckets;
    void *next_overflow;
} hmap;

// Convert B to actual length of *NORMAL* buckets.
size_t bucket_shift(uint8_t b) {
    return (size_t)1 << (b & (sizeof(size_t) * 8 - 1));
}

size_t bucket_mask(uint8_t b) { return bucket_shift(b) - 1; }

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
        // NOTE: Remove memory allocation round up.
    }

    size_t bucket_size = sizeof(bmap) + h->element_size * 8;
    h->buckets = malloc(nbuckets * bucket_size);

    if (base != nbuckets) {
        h->next_overflow = h->buckets + base * bucket_size;
        // When overflow is NULL, there are still available overflow buckets.
        // So here we need to manually mark the LAST overflow bucket.
        ((bmap *)(h->buckets + (nbuckets - 1) * bucket_size))->overflow =
            h->buckets;
    }
}

map_t hashmap_new(uint8_t element_size, size_t hint) {
    hmap *h = malloc(sizeof(hmap));
    h->count = 0;
    h->element_size = element_size;

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
