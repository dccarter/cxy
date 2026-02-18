//
// Created by Carter Mbotho on 2026-02-11.
//

#pragma once

#include "driver/driver.h"

typedef struct CBackend {
    cstring filename;
    FILE *output;
    bool testMode;
} CBackend;

typedef struct CodegenContext {
    CBackend *backend;
    StrPool *strings;
    FormatState types;
    FormatState state;
    cstring loopUpdate;
    bool loopUpdateUsed;
    bool hasTestCases;
    bool memTraceEnabled;
    bool inLoop;
    struct {
        bool enabled;
        FilePos pos;
    } debug;
} CodegenContext;

