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

typedef int (*fpInsert)(struct hashTable *htbl, void *data, int dLen, void *key, void *value, uint64_t timeout);
typedef void* (*fpSearch)(struct hashTable *htbl, void *data, int dLen, void *key, void *copy, struct UpdateCallBack *callback);

struct HashInterface
{
	fpInsert insert;
	fpSearch search;
};

struct hashTable
{
	struct ListElem *bucket;
	unsigned int bucketSize;
	unsigned int capacity;

	enum HASH_STRATEGY mode;
	int probeStep;
	int factor;

	struct HashInterface *inf;
	struct HashTableOps ops;
	struct HashTableStatis st;
};

static void *HashDefaultMalloc(size_t size)
{
	return malloc(size);
}

static void HashDefaultFree(void *ptr, int len)
{
	free(ptr);
}

static int Init(struct hashTable *htbl, int size)
{
	int i = 0;

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

static int InsertElemSelfExpired(struct hashTable *htbl, void *data, int dLen, void *key, void *value, uint64_t timeout)
{
	int idx = 0;
	struct ListElem *elem = NULL;
	uint64_t currentTime = 0;
	int count = 0;
	int ret = RET_FAILED;
	uint64_t expired = 0;

	expired = timeout;
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

static void *FindElemSelfExpired(struct hashTable *htbl, void *data, int dLen, void *key, void *copy, struct UpdateCallBack *callback)
{
	int idx = 0;
	struct ListElem *elem = NULL;
	int count = 0;
	int find = 0;
	uint64_t currentTime = 0;
	struct HashNodeCopy *cp = NULL;

	idx = htbl->ops.hash(data, dLen, key)%htbl->bucketSize;
	elem = &htbl->bucket[idx];
	currentTime = rte_rdtsc();
	cp = (struct HashNodeCopy*)copy;

	while( count <= htbl->probeStep )
	{
		rte_rwlock_read_lock(&elem->rwlock);
		if( (elem->status == STATUS_USE) && 
				(htbl->ops.cmp(elem->key, key) == 1) &&
				(currentTime < elem->timeout) )
		{
			find = 1;
			if( cp && cp->value )
			{
				htbl->ops.assignValue(elem->value, cp->value);
				cp->expired = elem->timeout;
			}
			rte_rwlock_read_unlock(&elem->rwlock);
			break;
		}
		rte_rwlock_read_unlock(&elem->rwlock);

		elem++;
		count++;
	}

	if( callback && callback->update && find )
	{
		rte_rwlock_write_lock(&elem->rwlock);
		callback->update(elem->value, callback->userData);	
		rte_rwlock_write_unlock(&elem->rwlock);
	}
	return (find?elem:NULL);
}

static int InsertElemLRU(struct hashTable *htbl, void *data, int dLen, void *key, void *value, uint64_t timeout)
{
	int idx = 0;
	struct ListElem *elem = NULL;
	uint64_t currentTime = 0;
	int count = 0;
	int ret = RET_FAILED;
	struct ListElem *oldest = NULL;

	idx = htbl->ops.hash(data, dLen, key)%htbl->bucketSize;
	elem = &htbl->bucket[idx];
	oldest = elem;

	while( count < htbl->probeStep )
	{
		rte_rwlock_write_lock(&elem->rwlock);
		if( elem->status == STATUS_AVAILABLE )
		{
			elem->key = key;
			elem->value = value;
			elem->timeout = timeout;
			elem->status = STATUS_USE;
			ret = RET_NEW;
			rte_rwlock_write_unlock(&elem->rwlock);
			break;
		}

		if( (elem->status == STATUS_USE) && htbl->ops.cmp(elem->key, key) )
		{
			htbl->ops.assignKey(key, elem->key);
			htbl->ops.assignValue(value, elem->value);
			elem->timeout = timeout;
			ret = RET_OCCUPY;
			rte_rwlock_write_unlock(&elem->rwlock);
			break;
		}

		if( elem->timeout < oldest->timeout )
		{
			oldest = elem;
		}
		rte_rwlock_write_unlock(&elem->rwlock);

		elem++;
		count++;
		rte_atomic32_inc(&htbl->st.collision);
	}

	if( ret == RET_FAILED )
	{
		rte_rwlock_write_lock(&oldest->rwlock);
		htbl->ops.assignKey(key, oldest->key);
		htbl->ops.assignValue(value, oldest->value);
		rte_rwlock_write_unlock(&oldest->rwlock);
		ret = RET_OCCUPY;
	}

	return ret;
}

static void *FindElemLRU(struct hashTable *htbl, void *data, int dLen, void *key, void *copy, struct UpdateCallBack *callback)
{
	int idx = 0;
	struct ListElem *elem = NULL;
	int count = 0;
	int find = 0;
	uint64_t currentTime = 0;
	struct HashNodeCopy *cp = NULL;

	idx = htbl->ops.hash(data, dLen, key)%htbl->bucketSize;
	elem = &htbl->bucket[idx];
	currentTime = rte_rdtsc();
	cp = (struct HashNodeCopy*)copy;

	while( count <= htbl->probeStep )
	{
		rte_rwlock_read_lock(&elem->rwlock);
		if( (elem->status == STATUS_USE) && 
				(htbl->ops.cmp(elem->key, key) == 1) )
		{
			find = 1;
			if( cp && cp->value )
			{
				htbl->ops.assignValue(elem->value, cp->value);
				cp->expired = elem->timeout;
			}
			elem->timeout = currentTime;
			rte_rwlock_read_unlock(&elem->rwlock);
			break;
		}
		rte_rwlock_read_unlock(&elem->rwlock);

		elem++;
		count++;
	}

	if( callback && callback->update && find )
	{
		rte_rwlock_write_lock(&elem->rwlock);
		callback->update(elem->value, callback->userData);	
		rte_rwlock_write_unlock(&elem->rwlock);
	}
	return (find?elem:NULL);
}

struct HashInterface gHashInf[HASH_STRATEGY_MAX] =
{
	[HASH_STRATEGY_SELF_EXPIRED] = 
	{
		.insert = InsertElemSelfExpired,
		.search = FindElemSelfExpired
	},

	[HASH_STRATEGY_LRU] = 
	{
		.insert = InsertElemLRU,
		.search = FindElemLRU
	}
};

void *hash_table_find(struct hashTable *htbl, void *data, int dLen, void *key, void *copy, struct UpdateCallBack *callback)
{
	struct ListElem *elem = NULL;
	if( !htbl || !key || !dLen )
	{
		return NULL;
	}

	elem = htbl->inf->search(htbl, data, dLen, key, copy, callback);
	return elem?elem->value:NULL;
}

int hash_table_update(struct hashTable *htbl, void *data, int dLen, void *key, struct UpdateCallBack *callback)
{
	struct ListElem *elem = NULL;
	if( !htbl || !key || !dLen )
	{
		return -1;
	}

	elem = htbl->inf->search(htbl, data, dLen, key, NULL, callback);
	return elem?0:-1;
}

int hash_table_insert(struct hashTable *htbl, void *data, int dLen, void *key, void *value, uint64_t timeout)
{
	if( !htbl || !key || !value || !dLen )
	{
		return -1;
	}

	return htbl->inf->insert(htbl, data, dLen, key, value, timeout);
}

struct hashTable *hash_table_create(unsigned int capacity, enum HASH_STRATEGY mode, struct HashTableOps *ops)
{
	struct hashTable *htbl = NULL;
	int ret = 0;

	if( !ops->hash || !ops->cmp || !ops->assignKey || !ops->assignValue )
	{
		printf("hash/cmp/assignKey/assignValue interface must be provided!\n");
		goto FAILED;
	}
	if( mode >= HASH_STRATEGY_MAX )
	{
		printf("Unsupported mode!\n");
		goto FAILED;
	}

	if( !ops->mallocFunc )
	{
		ops->mallocFunc = HashDefaultMalloc;
	}
	if( !ops->freeFunc )
	{
		ops->freeFunc = HashDefaultFree;
	}
	htbl = ops->mallocFunc(sizeof(struct hashTable));

	if( !htbl )
	{
		printf("Malloc Hash Table failed\n");
		goto FAILED;
	}
	htbl->factor = 3;
	htbl->probeStep = PROBE_COUNT;
	htbl->mode = mode;
	htbl->ops = *ops;
	htbl->inf = &gHashInf[mode];

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

    htbl->ops.freeFunc(htbl->bucket, sizeof(struct ListElem)*htbl->bucketSize);	
	htbl->ops.freeFunc(htbl, sizeof(*htbl));
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


