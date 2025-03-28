//
// Created by Carter on 2023-06-29.
//

#pragma once

#include "token.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

#define AST_ARITH_EXPR_LIST(f)                     \
    f(Add, 3, Plus, "+", "add")                    \
    f(Sub, 3, Minus,"-", "sub")                   \
    f(Mul, 2, Mult, "*", "mul")                    \
    f(Div, 2, Div,  "/", "div")                   \
    f(Mod, 2, Mod,  "%", "rem")

#define AST_BIT_EXPR_LIST(f)                        \
    f(BAnd, 7, BAnd, "&", "and")                    \
    f(BOr,  9, BOr,  "|", "or")                     \
    f(BXor, 8, BXor, "^", "xor")

#define AST_SHIFT_EXPR_LIST(f)         \
    f(Shl, 4, Shl, "<<", "lshift")     \
    f(Shr, 4, Shr, ">>", "rshift")

#define AST_CMP_EXPR_LIST(f)                   \
    f(Eq,  6, Equal,        "==", "eq")        \
    f(Ne,  6, NotEqual,     "!=", "neq")       \
    f(Is,  6, Is,           "is", "is")        \
    f(Gt,  5, Greater,      ">", "gt")         \
    f(Lt,  5, Less,         "<", "lt")         \
    f(Geq, 5, GreaterEqual, ">=", "geq")       \
    f(Leq, 5, LessEqual,    "<=", "leq")

#define AST_LOGIC_EXPR_LIST(f)                \
    f(LAnd, 11, LAnd, "&&", "land")             \
    f(LOr,  12, LOr,  "||", "lor")

#define AST_BINARY_EXPR_LIST(f)           \
    f(Range, 13,DotDot, "..", "range")    \
    f(Catch, 10,Catch,  "catch", "catch") \
    AST_ARITH_EXPR_LIST(f)                \
    AST_BIT_EXPR_LIST(f)                  \
    AST_SHIFT_EXPR_LIST(f)                \
    AST_CMP_EXPR_LIST(f)                  \
    AST_LOGIC_EXPR_LIST(f)

#define AST_ASSIGN_EXPR_LIST(f)          \
    f(Assign, 0, Assign, "",  "assign")  \
    AST_ARITH_EXPR_LIST(f)               \
    AST_BIT_EXPR_LIST(f)                 \
    AST_SHIFT_EXPR_LIST(f)

#define AST_PREFIX_EXPR_LIST(f)                     \
    f(PreDec, MinusMinus, "--", "pre-dec")          \
    f(PreInc, PlusPlus, "++", "pre-inc")            \
    f(Minus,  Minus, "-", "pre-minus")              \
    f(Plus,   Plus, "+",  "pre-plus")               \
    f(Deref,  Mult, "*",  "deref")                  \
    f(Not,    LNot, "!",  "lnot")                   \
    f(Compl,  BNot, "~",  "bnot")                   \
    f(Refof,  BAnd, "&",  "refof")                  \
    f(Move,   LAnd, "&&", "move")                   \
    f(Spread, Elipsis, "...", "spread")             \
    f(Await,  Await, "await", "await")              \
    f(Delete, Delete, "delete", "delete")           \
    f(Ptrof,  Ptrof,  "ptrof",  "ptrof")            \

#define AST_POSTFIX_EXPR_LIST(f)                \
    f(PostDec, MinusMinus, "--", "dec")         \
    f(PostInc, PlusPlus,  "++", "inc")

#define AST_UNARY_EXPR_LIST(f)                                                 \
    AST_PREFIX_EXPR_LIST(f)                                                    \
    AST_POSTFIX_EXPR_LIST(f)

#define AST_OVERLOAD_ONLY_OPS(f)                               \
    f(CallOverload,             "call", "()")                  \
    f(IndexOverload,            "idx", "[]")                   \
    f(IndexAssignOverload,      "idx_assign", "=[]")           \
    f(StringOverload,           "str", "str")                  \
    f(InitOverload,             "init", "init")                \
    f(DeinitOverload,           "deinit", "deinit")            \
    f(CopyOverload,             "copy",  "copy")               \
    f(ConstructorOverload,      "constructor", "constructor")  \
    f(DestructorOverload,       "destructor", "destructor")    \
    f(HashOverload,             "hash",       "hash")          \
    f(DestructorFwd,            "destructor_fwd",  "destructor__fwd") \
    f(InitDefaults,             "init_defaults", "init_defaults")\
    f(Redirect,                 "redirect", "&.")

typedef enum {
    opInvalid,
#define f(name, ...) op## name,
    AST_BINARY_EXPR_LIST(f)
    AST_UNARY_EXPR_LIST(f)
#undef f
#define f(name, ...) op## name## Equal,
    opAssign,
    AST_ASSIGN_EXPR_LIST(f)
#undef f

#define f(name, ...) op## name,
    AST_OVERLOAD_ONLY_OPS(f)
#undef f
    opTruthy
} Operator;

// clang-format on

const char *getUnaryOpString(Operator op);
const char *getBinaryOpString(Operator op);
const char *getAssignOpString(Operator op);
const char *getOpOverloadName(Operator op);
const char *getOperatorString(Operator op);
Operator tokenToUnaryOperator(TokenTag tag);
Operator tokenToPostfixUnaryOperator(TokenTag tag);

Operator tokenToBinaryOperator(TokenTag tag);

Operator tokenToAssignmentOperator(TokenTag tag);

bool isPrefixOpKeyword(Operator op);

int getMaxBinaryOpPrecedence(void);

int getBinaryOpPrecedence(Operator op);

#ifdef __cplusplus
}
#endif
