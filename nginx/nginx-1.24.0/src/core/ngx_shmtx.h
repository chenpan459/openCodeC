
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMTX_H_INCLUDED_
#define _NGX_SHMTX_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct {
    ngx_atomic_t   lock;
#if (NGX_HAVE_POSIX_SEM)
    ngx_atomic_t   wait;
#endif
} ngx_shmtx_sh_t;


typedef struct {
#if (NGX_HAVE_ATOMIC_OPS) //  如果定义了NGX_HAVE_ATOMIC_OPS，表示系统支持原子操作
    ngx_atomic_t  *lock; //  指向原子锁的指针
#if (NGX_HAVE_POSIX_SEM) //  如果定义了NGX_HAVE_POSIX_SEM，表示系统支持POSIX信号量
    ngx_atomic_t  *wait; //  指向等待计数器的指针，用于记录等待锁的线程数
    ngx_uint_t     semaphore; //  信号量值，用于控制访问资源的线程数
    sem_t          sem; //  POSIX信号量对象
#endif
#else
    ngx_fd_t       fd; //  如果系统不支持原子操作，使用文件描述符作为锁
    u_char        *name; //  锁的名称，通常用于标识锁
#endif
    ngx_uint_t     spin; //  自旋次数，用于自旋锁的实现
} ngx_shmtx_t;


ngx_int_t ngx_shmtx_create(ngx_shmtx_t *mtx, ngx_shmtx_sh_t *addr,
    u_char *name);
void ngx_shmtx_destroy(ngx_shmtx_t *mtx);
ngx_uint_t ngx_shmtx_trylock(ngx_shmtx_t *mtx);
void ngx_shmtx_lock(ngx_shmtx_t *mtx);
void ngx_shmtx_unlock(ngx_shmtx_t *mtx);
ngx_uint_t ngx_shmtx_force_unlock(ngx_shmtx_t *mtx, ngx_pid_t pid);


#endif /* _NGX_SHMTX_H_INCLUDED_ */
