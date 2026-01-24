/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#pragma once

#include "lang/frontend/ast.h"
#include "core/format.h"

#ifdef __cplusplus
extern "C" {
#endif

struct CompilerDriver;

/**
 * Dumps the given AST node and its children to the specified FormatState in S-expression format
 *
 * @param driver The compiler driver containing configuration options
 * @param node The AST node to dump
 * @param state The FormatState to write to
 * @return The same AST node that was passed in
 */
AstNode *dumpAstToSexpState(struct CompilerDriver *driver, AstNode *node, FormatState *state);

/**
 * Dumps the given AST node and its children to the specified file in S-expression format
 *
 * @param driver The compiler driver containing configuration options
 * @param node The AST node to dump
 * @param file The output file stream to write to
 * @return The same AST node that was passed in
 */
AstNode *dumpAstToSexp(struct CompilerDriver *driver, AstNode *node, FILE *file);

#ifdef __cplusplus
}
#endif