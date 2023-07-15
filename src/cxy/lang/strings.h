//
// Created by Carter Mbotho on 2023-07-13.
//

#pragma once

#include <core/utils.h>
#include <lang/operator.h>

// clang-format off
#define CXY_BUILTIN_NAMES(f, ff)    \
    f(main)                     \
    f(__builtin_alloc)          \
    f(__builtin_dealloc)        \
    f(__builtin_realloc)        \
    f(__builtin_alloc_slice)    \
    f(__builtin_dealloc_slice)   \
    f(__builtin_realloc_slice)  \
    f(__builtin_memset_slice)   \
    f(__builtin_init_slice)     \
    f(__builtin_free_slice)     \
    f(__builtin_assert)         \
    f(__builtin_sizeof)         \
    f(this)                     \
    f(This)                     \
    f(super)                    \
    f(static)                   \
    f(transient)                \
    f(inline)                   \
    f(explicit)                 \
    f(strlen)                   \
    f(char)                     \
    f(wputc)                    \
    f(toString)                 \
    f(cxy_range_t)              \
    f(StringBuilder)            \
    f(Optional)                 \
    ff(_assert, "assert")       \
    f(base_of)                  \
    f(column)                   \
    f(cstr)                     \
    f(data)                     \
    f(destructor)               \
    f(Destructor)               \
    f(file)                     \
    f(len)                      \
    f(line)                     \
    f(mkIdent)                  \
    f(mkInteger)                \
    f(ptroff)                   \
    f(sizeof)                   \
    f(typeof)                   \
    f(alias)                    \
    f(name)                     \
    f(unchecked)

// clang-format on

#define f(name, ...) extern cstring S_##name;
CXY_BUILTIN_NAMES(f, f)
AST_UNARY_EXPR_LIST(f)
AST_BINARY_EXPR_LIST(f)
AST_OVERLOAD_ONLY_OPS(f)
#undef f
#define f(name, ...) extern cstring S_##name##_eq;
AST_ASSIGN_EXPR_LIST(f)
#undef f

typedef struct StrPool StrPool;

void internCommonStrings(StrPool *pool);
