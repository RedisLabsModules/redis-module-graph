/*
* Copyright 2018-2019 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "group_cache.h"

CacheGroup *CacheGroupNew() {
	return raxNew();
}

void CacheGroupAdd(CacheGroup *groups, char *key, Group *group) {
	raxInsert(groups, (unsigned char *)key, strlen(key), group, NULL);
}

// Retrives a group,
// Sets group to NULL if key is missing.
Group *CacheGroupGet(CacheGroup *groups, char *key) {
	Group *g = raxFind(groups, (unsigned char *)key, strlen(key));
	if(g == raxNotFound) return NULL;
	return g;
}

void FreeGroupCache(CacheGroup *groups) {
	raxFreeWithCallback(groups, (void (*)(void *))FreeGroup);
}

// Populates an iterator to scan entire group cache
void CacheGroupIter(CacheGroup *groups, CacheGroupIterator *iter) {
	raxStart(iter, groups);
	raxSeek(iter, ">=", (unsigned char *)"", 0);
}

// Advance iterator and returns key & value in current position.
int CacheGroupIterNext(CacheGroupIterator *iter, char **key, Group **group) {
	int res = raxNext(iter);
	if(res == 0) {
		*group = NULL;
	} else {
		*group = iter->data; // TODO revisit this to fix up
	}
	return res;
}

void CacheGroupIterator_Free(CacheGroupIterator *iter) {
	if(iter) raxStop(iter);
}
