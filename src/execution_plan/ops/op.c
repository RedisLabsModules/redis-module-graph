/*
* Copyright 2018-2019 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "op.h"
#include "../../util/simple_timer.h"

#include <assert.h>

void OpBase_Init(OpBase *op) {
    op->modifies = NULL;
    op->childCount = 0;
    op->children = NULL;
    op->parent = NULL;
    
    // Function pointers.
    op->init = NULL;
    op->free = NULL;
    op->reset = NULL;
    op->consume = NULL;
    op->toString = NULL;
}

void OpBase_Reset(OpBase *op) {
    assert(op->reset(op) == OP_OK);
    for(int i = 0; i < op->childCount; i++) OpBase_Reset(op->children[i]);
}

int OpBase_ToString(const OpBase *op, char *buff, uint buff_len) {
    if(op->toString) return op->toString(op, buff, buff_len);
    else return snprintf(buff, buff_len, "%s", op->name);
}

Record OpBase_Profile(OpBase *op) {
    double tic [2];
    // Start timer.
    simple_tic(tic);
    Record r = op->profile(op);
    // Stop timer and accumulate.
    op->profileExecTime += simple_toc(tic);
    if(r) op->profileRecordCount++;
    return r;
}

void OpBase_Free(OpBase *op) {
    // Free internal operation
    op->free(op);
    if(op->children) free(op->children);
    if(op->modifies) Vector_Free(op->modifies);
    free(op);
}
