/*
* Copyright 2018-2019 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#pragma once

#include "GraphBLAS/Include/GraphBLAS.h"
#include "graph/entities/node.h"
#include "value.h"
#include "graph/entities/graph_entity.h"

// TODO: note it is possible to get into an inconsistency
// if we set src and srcNodeID to different nodes
typedef struct Edge {
    Entity *entity;          // MUST be the first property of Edge
    char *alias;             // Alias attached to edge
    char* relationship;      // Label attached to edge
    int relationId;          // Label ID
    Node* src;               // Pointer to source node
    Node* dest;              // Pointer to destination node
    NodeID srcNodeID;        // Source node ID
    NodeID destNodeID;       // Destination node ID
    GrB_Matrix mat;          // Adjacency matrix, associated with edge
} Edge;

// Creates a new edge, connecting src to dest node
Edge* Edge_New(Node *src, Node *dest, const char *relationship, const char *alias);

// Retrieve edge source node ID
NodeID Edge_GetSrcNodeID(const Edge *edge);

// Retrieve edge destination node ID
NodeID Edge_GetDestNodeID(const Edge *edge);

// Retrieve edge relation ID
int Edge_GetRelationID(const Edge *edge);

// Retrieve edge source node
Node* Edge_GetSrcNode(Edge *e);

// Retrieve edge destination node
Node* Edge_GetDestNode(Edge *e);

// Sets edge source node
void Edge_SetSrcNode(Edge *e, Node *src);

// Sets edge destination node
void Edge_SetDestNode(Edge *e, Node *dest);

// Sets edge relation type
void Edge_SetRelationID(Edge *e, int relationId);

void Edge_Print(const Edge *edge, FILE *out);

// Frees allocated space by given edge
void Edge_Free(Edge *edge);
