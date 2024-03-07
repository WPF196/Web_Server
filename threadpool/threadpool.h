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
    threadpool(int actor_model, connection_pool* connPool, 
                int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T* request, int state);
    bool append_p(T* request);

private:
    /* 工作线程运行的函数，循环获取工作队列中的任务并执行之 */
    static void *worker(void* arg);
    void run();

private:
    int m_thread_number;
    int m_max_requests;             // 请求队列中允许的最大请求数

    pthread_t* m_threads;           // 线程池数组
    std::list<T*> m_workqueue;      // 请求队列
    
    locker m_queuelocker;           // 保护请求队列的互斥锁
    sem m_queuestat;                // 是否有任务需要处理
    connection_pool* m_connPool;    // 数据库
    int m_actor_model;              // 模型切换（指Reactor/Proactor）
};

template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) 
                        : m_actor_model(actor_model), m_thread_number(thread_number), 
                          m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    if(thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
        throw std::exception();
    
    for(int i = 0; i < thread_number; ++i){
        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete[] m_threads;
            throw std::exception();
        }
        // 线程分离，使得子线程和主线程互不干涉
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

// reactor模式下的请求入队（需要指明任务的状态（读|写））
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;   // http_conn状态，0读 1写
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// proactor模式下的请求入队（不用指明任务状态，主线程直接处理）
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while(true){
        m_queuestat.wait();
        m_queuelocker.lock();   
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }

        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request)
            continue;

        // Reactor
        if(m_actor_model == 1){
            // 从客户端读
            if(request->m_state == 0){
                if(request->read_once()){
                    request->improv = 1;    // 读完
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();     // 读完后直接响应
                }
                else{
                    request->improv = 1;
                    request->timer_flag = 1;    // 出错
                }
            }
            // 写给客户端
            else{   
                if(request->write())
                    request->improv = 1;
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        // Proactor，线程池不需要进行数据读取（因为主线程已经读完了，子线程只处理响应）
        // 而是直接开始业务处理
        else{
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif