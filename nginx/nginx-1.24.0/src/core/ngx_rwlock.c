
/*
 * Copyright (C) Ruslan Ermilov
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


#if (NGX_HAVE_ATOMIC_OPS)


#define NGX_RWLOCK_SPIN   2048
#define NGX_RWLOCK_WLOCK  ((ngx_atomic_uint_t) -1)


void
ngx_rwlock_wlock(ngx_atomic_t *lock)
{ //  函数声明：获取写锁。
    ngx_uint_t  i, n;

    for ( ;; ) { //  定义两个无符号整型变量i和n，用于自旋计数。

        if (*lock == 0 && ngx_atomic_cmp_set(lock, 0, NGX_RWLOCK_WLOCK)) { //  无限循环，直到成功获取写锁。
            return; //  如果锁的当前值为0（即锁未被占用），并且原子操作成功将锁的值从0设置为NGX_RWLOCK_WLOCK（写锁标志）。
        }

        if (ngx_ncpu > 1) { //  检查系统CPU数量是否大于1，如果是则进入多CPU处理逻辑

            for (n = 1; n < NGX_RWLOCK_SPIN; n <<= 1) { //  循环尝试获取写锁，最多尝试NGX_RWLOCK_SPIN次

                for (i = 0; i < n; i++) { //  内层循环，执行CPU暂停指令，次数为n
                    ngx_cpu_pause(); //  CPU暂停，减少CPU占用
                }

                if (*lock == 0 //  尝试获取写锁                 如果锁值为0且成功将锁值设置为NGX_RWLOCK_WLOCK，则获取写锁成功，退出函数
                    && ngx_atomic_cmp_set(lock, 0, NGX_RWLOCK_WLOCK))
                {
                    return; //  获取写锁成功，退出函数
                }
            }
        }

        ngx_sched_yield(); //  如果多CPU处理未成功获取写锁，则调用调度器让出CPU时间片
    }
}


void
ngx_rwlock_rlock(ngx_atomic_t *lock)
{
    ngx_uint_t         i, n;
    ngx_atomic_uint_t  readers; //  读取锁的读者数量

    for ( ;; ) { //  无限循环尝试获取读锁
        readers = *lock; //  获取当前锁的值

        if (readers != NGX_RWLOCK_WLOCK //  检查当前锁状态是否不是写锁，并且尝试将读锁计数器加1
            && ngx_atomic_cmp_set(lock, readers, readers + 1))
        {
            return;
        }

        if (ngx_ncpu > 1) { //  如果系统有多个CPU核心

            for (n = 1; n < NGX_RWLOCK_SPIN; n <<= 1) { //  自旋锁的尝试次数，从1开始，每次翻倍，直到达到NGX_RWLOCK_SPIN

                for (i = 0; i < n; i++) { //  在每次尝试中，进行n次CPU暂停操作，减少CPU消耗
                    ngx_cpu_pause();
                }

                readers = *lock; //  重新读取锁的当前状态

                if (readers != NGX_RWLOCK_WLOCK //  再次检查锁状态是否不是写锁，并且尝试将读锁计数器加1
                    && ngx_atomic_cmp_set(lock, readers, readers + 1))
                {
                    return;
                }
            }
        }

        ngx_sched_yield(); //  如果自旋锁尝试失败，则调用调度器让出CPU时间片，等待下一次调度
    }
}


void
ngx_rwlock_unlock(ngx_atomic_t *lock)
{
    if (*lock == NGX_RWLOCK_WLOCK) {
        (void) ngx_atomic_cmp_set(lock, NGX_RWLOCK_WLOCK, 0);
    } else {
        (void) ngx_atomic_fetch_add(lock, -1);
    }
}


void
ngx_rwlock_downgrade(ngx_atomic_t *lock)
{
    if (*lock == NGX_RWLOCK_WLOCK) {
        *lock = 1;
    }
}


#else

#if (NGX_HTTP_UPSTREAM_ZONE || NGX_STREAM_UPSTREAM_ZONE)

#error ngx_atomic_cmp_set() is not defined!

#endif

#endif
