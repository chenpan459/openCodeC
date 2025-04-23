
/*
 * Copyright (C) Nginx, Inc.
 * Copyright (C) Valentin V. Bartenev
 * Copyright (C) Ruslan Ermilov
 */


#include <ngx_config.h> //  包含Nginx配置相关的头文件
#include <ngx_core.h> //  包含Nginx核心功能相关的头文件
#include <ngx_thread_pool.h> //  包含Nginx线程池相关的头文件


typedef struct { //  定义线程池配置结构体
    ngx_array_t               pools; //  用于存储线程池的数组
} ngx_thread_pool_conf_t;


typedef struct { //  定义线程池队列结构体
    ngx_thread_task_t        *first; //  指向队列中第一个任务的指针
    ngx_thread_task_t       **last; //  指向队列中最后一个任务指针的指针
} ngx_thread_pool_queue_t;

#define ngx_thread_pool_queue_init(q)                                         \ //  初始化线程池队列的宏定义
    (q)->first = NULL;                                                        \
    (q)->last = &(q)->first //  将队列的first指针置为NULL，last指针指向first


struct ngx_thread_pool_s { //  定义线程池结构体
    ngx_thread_mutex_t        mtx; //  线程互斥锁，用于保护线程池的同步
    ngx_thread_pool_queue_t   queue; //  线程池任务队列
    ngx_int_t                 waiting; //  等待任务的线程数量
    ngx_thread_cond_t         cond; //  条件变量，用于线程同步

    ngx_log_t                *log; //  指向日志结构的指针，用于记录线程池的日志信息

    ngx_str_t                 name; //  线程池的名称
    ngx_uint_t                threads; //  线程池中的线程数量
    ngx_int_t                 max_queue; //  线程池的最大队列长度

    u_char                   *file; //  文件指针，用于记录配置文件的位置
    ngx_uint_t                line; //  行号，用于记录配置文件的位置
};


static ngx_int_t ngx_thread_pool_init(ngx_thread_pool_t *tp, ngx_log_t *log, //  初始化线程池
    ngx_pool_t *pool);
static void ngx_thread_pool_destroy(ngx_thread_pool_t *tp); //  销毁线程池
static void ngx_thread_pool_exit_handler(void *data, ngx_log_t *log); //  线程池退出处理函数

static void *ngx_thread_pool_cycle(void *data); //  线程池循环函数
static void ngx_thread_pool_handler(ngx_event_t *ev); //  线程池事件处理函数

static char *ngx_thread_pool(ngx_conf_t *cf, ngx_command_t *cmd, void *conf); //  配置线程池

static void *ngx_thread_pool_create_conf(ngx_cycle_t *cycle); //  创建线程池配置
static char *ngx_thread_pool_init_conf(ngx_cycle_t *cycle, void *conf); //  初始化线程池配置

static ngx_int_t ngx_thread_pool_init_worker(ngx_cycle_t *cycle); //  初始化工作线程
static void ngx_thread_pool_exit_worker(ngx_cycle_t *cycle); //  退出工作线程


static ngx_command_t  ngx_thread_pool_commands[] = { //  线程池命令数组

    { ngx_string("thread_pool"), //  定义一个名为 "thread_pool" 的配置指令
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE23, //  指令可以在主配置块中使用，并且是直接配置指令，可以接受2或3个参数
      ngx_thread_pool, //  指令的处理函数
      0, //  指令的偏移量，这里为0，表示不需要偏移
      0, //  指令的默认值，这里为0，表示没有默认值
      NULL }, //  下一个指令的指针，这里为NULL，表示这是最后一个指令

      ngx_null_command //  定义一个空指令，表示指令列表的结束
};


static ngx_core_module_t  ngx_thread_pool_module_ctx = {
    ngx_string("thread_pool"), //  模块的名称
    ngx_thread_pool_create_conf,
    ngx_thread_pool_init_conf
};


