//
// Created by Carter on 2023-04-27.
//

#include "eval.h"

#include "lang/middle/builtins.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"

typedef AstNode *(*AstNodeMemberGetter)(EvalContext *,
                                        const FileLoc *,
                                        AstNode *,
                                        AstNode *);

typedef struct {
    cstring name;
    AstNodeMemberGetter get;
} AstNodeGetMember;

static HashTable structDeclMembers;
static HashTable fieldDeclMembers;
static HashTable defaultMembers;

static bool compareAstNodeGetMember(const void *lhs, const void *rhs)
{
    return ((const AstNodeGetMember *)lhs)->name ==
           ((const AstNodeGetMember *)rhs)->name;
}

static AstNodeMemberGetter findAstNodeGetter(HashTable *table, cstring name)
{
    AstNodeGetMember get = {.name = name};
    HashCode hash = hashStr(hashInit(), name);
    AstNodeGetMember *found = findInHashTable(
        table, &get, hash, sizeof(get), compareAstNodeGetMember);
    if (found)
        return found->get;
    return NULL;
}

static void insertAstNodeGetter(HashTable *table,
                                cstring name,
                                AstNodeMemberGetter getter)
{
    AstNodeGetMember get = {.name = name, .get = getter};
    HashCode hash = hashStr(hashInit(), name);
    bool found = insertInHashTable(
        table, &get, hash, sizeof(get), compareAstNodeGetMember);

    csAssert0(found);
}

static inline AstNode *comptimeWrapped(EvalContext *ctx,
                                       const FileLoc *loc,
                                       AstNode *node,
                                       u64 flags)
{
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astComptimeOnly, .next = node, .flags = flags});
}

static inline const Type *actualType(const Type *type)
{
    if (type == NULL)
        return type;

    switch (type->tag) {
    case typWrapped:
        return unwrapType(type, NULL);
    case typInfo:
        return type->info.target;
    default:
        return type;
    }
}

static AstNode *getName(EvalContext *ctx,
                        const FileLoc *loc,
                        AstNode *node,
                        attr(unused) attr(unused) AstNode *args)
{
    if (node == NULL)
        return NULL;

    cstring name = NULL;
    switch (node->tag) {
    case astFieldDecl:
    case astStructDecl:
    case astClassDecl:
    case astEnumDecl:
    case astFuncDecl:
    case astFuncParamDecl:
    case astVarDecl:
    case astEnumOptionDecl:
    case astAttr:
    case astAnnotation:
    case astTypeDecl:
        name = node->_name;
        break;
    case astPrimitiveType:
        name = getPrimitiveTypeName(node->primitiveType.id);
        break;
    case astGenericParam:
        return getName(ctx, loc, getTypeDecl(stripAll(node->type)), NULL);
    default:
        return NULL;
    }

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astStringLit, .stringLiteral.value = name});
}

static AstNode *getValue(EvalContext *ctx,
                         const FileLoc *loc,
                         AstNode *node,
                         attr(unused) attr(unused) AstNode *args)
{
    if (node == NULL)
        return NULL;

    switch (node->tag) {
    case astAnnotation:
        if (node->annotation.value)
            return deepCloneAstNode(ctx->pool, node->annotation.value);
        return NULL;
    case astLiteral:
        return deepCloneAstNode(ctx->pool, node->literal.value);
    default:
        break;
    }
    const Type *type = resolveUnThisUnwrapType(node->type);
    if (typeIs(type, Literal))
        return deepCloneAstNode(ctx->pool, type->literal.value);
    return NULL;
}

static AstNode *getMembers(EvalContext *ctx,
                           attr(unused) const FileLoc *loc,
                           AstNode *node,
                           attr(unused) AstNode *args)
{
    if (node == NULL)
        return NULL;

    const Type *type = resolveUnThisUnwrapType(stripAll(node->type));
    if (nodeIs(node, TypeRef) && isClassOrStructType(type)) {
        node = getTypeDecl(type);
        if (node == NULL)
            return NULL;
    }

    switch (node->tag) {
    case astTupleType:
        return comptimeWrapped(
            ctx, &node->loc, node->tupleType.elements, flgComptimeIterable);
    case astUnionDecl:
        return comptimeWrapped(
            ctx, &node->loc, node->unionDecl.members, flgComptimeIterable);
    case astStructDecl:
    case astClassDecl:
        return comptimeWrapped(
            ctx, &node->loc, node->structDecl.members, flgComptimeIterable);
    case astGenericParam:
        return getMembers(ctx, loc, getTypeDecl(type), NULL);
    case astEnumDecl:
        return comptimeWrapped(
            ctx, &node->loc, node->enumDecl.options, flgComptimeIterable);
    default:
        break;
    }
    return NULL;
}

