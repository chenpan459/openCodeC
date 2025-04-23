
/*
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


#if (NGX_HAVE_CPUSET_SETAFFINITY)

void
ngx_setaffinity(ngx_cpuset_t *cpu_affinity, ngx_log_t *log)
{
    ngx_uint_t  i;

    for (i = 0; i < CPU_SETSIZE; i++) { //  遍历CPU集合，检查每个CPU是否被设置在cpu_affinity中
        if (CPU_ISSET(i, cpu_affinity)) { //  如果CPU i 在cpu_affinity集合中
            ngx_log_error(NGX_LOG_NOTICE, log, 0,
                          "cpuset_setaffinity(): using cpu #%ui", i);
        }
    }

    if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, //  调用cpuset_setaffinity函数设置CPU亲和性     CPU_LEVEL_WHICH和CPU_WHICH_PID是参数，表示设置当前进程的CPU亲和性     -1表示当前进程ID     sizeof(cpuset_t)表示cpu_affinity结构的大小     cpu_affinity是包含CPU亲和性设置的集合
                           sizeof(cpuset_t), cpu_affinity) == -1)
    {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      "cpuset_setaffinity() failed");
    }
}

#elif (NGX_HAVE_SCHED_SETAFFINITY)

void
ngx_setaffinity(ngx_cpuset_t *cpu_affinity, ngx_log_t *log)
{
    ngx_uint_t  i; //  定义一个无符号整数 i，用于循环计数

    for (i = 0; i < CPU_SETSIZE; i++) { //  遍历 CPU_SETSIZE 个 CPU 核心编号
        if (CPU_ISSET(i, cpu_affinity)) { //  检查当前 CPU 核心是否在 cpu_affinity 集合中
            ngx_log_error(NGX_LOG_NOTICE, log, 0, //  如果在集合中，记录日志，表示将使用该 CPU 核心
                          "sched_setaffinity(): using cpu #%ui", i);
        }
    }

    if (sched_setaffinity(0, sizeof(cpu_set_t), cpu_affinity) == -1) { //  调用 sched_setaffinity 函数设置进程的 CPU 亲和性     参数 0 表示当前进程，sizeof(cpu_set_t) 表示 cpu_affinity 的大小
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno, //  如果设置失败，记录错误日志
                      "sched_setaffinity() failed");
    }
}

#endif
