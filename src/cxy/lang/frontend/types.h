//
// Created by Carter on 2023-03-11.
//

#pragma once

#include "core/array.h"
#include "core/format.h"
#include "core/utils.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Front-end types, including a simple module system based on M. Lillibridge's
 * translucent sums, and HM-style polymorphism. Types should always be created
 * via a `TypeTable` object.
 */

// clang-format off
#define UNSIGNED_INTEGER_TYPE_LIST(f) \
    f(U8,   "u8", 1)         \
    f(U16,  "u16", 2)        \
    f(U32,  "u32", 4)        \
    f(U64,  "u64", 8)        \

#define SIGNED_INTEGER_TYPE_LIST(f) \
    f(I8,   "i8", 1)         \
    f(I16,  "i16", 2)        \
    f(I32,  "i32", 4)        \
    f(I64,  "i64", 8)        \

#define INTEGER_TYPE_LIST(f)        \
    UNSIGNED_INTEGER_TYPE_LIST(f)   \
    SIGNED_INTEGER_TYPE_LIST(f)     \

#define FLOAT_TYPE_LIST(f)          \
    f(F32,  "f32", 4)                  \
    f(F64,  "f64", 8)

#define PRIM_TYPE_LIST(f)  \
    f(Bool, "bool", 1)     \
    f(Char, "wchar", 4)    \
    f(CChar, "char", 1)    \
    INTEGER_TYPE_LIST(f)   \
    FLOAT_TYPE_LIST(f)

typedef enum {
#define f(name, ...) prt## name,
    PRIM_TYPE_LIST(f)
#undef f
    prtCOUNT
} PrtId;

// clang-format on

typedef enum {
    typError,
    typContainer,
    typLiteral,
    typAuto,
    typVoid,
    typNull,
    typInfo,
    typThis,
    typPrimitive,
    typString,
    typPointer,
    typReference,
    typOptional,
    typArray,
    typMap,
    typAlias,
    typUnion,
    typUntaggedUnion,
    typOpaque,
    typTuple,
    typFunc,
    typEnum,
    typModule,
    typStruct,
    typClass,
    typInterface,
    typGeneric,
    typApplied,
    typWrapped,
    typException,
    typResult,
} TTag;

typedef struct Type Type;
typedef struct TypeTable TypeTable;
typedef struct Env Env;
typedef struct AstNode AstNode;

typedef struct NamedTypeMember {
    const char *name;
    const Type *type;
    const AstNode *decl;
} NamedTypeMember;

typedef struct TypeMemberContainer {
    NamedTypeMember *members;
    NamedTypeMember **sortedMembers;
    u64 count;
} TypeMembersContainer;

typedef struct TypeInheritance {
    const Type *base;
    const Type **interfaces;
    u64 interfacesCount;
} TypeInheritance;

typedef struct GenericParam {
    const char *name;
    u32 inferIndex;
} GenericParam;

typedef struct EnumOptionDecl {
    const char *name;
    i64 value;
    AstNode *decl;
} EnumOptionDecl;

typedef struct UnionMember {
    const Type *type;
    void *codegen;
} UnionMember;

typedef struct AppliedTypeParams {
    const Type **params;
    u64 count;
} AppliedTypeParams;

struct MirType;

#define CXY_TYPE_HEAD                                                          \
    TTag tag;                                                                  \
    u64 size;                                                                  \
    u64 index;                                                                 \
    u64 flags;                                                                 \
    cstring name;                                                              \
    cstring ns;                                                                \
    const Type *from;                                                          \
    void *dbg;                                                                 \
    union {                                                                    \
        void *codegen;                                                         \
        struct {                                                               \
            bool generating;                                                   \
            bool generated;                                                    \
        };                                                                     \
    };                                                                         \
    const Type *retyped;

#ifdef __cpluplus

#endif

