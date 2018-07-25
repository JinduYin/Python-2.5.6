
/* Integer object implementation */

#include "Python.h"
#include <ctype.h>

long
PyInt_GetMax(void)
{
	return LONG_MAX;	/* To initialize sys.maxint */
}

/* Integers are quite normal objects, to make object handling uniform.
   (Using odd pointers to represent integers would save much space
   but require extra checks for this special case throughout the code.)
   Since a typical Python program spends much of its time allocating
   and deallocating integers, these operations should be very fast.
   Therefore we use a dedicated allocation scheme with a much lower
   overhead (in space and time) than straight malloc(): a simple
   dedicated free list, filled when necessary with memory from malloc().

   block_list is a singly-linked list of all PyIntBlocks ever allocated,
   linked via their next members.  PyIntBlocks are never returned to the
   system before shutdown (PyInt_Fini).

   free_list is a singly-linked list of available PyIntObjects, linked
   via abuse of their ob_type members.
*/
//块大小
#define BLOCK_SIZE	1000	/* 1K less typical malloc overhead */
//存放可容纳64位指针大小的空间
#define BHEAD_SIZE	8	/* Enough for a 64-bit pointer */
//多少个PyIntObject对象  (1000 - 8) / 24  CentOS 7 64位系统 等于41个
// 32位系统 (1000 - 8) / 12 等于82
#define N_INTOBJECTS	((BLOCK_SIZE - BHEAD_SIZE) / sizeof(PyIntObject))

//块结构体，是一个单链表的指针和N_INTOBJECTS个PyIntObject对象的objects
struct _intblock {
	struct _intblock *next;
	PyIntObject objects[N_INTOBJECTS];
};

typedef struct _intblock PyIntBlock;

// PyIntBlock的单向列表通过block_list维护，
// 每一个block中都维护了一个PyIntObject数组——objects，
static PyIntBlock *block_list = NULL;
// 单向链表来管理全部block的objects中所有的空闲内存，表头指针就是free_list
static PyIntObject *free_list = NULL;

static PyIntObject *
fill_free_list(void)
{
	PyIntObject *p, *q;
	/* Python's object allocator isn't appropriate for large blocks. */
	//申请大小为sizeof(PyIntBlock)的内存空间，并链接到已有的block list中
	//大小等于 41 * 24 + 8 = 992
	p = (PyIntObject *) PyMem_MALLOC(sizeof(PyIntBlock));
	if (p == NULL)
		return (PyIntObject *) PyErr_NoMemory();
	// 当前block的next指向原来的block_list
	((PyIntBlock *)p)->next = block_list;
	// block_list执向新的block
	block_list = (PyIntBlock *)p;
	/* Link the int objects together, from rear to front, then return
	   the address of the last int object in the block. */
	//将PyIntBlock中的PyIntObject数组——objects——转变成单向链表
	// p是objects数组中的第一个元素地址
	p = &((PyIntBlock *)p)->objects[0];
	// q 是objects数组中最后一个元素地址的下一个元素地址
	// p 是指向数组第一个元素的地址，N_INTOBJECTS 是一个常数 41，
	// p 指向的数组下标0，q的下标41 因为q指向下标41的开始地址，所以地址(q-p)/24 = 41
	// q 指针实际指向的是 N_INTOBJECTS + 1 个元素的开始地址
	// 数组指针移动一位，地址移动数组元素的所占内存大小 所以 q = p + 41 * 3 * 8 = p + 984
	// q的实际内存地址偏移984位
	q = p + N_INTOBJECTS;

//	printf("-----------start------------\n");
//	printf("p address @%x\n", p);
//	printf("N_INTOBJECTS @%d\n", N_INTOBJECTS);
//	printf("q address @%x\n", p + N_INTOBJECTS);
//	printf("q -1 address @%x\n", (struct _typeobject *)(q-1));
//	printf("p + N_INTOBJECTS - 1 address @%x\n", p + N_INTOBJECTS - 1);
//	printf("-----------end------------\n");

	// 从后往前挨个链接，即最后一个指向倒数第二个以此类推
	while (--q > p){
		q->ob_type = (struct _typeobject *)(q-1);
	}
	// 第一个objects的ob_type指向null
	q->ob_type = NULL;
	// 返回的是最后一个objects所指向的object.
	// p + N_INTOBJECTS - 1 所以是最后一个元素的地址
	return p + N_INTOBJECTS - 1;
}

#ifndef NSMALLPOSINTS
#define NSMALLPOSINTS		257
#endif
#ifndef NSMALLNEGINTS
#define NSMALLNEGINTS		5
#endif
#if NSMALLNEGINTS + NSMALLPOSINTS > 0
/* References to small integers are saved in this array so that they
   can be shared.
   The integers that are saved are those in the range
   -NSMALLNEGINTS (inclusive) to NSMALLPOSINTS (not inclusive).
*/
//小整数对象池就是一个PyIntObject指针数组(注意是指针数组), 大小=257+5=262,
//范围是[-5, 257) 注意左闭右开. 即这个数组包含了262个指向PyIntObject的指针.
static PyIntObject *small_ints[NSMALLNEGINTS + NSMALLPOSINTS];
#endif
#ifdef COUNT_ALLOCS
int quick_int_allocs, quick_neg_int_allocs;
#endif

