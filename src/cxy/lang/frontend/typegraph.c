//
// Created by Carter Mbotho on 2026-03-19.
//

#include "typegraph.h"

#include "ast.h"
#include "ttable.h"

#define NODE_SIZE (sizeof(DynArray))

typedef struct GraphNodeContext {
    u32 visited; // Each bit represents a pass
} GraphNodeContext;

typedef struct GraphNode {
    const Type *type;
    GraphNodeContext context;
    HashTable edges;
} GraphNode;

static GraphNode *newGraphNode(MemPool *pool, const Type *type)
{
    GraphNode *node = allocFromMemPool(pool, sizeof(GraphNode));
    *node =
        (GraphNode){.type = type, .edges = newTempHashTable(sizeof(Type *))};
    return node;
}

static bool compareGraphNodes(const void *a, const void *b)
{
    return (*(GraphNode **)a)->type == (*(GraphNode **)b)->type;
}

static inline HashCode hashGraphNode(const GraphNode *node)
{
    return hashPtr(hashInit(), node->type);
}

static GraphNode *findNode(TypeGraph *g, const Type *type)
{
    GraphNode *node = &(GraphNode){.type = type};
    GraphNode **found = findInHashTable(&g->nodes,
                                        &node,
                                        hashGraphNode(node),
                                        sizeof(GraphNode *),
                                        compareGraphNodes);
    if (found != NULL) {
        return *found;
    }
    return NULL;
}

static bool addNode(TypeGraph *g, const Type *type)
{
    GraphNode *node = newGraphNode(&g->pool, type);
    const HashCode hash = hashGraphNode(node);
    return insertInHashTable(
        &g->nodes, &node, hash, sizeof(GraphNode *), compareGraphNodes);
}

typedef CxyPair(GraphNode *, bool) GetOrCreateNodeResult;
static GetOrCreateNodeResult getOrCreateNode(TypeGraph *g, const Type *type)
{
    GraphNode *node = &(GraphNode){.type = type};
    const HashCode hash = hashGraphNode(node);
    GraphNode **found = findInHashTable(
        &g->nodes, &node, hash, sizeof(GraphNode *), compareGraphNodes);
    if (found != NULL) {
        return (GetOrCreateNodeResult){*found, true};
    }

    node = newGraphNode(&g->pool, type);
    insertInHashTable(
        &g->nodes, &node, hash, sizeof(GraphNode *), compareGraphNodes);
    return (GetOrCreateNodeResult){findNode(g, type), false};
}

static bool findEdge(GraphNode *node, const Type *type)
{
    return findInHashTable(&node->edges,
                           &type,
                           hashPtr(hashInit(), type),
                           sizeof(Type *),
                           comparePointers);
}

static void addNodeEdge(GraphNode *node, const Type *type)
{
    if (type == NULL || node->type == type)
        return;
    switch (type->tag) {
    case typArray:
    case typTuple:
    case typUnion:
    case typFunc:
    case typClass:
    case typStruct:
    case typUntaggedUnion:
    case typModule:
    case typWrapped:
    case typAlias:
    case typPointer:
    case typReference:
    case typOptional:
    case typResult:
    case typException:
    case typEnum:
        if (!findEdge(node, type)) {
            bool status = insertInHashTable(&node->edges,
                                            &type,
                                            hashPtr(hashInit(), type),
                                            sizeof(Type *),
                                            comparePointers);
            csAssert0(status);
        }
        break;
    default:
        break;
    }
}

static void addSyntheticFunctionTypes(TypeGraph *g, const Type *funcType)
{
    if (!typeIs(funcType, Func))
        return;

    // Add return type if it's synthetic (not primitive, no decl)
    const Type *retType = stripAll(funcType->func.retType);
    if (!isPrimitiveType(retType) && getTypeDecl(retType) == NULL) {
        addTypeGraphNode(g, retType);
    }

    // Add parameter types if synthetic
    for (int i = 0; i < funcType->func.paramsCount; i++) {
        const Type *paramType = stripAll(funcType->func.params[i]);
        if (!isPrimitiveType(paramType) && getTypeDecl(paramType) == NULL) {
            addTypeGraphNode(g, paramType);
        }
    }
}

