
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


#if (NGX_HAVE_ATOMIC_OPS)


static void ngx_shmtx_wakeup(ngx_shmtx_t *mtx); //  定义一个静态函数ngx_shmtx_wakeup，用于唤醒等待的线程


ngx_int_t
ngx_shmtx_create(ngx_shmtx_t *mtx, ngx_shmtx_sh_t *addr, u_char *name)
{
    mtx->lock = &addr->lock; //  将传入的共享内存地址的锁字段赋值给互斥锁结构体的锁字段

    if (mtx->spin == (ngx_uint_t) -1) { //  检查互斥锁的spin字段是否为-1，如果是，则直接返回成功
        return NGX_OK; //  返回成功状态码
    }

    mtx->spin = 2048; //  如果spin字段不是-1，则将其设置为2048，表示自旋次数

#if (NGX_HAVE_POSIX_SEM) //  如果系统支持POSIX信号量

    mtx->wait = &addr->wait; //  将传入的共享内存地址的wait字段赋值给互斥锁结构体的wait字段

/*这里 &mtx->sem 是信号量对象。
第二个参数 1 表示该信号量可以在多个进程间共享。
第三个参数 0 表示信号量的初始值为 0，通常用于表示资源未被占用。
*/
    if (sem_init(&mtx->sem, 1, 0) == -1) { //  初始化信号量，参数1表示信号量在进程间共享，参数0表示初始值为0
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno, //  如果初始化失败，记录错误日志
                      "sem_init() failed");
    } else {
        mtx->semaphore = 1; //  如果初始化成功，将互斥锁结构体的semaphore字段设置为1
    }

#endif

    return NGX_OK; //  返回成功
}


void
ngx_shmtx_destroy(ngx_shmtx_t *mtx)
{
#if (NGX_HAVE_POSIX_SEM) //  检查系统是否支持POSIX信号量

    if (mtx->semaphore) { //  如果信号量指针不为空，表示信号量已经被创建
        if (sem_destroy(&mtx->sem) == -1) { //  调用sem_destroy函数销毁信号量
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno, //  如果销毁失败，记录错误日志
                          "sem_destroy() failed");
        }
    }

#endif
}


ngx_uint_t
ngx_shmtx_trylock(ngx_shmtx_t *mtx)
{
    return (*mtx->lock == 0 && ngx_atomic_cmp_set(mtx->lock, 0, ngx_pid)); 
    //  检查互斥锁的当前状态     如果锁的值为0，表示锁未被占用     使用原子操作尝试将锁的值从0设置为当前进程ID (ngx_pid)  
    //   如果设置成功，表示成功获取锁，返回true     如果设置失败，表示锁已被其他进程占用，返回false
}


void
ngx_shmtx_lock(ngx_shmtx_t *mtx)
{
    ngx_uint_t         i, n;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "shmtx lock");

    for ( ;; ) { //  无限循环，直到成功获取锁

        if (*mtx->lock == 0 && ngx_atomic_cmp_set(mtx->lock, 0, ngx_pid)) { //  尝试获取锁，如果锁未被占用且原子操作成功，则返回
            return;
        }

        if (ngx_ncpu > 1) { //  如果系统有多个CPU核心

            for (n = 1; n < mtx->spin; n <<= 1) { //  自旋锁的循环，初始值为1，每次左移一位，直到达到spin值

                for (i = 0; i < n; i++) {
                    ngx_cpu_pause(); //  让出CPU时间片，减少CPU消耗
                }

                if (*mtx->lock == 0
                    && ngx_atomic_cmp_set(mtx->lock, 0, ngx_pid))
                {
                    return;
                }
            }
        }

#if (NGX_HAVE_POSIX_SEM) //  如果系统支持POSIX信号量

        if (mtx->semaphore) { //  如果互斥锁使用的是信号量
            (void) ngx_atomic_fetch_add(mtx->wait, 1); //  原子操作，增加等待计数

            if (*mtx->lock == 0 && ngx_atomic_cmp_set(mtx->lock, 0, ngx_pid)) { //  尝试获取锁
                (void) ngx_atomic_fetch_add(mtx->wait, -1); //  成功获取锁，减少等待计数
                return; //  返回，锁已获取
            }

            ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, //  记录等待信号量的调试信息
                           "shmtx wait %uA", *mtx->wait);

            while (sem_wait(&mtx->sem) == -1) { //  循环等待信号量，直到成功获取信号量或者遇到非可中断错误
                ngx_err_t  err; //  定义一个 ngx_err_t 类型的变量 err，用于存储错误码

                err = ngx_errno; //  获取最近的错误码 ，ngx_errno 是一个全局变量，保存了最近一次系统调用的错误码

                if (err != NGX_EINTR) { //  如果错误码不是 NGX_EINTR（被信号中断），则记录错误日志并退出循环
                    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, err, //  调用 ngx_log_error 函数记录错误日志                     参数1: NGX_LOG_ALERT 表示日志级别为严重警告                     参数2: ngx_cycle->log 表示使用当前循环的日志对象                     参数3: err 表示错误码                     参数4: "sem_wait() failed while waiting on shmtx" 表示错误信息
                                  "sem_wait() failed while waiting on shmtx");
                    break; //  跳出当前循环或switch语句
                }
            }

            ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, //  记录调试日志，表示信号量已被获取
                           "shmtx awoke");

            continue; //  继续下一次循环
        }

