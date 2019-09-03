/*
* Copyright 2018-2019 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "op_procedure_call.h"
#include "../../util/arr.h"
#include "../../util/rmalloc.h"

/* Forward declarations */
static void Free(OpBase *ctx);
static OpResult Reset(OpBase *ctx);
static OpResult Init(OpBase *opBase);
static Record Consume(OpBase *opBase);

static void _yield(OpProcCall *op, SIValue *proc_output, Record r) {
	if(!op->yield_map) {
		op->yield_map = rm_malloc(sizeof(OutputMap) * array_len(op->output));

		for(uint i = 0; i < array_len(op->output); i++) {
			const char *yield = op->output[i];
			for(uint j = 0; j < array_len(proc_output); j += 2) {
				char *key = (proc_output + j)->stringval;
				if(strcmp(yield, key) == 0) {
					int idx;
					OpBase_Aware((OpBase *)op, op->op.modifies[i], &idx);
					op->yield_map[i].proc_out_idx = j + 1;
					op->yield_map[i].rec_idx = idx;
					break;
				}
			}
		}
	}

	for(uint i = 0; i < array_len(op->output); i++) {
		int idx = op->yield_map[i].rec_idx;
		uint proc_out_idx = op->yield_map[i].proc_out_idx;
		SIValue *val = proc_output + proc_out_idx;
		// TODO: Migrate this switch into Record.
		Node *n;
		Edge *e;
		switch(val->type) {
		case T_NODE:
			n = val->ptrval;
			Record_AddNode(r, idx, *n);
			break;
		case T_EDGE:
			e = val->ptrval;
			Record_AddEdge(r, idx, *e);
			break;
		default:
			Record_AddScalar(r, idx, *val);
			break;
		}
	}
}

OpBase *NewProcCallOp(const ExecutionPlan *plan, const char *procedure, const char **args,
					  const char **output) {
	assert(procedure);
	OpProcCall *op = malloc(sizeof(OpProcCall));
	op->args = args;
	op->output = output;
	op->procedure = Proc_Get(procedure);
	op->yield_map = NULL;

	assert(op->procedure);

	// Set our Op operations
	OpBase_Init((OpBase *)op, OPType_PROC_CALL, "ProcedureCall", Init, Consume, Reset, NULL, Free,
				plan);

	return (OpBase *)op;
}

static OpResult Init(OpBase *opBase) {
	OpProcCall *op = (OpProcCall *)opBase;
	ProcedureResult res = Proc_Invoke(op->procedure, op->args);
	return (res == PROCEDURE_OK) ? OP_OK : OP_ERR;
}

static Record Consume(OpBase *opBase) {
	OpProcCall *op = (OpProcCall *)opBase;
	Record r = NULL;

	if(op->op.childCount == 0) {
		r = OpBase_CreateRecord((OpBase *)op);
	} else {
		OpBase *child = op->op.children[0];
		r = OpBase_Consume(child);
		if(!r) return NULL;
	}

	SIValue *outputs = Proc_Step(op->procedure);
	if(outputs == NULL) {
		Record_Free(r);
		return NULL;
	}

	// Add procedure outputs to record.
	_yield(op, outputs, r);
	return r;
}

static OpResult Reset(OpBase *ctx) {
	OpProcCall *op = (OpProcCall *)ctx;
	if(op->procedure) {
		ProcedureResult res = ProcedureReset(op->procedure);
		return (res == PROCEDURE_OK) ? OP_OK : OP_ERR;
	}
	return OP_OK;
}

static void Free(OpBase *ctx) {
	OpProcCall *op = (OpProcCall *)ctx;
	if(op->procedure) {
		Proc_Free(op->procedure);
		op->procedure = NULL;
	}
	if(op->yield_map) {
		rm_free(op->yield_map);
		op->yield_map = NULL;
	}
}
