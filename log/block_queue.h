/* 循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size; */

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

#include "../lock/locker.h"

using namespace std;

// 阻塞队列类   （封装 生产者-消费者模型）
template <class T>
class block_queue
{
public:
    block_queue(int max_size = 1000)
    {
        if(max_size <= 0)
        exit(-1);    

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1; 
    }
    ~block_queue()
    {
        m_mutex.lock();
        if(m_array != NULL)
            delete [] m_array;
        m_mutex.unlock();
    }

    void clear()
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }
    bool full()
    {
        m_mutex.lock();
        if(m_size >= m_max_size){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    bool empty()
    {
        m_mutex.lock();
        if(m_size == 0){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    // front/back，以传参形式获取任务
    bool front(T& value)
    {
        m_mutex.lock();
        if(m_size == 0){
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    bool back(T& value)
    {
        m_mutex.lock();
        if(m_size == 0){
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }
    int size()
    {
        int tmp = 0;
        m_mutex.lock();     
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }
    int max_size()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

    bool push(const T& item)
    {
        m_mutex.lock();
        if(m_size >= m_max_size){
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;

        m_cond.broadcast();     // 有内容加入，广播告知消费者们去竞争消费
        m_mutex.unlock();
        return true;
    } 

    bool pop(T& item)      // 弹出首元素，并将下一个元素存入item
    {
        m_mutex.lock();
        
        while(m_size <= 0){
            if(!m_cond.wait(m_mutex.get())){ 
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }
    // 增加了超时处理（毫秒）
    bool pop(T& item, int ms_timeout)
    {
        struct timespec t = {0, 0};     // (秒，纳秒)
        struct timeval now = {0, 0};    // (秒，微秒)
        gettimeofday(&now, NULL);
        
        m_mutex.lock();
        if(m_size <= 0){
            t.tv_sec = now.tv_sec + ms_timeout / 1000;  // 时延秒
            t.tv_nsec = (ms_timeout % 1000) * 1000;     // 时延纳秒
            if(!m_cond.timewait(m_mutex.get(), t)){     // 开启超时阻塞
                m_mutex.unlock();
                return false;
            }
        }

        if(m_size <= 0){
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    } 

private:
    locker m_mutex;
    cond m_cond;

    T* m_array;         // 数组模拟队列，定义为指针，便于new时指定内存大小
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

#endif