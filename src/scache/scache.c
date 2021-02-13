///         @file  scache.c
///        @brief  SmartCache Redis module
///         @date  30/05/2017
///       @author  François Cerbelle (Fanfan), francois@cerbelle.net
///    @copyright  Copyright (c) 2017, François Cerbelle
///
/// @todo Refactor SCacheGetMeta_RedisCommand and SCacheGetValue_RedisCommand
/// @todo Store values in internal data structure
/// @todo TTL management : Create a regular redis key with TTL for value expiration,
/// subscribe to notifications in a background thread to delete values from internal
/// structure
/// @todo TTL management : Create a sorted set with TTL converted in absolute timestamp
/// as scores to achive a lazy expiration on values (check at read, and eventually
/// background thread checking periodically)
/// @todo Store connection details in Hash for persistence and replication, but keep
/// connection handler in an internal data structure per shard
/// @todo Return resultsets as complex values { {Metas} {Record1Values, Record2Values, Record3Values} }
/// @todo DB abstraction layer with dynamically loadable modules (dl)
/// @todo Cluster awareness (CE/EE)
/// @todo Add Log entries for DEBUG, INFO, NOTICE levels
/// @todo: use a connection pool
///
/// @todo 
/// @bug 
/// @version
/// @since
///
/// Implements a smart and autonomous cache in a Redis module. Your application
/// can query the cache for a value, / if the value is not in the cache, it will
/// block the client, spawn a thread to fetch the data from the / underlying data
/// source (MySQL in our case) while making Redis available for other queries
/// (because a MySQL / fetch can be long relative to other Redis queries), get the
/// value, store / it with an expiration time (TTL) and / send it back to the
/// client.
///
///  @internal
///      Compiler  gcc
/// Last modified  2017-05-31 18:29
///  Organization  Cerbelle.net
///       Company  Home
///
///  This source code is released for free distribution under the terms of the
///  GNU General Public License as published by the Free Software Foundation.
///

#define REDISMODULE_EXPERIMENTAL_API
#include "../redismodule.h"
#include <mysql/mysql.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

typedef struct CacheDetails_s {
    char* cachename;
    uint16_t ttl;
    char* dbhost;
    uint16_t dbport;
    char* dbname;
    char* dbuser;
    char* dbpass;
    MYSQL* dbhandle;
    struct CacheDetails_s* next;
} CacheDetails;

CacheDetails* CacheList = NULL;

void RedisModule_ReplyWithCacheDetails(RedisModuleCtx *ctx, CacheDetails* cur) {
    RedisModule_ReplyWithArray(ctx, 8);
    RedisModule_ReplyWithStringBuffer(ctx, cur->cachename, strlen(cur->cachename));
    RedisModule_ReplyWithLongLong(ctx,cur->ttl);
    RedisModule_ReplyWithStringBuffer(ctx, cur->dbhost, strlen(cur->dbhost));
    RedisModule_ReplyWithLongLong(ctx,cur->dbport);
    RedisModule_ReplyWithStringBuffer(ctx, cur->dbname, strlen(cur->dbname));
    RedisModule_ReplyWithStringBuffer(ctx, cur->dbuser, strlen(cur->dbuser));
    //	RedisModule_ReplyWithStringBuffer(ctx, cur->dbpass, strlen(cur->dbpass));
    RedisModule_ReplyWithStringBuffer(ctx, "xxxxxxxx",8);
    RedisModule_ReplyWithLongLong(ctx, (long long)cur->dbhandle);
}

