/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2014 Paul Sokolovsky
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "mpconfig.h"
#include "nlr.h"
#include "misc.h"
#include "qstr.h"
#include "obj.h"
#include "runtime0.h"
#include "runtime.h"
#include "binary.h"

#if MICROPY_PY_ARRAY || MICROPY_PY_BUILTINS_BYTEARRAY || MICROPY_PY_BUILTINS_MEMORYVIEW

// About memoryview object: We want to reuse as much code as possible from
// array, and keep the memoryview object 4 words in size so it fits in 1 GC
// block.  Also, memoryview must keep a pointer to the base of the buffer so
// that the buffer is not GC'd if the original parent object is no longer
// around (we are assuming that all memoryview'able objects return a pointer
// which points to the start of a GC chunk).  Given the above constraints we
// do the following:
//  - typecode high bit is set if the buffer is read-write (else read-only)
//  - free is the offset in elements to the first item in the memoryview
//  - len is the length in elements
//  - items points to the start of the original buffer
// Note that we don't handle the case where the original buffer might change
// size due to a resize of the original parent object.

// make (& TYPECODE_MASK) a null operation if memorview not enabled
#if MICROPY_PY_BUILTINS_MEMORYVIEW
#define TYPECODE_MASK (0x7f)
#else
#define TYPECODE_MASK (~(mp_uint_t)1)
#endif

typedef struct _mp_obj_array_t {
    mp_obj_base_t base;
    mp_uint_t typecode : 8;
    // free is number of unused elements after len used elements
    // alloc size = len + free
    mp_uint_t free : (8 * sizeof(mp_uint_t) - 8);
    mp_uint_t len; // in elements
    void *items;
} mp_obj_array_t;

STATIC mp_obj_t array_iterator_new(mp_obj_t array_in);
STATIC mp_obj_t array_append(mp_obj_t self_in, mp_obj_t arg);
STATIC mp_obj_t array_extend(mp_obj_t self_in, mp_obj_t arg_in);
STATIC mp_int_t array_get_buffer(mp_obj_t o_in, mp_buffer_info_t *bufinfo, mp_uint_t flags);

/******************************************************************************/
// array

#if MICROPY_PY_BUILTINS_BYTEARRAY || MICROPY_PY_ARRAY
STATIC void array_print(void (*print)(void *env, const char *fmt, ...), void *env, mp_obj_t o_in, mp_print_kind_t kind) {
    mp_obj_array_t *o = o_in;
    if (o->typecode == BYTEARRAY_TYPECODE) {
        print(env, "bytearray(b");
        mp_str_print_quoted(print, env, o->items, o->len, true);
    } else {
        print(env, "array('%c'", o->typecode);
        if (o->len > 0) {
            print(env, ", [");
            for (mp_uint_t i = 0; i < o->len; i++) {
                if (i > 0) {
                    print(env, ", ");
                }
                mp_obj_print_helper(print, env, mp_binary_get_val_array(o->typecode, o->items, i), PRINT_REPR);
            }
            print(env, "]");
        }
    }
    print(env, ")");
}
#endif

#if MICROPY_PY_BUILTINS_BYTEARRAY || MICROPY_PY_ARRAY
STATIC mp_obj_array_t *array_new(char typecode, mp_uint_t n) {
    int typecode_size = mp_binary_get_size('@', typecode, NULL);
    if (typecode_size <= 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "bad typecode"));
    }
    mp_obj_array_t *o = m_new_obj(mp_obj_array_t);
    #if MICROPY_PY_BUILTINS_BYTEARRAY && MICROPY_PY_ARRAY
    o->base.type = (typecode == BYTEARRAY_TYPECODE) ? &mp_type_bytearray : &mp_type_array;
    #elif MICROPY_PY_BUILTINS_BYTEARRAY
    o->base.type = &mp_type_bytearray;
    #else
    o->base.type = &mp_type_array;
    #endif
    o->typecode = typecode;
    o->free = 0;
    o->len = n;
    o->items = m_malloc(typecode_size * o->len);
    return o;
}
#endif