void addTypeGraphNode(TypeGraph *g, const Type *type)
{
    GetOrCreateNodeResult goi = getOrCreateNode(g, type);
    if (goi.s) {
        // node already added
        return;
    }

    GraphNode *node = goi.f;
    switch (type->tag) {
    case typArray:
        addNodeEdge(node, type->array.elementType);
        addTypeGraphNode(g, type->array.elementType);
        break;
    case typTuple:
        for (int i = 0; i < type->tuple.count; i++) {
            addNodeEdge(node, type->tuple.members[i]);
            addTypeGraphNode(g, type->tuple.members[i]);
        }
        // if (type->tuple.copyFunc) {
        //     addNodeEdge(node, type->tuple.copyFunc);
        //     addTypeGraphNode(g, type->tuple.copyFunc);
        // }
        // if (type->tuple.destructorFunc) {
        //     addNodeEdge(node, type->tuple.destructorFunc);
        //     addTypeGraphNode(g, type->tuple.destructorFunc);
        // }
        break;
    case typUnion:
        for (int i = 0; i < type->tUnion.count; i++) {
            addNodeEdge(node, type->tUnion.members[i].type);
            addTypeGraphNode(g, type->tUnion.members[i].type);
        }
        // if (type->tUnion.copyFunc) {
        //     addNodeEdge(node, type->tUnion.copyFunc);
        //     addTypeGraphNode(g, type->tUnion.copyFunc);
        // }
        // if (type->tUnion.destructorFunc) {
        //     addNodeEdge(node, type->tUnion.destructorFunc);
        //     addTypeGraphNode(g, type->tUnion.destructorFunc);
        // }
        break;
    case typStruct:
        for (int i = 0; i < type->tStruct.members->count; i++) {
            NamedTypeMember *ntm = &type->tStruct.members->members[i];
            if (nodeIs(ntm->decl, FuncDecl)) {
                // Don't add function type itself as dependency,
                // but do add synthetic parameter/return types (tuples, etc.)
                addSyntheticFunctionTypes(g, ntm->type);
                continue;
            }
            addNodeEdge(node, ntm->type);
            addTypeGraphNode(g, ntm->type);
        }
        break;
    case typFunc:
        addNodeEdge(node, type->func.retType);
        addTypeGraphNode(g, type->func.retType);
        for (int i = 0; i < type->func.paramsCount; i++) {
            addNodeEdge(node, type->func.params[i]);
            addTypeGraphNode(g, type->func.params[i]);
        }
        break;
    case typClass:
        if (type->tClass.inheritance) {
            if (type->tClass.inheritance->base) {
                addNodeEdge(node, type->tClass.inheritance->base);
                addTypeGraphNode(g, type->tClass.inheritance->base);
            }
            for (int i = 0; i < type->tClass.inheritance->interfacesCount;
                 i++) {
                addNodeEdge(node, type->tClass.inheritance->interfaces[i]);
                addTypeGraphNode(g, type->tClass.inheritance->interfaces[i]);
            }
        }
        for (int i = 0; i < type->tClass.members->count; i++) {
            NamedTypeMember *ntm = &type->tClass.members->members[i];
            if (nodeIs(ntm->decl, FuncDecl)) {
                // Don't add function type itself as dependency,
                // but do add synthetic parameter/return types (tuples, etc.)
                addSyntheticFunctionTypes(g, ntm->type);
                continue;
            }
            addNodeEdge(node, ntm->type);
            addTypeGraphNode(g, ntm->type);
        }
        break;
    case typUntaggedUnion:
        for (int i = 0; i < type->untaggedUnion.members->count; i++) {
            addNodeEdge(node, type->untaggedUnion.members->members[i].type);
            addTypeGraphNode(g, type->untaggedUnion.members->members[i].type);
        }
        break;
    case typModule:
        for (int i = 0; i < type->module.members->count; i++) {
            NamedTypeMember *ntm = &type->module.members->members[i];
            if (ntm->type == NULL)
                continue;
            if (nodeIs(ntm->decl, FuncDecl)) {
                // Don't add function type itself as dependency,
                // but do add synthetic parameter/return types (tuples, etc.)
                addSyntheticFunctionTypes(g, ntm->type);
                continue;
            }
            addNodeEdge(node, ntm->type);
            addTypeGraphNode(g, ntm->type);
        }
        break;
    case typWrapped:
        addNodeEdge(node, type->wrapped.target);
        addTypeGraphNode(g, type->wrapped.target);
        break;
    case typAlias:
        addNodeEdge(node, type->alias.aliased);
        addTypeGraphNode(g, type->alias.aliased);
        break;
    case typPointer:
        // addNodeEdge(node, type->pointer.pointed);
        // addTypeGraphNode(g, type->pointer.pointed);
        break;
    case typReference:
        // addNodeEdge(node, type->reference.referred);
        // addTypeGraphNode(g, type->reference.referred);
        break;
    case typOptional:
        addNodeEdge(node, type->optional.target);
        addTypeGraphNode(g, type->optional.target);
        break;
    case typResult:
        addNodeEdge(node, type->result.target);
        addTypeGraphNode(g, type->result.target);
        break;
    case typException:
        addNodeEdge(node, type->exception.decl->type);
        addTypeGraphNode(g, type->exception.decl->type);
        break;
    default:
        break;
    }
}

