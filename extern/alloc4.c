#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

typedef unsigned long long T;

extern T alloc4(T **p, T a, T b, T c, T d) {
    assert(p);
    fprintf(stderr, ">>>>>>>>>> p=%x a=%d b=%d c=%d d=%d\n", p, a, b, c, d);
    T *buffer = (T *)malloc(sizeof(T) * 4);
    int i = 0;
    buffer[i++] = a;
    buffer[i++] = b;
    buffer[i++] = c;
    buffer[i++] = d;
    *p = buffer;
    return i;
}
