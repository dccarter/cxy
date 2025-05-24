//
// Created by Carter Mbotho on 2025-05-14.
//

#include "driver/plugin.h"

bool compareCxyPluginActions(const void *a, const void *b)
{
    return ((const CxyPluginAction *)a)->name ==
           ((const CxyPluginAction *)b)->name;
}

AstNode *cxyPluginArgsPop(AstNode **args)
{
    if (args && *args) {
        AstNode *next = *args;
        *args = next->next;
        next->next = NULL;
        return next;
    }
    return NULL;
}