void addTypeGraphNodeToModule(TypeGraph *g, const Type *type)
{
    if (g->module == NULL || type == NULL || typeIs(type, Module))
        return;
    GraphNode *node = findNode(g, g->module), *typeNode = findNode(g, type);
    if (node == NULL || typeNode != NULL)
        return;
    addNodeEdge(node, type);
    addTypeGraphNode(g, type);
}

static void visitGraphNode(TypeGraph *g,
                           const Type *type,
                           TypeGraphVisitor *visitor,
                           int pass)
{
    GraphNode *node = findNode(g, type);
    if (node == NULL || (node->context.visited & BIT(pass)))
        return;

    if (visitor->previsit) {
        visitor->previsit(type, visitor);
    }
    node->context.visited |= BIT(pass);
    HashtableIt it = newHashTableIt(&node->edges, sizeof(Type *));
    while (hashTableItHasNext(&it)) {
        visitor->depth++;
        const Type **neighbour = hashTableItNext(&it);
        visitGraphNode(g, *neighbour, visitor, pass);
        visitor->depth--;
    }
    if (visitor->visit)
        visitor->visit(type, visitor);
}

TypeGraph newTypeGraph(TypeTable *types, const Type *module)
{
    TypeGraph g = {
        .module = module,
        .pool = newMemPool(),
    };
    // We want to keep everything contained in this temporary memory pool
    g.nodes = newTempHashTable(sizeof(GraphNode *)),
    addTypeGraphNode(&g, module);
    return g;
}

void visitTypeGraph(TypeGraph *g, TypeGraphVisitor* visitor, int pass)
{
    if (visitor->visitAll) {
        // Visit all nodes in hash table (order doesn't matter)
        HashtableIt it = newHashTableIt(&g->nodes, sizeof(GraphNode *));
        while (hashTableItHasNext(&it)) {
            GraphNode **node = hashTableItNext(&it);
            visitGraphNode(g, (*node)->type, visitor, pass);
        }
    } else {
        // DFS from module first (for dependency-ordered traversal)
        visitGraphNode(g, g->module, visitor, pass);
        
        // Then visit any remaining unvisited nodes (isolated synthetic types)
        HashtableIt it = newHashTableIt(&g->nodes, sizeof(GraphNode *));
        while (hashTableItHasNext(&it)) {
            GraphNode **node = hashTableItNext(&it);
            visitGraphNode(g, (*node)->type, visitor, pass);
        }
    }
}

void freeTypeGraph(TypeGraph *g)
{
    HashtableIt it = newHashTableIt(&g->nodes, sizeof(GraphNode *));
    while (hashTableItHasNext(&it)) {
        GraphNode **node = hashTableItNext(&it);
        freeHashTable(&(*node)->edges);
    }
    freeHashTable(&g->nodes);
    freeMemPool(&g->pool);
}
