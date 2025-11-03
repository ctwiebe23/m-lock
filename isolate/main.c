#include "mlock.c"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    mm_init();
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
    printf("done!\n");
    return 0;
}