PyObject *
PyInt_FromLong(long ival)
{
    //register 关键字暗示编译程序相应的变量将会被频繁的使用，
    //如果可能的话，应将其保存在 CPU 的寄存器中，以加快其存取速度
	register PyIntObject *v;
#if NSMALLNEGINTS + NSMALLPOSINTS > 0
    // 如果在小整数范围内，引用计数加1，直接返回
	if (-NSMALLNEGINTS <= ival && ival < NSMALLPOSINTS) {
		v = small_ints[ival + NSMALLNEGINTS];
		//引用+1
		Py_INCREF(v);
#ifdef COUNT_ALLOCS
		if (ival >= 0)
			quick_int_allocs++;
		else
			quick_neg_int_allocs++;
#endif
		return (PyObject *) v;
	}
#endif
    // 如果空闲列表为空
	if (free_list == NULL) {
		// 创建通用列表并返回空闲列表指针
		if ((free_list = fill_free_list()) == NULL)
			return NULL;
	}
	/* Inline PyObject_New */
	// free_list指针赋值给v
	v = free_list;
	// free_list 指向下一个object
	free_list = (PyIntObject *)v->ob_type;
	// 初始化类型对象，引用计数等
	PyObject_INIT(v, &PyInt_Type);
	// 赋值
	v->ob_ival = ival;
	return (PyObject *) v;
}

PyObject *
PyInt_FromSize_t(size_t ival)
{
	if (ival <= LONG_MAX)
		return PyInt_FromLong((long)ival);
	return _PyLong_FromSize_t(ival);
}

PyObject *
PyInt_FromSsize_t(Py_ssize_t ival)
{
	if (ival >= LONG_MIN && ival <= LONG_MAX)
		return PyInt_FromLong((long)ival);
	return _PyLong_FromSsize_t(ival);
}

static void
int_dealloc(PyIntObject *v)
{
    // 是整数类型, 将对象放入free_list单向链表头
	if (PyInt_CheckExact(v)) {
		// 把free_list的指针赋值给当前object的ob_type
		v->ob_type = (struct _typeobject *)free_list;
		// free_list指针指向当前object。
		// 即当前释放的空间会最早被利用
		free_list = v;
	} else {
        //派生类的对象调用方法
		v->ob_type->tp_free((PyObject *)v);
	}
}

static void
int_free(PyIntObject *v)
{
	v->ob_type = (struct _typeobject *)free_list;
	free_list = v;
}

long
PyInt_AsLong(register PyObject *op)
{
	PyNumberMethods *nb;
	PyIntObject *io;
	long val;

	if (op && PyInt_Check(op))
		return PyInt_AS_LONG((PyIntObject*) op);

	if (op == NULL || (nb = op->ob_type->tp_as_number) == NULL ||
	    nb->nb_int == NULL) {
		PyErr_SetString(PyExc_TypeError, "an integer is required");
		return -1;
	}

	io = (PyIntObject*) (*nb->nb_int) (op);
	if (io == NULL)
		return -1;
	if (!PyInt_Check(io)) {
		if (PyLong_Check(io)) {
			/* got a long? => retry int conversion */
			val = PyLong_AsLong((PyObject *)io);
			Py_DECREF(io);
			if ((val == -1) && PyErr_Occurred())
				return -1;
			return val;
		}
		else
		{
			Py_DECREF(io);
			PyErr_SetString(PyExc_TypeError,
					"nb_int should return int object");
			return -1;
		}
	}

	val = PyInt_AS_LONG(io);
	Py_DECREF(io);

	return val;
}

Py_ssize_t
PyInt_AsSsize_t(register PyObject *op)
{
#if SIZEOF_SIZE_T != SIZEOF_LONG
	PyNumberMethods *nb;
	PyIntObject *io;
	Py_ssize_t val;
#endif

	if (op == NULL) {
		PyErr_SetString(PyExc_TypeError, "an integer is required");
		return -1;
	}

	if (PyInt_Check(op))
		return PyInt_AS_LONG((PyIntObject*) op);
	if (PyLong_Check(op))
		return _PyLong_AsSsize_t(op);
#if SIZEOF_SIZE_T == SIZEOF_LONG
	return PyInt_AsLong(op);
#else

	if ((nb = op->ob_type->tp_as_number) == NULL ||
	    (nb->nb_int == NULL && nb->nb_long == 0)) {
		PyErr_SetString(PyExc_TypeError, "an integer is required");
		return -1;
	}

	if (nb->nb_long != 0) {
		io = (PyIntObject*) (*nb->nb_long) (op);
	} else {
		io = (PyIntObject*) (*nb->nb_int) (op);
	}
	if (io == NULL)
		return -1;
	if (!PyInt_Check(io)) {
		if (PyLong_Check(io)) {
			/* got a long? => retry int conversion */
			val = _PyLong_AsSsize_t((PyObject *)io);
			Py_DECREF(io);
			if ((val == -1) && PyErr_Occurred())
				return -1;
			return val;
		}
		else
		{
			Py_DECREF(io);
			PyErr_SetString(PyExc_TypeError,
					"nb_int should return int object");
			return -1;
		}
	}

	val = PyInt_AS_LONG(io);
	Py_DECREF(io);

	return val;
#endif
}

unsigned long
PyInt_AsUnsignedLongMask(register PyObject *op)
{
	PyNumberMethods *nb;
	PyIntObject *io;
	unsigned long val;

	if (op && PyInt_Check(op))
		return PyInt_AS_LONG((PyIntObject*) op);
	if (op && PyLong_Check(op))
		return PyLong_AsUnsignedLongMask(op);

	if (op == NULL || (nb = op->ob_type->tp_as_number) == NULL ||
	    nb->nb_int == NULL) {
		PyErr_SetString(PyExc_TypeError, "an integer is required");
		return (unsigned long)-1;
	}

	io = (PyIntObject*) (*nb->nb_int) (op);
	if (io == NULL)
		return (unsigned long)-1;
	if (!PyInt_Check(io)) {
		if (PyLong_Check(io)) {
			val = PyLong_AsUnsignedLongMask((PyObject *)io);
			Py_DECREF(io);
			if (PyErr_Occurred())
				return (unsigned long)-1;
			return val;
		}
		else
		{
			Py_DECREF(io);
			PyErr_SetString(PyExc_TypeError,
					"nb_int should return int object");
			return (unsigned long)-1;
		}
	}

	val = PyInt_AS_LONG(io);
	Py_DECREF(io);

	return val;
}

