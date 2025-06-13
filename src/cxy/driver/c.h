//
// Created by Carter Mbotho on 2024-04-26.
//

#pragma once

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

AstNode *importCHeader(CompilerDriver *driver, const AstNode *node);

#ifdef __cplusplus
}
#endif