typedef struct Type {
    union {
        struct {
            CXY_TYPE_HEAD
        };
        struct {
            CXY_TYPE_HEAD
        } _head;
    };

    union {
        struct {
            u8 _[1];
        } _body;

        struct {
            PrtId id;
        } primitive;

        struct {
            const Type *pointed;
        } pointer;

        struct {
            const Type *referred;
        } reference;

        struct {
            bool trackReferences;
            DynArray references;
            const Type *that;
        } _this;

        struct {
            u64 len;
            const Type *elementType;
        } array;

        struct {
            const Type *key;
            const Type *value;
        } map;

        struct {
            const Type *aliased;
            AstNode *decl;
        } alias;

        struct {
            AstNode *decl;
        } opaque;

        struct {
            const Type *target;
        } optional, info, wrapped;

        struct {
            u64 count;
            UnionMember *members;
            const Type *copyFunc;
            const Type *destructorFunc;
            void *codegenTag;
        } tUnion;

        struct {
            TypeMembersContainer *members;
            AstNode *decl;
        } untaggedUnion;

        struct {
            u64 count;
            const Type **members;
            const Type *copyFunc;
            const Type *destructorFunc;
        } tuple;

        struct {
            u16 paramsCount;
            u16 capturedNamesCount;
            u16 defaultValuesCount;
            const Type *retType;
            const Type **params;
            const char **captureNames;
            AstNode *decl;
        } func;

        struct {
            const Type *base;
            cstring *names;
            u64 namesCount;
        } container;

        struct {
            const AstNode *value;
        } literal;

        struct {
            TypeMembersContainer *members;
            struct {
                const Type **items;
                u64 count;
            } _incs;
            cstring path;
        } module;

        struct {
            const Type *base;
            EnumOptionDecl *options;
            EnumOptionDecl **sortedOptions;
            u64 optionsCount;
            AstNode *decl;
        } tEnum;

        struct {
            TypeMembersContainer *members;
            AstNode *decl;
            const Type *initializer;
        } tStruct;

        struct {
            TypeMembersContainer *members;
            AstNode *decl;
            const Type *initializer;
            TypeInheritance *inheritance;
        } tClass;

        struct {
            TypeMembersContainer *members;
            AstNode *decl;
        } tInterface;

        struct {
            GenericParam *params;
            u64 paramsCount;
            AstNode *decl;
            bool inferrable;
        } generic;

        struct {
            const Type **args;
            u32 argsCount;
            u64 totalArgsCount;
            const Type *from;
            AstNode *decl;
        } applied;

        struct {
            AstNode *decl;
        } exception;

        struct {
            const Type *target;
        } result;
    };
} Type;

typedef CxyPair(i64, u64) IntMinMax;

#define CYX_TYPE_BODY_SIZE (sizeof(Type) - sizeof(((Type *)0)->_head))

static inline bool typeIs_(const Type *type, TTag tag)
{
    return type && type->tag == tag;
}

#define typeIs(T, TAG) typeIs_((T), typ##TAG)

bool compareTypes(const Type *lhs, const Type *rhs);

bool compareFuncTypes(const Type *lhs, const Type *rhs, bool ignoreNames);

bool isTypeAssignableFrom(const Type *to, const Type *from);

bool isTypeCastAssignable(const Type *to, const Type *from);

bool isPrimitiveTypeBigger(const Type *to, const Type *from);

bool isIntegerType(const Type *type);

bool isIntegralType(const Type *type);

bool isSignedType(const Type *type);

bool isSignedIntegerType(const Type *type);

bool isUnsignedType(const Type *type);

bool isFloatType(const Type *type);

bool isNumericType(const Type *type);

bool isBuiltinType(const Type *type);

bool isBooleanType(const Type *type);

bool isCharacterType(const Type *type);

bool isArrayType(const Type *type);

bool isPointerType(const Type *type);

bool isReferenceType(const Type *type);

bool isPointerOrReferenceType(const Type *type);

bool isReferable(const Type *type);

bool isVoidPointer(const Type *type);

bool isVoidType(const Type *type);

bool isClassType(const Type *type);

bool isComplexType(const Type *type);

bool isClassReferenceType(const Type *type);

bool isStructType(const Type *type);

bool isTupleType(const Type *type);

bool isUnionType(const Type *type);

bool isConstType(const Type *type);

bool isResultType(const Type *type);

bool isExceptionType(const Type *type);

bool isBaseExceptionType(const Type *type);

bool isVoidResultType(const Type *type);

bool typeIsBaseOf(const Type *base, const Type *type);

bool hasReferenceMembers(const Type *type);

bool isDestructible(const Type *type);

static inline bool isClassOrStructType(const Type *type)
{
    return isClassType(type) || isStructType(type);
}

static inline bool isStructPointer(const Type *type)
{
    switch (type->tag) {
    case typPointer:
        return typeIs(type->pointer.pointed, Struct) ||
               isStructPointer(type->pointer.pointed);
    case typThis:
        return typeIs(type->_this.that, Struct);
    default:
        return false;
    }
}

const char *getPrimitiveTypeName(PrtId tag);

u64 getPrimitiveTypeSizeFromTag(PrtId tag);

