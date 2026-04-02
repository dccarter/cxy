//
// Created by Carter on 2023-07-06.
//
#pragma once

#include <driver/stages.h>

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CompilerDriver;

typedef struct {
    MemPoolStats poolStats;
} StatsSnapshot;

typedef struct CompilerStats {
    struct {
        bool captured;
        MemPoolStats pool;
    } stages[ccsCOUNT];
    StatsSnapshot snapshot;
    struct timespec start;
    u64 duration;
} CompilerStats;

void startCompilerStats(struct CompilerDriver *driver);
void stopCompilerStats(struct CompilerDriver *driver);
void compilerStatsSnapshot(struct CompilerDriver *driver);
void compilerStatsRecord(struct CompilerDriver *driver, CompilerStage stage);
void compilerStatsPrint(const struct CompilerDriver *driver);



#ifdef __cplusplus
}
#endif
