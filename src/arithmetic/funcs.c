
/*
* Copyright 2018-2020 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "funcs.h"
#include "../RG.h"
#include "../../deps/rax/rax.h"

extern rax *__aeRegisteredFuncs;
extern rax *__aggFuncs; // Set of all aggregate function names.

void AR_RegisterFuncs() {
	ASSERT(!__aeRegisteredFuncs);
	__aeRegisteredFuncs = raxNew();
	__aggFuncs = raxNew();

	Register_ListFuncs();
	Register_TimeFuncs();
	Register_EntityFuncs();
	Register_StringFuncs();
	Register_NumericFuncs();
	Register_BooleanFuncs();
	Register_ConditionalFuncs();
	Register_ComprehensionFuncs();
	Register_PathFuncs();
	Register_PlaceholderFuncs();
	Register_AggFuncs();
}