static AstNode *getAttributes(EvalContext *ctx,
                              attr(unused) const FileLoc *loc,
                              AstNode *node,
                              attr(unused) AstNode *args)
{
    AstNode *decl = node;
    if (nodeIs(decl, GenericParam))
        decl = getTypeDecl(decl->type);
    if (decl == NULL)
        return NULL;

    return comptimeWrapped(ctx, &node->loc, decl->attrs, flgComptimeIterable);
}

static AstNode *getAnnotations(EvalContext *ctx,
                               attr(unused) const FileLoc *loc,
                               AstNode *node,
                               attr(unused) AstNode *args)
{
    if (isClassOrStructType(node->type)) {
        AstNode *decl = getTypeDecl(node->type);
        return comptimeWrapped(
            ctx, &node->loc, decl->classDecl.annotations, flgComptimeIterable);
    }
    else {
        return comptimeWrapped(ctx, &node->loc, NULL, flgComptimeIterable);
    }
}

static AstNode *getMembersCount(EvalContext *ctx,
                                attr(unused) const FileLoc *loc,
                                AstNode *node,
                                attr(unused) AstNode *args)
{
    if (node == NULL)
        return NULL;

    const Type *type = resolveType(stripAll(node->type));
    u64 count = 0;
    switch (type->tag) {
    case typTuple:
        count = type->tuple.count;
        break;
    case typUnion:
        count = type->tUnion.count;
        break;
    case typClass:
        count = type->tClass.members->count;
        break;
    case typStruct:
        count = type->tStruct.members->count;
        break;
    default:
        return NULL;
    }
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astIntegerLit, .intLiteral.uValue = count});
}

static AstNode *getTypeInfo(EvalContext *ctx,
                            attr(unused) const FileLoc *loc,
                            AstNode *node,
                            attr(unused) AstNode *args)
{
    switch (node->tag) {
    case astFuncParamDecl:
        if (hasFlag(node, Variadic))
            return comptimeWrapped(ctx,
                                   &node->loc,
                                   node->funcParam.type->tupleType.elements,
                                   flgComptimeIterable);

        return node->funcParam.type;
    default:
        break;
    }
    return NULL;
}

static AstNode *getElementType(EvalContext *ctx,
                               const FileLoc *loc,
                               AstNode *node,
                               attr(unused) AstNode *args)
{
    const Type *type = actualType(node->type ?: evalType(ctx, node));
    if (!isArrayType(type))
        return NULL;

    return makeTypeReferenceNode(ctx->pool, type->array.elementType, loc);
}

static AstNode *getCallableType(EvalContext *ctx,
                                const FileLoc *loc,
                                AstNode *node,
                                attr(unused) AstNode *args)
{
    const Type *type = resolveType(node->type ?: evalType(ctx, node));
    if (typeIs(type, Func))
        return makeTypeReferenceNode(ctx->pool, type, loc);

    if (typeIs(type, Tuple) && hasFlag(type, FuncTypeParam)) {
        const Type *func = getTypeDecl(type->tuple.members[1])->type;
        func = makeFuncType(
            ctx->types,
            &(Type){.tag = typFunc,
                    .func = {.paramsCount = func->func.paramsCount - 1,
                             .params = &func->func.params[1],
                             .retType = func->func.retType}});
        return makeTypeReferenceNode(ctx->pool, func, loc);
    }
    return NULL;
}

static AstNode *getReturnType(EvalContext *ctx,
                              const FileLoc *loc,
                              AstNode *node,
                              attr(unused) AstNode *args)
{
    const Type *type = resolveType(node->type ?: evalType(ctx, node));
    if (typeIs(type, Func))
        return makeTypeReferenceNode(ctx->pool, type->func.retType, loc);

    if (typeIs(type, Tuple) && hasFlag(type, FuncTypeParam))
        return makeTypeReferenceNode(
            ctx->pool,
            getTypeDecl(type->tuple.members[1])->type->func.retType,
            loc);

    return NULL;
}

