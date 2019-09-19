#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <rte_config.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_rwlock.h>

#include "hashTable.h"

#define PROBE_COUNT 5

#define STATUS_AVAILABLE 0
#define STATUS_INIT 1
#define STATUS_USE 2

struct ListElem
{
	rte_rwlock_t rwlock;
	unsigned int status;
	uint64_t timeout;

	void *key;
	void *value;
};

struct HashTableStatis
{
	rte_atomic32_t failed;
	rte_atomic32_t collision;
	unsigned int totalMem;
};

struct hashTable
{
	struct ListElem *bucket;
	unsigned int bucketSize;
	unsigned int capacity;

	int probeStep;
	int factor;
	struct HashTableOps ops;
	struct HashTableStatis st;
};

static int Init(struct hashTable *htbl, int size)
{
	unsigned int i = 0;

	htbl->capacity = size;
	htbl->bucketSize = htbl->factor*size;
	htbl->bucket = htbl->ops.mallocFunc(sizeof(struct ListElem)*htbl->bucketSize);
	if( !htbl->bucket )
	{
		printf("Malloc bucket failed.\n");
		return -1;
	}
	memset(htbl->bucket, 0x00, sizeof(struct ListElem)*htbl->bucketSize);
	for( ; i < htbl->bucketSize; i++)
	{
		rte_rwlock_init(&(htbl->bucket[i].rwlock));
	}
	htbl->st.totalMem = sizeof(*htbl) + sizeof(struct ListElem)*htbl->bucketSize;

	return 0;
}

static int InsertElem(struct hashTable *htbl, void *data, int dLen, void *key, void *value, uint64_t timeout)
{
	int idx = 0;
	struct ListElem *elem = NULL;
	uint64_t currentTime = 0;
	int count = 0;
	int ret = RET_FAILED;
	uint64_t expired = 0;

	currentTime = rte_rdtsc();
	expired = currentTime + timeout;
	idx = htbl->ops.hash(data, dLen, key)%htbl->bucketSize;
	elem = &htbl->bucket[idx];

	while( count < htbl->probeStep )
	{
		rte_rwlock_write_lock(&elem->rwlock);
		if( elem->status == STATUS_AVAILABLE )
		{
			elem->key = key;
			elem->value = value;
			elem->timeout = expired;
			elem->status = STATUS_USE;
			ret = RET_NEW;
			rte_rwlock_write_unlock(&elem->rwlock);
			break;
		}

		if( (elem->status == STATUS_USE) && (currentTime >= elem->timeout || htbl->ops.cmp(elem->key, key)) )
		{
			htbl->ops.assignKey(key, elem->key);
			htbl->ops.assignValue(value, elem->value);
			elem->timeout = expired;
			ret = RET_OCCUPY;
			rte_rwlock_write_unlock(&elem->rwlock);
			break;
		}
		rte_rwlock_write_unlock(&elem->rwlock);

		elem++;
		count++;
		rte_atomic32_inc(&htbl->st.collision);
	}

	if( ret == RET_FAILED )
	{
		rte_atomic32_inc(&htbl->st.failed);
		goto FAILED;
	}

	return ret;

FAILED:
	return ret;
}

static void *FindElem(struct hashTable *htbl, void *data, int dLen, void *key, void *value)
{
	int idx = 0;
	struct ListElem *elem = NULL;
	int count = 0;
	int find = 0;
	uint64_t currentTime = 0;

	idx = htbl->ops.hash(data, dLen, key)%htbl->bucketSize;
	elem = &htbl->bucket[idx];
	currentTime = rte_rdtsc();

	while( count <= htbl->probeStep )
	{
		rte_rwlock_read_lock(&elem->rwlock);
		if( (elem->status == STATUS_USE) && 
				(htbl->ops.cmp(elem->key, key) == 1) &&
				(currentTime < elem->timeout) )
		{
			find = 1;
			if( value )
			{
				htbl->ops.assignValue(elem->value, value);
			}
			rte_rwlock_read_unlock(&elem->rwlock);
			break;
		}
		rte_rwlock_read_unlock(&elem->rwlock);

		elem++;
		count++;
	}

	return (find?elem:NULL);
}

void *hash_table_find(struct hashTable *htbl, void *data, int dLen, void *key, void *value)
{
	struct ListElem *elem = NULL;
	if( !htbl || !key || !dLen )
	{
		return NULL;
	}

	elem = FindElem(htbl, data, dLen, key, value);
	return elem?elem->value:NULL;
}

int hash_table_insert(struct hashTable *htbl, void *data, int dLen, void *key, void *value, uint64_t timeout)
{
	if( !htbl || !key || !value || !dLen )
	{
		return -1;
	}

	return InsertElem(htbl, data, dLen, key, value, timeout);
}

struct hashTable *hash_table_create(unsigned int capacity, struct HashTableOps *ops)
{
	struct hashTable *htbl = NULL;
	int ret = 0;

	if( !ops->hash || !ops->cmp || !ops->assignKey || !ops->assignValue )
	{
		printf("hash/cmp/assignKey/assignValue interface must be provided!\n");
		goto FAILED;
	}

	if( !ops->mallocFunc )
	{
		ops->mallocFunc = malloc;
	}
	if( !ops->freeFunc )
	{
		ops->freeFunc = free;
	}
	htbl = ops->mallocFunc(sizeof(struct hashTable));

	if( !htbl )
	{
		printf("Malloc Hash Table failed\n");
		goto FAILED;
	}
	htbl->factor = 3;
	htbl->probeStep = PROBE_COUNT;
	htbl->ops = *ops;

	ret = Init(htbl, capacity);
	if( ret < 0 )
	{
		printf("Init Hash Table failed\n");
		goto FAILED;
	}

	return htbl;

FAILED:
	if( htbl )
	{
		hash_table_destroy(htbl);
	}
	return NULL;
}

void hash_table_destroy(struct hashTable *htbl)
{
	if( !htbl )
	{
		return;
	}

    htbl->ops.freeFunc(htbl->bucket);	
	htbl->ops.freeFunc(htbl);
}

void hash_table_assess(struct hashTable *htbl)
{
	unsigned int i = 0;
	int cnt = 0;
	int timeout = 0;
	int available = 0;
	uint64_t current = 0;

	current = rte_rdtsc();
	for( ; i < htbl->bucketSize; i++)
	{
		if( htbl->bucket[i].status == STATUS_USE )
		{
			cnt++;
			if( htbl->ops.assessFunc )
			{
				htbl->ops.assessFunc(htbl->bucket[i].value);
			}
			if( htbl->bucket[i].timeout < current )
			{
				timeout++;
			}
		}
		else if( htbl->bucket[i].status == STATUS_AVAILABLE )
		{
			available++;
		}
	}

	printf("Hash Insert Failed:%d. Collision:%d\n", rte_atomic32_read(&(htbl->st.failed)), rte_atomic32_read(&(htbl->st.collision)));
	printf("Hash capacity:%d. Current cnt:%d. Expired cnt:%d.\n", htbl->capacity, cnt, timeout);
	printf("Hash total usage rate:%f\n", (double)cnt/(double)htbl->bucketSize);
	printf("Hash usage rate:%f\n", (double)(cnt-timeout)/(double)htbl->bucketSize);
	printf("Hash memory usage:%d bytes\n", htbl->st.totalMem);
}


