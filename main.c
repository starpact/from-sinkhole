#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "crc32hash.h"
#include "hashmap.h"

typedef struct _hmap {
    size_t count;
    uint8_t B;
    uint8_t value_size;
} hmap;

void test_new() {
    hmap *m = hashmap_new(8, 1000);
    assert(m->count == 0);
    assert(m->value_size == 8);
    assert(1 << m->B == 256);
}

void test_insert_get() {
    int X = 1;
    hmap *m = hashmap_new(sizeof(int), 0);
    int ok = hashmap_insert(m, "abc", &X);
    assert(ok == MAP_OK);
    printf("%d\n", X);

    int x;
    ok = hashmap_get(m, "abc", &x);
    assert(ok == MAP_OK);
    assert(x == X);
}

int main() { test_insert_get(); }