#if MICROPY_PY_BUILTINS_BYTEARRAY || MICROPY_PY_ARRAY
STATIC mp_obj_t array_construct(char typecode, mp_obj_t initializer) {
    uint len;
    // Try to create array of exact len if initializer len is known
    mp_obj_t len_in = mp_obj_len_maybe(initializer);
    if (len_in == MP_OBJ_NULL) {
        len = 0;
    } else {
        len = MP_OBJ_SMALL_INT_VALUE(len_in);
    }

    mp_obj_array_t *array = array_new(typecode, len);

    mp_obj_t iterable = mp_getiter(initializer);
    mp_obj_t item;
    mp_uint_t i = 0;
    while ((item = mp_iternext(iterable)) != MP_OBJ_STOP_ITERATION) {
        if (len == 0) {
            array_append(array, item);
        } else {
            mp_binary_set_val_array(typecode, array->items, i++, item);
        }
    }

    return array;
}
#endif

#if MICROPY_PY_ARRAY
STATIC mp_obj_t array_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 2, false);

    // get typecode
    mp_uint_t l;
    const char *typecode = mp_obj_str_get_data(args[0], &l);

    if (n_args == 1) {
        // 1 arg: make an empty array
        return array_new(*typecode, 0);
    } else {
        // 2 args: construct the array from the given iterator
        return array_construct(*typecode, args[1]);
    }
}
#endif

#if MICROPY_PY_BUILTINS_BYTEARRAY
STATIC mp_obj_t bytearray_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);

    if (n_args == 0) {
        // no args: construct an empty bytearray
        return array_new(BYTEARRAY_TYPECODE, 0);
    } else if (MP_OBJ_IS_SMALL_INT(args[0])) {
        // 1 arg, an integer: construct a blank bytearray of that length
        uint len = MP_OBJ_SMALL_INT_VALUE(args[0]);
        mp_obj_array_t *o = array_new(BYTEARRAY_TYPECODE, len);
        memset(o->items, 0, len);
        return o;
    } else {
        // 1 arg, an iterator: construct the bytearray from that
        return array_construct(BYTEARRAY_TYPECODE, args[0]);
    }
}
#endif

#if MICROPY_PY_BUILTINS_MEMORYVIEW
STATIC mp_obj_t memoryview_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    // TODO possibly allow memoryview constructor to take start/stop so that one
    // can do memoryview(b, 4, 8) instead of memoryview(b)[4:8] (uses less RAM)

    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);

    mp_obj_array_t *self = m_new_obj(mp_obj_array_t);
    self->base.type = type_in;
    self->typecode = bufinfo.typecode;
    self->free = 0;
    self->len = bufinfo.len / mp_binary_get_size('@', bufinfo.typecode, NULL); // element len
    self->items = bufinfo.buf;

    // test if the object can be written to
    if (mp_get_buffer(args[0], &bufinfo, MP_BUFFER_RW)) {
        self->typecode |= 0x80; // used to indicate writable buffer
    }

    return self;
}
#endif

STATIC mp_obj_t array_unary_op(mp_uint_t op, mp_obj_t o_in) {
    mp_obj_array_t *o = o_in;
    switch (op) {
        case MP_UNARY_OP_BOOL: return MP_BOOL(o->len != 0);
        case MP_UNARY_OP_LEN: return MP_OBJ_NEW_SMALL_INT(o->len);
        default: return MP_OBJ_NULL; // op not supported
    }
}

