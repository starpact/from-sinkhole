.PHONY: build
build:
	mkdir -p build
	$(CC) -g -Wall -Wextra -fsanitize=address,undefined test.c hashmap.c -o build/main

.PHONY: test
test: build
	build/main

.PHONY: lint
lint:
	clang-tidy *.c
