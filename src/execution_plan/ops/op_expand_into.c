/*
* Copyright 2018-2020 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "op_expand_into.h"
#include "shared/print_functions.h"
#include "../../ast/ast.h"
#include "../../util/arr.h"
#include "../../util/rmalloc.h"
#include "../../query_ctx.h"
#include "../../GraphBLASExt/GxB_Delete.h"

/* Forward declarations. */
static OpResult ExpandIntoInit(OpBase *opBase);
static Record ExpandIntoConsume(OpBase *opBase);
static OpResult ExpandIntoReset(OpBase *opBase);
static OpBase *ExpandIntoClone(const ExecutionPlan *plan, const OpBase *opBase);
static void ExpandIntoFree(OpBase *opBase);

// String representation of operation.
static inline int ExpandIntoToString(const OpBase *ctx, char *buf, uint buf_len) {
	return TraversalToString(ctx, buf, buf_len, ((const OpExpandInto *)ctx)->ae);
}

/* Collects traversed edge relations.
 * e.g. [e:R0|R1]
 * op->edgeRelationTypes will hold both R0 and R1 IDs.
 * in the case where no relationship types are specified
 * op->edgeRelationTypes will contain GRAPH_NO_RELATION. */
static void _setupTraversedRelations(OpExpandInto *op, QGEdge *e) {
	uint reltype_count = array_len(e->reltypeIDs);
	if(reltype_count > 0) {
		array_clone(op->edgeRelationTypes, e->reltypeIDs);
		op->edgeRelationCount = reltype_count;
	} else {
		op->edgeRelationCount = 1;
		op->edgeRelationTypes = array_new(int, 1);
		op->edgeRelationTypes = array_append(op->edgeRelationTypes, GRAPH_NO_RELATION);
	}
}

// Sets traversed edge within record.
static bool _setEdge(OpExpandInto *op) {
	// Consumed edges connecting current source and destination nodes.
	if(!array_len(op->edges)) return false;

	Edge *e = op->edges + (array_len(op->edges) - 1);
	Record_AddEdge(op->r, op->edgeIdx, *e);
	array_pop(op->edges);
	return true;
}

static void _populate_filter_matrix(OpExpandInto *op) {
	for(uint i = 0; i < op->recordCount; i++) {
		Record r = op->records[i];
		/* Update filter matrix F, set row i at position srcId
		 * F[i, srcId] = true. */
		Node *n = Record_GetNode(r, op->srcNodeIdx);
		NodeID srcId = ENTITY_GET_ID(n);
		GrB_Matrix_setElement_BOOL(op->F, true, i, srcId);
	}
}

/* Evaluate algebraic expression:
 * appends filter matrix as the left most operand
 * perform multiplications.
 * removed filter matrix from original expression
 * clears filter matrix. */
static void _traverse(OpExpandInto *op) {
	// Create both filter and result matrices.
	if(op->F == GrB_NULL) {
		size_t required_dim = Graph_RequiredMatrixDim(op->graph);
		GrB_Matrix_new(&op->M, GrB_BOOL, op->recordsCap, required_dim);
		GrB_Matrix_new(&op->F, GrB_BOOL, op->recordsCap, required_dim);
	}

	// Populate filter matrix.
	_populate_filter_matrix(op);
	// Clone expression, as we're about to modify the structure with Optimize.
	AlgebraicExpression *clone = AlgebraicExpression_Clone(op->ae);
	// Append filter matrix to algebraic expression, as the left most operand.
	AlgebraicExpression_MultiplyToTheLeft(&clone, op->F);
	// TODO: consider performing optimization as part of evaluation.
	AlgebraicExpression_Optimize(&clone);
	// Evaluate expression.
	AlgebraicExpression_Eval(clone, op->M);
	// Free clone.
	AlgebraicExpression_Free(clone);
	// Clear filter matrix.
	GrB_Matrix_clear(op->F);
}

