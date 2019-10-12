#ifndef __CCALG_H__
#define __CCALG_H__

#include <rte_jhash.h>
#include <rte_malloc.h>

enum ALG_TYPE
{
    ALG_TYPE_CAPTCHA = 0,
    ALG_TYPE_HTTP_COOKIE,
    ALG_TYPE_GET_REDIRECT_200,
    ALG_TYPE_GET_REDIRECT_302,
    ALG_TYPE_POST_REDIRECT,
	ALG_TYPE_MAX
};

#define VERIFY_BEGIN 0
#define VERIFY_SUCCESS 1
#define VERIFY_FAILED 2
#define VERIFY_REPEAT 3

#define NODE_STATUS_INIT 1
#define NODE_STATUS_TRUST 2
#define NODE_STATUS_UNTRUST 3

#define ALG_HASH_TABLE_SIZE 12000000

struct AlgParam
{
	uint64_t expired;
};

struct key
{
	unsigned int hashKey;
	unsigned int verifyKey;
};

struct value
{
	unsigned int status;
	unsigned int algorithm;
	unsigned int count;
};

struct CCVerifyNode 
{
	struct key k;
	struct value v;
};

extern struct HashTableOps g_stAlgHtblOps;

void *rte_malloc_wrap(size_t size);
void rte_free_wrap(void *addr, int len);
void assess_node(void *data);
void assign_key(void *src, void *dst);
void assign_value(void *src, void *dst);
int compare(void *key1, void *key2);
int hash(void *data, int dLen, void *key);

#endif