static AstNode *getBaseType(EvalContext *ctx,
                            const FileLoc *loc,
                            AstNode *node,
                            attr(unused) AstNode *args)
{
    const Type *type =
        resolveUnThisUnwrapType(node->type ?: evalType(ctx, node));
    type = getTypeBase(type);
    if (type)
        return makeTypeReferenceNode(ctx->pool, type, loc);

    return NULL;
}

static AstNode *getParams(EvalContext *ctx,
                          attr(unused) const FileLoc *loc,
                          AstNode *node,
                          attr(unused) AstNode *args)
{

    AstNode *params;
    AstNode *decl = node;
getParam_from_decl:
    if (decl == NULL)
        return NULL;
    if (nodeIs(decl, Attr))
        params = decl->attr.args;
    else if (nodeIs(decl, FuncDecl))
        params = decl->funcDecl.signature->params;
    else if (nodeIs(decl, FuncType))
        params = decl->funcType.params;
    else if (nodeIs(decl, ClosureExpr))
        params = decl->closureExpr.params;
    else {
        const Type *type = resolveType(node->type ?: evalType(ctx, node));
        if (typeIs(type, Tuple) && hasFlag(type, FuncTypeParam))
            type = type->tuple.members[1];
        if (!typeIs(type, Func))
            return NULL;
        decl = getTypeDecl(type);
        goto getParam_from_decl;
    }

    return comptimeWrapped(ctx, &node->loc, params, flgComptimeIterable);
}

static AstNode *getStripedType(EvalContext *ctx,
                               const FileLoc *loc,
                               AstNode *node,
                               attr(unused) AstNode *args)
{
    const Type *type = stripAll(node->type ?: evalType(ctx, node));
    return makeTypeReferenceNode(ctx->pool, type, loc);
}

static AstNode *getTargetType(EvalContext *ctx,
                              const FileLoc *loc,
                              AstNode *node,
                              attr(unused) AstNode *args)
{
    const Type *type =
        resolveUnThisUnwrapType(node->type ?: evalType(ctx, node));
    switch (type->tag) {
    case typStruct:
        if (typeIs(type, Struct) && hasFlag(type, Optional)) {
            type = getOptionalTargetType(type);
            break;
        }
        return NULL;
    case typUnion:
        if (isResultType(type)) {
            type = getResultTargetType(type);
            break;
        }
        return NULL;
    default:
        return NULL;
    }

    return makeTypeReferenceNode(ctx->pool, type, loc);
}

static AstNode *makePointedTypeAstNode(EvalContext *ctx,
                                       const FileLoc *loc,
                                       AstNode *node,
                                       attr(unused) AstNode *args)
{
    const Type *type = actualType(node->type ?: evalType(ctx, node));
    if (!isPointerType(type))
        return NULL;

    return makeTypeReferenceNode(ctx->pool, type->pointer.pointed, loc);
}

static AstNode *isString(EvalContext *ctx,
                         const FileLoc *loc,
                         AstNode *node,
                         attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(unwrapType(type, NULL));

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, String) ||
                                                       isBuiltinString(type)});
}

static AstNode *isChar(EvalContext *ctx,
                       const FileLoc *loc,
                       AstNode *node,
                       attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isCharacterType(type)});
}

static AstNode *isArray(EvalContext *ctx,
                        const FileLoc *loc,
                        AstNode *node,
                        attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isArrayType(type)});
}

static AstNode *isSlice(EvalContext *ctx,
                        const FileLoc *loc,
                        AstNode *node,
                        attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isSliceType(type)});
}

static AstNode *isBoolean(EvalContext *ctx,
                          const FileLoc *loc,
                          AstNode *node,
                          attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isBooleanType(type)});
}

static AstNode *isNumeric(EvalContext *ctx,
                          const FileLoc *loc,
                          AstNode *node,
                          attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isNumericType(type)});
}

static AstNode *isInteger(EvalContext *ctx,
                          const FileLoc *loc,
                          AstNode *node,
                          attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isIntegerType(type)});
}

static AstNode *isSigned(EvalContext *ctx,
                         const FileLoc *loc,
                         AstNode *node,
                         attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isSignedType(type)});
}

static AstNode *isUnsigned(EvalContext *ctx,
                           const FileLoc *loc,
                           AstNode *node,
                           attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isUnsignedType(type)});
}

static AstNode *isFloat(EvalContext *ctx,
                        const FileLoc *loc,
                        AstNode *node,
                        attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isFloatType(type)});
}