#ifdef HAVE_LONG_LONG
unsigned PY_LONG_LONG
PyInt_AsUnsignedLongLongMask(register PyObject *op)
{
	PyNumberMethods *nb;
	PyIntObject *io;
	unsigned PY_LONG_LONG val;

	if (op && PyInt_Check(op))
		return PyInt_AS_LONG((PyIntObject*) op);
	if (op && PyLong_Check(op))
		return PyLong_AsUnsignedLongLongMask(op);

	if (op == NULL || (nb = op->ob_type->tp_as_number) == NULL ||
	    nb->nb_int == NULL) {
		PyErr_SetString(PyExc_TypeError, "an integer is required");
		return (unsigned PY_LONG_LONG)-1;
	}

	io = (PyIntObject*) (*nb->nb_int) (op);
	if (io == NULL)
		return (unsigned PY_LONG_LONG)-1;
	if (!PyInt_Check(io)) {
		if (PyLong_Check(io)) {
			val = PyLong_AsUnsignedLongLongMask((PyObject *)io);
			Py_DECREF(io);
			if (PyErr_Occurred())
				return (unsigned PY_LONG_LONG)-1;
			return val;
		}
		else
		{
			Py_DECREF(io);
			PyErr_SetString(PyExc_TypeError,
					"nb_int should return int object");
			return (unsigned PY_LONG_LONG)-1;
		}
	}

	val = PyInt_AS_LONG(io);
	Py_DECREF(io);

	return val;
}
#endif

PyObject *
PyInt_FromString(char *s, char **pend, int base)
{
	char *end;
	long x;
	Py_ssize_t slen;
	PyObject *sobj, *srepr;

	if ((base != 0 && base < 2) || base > 36) {
		PyErr_SetString(PyExc_ValueError,
				"int() base must be >= 2 and <= 36");
		return NULL;
	}

	while (*s && isspace(Py_CHARMASK(*s)))
		s++;
	errno = 0;
	if (base == 0 && s[0] == '0') {
		x = (long) PyOS_strtoul(s, &end, base);
		if (x < 0)
			return PyLong_FromString(s, pend, base);
	}
	else
		x = PyOS_strtol(s, &end, base);
	if (end == s || !isalnum(Py_CHARMASK(end[-1])))
		goto bad;
	while (*end && isspace(Py_CHARMASK(*end)))
		end++;
	if (*end != '\0') {
  bad:
		slen = strlen(s) < 200 ? strlen(s) : 200;
		sobj = PyString_FromStringAndSize(s, slen);
		if (sobj == NULL)
			return NULL;
		srepr = PyObject_Repr(sobj);
		Py_DECREF(sobj);
		if (srepr == NULL)
			return NULL;
		PyErr_Format(PyExc_ValueError,
			     "invalid literal for int() with base %d: %s",
			     base, PyString_AS_STRING(srepr));
		Py_DECREF(srepr);
		return NULL;
	}
	else if (errno != 0)
		return PyLong_FromString(s, pend, base);
	if (pend)
		*pend = end;
	return PyInt_FromLong(x);
}

#ifdef Py_USING_UNICODE
PyObject *
PyInt_FromUnicode(Py_UNICODE *s, Py_ssize_t length, int base)
{
	PyObject *result;
	char *buffer = (char *)PyMem_MALLOC(length+1);

	if (buffer == NULL)
		return PyErr_NoMemory();

	if (PyUnicode_EncodeDecimal(s, length, buffer, NULL)) {
		PyMem_FREE(buffer);
		return NULL;
	}
	result = PyInt_FromString(buffer, NULL, base);
	PyMem_FREE(buffer);
	return result;
}
#endif

/* Methods */

/* Integers are seen as the "smallest" of all numeric types and thus
   don't have any knowledge about conversion of other types to
   integers. */

#define CONVERT_TO_LONG(obj, lng)		\
	if (PyInt_Check(obj)) {			\
		lng = PyInt_AS_LONG(obj);	\
	}					\
	else {					\
		Py_INCREF(Py_NotImplemented);	\
		return Py_NotImplemented;	\
	}

/* ARGSUSED */
static int
int_print(PyIntObject *v, FILE *fp, int flags)
     /* flags -- not used but required by interface */
{
	fprintf(fp, "%ld\n", v->ob_ival);
	printf(" free_list : %p", free_list);
	return 0;

//	//JIN_DU
//    int values[10];
//    int refcounts[10];
//    PyIntObject* intObjectPtr;
//    PyIntBlock *p = block_list;
//    PyIntBlock *last = NULL;
//    int count = 0;
//    int i;
//
//    while(p != NULL)  {
//        ++count;
//        last = p;
//        p = p->next;
//    }
//
//    intObjectPtr = last->objects;
//    intObjectPtr += N_INTOBJECTS - 1;
//    printf(" value   @%ld\n", v->ob_ival);
//    printf(" address @%p\n", v);
//
//    for(i = 0; i < 10; ++i, --intObjectPtr)  {
//        values[i] = intObjectPtr->ob_ival;
//        refcounts[i] = intObjectPtr->ob_refcnt;
//    }
//
//    printf(" value : ");
//    for(i = 0; i < 8; ++i)  {
//        printf("%d\t", values[i]);
//    }
//    printf("\n");
//
//    printf(" refcnt : ");
//    for(i = 0; i < 8; ++i)  {
//        printf("%d\t", refcounts[i]);
//    }
//
//    printf("\n");
//    printf(" block_list count : %d\n", count);
//    printf(" free_list : %p\n", free_list);
//    return 0;
}

