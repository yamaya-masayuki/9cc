#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

typedef unsigned long long T; // レジスタの型 = 8byte

#define N (4)
static int buffer[N];

extern T alloc4(int **r, T a, T b, T c, T d) {
    //fprintf(stderr, ">> r=%p a=%llu b=%llu c=%llu d=%llu sizeof(int)=%ld\n", r, a, b, c, d, sizeof(int));
    buffer[0] = a;
    buffer[1] = b;
    buffer[2] = c;
    buffer[3] = d;
    //fprintf(stderr, ">> [%d, %d, %d, %d]\n", buffer[0], buffer[1], buffer[2], buffer[3]);
    *r = buffer;
    return N;
}