static AstNode *isOptional(EvalContext *ctx,
                           const FileLoc *loc,
                           AstNode *node,
                           attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveAndUnThisType(unwrapType(type, NULL));

    bool isOptional = typeIs(type, Struct) && hasFlag(type, Optional);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isOptional});
}

static AstNode *isPointer(EvalContext *ctx,
                          const FileLoc *loc,
                          AstNode *node,
                          attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Pointer)});
}

static AstNode *isReference(EvalContext *ctx,
                            const FileLoc *loc,
                            AstNode *node,
                            attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = typeIs(type, Reference)});
}

static AstNode *isUnresolved(EvalContext *ctx,
                             const FileLoc *loc,
                             AstNode *node,
                             attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = stripPointerOrReference(resolveType(type));

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = unThisType(type) == NULL});
}

static AstNode *isStruct(EvalContext *ctx,
                         const FileLoc *loc,
                         AstNode *node,
                         attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(resolveAndUnThisType(type), NULL);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Struct)});
}

static AstNode *isClass(EvalContext *ctx,
                        const FileLoc *loc,
                        AstNode *node,
                        attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveAndUnThisType(unwrapType(type, NULL));

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Class)});
}

static AstNode *evalIsTupleType(EvalContext *ctx,
                                const FileLoc *loc,
                                AstNode *node,
                                attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(resolveType(type), NULL);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Tuple)});
}

static AstNode *evalIsUnionType(EvalContext *ctx,
                                const FileLoc *loc,
                                AstNode *node,
                                attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(resolveType(type), NULL);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Union)});
}

static AstNode *evalIsPrimitive(EvalContext *ctx,
                                const FileLoc *loc,
                                AstNode *node,
                                attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(resolveType(type), NULL);
    bool isPrimitive = typeIs(type, Primitive);
    if (!isPrimitive) {
        isPrimitive = typeIs(type, String) || isBuiltinString(type);
    }

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isPrimitive});
}

static AstNode *isEnum(EvalContext *ctx,
                       const FileLoc *loc,
                       AstNode *node,
                       attr(unused) AstNode *args)
{
    node->type ?: evalType(ctx, node);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = typeIs(node->type, Enum)});
}

static AstNode *isVoidComptime(EvalContext *ctx,
                               const FileLoc *loc,
                               AstNode *node,
                               attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveUnThisUnwrapType(node->type);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = isVoidType(type)});
}

static AstNode *hasVoidReturnType(EvalContext *ctx,
                                  const FileLoc *loc,
                                  AstNode *node,
                                  attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    if (!typeIs(type, Func))
        return NULL;
    type = resolveUnThisUnwrapType(node->type->func.retType);
    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isVoidType(type) ||
                                                       isVoidResultType(type)});
}

static AstNode *isDestructibleType(EvalContext *ctx,
                                   const FileLoc *loc,
                                   AstNode *node,
                                   attr(unused) AstNode *args)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveAndUnThisType(unwrapType(type, NULL));
    AstNode *decl = getTypeDecl(type);

    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = isClassType(type) ||
                                                       isDestructible(type)});
}

static AstNode *isCover(EvalContext *ctx,
                        const FileLoc *loc,
                        AstNode *node,
                        attr(unused) AstNode *args)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(type, NULL);

    AstNode *decl = getTypeDecl(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value =
                       isClassOrStructType(type) && hasFlag(decl, Extern)});
}

static AstNode *isFunction(EvalContext *ctx,
                           const FileLoc *loc,
                           AstNode *node,
                           attr(unused) AstNode *args)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveType(unwrapType(type, NULL));
    bool value = typeIs(type, Func) ||
                 (typeIs(type, Tuple) && hasFlag(type, FuncTypeParam));

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = value});
}

static AstNode *isClosure(EvalContext *ctx,
                          const FileLoc *loc,
                          AstNode *node,
                          attr(unused) AstNode *args)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveUnThisUnwrapType(type);
    bool value = typeIs(type, Class) || hasFlag(type, Closure);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = value});
}

static AstNode *isFuncTypeParam(EvalContext *ctx,
                                const FileLoc *loc,
                                AstNode *node,
                                attr(unused) AstNode *args)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveUnThisUnwrapType(type);
    bool value = typeIs(type, Tuple) && hasFlag(type, FuncTypeParam);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = value});
}