/* Reply callback for blocking command SCACHE.CREATE */
int SCacheCreate_Reply(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    CacheDetails *privdata=RedisModule_GetBlockedClientPrivateData(ctx);

    if ( NULL == privdata->dbhandle) {
        RedisModule_ReplyWithError(ctx,"ERR cannot connect to DB");
        return REDISMODULE_OK;
    }

    // FreeData will be automatically called after this callback to release the
    // memory, we need to create and store a copy of privdata.
    CacheDetails *cur=(CacheDetails*)RedisModule_Alloc(sizeof(CacheDetails));
    cur->cachename = RedisModule_Strdup(privdata->cachename);
    cur->ttl = privdata->ttl;
    cur->dbhost = RedisModule_Strdup(privdata->dbhost);
    cur->dbport = privdata->dbport;
    cur->dbname = RedisModule_Strdup(privdata->dbname);
    cur->dbuser = RedisModule_Strdup(privdata->dbuser);
    cur->dbpass = RedisModule_Strdup(privdata->dbpass);
    cur->dbhandle = privdata->dbhandle;

    // CRITICAL SECTION BEGIN : should be in a mutex
    cur->next = CacheList;
    CacheList = cur;
    // CRITICAL SECTION END : should be in a mutex

    RedisModule_ReplyWithCacheDetails(ctx,cur);
    return REDISMODULE_OK;
}

/* Timeout callback for SCACHE.CREATE command */
int SCacheCreate_Timeout(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    return RedisModule_ReplyWithSimpleString(ctx,"Request timedout");
}

/* Private data freeing callback for SCACHE.CREATE command. */
void SCacheCreate_FreeData(RedisModuleCtx *ctx, void *privdata) {
    REDISMODULE_NOT_USED(ctx);
    CacheDetails *cur=privdata;
    RedisModule_Free(cur->cachename);
    RedisModule_Free(cur->dbhost);
    RedisModule_Free(cur->dbname);
    RedisModule_Free(cur->dbuser);
    RedisModule_Free(cur->dbpass);
    RedisModule_Free(cur);
}

/* The thread entry point that actually executes the blocking part
 * of the  SCACHE.CREATEi command. */
void *SCacheCreate_ThreadMain(void *arg) {
    void **targ = arg;
    RedisModuleBlockedClient *bc = targ[0];
    CacheDetails *cur = targ[1];
    RedisModule_Free(targ);

    MYSQL* mysql;
    // Init MySQL options structure with auto-reconnect in case of lost connection
    mysql = mysql_init(NULL);
    my_bool reconnect=1;
    mysql_options(mysql,MYSQL_OPT_RECONNECT,&reconnect);
    // Open the connection to MySQL
    cur-> dbhandle = mysql_real_connect(mysql, cur->dbhost, cur->dbuser, cur->dbpass, cur->dbname, cur->dbport, 0, 0);

    RedisModule_UnblockClient(bc,cur);
    return NULL;
}