ngx_module_t  ngx_thread_pool_module = {
    NGX_MODULE_V1,
    &ngx_thread_pool_module_ctx,           /* module context */
    ngx_thread_pool_commands,              /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_thread_pool_init_worker,           /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_thread_pool_exit_worker,           /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_str_t  ngx_thread_pool_default = ngx_string("default");

static ngx_uint_t               ngx_thread_pool_task_id;
static ngx_atomic_t             ngx_thread_pool_done_lock;
static ngx_thread_pool_queue_t  ngx_thread_pool_done;


static ngx_int_t
ngx_thread_pool_init(ngx_thread_pool_t *tp, ngx_log_t *log, ngx_pool_t *pool)
{
    int             err; //  用于存储pthread函数的返回错误码
    pthread_t       tid; //  用于存储线程ID
    ngx_uint_t      n; //  用于循环计数
    pthread_attr_t  attr; //  用于设置线程属性

    if (ngx_notify == NULL) { //  检查ngx_notify是否为NULL，如果为NULL则表示配置的事件方法不能与线程池一起使用
        ngx_log_error(NGX_LOG_ALERT, log, 0,
               "the configured event method cannot be used with thread pools");
        return NGX_ERROR;
    }

    ngx_thread_pool_queue_init(&tp->queue); //  初始化线程池队列

    if (ngx_thread_mutex_create(&tp->mtx, log) != NGX_OK) { //  创建线程互斥锁
        return NGX_ERROR;
    }

    if (ngx_thread_cond_create(&tp->cond, log) != NGX_OK) { //  创建线程条件变量
        (void) ngx_thread_mutex_destroy(&tp->mtx, log); //  如果条件变量创建失败，销毁已创建的互斥锁
        return NGX_ERROR;
    }

    tp->log = log; //  设置线程池的日志

    err = pthread_attr_init(&attr); //  初始化线程属性
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, log, err,
                      "pthread_attr_init() failed");
        return NGX_ERROR;
    }

    err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); //  设置线程属性为分离状态，使得线程在结束时自动释放资源
    if (err) { //  检查设置是否成功，err为非零表示失败
        ngx_log_error(NGX_LOG_ALERT, log, err,
                      "pthread_attr_setdetachstate() failed");
        return NGX_ERROR;
    }

#if 0
    err = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, log, err,
                      "pthread_attr_setstacksize() failed");
        return NGX_ERROR;
    }
#endif

    for (n = 0; n < tp->threads; n++) {
        err = pthread_create(&tid, &attr, ngx_thread_pool_cycle, tp);
        if (err) {
            ngx_log_error(NGX_LOG_ALERT, log, err,
                          "pthread_create() failed");
            return NGX_ERROR;
        }
    }

    (void) pthread_attr_destroy(&attr); //  使用 pthread_attr_destroy 函数销毁线程属性对象     该函数用于释放与线程属性对象相关的资源     参数 &attr 是指向要销毁的线程属性对象的指针     (void) 表示忽略该函数的返回值，因为在此处不需要处理返回值

    return NGX_OK;
}


static void
ngx_thread_pool_destroy(ngx_thread_pool_t *tp)
{
    ngx_uint_t           n; //  定义一个无符号整数n，用于循环计数
    ngx_thread_task_t    task; //  定义一个线程任务结构体task
    volatile ngx_uint_t  lock; //  定义一个volatile修饰的无符号整数lock，用于线程同步

    ngx_memzero(&task, sizeof(ngx_thread_task_t)); //  使用ngx_memzero函数将task结构体初始化为0

    task.handler = ngx_thread_pool_exit_handler; //  设置task的handler为ngx_thread_pool_exit_handler，这是一个退出处理函数
    task.ctx = (void *) &lock; //  设置task的ctx为lock的地址，用于传递上下文信息

    for (n = 0; n < tp->threads; n++) { //  循环遍历线程池中的所有线程
        lock = 1; //  将lock设置为1，表示需要等待

        if (ngx_thread_task_post(tp, &task) != NGX_OK) { //  调用ngx_thread_task_post函数将任务task提交到线程池 如果提交失败，直接返回
            return;
        }

        while (lock) { //  循环等待lock变为0，表示任务已经完成
            ngx_sched_yield(); //  调用ngx_sched_yield函数让出CPU时间片，避免忙等待
        }

        task.event.active = 0;
    }

    (void) ngx_thread_cond_destroy(&tp->cond, tp->log);

    (void) ngx_thread_mutex_destroy(&tp->mtx, tp->log);
}


