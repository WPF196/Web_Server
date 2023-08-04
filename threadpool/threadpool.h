#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，
      max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool* connPool, 
                int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T* request, int state);  // 请求队列中插入任务请求(http_conn)
    bool append_p(T* request);

private:
    /* 工作线程运行的函数，它不断从工作队列中取出任务并执行之 */
    static void *worker(void* arg);
    void run();

private:
    int m_thread_number;            // 线程池中的线程数量
    int m_max_requests;             // 请求队列中允许的最大请求数

    pthread_t* m_threads;           // 描述线程池的数组，其大小为 m_thread_number
    std::list<T*> m_workqueue;      // 请求队列
    
    locker m_queuelocker;           // 保护请求队列的互斥锁
    sem m_queuestat;                // 是否有任务需要处理
    connection_pool* m_connPool;    // 数据库
    int m_actor_model;              // 模型切换（指Reactor/Proactor）
};

#endif