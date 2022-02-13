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

size_t bucket_shift(uint8_t B) { return 1 << (B & (sizeof(size_t) * 8 - 1)); }
size_t bucket_mask(uint8_t B) { return bucket_shift(B) - 1; }

bool over_load_factor(size_t count, uint8_t B) {
    return count > BUCKET_COUNT &&
           count > LOAD_FACTOR_NUM * (bucket_shift(B) / LOAD_FACTOR_DEN);
}

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

void make_bucket_array(uint8_t b, bmap **buckets, bmap **next_overflow) {
    size_t base = bucket_shift(b);
    size_t nbuckets = base;

    if (b > 4) {
        nbuckets += bucket_shift(b - 4);
    }

    *buckets = malloc(nbuckets * sizeof(bmap));

    if (base != nbuckets) {
        *next_overflow = *buckets + base;
        buckets[nbuckets - 1]->overflow = *buckets;
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