static void
ngx_thread_pool_exit_handler(void *data, ngx_log_t *log)
{
    ngx_uint_t *lock = data;

    *lock = 0;

    pthread_exit(0);
}


ngx_thread_task_t *
ngx_thread_task_alloc(ngx_pool_t *pool, size_t size)
{
    ngx_thread_task_t  *task;

    task = ngx_pcalloc(pool, sizeof(ngx_thread_task_t) + size);
    if (task == NULL) {
        return NULL;
    }

    task->ctx = task + 1;

    return task;
}


ngx_int_t
ngx_thread_task_post(ngx_thread_pool_t *tp, ngx_thread_task_t *task)
{
    if (task->event.active) {
        ngx_log_error(NGX_LOG_ALERT, tp->log, 0,
                      "task #%ui already active", task->id);
        return NGX_ERROR;
    }

    if (ngx_thread_mutex_lock(&tp->mtx, tp->log) != NGX_OK) {
        return NGX_ERROR;
    }

    if (tp->waiting >= tp->max_queue) {
        (void) ngx_thread_mutex_unlock(&tp->mtx, tp->log);

        ngx_log_error(NGX_LOG_ERR, tp->log, 0,
                      "thread pool \"%V\" queue overflow: %i tasks waiting",
                      &tp->name, tp->waiting);
        return NGX_ERROR;
    }

    task->event.active = 1;

    task->id = ngx_thread_pool_task_id++;
    task->next = NULL;

    if (ngx_thread_cond_signal(&tp->cond, tp->log) != NGX_OK) {
        (void) ngx_thread_mutex_unlock(&tp->mtx, tp->log);
        return NGX_ERROR;
    }

    *tp->queue.last = task;
    tp->queue.last = &task->next;

    tp->waiting++;

    (void) ngx_thread_mutex_unlock(&tp->mtx, tp->log);

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, tp->log, 0,
                   "task #%ui added to thread pool \"%V\"",
                   task->id, &tp->name);

    return NGX_OK;
}


static void *
ngx_thread_pool_cycle(void *data)
{
    ngx_thread_pool_t *tp = data;

    int                 err;
    sigset_t            set;
    ngx_thread_task_t  *task;

#if 0
    ngx_time_update();
#endif

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, tp->log, 0,
                   "thread in pool \"%V\" started", &tp->name);

    sigfillset(&set);

    sigdelset(&set, SIGILL);
    sigdelset(&set, SIGFPE);
    sigdelset(&set, SIGSEGV);
    sigdelset(&set, SIGBUS);

    err = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, tp->log, err, "pthread_sigmask() failed");
        return NULL;
    }

    for ( ;; ) {
        if (ngx_thread_mutex_lock(&tp->mtx, tp->log) != NGX_OK) {
            return NULL;
        }

        /* the number may become negative */
        tp->waiting--;

        while (tp->queue.first == NULL) {
            if (ngx_thread_cond_wait(&tp->cond, &tp->mtx, tp->log)
                != NGX_OK)
            {
                (void) ngx_thread_mutex_unlock(&tp->mtx, tp->log);
                return NULL;
            }
        }

        task = tp->queue.first;
        tp->queue.first = task->next;

        if (tp->queue.first == NULL) {
            tp->queue.last = &tp->queue.first;
        }

        if (ngx_thread_mutex_unlock(&tp->mtx, tp->log) != NGX_OK) {
            return NULL;
        }

#if 0
        ngx_time_update();
#endif

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, tp->log, 0,
                       "run task #%ui in thread pool \"%V\"",
                       task->id, &tp->name);

        task->handler(task->ctx, tp->log);

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, tp->log, 0,
                       "complete task #%ui in thread pool \"%V\"",
                       task->id, &tp->name);

        task->next = NULL;

        ngx_spinlock(&ngx_thread_pool_done_lock, 1, 2048);

        *ngx_thread_pool_done.last = task;
        ngx_thread_pool_done.last = &task->next;

        ngx_memory_barrier();

        ngx_unlock(&ngx_thread_pool_done_lock);

        (void) ngx_notify(ngx_thread_pool_handler);
    }
}