static PyObject *
int_repr(PyIntObject *v)
{
	char buf[64];
	PyOS_snprintf(buf, sizeof(buf), "%ld", v->ob_ival);
	return PyString_FromString(buf);
}

static int
int_compare(PyIntObject *v, PyIntObject *w)
{
	register long i = v->ob_ival;
	register long j = w->ob_ival;
	return (i < j) ? -1 : (i > j) ? 1 : 0;
}

static long
int_hash(PyIntObject *v)
{
	/* XXX If this is changed, you also need to change the way
	   Python's long, float and complex types are hashed. */
	long x = v -> ob_ival;
	if (x == -1)
		x = -2;
	return x;
}

static PyObject *
int_add(PyIntObject *v, PyIntObject *w)
{
	register long a, b, x;
	CONVERT_TO_LONG(v, a);
	CONVERT_TO_LONG(w, b);
	x = a + b;
	if ((x^a) >= 0 || (x^b) >= 0)
		return PyInt_FromLong(x);
	return PyLong_Type.tp_as_number->nb_add((PyObject *)v, (PyObject *)w);
}

static PyObject *
int_sub(PyIntObject *v, PyIntObject *w)
{
	register long a, b, x;
	CONVERT_TO_LONG(v, a);
	CONVERT_TO_LONG(w, b);
	x = a - b;
	if ((x^a) >= 0 || (x^~b) >= 0)
		return PyInt_FromLong(x);
	return PyLong_Type.tp_as_number->nb_subtract((PyObject *)v,
						     (PyObject *)w);
}

/*
Integer overflow checking for * is painful:  Python tried a couple ways, but
they didn't work on all platforms, or failed in endcases (a product of
-sys.maxint-1 has been a particular pain).

Here's another way:

The native long product x*y is either exactly right or *way* off, being
just the last n bits of the true product, where n is the number of bits
in a long (the delivered product is the true product plus i*2**n for
some integer i).

The native double product (double)x * (double)y is subject to three
rounding errors:  on a sizeof(long)==8 box, each cast to double can lose
info, and even on a sizeof(long)==4 box, the multiplication can lose info.
But, unlike the native long product, it's not in *range* trouble:  even
if sizeof(long)==32 (256-bit longs), the product easily fits in the
dynamic range of a double.  So the leading 50 (or so) bits of the double
product are correct.

We check these two ways against each other, and declare victory if they're
approximately the same.  Else, because the native long product is the only
one that can lose catastrophic amounts of information, it's the native long
product that must have overflowed.
*/

static PyObject *
int_mul(PyObject *v, PyObject *w)
{
	long a, b;
	long longprod;			/* a*b in native long arithmetic */
	double doubled_longprod;	/* (double)longprod */
	double doubleprod;		/* (double)a * (double)b */

	CONVERT_TO_LONG(v, a);
	CONVERT_TO_LONG(w, b);
	longprod = a * b;
	doubleprod = (double)a * (double)b;
	doubled_longprod = (double)longprod;

	/* Fast path for normal case:  small multiplicands, and no info
	   is lost in either method. */
	if (doubled_longprod == doubleprod)
		return PyInt_FromLong(longprod);

	/* Somebody somewhere lost info.  Close enough, or way off?  Note
	   that a != 0 and b != 0 (else doubled_longprod == doubleprod == 0).
	   The difference either is or isn't significant compared to the
	   true value (of which doubleprod is a good approximation).
	*/
	{
		const double diff = doubled_longprod - doubleprod;
		const double absdiff = diff >= 0.0 ? diff : -diff;
		const double absprod = doubleprod >= 0.0 ? doubleprod :
							  -doubleprod;
		/* absdiff/absprod <= 1/32 iff
		   32 * absdiff <= absprod -- 5 good bits is "close enough" */
		if (32.0 * absdiff <= absprod)
			return PyInt_FromLong(longprod);
		else
			return PyLong_Type.tp_as_number->nb_multiply(v, w);
	}
}

/* Integer overflow checking for unary negation: on a 2's-complement
 * box, -x overflows iff x is the most negative long.  In this case we
 * get -x == x.  However, -x is undefined (by C) if x /is/ the most
 * negative long (it's a signed overflow case), and some compilers care.
 * So we cast x to unsigned long first.  However, then other compilers
 * warn about applying unary minus to an unsigned operand.  Hence the
 * weird "0-".
 */
#define UNARY_NEG_WOULD_OVERFLOW(x)	\
	((x) < 0 && (unsigned long)(x) == 0-(unsigned long)(x))

/* Return type of i_divmod */
enum divmod_result {
	DIVMOD_OK,		/* Correct result */
	DIVMOD_OVERFLOW,	/* Overflow, try again using longs */
	DIVMOD_ERROR		/* Exception raised */
};

