#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_mempool.h>
#include <rte_cycles.h>

#include "mempool.h"
#include "hashTable.h"
#include "hash.h"

#define RTE_PROC_MAX 2
#define MEMPOOL_SIZE 1000000
#define ELEMENT_SIZE 40 //48,16

#define HASH_TABLE_SIZE 12000000

typedef int (*pFunc)(void*);

struct lcore_conf 
{
	struct HashTable *htbl;

	unsigned int poolSize;
	unsigned int elementSize;

	unsigned int ipStart;
	pid_t pid;
};

struct key
{
	unsigned int hashKey;
	unsigned int verifyKey;
};

struct value
{
	unsigned int status;
	unsigned int count;
};

struct userData
{
	struct key k;
	struct value v;
};

uint64_t g_cycles_per_second = 0;
struct lcore_conf g_lcore_conf[RTE_MAX_LCORE];

static int compare(void *key1, void *key2)
{
	struct key *src = NULL;
	struct key *dst = NULL;

	if( !key1 || !key2 )
	{
		return 0;
	}

	src = (struct key*)key1;
	dst = (struct key*)key2;

	if( (src->hashKey == dst->hashKey) && (src->verifyKey == dst->verifyKey) )
	{
		return 1;
	}

	return 0;
}

static int hash(void *data, void *key)
{
	struct key *k = NULL;

	if( !data || !key )
	{
		return -1;
	}
	k = (struct key *)key;

	k->hashKey = BKDRHash((char*)data);
	k->verifyKey = ELFHash((char*)data);

	return k->hashKey;
}

static void *rte_malloc_wrap(size_t size)
{
	void *addr = NULL;

	addr = rte_malloc(NULL, size, 64);

	return addr;
}

static void rte_free_wrap(void *addr)
{
	if( addr )
	{
		rte_free(addr);
	}
}

static int primary_process(__attribute__((unused))void *args)
{
	while( 1 )
	{
		sleep(5);
	}

	return 0;
}

static void generate_random_data(char *data, int len)
{
#undef BUF_LEN
#define BUF_LEN 128 
	FILE *f = NULL;
	char host[BUF_LEN] = {0};
	int copy = 0;

	if( data )
	{
		return;
	}
	f = popen("cat /dev/urandom|base64|head -n 1", "r");
	if( f == NULL )
	{
		printf("popen failed\n");
		return;
	}
	fgets(host, BUF_LEN, f);
	copy = strlen((const char*)host);
	copy = (copy > len-1)? (len-1):copy;
	memcpy(data, host, copy);
	if( pclose(f) < 0 )
	{
		printf("pclose failed:%s\n", strerror(errno));
	}
}

static int secondary_process(__attribute__((unused))void *args)
{
#undef BUF_LEN
#define BUF_LEN 8*1024
#define UA_SAMPLE "Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/33.0.1750.27 Safari/537.36 TST(Tencent_Security_Team) 80dc"
	struct HashTable *htbl = NULL;
	struct Mempool *mp = NULL;
	struct userData *obj = NULL;

	struct lcore_conf *lconf = NULL;
	unsigned int lcore_id = -1;

	static char ipHostUa[BUF_LEN] = {0};
	unsigned int uaLen = 0;
	unsigned int ip = 0;
	unsigned int i = 0;
	char buf[INET_ADDRSTRLEN] = {0};
	int copy = 0;
	int ret = 0;
	pid_t pid = 0;
	int insertCnt = 0;
	int occupyCnt = 0;

	lcore_id = rte_lcore_id();
	lconf = &g_lcore_conf[lcore_id];
	ip = lconf->ipStart;
	uaLen = strlen(UA_SAMPLE);
	htbl = lconf->htbl;

	pid = fork();
	if( pid > 0 )
	{
		lconf->pid = pid;
		printf("lcore %d Fork child %d.\n", lcore_id, pid);
		return 0;
	}
	else if( pid < 0 )
	{
		printf("Fork failed!\n");
		return -1;
	}

	mp = mempool_create(lconf->elementSize, lconf->poolSize, rte_malloc_wrap, rte_free_wrap);

	memcpy(ipHostUa, UA_SAMPLE, uaLen);
	for( ; i < lconf->poolSize; i++)
	{
		obj = (struct userData*)mempool_get_object(mp);
		copy = uaLen;
		memset(buf, 0x00, INET_ADDRSTRLEN);
		if( inet_ntop(AF_INET, (const void*)&ip, buf, INET_ADDRSTRLEN) == NULL )
		{
			printf("Convert ip failed\n");
			continue;
		}
		memcpy(ipHostUa+uaLen, buf, strlen(buf));
		copy += strlen(buf);
		generate_random_data(ipHostUa+copy, BUF_LEN-copy);
		if( hash_table_find(htbl, ipHostUa, (void*)&(obj->k)) )
		{
			continue;
		}
		ret = hash_table_insert(htbl, (void*)ipHostUa, (void*)&(obj->k), (void*)&(obj->v), 10*60*g_cycles_per_second);
		switch(ret)
		{
			case RET_NEW:
				insertCnt++;
				break;
			case RET_OCCUPY:
				occupyCnt++;
				mempool_put_object(mp, obj);
				break;
			case RET_FAILED:
				break;
		}
		ip++;
	}

	printf("Insert Cnt:%d. Occupy Cnt:%d\n", insertCnt, occupyCnt);
	exit(0);
}

