///         @file  scache.c
///        @brief  Smart autonomous MySQL cache
///         @date  24/05/2017
///       @author  François Cerbelle (Fanfan), francois@cerbelle.net
///    @copyright  Copyright (c) 2017, François Cerbelle
///
/// @todo 
/// @bug 
/// @version
/// @since
///
/// Implements a smart and autonomous cache in a Redis module. Your application can query the cache for a value,
/// if the value is not in the cache, it will block the client, spawn a thread to fetch the data from the
/// underlying data source (MySQL in our case) while making Redis available for other queries (because a MySQL 
/// fetch can be long relative to other Redis queries), get the value, store / it with an expiration time (TTL) and
/// send it back to the client.
///
///  @internal
///      Compiler  gcc
/// Last modified  2017-05-24 16:15
///  Organization  Cerbelle.net
///       Company  Home
///
///  This source code is released for free distribution under the terms of the
///  GNU Affero General Public License as published by the Free Software Foundation.
///


#include "../redismodule.h"
#include <string.h> // memcpy
#include <stdlib.h> // atoi

/** Structure to store mySQL connection details */
typedef struct MysqlCredentials {
    char* cachename;
    char* dbhost;
    uint16_t dbport;
    char* dbname;
    char* dbuser;
    char* dbpass;
    uint16_t ttl;
    struct MysqlCredentials* next;
} MysqlCredentials_t;

MysqlCredentials_t *mysqlconnections=NULL;

void RedisModule_ReplyWithCacheDetails(RedisModuleCtx *ctx, MysqlCredentials_t *cur) {
    RedisModule_ReplyWithArray(ctx, 7);
    RedisModule_ReplyWithStringBuffer(ctx, cur->cachename, strlen(cur->cachename));
    RedisModule_ReplyWithStringBuffer(ctx, cur->dbhost, strlen(cur->dbhost));
    RedisModule_ReplyWithLongLong(ctx,cur->dbport);
    RedisModule_ReplyWithStringBuffer(ctx, cur->dbname, strlen(cur->dbname));
    RedisModule_ReplyWithStringBuffer(ctx, cur->dbuser, strlen(cur->dbuser));
    RedisModule_ReplyWithStringBuffer(ctx, cur->dbpass, strlen(cur->dbpass));
    RedisModule_ReplyWithLongLong(ctx,cur->ttl);
}

int SCacheCreate_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 8) return RedisModule_WrongArity(ctx);

    MysqlCredentials_t *cur = (MysqlCredentials_t*)RedisModule_Alloc(sizeof(MysqlCredentials_t));

    size_t len;

    const char* cachename = RedisModule_StringPtrLen(argv[1], &len);
    if (!(cur->cachename = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the cachename.");
    }
    memcpy(cur->cachename, cachename, len+1);

    const char* dbhost = RedisModule_StringPtrLen(argv[2], &len);
    if (!(cur->dbhost = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the dbhost.");
    }
    memcpy(cur->dbhost, dbhost, len+1);

    if (0 == (cur->dbport=atoi(RedisModule_StringPtrLen(argv[3], &len)))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR invalid dbport number");
    }

    const char* dbname = RedisModule_StringPtrLen(argv[4], &len);
    if (!(cur->dbname = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the dbname.");
    }
    memcpy(cur->dbname, dbname, len+1);

    const char* dbuser = RedisModule_StringPtrLen(argv[5], &len);
    if (!(cur->dbuser = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the dbuser.");
    }
    memcpy(cur->dbuser, dbuser, len+1);

    const char* dbpass = RedisModule_StringPtrLen(argv[6], &len);
    if (!(cur->dbpass = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the dbpass.");
    }
    memcpy(cur->dbpass, dbpass, len+1);

    if (0 == (cur->ttl=atoi(RedisModule_StringPtrLen(argv[7], &len)))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR invalid default TTL");
    }

    cur->next = mysqlconnections;
    mysqlconnections = cur;

    RedisModule_ReplyWithCacheDetails(ctx,cur);
    return REDISMODULE_OK;
}

int SCacheList_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    if (argc != 1) return RedisModule_WrongArity(ctx);

    uint16_t count=0;
    MysqlCredentials_t *cur=mysqlconnections;

    RedisModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);
    while (cur) {
        count++;
        RedisModule_ReplyWithCacheDetails(ctx,cur);
        cur=cur->next;
    }
    RedisModule_ReplySetArrayLength(ctx,count);
    return REDISMODULE_OK;
}

int SCacheGet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 3) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);

    RedisModuleString *key = RedisModule_CreateStringFromString(ctx,argv[1]);
    RedisModule_StringAppendBuffer(ctx,key,"::",2);
    size_t len;
    const char* query = RedisModule_StringPtrLen(argv[2], &len);
    RedisModule_StringAppendBuffer(ctx,key,query,len);

    RedisModuleCallReply *reply = RedisModule_Call(ctx,"LRANGE","scc",key,"0","-1");

    if (0 != RedisModule_CallReplyLength(reply)) {
        RedisModule_ReplyWithCallReply(ctx,reply);
        return REDISMODULE_OK;
    } else {
        MysqlCredentials_t *cur=mysqlconnections;

        const char* cachename = RedisModule_StringPtrLen(argv[1], &len);
        while ((cur)&&strcmp(cur->cachename,cachename)) {
            cur=cur->next;
        }

        if (NULL == cur) {
            RedisModule_ReplyWithError(ctx,"ERR cache does not exist");
            return REDISMODULE_OK;
        }

        // TODO: Query MySQL

unsigned short count=0;
RedisModule_Log(ctx, "notice", "%d",count++);
        RedisModule_Call(ctx,"RPUSH","scc",key,"Id,Nom,Prénom,Date de naissance");
RedisModule_Log(ctx, "notice", "%d",count++);
        RedisModule_Call(ctx,"RPUSH","scc",key,"INT,VARCHAR(255),VARCHAR(255),DATETIME");
RedisModule_Log(ctx, "notice", "%d",count++);
        RedisModule_Call(ctx,"RPUSH","scc",key,"1,Cerbelle,François,26/05/1975");
RedisModule_Log(ctx, "notice", "%d",count++);
        RedisModule_Call(ctx,"RPUSH","scc",key,"2,Carbonnel,Georges,01/01/1970");
        RedisModule_Call(ctx,"EXPIRE","sl",key,cur->ttl);
        RedisModule_FreeCallReply(reply);
        reply = RedisModule_Call(ctx,"LRANGE","scc",key,"0","-1");
        RedisModule_ReplyWithCallReply(ctx,reply);
        return REDISMODULE_OK;
    }
}

/* This function must be present on each Redis module. It is used in order to
 * register the commands into the Redis server. */
int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (RedisModule_Init(ctx,"scache",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.create",
        SCacheCreate_RedisCommand,"readonly deny-oom no-monitor fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.list",
        SCacheList_RedisCommand,"readonly deny-oom fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.get",
        SCacheGet_RedisCommand,"write deny-oom fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;


    // info key
    // flush key
    // delete key

    return REDISMODULE_OK;
}
