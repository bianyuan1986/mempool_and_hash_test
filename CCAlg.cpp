#include "CCAlg.h"
#include "CWAFProcApp.h"
#include "mempool.h"
#include "hashTable.h"
#include "hash.h"
#include <rte_jhash.h>
#include <sys/mman.h>
#include <errno.h>


extern uint64_t g_ullOneSecondCycle;
extern __thread struct lcore_conf *t_qconf;

typedef int (*verifyResultCheck)(void);

struct HashTableOps g_stAlgHtblOps =
{
	.cmp = compare,
	.hash = hash,
	.mallocFunc = rte_malloc_wrap,
	.freeFunc = rte_free_wrap,
	.assignKey = assign_key,
	.assignValue = assign_value,
	.assessFunc = assess_node,
};

void *rte_malloc_wrap(size_t size)
{
	void *addr = NULL;

	//addr = rte_malloc(NULL, size, 64);
	addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
	if( addr == MAP_FAILED )
	{
		PERR("Mmap() failed due to:%s\n", strerror(errno));
		return NULL;
	}

	return addr;
}

void rte_free_wrap(void *addr, int len)
{
	if( addr )
	{
		//rte_free(addr);
		munmap(addr, len);
	}
}

void assess_node(void *data)
{
	struct value *v = NULL;
	static int trustCount = 0;
	static int untrustCount = 0;

	v = (struct value*)data;
	switch(v->status)
	{
		case NODE_STATUS_TRUST:
			trustCount++;
			break;
		case NODE_STATUS_UNTRUST:
			untrustCount++;
			break;
		default:
			break;
	}

	PERR("Trust node count:%d. Untrust node count:%d\n", untrustCount, trustCount);
}

void assign_key(void *src, void *dst)
{
	struct key *s = (struct key*)src;
	struct key *d = (struct key*)dst;

	if( s && d )
	{
		d->hashKey = s->hashKey;
		d->verifyKey = s->verifyKey;
	}
}

void assign_value(void *src, void *dst)
{
	struct value *s = (struct value*)src;
	struct value *d = (struct value*)dst;

	if( s && d )
	{
		d->status = s->status;
		d->algorithm = s->algorithm;
		d->count = s->count;
	}
}

int compare(void *key1, void *key2)
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

int hash(void *data, int dLen, void *key)
{
	struct key *k = NULL;

	if( !data || !key )
	{
		return -1;
	}
	k = (struct key *)key;

	k->hashKey = rte_jhash(data, dLen, 0);
	k->verifyKey = BKDRHash((char*)data);

	return k->hashKey;
}

void update_value(void *v, void *userData)
{
	struct value *s = (struct value*)userData;
	struct value *d = (struct value*)v;

	if( s && d )
	{
		d->status = s->status;
		d->algorithm = s->algorithm;
		d->count = s->count;
	}
}

static int CalculateCookieKey()
{
    uint32_t ulSrcipCrc = CRC32_INIT_VAL;
    char *pReqHost = GetHttpDataFieldPtr(HTTP_HOST);
    uint32_t ulReqHostLen = GetHttpDataFieldLen(HTTP_HOST);
    char *pReqUA = GetHttpDataFieldPtr(HTTP_USER_AGENT);
    uint32_t ulReqUALen = GetHttpDataFieldLen(HTTP_USER_AGENT);

    ulSrcipCrc = rte_hash_crc_4byte(t_qconf->pPkt->stSipUsed.get_hash_value(), ulSrcipCrc);
	if( pReqHost && ulReqHostLen )
	{
		ulSrcipCrc = rte_hash_crc( pReqHost, ulReqHostLen, ulSrcipCrc);
	}
	if( pReqUA && ulReqUALen )
	{
		ulSrcipCrc = rte_hash_crc( pReqUA, ulReqUALen, ulSrcipCrc);
	}

	return ulSrcipCrc;
}