// Creates a new cache configuration and stores it in a hash
// SCACHE.CREATE <CacheName> <DefaultTTL> <dbhost> <dbport> <dbname> <dbuser> <dbpass>
int SCacheCreate_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // REDISMODULE_NOT_USED(argv);
    //REDISMODULE_NOT_USED(argc);
    if (argc != 8 ) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);
    CacheDetails *cur = CacheList;
    size_t len;
    const char* cachename = RedisModule_StringPtrLen(argv[1], &len);

    // Search for already defined cache
    while ((cur)&&(strcmp(cachename,cur->cachename)))
        cur=cur->next;
    if (cur) {
        RedisModule_ReplyWithError(ctx,"ERR Cache already defined, please delete before.");
        return REDISMODULE_OK;
    } else 
        cur = (CacheDetails*)RedisModule_Alloc(sizeof(CacheDetails));

    // Initialize cachename from the arguments in the structure
    if (!(cur->cachename = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the cachename.");
    }
    memcpy(cur->cachename, cachename, len+1);

    // Initialize ttl from the arguments in the structure
    if (0 == (cur->ttl=atoi(RedisModule_StringPtrLen(argv[2], &len)))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR invalid default TTL");
    }

    // Initialize dbhost from the arguments in the structure
    const char* dbhost = RedisModule_StringPtrLen(argv[3], &len);
    if (!(cur->dbhost = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the dbhost.");
    }
    memcpy(cur->dbhost, dbhost, len+1);

    // Initialize dbport from the arguments in the structure
    if (0 == (cur->dbport=atoi(RedisModule_StringPtrLen(argv[4], &len)))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR invalid dbport number");
    }

    // Initialize dbname from the arguments in the structure
    const char* dbname = RedisModule_StringPtrLen(argv[5], &len);
    if (!(cur->dbname = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the dbname.");
    }
    memcpy(cur->dbname, dbname, len+1);

    // Initialize dbuser from the arguments in the structure
    const char* dbuser = RedisModule_StringPtrLen(argv[6], &len);
    if (!(cur->dbuser = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the dbuser.");
    }
    memcpy(cur->dbuser, dbuser, len+1);

    // Initialize dbpass from the arguments in the structure
    const char* dbpass = RedisModule_StringPtrLen(argv[7], &len);
    if (!(cur->dbpass = (char*)RedisModule_Alloc(len+1))) {
        RedisModule_Free(cur);
        return RedisModule_ReplyWithError(ctx,"ERR unable to allocate memory to store the dbpass.");
    }
    memcpy(cur->dbpass, dbpass, len+1);

    // Blocks the client connection with callbacks
    RedisModuleBlockedClient *bc = RedisModule_BlockClient(ctx,
            SCacheCreate_Reply,
            SCacheCreate_Timeout,
            SCacheCreate_FreeData,
            1000); // timeout in milliseconds

    // Initialize the thread arguments structure
    void **targ = RedisModule_Alloc(sizeof(void*)*2);
    targ[0] = bc;
    targ[1] = cur;

    // Create the background thread
    pthread_t tid;
    if (pthread_create(&tid,NULL,SCacheCreate_ThreadMain,targ) != 0) {
        RedisModule_AbortBlock(bc);
        return RedisModule_ReplyWithError(ctx,"-ERR Can't start thread");
    }

    // Return to the main redis loop (unblock it)
    return REDISMODULE_OK;
}

// Lists all configured caches 
// O(n) n=nb caches
int SCacheList_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (argc != 1) return RedisModule_WrongArity(ctx);

    uint16_t count=0;
    CacheDetails* cur=CacheList;

    RedisModule_ReplyWithArray(ctx, REDISMODULE_POSTPONED_ARRAY_LEN);
    while (cur) {
        count++;
        RedisModule_ReplyWithStringBuffer(ctx, cur->cachename, strlen(cur->cachename));
        cur=cur->next;
    }
    RedisModule_ReplySetArrayLength(ctx,count);

    return REDISMODULE_OK;
}

// Returns a cache configuration details
// O(n/2) n = nb caches
int SCacheInfo_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (argc != 2 ) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);

    CacheDetails* cur=CacheList;
    size_t len;
    const char* cachename = RedisModule_StringPtrLen(argv[1], &len);

    while ((cur)&&(strcmp(cachename,cur->cachename)))
        cur=cur->next;

    if (cur)
        RedisModule_ReplyWithCacheDetails(ctx,cur);
    else 
        RedisModule_ReplyWithError(ctx,"ERR Cache definition not found.");

    return REDISMODULE_OK;
}

// Tests a cache's underlying database connection
// O(n/2) n = nb caches
int SCacheTest_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (argc != 2 ) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);

    CacheDetails* cur=CacheList;
    size_t len;
    const char* cachename = RedisModule_StringPtrLen(argv[1], &len);

    while ((cur)&&(strcmp(cachename,cur->cachename)))
        cur=cur->next;

    if (cur) {
        if (mysql_ping(cur->dbhandle))
            RedisModule_ReplyWithError(ctx,"ERR Connection failed.");
        else
            RedisModule_ReplyWithLongLong(ctx,1);
    } else 
        RedisModule_ReplyWithError(ctx,"ERR Cache definition not found.");

    return REDISMODULE_OK;
}

