
# find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
	SHOBJ_CFLAGS ?= -W -Wall -fno-common -g -ggdb -std=c99 -O2
	SHOBJ_LDFLAGS ?= -shared
else
	SHOBJ_CFLAGS ?= -W -Wall -dynamic -fno-common -g -ggdb -std=c99 -O2
	SHOBJ_LDFLAGS ?= -bundle -undefined dynamic_lookup
endif

MYSQL_CFLAGS =  -I/usr/include/mysql -DBIG_JOINS=1  -fno-strict-aliasing    -g -DNDEBUG
MYSQL_LIBS =  -L/usr/lib64/mysql/ -L/usr/lib/x86_64-linux-gnu -lmysqlclient -lpthread -lz -lm -ldl

.SUFFIXES: .c .so .xo .o

all: scache.so

.c.xo:
	$(CC) -I. $(CFLAGS) $(SHOBJ_CFLAGS) $(MYSQL_CFLAGS) -fPIC -c $< -o $@

scache.xo: ../redismodule.h

scache.so: scache.xo 
	$(LD) -o $@ $< $(SHOBJ_LDFLAGS) $(LIBS) $(MYSQL_LIBS) -lc

clean:
	rm -rf *.xo *.so