#endif

        ngx_sched_yield(); //  让出CPU时间片，以允许其他线程或进程运行
    }
}


void
ngx_shmtx_unlock(ngx_shmtx_t *mtx)
{
    if (mtx->spin != (ngx_uint_t) -1) { //  检查互斥锁的spin值是否不等于-1
        ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "shmtx unlock"); //  如果不等于-1，记录调试日志，表示正在解锁共享互斥锁
    }

    if (ngx_atomic_cmp_set(mtx->lock, ngx_pid, 0)) { //  使用原子操作尝试将互斥锁的lock值从ngx_pid设置为0
        ngx_shmtx_wakeup(mtx); //  如果原子操作成功，调用ngx_shmtx_wakeup函数唤醒等待该锁的线程或进程
    }
}


ngx_uint_t
ngx_shmtx_force_unlock(ngx_shmtx_t *mtx, ngx_pid_t pid)
{
    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, //  使用ngx_log_debug0函数记录调试日志，日志级别为NGX_LOG_DEBUG_CORE     ngx_cycle->log表示当前的日志对象，0表示没有错误代码     "shmtx forced unlock"是日志消息，表示共享锁被强制解锁
                   "shmtx forced unlock");

    if (ngx_atomic_cmp_set(mtx->lock, pid, 0)) { //  使用ngx_atomic_cmp_set函数尝试将共享锁的锁值从pid设置为0     mtx->lock表示共享锁的锁值，pid表示当前进程ID，0表示解锁状态     如果原子操作成功（即锁值从pid变为0），则执行以下代码
        ngx_shmtx_wakeup(mtx); //  调用ngx_shmtx_wakeup函数唤醒等待该锁的线程或进程
        return 1; //  返回1表示解锁成功
    }

    return 0; //  如果解锁失败，返回0
}


static void
ngx_shmtx_wakeup(ngx_shmtx_t *mtx)
{
#if (NGX_HAVE_POSIX_SEM) //  检查系统是否支持POSIX信号量
    ngx_atomic_uint_t  wait;

    if (!mtx->semaphore) { //  如果信号量未初始化，直接返回
        return;
    }

    for ( ;; ) { //  无限循环，直到成功唤醒一个等待的线程

        wait = *mtx->wait; //  获取当前等待的线程数

        if ((ngx_atomic_int_t) wait <= 0) { //  如果没有等待的线程，直接返回
            return;
        }

        if (ngx_atomic_cmp_set(mtx->wait, wait, wait - 1)) { //  尝试将等待线程数减1，如果成功则跳出循环
            break;
        }
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, //  记录调试日志，显示唤醒前等待的线程数
                   "shmtx wake %uA", wait);

    if (sem_post(&mtx->sem) == -1) { //  尝试唤醒一个等待的线程，如果失败则记录错误日志
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      "sem_post() failed while wake shmtx");
    }

#endif
}


#else


ngx_int_t
ngx_shmtx_create(ngx_shmtx_t *mtx, ngx_shmtx_sh_t *addr, u_char *name)
{
    if (mtx->name) {

        if (ngx_strcmp(name, mtx->name) == 0) {
            mtx->name = name;
            return NGX_OK;
        }

        ngx_shmtx_destroy(mtx);
    }

    mtx->fd = ngx_open_file(name, NGX_FILE_RDWR, NGX_FILE_CREATE_OR_OPEN,
                            NGX_FILE_DEFAULT_ACCESS);

    if (mtx->fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, ngx_cycle->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", name);
        return NGX_ERROR;
    }

    if (ngx_delete_file(name) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", name);
    }

    mtx->name = name;

    return NGX_OK;
}


void
ngx_shmtx_destroy(ngx_shmtx_t *mtx)
{
    if (ngx_close_file(mtx->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", mtx->name);
    }
}


ngx_uint_t
ngx_shmtx_trylock(ngx_shmtx_t *mtx)
{
    ngx_err_t  err;

    err = ngx_trylock_fd(mtx->fd);

    if (err == 0) {
        return 1;
    }

    if (err == NGX_EAGAIN) {
        return 0;
    }

#if __osf__ /* Tru64 UNIX */

    if (err == NGX_EACCES) {
        return 0;
    }

#endif

    ngx_log_abort(err, ngx_trylock_fd_n " %s failed", mtx->name);

    return 0;
}


void
ngx_shmtx_lock(ngx_shmtx_t *mtx)
{
    ngx_err_t  err;

    err = ngx_lock_fd(mtx->fd);

    if (err == 0) {
        return;
    }

    ngx_log_abort(err, ngx_lock_fd_n " %s failed", mtx->name);
}


void
ngx_shmtx_unlock(ngx_shmtx_t *mtx)
{
    ngx_err_t  err;

    err = ngx_unlock_fd(mtx->fd);

    if (err == 0) {
        return;
    }

    ngx_log_abort(err, ngx_unlock_fd_n " %s failed", mtx->name);
}


ngx_uint_t
ngx_shmtx_force_unlock(ngx_shmtx_t *mtx, ngx_pid_t pid)
{
    return 0;
}

#endif
