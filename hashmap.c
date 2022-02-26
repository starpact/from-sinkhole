#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "crc32hash.h"
#include "hashmap.h"

#define BUCKET_BITS 3
#define BUCKET_COUNT 8

#define LOAD_FACTOR_NUM 13
#define LOAD_FACTOR_DEN 2

typedef struct _bmap {
    uint8_t tophash[BUCKET_COUNT];
    struct _bmap *overflow;
    // NOTE: Here we directly use string as keys rather than dynamic type to
    // simplify the problem because keys need extra function pointers such as
    // `hash` and `equal`.
    char *keys[BUCKET_COUNT];
    // Followed by values[BUCKET_COUNT], size of which depends on the type of
    // values stored in the map instance.
} bmap;

typedef struct _hmap {
    size_t count;
    uint8_t B; // log2(len(buckets))
    uint8_t value_size;
    void *buckets;
    void *next_overflow;
} hmap;

// Convert B to actual length of *NORMAL* buckets.
size_t bucket_shift(uint8_t b) {
    return (size_t)1 << (b & (sizeof(size_t) * BUCKET_COUNT - 1));
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
        // TODO: Memory allocation round up.
    }

    size_t bucket_size = sizeof(bmap) + h->value_size * BUCKET_COUNT;
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
    h->value_size = element_size;

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
