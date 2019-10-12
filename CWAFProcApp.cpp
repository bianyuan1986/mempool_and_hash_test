#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>

bool CWAFProcApp::InitMemStart(CConfig *pConfig)
{
	if (app_defend_defend != 0)
	{
		uint32_t uiMaxElemCount = ALG_HASH_TABLE_SIZE / app_defend_defend + 1000;
		struct hashTable *htblAlg = hash_table_create(ALG_HASH_TABLE_SIZE, HASH_STRATEGY_SELF_EXPIRED, &g_stAlgHtblOps);
		for (uint32_t i = 0; i < MS_MAX_LCORE; i++)
		{
			struct lcore_conf *lconf = &(g_serverApp.lcoreConf[i]);

			lconf->htblAlg = htblAlg;
			lconf->mpAlg = mempool_create(sizeof(struct CCVerifyNode), uiMaxElemCount, rte_malloc_wrap, rte_free_wrap);
			if ((!lconf->htblAlg) || (!lconf->mpAlg))
			{
				return false;
			}
		}
	}
		
    return true;
}

