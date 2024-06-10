
build:
	gcc src/main.c -o bin/main

run: build
	./bin/main