static void test_mempool(unsigned int cnt)
{
	struct timeval t1;
	struct timeval t2;
	void **array = NULL;
	int flags = 0;
	unsigned int i = 0;

	struct Mempool *mp = NULL;
	struct rte_mempool *mmp = NULL;

	array = malloc(sizeof(void*)*cnt);
	flags = MEMPOOL_F_SP_PUT|MEMPOOL_F_SC_GET; 
	mmp = rte_mempool_create("tiger_test", MEMPOOL_SIZE, ELEMENT_SIZE, 128, 0, NULL, NULL, NULL, NULL, rte_socket_id(), flags);
	if( !mmp )
	{
		printf("Can't create mempool!\n");
	}

	gettimeofday(&t1, NULL);
	mp = mempool_create(ELEMENT_SIZE, MEMPOOL_SIZE, rte_malloc_wrap, rte_free_wrap);
	gettimeofday(&t2, NULL);
	printf("Create Mempool Consume %ldus\n", (t2.tv_sec-t1.tv_sec)*1000000+(t2.tv_usec-t1.tv_usec));

	printf("---------------------------------------------------------\n");
	gettimeofday(&t1, NULL);
	for( i = 0; i < cnt; i++)
	{
		rte_mempool_get_bulk(mmp, &array[i], 1);
	}
	gettimeofday(&t2, NULL);
	printf("DPDK Mempool Get Object Consume %ldus\n", (t2.tv_sec-t1.tv_sec)*1000000+(t2.tv_usec-t1.tv_usec));

	gettimeofday(&t1, NULL);
	for( i = 0; i < cnt; i++)
	{
		rte_mempool_put_bulk(mmp, &array[i], 1);
	}
	gettimeofday(&t2, NULL);
	printf("DPDK Mempool Put Object Consume %ldus\n", (t2.tv_sec-t1.tv_sec)*1000000+(t2.tv_usec-t1.tv_usec));
	printf("---------------------------------------------------------\n");

	gettimeofday(&t1, NULL);
	for( i = 0; i < cnt; i++)
	{
		array[i] = mempool_get_object(mp);
	}
	gettimeofday(&t2, NULL);
	printf("Get Object Consume %ldus\n", (t2.tv_sec-t1.tv_sec)*1000000+(t2.tv_usec-t1.tv_usec));

	gettimeofday(&t1, NULL);
	for( i = 0; i < cnt; i++)
	{
		mempool_put_object(mp, array[i]);
	}
	gettimeofday(&t2, NULL);
	printf("Put Object Consume %ldus\n", (t2.tv_sec-t1.tv_sec)*1000000+(t2.tv_usec-t1.tv_usec));
	printf("---------------------------------------------------------\n");

	gettimeofday(&t1, NULL);
	for( i = 0; i < cnt; i++)
	{
		array[i] = malloc(ELEMENT_SIZE);
	}
	gettimeofday(&t2, NULL);
	printf("Malloc Object Consume %ldus\n", (t2.tv_sec-t1.tv_sec)*1000000+(t2.tv_usec-t1.tv_usec));

	gettimeofday(&t1, NULL);
	for( i = 0; i < cnt; i++)
	{
		free(array[i]);
	}
	gettimeofday(&t2, NULL);
	printf("Free Object Consume %ldus\n", (t2.tv_sec-t1.tv_sec)*1000000+(t2.tv_usec-t1.tv_usec));
	printf("---------------------------------------------------------\n");

	mempool_free(mp);
	rte_mempool_free(mmp);
}

