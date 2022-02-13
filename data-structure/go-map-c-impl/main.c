#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "crc32hash.h"

int main() {
    size_t x = 1;
    size_t y = 64;
    printf("%lu\n", x << y);
}
