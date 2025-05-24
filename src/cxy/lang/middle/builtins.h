//
// Created by Carter on 2023-09-01.
//

#pragma once

#include <lang/frontend/ast.h>

#ifdef __cplusplus
extern "C" {
#endif

bool isBuiltinsInitialized();
void initializeBuiltins(Log *L);
void setBuiltinsModule(const Type *module, const FileLoc *loc);
AstNode *findBuiltinDecl(cstring name);
const Type *findBuiltinType(cstring name);
bool isBuiltinString(const Type *type);
bool overrideBuiltin(cstring name, AstNode *node);

#ifdef __cplusplus
}
#endif