static enum divmod_result
i_divmod(register long x, register long y,
         long *p_xdivy, long *p_xmody)
{
	long xdivy, xmody;

	if (y == 0) {
		PyErr_SetString(PyExc_ZeroDivisionError,
				"integer division or modulo by zero");
		return DIVMOD_ERROR;
	}
	/* (-sys.maxint-1)/-1 is the only overflow case. */
	if (y == -1 && UNARY_NEG_WOULD_OVERFLOW(x))
		return DIVMOD_OVERFLOW;
	xdivy = x / y;
	xmody = x - xdivy * y;
	/* If the signs of x and y differ, and the remainder is non-0,
	 * C89 doesn't define whether xdivy is now the floor or the
	 * ceiling of the infinitely precise quotient.  We want the floor,
	 * and we have it iff the remainder's sign matches y's.
	 */
	if (xmody && ((y ^ xmody) < 0) /* i.e. and signs differ */) {
		xmody += y;
		--xdivy;
		assert(xmody && ((y ^ xmody) >= 0));
	}
	*p_xdivy = xdivy;
	*p_xmody = xmody;
	return DIVMOD_OK;
}

static PyObject *
int_div(PyIntObject *x, PyIntObject *y)
{
	long xi, yi;
	long d, m;
	CONVERT_TO_LONG(x, xi);
	CONVERT_TO_LONG(y, yi);
	switch (i_divmod(xi, yi, &d, &m)) {
	case DIVMOD_OK:
		return PyInt_FromLong(d);
	case DIVMOD_OVERFLOW:
		return PyLong_Type.tp_as_number->nb_divide((PyObject *)x,
							   (PyObject *)y);
	default:
		return NULL;
	}
}

static PyObject *
int_classic_div(PyIntObject *x, PyIntObject *y)
{
	long xi, yi;
	long d, m;
	CONVERT_TO_LONG(x, xi);
	CONVERT_TO_LONG(y, yi);
	if (Py_DivisionWarningFlag &&
	    PyErr_Warn(PyExc_DeprecationWarning, "classic int division") < 0)
		return NULL;
	switch (i_divmod(xi, yi, &d, &m)) {
	case DIVMOD_OK:
		return PyInt_FromLong(d);
	case DIVMOD_OVERFLOW:
		return PyLong_Type.tp_as_number->nb_divide((PyObject *)x,
							   (PyObject *)y);
	default:
		return NULL;
	}
}

static PyObject *
int_true_divide(PyObject *v, PyObject *w)
{
	/* If they aren't both ints, give someone else a chance.  In
	   particular, this lets int/long get handled by longs, which
	   underflows to 0 gracefully if the long is too big to convert
	   to float. */
	if (PyInt_Check(v) && PyInt_Check(w))
		return PyFloat_Type.tp_as_number->nb_true_divide(v, w);
	Py_INCREF(Py_NotImplemented);
	return Py_NotImplemented;
}

static PyObject *
int_mod(PyIntObject *x, PyIntObject *y)
{
	long xi, yi;
	long d, m;
	CONVERT_TO_LONG(x, xi);
	CONVERT_TO_LONG(y, yi);
	switch (i_divmod(xi, yi, &d, &m)) {
	case DIVMOD_OK:
		return PyInt_FromLong(m);
	case DIVMOD_OVERFLOW:
		return PyLong_Type.tp_as_number->nb_remainder((PyObject *)x,
							      (PyObject *)y);
	default:
		return NULL;
	}
}

static PyObject *
int_divmod(PyIntObject *x, PyIntObject *y)
{
	long xi, yi;
	long d, m;
	CONVERT_TO_LONG(x, xi);
	CONVERT_TO_LONG(y, yi);
	switch (i_divmod(xi, yi, &d, &m)) {
	case DIVMOD_OK:
		return Py_BuildValue("(ll)", d, m);
	case DIVMOD_OVERFLOW:
		return PyLong_Type.tp_as_number->nb_divmod((PyObject *)x,
							   (PyObject *)y);
	default:
		return NULL;
	}
}

static PyObject *
int_pow(PyIntObject *v, PyIntObject *w, PyIntObject *z)
{
	register long iv, iw, iz=0, ix, temp, prev;
	CONVERT_TO_LONG(v, iv);
	CONVERT_TO_LONG(w, iw);
	if (iw < 0) {
		if ((PyObject *)z != Py_None) {
			PyErr_SetString(PyExc_TypeError, "pow() 2nd argument "
			     "cannot be negative when 3rd argument specified");
			return NULL;
		}
		/* Return a float.  This works because we know that
		   this calls float_pow() which converts its
		   arguments to double. */
		return PyFloat_Type.tp_as_number->nb_power(
			(PyObject *)v, (PyObject *)w, (PyObject *)z);
	}
 	if ((PyObject *)z != Py_None) {
		CONVERT_TO_LONG(z, iz);
		if (iz == 0) {
			PyErr_SetString(PyExc_ValueError,
					"pow() 3rd argument cannot be 0");
			return NULL;
		}
	}
	/*
	 * XXX: The original exponentiation code stopped looping
	 * when temp hit zero; this code will continue onwards
	 * unnecessarily, but at least it won't cause any errors.
	 * Hopefully the speed improvement from the fast exponentiation
	 * will compensate for the slight inefficiency.
	 * XXX: Better handling of overflows is desperately needed.
	 */
 	temp = iv;
	ix = 1;
	while (iw > 0) {
	 	prev = ix;	/* Save value for overflow check */
	 	if (iw & 1) {
		 	ix = ix*temp;
			if (temp == 0)
				break; /* Avoid ix / 0 */
			if (ix / temp != prev) {
				return PyLong_Type.tp_as_number->nb_power(
					(PyObject *)v,
					(PyObject *)w,
					(PyObject *)z);
			}
		}
	 	iw >>= 1;	/* Shift exponent down by 1 bit */
	        if (iw==0) break;
	 	prev = temp;
	 	temp *= temp;	/* Square the value of temp */
	 	if (prev != 0 && temp / prev != prev) {
			return PyLong_Type.tp_as_number->nb_power(
				(PyObject *)v, (PyObject *)w, (PyObject *)z);
		}
	 	if (iz) {
			/* If we did a multiplication, perform a modulo */
		 	ix = ix % iz;
		 	temp = temp % iz;
		}
	}
	if (iz) {
	 	long div, mod;
		switch (i_divmod(ix, iz, &div, &mod)) {
		case DIVMOD_OK:
			ix = mod;
			break;
		case DIVMOD_OVERFLOW:
			return PyLong_Type.tp_as_number->nb_power(
				(PyObject *)v, (PyObject *)w, (PyObject *)z);
		default:
			return NULL;
		}
	}
	return PyInt_FromLong(ix);
}

