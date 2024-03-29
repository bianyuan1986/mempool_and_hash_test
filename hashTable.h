/*
 *
 *  Author: baohuren
 *  Email: baohuren@tencent.com
 *  Data: 2019-8-23
 *
 *
 *
 * This implementation implement a hash table. And the node is self-expired. 
 *
 *
 */

#ifndef _HASHTABLE_H_
#define _HASHTABLE_H_

#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

#define RET_FAILED -1
#define RET_NEW 0
#define RET_OCCUPY 1

enum HASH_STRATEGY
{
	HASH_STRATEGY_SELF_EXPIRED,
	HASH_STRATEGY_LRU,
	HASH_STRATEGY_MAX
};

struct hashTable;
/*check weather two keys are identical*/
typedef int (*fpCompare)(void *key1, void *key2);
/*calculate the hash info of data and stored in key*/
typedef int (*fpHash)(void *data, int dLen, void *key);
typedef void* (*fpMalloc)(size_t size);
typedef void (*fpFree)(void *ptr, int len);
typedef void (*fpAssess)(void *data);
/*copy the content of key from src to dst*/
typedef void (*fpAssignK)(void *src, void *dst);
/*copy the content of value from src to dst*/
typedef void (*fpAssignV)(void *src, void *dst);
/*update the node content when find a node in hash table*/
typedef void (*fpUpdateV)(void *v, void *userData);

struct UpdateCallBack
{
	fpUpdateV update;
	void *userData;
};

struct HashTableOps
{
	fpCompare cmp;
	fpHash hash;
	fpMalloc mallocFunc;
	fpFree freeFunc;
	fpAssignK assignKey;
	fpAssignV assignValue;
	fpAssess assessFunc;
};

struct HashNodeCopy
{
	uint64_t expired;
	void *value;
};

/*
 * @Create hash table and init
 *
 * @param
 *  capacity: the maximum count number that can be stored in the hash table
 *  mode: specify different expiration-strategy
 *  ops: user-defined interface 
 *
 * @return
 *  The hash table create by this function
 */
struct hashTable *hash_table_create(unsigned int capacity, enum HASH_STRATEGY mode, struct HashTableOps *ops);
/*
 * @Insert node into hash table
 *
 * @param
 *  htbl: hash table to be insert 
 *  data: the data used for calculating hash value
 *  dLen: data length
 *  key: the key info stored in hash table, it can be any of type
 *  value: the value struct stored in hash table, it can be any of type
 *  timeout: the time when the node expire
 *
 * @return
 *  RET_FAILED: Insert failed 
 *  RET_NEW: Insert a new node
 *  RET_OCCUPY: Copy the content of the current node to an existed node in hash table, the current node can be release
 */
int hash_table_insert(struct hashTable *htbl, void *data, int dLen, void *key, void *value, uint64_t timeout);
/*
 * @search node in hash table and maybe update the node 
 *
 * @param
 *  htbl: hash table to be searched for
 *  data: the data used for calculating hash value
 *  dLen: data length
 *  key: calculate the hash info of data and stored in key struct which is used for searching in hash table
 *  copy: a copy of the value of the target node and the time when the node expire.
 *  callback: update the value when find the node in hash table. The operation is protected by write-lock
 *
 * @return
 *  NULL: not find
 *  Not NULL: the pointer to the node
 */
void *hash_table_find(struct hashTable *htbl, void *data, int dLen, void *key, void *copy, struct UpdateCallBack *callback);

/*
 * @update node in hash table
 *
 * @param
 *  htbl: hash table to be searched for
 *  data: the data used for calculating hash value
 *  dLen: data length
 *  key: calculate the hash info of data and stored in key struct which is used for searching in hash table
 *  callback: update the value when find the node in hash table. 
 *
 * @return
 *  0: success 
 *  -1: failed 
 */
int hash_table_update(struct hashTable *htbl, void *data, int dLen, void *key, struct UpdateCallBack *callback);

void hash_table_destroy(struct hashTable *htbl);
void hash_table_assess(struct hashTable *htbl);

#ifdef __cplusplus
}
#endif

#endif

