#include <ev.h>
#include <stdio.h>

// 定时器回调函数
static void timeout_cb(EV_P_ ev_timer *w, int revents) {
    puts("Timeout triggered.");
    ev_break(EV_A_ EVBREAK_ALL);  // 触发一次后退出事件循环
}

int main() {
    struct ev_loop *loop = EV_DEFAULT;

    ev_timer timeout_watcher;
    ev_timer_init(&timeout_watcher, timeout_cb, 2.0, 0.); // 2 秒后触发一次
    ev_timer_start(loop, &timeout_watcher);

    puts("Event loop starting...");
    ev_run(loop, 0);
    puts("Event loop stopped.");
    return 0;
}
