//
// Created by Carter Mbotho on 2023-07-13.
//

#pragma once

#include "core/utils.h"
#include "operator.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define CXY_BUILTIN_NAMES(f, ff)    \
    f(main)                         \
    f(__builtins)               \
    f(__cxy_assert)             \
    f(CXY__builtins_sizeof)     \
    f(this)                     \
    f(This)                     \
    f(super)                    \
    f(static)                   \
    f(transient)                \
    f(abstract)                 \
    f(inline)                   \
    f(noinline)                 \
    f(optimize)                 \
    f(volatile)                 \
    f(explicit)                 \
    f(pure)                     \
    f(strlen)                   \
    f(memset)                   \
    f(char)                     \
    f(wputc)                    \
    f(cxy_range_t)              \
    f(sb)                       \
    f(s)                        \
    f(Optional)               \
    f(Slice)                    \
    f(String)                   \
    f(__string)                  \
    f(Iterator)                 \
    ff(_assert, "assert")       \
    f(base_of)                  \
    f(column)                   \
    f(ptr)                      \
    f(cstr)                     \
    f(data)                     \
    f(init)                     \
    f(destructor)               \
    f(file)                     \
    f(len)                      \
    f(line)                     \
    f(mkIdent)                  \
    f(mkInteger)                \
    f(ptroff)                   \
    f(sizeof)                   \
    f(typeof)                   \
    f(allocate)                 \
    f(alias)                    \
    f(align)                    \
    f(name)                     \
    f(None)                     \
    f(Some)                     \
    f(unchecked)                \
    f(unused)                   \
    f(_Variadic)                \
    f(consistent)               \
    f(final)                    \
    f(getref)                   \
    f(dropref)                  \
    f(structToString)           \
    f(classToString)            \
    f(computeStructHash)        \
    f(computeClassHash)         \
    f(destructorForward)        \
    f(newClass)                 \
    f(release)                  \
    f(vtable)                   \
    f(poco)                     \
    f(Promise)                  \
    f(Coroutine)                \
    f(asyncLaunch)              \
    f(asyncLaunchMember)        \
    f(allTestCases)             \
    f(sync)                     \
    f(self)                     \
    f(External)                 \
    f(Appending)                \
    f(linkage)                  \
    f(section)                  \
    f(packed)                   \
    f(Exception)                \
    f(Void)                     \
    f(what)                     \
    f(push)                     \
    f(ex)                       \
    f(thread)                   \
    f(likely)                   \
    f(unlikely)                 \
    f(atomic)                   \
    f(__init)                   \
    f(__defaults_init)          \
    f(__startup)                \
    f(__name)                   \
    f(__construct0)             \
    f(__construct1)             \
    f(__fwd)                    \
    f(__override_builtin)       \
    f(__copy)                   \
    f(__destroy)                \
    f(__destructor_fwd)         \
    f(__tuple_dctor)            \
    f(__tuple_copy)             \
    f(__union_dctor)            \
    f(__union_copy)             \
    f(__async)                  \
    f(__CxyPluginAction)        \
    f(__tid)                    \
    f(CXY__main)                \
    f(resolve)                  \
    f(reject)                   \
    f(result)                   \
    f(clib)                     \
    f(src)                      \
    ff(__LLVM_global_ctors, "llvm.global_ctors") \
    ff(ctor_section,        ".ctor")             \
    ff(sptr_ref,            "__smart_ptr_get")   \
    ff(sptr_drop,           "__smart_ptr_drop")  \
    ff(sptr_alloc,          "__smart_ptr_alloc") \
    ff(sptr_alloc_trace,    "__smart_ptr_alloc_trace") \
    ff(sptr_drop_trace,     "__smart_ptr_drop_trace")  \
    ff(sptr_get_trace,      "__smart_ptr_get_trace")   \
    ff(copy,                "__copy")            \
    ff(AsmInputPrefix,      "\"r\"")             \
    ff(AsmOutputPrefix,     "\"=r\"")

#define OVERRIDABLE_BUILTINS(f)    \
    f(__scheduler)                 \
    f(__thread_launch)             \
    f(timestamp)                   \
    f(fdWaitRead)                  \
    f(fdWaitWrite)                 \
    f(sleepAsync)                  \
    f(__smart_ptr_alloc)           \
    f(__smart_ptr_alloc_trace)

// clang-format on

#define f(name, ...) extern cstring S_##name;
CXY_BUILTIN_NAMES(f, f)
AST_UNARY_EXPR_LIST(f)
AST_BINARY_EXPR_LIST(f)
OVERRIDABLE_BUILTINS(f)
f(Truthy);
#undef f

#define f(name, ...)                                                           \
    extern cstring S_##name;                                                   \
    extern cstring S_##name##_;
AST_OVERLOAD_ONLY_OPS(f)
#undef f
extern cstring S_Deref_;

#define f(name, ...) extern cstring S_##name##_eq;
AST_ASSIGN_EXPR_LIST(f)
#undef f

typedef struct StrPool StrPool;

void internCommonStrings(StrPool *pool);

#ifdef __cplusplus
}
#endif
