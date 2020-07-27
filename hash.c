#include <stdio.h>
#include <stdlib.h>
#include "hash.h"

struct intHash *hashInit(unsigned int primaryTableSize) {
	struct intHash *hash = (struct intHash *)malloc(sizeof(struct intHash));
	hash->primaryTableSize = primaryTableSize;
	hash->data             = (struct hashData**)malloc(sizeof(struct hashData *) * primaryTableSize);
	hash->active           = 0;

	for (unsigned int i = 0; i < primaryTableSize; i++) {
		hash->data[i] = NULL;
	}

	pthread_mutex_init(&hash->lock, NULL);

	return hash;
}

void hashDestroy(struct intHash *hash) {
	pthread_mutex_destroy(&hash->lock);
	free(hash->data);
	free(hash);
}

void hashAdd(struct intHash *hash, unsigned int index, void *data) {
	pthread_mutex_lock(&hash->lock);

	unsigned int      hi = index % hash->primaryTableSize;
	struct hashData   *field = hash->data[hi];

	// this section is empty
	if (field == NULL) {
		field = (struct hashData *)malloc(sizeof(struct hashData));
		field->index = index;
		field->value = data;
		field->prev  = NULL;
		field->next  = NULL;
		hash->data[hi] = field;
		hash->active++;
		pthread_mutex_unlock(&hash->lock);
		return;
	}
	
	// non empty, find the last index
	// when going through the list check if the index does not already exist
	struct hashData *prev = field;
	while (field != NULL) {
		if (field->index == index) {
			field->value = data;
			pthread_mutex_unlock(&hash->lock);
			return;
		}

		prev  = field;
		field = field->next;
	}

	// if we got here, there was no previous index
	prev->next = (struct hashData *)malloc(sizeof(struct hashData));
	prev->next->index = index;
	prev->next->value = data;
	prev->next->prev  = prev;
	prev->next->next  = NULL;
	hash->active++;
	pthread_mutex_unlock(&hash->lock);
	return;
}

int hashDelete(struct intHash *hash, unsigned int index) {
	pthread_mutex_lock(&hash->lock);
	
	unsigned int      hi = index % hash->primaryTableSize;
	struct hashData   *field = hash->data[hi];

	if (field == NULL) {
		pthread_mutex_unlock(&hash->lock);
		return 0;
	}

	while (field != NULL) {
		if (field->index == index) {

			if (field->next == NULL && field->prev == NULL) {
				hash->data[hi] = NULL;

			} else if (field->next == NULL && field->prev != NULL) {
				field->prev->next = NULL;

			} else if (field->next != NULL && field->prev == NULL) {
				hash->data[hi] = field->next;
				hash->data[hi]->prev = NULL;

			} else { // field->next != NULL && field->prev != NULL
				// we can never get here
				field->prev->next = field->next;
				field->next->prev = field->prev;
			}

			free(field);
			hash->active--;
			pthread_mutex_unlock(&hash->lock);
			return 1;

		}

		field = field->next;
	}

	fprintf(stderr, "Unable to delete non-existing index %u\n", index);

	pthread_mutex_unlock(&hash->lock);
	return 0;
}

unsigned int hashGetActive(struct intHash *hash) { 
	pthread_mutex_lock(&hash->lock);
	unsigned int active = hash->active;
	pthread_mutex_unlock(&hash->lock);
	return active;
}

unsigned int hashGetDepth(struct intHash *hash) {
	unsigned int maxDepth = 0;

	for (unsigned int i = 0; i < hash->primaryTableSize; i++) {
		struct hashData *field = hash->data[i];
		unsigned int depth = 0;
		
		while (field != NULL) {
			depth++;
			field = field->next;
		}

		if (depth > maxDepth) { maxDepth = depth; }
	}

	return maxDepth;
}

void **hashGetAll(struct intHash *hash, unsigned int *count) {
	pthread_mutex_lock(&hash->lock);

	void **all = (void **)malloc(sizeof(void *) * hash->active);
	*count = 0;

	for (unsigned int i = 0; i < hash->primaryTableSize; i++) {
		struct hashData *field = hash->data[i];
		
		while (field != NULL) {
			all[(*count)++] = field->value;	
			field = field->next;
		}
	}

	pthread_mutex_unlock(&hash->lock);
	return all;
}

