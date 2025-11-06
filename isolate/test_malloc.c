#include <stdlib.h>
#include <stdio.h>

#define PATTERN_SIZE 14

int main(void)
{
    int* arr = malloc(sizeof(int) * 10);
    for (int i = 0; i < 10; i++) {
        arr[i] = i;
        fprintf(stderr, "%d\n", arr[i]);
    }

    arr = realloc(arr, sizeof(int) * 20);
    for (int i = 0; i < 20; i++) {
        arr[i] = i;
        fprintf(stderr, "%d\n", arr[i]);
    }
    free(arr);

    size_t big_num = (1 << 13);
    size_t* big_arr = malloc(sizeof(size_t) * big_num);
    for (int i = 0; i < big_num; i += 100) {
        big_arr[i] = i;
        fprintf(stderr, "%ld\n", big_arr[i]);
    }
    free(big_arr);

    for (int i = 0; i < (1 << 20); i++) {
        int pattern[PATTERN_SIZE] = {
            (1 << 4),
            (1 << 8),
            (1 << 6),
            (1 << 12),
            (1 << 6),
            (1 << 30),
            (1 << 8),
            (1 << 10),
            (1 << 20),
            (1 << 5),
            (1 << 5),
            (1 << 12),
            (1 << 7),
            (1 << 20),
        };
        char* arrs[PATTERN_SIZE] = { NULL };
        for (int k = 0; k < PATTERN_SIZE; k++) {
            arrs[k] = malloc(sizeof(char) * pattern[k]);
        }
        for (int k = 0; k < PATTERN_SIZE; k++) {
            free(arrs[k]);
        }
    }

    fprintf(stderr, "done!\n");
    return 0;
}
