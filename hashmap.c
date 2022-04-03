#include <assert.h>
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
// Entry has been evacuated to first half of larger table.
#define TOPHASH_EVACUATED_X 2
// Same as above, but evacuated to second half of larger table.
#define TOPHASH_EVACUATED_Y 3
// Cell is empty, bucket is evacuated.
#define TOPHASH_EVACUATED_EMPTY 4
// Minimum tophash for a normal filled cell.
#define TOPHASH_MIN 5

#define FLAG_SAME_SIZE_GROW 8

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
    char values[0];
} bmap;

typedef struct _hmap {
    size_t count;
    uint8_t flags;
    uint8_t B; // log2(len(buckets))
    uint8_t value_size;
    uint16_t bucket_size;
    uint16_t noverflow;

    void *buckets;
    void *oldbuckets;
    size_t nevacuate; // buckets less than this have been evacuated

    void *next_overflow;
} hmap;

// Convert B to actual length of *NORMAL* buckets.
size_t bucket_shift(uint8_t b) {
    return (size_t)1 << (b & (sizeof(size_t) * BUCKET_COUNT - 1));
}

size_t bucket_mask(uint8_t b) { return bucket_shift(b) - 1; }

bool bucket_evacuated(bmap *b) {
    uint8_t h = b->tophash[0];
    return h > TOPHASH_EMPTY_ONE && h < TOPHASH_MIN;
}

bool same_size_grow(hmap *h) { return (h->flags & FLAG_SAME_SIZE_GROW) != 0; }

size_t noldbuckets(hmap *h) {
    uint8_t oldB = h->B;
    if (!same_size_grow(h))
        oldB--;
    return bucket_shift(oldB);
}

size_t oldbucket_mask(hmap *h) { return noldbuckets(h) - 1; }

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

bool too_many_overflow_buckets(uint16_t noverflow, uint8_t B) {
    return noverflow >= (uint16_t)1 << (B > 15 ? 15 : B);
}