static void
ngx_thread_pool_handler(ngx_event_t *ev)
{
    ngx_event_t        *event;
    ngx_thread_task_t  *task;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ev->log, 0, "thread pool handler");

    ngx_spinlock(&ngx_thread_pool_done_lock, 1, 2048);

    task = ngx_thread_pool_done.first;
    ngx_thread_pool_done.first = NULL;
    ngx_thread_pool_done.last = &ngx_thread_pool_done.first;

    ngx_memory_barrier();

    ngx_unlock(&ngx_thread_pool_done_lock);

    while (task) {
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,
                       "run completion handler for task #%ui", task->id);

        event = &task->event;
        task = task->next;

        event->complete = 1;
        event->active = 0;

        event->handler(event);
    }
}


static void *
ngx_thread_pool_create_conf(ngx_cycle_t *cycle)
{
    ngx_thread_pool_conf_t  *tcf;

    tcf = ngx_pcalloc(cycle->pool, sizeof(ngx_thread_pool_conf_t));
    if (tcf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&tcf->pools, cycle->pool, 4,
                       sizeof(ngx_thread_pool_t *))
        != NGX_OK)
    {
        return NULL;
    }

    return tcf;
}


static char *
ngx_thread_pool_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_thread_pool_conf_t *tcf = conf;

    ngx_uint_t           i;
    ngx_thread_pool_t  **tpp;

    tpp = tcf->pools.elts;

    for (i = 0; i < tcf->pools.nelts; i++) {

        if (tpp[i]->threads) {
            continue;
        }

        if (tpp[i]->name.len == ngx_thread_pool_default.len
            && ngx_strncmp(tpp[i]->name.data, ngx_thread_pool_default.data,
                           ngx_thread_pool_default.len)
               == 0)
        {
            tpp[i]->threads = 32;
            tpp[i]->max_queue = 65536;
            continue;
        }

        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "unknown thread pool \"%V\" in %s:%ui",
                      &tpp[i]->name, tpp[i]->file, tpp[i]->line);

        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_thread_pool(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t          *value;
    ngx_uint_t          i;
    ngx_thread_pool_t  *tp;

    value = cf->args->elts;

    tp = ngx_thread_pool_add(cf, &value[1]);

    if (tp == NULL) {
        return NGX_CONF_ERROR;
    }

    if (tp->threads) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "duplicate thread pool \"%V\"", &tp->name);
        return NGX_CONF_ERROR;
    }

    tp->max_queue = 65536;

    for (i = 2; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "threads=", 8) == 0) {

            tp->threads = ngx_atoi(value[i].data + 8, value[i].len - 8);

            if (tp->threads == (ngx_uint_t) NGX_ERROR || tp->threads == 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid threads value \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        if (ngx_strncmp(value[i].data, "max_queue=", 10) == 0) {

            tp->max_queue = ngx_atoi(value[i].data + 10, value[i].len - 10);

            if (tp->max_queue == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid max_queue value \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            continue;
        }
    }

    if (tp->threads == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"%V\" must have \"threads\" parameter",
                           &cmd->name);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


ngx_thread_pool_t *
ngx_thread_pool_add(ngx_conf_t *cf, ngx_str_t *name)
{
    ngx_thread_pool_t       *tp, **tpp;
    ngx_thread_pool_conf_t  *tcf;

    if (name == NULL) {
        name = &ngx_thread_pool_default;
    }

    tp = ngx_thread_pool_get(cf->cycle, name);

    if (tp) {
        return tp;
    }

    tp = ngx_pcalloc(cf->pool, sizeof(ngx_thread_pool_t));
    if (tp == NULL) {
        return NULL;
    }

    tp->name = *name;
    tp->file = cf->conf_file->file.name.data;
    tp->line = cf->conf_file->line;

    tcf = (ngx_thread_pool_conf_t *) ngx_get_conf(cf->cycle->conf_ctx,
                                                  ngx_thread_pool_module);

    tpp = ngx_array_push(&tcf->pools);
    if (tpp == NULL) {
        return NULL;
    }

    *tpp = tp;

    return tp;
}


ngx_thread_pool_t *
ngx_thread_pool_get(ngx_cycle_t *cycle, ngx_str_t *name)
{
    ngx_uint_t                i;
    ngx_thread_pool_t       **tpp;
    ngx_thread_pool_conf_t   *tcf;

    tcf = (ngx_thread_pool_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                  ngx_thread_pool_module);

    tpp = tcf->pools.elts;

    for (i = 0; i < tcf->pools.nelts; i++) {

        if (tpp[i]->name.len == name->len
            && ngx_strncmp(tpp[i]->name.data, name->data, name->len) == 0)
        {
            return tpp[i];
        }
    }

    return NULL;
}


static ngx_int_t
ngx_thread_pool_init_worker(ngx_cycle_t *cycle)
{
    ngx_uint_t                i; //  定义一个无符号整数 i，用于循环计数
    ngx_thread_pool_t       **tpp; //  定义一个指向 ngx_thread_pool_t 结构体指针的指针 tpp
    ngx_thread_pool_conf_t   *tcf; //  定义一个指向 ngx_thread_pool_conf_t 结构体指针 tcf

    if (ngx_process != NGX_PROCESS_WORKER //  检查当前进程是否为工作进程或单进程模式
        && ngx_process != NGX_PROCESS_SINGLE)
    {
        return NGX_OK; //  如果不是工作进程或单进程模式，直接返回 NGX_OK
    }

    tcf = (ngx_thread_pool_conf_t *) ngx_get_conf(cycle->conf_ctx, //  获取线程池配置
                                                  ngx_thread_pool_module);

    if (tcf == NULL) {
        return NGX_OK;
    }

    ngx_thread_pool_queue_init(&ngx_thread_pool_done); //  初始化线程池队列 ngx_thread_pool_done

    tpp = tcf->pools.elts; //  获取线程池数组 tpp，该数组存储了所有的线程池

    for (i = 0; i < tcf->pools.nelts; i++) { //  遍历线程池数组，初始化每个线程池
        if (ngx_thread_pool_init(tpp[i], cycle->log, cycle->pool) != NGX_OK) { //  调用 ngx_thread_pool_init 初始化线程池，传入线程池指针、日志和内存池
            return NGX_ERROR;
        }
    }

    return NGX_OK; //  如果所有线程池都成功初始化，返回 NGX_OK，表示成功
}


static void
ngx_thread_pool_exit_worker(ngx_cycle_t *cycle)
{
    ngx_uint_t                i; //  定义一个无符号整数变量i，用于循环计数
    ngx_thread_pool_t       **tpp; //  定义一个指向线程池结构体指针的指针，用于遍历线程池数组
    ngx_thread_pool_conf_t   *tcf; //  定义一个指向线程池配置结构体的指针

    if (ngx_process != NGX_PROCESS_WORKER //  检查当前进程是否为工作进程或单进程模式，如果不是则直接返回
        && ngx_process != NGX_PROCESS_SINGLE)
    {
        return;
    }

    tcf = (ngx_thread_pool_conf_t *) ngx_get_conf(cycle->conf_ctx, //  获取线程池模块的配置信息
                                                  ngx_thread_pool_module);

    if (tcf == NULL) {
        return;
    }

    tpp = tcf->pools.elts;

    for (i = 0; i < tcf->pools.nelts; i++) {
        ngx_thread_pool_destroy(tpp[i]);
    }
}
