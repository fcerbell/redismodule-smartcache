# redismodule-smartcache
Smart and autonomous cache in a redis module

# Summary

This modules implements a pass-through cache, or a proxy cache,
or a transparent cache.

# Longer description

It currently only connects to MySQL database, but can be easily
ported to any other database, SQL or NoSQL, to accelerate the
queries or to minimize the load on the underlying database.

The goal is to make the application simple, it only has to query
the cache and the cache will eventually query the underlying
database. No need to manage the cache and the DB connection at
the application level anymore. No need to link and use the DB
driver in the application anymore.

One of the usecase is to accelerate the queries and minimize the
latency, another usecase is to lower the load pressure on the
database (making more resources available for other tasks or
minimizing the costs).

# Commands

The module implements two sets of Redis commands. The first one
is used to manage the caches, whereas the second one is used to
query the caches.

## Cache management

The following commands are used to administrate the caches, some
kind of DDL for SCache.

### scache.create

scache.create

Defines a new cache and its underlying database
connection.  A connection is immediately openned to the
database and the creation will fail if the database is
not reachable.

**Arguments**
- *cachename* is the cache identifier. It has to be unique.
- *ttl* is the default Time To Live in seconds before value expiration
- *host* IP address or DNS name of the database server
- *port* TCP port of the database server (usually 3306)
- *user* login name to connect with to the database
- *password* password to connect to the database
- *schema* name of the database schema

**Return value**
If the connection test succeed, returns the cache configuration (without password), otherwise returns an error.

*Note*: This command does not appear in the `MONITOR` output to avoid displaying passwords.

### scache.list

Lists all the defined caches

**Arguments**
None

**Return value**
A simple list of available cachenames.

### scache.info

Gets information about one specific defined cache.

**Arguments**
*cachename* Name of the cache

**Return value**
If the cache exists, returns its configuration (without password), otherwise returns an error

### scache.test

Test the database connection of a specific cache (in case it broke after the cache creation).

**Arguments**
*cachename* Name of the cache

**Return value**
"1" if the test succeed, otherwise an error.

### scache.flush

Flush all the cached resultsets from a cache.

**Arguments**
*cachename* Name of the cache

**Return value**
Number of purged values

### scache.delete

Flush a cache and delete its definition

**Arguments**
*cachename* Name of the cache

**Return value**
Number of purged values

## Cache querying

These commands are used by the application to actually query the
cache, some kind of DML.

### scache.getvalue

Returns a resultset values from the cache, eventually fetching them automatically from the database.

**Arguments**
*cachename* Name of the cache
*query* Underlying database query string

**Return value**
A list of records, each of them is a pipe-separated column values

### scache.getmeta

Returns a resultset metadata from the cache, eventually fetching them automatically from the database.

**Arguments**
*cachename* Name of the cache
*query* Underlying database query string

**Return value**
A list of column name / column type, pipe-separated.

# Specifications

The module defines caches. Each cache is currently a MySQL
connection (host/port/user/password/schema) and a default TTL.
Once a cache is defined, it can be queried with SQL queries, if
it does not already have the resultset, it blocks the client and
execute the SQL query against MySQL in a thread (to avoid
blocking Redis and make the query asynchronous), store the
result set in Redis with a TTL. At the end, it returns the
resultset to the client.

The cache definition has to be stored in a Redis datastructure
to have the benefit of easy persistency and replication across a
cluster nodes, but the connection handle has to be stored in the
node memory, in internal datastructure as it is specific to a
single instance. We keep the connection handle to avoid
opening/closing connections and we use the auto-reconnect MySQL
feature to keep the connection always ready for queries.

The resultsets don't have to be replicated across the cluster
and don't have to be persisted, neither. Thus, we store them in
an internal datastructure. We try to leverave the Redis TTL to
expire our resultset. For each dataset, we create a key in a
Redis data structure, with a TTL. The module starts a thread
that subscribe to the notification channel to be notified when a
key expires and to delete the related resultset from internal
datastructures.

# Build instructions

## Prerequisites

Install mysqlclient development libraries.

```
sudo aptitude install libmysqlclient-dev
```

## Compile

```
cd src/scache
make
```

# Test

## Prerequisites

Install mysql server :

```
sudo aptitude install libmysqlclient-dev mysql-server
```

Connect to mysql server using the CLI client and the admin account :

```
mysql -u root
```

Create the test database, the test user and grant him all permissions on the database :

``` sql
create database redisdb;
create user redisuser@'%' identified by 'redispassword';
grant all privileges on redisdb.* to redisuser;
```

Create some test values

``` sql
use redisdb;
create table customer (id integer, Nom varchar(255), prenom varchar(255), `date de naissance` datetime);
insert into customer values (1,"Cerbelle","François","2017-05-26");
insert into customer values (2,"Carbonnel","Georges","1970-01-01");
insert into customer values (3,"Sanfilippo","Salvatore","1970-01-01");
```

## Testcase

You can use a second redis-cli instance with `MONITOR` and `tail -f /var/log/redis*.log`

Start Redis client :

```
redis-cli
```

Load the module :

```
module load /home/vagrant/DownloadCache/redismodule-smartcache/src/scache/scache.so
```

Enjoy:

```
scache.list
scache.create cache1 20 127.0.0.1 3306 redisdb redisuser redispassword
scache.create cache2 10 localhost 3306 redisdb redisuser redispassword
scache.create cache3 5 node4.vm 3306 redisdb redisuser redispassword
scache.list
scache.info cache2
scache.delete cache2
scache.list
scache.getvalue cache1 'select * from customer'
```
