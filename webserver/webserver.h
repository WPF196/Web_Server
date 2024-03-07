#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "../threadpool/threadpool.h"
#include "../http/http_conn.h"

const int MAX_FD = 65535;               // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000;     // 最大事件数
const int TIMESLOT = 5;                 // 最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();     // 创建线程池
    void sql_pool();        // 初始化数据库连接池
    void log_write();       // 初始化日志系统
    void trig_mode();       // 设置epoll的触发模式：ET、LT
    void eventListen();     // 创建网络编程：Socket网络编程基础步骤
    void eventLoop();       // 事件回环（即服务器主线程）
    
    void timer(int connfd, struct sockaddr_in client_address);  // 创建一个定时器节点，将连接信息挂载
    void adjust_timer(util_timer *timer);   // 调整定时器在链表的位置
    void deal_timer(util_timer *timer, int sockfd); // 删除定时器节点，关闭连接
    bool dealclinetdata();  // http 处理用户数据（接收用户连接并分配相关定时器等）
    bool dealwithsignal(bool& timeout, bool& stop_server);  // 处理定时器信号
    void dealwithread(int sockfd);  // 处理客户连接上接收到的数据
    void dealwithwrite(int sockfd); // 写操作

public:        
    int m_port;         // 端口
    char *m_root;       // 当前文件根目录
    int m_log_write;    // 日志类型（同步/异步）
    int m_close_log;    // 是否关闭日志
    int m_actormodel;   // Reactor 1 / Proactor 0
    
    int m_pipefd[2];    // 存相互连接的套接字
    int m_epollfd;      // epoll对象
    http_conn *users;   // http连接组

    connection_pool *m_connPool;
    string m_user;          // 数据库用户名
    string m_passWord;      // 数据库密码
    string m_databaseName;  // 数据库名
    int m_sql_num;          // 数据库连接池数量

    threadpool<http_conn> *m_pool;
    int m_thread_num;

    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;         // 监听套接字
    int m_OPT_LINGER;       // 长连接选项
    int m_TRIGMode;         // 触发模式
    int m_LISTENTrigmode;   // 监听模式 LT/ET
    int m_CONNTringmode;    // 连接模式 ET/LT

    client_data *users_timer;
    Utils utils;
};

#endif