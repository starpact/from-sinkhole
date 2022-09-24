run:
	mkdir -p bin
	$(CC) -g -fsanitize=address,undefined main.c hashmap.c -o bin/main && bin/main
lint:
	clang-tidy *.c