// Flushes all the values from a cache
int SCacheFlush_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    RedisModule_ReplyWithError(ctx,"ERR Not yet implemented.");
    return REDISMODULE_OK;
}

// Flushes and delete a cache
// O(n/2) + Flush n = nb caches
int SCacheDelete_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (argc != 2 ) return RedisModule_WrongArity(ctx);

    // Flushes the cache first
//    SCacheFlush_RedisCommand(ctx, argv, argc);

    RedisModule_AutoMemory(ctx);

    CacheDetails* cur=CacheList;
    CacheDetails* tmp=NULL;
    size_t len;
    const char* cachename = RedisModule_StringPtrLen(argv[1], &len);

    if ((CacheList)&&!strcmp(cachename,CacheList->cachename)) {
        // First cache in the list
        tmp=CacheList;
        CacheList = CacheList->next;
        mysql_close(tmp->dbhandle);
        RedisModule_Free(tmp);
        RedisModule_ReplyWithLongLong(ctx,1);
    } else {
        // Not the first cache in the list, search in the list
        while ((cur)&&(cur->next)&&(strcmp(cachename,cur->next->cachename)))
            cur=cur->next;

        if (cur->next) {
            // Cache definition found
            tmp=cur->next;
            cur->next = cur->next->next;
            mysql_close(tmp->dbhandle);
            RedisModule_Free(tmp);
            RedisModule_ReplyWithLongLong(ctx,1);
        } else {
            // Cache definition not in the list
            RedisModule_ReplyWithError(ctx,"ERR Cache definition not found.");
        }
    }
    return REDISMODULE_OK;
}

