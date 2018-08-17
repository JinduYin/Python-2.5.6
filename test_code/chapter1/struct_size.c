#include <stdio.h>

typedef struct _object {
    long ob_size;
    long *ob_type;
    long ob_ival;
} PyObject;

#define N_INTOBJECTS 41

struct _intblock {
    PyObject objects[N_INTOBJECTS];
};

typedef struct _intblock PyIntBlock;

int main(){
    PyObject *p;
    p = (PyObject *) malloc(sizeof(PyIntBlock));    

    p = &((PyIntBlock *)p)->objects[0];
    
    printf("block size address @%d\n", sizeof(PyIntBlock));
    printf("p address @%x\n", p);

    p += 1;
    printf("p + 1 address @%x\n", p);
    p += 1;
    printf("p + 2 address @%x\n", p);

    return 0;
}