STATIC mp_obj_t array_binary_op(mp_uint_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
    mp_obj_array_t *lhs = lhs_in;
    switch (op) {
        case MP_BINARY_OP_ADD: {
            // allow to add anything that has the buffer protocol (extension to CPython)
            mp_buffer_info_t lhs_bufinfo;
            mp_buffer_info_t rhs_bufinfo;
            array_get_buffer(lhs_in, &lhs_bufinfo, MP_BUFFER_READ);
            mp_get_buffer_raise(rhs_in, &rhs_bufinfo, MP_BUFFER_READ);

            int sz = mp_binary_get_size('@', lhs_bufinfo.typecode, NULL);

            // convert byte count to element count (in case rhs is not multiple of sz)
            mp_uint_t rhs_len = rhs_bufinfo.len / sz;

            // note: lhs->len is element count of lhs, lhs_bufinfo.len is byte count
            mp_obj_array_t *res = array_new(lhs_bufinfo.typecode, lhs->len + rhs_len);
            mp_seq_cat((byte*)res->items, lhs_bufinfo.buf, lhs_bufinfo.len, rhs_bufinfo.buf, rhs_len * sz, byte);
            return res;
        }

        case MP_BINARY_OP_INPLACE_ADD: {
            #if MICROPY_PY_BUILTINS_MEMORYVIEW
            if (lhs->base.type == &mp_type_memoryview) {
                return MP_OBJ_NULL; // op not supported
            }
            #endif
            array_extend(lhs, rhs_in);
            return lhs;
        }

        case MP_BINARY_OP_EQUAL: {
            mp_buffer_info_t lhs_bufinfo;
            mp_buffer_info_t rhs_bufinfo;
            array_get_buffer(lhs_in, &lhs_bufinfo, MP_BUFFER_READ);
            if (!mp_get_buffer(rhs_in, &rhs_bufinfo, MP_BUFFER_READ)) {
                return mp_const_false;
            }
            return MP_BOOL(mp_seq_cmp_bytes(op, lhs_bufinfo.buf, lhs_bufinfo.len, rhs_bufinfo.buf, rhs_bufinfo.len));
        }

        default:
            return MP_OBJ_NULL; // op not supported
    }
}

