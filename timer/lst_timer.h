#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

// 前置声明
class util_timer;

// 用户端数据
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

// 定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;     // 超时时间

    // 回调函数，处理不活跃连接
    void (* cb_func)(client_data *); 
    client_data *user_data;
    util_timer *prev;
    util_timer *next;
};

// 双向链表类（顺序排列，元素为 定时器类）
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();    // 定时任务处理接口（处理不活跃连接）

private:
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;
    util_timer *tail;
};

// 定时器链表管理类
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);
    int setnonblocking(int fd);
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
 
    static void sig_handler(int sig);
    void addsig(int sig, void(handler)(int), bool restart = true);
    void timer_handler();
    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;           // 管道id
    sort_timer_lst m_timer_lst;     // 定时器链表
    static int u_epollfd;           // epollfd
    int m_TIMESLOT;                 // 最小时间间隙
};

// 定时器回调函数：从内核事件表删除事件，关闭文件描述符，释放连接资源
void cb_func(client_data *user_data);

#endif