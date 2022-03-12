#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32hash.h"
#include "hashmap.h"

void test_init_with_size_hint() {
    typedef struct _hmap {
        size_t count;
        uint8_t flags;
        uint8_t B;
        uint8_t value_size;
    } hmap;

    hmap *h = hashmap_new(size_t, 1000);
    assert(h->count == 0);
    assert(h->value_size == 8);
    assert(1 << h->B == 256);
}

void test_different_value_type() {
    // struct.
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

    // pointer.
    int x0 = 1;
    int *xp0 = &x0;
    m = hashmap_new(int *, 0);
    ret = hashmap_insert(m, "abc", &xp0);
    assert(ret == MAP_OK);
    int *xp1;
    ret = hashmap_get(m, "def", &xp1);
    assert(ret == MAP_MISSING);
    ret = hashmap_get(m, "abc", &xp1);
    assert(ret == MAP_OK);
    assert(xp0 == xp1);

    // zero-sized type.
    m = hashmap_new(struct {}, 0);
    ret = hashmap_insert(m, "abc", NULL);
    assert(ret == MAP_OK);
    ret = hashmap_get(m, "abc", NULL);
    assert(ret == MAP_OK);
    ret = hashmap_get(m, "def", NULL);
    assert(ret == MAP_MISSING);
}

char *rand_str() {
    size_t r = rand();
    size_t len = 3 + (r & 3);
    char *str = malloc((len + 1) * sizeof(char *));
    for (size_t i = 0; i < len; i++) {
        str[i] = (rand() % 26) + 'a';
    }
    str[len] = '\0';

    return str;
}

void test_basic() {
    const int cnt = 8;
    const char **keys = malloc(cnt * sizeof(char *));
    map_t m = hashmap_new(int, 0);

    for (int i = 0; i < cnt; i++) {
        keys[i] = rand_str();
        int ret = hashmap_insert(m, keys[i], &i);
        assert(ret == MAP_OK);
    }

    for (int i = 0; i < cnt; i++) {
        int x;
        int ret = hashmap_get(m, keys[i], &x);
        assert(ret == MAP_OK);
    }

    hashmap_print(m);
}

int main() {
    test_init_with_size_hint();
    test_different_value_type();

    test_basic();
}
