
/* List object interface */

/*
Another generally useful object type is an list of object pointers.
This is a mutable type: the list items can be changed, and items can be
added or removed.  Out-of-range indices or non-list objects are ignored.

*** WARNING *** PyList_SetItem does not increment the new item's reference
count, but does decrement the reference count of the item it replaces,
if not nil.  It does *decrement* the reference count if it is *not*
inserted in the list.  Similarly, PyList_GetItem does not increment the
returned item's reference count.
*/

#ifndef Py_LISTOBJECT_H
#define Py_LISTOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif


/*
    创建list时，不是存多少东西申请多大内存，而是一次申请一大块内存。（内存管理策略，提高效率）
    申请的内存能存下多少个PyObject存放在allocated中

    allocated申请内存块总共能存放的对象数量
    PyObject_VAR_HEAD中的ob_size是实际存放对象元素的个数（实际使用的数量）

*/

typedef struct {
    //变长对象
    PyObject_VAR_HEAD
    /* Vector of pointers to list elements.  list[0] is ob_item[0], etc. */
    //ob_item为指向元素列表的指针，实际上，Python中的list[0]就是ob_item[0]
    //PyObject**是指向指针的指针，确保list可以同时管理不同类型的元素对象，而不像C++的向量，
    //只能管理同一类型的元素对象。实际上，它是一个指向PyObject数组类型的指针，这样才能做到list元素的随机访问。
    PyObject **ob_item;

    /* ob_item contains space for 'allocated' elements.  The number
     * currently in use is ob_size.
     * Invariants:
     *     0 <= ob_size <= allocated
     *     len(list) == ob_size
     *     ob_item == NULL implies ob_size == allocated == 0
     * list.sort() temporarily sets allocated to -1 to detect mutations.
     *
     * Items must normally not be NULL, except during construction when
     * the list is not yet visible outside the function that builds it.
     */
    //维护了当前列表中的可容纳的元素的总数
    Py_ssize_t allocated;
} PyListObject;

PyAPI_DATA(PyTypeObject) PyList_Type;

#define PyList_Check(op) PyObject_TypeCheck(op, &PyList_Type)
#define PyList_CheckExact(op) ((op)->ob_type == &PyList_Type)

PyAPI_FUNC(PyObject *) PyList_New(Py_ssize_t size);
PyAPI_FUNC(Py_ssize_t) PyList_Size(PyObject *);
PyAPI_FUNC(PyObject *) PyList_GetItem(PyObject *, Py_ssize_t);
PyAPI_FUNC(int) PyList_SetItem(PyObject *, Py_ssize_t, PyObject *);
PyAPI_FUNC(int) PyList_Insert(PyObject *, Py_ssize_t, PyObject *);
PyAPI_FUNC(int) PyList_Append(PyObject *, PyObject *);
PyAPI_FUNC(PyObject *) PyList_GetSlice(PyObject *, Py_ssize_t, Py_ssize_t);
PyAPI_FUNC(int) PyList_SetSlice(PyObject *, Py_ssize_t, Py_ssize_t, PyObject *);
PyAPI_FUNC(int) PyList_Sort(PyObject *);
PyAPI_FUNC(int) PyList_Reverse(PyObject *);
PyAPI_FUNC(PyObject *) PyList_AsTuple(PyObject *);
PyAPI_FUNC(PyObject *) _PyList_Extend(PyListObject *, PyObject *);

/* Macro, trading safety for speed */
#define PyList_GET_ITEM(op, i) (((PyListObject *)(op))->ob_item[i])
#define PyList_SET_ITEM(op, i, v) (((PyListObject *)(op))->ob_item[i] = (v))
#define PyList_GET_SIZE(op)    (((PyListObject *)(op))->ob_size)

#ifdef __cplusplus
}
#endif
#endif /* !Py_LISTOBJECT_H */
