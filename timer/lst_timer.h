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

class util_timer;       // 前置声明

// 用户端数据
struct client_data
{
    sockaddr_in address;    // 地址
    int sockfd;             // 套接字
    util_timer *timer;      // 定时器
};

// 定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;          // 超时时间

    void (* cb_func)(client_data *);    // 回调函数
    client_data *user_data; // 连接资源
    util_timer *prev;       // 前驱定时器
    util_timer *next;       // 后继定时器
};

// 定时器链表类（按 超时时间 从小到大排序）
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    // 添加定时器，内部调用私有成员add_timer
    void add_timer(util_timer *timer);
    // 调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(util_timer *timer);
    // 删除定时器
    void del_timer(util_timer *timer);
    // 定时任务处理函数（即调用节点内的回调函数）
    void tick();

private:
    // 私有成员，被公有成员 add_timer 和 adjust_time 调用，主要用于调整链表内部结点顺序
    // 函数参数（待插入的元素，插入链表的头节点）
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;       // 链表头
    util_timer *tail;       // 链表尾
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    // 初始化Utils，传入超时单位
    void init(int timeslot);
    // 将文件描述符设为非阻塞
    int setnonblocking(int fd);
    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    // 信号处理函数
    static void sig_handler(int sig);
    // 设置信号函数，默认被信号打断的系统调用自动重启
    void addsig(int sig, void(handler)(int), bool restart = true);
    // 定时器处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();
    // 向套接字connfd发送错误信息info，并断开connfd连接
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