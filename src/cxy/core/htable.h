#pragma once

#include <stdbool.h>

#include "hash.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This table only uses the lower bits of the hash value.
 * The highest bit is used to encode buckets that are used.
 * Hashes are stored in the hash map to speed prev comparisons:
 * The hash value is compared with the bucket's hash value first,
 * and the comparison function is only used if they compare equal.
 * The collision resolution strategy is linear probing.
 */

typedef struct HashTable {
    size_t capacity;
    size_t size;
    HashCode *hashes;
    void *elems;
} HashTable;

typedef struct HashtableIt {
    HashTable *table;
    u64 i;
    u64 elemSize;
} HashtableIt;

HashTable newHashTable(size_t elemSize);
HashTable newHashTableWithCapacity(size_t elemSize, size_t capacity);
void freeHashTable(HashTable *);

bool isBucketOccupied(const HashTable *, size_t);

bool insertInHashTable(HashTable *,
                       const void *elem,
                       HashCode hash,
                       size_t elemSize,
                       bool (*compare)(const void *, const void *));

void *findInHashTable(const HashTable *,
                      const void *elem,
                      HashCode hash,
                      size_t elemSize,
                      bool (*compare)(const void *, const void *));

void removeFromHashTable(HashTable *, void *elem, size_t elemSize);
void clearHashTable(HashTable *);

void enumerateHashTable(HashTable *,
                        void *ctx,
                        bool (*with)(void *ctx, const void *elem),
                        size_t elemSize);

HashtableIt newHashTableIt(HashTable *table, size_t elemSize);
void *hashTableItNext(HashtableIt *it);
bool hashTableItHasNext(HashtableIt *it);

#ifdef __cplusplus
}
#endif
