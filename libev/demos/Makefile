CC = gcc

# 自动查找所有头文件所在的目录
#INCLUDE_DIRS = $(shell find ../../src -type f -name '*.h' -exec dirname {} \; | sort -u)
#CFLAGS = -Wall $(foreach dir, $(INCLUDE_DIRS), -I$(dir))

#CFLAGS = -Wall -Wextra -I../../src
LDFLAGS = -lev -pthread -lrt -lm -ldl

TARGETS = ev1 ev_svr 

all: $(TARGETS)

# 生成 nv_tcp_cli 可执行文件
ev1: ev1.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# 生成 nv_tcp_srv 可执行文件
ev_svr: ev_svr.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)


clean:
	rm -f $(TARGETS)

.PHONY: all clean
