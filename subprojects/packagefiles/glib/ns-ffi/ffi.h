/* Weststar SDL Browser — libffi's names, for GObject's generic marshaller.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * gobject/gclosure.c calls through libffi in g_cclosure_marshal_generic,
 * the marshaller of a signal declared without one. No signal the browser
 * reaches is such a signal, so this header stands in for <ffi.h>: enough
 * for gclosure.c to compile, and an abort should the generic marshaller
 * ever be reached.
 */

#ifndef NS_FFI_STUB_H
#define NS_FFI_STUB_H

#include <stdio.h>
#include <stdlib.h>

#include <stddef.h>

typedef struct { size_t size; } ffi_type;
typedef struct { int unused; } ffi_cif;
typedef unsigned long ffi_arg;
typedef long ffi_sarg;
typedef enum { FFI_OK = 0, FFI_BAD_TYPEDEF, FFI_BAD_ABI } ffi_status;
typedef enum { FFI_DEFAULT_ABI = 0 } ffi_abi;

static ffi_type ns_ffi_no_type;
#define ffi_type_void    ns_ffi_no_type
#define ffi_type_pointer ns_ffi_no_type
#define ffi_type_sint    ns_ffi_no_type
#define ffi_type_uint    ns_ffi_no_type
#define ffi_type_sint8   ns_ffi_no_type
#define ffi_type_uint8   ns_ffi_no_type
#define ffi_type_sint16  ns_ffi_no_type
#define ffi_type_uint16  ns_ffi_no_type
#define ffi_type_sint32  ns_ffi_no_type
#define ffi_type_uint32  ns_ffi_no_type
#define ffi_type_sint64  ns_ffi_no_type
#define ffi_type_uint64  ns_ffi_no_type
#define ffi_type_slong   ns_ffi_no_type
#define ffi_type_ulong   ns_ffi_no_type
#define ffi_type_float   ns_ffi_no_type
#define ffi_type_double  ns_ffi_no_type

static inline ffi_status
ffi_prep_cif(ffi_cif *cif, ffi_abi abi, unsigned int nargs, ffi_type *rtype,
             ffi_type **atypes)
{
    (void)cif; (void)abi; (void)nargs; (void)rtype; (void)atypes;
    fputs("GObject: a signal without a marshaller needs libffi, which this "
          "build of GLib leaves out\n", stderr);
    abort();
}

static inline void
ffi_call(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue)
{
    (void)cif; (void)fn; (void)rvalue; (void)avalue;
    abort();
}

#endif /* NS_FFI_STUB_H */