OpBase *NewExpandIntoOp(const ExecutionPlan *plan, Graph *g, AlgebraicExpression *ae,
						AR_ExpNode *records_cap_expr) {
	OpExpandInto *op = rm_calloc(1, sizeof(OpExpandInto));
	op->graph = g;
	op->ae = ae;
	op->r = NULL;
	op->edges = NULL;
	op->F = GrB_NULL;
	op->M = GrB_NULL;
	op->recordCount = 0;
	op->edgeRelationTypes = NULL;
	op->recordsCapExpr = records_cap_expr;
	op->recordsCap = 0;
	op->records = rm_calloc(op->recordsCap, sizeof(Record));

	// Set our Op operations
	OpBase_Init((OpBase *)op, OPType_EXPAND_INTO, "Expand Into", ExpandIntoInit, ExpandIntoConsume,
				ExpandIntoReset, ExpandIntoToString, ExpandIntoClone, ExpandIntoFree, false, plan);

	// Make sure that all entities are represented in Record
	op->edgeIdx = IDENTIFIER_NOT_FOUND;
	assert(OpBase_Aware((OpBase *)op, AlgebraicExpression_Source(ae), &op->srcNodeIdx));
	assert(OpBase_Aware((OpBase *)op, AlgebraicExpression_Destination(ae), &op->destNodeIdx));

	const char *edge = AlgebraicExpression_Edge(ae);
	if(edge) {
		op->setEdge = true;
		op->edges = array_new(Edge, 32);
		QGEdge *e = QueryGraph_GetEdgeByAlias(plan->query_graph, edge);
		_setupTraversedRelations(op, e);
		op->edgeIdx = OpBase_Modifies((OpBase *)op, edge);
	}

	return (OpBase *)op;
}

static OpResult ExpandIntoInit(OpBase *opBase) {
	OpExpandInto *op = (OpExpandInto *)opBase;
	op->recordsCap = TRAVERSE_RECORDS_CAP;
	if(op->recordsCapExpr) {
		SIValue limit_value =  AR_EXP_Evaluate(op->recordsCapExpr, NULL);
		if(SI_TYPE(limit_value) != T_INT64) {
			char *error;
			asprintf(&error, "LIMIT specified value of invalid type, must be a positive integer");
			QueryCtx_SetError(error); // Set the query-level error.
			QueryCtx_RaiseRuntimeException();
			op->recordsCap = 0;
			return OP_ERR;
		}
		op->recordsCap = MIN(op->recordsCap, limit_value.longval);
	}
	op->records = rm_calloc(op->recordsCap, sizeof(Record));
	return OP_OK;
}

/* Emits a record when possible,
 * Returns NULL when we've got no more records. */
static Record _handoff(OpExpandInto *op) {
	/* If we're required to update edge,
	 * try to get an edge, if successful we can return quickly,
	 * otherwise try to get a new pair of source and destination nodes. */
	if(op->setEdge) {
		if(_setEdge(op)) return OpBase_CloneRecord(op->r);
	}

	Node *srcNode;
	Node *destNode;
	NodeID srcId = INVALID_ENTITY_ID;
	NodeID destId = INVALID_ENTITY_ID;

	/* Find a record where both record's source and destination
	 * nodes are connected. */
	while(op->recordCount) {
		op->recordCount--;
		// Current record resides at row recordCount.
		int rowIdx = op->recordCount;
		op->r = op->records[op->recordCount];
		assert(Record_GetType(op->r, op->srcNodeIdx) == REC_TYPE_NODE);
		assert(Record_GetType(op->r, op->destNodeIdx) == REC_TYPE_NODE);
		srcNode = Record_GetNode(op->r, op->srcNodeIdx);
		destNode = Record_GetNode(op->r, op->destNodeIdx);
		srcId = ENTITY_GET_ID(srcNode);
		destId = ENTITY_GET_ID(destNode);
		bool x;
		GrB_Info res = GrB_Matrix_extractElement_BOOL(&x, op->M, rowIdx, destId);
		// Src is not connected to dest.
		if(res != GrB_SUCCESS) continue;

		// If we're here src is connected to dest.
		if(op->setEdge) {
			for(int i = 0; i < op->edgeRelationCount; i++) {
				Graph_GetEdgesConnectingNodes(op->graph,
											  srcId,
											  destId,
											  op->edgeRelationTypes[i],
											  &op->edges);
			}
			_setEdge(op);
			return OpBase_CloneRecord(op->r);
		}

		// Mark as NULL to avoid double free.
		op->records[op->recordCount] = NULL;
		return op->r;
	}

	// Didn't manage to emit record.
	return NULL;
}

