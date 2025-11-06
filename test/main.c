// #define MLOCK_ENABLE_DEBUG
// #define MLOCK_WORD_SIZE 4
#include "../src/mlock.c"
#include <stdio.h>

#define REQUIRED_ARGS \
    REQUIRED_LONG_ARG(n, "num-loops", "Number of times to loop alloc test")

#define BOOLEAN_ARGS \
    BOOLEAN_ARG(malloc, "--malloc", "Use the standard library's malloc")

#include "easyargs.h"

int main(int argc, char** argv)
{
    args_t args = make_default_args();

    if (!parse_args(argc, argv, &args)) {
        print_help(argv[0]);
        return 1;
    }

    void* (*alloc_fn)(size_t size);
    void* (*realloc_fn)(void* ptr, size_t size);
    void (*free_fn)(void* ptr);

    if (args.malloc) {
        alloc_fn = malloc;
        realloc_fn = realloc;
        free_fn = free;
    } else {
        alloc_fn = mlock;
        realloc_fn = relock;
        free_fn = unlock;

        init_lock();
    }

    int* arr = alloc_fn(sizeof(int) * 10);
    for (int i = 0; i < 10; i++) {
        arr[i] = i;
        fprintf(stderr, "%d\n", arr[i]);
    }

    arr = realloc_fn(arr, sizeof(int) * 20);
    for (int i = 0; i < 20; i++) {
        arr[i] = i;
        fprintf(stderr, "%d\n", arr[i]);
    }
    free_fn(arr);

    size_t big_num = (1 << 13);
    size_t* big_arr = alloc_fn(sizeof(size_t) * big_num);
    for (int i = 0; i < big_num; i += 100) {
        big_arr[i] = i;
        fprintf(stderr, "%ld\n", big_arr[i]);
    }
    free_fn(big_arr);

#define PATTERN_SIZE 14

    for (int i = 0; i < args.n; i++) {
        int pattern[PATTERN_SIZE] = {
            (1 << 1),
            (1 << 8),
            (1 << 1),
            (1 << 12),
            (1 << 3),
            (1 << 30),
            (1 << 8),
            (1 << 10),
            (1 << 20),
            (1 << 2),
            (1 << 5),
            (1 << 12),
            (1 << 7),
            (1 << 20),
        };
        char* arrs[PATTERN_SIZE] = { NULL };
        for (int k = 0; k < PATTERN_SIZE; k++) {
            arrs[k] = alloc_fn(sizeof(char) * pattern[k]);
        }
        for (int k = 0; k < PATTERN_SIZE; k++) {
            free_fn(arrs[k]);
        }
    }

    fprintf(stderr, "done!\n");
    return 0;
}