// Queries the underlying DB to populate the keys (names, types and values) in the cache with TTL 
int SCachePopulate(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (argc != 3) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);

    size_t len;
    const char* cachename = RedisModule_StringPtrLen(argv[1], &len);

    CacheDetails* cur=CacheList;
    while ((cur)&&strcmp(cur->cachename,cachename))
        cur=cur->next;

    if (NULL == cur) {
        RedisModule_ReplyWithError(ctx,"ERR cache definition not found.");
        return REDISMODULE_OK;
    }

    // Execute the underlying query
    const char* query = RedisModule_StringPtrLen(argv[2], &len);
    int state = mysql_query(cur->dbhandle, query);

    if( state != 0 ) {
        // Underlying error
        const char *error = mysql_error(cur->dbhandle);
        RedisModule_ReplyWithError(ctx,error);
        return REDISMODULE_OK;
    } else {
        // Fetch resultset
        MYSQL_RES* result = mysql_store_result(cur->dbhandle);
        if( result == (MYSQL_RES *)NULL ) {
            const char *error = mysql_error(cur->dbhandle);
            RedisModule_ReplyWithError(ctx,error);
        }

        // Build value and meta keynames cachename::query::value/cachename::query::meta
        RedisModuleString *valuekey = RedisModule_CreateStringFromString(ctx,argv[1]);
        RedisModule_StringAppendBuffer(ctx,valuekey,"::",2);
        RedisModule_StringAppendBuffer(ctx,valuekey,query,len);
        RedisModuleString *metakey = RedisModule_CreateStringFromString(ctx,valuekey);
        RedisModule_StringAppendBuffer(ctx,valuekey,"::value",7);
        RedisModule_StringAppendBuffer(ctx,metakey,"::meta",6);

        // Cache results meta
        unsigned int num_fields = mysql_num_fields(result);
        MYSQL_FIELD *fields = mysql_fetch_fields(result);
        RedisModuleString* tmp=NULL;
        unsigned int i=0;
        char* name;
        char* type;
        while (i < num_fields) {
            name = RedisModule_Strdup(fields[i].name);
            switch (fields[i].type) {
                case MYSQL_TYPE_TINY: type = RedisModule_Strdup("MYSQL_TYPE_TINY"); break;
                case MYSQL_TYPE_SHORT: type = RedisModule_Strdup("MYSQL_TYPE_SHORT"); break;
                case MYSQL_TYPE_LONG: type = RedisModule_Strdup("MYSQL_TYPE_LONG"); break;
                case MYSQL_TYPE_INT24: type = RedisModule_Strdup("MYSQL_TYPE_INT24"); break;
                case MYSQL_TYPE_LONGLONG: type = RedisModule_Strdup("MYSQL_TYPE_LONGLONG"); break;
                case MYSQL_TYPE_DECIMAL: type = RedisModule_Strdup("MYSQL_TYPE_DECIMAL"); break;
                case MYSQL_TYPE_NEWDECIMAL: type = RedisModule_Strdup("MYSQL_TYPE_NEWDECIMAL"); break;
                case MYSQL_TYPE_FLOAT: type = RedisModule_Strdup("MYSQL_TYPE_FLOAT"); break;
                case MYSQL_TYPE_DOUBLE: type = RedisModule_Strdup("MYSQL_TYPE_DOUBLE"); break;
                case MYSQL_TYPE_BIT: type = RedisModule_Strdup("MYSQL_TYPE_BIT"); break;
                case MYSQL_TYPE_TIMESTAMP: type = RedisModule_Strdup("MYSQL_TYPE_TIMESTAMP"); break;
                case MYSQL_TYPE_DATE: type = RedisModule_Strdup("MYSQL_TYPE_DATE"); break;
                case MYSQL_TYPE_TIME: type = RedisModule_Strdup("MYSQL_TYPE_TIME"); break;
                case MYSQL_TYPE_DATETIME: type = RedisModule_Strdup("MYSQL_TYPE_DATETIME"); break;
                case MYSQL_TYPE_YEAR: type = RedisModule_Strdup("MYSQL_TYPE_YEAR"); break;
                case MYSQL_TYPE_STRING: type = RedisModule_Strdup("MYSQL_TYPE_STRING"); break;
                case MYSQL_TYPE_VAR_STRING: type = RedisModule_Strdup("MYSQL_TYPE_VAR_STRING"); break;
                case MYSQL_TYPE_BLOB: type = RedisModule_Strdup("MYSQL_TYPE_BLOB"); break;
                case MYSQL_TYPE_SET: type = RedisModule_Strdup("MYSQL_TYPE_SET"); break;
                case MYSQL_TYPE_ENUM: type = RedisModule_Strdup("MYSQL_TYPE_ENUM"); break;
                case MYSQL_TYPE_GEOMETRY: type = RedisModule_Strdup("MYSQL_TYPE_GEOMETRY"); break;
                case MYSQL_TYPE_NULL: type = RedisModule_Strdup("MYSQL_TYPE_NULL"); break;
                default: type = RedisModule_Strdup("UNKNOWN"); break;
            }
            tmp = RedisModule_CreateString(ctx,name,strlen(name));
            RedisModule_StringAppendBuffer(ctx,tmp,"|",1);
            RedisModule_StringAppendBuffer(ctx,tmp,type,strlen(type));
            RedisModule_Call(ctx,"RPUSH","ss",metakey,tmp);
            RedisModule_FreeString(ctx,tmp);
            RedisModule_Free(name);
            RedisModule_Free(type);
            i++;
        }

        // Cache result values
        MYSQL_ROW row;
        char* rowstr;
        char* value;
        uint16_t count=0;
        while (NULL != (row = mysql_fetch_row(result))) {
            rowstr = RedisModule_Strdup("");
            for(i = 0; i < num_fields; i++) {
                value = (char*)(row[i] ? row[i] : "NULL");
                rowstr = (char*)RedisModule_Realloc(rowstr,strlen(rowstr)+1+strlen(value)+1);
                strcat(rowstr,"|");
                strcat(rowstr,value);
            }
            RedisModule_Call(ctx,"RPUSH","sc",valuekey,&rowstr[1],strlen(&rowstr[1]));
            RedisModule_Free(rowstr);
            count++;
        }

        // Set expiration time (TTL) on the meta and value keys
        RedisModule_Call(ctx,"EXPIRE","sl",metakey,cur->ttl);
        RedisModule_Call(ctx,"EXPIRE","sl",valuekey,cur->ttl);

        return REDISMODULE_OK;
    }
}

