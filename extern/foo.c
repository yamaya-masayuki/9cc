#include <stdio.h>

extern int foo() {
    puts("hello! from external function.");
    return 0;
}