static AstNode *isAnonymousStruct(EvalContext *ctx,
                                  const FileLoc *loc,
                                  AstNode *node,
                                  attr(unused) AstNode *args)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveUnThisUnwrapType(type);
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = hasFlag(type, Anonymous)});
}

static AstNode *isResultTypeComptime(EvalContext *ctx,
                                     const FileLoc *loc,
                                     AstNode *node,
                                     attr(unused) AstNode *args)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveUnThisUnwrapType(type);
    bool value = isResultType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit, .boolLiteral.value = value});
}

static AstNode *isLiteral(EvalContext *ctx,
                          const FileLoc *loc,
                          AstNode *node,
                          attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    return makeAstNode(ctx->pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .boolLiteral.value = typeIs(type, Literal)});
}

static AstNode *hasDeinit(EvalContext *ctx,
                          const FileLoc *loc,
                          AstNode *node,
                          attr(unused) AstNode *args)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveUnThisUnwrapType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value =
                       isClassOrStructType(type) &&
                       findMemberInType(type, S_DeinitOverload) != NULL});
}

static AstNode *hasBaseType(EvalContext *ctx,
                            const FileLoc *loc,
                            AstNode *node,
                            attr(unused) AstNode *args)
{

    const Type *type = node->type ?: evalType(ctx, node);
    type = resolveUnThisUnwrapType(type);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = getTypeBase(type) != NULL});
}

static AstNode *typeHasReferenceMembers(EvalContext *ctx,
                                        const FileLoc *loc,
                                        AstNode *node,
                                        attr(unused) AstNode *args)
{
    const Type *type = node->type ?: evalType(ctx, node);
    type = unwrapType(type, NULL);

    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = hasReferenceMembers(type)});
}

static AstNode *isField(EvalContext *ctx,
                        const FileLoc *loc,
                        AstNode *node,
                        attr(unused) AstNode *args)
{
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astBoolLit,
                   .boolLiteral.value = nodeIs(node, FieldDecl)});
}

AstNode *evalAstAnnotationMemberAccess(EvalContext *ctx,
                                       const FileLoc *loc,
                                       AstNode *node,
                                       cstring name)
{
    const Type *type = node->type;
    if (isClassOrStructType(type)) {
        AstNode *decl = getTypeDecl(type), *annot = decl->classDecl.annotations;
        for (; annot; annot = annot->next) {
            if (annot->_name == name)
                return deepCloneAstNode(ctx->pool, annot->annotation.value);
        }
    }
    return makeNullLiteral(ctx->pool, loc, NULL, makeNullType(ctx->types));
}

AstNode *evalPluginMemberAccess(EvalContext *ctx,
                                const FileLoc *loc,
                                AstNode *node,
                                cstring name)
{
    return makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astCxyPluginAction,
                   .CxyPluginAction = {.name = name,
                                       .plugin = node->pluginDecl.plugin}});
}

static void initDefaultMembers(EvalContext *ctx)
{
    defaultMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&defaultMembers, makeString(ctx->strings, name), G)

    ADD_MEMBER("name", getName);
    ADD_MEMBER("value", getValue);
    ADD_MEMBER("members", getMembers);
    ADD_MEMBER("attributes", getAttributes);
    ADD_MEMBER("annotations", getAnnotations);
    ADD_MEMBER("Tinfo", getTypeInfo);
    ADD_MEMBER("elementType", getElementType);
    ADD_MEMBER("pointedType", makePointedTypeAstNode);
    ADD_MEMBER("strippedType", getStripedType);
    ADD_MEMBER("targetType", getTargetType);
    ADD_MEMBER("callable", getCallableType);
    ADD_MEMBER("returnType", getReturnType);
    ADD_MEMBER("baseType", getBaseType);
    ADD_MEMBER("params", getParams);
    ADD_MEMBER("membersCount", getMembersCount);
    ADD_MEMBER("isInteger", isInteger);
    ADD_MEMBER("isSigned", isSigned);
    ADD_MEMBER("isUnsigned", isUnsigned);
    ADD_MEMBER("isFloat", isFloat);
    ADD_MEMBER("isOptional", isOptional);
    ADD_MEMBER("isNumber", isNumeric);
    ADD_MEMBER("isPointer", isPointer);
    ADD_MEMBER("isReference", isReference);
    ADD_MEMBER("isUnresolved", isUnresolved);
    ADD_MEMBER("isStruct", isStruct);
    ADD_MEMBER("isClass", isClass);
    ADD_MEMBER("isTuple", evalIsTupleType);
    ADD_MEMBER("isUnion", evalIsUnionType);
    ADD_MEMBER("isPrimitive", evalIsPrimitive);
    ADD_MEMBER("isField", isField);
    ADD_MEMBER("isString", isString);
    ADD_MEMBER("isBoolean", isBoolean);
    ADD_MEMBER("isChar", isChar);
    ADD_MEMBER("isArray", isArray);
    ADD_MEMBER("isSlice", isSlice);
    ADD_MEMBER("isEnum", isEnum);
    ADD_MEMBER("isVoid", isVoidComptime);
    ADD_MEMBER("isDestructible", isDestructibleType);
    ADD_MEMBER("isCover", isCover);
    ADD_MEMBER("isFunction", isFunction);
    ADD_MEMBER("isClosure", isClosure);
    ADD_MEMBER("isFuncTypeParam", isFuncTypeParam);
    ADD_MEMBER("isAnonymousStruct", isAnonymousStruct);
    ADD_MEMBER("isResultType", isResultTypeComptime);
    ADD_MEMBER("isLiteral", isLiteral);
    ADD_MEMBER("hasBase", hasBaseType);
    ADD_MEMBER("hasDeinit", hasDeinit);
    ADD_MEMBER("hasVoidReturnType", hasVoidReturnType);
    ADD_MEMBER("hasReferenceMembers", typeHasReferenceMembers);

