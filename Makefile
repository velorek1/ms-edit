CC = cc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra -Werror -O2
SRC = src/cli.c src/files.c src/buffer.c src/utf8.c src/editor.c src/host.c

.PHONY: all test clean
all: editr

build:
	mkdir -p build

editr: $(SRC) $(wildcard src/*.h) | build
	$(CC) $(CFLAGS) -Isrc $(SRC) -o $@

build/test-core: tests/test_core.c src/buffer.c src/utf8.c src/buffer.h src/utf8.h | build
	$(CC) $(CFLAGS) -Isrc tests/test_core.c src/buffer.c src/utf8.c -o $@

build/test-failures: tests/test_failures.c src/buffer.c src/utf8.c src/buffer.h src/utf8.h | build
	$(CC) $(CFLAGS) -Isrc tests/test_failures.c src/utf8.c -o $@

build/test-cli: tests/test_cli.c src/cli.c src/cli.h | build
	$(CC) $(CFLAGS) -Isrc tests/test_cli.c src/cli.c -o $@

build/test-files: tests/test_files.c src/files.c src/files.h src/buffer.h | build
	$(CC) $(CFLAGS) -Isrc tests/test_files.c src/files.c -o $@

build/test-storage: tests/test_storage.c src/buffer.c src/utf8.c src/buffer.h src/utf8.h | build
	$(CC) $(CFLAGS) -Isrc tests/test_storage.c src/buffer.c src/utf8.c -o $@

test: build/test-storage build/test-files build/test-core editr build/test-failures build/test-cli
	cd build && ./test-storage
	./build/test-files
	./build/test-core
	./build/test-failures
	./build/test-cli
	python3 -m unittest discover -s tests -p 'test_*.py' -v

clean:
	rm -rf build editr