#if MICROPY_PY_BUILTINS_BYTEARRAY || MICROPY_PY_ARRAY
STATIC mp_obj_t array_append(mp_obj_t self_in, mp_obj_t arg) {
    // self is not a memoryview, so we don't need to use (& TYPECODE_MASK)
    assert(MP_OBJ_IS_TYPE(self_in, &mp_type_array) || MP_OBJ_IS_TYPE(self_in, &mp_type_bytearray));
    mp_obj_array_t *self = self_in;

    if (self->free == 0) {
        int item_sz = mp_binary_get_size('@', self->typecode, NULL);
        // TODO: alloc policy
        self->free = 8;
        self->items = m_realloc(self->items,  item_sz * self->len, item_sz * (self->len + self->free));
        mp_seq_clear(self->items, self->len + 1, self->len + self->free, item_sz);
    }
    mp_binary_set_val_array(self->typecode, self->items, self->len++, arg);
    self->free--;
    return mp_const_none; // return None, as per CPython
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(array_append_obj, array_append);

STATIC mp_obj_t array_extend(mp_obj_t self_in, mp_obj_t arg_in) {
    // self is not a memoryview, so we don't need to use (& TYPECODE_MASK)
    assert(MP_OBJ_IS_TYPE(self_in, &mp_type_array) || MP_OBJ_IS_TYPE(self_in, &mp_type_bytearray));
    mp_obj_array_t *self = self_in;

    // allow to extend by anything that has the buffer protocol (extension to CPython)
    mp_buffer_info_t arg_bufinfo;
    mp_get_buffer_raise(arg_in, &arg_bufinfo, MP_BUFFER_READ);

    int sz = mp_binary_get_size('@', self->typecode, NULL);

    // convert byte count to element count
    mp_uint_t len = arg_bufinfo.len / sz;

    // make sure we have enough room to extend
    // TODO: alloc policy; at the moment we go conservative
    if (self->free < len) {
        self->items = m_realloc(self->items, (self->len + self->free) * sz, (self->len + len) * sz);
        self->free = 0;
    } else {
        self->free -= len;
    }

    // extend
    mp_seq_copy((byte*)self->items + self->len * sz, arg_bufinfo.buf, len * sz, byte);
    self->len += len;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(array_extend_obj, array_extend);
#endif

STATIC mp_obj_t array_subscr(mp_obj_t self_in, mp_obj_t index_in, mp_obj_t value) {
    if (value == MP_OBJ_NULL) {
        // delete item
        // TODO implement
        // TODO: confirmed that both bytearray and array.array support
        // slice deletion
        return MP_OBJ_NULL; // op not supported
    } else {
        mp_obj_array_t *o = self_in;
        if (0) {
#if MICROPY_PY_BUILTINS_SLICE
        } else if (MP_OBJ_IS_TYPE(index_in, &mp_type_slice)) {
            if (value != MP_OBJ_SENTINEL) {
                // Only getting a slice is suported so far, not assignment
                // TODO: confirmed that both bytearray and array.array support
                // slice assignment (incl. of different size)
                return MP_OBJ_NULL; // op not supported
            }
            mp_bound_slice_t slice;
            if (!mp_seq_get_fast_slice_indexes(o->len, index_in, &slice)) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_NotImplementedError,
                    "only slices with step=1 (aka None) are supported"));
            }
            mp_obj_array_t *res;
            int sz = mp_binary_get_size('@', o->typecode & TYPECODE_MASK, NULL);
            assert(sz > 0);
            if (0) {
                // dummy
            #if MICROPY_PY_BUILTINS_MEMORYVIEW
            } else if (o->base.type == &mp_type_memoryview) {
                res = m_new_obj(mp_obj_array_t);
                *res = *o;
                res->free += slice.start;
                res->len = slice.stop - slice.start;
            #endif
            } else {
                res = array_new(o->typecode, slice.stop - slice.start);
                memcpy(res->items, (uint8_t*)o->items + slice.start * sz, (slice.stop - slice.start) * sz);
            }
            return res;
#endif
        } else {
            mp_uint_t index = mp_get_index(o->base.type, o->len, index_in, false);
            #if MICROPY_PY_BUILTINS_MEMORYVIEW
            if (o->base.type == &mp_type_memoryview) {
                index += o->free;
                if (value != MP_OBJ_SENTINEL && (o->typecode & 0x80) == 0) {
                    // store to read-only memoryview
                    return MP_OBJ_NULL;
                }
            }
            #endif
            if (value == MP_OBJ_SENTINEL) {
                // load
                return mp_binary_get_val_array(o->typecode & TYPECODE_MASK, o->items, index);
            } else {
                // store
                mp_binary_set_val_array(o->typecode & TYPECODE_MASK, o->items, index, value);
                return mp_const_none;
            }
        }
    }
}

STATIC mp_int_t array_get_buffer(mp_obj_t o_in, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    mp_obj_array_t *o = o_in;
    int sz = mp_binary_get_size('@', o->typecode & TYPECODE_MASK, NULL);
    bufinfo->buf = o->items;
    bufinfo->len = o->len * sz;
    bufinfo->typecode = o->typecode & TYPECODE_MASK;
    #if MICROPY_PY_BUILTINS_MEMORYVIEW
    if (o->base.type == &mp_type_memoryview) {
        if ((o->typecode & 0x80) == 0 && (flags & MP_BUFFER_WRITE)) {
            // read-only memoryview
            return 1;
        }
        bufinfo->buf = (uint8_t*)bufinfo->buf + (mp_uint_t)o->free * sz;
    }
    #endif
    return 0;
}

#if MICROPY_PY_BUILTINS_BYTEARRAY || MICROPY_PY_ARRAY
STATIC const mp_map_elem_t array_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_append), (mp_obj_t)&array_append_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_extend), (mp_obj_t)&array_extend_obj },
};

STATIC MP_DEFINE_CONST_DICT(array_locals_dict, array_locals_dict_table);
#endif

