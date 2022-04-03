run:
	clang -fsanitize=address,undefined main.c hashmap.c -o bin/main && bin/main
