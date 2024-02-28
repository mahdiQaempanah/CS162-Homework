/* A simple test harness for memory alloction. */

#include "mm_alloc.h"
#include <stdio.h>
int main(int argc, char **argv)
{
    int* a= (int*) mm_malloc(10*sizeof(int));
    printf("%p", a);
    printf("%d", a[5]);
}