static PyObject *
int_neg(PyIntObject *v)
{
	register long a;
	a = v->ob_ival;
        /* check for overflow */
	if (UNARY_NEG_WOULD_OVERFLOW(a)) {
		PyObject *o = PyLong_FromLong(a);
		if (o != NULL) {
			PyObject *result = PyNumber_Negative(o);
			Py_DECREF(o);
			return result;
		}
		return NULL;
	}
	return PyInt_FromLong(-a);
}

static PyObject *
int_pos(PyIntObject *v)
{
	if (PyInt_CheckExact(v)) {
		Py_INCREF(v);
		return (PyObject *)v;
	}
	else
		return PyInt_FromLong(v->ob_ival);
}

static PyObject *
int_abs(PyIntObject *v)
{
	if (v->ob_ival >= 0)
		return int_pos(v);
	else
		return int_neg(v);
}

static int
int_nonzero(PyIntObject *v)
{
	return v->ob_ival != 0;
}

static PyObject *
int_invert(PyIntObject *v)
{
	return PyInt_FromLong(~v->ob_ival);
}

static PyObject *
int_lshift(PyIntObject *v, PyIntObject *w)
{
	long a, b, c;
	PyObject *vv, *ww, *result;

	CONVERT_TO_LONG(v, a);
	CONVERT_TO_LONG(w, b);
	if (b < 0) {
		PyErr_SetString(PyExc_ValueError, "negative shift count");
		return NULL;
	}
	if (a == 0 || b == 0)
		return int_pos(v);
	if (b >= LONG_BIT) {
		vv = PyLong_FromLong(PyInt_AS_LONG(v));
		if (vv == NULL)
			return NULL;
		ww = PyLong_FromLong(PyInt_AS_LONG(w));
		if (ww == NULL) {
			Py_DECREF(vv);
			return NULL;
		}
		result = PyNumber_Lshift(vv, ww);
		Py_DECREF(vv);
		Py_DECREF(ww);
		return result;
	}
	c = a << b;
	if (a != Py_ARITHMETIC_RIGHT_SHIFT(long, c, b)) {
		vv = PyLong_FromLong(PyInt_AS_LONG(v));
		if (vv == NULL)
			return NULL;
		ww = PyLong_FromLong(PyInt_AS_LONG(w));
		if (ww == NULL) {
			Py_DECREF(vv);
			return NULL;
		}
		result = PyNumber_Lshift(vv, ww);
		Py_DECREF(vv);
		Py_DECREF(ww);
		return result;
	}
	return PyInt_FromLong(c);
}

static PyObject *
int_rshift(PyIntObject *v, PyIntObject *w)
{
	register long a, b;
	CONVERT_TO_LONG(v, a);
	CONVERT_TO_LONG(w, b);
	if (b < 0) {
		PyErr_SetString(PyExc_ValueError, "negative shift count");
		return NULL;
	}
	if (a == 0 || b == 0)
		return int_pos(v);
	if (b >= LONG_BIT) {
		if (a < 0)
			a = -1;
		else
			a = 0;
	}
	else {
		a = Py_ARITHMETIC_RIGHT_SHIFT(long, a, b);
	}
	return PyInt_FromLong(a);
}

static PyObject *
int_and(PyIntObject *v, PyIntObject *w)
{
	register long a, b;
	CONVERT_TO_LONG(v, a);
	CONVERT_TO_LONG(w, b);
	return PyInt_FromLong(a & b);
}

static PyObject *
int_xor(PyIntObject *v, PyIntObject *w)
{
	register long a, b;
	CONVERT_TO_LONG(v, a);
	CONVERT_TO_LONG(w, b);
	return PyInt_FromLong(a ^ b);
}

static PyObject *
int_or(PyIntObject *v, PyIntObject *w)
{
	register long a, b;
	CONVERT_TO_LONG(v, a);
	CONVERT_TO_LONG(w, b);
	return PyInt_FromLong(a | b);
}

static int
int_coerce(PyObject **pv, PyObject **pw)
{
	if (PyInt_Check(*pw)) {
		Py_INCREF(*pv);
		Py_INCREF(*pw);
		return 0;
	}
	return 1; /* Can't do it */
}

static PyObject *
int_int(PyIntObject *v)
{
	if (PyInt_CheckExact(v))
		Py_INCREF(v);
	else
		v = (PyIntObject *)PyInt_FromLong(v->ob_ival);
	return (PyObject *)v;
}

static PyObject *
int_long(PyIntObject *v)
{
	return PyLong_FromLong((v -> ob_ival));
}

static PyObject *
int_float(PyIntObject *v)
{
	return PyFloat_FromDouble((double)(v -> ob_ival));
}

