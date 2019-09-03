/*
* Copyright 2018-2019 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#pragma once

#include "op.h"
#include "../execution_plan.h"

/* Cartesian product AKA Join. */
typedef struct {
	OpBase op;
	bool init;
	Record r;
} CartesianProduct;

OpBase *NewCartesianProductOp(const ExecutionPlan *plan);
OpResult CartesianProductInit(OpBase *opBase);
Record CartesianProductConsume(OpBase *opBase);
OpResult CartesianProductReset(OpBase *opBase);
void CartesianProductFree(OpBase *opBase);