static uint64_t get_cycles_per_second(void)
{
	uint64_t start_cycles = 0;
	uint64_t end_cycles = 0;

	start_cycles = rte_rdtsc();
	sleep(1);
	end_cycles = rte_rdtsc();

	return end_cycles-start_cycles;
}

int main(int argc, char *argv[])
{
	int cnt = 0;
	int ret = 0;

	unsigned int lcore_id = 0;
	struct rte_config *cfg = NULL;

	if( argc < 2 )
	{
		printf("Usage %s COUNT\n", argv[0]);
		return 0;
	}

	ret = rte_eal_init(argc, argv);
	if( ret < 0 )
	{
		printf("DPDK Init Failed!\n");
		return -1;
	}
	argc -= ret;
	argv += ret;

	g_cycles_per_second = get_cycles_per_second();
	cfg = rte_eal_get_configuration();
	switch(cfg->process_type )
	{
		case RTE_PROC_PRIMARY:
			cnt = atoi(argv[1]);
			test_mempool(cnt);
			/*rte_eal_mp_remote_launch( primary_process, NULL, SKIP_MASTER);*/
			primary_process(NULL);
			RTE_LCORE_FOREACH_SLAVE(lcore_id)
			{
				if( rte_eal_wait_lcore(lcore_id) < 0 )
				{
					printf("lcore %u failed!\n", lcore_id);
				}
			}
			break;

		case RTE_PROC_SECONDARY:
		{
			static struct HashTable *htbl = NULL;
			struct lcore_conf *lconf = NULL;
			int i = 0;
			int idx = 0;
			int ret = 0;

			htbl = hash_table_create( HASH_TABLE_SIZE, sizeof(struct key), sizeof(struct value), compare, hash, rte_malloc_wrap, rte_free_wrap);
			memset(g_lcore_conf, 0x00, sizeof(g_lcore_conf));
			for( ; i < RTE_MAX_LCORE; i++)
			{
				if( !rte_lcore_is_enabled(i) )
				{
					continue;
				}
				lconf = &g_lcore_conf[i];
				lconf->htbl = htbl;
				lconf->poolSize = HASH_TABLE_SIZE/(cfg->lcore_count-1);
				lconf->elementSize = sizeof(struct userData);
				idx = rte_lcore_index(i);
				lconf->ipStart = 0xc80a0a0a+idx*lconf->poolSize;
			}
			rte_eal_mp_remote_launch( secondary_process, NULL, SKIP_MASTER);

			RTE_LCORE_FOREACH_SLAVE(lcore_id)
			{
				if( rte_eal_wait_lcore(lcore_id) < 0 )
				{
					printf("lcore %u failed!\n", lcore_id);
				}
			}

			for( i = 0; i < RTE_MAX_LCORE; i++)
			{
				if( !rte_lcore_is_enabled(i) )
				{
					continue;
				}
				if( rte_get_master_lcore() == (unsigned int)i )
				{
					continue;
				}

				lconf = &g_lcore_conf[i];
				if( (ret = waitpid(lconf->pid, NULL, 0)) > 0 )
				{
					printf("Process %d finish!\n", ret);
				}
				else
				{
					printf("Wait failed:%d:%s!\n", ret, strerror(errno));
				}
			}
			hash_table_assess(htbl);
			break;
		}
		default:
			break;
	}

	return 0;
}

