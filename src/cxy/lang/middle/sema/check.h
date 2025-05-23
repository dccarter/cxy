//
// Created by Carter Mbotho on 2023-07-26.
//

#pragma once

#include <lang/frontend/ast.h>
#include <lang/frontend/ttable.h>
#include <lang/frontend/visitor.h>

#include <core/strpool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ERROR_TYPE(CTX) makeErrorType((CTX)->types)

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    TypeTable *types;
    AstVisitor *evaluator;
    AstModifier root;
    AstModifier blockModifier;
    bool traceMemory;
    bool returnState;
    bool exceptionTrace;

    union {
        struct {
            AstNode *currentCall;
            AstNode *currentStruct;
            AstNode *currentClass;
            AstNode *currentFunction;
            cstring fun;
            cstring cls;
            cstring mod;
            cstring path;
            bool shallow;
            bool explicitCatch;
        } stack;

        struct {
            AstNode *currentCall;
            AstNode *currentStruct;
            AstNode *currentClass;
            AstNode *currentFunction;
            cstring fun;
            cstring cls;
            cstring mod;
            cstring path;

            bool shallow;
            bool explicitCatch;
        };
    };

    struct ExceptionCather {
        AstNode *result;
        AstNode *ex;
        AstNode *variable;
        AstNode *block;
        AstNode *expr;
    } catcher;
} TypingContext;

const FileLoc *manyNodesLoc_(FileLoc *dst, AstNode *nodes);
#define manyNodesLoc(nodes) manyNodesLoc_(&(FileLoc){}, (nodes))

const FileLoc *lastNodeLoc_(FileLoc *dst, AstNode *nodes);
#define lastNodeLoc(nodes) lastNodeLoc_(&(FileLoc){}, (nodes))

AstNode *makeDefaultValue(MemPool *pool, const Type *type, FileLoc *loc);

bool transformToTruthyOperator(AstVisitor *visitor, AstNode *node);
bool transformToAwaitOperator(AstVisitor *visitor, AstNode *node);
bool transformToDerefOperator(AstVisitor *visitor, AstNode *node);
bool transformOptionalType(AstVisitor *visitor,
                           AstNode *node,
                           const Type *type);

AstNode *transformToUnionValue(TypingContext *ctx,
                               AstNode *right,
                               const Type *lhs,
                               const Type *rhs);

bool transformOptionalSome(AstVisitor *visitor, AstNode *node, AstNode *value);
bool transformOptionalNone(AstVisitor *visitor,
                           AstNode *node,
                           const Type *type);

bool isExplicitConstructableFrom(Log *L, const Type *type, const Type *from);
bool evalExplicitConstruction(AstVisitor *visitor,
                              const Type *type,
                              AstNode *node);

bool checkTypeImplementsAllMembers(TypingContext *ctx, AstNode *node);

bool checkMemberFunctions(AstVisitor *visitor,
                          AstNode *node,
                          NamedTypeMember *members);

bool exceptionVerifyRaiseExpr(TypingContext *ctx,
                              const AstNode *ret,
                              AstNode *node);

AstNode *makeCastResultTo(TypingContext *ctx, AstNode *var, bool except);

void implementClassOrStructBuiltins(AstVisitor *visitor, AstNode *node);
AstNode *implementDefaultInitializer(AstVisitor *visitor,
                                     AstNode *node,
                                     bool isVirtual);
u64 removeClassOrStructBuiltins(AstNode *node, NamedTypeMember *members);

void checkBaseDecl(AstVisitor *visitor, AstNode *node);
void checkImplements(AstVisitor *visitor,
                     AstNode *node,
                     const Type **implements,
                     u64 count);
void checkCatchBinaryOperator(AstVisitor *visitor,
                              AstNode *node,
                              struct ExceptionCather *catcher);

void checkCallExceptionBubbleUp(AstVisitor *visitor, AstNode *node);

AstNode *makeDropReferenceCall(TypingContext *ctx,
                               AstNode *member,
                               const FileLoc *loc);

AstNode *makeAllocateCall(TypingContext *ctx, AstNode *node);

AstNode *makeCopyReferenceCall(TypingContext *ctx,
                               AstNode *member,
                               const FileLoc *loc);

AstNode *makeSliceConstructor(TypingContext *ctx,
                              const Type *slice,
                              AstNode *expr);

AstNode *transformArrayExprToSliceCall(TypingContext *ctx,
                                       const Type *slice,
                                       AstNode *expr);
void transformArrayExprToSlice(AstVisitor *visitor,
                               const Type *slice,
                               AstNode *expr);

AstNode *createAllocateClass(TypingContext *ctx,
                             const Type *type,
                             const FileLoc *loc);

void transformToMemberCallExpr(AstVisitor *visitor,
                               AstNode *node,
                               AstNode *target,
                               cstring member,
                               AstNode *args);

const Type *resolveGenericDecl(AstVisitor *visitor,
                               const AstNode *generic,
                               AstNode *node);

