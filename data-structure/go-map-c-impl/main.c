#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "crc32hash.h"
#include "hashmap.h"

typedef struct _hmap {
    size_t count;
    uint8_t B;
} hmap;

int main() {
    hmap *m = hashmap_new(105);
    printf("%lu\n", m->count);
    printf("%d\n", m->B);
}