static int CaptchaResultCheck(void)
{
    char *pReqParam = GetHttpDataFieldPtr(HTTP_PARAM);
    uint32_t ulReqParamLen = GetHttpDataFieldLen(HTTP_PARAM);
	char *pStart = NULL;
	int i = 0;
	char sigValue[MAX_SIG_NUM] = {0};
	char tmp = 0;
	int ret = 0;
	tTicketData stData;

	if( !pReqParam || !ulReqParamLen )
	{
		return VERIFY_FAILED;
	}

	tmp = pReqParam[ulReqParamLen];
	pReqParam[ulReqParamLen] = '\0';
	pStart = strstr(pReqParam, CC_CAPTCHA_WORD);
	if( !pStart )
	{
		pReqParam[ulReqParamLen] = tmp;
		return VERIFY_FAILED;
	}
	pStart += CC_CAPTCHA_WORD_LEN;
	for( ; i < MAX_SIG_NUM-1; i++)
	{
		if( (pStart[i] == '&') || (pStart[i] == ' ') )
		{
			break;
		}
		sigValue[i] = pStart[i];
	}
	ret = DecryptTicket(sigValue, CC_CAPTCHA_KEY, stData);
	if( (ret != CAPSIG_RET_CODE_OK) ||
			(time(NULL) - stData.ulTime > 30) )
	{
		pReqParam[ulReqParamLen] = tmp;
		return VERIFY_FAILED;
	}

	pReqParam[ulReqParamLen] = tmp;
	return VERIFY_SUCCESS;
}

static int CookieResultCheck(void)
{
    char *pReqCookie = GetHttpDataFieldPtr(HTTP_COOKIE);
    uint32_t ulReqCookieLen = GetHttpDataFieldLen(HTTP_COOKIE);
	char cookieKey[64] = {0};
	char tmp = 0;
	int ulSrcipCrc = 0;

	if( !pReqCookie || !ulReqCookieLen )
	{
		return VERIFY_FAILED;
	}

	ulSrcipCrc = CalculateCookieKey();
	snprintf(cookieKey, 64, "%s%u", GET_COOKIES_VERIFY_ARG, ulSrcipCrc);
	tmp = pReqCookie[ulReqCookieLen];
	pReqCookie[ulReqCookieLen] = '\0';
	if( strstr(pReqCookie, cookieKey) )
	{
		pReqCookie[ulReqCookieLen] = tmp;
		return VERIFY_SUCCESS;
	}

	pReqCookie[ulReqCookieLen] = tmp;
	return VERIFY_FAILED;
}

verifyResultCheck resultCheck[ALG_TYPE_MAX] = { CaptchaResultCheck, CookieResultCheck, NULL, NULL, NULL	};

int HttpEncode(char *url, int dLen, int max)
{
	int i = 0;

	while( i < dLen )
	{
		i++;
	}

	return 0;
}

inline int GetCount(int number)
{
	int count = 0;

	while( number > 0 )
	{
		number /= 10;
		count++;
	}

	return count;
}

