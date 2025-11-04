#define MLOCK_ENABLE_DEBUG
#include "mlock.c"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    initlock();
    int* arr = mlock(sizeof(int) * 10);
    for (int i = 0; i < 10; i++) {
        arr[i] = i;
        printf("%d\n", arr[i]);
    }
    arr = relock(arr, sizeof(int) * 20);
    for (int i = 0; i < 20; i++) {
        arr[i] = i;
        printf("%d\n", arr[i]);
    }
    unlock(arr);
    printf("\n\n\n");
    size_t big_num = (1 << 10);
    size_t* big_arr = mlock(sizeof(size_t) * big_num);
    for (int i = 0; i < big_num; i += 100) {
        big_arr[i] = i;
        printf("%ld\n", big_arr[i]);
    }
    unlock(big_arr);
    printf("done!\n");
    return 0;
}