#if MICROPY_PY_ARRAY
const mp_obj_type_t mp_type_array = {
    { &mp_type_type },
    .name = MP_QSTR_array,
    .print = array_print,
    .make_new = array_make_new,
    .getiter = array_iterator_new,
    .unary_op = array_unary_op,
    .binary_op = array_binary_op,
    .subscr = array_subscr,
    .buffer_p = { .get_buffer = array_get_buffer },
    .locals_dict = (mp_obj_t)&array_locals_dict,
};
#endif

#if MICROPY_PY_BUILTINS_BYTEARRAY
const mp_obj_type_t mp_type_bytearray = {
    { &mp_type_type },
    .name = MP_QSTR_bytearray,
    .print = array_print,
    .make_new = bytearray_make_new,
    .getiter = array_iterator_new,
    .unary_op = array_unary_op,
    .binary_op = array_binary_op,
    .subscr = array_subscr,
    .buffer_p = { .get_buffer = array_get_buffer },
    .locals_dict = (mp_obj_t)&array_locals_dict,
};
#endif

#if MICROPY_PY_BUILTINS_MEMORYVIEW
const mp_obj_type_t mp_type_memoryview = {
    { &mp_type_type },
    .name = MP_QSTR_memoryview,
    .make_new = memoryview_make_new,
    .getiter = array_iterator_new,
    .unary_op = array_unary_op,
    .binary_op = array_binary_op,
    .subscr = array_subscr,
    .buffer_p = { .get_buffer = array_get_buffer },
};
#endif

/* unused
mp_uint_t mp_obj_array_len(mp_obj_t self_in) {
    return ((mp_obj_array_t *)self_in)->len;
}
*/

#if MICROPY_PY_BUILTINS_BYTEARRAY
mp_obj_t mp_obj_new_bytearray(mp_uint_t n, void *items) {
    mp_obj_array_t *o = array_new(BYTEARRAY_TYPECODE, n);
    memcpy(o->items, items, n);
    return o;
}

// Create bytearray which references specified memory area
mp_obj_t mp_obj_new_bytearray_by_ref(mp_uint_t n, void *items) {
    mp_obj_array_t *o = m_new_obj(mp_obj_array_t);
    o->base.type = &mp_type_bytearray;
    o->typecode = BYTEARRAY_TYPECODE;
    o->free = 0;
    o->len = n;
    o->items = items;
    return o;
}
#endif

/******************************************************************************/
// array iterator

typedef struct _mp_obj_array_it_t {
    mp_obj_base_t base;
    mp_obj_array_t *array;
    mp_uint_t offset;
    mp_uint_t cur;
} mp_obj_array_it_t;

STATIC mp_obj_t array_it_iternext(mp_obj_t self_in) {
    mp_obj_array_it_t *self = self_in;
    if (self->cur < self->array->len) {
        return mp_binary_get_val_array(self->array->typecode & TYPECODE_MASK, self->array->items, self->offset + self->cur++);
    } else {
        return MP_OBJ_STOP_ITERATION;
    }
}

STATIC const mp_obj_type_t array_it_type = {
    { &mp_type_type },
    .name = MP_QSTR_iterator,
    .getiter = mp_identity,
    .iternext = array_it_iternext,
};

STATIC mp_obj_t array_iterator_new(mp_obj_t array_in) {
    mp_obj_array_t *array = array_in;
    mp_obj_array_it_t *o = m_new0(mp_obj_array_it_t, 1);
    o->base.type = &array_it_type;
    o->array = array;
    #if MICROPY_PY_BUILTINS_MEMORYVIEW
    if (array->base.type == &mp_type_memoryview) {
        o->offset = array->free;
    }
    #endif
    return o;
}

#endif // MICROPY_PY_ARRAY || MICROPY_PY_BUILTINS_BYTEARRAY || MICROPY_PY_BUILTINS_MEMORYVIEW