AstNode *inheritanceMakeVTableMember(TypingContext *ctx,
                                     AstNode *node,
                                     AstNode *init);
AstNode *inheritanceBuildVTableType(AstVisitor *visitor, AstNode *node);
AstNode *inheritanceBuildVTable(TypingContext *ctx, AstNode *node);

const Type *transformToConstructCallExpr(AstVisitor *visitor, AstNode *node);
AstNode *transformClosureArgument(AstVisitor *visitor, AstNode *node);
const Type *transformToInitCoverCallExpr(AstVisitor *visitor,
                                         AstNode *node,
                                         const Type *target);

void implementTupleTypeCopyAndDestructor(AstVisitor *visitor, AstNode *node);

const Type *matchOverloadedFunctionPerfectMatch(Log *L,
                                                const Type *callee,
                                                const Type **argTypes,
                                                u64 argsCount,
                                                const FileLoc *loc,
                                                u64 flags,
                                                bool perfectMatch);

static inline const Type *matchOverloadedFunction(Log *L,
                                                  const Type *callee,
                                                  const Type **argTypes,
                                                  u64 argsCount,
                                                  const FileLoc *loc,
                                                  u64 flags)
{
    return matchOverloadedFunctionPerfectMatch(
        L, callee, argTypes, argsCount, loc, flags, false);
}

AstNode *makeEnumGetName(TypingContext *ctx, AstNode *node);
const Type *checkMember(AstVisitor *visitor, const Type *parent, AstNode *node);
const Type *checkMaybeComptime(AstVisitor *visitor, AstNode *node);
const Type *checkTypeShallow(AstVisitor *visitor, AstNode *node, bool shallow);

static inline const Type *checkType(AstVisitor *visitor, AstNode *node)
{
    return checkTypeShallow(visitor, node, false);
}

const Type *checkFunctionSignature(AstVisitor *visitor, AstNode *node);
const Type *checkFunctionBody(AstVisitor *visitor, AstNode *node);

void checkVarDecl(AstVisitor *visitor, AstNode *node);
void checkPath(AstVisitor *visitor, AstNode *node);
void checkFunctionParam(AstVisitor *visitor, AstNode *node);
void checkStructDecl(AstVisitor *visitor, AstNode *node);
void checkClassDecl(AstVisitor *visitor, AstNode *node);
void checkInterfaceDecl(AstVisitor *visitor, AstNode *node);
void checkFunctionDecl(AstVisitor *visitor, AstNode *node);
void checkEnumDecl(AstVisitor *visitor, AstNode *node);
void checkTypeDecl(AstVisitor *visitor, AstNode *node);
void checkUnionDecl(AstVisitor *visitor, AstNode *node);
void checkGenericDecl(AstVisitor *visitor, AstNode *node);
void checkExceptionDecl(AstVisitor *visitor, AstNode *node);

void checkBinaryExpr(AstVisitor *visitor, AstNode *node);
void checkUnaryExpr(AstVisitor *visitor, AstNode *node);
void checkPointerOfExpr(AstVisitor *visitor, AstNode *node);
void checkReferenceOfExpr(AstVisitor *visitor, AstNode *node);
void checkAssignExpr(AstVisitor *visitor, AstNode *node);
void checkIndexExpr(AstVisitor *visitor, AstNode *node);
void checkStructExpr(AstVisitor *visitor, AstNode *node);
void checkNewExpr(AstVisitor *visitor, AstNode *node);
void checkClosureExpr(AstVisitor *visitor, AstNode *node);
void checkArrayExpr(AstVisitor *visitor, AstNode *node);
void checkCallExpr(AstVisitor *visitor, AstNode *node);
void checkTupleExpr(AstVisitor *visitor, AstNode *node);
void checkMemberExpr(AstVisitor *visitor, AstNode *node);
void checkCastExpr(AstVisitor *visitor, AstNode *node);
void checkTypedExpr(AstVisitor *visitor, AstNode *node);

void checkForStmt(AstVisitor *visitor, AstNode *node);
void checkRangeExpr(AstVisitor *visitor, AstNode *node);
void checkCaseStmt(AstVisitor *visitor, AstNode *node);
void checkSwitchStmt(AstVisitor *visitor, AstNode *node);
void checkMatchCaseStmt(AstVisitor *visitor, AstNode *node);
void checkMatchStmt(AstVisitor *visitor, AstNode *node);
void checkIfStmt(AstVisitor *visitor, AstNode *node);

void checkTupleType(AstVisitor *visitor, AstNode *node);
void checkArrayType(AstVisitor *visitor, AstNode *node);
void checkFunctionType(AstVisitor *visitor, AstNode *node);
void checkBuiltinType(AstVisitor *visitor, AstNode *node);
void checkOptionalType(AstVisitor *visitor, AstNode *node);
void checkPointerType(AstVisitor *visitor, AstNode *node);
void checkReferenceType(AstVisitor *visitor, AstNode *node);
void checkResultType(AstVisitor *visitor, AstNode *node);

#ifdef __cplusplus
}
#endif