/* ExpandIntoConsume next operation
 * returns OP_DEPLETED when no additional updates are available */
static Record ExpandIntoConsume(OpBase *opBase) {
	Node *n;
	Record r;
	OpExpandInto *op = (OpExpandInto *)opBase;
	OpBase *child = op->op.children[0];

	// As long as we don't have a record to emit.
	while((r = _handoff(op)) == NULL) {
		/* If we're here, we didn't managed to emit a record,
		 * clean up and try to get new data points. */
		for(int i = 0; i < op->recordsCap; i++) {
			if(op->records[i]) {
				OpBase_DeleteRecord(op->records[i]);
				op->records[i] = NULL;
			} else {
				break;
			}
		}

		// Ask child operations for data.
		for(op->recordCount = 0; op->recordCount < op->recordsCap; op->recordCount++) {
			Record childRecord = OpBase_Consume(child);
			// Did not managed to get new data, break.
			if(!childRecord) break;

			// Store received record.
			op->records[op->recordCount] = childRecord;
		}

		// Depleted.
		if(op->recordCount == 0) return NULL;
		_traverse(op);
	}

	return r;
}

static OpResult ExpandIntoReset(OpBase *ctx) {
	OpExpandInto *op = (OpExpandInto *)ctx;
	op->r = NULL;
	for(int i = 0; i < op->recordCount; i++) {
		if(op->records[i]) OpBase_DeleteRecord(op->records[i]);
	}
	op->recordCount = 0;

	if(op->edges) array_clear(op->edges);
	if(op->iter) {
		GxB_MatrixTupleIter_free(op->iter);
		op->iter = NULL;
	}
	if(op->F != GrB_NULL) GrB_Matrix_clear(op->F);
	return OP_OK;
}

static inline OpBase *ExpandIntoClone(const ExecutionPlan *plan, const OpBase *opBase) {
	assert(opBase->type == OPType_EXPAND_INTO);
	OpExpandInto *op = (OpExpandInto *)opBase;
	return NewExpandIntoOp(plan, QueryCtx_GetGraph(), AlgebraicExpression_Clone(op->ae),
						   AR_EXP_Clone(op->recordsCapExpr));
}

/* Frees ExpandInto */
static void ExpandIntoFree(OpBase *ctx) {
	OpExpandInto *op = (OpExpandInto *)ctx;
	if(op->F != GrB_NULL) {
		GrB_Matrix_free(&op->F);
		op->F = GrB_NULL;
	}

	if(op->M != GrB_NULL) {
		GrB_Matrix_free(&op->M);
		op->M = GrB_NULL;
	}

	if(op->edges) {
		array_free(op->edges);
		op->edges = NULL;
	}

	if(op->edgeRelationTypes) {
		array_free(op->edgeRelationTypes);
		op->edgeRelationTypes = NULL;
	}

	if(op->ae) {
		AlgebraicExpression_Free(op->ae);
		op->ae = NULL;
	}

	if(op->records) {
		for(int i = 0; i < op->recordsCap; i++) {
			if(op->records[i]) OpBase_DeleteRecord(op->records[i]);
		}
		rm_free(op->records);
		op->records = NULL;
	}

	if(op->recordsCapExpr) {
		AR_EXP_Free(op->recordsCapExpr);
		op->recordsCapExpr = NULL;
	}
}

