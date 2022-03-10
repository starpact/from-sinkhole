#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "crc32hash.h"
#include "hashmap.h"

void test_init_with_size_hint() {
    typedef struct _hmap {
        size_t count;
        uint8_t B;
        uint8_t value_size;
    } hmap;

    hmap *m = hashmap_new(size_t, 1000);
    assert(m->count == 0);
    assert(m->value_size == 8);
    assert(1 << m->B == 256);
}

void test_basic_type() {
    map_t m = hashmap_new(int, 0);
    char *keys[9] = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    for (int i = 0; i < sizeof(keys) / sizeof(char *); i++) {
        int ret = hashmap_insert(m, keys[i], &i);
        assert(ret == MAP_OK);
    }

    for (int i = 0; i < 8; i++) {
        int x;
        int ret = hashmap_get(m, keys[i], &x);
        assert(ret == MAP_OK);
        assert(x == i);
        printf("%s\n", keys[i]);
    }

    // Overwrite.
    int x = 100;
    int ret = hashmap_insert(m, "a", &x);
    assert(ret == MAP_OK);
    int y;
    ret = hashmap_get(m, "a", &y);
    assert(ret == MAP_OK);
    assert(x == y);
}

void test_struct() {
    typedef struct _a_t {
        int x;
        char *y;
    } a_t;

    a_t a0 = {.x = 1, .y = "biu"};
    map_t m = hashmap_new(a_t, 0);
    int ret = hashmap_insert(m, "abc", &a0);
    assert(ret == MAP_OK);

    a_t a1;
    ret = hashmap_get(m, "def", &a1);
    assert(ret == MAP_MISSING);
    ret = hashmap_get(m, "abc", &a1);
    assert(ret == MAP_OK);
    assert(a1.x == a0.x && a1.y == a0.y);
}

void test_pointer() {
    int x0 = 1;
    int *xp0 = &x0;
    map_t m = hashmap_new(int *, 0);
    int ret = hashmap_insert(m, "abc", &xp0);
    assert(ret == MAP_OK);

    int *xp1;
    ret = hashmap_get(m, "def", &xp1);
    assert(ret == MAP_MISSING);
    ret = hashmap_get(m, "abc", &xp1);
    assert(ret == MAP_OK);
    assert(xp0 == xp1);
}

void test_zst() {
    map_t m = hashmap_new(struct {}, 0);
    int ret = hashmap_insert(m, "abc", NULL);
    assert(ret == MAP_OK);

    ret = hashmap_get(m, "abc", NULL);
    assert(ret == MAP_OK);
    ret = hashmap_get(m, "def", NULL);
    assert(ret == MAP_MISSING);
}

int main() {
    // test_init_with_size_hint();
    // test_struct();
    // test_pointer();
    // test_zst();

    test_basic_type();
}
