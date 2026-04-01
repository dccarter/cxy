//
// Created by Carter on 2026-03-19.
//

#pragma once

#include "core/htable.h"
#include "core/mempool.h"
#include "lang/frontend/types.h"

typedef struct TypeGraph {
    MemPool pool;
    const Type *module;
    HashTable nodes;
} TypeGraph;

typedef struct PrintGraphCtx {
    int tab;
} PrintGraphCtx;

typedef struct TypeGraphVisitor {
    int depth;
    void (*previsit)(const Type *, struct TypeGraphVisitor *);
    void (*visit)(const Type *, struct TypeGraphVisitor *);
    void *ctx;
} TypeGraphVisitor;

TypeGraph newTypeGraph(TypeTable *types, const Type *module);
void addTypeGraphNode(TypeGraph *g, const Type *type);
void addTypeGraphNodeToModule(TypeGraph *g, const Type *type);
void visitTypeGraph(TypeGraph *g, TypeGraphVisitor *visitor);
void freeTypeGraph(TypeGraph *g);
