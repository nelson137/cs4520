#include "../include/structures.h"
#include <stdio.h>

int compare_structs(sample_t* x, sample_t* y)
{
    return x != NULL && y != NULL &&
        x->a == y->a && x->b == y->b && x->c == y->c;
}

void print_alignments()
{
    printf("Alignment of int is %zu bytes\n",__alignof__(int));
    printf("Alignment of double is %zu bytes\n",__alignof__(double));
    printf("Alignment of float is %zu bytes\n",__alignof__(float));
    printf("Alignment of char is %zu bytes\n",__alignof__(char));
    printf("Alignment of long long is %zu bytes\n",__alignof__(long long));
    printf("Alignment of short is %zu bytes\n",__alignof__(short));
    printf("Alignment of structs are %zu bytes\n",__alignof__(fruit_t));
}

int sort_fruit(const fruit_t *a, int *apples, int *oranges, const size_t size)
{
    if (a == NULL || apples == NULL || oranges == NULL)
        return -1;

    int ret = size;

    for (size_t i=0; i<size && ret>0; i++) {
        if (IS_APPLE(a + i))       (*apples)++;
        else if (IS_ORANGE(a + i)) (*oranges)++;
        else                       ret = -1;
    }

    return ret;
}

int initialize_array(fruit_t *a, int apples, int oranges)
{
    if (a == NULL)
        return -1;

    int i;
    for (i=0; i<apples; i++, a++)
        initialize_apple((apple_t*) a);
    for (i=0; i<oranges; i++, a++)
        initialize_orange((orange_t*) a);

    return 0;
}

int initialize_orange(orange_t *a)
{
    a->type = ORANGE;
    return 0;
}

int initialize_apple(apple_t *a)
{
    a->type = APPLE;
    return 0;
}
