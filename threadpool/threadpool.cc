#include "threadpool.h"

template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) 
                        : m_actor_model(actor_model), m_thread_number(thread_number), 
                          m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    if(thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    
    // 创建线程数组，用于存储指定数量的线程
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
        throw std::exception();
    
    // 创建指定个工作的线程
    for(int i = 0; i < thread_number; ++i){
        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete[] m_threads;
            throw std::exception();
        }
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

// reactor模式下的请求入队
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests){   // 溢出
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;   // http_conn状态，0读 1写
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();         // 待处理任务信号量+1
    return true;
}

// proactor模式下的请求入队
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
    m_queuelocker.post();
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
        m_queuestat.wait();     // 等待任务
        m_queuelocker.lock();   
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }

        T *request = m_workqueue.front();
        m_queuelocker.pop_front();
        m_queuelocker.unlock();
        if(!request)
            continue;

        // Reactor
        if(m_actor_model == 1){
            // 从客户端读
            if(request->m_state == 0){
                if(request->read_once()){
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
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
        // Proactor，线程池不需要进行数据读取，而是直接开始业务处理
        // 之前的操作已经将数据读取到http的read和write的buffer中了
        else{
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}