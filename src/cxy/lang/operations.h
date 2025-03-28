//
// Created by Carter on 2023-07-06.
//

#pragma once

#include "driver/driver.h"
#include "lang/frontend/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cJSON cJSON;

AstNode *shakeAstNode(CompilerDriver *driver, AstNode *node);
AstNode *dumpAstJson(CompilerDriver *driver, AstNode *node, FILE *file);
AstNode *dumpAstToYaml(CompilerDriver *driver, AstNode *node, FILE *file);
AstNode *dumpCxySource(CompilerDriver *driver, AstNode *node, FILE *file);
AstNode *preprocessAst(CompilerDriver *driver, AstNode *node);
AstNode *bindAst(CompilerDriver *driver, AstNode *node);
AstNode *checkAst(CompilerDriver *driver, AstNode *node);
AstNode *memoryManageAst(CompilerDriver *driver, AstNode *node);
AstNode *finalizeAst(CompilerDriver *driver, AstNode *node);
AstNode *generateCode(CompilerDriver *driver, AstNode *node);
AstNode *collectAst(CompilerDriver *driver, AstNode *node);
AstNode *backendDumpIR(CompilerDriver *driver, AstNode *node);
AstNode *simplifyAst(CompilerDriver *driver, AstNode *node);
AstNode *lowerAstNode(CompilerDriver *driver, AstNode *node);
AstNode *astToMir(CompilerDriver *driver, AstNode *node);
AstNode *dumpMir(CompilerDriver *driver, AstNode *node);
#ifdef __cplusplus
}
#endif
