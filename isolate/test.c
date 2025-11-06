#define MLOCK_ENABLE_DEBUG
#include "mlock.c"
#include <stdio.h>

int main(void)
{
    init_lock();

    // int* arr = mlock(sizeof(int) * 10);
    // for (int i = 0; i < 10; i++) {
    //     arr[i] = i;
    //     fprintf(stderr, "%d\n", arr[i]);
    // }
    //
    // arr = relock(arr, sizeof(int) * 20);
    // for (int i = 0; i < 20; i++) {
    //     arr[i] = i;
    //     fprintf(stderr, "%d\n", arr[i]);
    // }
    // unlock(arr);
    //
    // size_t big_num = (1 << 13);
    // size_t* big_arr = mlock(sizeof(size_t) * big_num);
    // for (int i = 0; i < big_num; i += 100) {
    //     big_arr[i] = i;
    //     fprintf(stderr, "%ld\n", big_arr[i]);
    // }
    // unlock(big_arr);

    for (int i = 0; i < (1 << 20); i++) {
        int pattern[14] = {
            (1 << 1),
            (1 << 8),
            (1 << 3),
            (1 << 12),
            (1 << 6),
            (1 << 4),
            (1 << 8),
            (1 << 10),
            (1 << 20),
            (1 << 3),
            (1 << 5),
            (1 << 12),
            (1 << 7),
            (1 << 2),
        };
        char* arrs[14] = { NULL };
        for (int k = 0; k < 14; k++) {
            arrs[k] = mlock(sizeof(char) * pattern[k]);
        }
        for (int k = 0; k < 14; k++) {
            unlock(arrs[k]);
        }
    }

    fprintf(stderr, "done!\n");
    return 0;
}
