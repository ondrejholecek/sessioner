#ifndef __HASH_H__
#define __HASH_H__

#include <pthread.h>

struct hashData {
	unsigned int    index;
	void            *value;
	struct hashData *prev, *next;
};

struct intHash {
	pthread_mutex_t  lock;
	unsigned int     primaryTableSize;
	unsigned int     active;
	struct hashData  **data;
};

struct intHash *hashInit(unsigned int primaryTableSize);
void hashDestroy(struct intHash *hash);
void hashAdd(struct intHash *hash, unsigned int index, void *data);
int hashDelete(struct intHash *hash, unsigned int index);
unsigned int hashGetActive(struct intHash *hash);
unsigned int hashGetDepth(struct intHash *hash);
void **hashGetAll(struct intHash *hash, unsigned int *count);


#endif