#undef ADD_MEMBER
}

static AstNode *getStructOrClassMembers(EvalContext *ctx,
                                        const FileLoc *loc,
                                        AstNode *node,
                                        attr(unused) AstNode *args)
{
    return comptimeWrapped(
        ctx, loc, node->structDecl.members, flgComptimeIterable);
}

static AstNode *getClassBase(EvalContext *ctx,
                             const FileLoc *loc,
                             AstNode *node,
                             attr(unused) AstNode *args)
{
    if (node->classDecl.base) {
        return getResolvedPath(node->classDecl.base);
    }

    logError(ctx->L,
             loc,
             "class '{s}' does not extend any base type",
             (FormatArg[]){{.s = node->classDecl.name}});
    logNote(ctx->L, &node->loc, "class declared here", NULL);

    return NULL;
}

static void initStructDeclMembers(EvalContext *ctx)
{
    structDeclMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&structDeclMembers, makeString(ctx->strings, name), G)

    ADD_MEMBER("members", getStructOrClassMembers);
    ADD_MEMBER("base", getClassBase);

#undef ADD_MEMBER
}

static AstNode *getStructFieldType(EvalContext *ctx,
                                   const FileLoc *loc,
                                   AstNode *node,
                                   attr(unused) AstNode *args)
{
    return makeTypeReferenceNode(ctx->pool, node->type, loc);
}

static void initStructFieldMembers(EvalContext *ctx)
{
    fieldDeclMembers = newHashTable(sizeof(AstNodeGetMember));
#define ADD_MEMBER(name, G)                                                    \
    insertAstNodeGetter(&fieldDeclMembers, makeString(ctx->strings, name), G)

    ADD_MEMBER("Tinfo", getStructFieldType);

#undef ADD_MEMBER
}

void initComptime(EvalContext *ctx)
{
    static bool initialized = false;
    if (!initialized) {
        initDefaultMembers(ctx);
        initStructDeclMembers(ctx);
        initStructFieldMembers(ctx);
        initialized = true;
    }
}

AstNode *evalAstNodeMemberAccess(EvalContext *ctx,
                                 const FileLoc *loc,
                                 AstNode *node,
                                 cstring name,
                                 bool annotation)
{
    if (annotation)
        return evalAstAnnotationMemberAccess(ctx, loc, node, name);
    if (nodeIs(node, PluginDecl))
        return evalPluginMemberAccess(ctx, loc, node, name);
    HashTable *table = NULL;
    switch (node->tag) {
    case astStructDecl:
        table = &structDeclMembers;
        break;
    case astFieldDecl:
        table = &fieldDeclMembers;
        break;
    default:
        break;
    }

    AstNodeMemberGetter getter =
        findAstNodeGetter(table ?: &defaultMembers, name)
            ?: (table != NULL ? findAstNodeGetter(&defaultMembers, name)
                              : NULL);
    if (getter == NULL)
        return NULL;

    return getter(ctx, loc, node, NULL);
}
