//
// Created by Carter Mbotho on 2024-01-09.
//

#include "ast.h"
#include "lang/middle/sema/check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"
#include "ttable.h"
#include "types.h"

static u64 addDefineToModuleMembers(NamedTypeMember *members,
                                    u64 index,
                                    AstNode *decl,
                                    u64 builtinFlags)
{
    if (decl->define.container) {
        decl->flags |= builtinFlags;
        members[index++] = (NamedTypeMember){
            .decl = decl, .name = getDeclarationName(decl), .type = decl->type};
    }
    else {
        AstNode *name = decl->define.names;
        for (; name; name = name->next) {
            name->flags |= builtinFlags;
            members[index++] = (NamedTypeMember){
                .decl = name,
                .name = name->ident.alias ?: name->ident.value,
                .type = name->type};
        }
    }
    return index;
}

static u64 addModuleTypeMember(NamedTypeMember *members,
                               u64 index,
                               AstNode *decl,
                               u64 builtinFlags)
{
    if (nodeIs(decl, Define)) {
        return addDefineToModuleMembers(members, index, decl, builtinFlags);
    }
    else if (!nodeIs(decl, CCode) && !nodeIs(decl, Noop)) {
        decl->flags |= builtinFlags;
        members[index++] = (NamedTypeMember){
            .decl = decl, .name = getDeclarationName(decl), .type = decl->type};
        return index;
    }

    return index;
}

static u64 addPackageTypeMember(NamedTypeMember *members,
                               u64 index,
                               AstNode *decl)
{
    AstNode *entities = decl->import.entities;
    if (entities != NULL) {
        for (AstNode *entity = entities; entity; entity = entity->next) {
            members[index++] = (NamedTypeMember){
                .decl = entity->importEntity.target,
                .name = entity->importEntity.name,
                .type = entity->importEntity.target->type};
        }
    }
    else {
        const Type *module = decl->type;
        for (u64 i = 0; i < module->module.members->count; i++) {
            if (!hasFlag(module->module.members->members[i].decl, Public))
                continue;

            members[index++] = module->module.members->members[i];
        }
    }
    return index;
}

void buildModuleType(TypeTable *types, AstNode *node, bool isBuiltinModule)
{
    u64 builtinsFlags = (isBuiltinModule ? flgBuiltin : flgNone);
    u64 count = countProgramDecls(node->program.decls) +
                countProgramDecls(node->program.top),
        i = 0;

    NamedTypeMember *members = mallocOrDie(sizeof(NamedTypeMember) * count);

    AstNode *decl = node->program.top;
    for (; decl; decl = decl->next) {
        if (nodeIs(decl, ImportDecl) || nodeIs(decl, PluginDecl))
            continue;
        i = addModuleTypeMember(members, i, decl, builtinsFlags);
    }

    decl = node->program.decls;
    for (; decl; decl = decl->next) {
        i = addModuleTypeMember(members, i, decl, builtinsFlags);
    }

    node->type = makeModuleType(
        types,
        isBuiltinModule ? S___builtins : (
            node->program.module != NULL? node->program.module->moduleDecl.name : makeString(types->strPool, "__main")),
        node->loc.fileName,
        members,
        i,
        builtinsFlags | node->flags);
    free(members);
}

void buildPackageType(TypeTable *types, AstNode *node)
{
    u64 count = countPackageExports(node->program.top),
        i = 0;

    NamedTypeMember *members = mallocOrDie(sizeof(NamedTypeMember) * count);

    AstNode *decl = node->program.top;
    for (; decl; decl = decl->next) {
        if (!nodeIs(decl, ImportDecl))
            continue;
        i = addPackageTypeMember(members, i, decl);
    }

    node->type = makeModuleType(
        types,
        node->program.module->moduleDecl.name,
        node->loc.fileName,
        members,
        i,
        node->flags);
    free(members);
}
