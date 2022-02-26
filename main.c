#include <stddef.h>
#include <stdio.h>

#include "crc32hash.h"
#include "hashmap.h"

typedef struct _hmap {
    size_t count;
    uint8_t B;
    uint8_t value_size;
} hmap;

int main() {
    hmap *m = hashmap_new(8, 1000);
    printf("count: %lu\n", m->count);
    printf("element_size: %d\n", m->value_size);
    printf("buckets: %d\n", 1 << m->B);
}
