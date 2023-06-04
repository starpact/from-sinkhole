run:
	mkdir -p build
	$(CC) -g -Wall -fsanitize=address,undefined test.c hashmap.c -o build/main && build/main
lint:
	clang-tidy *.c
