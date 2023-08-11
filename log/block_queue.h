/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

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
    block_queue(int max_size = 1000)   // 默认消息队列长为1000
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

    void clear()           // 清空
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }
    bool full()            // 判满
    {
        m_mutex.lock();
        if(m_size >= m_max_size){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    bool empty()           // 判空
    {
        m_mutex.lock();
        if(m_size == 0){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    bool front(T& value)   // 获取队首元素，存于value
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
    bool back(T& value)    // 获取队尾元素，存于value
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
    int size()             // 获取size 
    {
        int tmp = 0;
        m_mutex.lock();     
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }
    int max_size()         // 获取max_size
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

    /* 往队列添加元素，需要将所有使用队列的线程先唤醒
     * 当有元素push进队列,相当于生产者生产了一个元素
     * 若当前没有线程等待条件变量,则唤醒无意义 */
    bool push(const T& item)
    {
        m_mutex.lock();
        if(m_size >= m_max_size){   // 超出队列最大容量
            m_cond.broadcast();     // 唤醒所有使用队列的线程来消费
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;     //尾插
        m_array[m_back] = item;
        m_size++;

        m_cond.broadcast();         // 有内容加入，告知消费者们去消费
        m_mutex.unlock();
        return true;
    } 
    // pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T& item)      // 弹出元素，并存于item
    {
        m_mutex.lock();
        
        // 当队列中没有元素可供消费时，需要等待生产者线程将元素放入队列
        // 使用 while 循环而不是 if，以防止虚假唤醒（spurious wake-up）
        // 虚假唤醒：当唤醒多个卡住的生产者，可能导致a要用的资源已经被b拿走了
        // 如果使用while，被唤醒的生产者会循环回来进行校验，a会重新进入等待态
        while(m_size <= 0){
            // 当前线程等待条件变量，自动释放互斥锁(m_mutex.unlock()) 并进入阻塞状态，此时生产者可以调用互斥锁
            // 在被唤醒之前，其他线程必须调用 pthread_cond_signal 或 pthread_cond_broadcast
            // 来通知有数据可供消费，唤醒等待的消费者线程
            if(!m_cond.wait(m_mutex.get())){    // wait错误
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;   // 头出
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
        gettimeofday(&now, NULL);       // 获取当前时间，用tv结构返回给now
        
        m_mutex.lock();
        if(m_size <= 0){                // 没有内容可消费
            t.tv_sec = now.tv_sec + ms_timeout / 1000;  // 时延秒
            t.tv_nsec = (ms_timeout % 1000) * 1000;     // 时延纳秒
            if(!m_cond.timewait(m_mutex.get(), t)){       // 开启超时阻塞
                m_mutex.unlock();
                return false;
            }
        }

        if(m_size <= 0){            // 队列已空
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

    T* m_array;         // 数组模拟队列
    int m_size;         // 队列中当前元素数量
    int m_max_size;     // 阻塞队列最大容量
    int m_front;        // 前驱
    int m_back;         // 后继
};

#endif