void make_bucket_array(hmap *h) {
    size_t base = bucket_shift(h->B);
    size_t nbuckets = base;

    // For small b, overflow is almost impossible so do not allocate extra
    // memory for overflow buckets.
    if (h->B >= 4)
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

void hash_grow(hmap *h) {
    if (over_load_factor(h->count + 1, h->B))
        h->B++;
    else
        h->flags |= FLAG_SAME_SIZE_GROW;

    h->oldbuckets = h->buckets;
    make_bucket_array(h);
    h->nevacuate = 0;
    h->noverflow = 0;
}

void advance_evacuation_mark(hmap *h, size_t nold) {
    h->nevacuate++;
    size_t stop = h->nevacuate + 1024;
    if (stop > nold)
        stop = nold;
    while (h->nevacuate != stop &&
           bucket_evacuated(h->oldbuckets + h->bucket_size * h->nevacuate))
        h->nevacuate++;
    if (h->nevacuate == nold) {
        free(h->oldbuckets);
        h->oldbuckets = NULL;
        h->flags &= ~FLAG_SAME_SIZE_GROW;
    }
}

bmap *new_overflow(hmap *h, bmap *b) {
    bmap *ovf;
    if (h->next_overflow) {
        ovf = h->next_overflow;
        if (!ovf->overflow) {
            // This is not the last preallocated overflow buckets.
            h->next_overflow = (void *)ovf + h->bucket_size;
        } else {
            ovf->overflow = NULL;
            h->next_overflow = NULL;
        }
    } else {
        ovf = calloc(h->bucket_size, 1);
    }

    if (h->B < 16) {
        h->noverflow++;
    } else {
        int mask = (1 << (h->B - 15)) - 1;
        if ((rand() & mask) == 0)
            h->noverflow++;
    }

    b->overflow = ovf;

    return ovf;
}

bool is_isolated_overflow(void *b, void *buckets, size_t bucket_size,
                          size_t nbuckets) {
    return b < buckets || b > buckets + nbuckets * bucket_size;
}

void evacuate(hmap *h, size_t oldbucket_index) {
    bmap *b = h->oldbuckets + (oldbucket_index * h->bucket_size);
    size_t nold = noldbuckets(h);

    if (!bucket_evacuated(b)) {
        typedef struct _evadst {
            bmap *b;
            size_t i;
            void *v;
        } evadst;

        evadst xy[2];
        xy[0].i = 0;
        xy[0].b = h->buckets + oldbucket_index * h->bucket_size;
        xy[0].v = xy[0].b->values;
        if (!same_size_grow(h)) {
            xy[1].i = 0;
            xy[1].b = h->buckets + (oldbucket_index + nold) * h->bucket_size;
            xy[1].v = xy[1].b->values;
        }

        for (bool is_overflow_bucket = false; b;) {
            void *v = b->values;
            for (size_t i = 0; i < BUCKET_COUNT; i++, v += h->value_size) {
                uint8_t top = b->tophash[i];
                if (tophash_is_empty(top)) {
                    b->tophash[i] = TOPHASH_EVACUATED_EMPTY;
                    continue;
                }
                uint8_t usey = 0;
                if (!same_size_grow(h)) {
                    size_t hash = hash_str(b->keys[i]);
                    if (hash & nold) {
                        usey = 1;
                    }
                }
                // Mark this entry has been evacuated.
                b->tophash[i] = TOPHASH_EVACUATED_X + usey;
                evadst *dst = &xy[usey];

                if (dst->i == BUCKET_COUNT) {
                    dst->b = new_overflow(h, dst->b);
                    dst->i = 0;
                    dst->v = dst->b->values;
                }

                dst->b->tophash[dst->i] = top;
                dst->b->keys[dst->i] = b->keys[i];
                memcpy(dst->v, v, h->value_size);

                dst->i++;
                dst->v += h->value_size;
            }

            if (!is_overflow_bucket) {
                is_overflow_bucket = true;
                b = b->overflow;
                continue;
            }

            bmap *oldb = b;
            b = b->overflow;

            // If b is isolated overflow bucket, we need to free it.
            if (h->B < 4 || is_isolated_overflow(
                                oldb, h->oldbuckets, h->bucket_size,
                                bucket_shift(h->B) + bucket_shift(h->B - 4)))
                free(oldb);
        }
    }

    if (oldbucket_index == h->nevacuate)
        advance_evacuation_mark(h, nold);
}

void grow_work(hmap *h, size_t bucket_index) {
    evacuate(h, bucket_index & oldbucket_mask(h));
    if (h->oldbuckets)
        evacuate(h, h->nevacuate);
}

void hashmap_print(map_t m) {
    if (!m) {
        printf("Uninitialized map\n");
        return;
    }

    hmap *h = m;
    size_t nbuckets = h->B >= 4 ? bucket_shift(h->B) + bucket_shift(h->B - 4)
                                : bucket_shift(h->B);
    printf("\n");
    printf("==================== Metadata ====================\n");
    printf("total count: \t%zu\n", h->count);
    printf("base buckets: \t%zu\n", bucket_shift(h->B));
    printf("total buckets: \t%zu\n", nbuckets);
    printf("noverflow: \t%d\n", h->noverflow);
    printf("nevacuate: \t%zu\n", h->nevacuate);
    printf("next overflow: \t%p\n", h->next_overflow);
    printf("==================================================\n\n");
    printf("[n]:\tbucket index\n");
    printf("?:\tisolated overflow bucket\n");
    printf(":\tvalid entry\n");
    printf("\n");

    void *nb = h->buckets;
    for (size_t n = 0; n < bucket_shift(h->B); n++, nb += h->bucket_size) {
        size_t chain_cnt = 0;
        for (bmap *b = nb; b; b = b->overflow, chain_cnt++) {
            if (chain_cnt)
                printf(" ->   ");
            if (chain_cnt == 0)
                printf("[%zu]", n);
            else {
                int64_t offset = (void *)b - h->buckets;
                if (offset > 0 && offset < nbuckets * h->bucket_size)
                    printf("[%zu]", offset / h->bucket_size);
                else
                    printf("[?]");
            }

            printf("\t");
            for (size_t i = 0; i < BUCKET_COUNT; i++) {
                uint8_t top = b->tophash[i];
                if (top < TOPHASH_MIN)
                    printf("%d  ", top);
                else
                    printf("  ");
            }
        }
        printf("\n");
    }

    if (!h->oldbuckets)
        return;

    nb = h->oldbuckets;
    uint8_t oldB = h->B;
    if (!same_size_grow(h))
        oldB -= 1;
    nbuckets = oldB >= 4 ? bucket_shift(oldB) + bucket_shift(oldB - 4)
                         : bucket_shift(oldB);

    printf("\nold buckets:\n");
    for (size_t n = 0; n < bucket_shift(oldB); n++, nb += h->bucket_size) {
        size_t chain_cnt = 0;
        for (bmap *b = nb; b; b = b->overflow, chain_cnt++) {
            if (chain_cnt)
                printf("->   ");
            if (chain_cnt == 0)
                printf("[%zu]", n);
            else {
                int64_t offset = (void *)b - h->oldbuckets;
                if (offset > 0 && offset < nbuckets * h->bucket_size)
                    printf("[%zu]", offset / h->bucket_size);
                else
                    printf("[?]");
            }

            printf("\t");
            for (size_t i = 0; i < BUCKET_COUNT; i++) {
                uint8_t top = b->tophash[i];
                if (top < TOPHASH_MIN)
                    printf("%d  ", top);
                else
                    printf("  ");
            }

            if (bucket_evacuated(nb)) {
                printf("(evacuated)");
                break;
            }
        }
        printf("\n");
    }
}

map_t _hashmap_new(uint8_t value_size, size_t hint) {
    const size_t bucket_size = sizeof(bmap) + value_size * BUCKET_COUNT;
    if (bucket_size > (uint16_t)(-1)) {
        panicf("Bucket_size(%zu) exceeds limit(%d), use pointer instead\n",
               bucket_size, (uint16_t)(-1));
    }

    hmap *h = malloc(sizeof(hmap));
    h->count = 0;
    // h->flags = 0;
    h->value_size = value_size;
    h->bucket_size = bucket_size;
    h->noverflow = 0;
    h->buckets = NULL;
    h->oldbuckets = NULL;
    h->nevacuate = 0;
    h->next_overflow = NULL;

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
    if (!h || h->count == 0)
        return MAP_MISSING;

    size_t hash = hash_str(key);
    size_t mask = bucket_mask(h->B);
    bmap *b = h->buckets + (hash & mask) * h->bucket_size;
    if (h->oldbuckets) {
        if (!same_size_grow(h))
            mask >>= 1;
        bmap *oldb = h->oldbuckets + (hash & mask) * h->bucket_size;
        if (!bucket_evacuated(oldb))
            b = oldb;
    }
    uint8_t top = tophash(hash);

    for (; b; b = b->overflow) {
        for (size_t i = 0; i < BUCKET_COUNT; i++) {
            if (b->tophash[i] != top) {
                if (b->tophash[i] == TOPHASH_EMPTY_REST)
                    return MAP_MISSING;
                continue;
            }
            if (strcmp(b->keys[i], key) == 0) {
                if (h->value_size > 0) {
                    void *value = b->values + h->value_size * i;
                    memcpy(value_ref, value, h->value_size);
                }
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

again:;
    size_t bucket_index = hash & bucket_mask(h->B);
    if (h->oldbuckets) {
        grow_work(h, bucket_index);
    }
    bmap *b = h->buckets + h->bucket_size * bucket_index;
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
                    value_write = b->values + h->value_size * i;
                }
                if (b->tophash[i] == TOPHASH_EMPTY_REST)
                    goto writekey;
                continue;
            }
            if (strcmp(b->keys[i], key) == 0) {
                // Already have a mapping for key. Update it.
                if (h->value_size > 0)
                    value_write = b->values + h->value_size * i;
                goto writevalue;
            }
        }
        bmap *ovf = b->overflow;
        if (!ovf)
            break;
        b = ovf;
    }

writekey:
    if (!h->oldbuckets && (over_load_factor(h->count + 1, h->B) ||
                           too_many_overflow_buckets(h->noverflow, h->B))) {
        hash_grow(h);
        goto again;
    }

    if (!top_write) {
        bmap *newb = new_overflow(h, b);
        top_write = &newb->tophash[0];
        key_write = &newb->keys[0];
        value_write = newb->values;
    }

    *top_write = top;
    *key_write = key;
    h->count++;

writevalue:
    if (h->value_size > 0)
        memcpy(value_write, value_ref, h->value_size);

    return MAP_OK;
}
