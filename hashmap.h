#ifndef __HASHMAP_H__
#define __HASHMAP_H__

#include <stddef.h>
#include <stdint.h>

typedef void *map_t;

#define MAP_NOT_FOUND -2 // No such element
#define MAP_OOM -1     // Out of Memory
#define MAP_OK 0       // Ok

#define hashmap_new(value_type, hint) _hashmap_new(sizeof(value_type), hint)

map_t _hashmap_new(uint8_t value_size, size_t hint);

void hashmap_free(map_t m);

void hashmap_print(map_t m);

size_t hashmap_len(map_t m);

int hashmap_get(map_t m, const char *key, void *value_ref);

int hashmap_insert(map_t m, const char *key, const void *value_ref);

int hashmap_remove(map_t m, const char *key, void *value_ref);

#endif