static PyObject *
int_oct(PyIntObject *v)
{
	char buf[100];
	long x = v -> ob_ival;
	if (x < 0)
		PyOS_snprintf(buf, sizeof(buf), "-0%lo", -x);
	else if (x == 0)
		strcpy(buf, "0");
	else
		PyOS_snprintf(buf, sizeof(buf), "0%lo", x);
	return PyString_FromString(buf);
}

static PyObject *
int_hex(PyIntObject *v)
{
	char buf[100];
	long x = v -> ob_ival;
	if (x < 0)
		PyOS_snprintf(buf, sizeof(buf), "-0x%lx", -x);
	else
		PyOS_snprintf(buf, sizeof(buf), "0x%lx", x);
	return PyString_FromString(buf);
}

static PyObject *
int_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

static PyObject *
int_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *x = NULL;
	int base = -909;
	static char *kwlist[] = {"x", "base", 0};

	if (type != &PyInt_Type)
		return int_subtype_new(type, args, kwds); /* Wimp out */
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|Oi:int", kwlist,
					 &x, &base))
		return NULL;
	if (x == NULL)
		return PyInt_FromLong(0L);
	if (base == -909)
		return PyNumber_Int(x);
	if (PyString_Check(x)) {
		/* Since PyInt_FromString doesn't have a length parameter,
		 * check here for possible NULs in the string. */
		char *string = PyString_AS_STRING(x);
		if (strlen(string) != PyString_Size(x)) {
			/* create a repr() of the input string,
			 * just like PyInt_FromString does */
			PyObject *srepr;
			srepr = PyObject_Repr(x);
			if (srepr == NULL)
				return NULL;
			PyErr_Format(PyExc_ValueError,
			     "invalid literal for int() with base %d: %s",
			     base, PyString_AS_STRING(srepr));
			Py_DECREF(srepr);
			return NULL;
		}
		return PyInt_FromString(string, NULL, base);
	}
#ifdef Py_USING_UNICODE
	if (PyUnicode_Check(x))
		return PyInt_FromUnicode(PyUnicode_AS_UNICODE(x),
					 PyUnicode_GET_SIZE(x),
					 base);
#endif
	PyErr_SetString(PyExc_TypeError,
			"int() can't convert non-string with explicit base");
	return NULL;
}

/* Wimpy, slow approach to tp_new calls for subtypes of int:
   first create a regular int from whatever arguments we got,
   then allocate a subtype instance and initialize its ob_ival
   from the regular int.  The regular int is then thrown away.
*/
static PyObject *
int_subtype_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject *tmp, *newobj;
	long ival;

	assert(PyType_IsSubtype(type, &PyInt_Type));
	tmp = int_new(&PyInt_Type, args, kwds);
	if (tmp == NULL)
		return NULL;
	if (!PyInt_Check(tmp)) {
		ival = PyLong_AsLong(tmp);
		if (ival == -1 && PyErr_Occurred()) {
			Py_DECREF(tmp);
			return NULL;
		}
	} else {
		ival = ((PyIntObject *)tmp)->ob_ival;
	}

	newobj = type->tp_alloc(type, 0);
	if (newobj == NULL) {
		Py_DECREF(tmp);
		return NULL;
	}
	((PyIntObject *)newobj)->ob_ival = ival;
	Py_DECREF(tmp);
	return newobj;
}

static PyObject *
int_getnewargs(PyIntObject *v)
{
	return Py_BuildValue("(l)", v->ob_ival);
}

static PyMethodDef int_methods[] = {
	{"__getnewargs__",	(PyCFunction)int_getnewargs,	METH_NOARGS},
	{NULL,		NULL}		/* sentinel */
};

PyDoc_STRVAR(int_doc,
"int(x[, base]) -> integer\n\
\n\
Convert a string or number to an integer, if possible.  A floating point\n\
argument will be truncated towards zero (this does not include a string\n\
representation of a floating point number!)  When converting a string, use\n\
the optional base.  It is an error to supply a base when converting a\n\
non-string. If the argument is outside the integer range a long object\n\
will be returned instead.");

static PyNumberMethods int_as_number = {
	(binaryfunc)int_add,	/*nb_add*/
	(binaryfunc)int_sub,	/*nb_subtract*/
	(binaryfunc)int_mul,	/*nb_multiply*/
	(binaryfunc)int_classic_div, /*nb_divide*/
	(binaryfunc)int_mod,	/*nb_remainder*/
	(binaryfunc)int_divmod,	/*nb_divmod*/
	(ternaryfunc)int_pow,	/*nb_power*/
	(unaryfunc)int_neg,	/*nb_negative*/
	(unaryfunc)int_pos,	/*nb_positive*/
	(unaryfunc)int_abs,	/*nb_absolute*/
	(inquiry)int_nonzero,	/*nb_nonzero*/
	(unaryfunc)int_invert,	/*nb_invert*/
	(binaryfunc)int_lshift,	/*nb_lshift*/
	(binaryfunc)int_rshift,	/*nb_rshift*/
	(binaryfunc)int_and,	/*nb_and*/
	(binaryfunc)int_xor,	/*nb_xor*/
	(binaryfunc)int_or,	/*nb_or*/
	int_coerce,		/*nb_coerce*/
	(unaryfunc)int_int,	/*nb_int*/
	(unaryfunc)int_long,	/*nb_long*/
	(unaryfunc)int_float,	/*nb_float*/
	(unaryfunc)int_oct,	/*nb_oct*/
	(unaryfunc)int_hex, 	/*nb_hex*/
	0,			/*nb_inplace_add*/
	0,			/*nb_inplace_subtract*/
	0,			/*nb_inplace_multiply*/
	0,			/*nb_inplace_divide*/
	0,			/*nb_inplace_remainder*/
	0,			/*nb_inplace_power*/
	0,			/*nb_inplace_lshift*/
	0,			/*nb_inplace_rshift*/
	0,			/*nb_inplace_and*/
	0,			/*nb_inplace_xor*/
	0,			/*nb_inplace_or*/
	(binaryfunc)int_div,	/* nb_floor_divide */
	int_true_divide,	/* nb_true_divide */
	0,			/* nb_inplace_floor_divide */
	0,			/* nb_inplace_true_divide */
	(unaryfunc)int_int,	/* nb_index */
};

