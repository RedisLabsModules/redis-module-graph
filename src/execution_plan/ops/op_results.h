/*
 * Copyright 2018-2019 Redis Labs Ltd. and Contributors
 *
 * This file is available under the Redis Labs Source Available License Agreement
 */

#pragma once

#include "op.h"
#include "../execution_plan.h"
#include "../../redismodule.h"
#include "../../graph/graphcontext.h"
#include "../../resultset/resultset.h"

/* Results generates result set */

typedef struct {
	OpBase op;
	ResultSet *result_set;
} Results;

/* Creates a new Results operation */
OpBase *NewResultsOp(const ExecutionPlan *plan, ResultSet *result_set);
