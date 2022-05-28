#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static unsigned long crc32_tab[] = {
    0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
    0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
    0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
    0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
    0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
    0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
    0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
    0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
    0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
    0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
    0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
    0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
    0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
    0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
    0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
    0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
    0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
    0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
    0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
    0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
    0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
    0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
    0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
    0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
    0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
    0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
    0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
    0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
    0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
    0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
    0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
    0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
    0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
    0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
    0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
    0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
    0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
    0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
    0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
    0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
    0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
    0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
    0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
    0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
    0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
    0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
    0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
    0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
    0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
    0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
    0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
    0x2d02ef8dL,
};

size_t crc32(const unsigned char *s, size_t len) {
    uintptr_t crc32val = 0;

    for (size_t i = 0; i < len; i++) {
        crc32val = crc32_tab[(crc32val ^ s[i]) & 0xff] ^ (crc32val >> 8);
    }

    return crc32val;
}

size_t hash_str(const char *keystr) {
    size_t key = crc32((unsigned char *)(keystr), strlen(keystr));

    /* Robert Jenkins' 32 bit Mix Function */
    key += (key << 12);
    key ^= (key >> 22);
    key += (key << 4);
    key ^= (key >> 9);
    key += (key << 10);
    key ^= (key >> 2);
    key += (key << 7);
    key ^= (key >> 12);

    /* Knuth's Multiplicative Method */
    key = (key >> 3) * 2654435761;

    return key;
}

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
                memcpy(dst->v, v, h->value_size); // NOLINT

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
    h->flags = 0;
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
        return MAP_NOT_FOUND;

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
                    return MAP_NOT_FOUND;
                continue;
            }
            if (strcmp(b->keys[i], key) == 0) {
                if (h->value_size > 0) {
                    void *value = b->values + h->value_size * i;
                    memcpy(value_ref, value, h->value_size); // NOLINT
                }
                return MAP_OK;
            }
        }
    }

    return MAP_NOT_FOUND;
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
        memcpy(value_write, value_ref, h->value_size); // NOLINT

    return MAP_OK;
}
