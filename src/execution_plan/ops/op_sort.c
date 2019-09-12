/*
* Copyright 2018-2019 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "op_sort.h"
#include "op_project.h"
#include "op_aggregate.h"
#include "../../util/arr.h"
#include "../../util/qsort.h"
#include "../../util/rmalloc.h"

/* Forward declarations. */
static OpResult Init(OpBase *opBase);
static Record Consume(OpBase *opBase);
static OpResult Reset(OpBase *opBase);
static void Free(OpBase *opBase);

static bool _record_islt(Record a, Record b, const OpSort *op) {
	// First N values in record correspond to RETURN expressions
	// N .. M values correspond to ORDER-BY expressions
	// Query: RETURN A.V, B.V, C.V ORDER BY B.W, C.V*A.V
	// Record
	// ------------------------------------
	// |  V0  |  V1  |  V2  |  V3  |  V4  |
	// ------------------------------------
	// V0 - A.V
	// V1 - B.V
	// V2 - C.V
	// V3 - B.W
	// V4 - C.V*A.
	uint offset = op->offset;
	uint comparables = array_len(op->expressions);

	for(uint i = 0; i < comparables; i++) {
		SIValue aVal = Record_Get(a, offset + i);
		SIValue bVal = Record_Get(b, offset + i);
		int rel = SIValue_Compare(aVal, bVal, NULL);
		if(rel == 0) continue;  // Elements are equal; try next ORDER BY element
		rel *= op->direction;   // Flip value for descending order.
		return rel > 0;         // Return true if the current left element is less than the right.
	}
	return false;   // b >= a
}

// Compares two records on a subset of fields
// Return value similar to strcmp
static int _record_compare(Record a, Record b, const OpSort *op) {
	// First N values in record correspond to RETURN expressions
	// N .. M values correspond to ORDER-BY expressions
	// Query: RETURN A.V, B.V, C.V ORDER BY B.W, C.V*A.V
	// Record
	// ------------------------------------
	// |  V0  |  V1  |  V2  |  V3  |  V4  |
	// ------------------------------------
	// V0 - A.V
	// V1 - B.V
	// V2 - C.V
	// V3 - B.W
	// V4 - C.V*A.
	uint offset = op->offset;
	uint comparables = array_len(op->expressions);

	for(uint i = 0; i < comparables; i++) {
		SIValue aVal = Record_Get(a, offset + i);
		SIValue bVal = Record_Get(b, offset + i);
		int rel = SIValue_Compare(aVal, bVal, NULL);
		if(rel) return rel;
	}
	return 0;
}

// Compares two heap record nodes.
static int _heap_elem_compare(const void *A, const void *B, const void *udata) {
	OpSort *op = (OpSort *)udata;
	Record aRec = (Record)A;
	Record bRec = (Record)B;
	return _record_compare(aRec, bRec, op) * op->direction;
}

static void _accumulate(OpSort *op, Record r) {
	if(!op->limit) {
		/* Not using a heap and there's room for record. */
		op->buffer = array_append(op->buffer, r);
		return;
	}

	if(heap_count(op->heap) < op->limit) {
		heap_offer(&op->heap, r);
	} else {
		// No room in the heap, see if we need to replace
		// a heap stored record with the current record.
		if(_heap_elem_compare(heap_peek(op->heap), r, op) > 0) {
			Record replaced = heap_poll(op->heap);
			Record_Free(replaced);
			heap_offer(&op->heap, r);
		} else {
			Record_Free(r);
		}
	}
}

static Record _handoff(OpSort *op) {
	if(array_len(op->buffer) > 0) {
		Record r = array_pop(op->buffer);
		return r;
	}
	return NULL;
}

uint _determineOffset(OpBase *op) {
	assert(op);

	if(op->type == OPType_AGGREGATE) {
		OpAggregate *aggregate = (OpAggregate *)op;
		return array_len(aggregate->exps);
	} else if(op->type == OPType_PROJECT) {
		OpProject *project = (OpProject *)op;
		return project->exp_count;
	}

	return _determineOffset(op->children[0]);
}

OpBase *NewSortOp(const ExecutionPlan *plan, AR_ExpNode **expressions, int direction,
				  unsigned int limit) {
	OpSort *op = malloc(sizeof(OpSort));
	op->offset = 0;
	op->heap = NULL;
	op->buffer = NULL;
	op->limit = limit;
	op->direction = direction;
	op->expressions = expressions;

	if(op->limit) op->heap = heap_new(_heap_elem_compare, op);
	else op->buffer = array_new(Record, 32);

	// Set our Op operations
	OpBase_Init((OpBase *)op, OPType_SORT, "Sort", Init, Consume, Reset, NULL, Free, plan);

	return (OpBase *)op;
}

/* `op` is an actual variable in the caller function. Using it in a
 * macro like this is rather ugly, but the macro passed to QSORT must
 * accept only 2 arguments. */
#define RECORD_SORT(a, b) (_record_islt((*a), (*b), op))

static OpResult Init(OpBase *opBase) {
	OpSort *op = (OpSort *)opBase;
	op->offset = _determineOffset(opBase->children[0]);
	return OP_OK;
}

static Record Consume(OpBase *opBase) {
	OpSort *op = (OpSort *)opBase;
	Record r = _handoff(op);
	if(r) return r;

	// If we're here, we don't have any records to return
	// try to get records.
	OpBase *child = op->op.children[0];
	bool newData = false;
	while((r = OpBase_Consume(child))) {
		_accumulate(op, r);
		newData = true;
	}
	if(!newData) return NULL;

	if(op->buffer) {
		QSORT(Record, op->buffer, array_len(op->buffer), RECORD_SORT);
	} else {
		// Heap, responses need to be reversed.
		int records_count = heap_count(op->heap);
		op->buffer = array_new(Record, records_count);

		/* Pop items from heap */
		while(records_count > 0) {
			r = heap_poll(op->heap);
			op->buffer = array_append(op->buffer, r);
			records_count--;
		}
	}

	// Pass ordered records downward.
	return _handoff(op);
}

/* Restart iterator */
static OpResult Reset(OpBase *ctx) {
	OpSort *op = (OpSort *)ctx;
	uint recordCount;

	if(op->heap) {
		recordCount = heap_count(op->heap);
		for(uint i = 0; i < recordCount; i++) {
			Record r = (Record)heap_poll(op->heap);
			Record_Free(r);
		}
	}

	if(op->buffer) {
		recordCount = array_len(op->buffer);
		for(uint i = 0; i < recordCount; i++) {
			Record r = array_pop(op->buffer);
			Record_Free(r);
		}
	}

	return OP_OK;
}

/* Frees Sort */
static void Free(OpBase *ctx) {
	OpSort *op = (OpSort *)ctx;

	if(op->heap) {
		uint recordCount = heap_count(op->heap);
		for(uint i = 0; i < recordCount; i++) {
			Record r = (Record)heap_poll(op->heap);
			Record_Free(r);
		}
		heap_free(op->heap);
		op->heap = NULL;
	}

	if(op->buffer) {
		uint recordCount = array_len(op->buffer);
		for(uint i = 0; i < recordCount; i++) {
			Record r = array_pop(op->buffer);
			Record_Free(r);
		}
		array_free(op->buffer);
		op->buffer = NULL;
	}

	if(op->expressions) {
		for(int i = 0; i < array_len(op->expressions); i++) AR_EXP_Free(op->expressions[i]);
		array_free(op->expressions);
		op->expressions = NULL;
	}
}