static inline u64 getPrimitiveTypeSize(const Type *type)
{
    csAssert0(typeIs_(type, typPrimitive));
    return getPrimitiveTypeSizeFromTag(type->primitive.id);
}

void printType_(FormatState *state, const Type *type, bool keyword);

static inline void printType(FormatState *state, const Type *type)
{
    printType_(state, type, true);
}

bool isSliceType(const Type *type);

IntMinMax getIntegerTypeMinMax(const Type *id);

static inline const Type *unThisType(const Type *_this)
{
    return typeIs(_this, This) ? _this->_this.that : _this;
}

static inline const Type *maybeUnThisType(const Type *_this)
{
    return typeIs(_this, This) ? _this->_this.that ?: _this : _this;
}

const NamedTypeMember *findOverloadMemberUpInheritanceChain(const Type *type,
                                                            cstring member);

const NamedTypeMember *findNamedTypeMemberInContainer(
    const TypeMembersContainer *container, cstring member);

static inline const NamedTypeMember *findStructMember(const Type *type,
                                                      cstring member)
{
    return findNamedTypeMemberInContainer(unThisType(type)->tStruct.members,
                                          member);
}

static inline const Type *findStructMemberType(const Type *type, cstring member)
{
    const NamedTypeMember *found = findStructMember(type, member);
    return found ? found->type : NULL;
}

static inline const NamedTypeMember *findClassMember(const Type *type,
                                                     cstring member)
{
    return findNamedTypeMemberInContainer(unThisType(type)->tClass.members,
                                          member);
}

static inline const Type *findClassMemberType(const Type *type, cstring member)
{
    const NamedTypeMember *found = findClassMember(type, member);
    return found ? found->type : NULL;
}

static inline const NamedTypeMember *findUntaggedUnionMember(const Type *type,
                                                             cstring member)
{
    return findNamedTypeMemberInContainer(type->untaggedUnion.members, member);
}

static inline const Type *findUntaggedUnionMemberType(const Type *type,
                                                      cstring member)
{
    const NamedTypeMember *found = findUntaggedUnionMember(type, member);
    return found ? found->type : NULL;
}

const TypeInheritance *getTypeInheritance(const Type *type);

const Type *getTypeBase(const Type *type);

bool implementsInterface(const Type *type, const Type *inf);

static inline const NamedTypeMember *findInterfaceMember(const Type *type,
                                                         cstring member)
{
    return findNamedTypeMemberInContainer(type->tInterface.members, member);
}

static inline const Type *findInterfaceMemberType(const Type *type,
                                                  cstring member)
{
    const NamedTypeMember *found = findInterfaceMember(type, member);
    return found ? found->type : NULL;
}

const EnumOptionDecl *findEnumOption(const Type *type, cstring member);

static inline const Type *findEnumOptionType(const Type *type, cstring member)
{
    const EnumOptionDecl *found = findEnumOption(type, member);
    return found ? type : NULL;
}

static const NamedTypeMember *findModuleMember(const Type *type, cstring member)
{
    if (type == NULL)
        return NULL;
    const NamedTypeMember *found =
        findNamedTypeMemberInContainer(type->module.members, member);
    if (found != NULL || type->module._incs.count == 0)
        return found;

    for (int i = 0; i < type->module._incs.count; i++) {
        found = findModuleMember(type->module._incs.items[i], member);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static inline const Type *findModuleMemberType(const Type *type, cstring member)
{
    const NamedTypeMember *found = findModuleMember(type, member);
    return found ? found->type : NULL;
}

int sortCompareStructMember(const void *lhs, const void *rhs);

TypeMembersContainer *makeTypeMembersContainer(TypeTable *types,
                                               const NamedTypeMember *members,
                                               u64 count);

TypeInheritance *makeTypeInheritance(TypeTable *types,
                                     const Type *base,
                                     const Type **interfaces,
                                     u64 interfaceCount);

AstNode *findMemberDeclInType(const Type *type, cstring name);

const Type *getPointedType(const Type *type);

bool isTruthyType(const Type *type);

const Type *getOptionalType();

const Type *getOptionalTargetType(const Type *type);

const Type *getSliceTargetType(const Type *type);

const Type *getResultTargetType(const Type *type);

u32 findUnionTypeIndex(const Type *tagged, const Type *type);

void pushThisReference(const Type *_this, AstNode *node);

void resolveThisReferences(TypeTable *table,
                           const Type *_this,
                           const Type *type);

#ifdef __cplusplus
}
#endif
