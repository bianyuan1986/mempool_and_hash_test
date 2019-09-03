#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <rte_atomic.h>
#include <rte_cycles.h>

#include "hashTable.h"

#define PROBE_COUNT 5

#define STATUS_AVAILABLE 0
#define STATUS_INIT 1
#define STATUS_USE 2


struct ListElem
{
	unsigned int status;
	uint64_t timeout;

	void *key;
	void *value;
};

struct HashTableOps
{
	fpCompare cmp;
	fpHash hash;
	mallocPtr mallocFunc;
	freePtr freeFunc;
};

struct HashTableStatis
{
	rte_atomic32_t failed;
	rte_atomic32_t collision;
	unsigned int totalMem;
};

struct HashTable
{
	struct ListElem *bucket;
	unsigned int bucketSize;
	unsigned int capacity;
	unsigned int kLen;
	unsigned int vLen;

	int probeStep;
	int factor;
	struct HashTableOps ops;
	struct HashTableStatis st;
};

static int Init(struct HashTable *htbl, int size)
{
	htbl->capacity = size;
	htbl->bucketSize = htbl->factor*size;
	htbl->bucket = htbl->ops.mallocFunc(sizeof(struct ListElem)*htbl->bucketSize);
	if( !htbl->bucket )
	{
		printf("Malloc bucket failed.\n");
		return -1;
	}
	memset(htbl->bucket, 0x00, sizeof(struct ListElem)*htbl->bucketSize);
	htbl->st.totalMem = sizeof(*htbl) + sizeof(struct ListElem)*htbl->bucketSize;

	return 0;
}

static int InsertElem(struct HashTable *htbl, void *data, void *key, void *value, uint64_t timeout)
{
	int idx = 0;
	struct ListElem *elem = NULL;
	uint64_t currentTime = 0;
	int count = 0;
	int ret = RET_FAILED;
	uint64_t expired = 0;

	currentTime = rte_rdtsc();
	expired = currentTime + timeout;
	idx = htbl->ops.hash(data, key)%htbl->bucketSize;
	elem = &htbl->bucket[idx];
	if( rte_atomic32_cmpset(&elem->status, STATUS_AVAILABLE, STATUS_INIT) )
	{
		elem->key = key;
		elem->value = value;
		elem->timeout = expired;
		rte_atomic32_cmpset(&elem->status, STATUS_INIT, STATUS_USE);
		ret = RET_NEW;

		goto SUCCESS;
	}

	if( (elem->status == STATUS_USE) && (currentTime >= elem->timeout || htbl->ops.cmp(elem->key, key)) )
	{
		memcpy(elem->key, key, htbl->kLen);
		memcpy(elem->value, value, htbl->vLen);
		elem->timeout = expired;
		ret = RET_OCCUPY;

		goto SUCCESS;
	}

	rte_atomic32_inc(&htbl->st.collision);
	while( count < htbl->probeStep )
	{
		elem++;
		if( rte_atomic32_cmpset(&elem->status, STATUS_AVAILABLE, STATUS_INIT) )
		{
			elem->key = key;
			elem->value = value;
			elem->timeout = expired;
			rte_atomic32_cmpset(&elem->status, STATUS_INIT, STATUS_USE);
			ret = RET_NEW;
			break;
		}

		if( (elem->status == STATUS_USE) && (currentTime >= elem->timeout || htbl->ops.cmp(elem->key, key)) )
		{
			memcpy(elem->key, key, htbl->kLen);
			memcpy(elem->value, value, htbl->vLen);
			elem->timeout = expired;
			ret = RET_OCCUPY;
			break;
		}

		count++;
	}

	if( ret == RET_FAILED )
	{
		rte_atomic32_inc(&htbl->st.failed);
		goto FAILED;
	}

SUCCESS:
	return ret;

FAILED:
	return ret;
}

static void *FindElem(struct HashTable *htbl, void *data, void *key)
{
	int idx = 0;
	struct ListElem *elem = NULL;
	int count = 0;
	int find = 0;
	uint64_t currentTime = 0;

	idx = htbl->ops.hash(data, key)%htbl->bucketSize;
	elem = &htbl->bucket[idx];
	currentTime = rte_rdtsc();
	if( (elem->status == STATUS_USE) && 
			(htbl->ops.cmp(elem->key, key) == 1) &&
			(currentTime < elem->timeout) )
	{
		return elem;
	}

	while( count < htbl->probeStep )
	{
		elem++;
		if( (elem->status == STATUS_USE) && 
				(htbl->ops.cmp(elem->key, key) == 1) &&
				(currentTime < elem->timeout) )
		{
			find = 1;
			break;
		}
		count++;
	}

	return (find?elem:NULL);
}

void *hash_table_find(struct HashTable *htbl, void *data, void *key)
{
	struct ListElem *elem = NULL;
	if( !htbl || !key )
	{
		return NULL;
	}

	elem = FindElem(htbl, data, key);
	return elem?elem->value:NULL;
}

int hash_table_insert(struct HashTable *htbl, void *data, void *key, void *value, uint64_t timeout)
{
	if( !htbl || !key || !value)
	{
		return -1;
	}

	return InsertElem(htbl, data, key, value, timeout);
}

struct HashTable *hash_table_create(unsigned int capacity, unsigned int kLen, unsigned int vLen, fpCompare cmp, fpHash hash, mallocPtr mallocFunc, freePtr freeFunc)
{
	struct HashTable *htbl = NULL;
	int ret = 0;

	if( mallocFunc )
	{
		htbl = (struct HashTable*)mallocFunc(sizeof(struct HashTable));
	}
	else
	{
		htbl = (struct HashTable*)malloc(sizeof(struct HashTable));
	}

	if( !htbl )
	{
		printf("Malloc Hash Table failed\n");
		goto FAILED;
	}
	htbl->kLen = kLen;
	htbl->vLen = vLen;
	htbl->factor = 3;
	htbl->probeStep = PROBE_COUNT;
	htbl->ops.cmp = cmp;
	htbl->ops.hash = hash;
	htbl->ops.mallocFunc = mallocFunc?mallocFunc:malloc;
	htbl->ops.freeFunc = freeFunc?freeFunc:free;

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

void hash_table_destroy(struct HashTable *htbl)
{
	if( !htbl )
	{
		return;
	}

    htbl->ops.freeFunc(htbl->bucket);	
	htbl->ops.freeFunc(htbl);
}

void hash_table_assess(struct HashTable *htbl)
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


