/*
 *
 *  Author: baohuren
 *  Email: baohuren@tencent.com
 *  Data: 2019-8-23
 *
 *
 *
 * This implementation implement a lru hash table. It will eliminate a node when fail to insert 
 * node into the table. And the node is self-expired. 
 *
 *
 */

#ifndef _HASHTABLE_H_
#define _HASHTABLE_H_

#ifdef __cplusplus__
extern "C" {
#endif

#define RET_FAILED -1
#define RET_NEW 0
#define RET_OCCUPY 1

struct HashTable;
typedef int (*fpCompare)(void *key1, void *key2);
typedef int (*fpHash)(void *data, void *key);
typedef void* (*mallocPtr)(size_t size);
typedef void (*freePtr)(void *ptr);


struct HashTable *hash_table_create(unsigned int capacity, unsigned int kLen, unsigned int vLen, fpCompare cmp, fpHash hash, mallocPtr mallocFunc, freePtr freeFunc);
int hash_table_insert(struct HashTable *htbl, void *data, void *key, void *value, uint64_t timeout);
void *hash_table_find(struct HashTable *htbl, void *data, void *key);
void hash_table_destroy(struct HashTable *htbl);
void hash_table_assess(struct HashTable *htbl);

#ifdef __cplusplus__
}
#endif

#endif

