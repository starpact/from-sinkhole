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

typedef struct _entry {
    char *key;
    any_t value;
} entry;

typedef struct _bmap {
    uint8_t tophashes[8];
    entry entries[8];
    struct _bmap *overflow;
} bmap;

typedef struct _hmap {
    size_t count;
    uint8_t B; // log2(len(buckets))
    bmap *buckets;
    bmap *next_overflow;
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

void make_bucket_array(uint8_t b, bmap **buckets_ref,
                       bmap **next_overflow_ref) {
    size_t base = bucket_shift(b);
    size_t nbuckets = base;

    // For small b, overflow is almost impossible so do not allocate extra
    // memory for overflow buckets.
    if (b > 4) {
        // About extra 1/16 of normal buckets is allocated for overflow buckets.
        nbuckets += bucket_shift(b - 4);
        // NOTE: Remove memory allcation round up.
    }

    bmap *buckets = malloc(nbuckets * sizeof(bmap));
    *buckets_ref = buckets;

    if (base != nbuckets) {
        *next_overflow_ref = buckets + base;
        // When overflow is NULL, there are still available overflow buckets.
        // So here we need to manually mark the LAST overflow bucket.
        buckets[nbuckets - 1].overflow = buckets;
    }
}

map_t hashmap_new(size_t hint) {
    hmap *h = malloc(sizeof(hmap));
    h->count = 0;

    uint8_t B = 0;
    while (over_load_factor(hint, B)) {
        B++;
    }
    h->B = B;

    if (h->B > 0) {
        make_bucket_array(h->B, &h->buckets, &h->next_overflow);
    }

    return h;
}
