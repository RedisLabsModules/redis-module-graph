/*
* Copyright 2018-2019 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#ifndef __GRAPH_RESULTSET_H__
#define __GRAPH_RESULTSET_H__

#include "resultset_header.h"
#include "resultset_records.h"
#include "resultset_statistics.h"
#include "../parser/ast.h"
#include "../redismodule.h"
#include "../util/vector.h"
#include "../execution_plan/record.h"
#include "../util/triemap/triemap.h"

#define RESULTSET_UNLIMITED 0
#define RESULTSET_OK 1
#define RESULTSET_FULL 0

typedef struct {
    RedisModuleCtx *ctx;
    ResultSetHeader *header;    /* Describes how records should look like. */
    bool distinct;              /* Rather or not each record is unique. */
    size_t recordCount;         /* Number of records introduced. */
    char *buffer;               /* Reusable buffer for record streaming. */
    size_t bufferLen;           /* Size of buffer in bytes. */
    ResultSetStatistics stats;  /* ResultSet statistics. */
    EmitRecordFunc EmitRecord;  /* Function pointer to Record reply routine. */
} ResultSet;

ResultSet* NewResultSet(AST* ast, RedisModuleCtx *ctx, bool compact);

void ResultSet_CreateHeader(ResultSet* set, const AST *ast);

int ResultSet_AddRecord(ResultSet* set, GraphContext *gc, Record r);

void ResultSet_Replay(ResultSet* set);

void ResultSet_Free(ResultSet* set);

#endif
