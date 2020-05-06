/*
* Copyright 2018-2020 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#pragma once

// A single header file including all important headers for serialization.

// Include Redis Modules API.
#include "../redismodule.h"
// Include Graph Context.
#include "../graph/graphcontext.h"
// Include Query contxt.
#include "../query_ctx.h"
// Include GraphBLAS.
#include "../../deps/GraphBLAS/Include/GraphBLAS.h"
// Utils.
#include "../util/arr.h"
#include "../util/rmalloc.h"
#include "../util/qsort.h"
// Non primitive data types.
#include "../datatypes/array.h"
// Graph extentions.
#include "graph_extensions.h"
// Module configuration
#include "../config.h"

// This struct is for encoding version 7 and up to declare or infer the payload of the key.
// It contains the type that was encoded and the number of entities.
typedef struct {
	EncodeState state;        // Payload contents.
	uint64_t entities_count;  // Number of entities in the payload.
} PayloadInfo;