int ConstructResponse(int methodType)
{
#define URL_BUF_LEN 3*1024
#define HTTPS_SCHEMA "https://"
#define HTTPS_SCHEMA_LEN 8
#define HTTP_SCHEMA "http://"
#define HTTP_SCHEMA_LEN 7
#define HTTP_HEADER_TRAILER_LEN 4
    char *pReqHost = GetHttpDataHostPtr();
    uint32_t ulReqHostLen = GetHttpDataHostLen();
    char *pReqParam = GetHttpDataFieldPtr(HTTP_PARAM);
    uint32_t ulReqParamLen = GetHttpDataFieldLen(HTTP_PARAM);
    char *pReqCookie = GetHttpDataFieldPtr(HTTP_COOKIE);
    uint32_t ulReqCookieLen = GetHttpDataFieldLen(HTTP_COOKIE);
    char *pReqCgi = GetHttpDataFieldPtr(HTTP_CGI);
    uint32_t ulReqCgiLen = GetHttpDataFieldLen(HTTP_CGI);

	static char urlBuf[URL_BUF_LEN] = {0};
	static unsigned int captchaLen = strlen(CC_CAPTCHA_JS);
	static unsigned int headerFirstLen = strlen(CC_CAPTCHA_SIG_RESPONSE_HEAD_FIRST);
	static unsigned int headerSecondLen = strlen(CC_CAPTCHA_SIG_RESPONSE_HEAD_SECOND);
	static unsigned int redirectFirstLen = strlen(POST_302_RESPONSE_HEAD_FIRST);
	static unsigned int redirectSecondLen = strlen(POST_302_RESPONSE_HEAD_SECOND);
	char *pBuf = urlBuf;
	int bufLen = URL_BUF_LEN;
	int copy = 0;
	unsigned int headerLen = 0;
	unsigned int bodyLen = 0;
	int ret = 0;

	/*'?' between cgi and parameter*/
	copy = HTTP_SCHEMA_LEN + ulReqHostLen + ulReqCgiLen + ulReqParamLen + 1;
	while( copy >= bufLen )
	{
		bufLen *= 2;
	}
	if( bufLen != URL_BUF_LEN )
	{
		pBuf = (char*)malloc(copy*3);
		bufLen = copy*3;
	}
	copy = 0;

	/* http:// or https:// */
	memcpy(pBuf, HTTP_SCHEMA, HTTP_SCHEMA_LEN);
	copy += HTTP_SCHEMA_LEN;
	memcpy(pBuf+copy, pReqHost, ulReqHostLen);
	copy += ulReqHostLen;
	memcpy(pBuf+copy, pReqCgi, ulReqCgiLen);
	copy += ulReqCgiLen;
	if( pReqParam && ulReqParamLen )
	{
		pBuf[copy++] = '?';
		memcpy(pBuf+copy, pReqParam, ulReqParamLen);
		copy += ulReqParamLen;
	}
	pBuf[copy] = '\0';
	/* if http encode is necessary */
	//HttpEncode(pBuf, copy, bufLen);

	switch(methodType)
	{
		case HTTP_HDR_GET:
		{
			int count = 0;
			bodyLen = captchaLen - 4 + strlen(pBuf);
			count = GetCount(bodyLen);
			headerLen = headerFirstLen + headerSecondLen + count + HTTP_HEADER_TRAILER_LEN;
			if( bodyLen + headerLen >= MAX_HTTP_BODY_BUF_LEN )
			{
				return ERR_SENDBACK_BY_CAPTCHA;
			}
			ret = snprintf((char*)t_qconf->pPkt->pAppData, headerLen+1, "%s%s%u\r\n\r\n", CC_CAPTCHA_SIG_RESPONSE_HEAD_FIRST, CC_CAPTCHA_SIG_RESPONSE_HEAD_SECOND, bodyLen);
			ret = snprintf((char*)t_qconf->pPkt->pAppData+ret, bodyLen+1, CC_CAPTCHA_JS, pBuf);
			t_qconf->pPkt->ulAppLen = headerLen + bodyLen;
			ret = DEFEND_RET_ACCEPT; 
			break;
		}

		case HTTP_HDR_HEAD:
		case HTTP_HDR_POST:
		case HTTP_HDR_PUT:
		case HTTP_HDR_DELETE:
		case HTTP_HDR_CONNECT:
		case HTTP_HDR_OPTIONS:
		case HTTP_HDR_TRACE:
		{
			uint32_t ulSrcipCrc = CalculateCookieKey();
			char cookieKey[32] = {0};
			snprintf(cookieKey, 32, "%u\r\n\r\n", ulSrcipCrc);
			headerLen = redirectFirstLen + strlen(pBuf) + redirectSecondLen + strlen(cookieKey); 
			if( headerLen > HTTP_RSP_LEN - 1 )
			{
				return ERR_SENDBACK_BY_307;
			}
			snprintf((char*)t_qconf->pPkt->pAppData, headerLen+1, "%s%s%s%s", POST_302_RESPONSE_HEAD_FIRST, pBuf, POST_302_RESPONSE_HEAD_SECOND, cookieKey);
			t_qconf->pPkt->ulAppLen = headerLen;
			ret = DEFEND_RET_ACCEPT; 
			break;
		}
		default:
			PERR("Unsupported method!\n");
			break;
	}

	if( pBuf != urlBuf )
	{
		free(pBuf);
	}
	return ret;
}

