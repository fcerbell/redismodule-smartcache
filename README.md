# redismodule-smartcache
Smart and autonomous cache in a redis module

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