PyTypeObject PyInt_Type = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,                          /* ob_size 变长对象所容纳元素个数 */
	"int",                      /* 类型名 */
	sizeof(PyIntObject),        /* 分配内存大小 */
	0,
	(destructor)int_dealloc,		/* tp_dealloc */
	(printfunc)int_print,			/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	(cmpfunc)int_compare,			/* tp_compare */
	(reprfunc)int_repr,			/* tp_repr */
	&int_as_number,				/* tp_as_number */
	0,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	(hashfunc)int_hash,			/* tp_hash */
        0,					/* tp_call */
        (reprfunc)int_repr,			/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
		Py_TPFLAGS_BASETYPE,		/* tp_flags */
	int_doc,				/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	int_methods,				/* tp_methods */
	0,					/* tp_members */
	0,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	0,					/* tp_init */
	0,					/* tp_alloc */
	int_new,				/* tp_new */
	(freefunc)int_free,           		/* tp_free */
};

int
_PyInt_Init(void)
{
	PyIntObject *v;
	int ival;
#if NSMALLNEGINTS + NSMALLPOSINTS > 0
	for (ival = -NSMALLNEGINTS; ival < NSMALLPOSINTS; ival++) {
        //如果free_list还不存在, 或者满了.
        //新建一块PyIntBlock, 并将空闲空间链表头部地址给free_list
        if (!free_list && (free_list = fill_free_list()) == NULL)
			return 0;
		/* PyObject_New is inlined */
		// 使用单向链表头位置
		v = free_list;

		// free_list指向单向链表下一个位置
		free_list = (PyIntObject *)v->ob_type;

		// 初始化对象v 类型是PyInt_Type,
		PyObject_INIT(v, &PyInt_Type);

		// 赋值 ival
		v->ob_ival = ival;

		// PyIntObject指针数组  索引（ival + NSMALLNEGINTS） 第一个 -5 + 5
		small_ints[ival + NSMALLNEGINTS] = v;
	}
#endif
	return 1;
}

void
PyInt_Fini(void)
{
	PyIntObject *p;
	PyIntBlock *list, *next;
	int i;
	unsigned int ctr;
	int bc, bf;	/* block count, number of freed blocks */
	int irem, isum;	/* remaining unfreed ints per block, total */

#if NSMALLNEGINTS + NSMALLPOSINTS > 0
        PyIntObject **q;

        i = NSMALLNEGINTS + NSMALLPOSINTS;
        q = small_ints;
        while (--i >= 0) {
                Py_XDECREF(*q);
                *q++ = NULL;
        }
#endif
	bc = 0;
	bf = 0;
	isum = 0;
	list = block_list;
	block_list = NULL;
	free_list = NULL;
	while (list != NULL) {
		bc++;
		irem = 0;
		for (ctr = 0, p = &list->objects[0];
		     ctr < N_INTOBJECTS;
		     ctr++, p++) {
			if (PyInt_CheckExact(p) && p->ob_refcnt != 0)
				irem++;
		}
		next = list->next;
		if (irem) {
			list->next = block_list;
			block_list = list;
			for (ctr = 0, p = &list->objects[0];
			     ctr < N_INTOBJECTS;
			     ctr++, p++) {
				if (!PyInt_CheckExact(p) ||
				    p->ob_refcnt == 0) {
					p->ob_type = (struct _typeobject *)
						free_list;
					free_list = p;
				}
#if NSMALLNEGINTS + NSMALLPOSINTS > 0
				else if (-NSMALLNEGINTS <= p->ob_ival &&
					 p->ob_ival < NSMALLPOSINTS &&
					 small_ints[p->ob_ival +
						    NSMALLNEGINTS] == NULL) {
					Py_INCREF(p);
					small_ints[p->ob_ival +
						   NSMALLNEGINTS] = p;
				}
#endif
			}
		}
		else {
			PyMem_FREE(list);
			bf++;
		}
		isum += irem;
		list = next;
	}
	if (!Py_VerboseFlag)
		return;
	fprintf(stderr, "# cleanup ints");
	if (!isum) {
		fprintf(stderr, "\n");
	}
	else {
		fprintf(stderr,
			": %d unfreed int%s in %d out of %d block%s\n",
			isum, isum == 1 ? "" : "s",
			bc - bf, bc, bc == 1 ? "" : "s");
	}
	if (Py_VerboseFlag > 1) {
		list = block_list;
		while (list != NULL) {
			for (ctr = 0, p = &list->objects[0];
			     ctr < N_INTOBJECTS;
			     ctr++, p++) {
				if (PyInt_CheckExact(p) && p->ob_refcnt != 0)
					/* XXX(twouters) cast refcount to
					   long until %zd is universally
					   available
					 */
					fprintf(stderr,
				"#   <int at %p, refcnt=%ld, val=%ld>\n",
						p, (long)p->ob_refcnt,
						p->ob_ival);
			}
			list = list->next;
		}
	}
}