int DefendAlgSendBack(struct AlgParam *param)
{
#define HOST_LEN_MAX 128
#define USER_AGENT_LEN_MAX 512
#define REPEAT_MAX 5
	int ret = VERIFY_BEGIN;
	char *pReqHost = GetHttpDataHostPtr();
	uint32_t ulReqHostLen = GetHttpDataHostLen();
	char *pReqUA = GetHttpDataFieldPtr(HTTP_USER_AGENT);
	uint32_t ulReqUALen = GetHttpDataFieldLen(HTTP_USER_AGENT);
	char dataBuf[HOST_LEN_MAX+64+USER_AGENT_LEN_MAX] = {0};
	const char *client_ip = NULL;
	unsigned int copy = 0;
	struct hashTable *htbl = NULL;
	struct Mempool *mp = NULL;
	struct CCVerifyNode *obj = NULL;
	struct value *v = NULL;
	int update = 1;
	struct HashNodeCopy cp;
	int result = 0;
	static struct UpdateCallBack fUpdate = { update_value, NULL};

	uint32_t methodType = GetHttpDataPtr()->m_ulBigHttpType;
	htbl = t_qconf->htblAlg;
	mp = t_qconf->mpAlg;

	if( !param || !htbl || !mp )
	{
		return VERIFY_FAILED;
	}

	if( pReqHost && (ulReqHostLen > 0) )
	{
		ulReqHostLen = ulReqHostLen>HOST_LEN_MAX?HOST_LEN_MAX:ulReqHostLen;
		memcpy(dataBuf, pReqHost, ulReqHostLen);
		copy += ulReqHostLen;
	}
	if( pReqUA && (ulReqUALen > 0) )
	{
		ulReqUALen = ulReqUALen>USER_AGENT_LEN_MAX?USER_AGENT_LEN_MAX:ulReqUALen;
		memcpy(dataBuf+copy, pReqUA, ulReqUALen);
		copy += ulReqUALen;
	}
	client_ip = t_qconf->pPkt->stSip.to_str();
	strcpy(dataBuf+copy, client_ip);
	copy += strlen(client_ip);

	obj = (CCVerifyNode*)mempool_get_object(mp);
	if( !obj )
	{
		return VERIFY_FAILED;
	}
	cp.value = &(obj->v);
	if( (v = (struct value*)hash_table_find(htbl, dataBuf, copy, (void*)&(obj->k), (void*)&cp, NULL)) != NULL )
	{
		v = (struct value*)cp.value;
		if( v->status == NODE_STATUS_TRUST )
		{
			if( param->expired <= cp.expired )
			{
				update = 0;
			}
			ret = VERIFY_SUCCESS;
			goto DONE;
		}
		else if( v->status == NODE_STATUS_UNTRUST )
		{
			if( param->expired <= cp.expired )
			{
				update = 0;
			}
			ret = VERIFY_FAILED;
			goto DONE;
		}
		/*check the http request */
		int verifyResult = 0;
		int algorithm = v->algorithm;
		if( (algorithm >= ALG_TYPE_MAX) || (algorithm < 0)
				|| (resultCheck[algorithm] == NULL) )
		{
			PERR("Unsupported algorithm!\n");
			ret = VERIFY_FAILED;
			goto DONE;
		}
		verifyResult = resultCheck[algorithm]();

		if( verifyResult == VERIFY_FAILED )
		{
			if( algorithm == ALG_TYPE_CAPTCHA )
			{
				if( v->count > REPEAT_MAX )
				{
					ret = VERIFY_FAILED;
					goto DONE;
				}
				else
				{
					v->count++;
					ConstructResponse(methodType);
				}
				ret = VERIFY_REPEAT;
				goto DONE;
			}
			v->status = NODE_STATUS_UNTRUST;
			ret = VERIFY_FAILED;
		}
		else
		{
			v->status = NODE_STATUS_TRUST;
			ret = VERIFY_SUCCESS;
		}
	}
	else
	{
		obj->v.status = NODE_STATUS_INIT;
		obj->v.algorithm = (methodType==HTTP_HDR_GET?ALG_TYPE_CAPTCHA:ALG_TYPE_HTTP_COOKIE);
		obj->v.count = 0;
		ConstructResponse(methodType);
		result = hash_table_insert(htbl, dataBuf, copy, (void*)&(obj->k), (void*)&(obj->v), param->expired);
		if( result == RET_OCCUPY )
		{
			mempool_put_object(mp, obj);
		}

		return VERIFY_BEGIN;
	}

DONE:
	if( v && update )
	{
		fUpdate.userData = (void*)v;
		hash_table_update(htbl, dataBuf, copy, (void*)&(obj->k), &fUpdate);
	}

	return ret;
}

