//
// Created by Carter on 2023-03-11.
//

#pragma once

#include "core/format.h"
#include "core/utils.h"
#include <stdbool.h>

/*
 * Front-end types, including a simple module system based on M. Lillibridge's
 * translucent sums, and HM-style polymorphism. Types should always be created
 * via a `TypeTable` object.
 */

// clang-format off

#define UNSIGNED_INTEGER_TYPE_LIST(f) \
    f(U8,   "u8")         \
    f(U16,  "u16")        \
    f(U32,  "u32")        \
    f(U64,  "u64")           \

#define SIGNED_INTEGER_TYPE_LIST(f) \
    f(I8,   "i8")         \
    f(I16,  "i16")        \
    f(I32,  "i32")        \
    f(I64,  "i64")        \

#define INTEGER_TYPE_LIST(f)        \
    UNSIGNED_INTEGER_TYPE_LIST(f)   \
    SIGNED_INTEGER_TYPE_LIST(f)     \

#define FLOAT_TYPE_LIST(f)          \
    f(F32,  "f32")                  \
    f(F64,  "f64")

#define PRIM_TYPE_LIST(f) \
    f(Bool, "bool")       \
    f(Char, "char")       \
    INTEGER_TYPE_LIST(f)  \
    FLOAT_TYPE_LIST(f)

typedef enum {
#define f(name, ...) prt##name,
    PRIM_TYPE_LIST(f)
#undef f
    prtCOUNT
} PrtId;

// clang-format on

typedef enum {
    typError,
    typAuto,
    typVoid,
    typNull,
    typPrimitive,
    typString,
    typPointer,
    typArray,
    typMap,
    typAlias,
    typUnion,
    typTuple,
    typFunc,
    typEnum,
    typStruct
} TTag;

typedef struct Type Type;
typedef struct TypeTable TypeTable;

typedef struct StructField {
    const char *name;
    const Type *type;
} StructField;

typedef struct EnumOption {
    const char *name;
    u64 value;
} EnumOption;

typedef struct Type {
    TTag tag;
    u64 size;
    cstring name;

    struct {
        PrtId id;
    } primitive;

    struct {
        bool isConst : 1;
        const Type *pointed;
    } pointer;

    struct {
        const u64 *indexes;
        u64 arity;
        const Type *elementType;
    } array;

    struct {
        const Type *key;
        const Type *value;
    } map;

    struct {
        const Type *aliased;
    } alias;

    struct {
        u64 count;
        const Type **members;
    } tUnion;

    struct {
        u64 count;
        const Type **members;
    } tuple;

    struct {
        bool isVariadic : 1;
        u32 paramsCount;
        const Type *retType;
        const Type **params;
    } func;

    struct {
        const Type *base;
        EnumOption **options;
        u64 optionsCount;
    } tEnum;

    struct {
        const Type *base;
        StructField **fields;
        u64 fieldsCount;
    } tStruct;
} Type;

bool isTypeAssignableFrom(TypeTable *table, const Type *to, const Type *from);
bool isIntegerType(TypeTable *table, const Type *type);
bool isSignedType(TypeTable *table, const Type *type);
bool isUnsignedType(TypeTable *table, const Type *type);
bool isFloatType(TypeTable *table, const Type *type);

void printType(FormatState *state, const Type *type);
