/*
* Copyright 2018 - 2019 Redis Labs Ltd. and Contributors
*
*This file is available under the Redis Labs Source Available License Agreement
*/

#include "enrichment/annotate_entities.h"
#include "enrichment/annotate_params.h"
#include "enrichment/annotate_project_all.h"
#include "enrichment/annotate_projected_named_paths.h"

//------------------------------------------------------------------------------
//  Main AST enrichment
//------------------------------------------------------------------------------
/* Directives like CREATE INDEX are not queries. */

void AST_Enrich(AST *ast) {
	if(cypher_astnode_type(ast->root) != CYPHER_AST_QUERY) return;

	AST_AnnotateEntities(ast);
	AST_AnnotateProjectAll(ast);
	AST_AnnotateNamedPaths(ast);
	AST_AnnotateParams(ast);
}