// Gets values from the cache (eventually fetching them from underlying database)
int SCacheGetValue_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argc);
    REDISMODULE_NOT_USED(argv);
    if (argc != 3) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);

    // Build the keys cachename::query::value
    RedisModuleString *valuekey = RedisModule_CreateStringFromString(ctx,argv[1]);
    RedisModule_StringAppendBuffer(ctx,valuekey,"::",2);
    size_t len;
    const char* query = RedisModule_StringPtrLen(argv[2], &len);
    RedisModule_StringAppendBuffer(ctx,valuekey,query,len);
    RedisModule_StringAppendBuffer(ctx,valuekey,"::value",7);

    // Try to get the resultset from the built valuekey in the cache
    RedisModuleCallReply *reply = RedisModule_Call(ctx,"LRANGE","scc",valuekey,"0","-1");
    if (0 == RedisModule_CallReplyLength(reply)) {
        // Not found : Populate it from the underlying DB and retry
        // Forget the empty result
        RedisModule_FreeCallReply(reply);
        // Populate the cache using the underlying database
        SCachePopulate(ctx,argv,argc);
        // Retry cache query
        reply = RedisModule_Call(ctx,"LRANGE","scc",valuekey,"0","-1");
    }

    RedisModule_ReplyWithCallReply(ctx,reply);
    return REDISMODULE_OK;
}

// Gets resultset's meta data from the cache (eventually fetching them from underlying database)
int SCacheGetMeta_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argc);
    REDISMODULE_NOT_USED(argv);
    if (argc != 3) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);

    // Build the meta key cachename::query::meta
    RedisModuleString *valuekey = RedisModule_CreateStringFromString(ctx,argv[1]);
    RedisModule_StringAppendBuffer(ctx,valuekey,"::",2);
    size_t len;
    const char* query = RedisModule_StringPtrLen(argv[2], &len);
    RedisModule_StringAppendBuffer(ctx,valuekey,query,len);
    RedisModule_StringAppendBuffer(ctx,valuekey,"::meta",6);

    // Try to fetch the meta from the built meta key, from the cache
    RedisModuleCallReply *reply = RedisModule_Call(ctx,"LRANGE","scc",valuekey,"0","-1");
    if (0 == RedisModule_CallReplyLength(reply)) {
        // Not found : populate the cache from the underlying DB and retry
        // Forget the empty result
        RedisModule_FreeCallReply(reply);
        // Populate the cache using the underlying database
        SCachePopulate(ctx,argv,argc);
        // Retry cache query
        reply = RedisModule_Call(ctx,"LRANGE","scc",valuekey,"0","-1");
    }

    RedisModule_ReplyWithCallReply(ctx,reply);
    return REDISMODULE_OK;
}

// Module initialization
int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (RedisModule_Init(ctx,"scache",1,REDISMODULE_APIVER_1)
            == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.create",
                SCacheCreate_RedisCommand,"write deny-oom no-monitor fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.list",
                SCacheList_RedisCommand,"readonly deny-oom fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.info",
                SCacheInfo_RedisCommand,"readonly deny-oom fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.test",
                SCacheTest_RedisCommand,"readonly deny-oom fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.flush",
                SCacheFlush_RedisCommand,"readonly deny-oom fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.delete",
                SCacheDelete_RedisCommand,"readonly deny-oom fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.getvalue",
                SCacheGetValue_RedisCommand,"write deny-oom fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"scache.getmeta",
                SCacheGetMeta_RedisCommand,"write deny-oom fast",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
