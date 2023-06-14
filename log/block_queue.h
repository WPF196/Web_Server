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
    block_queue(int max_size = 1000);
    ~block_queue();

    void clear();           // 清空
    bool full();            // 判满
    bool empty();           // 判空
    bool front(T& value);   // 获取队首元素，存于value
    bool back(T& value);    // 获取队尾元素，存于value
    int size();             
    int max_size();
     
    /* 往队列添加元素，需要将所有使用队列的线程先唤醒
     * 当有元素push进队列,相当于生产者生产了一个元素
     * 若当前没有线程等待条件变量,则唤醒无意义 */
    bool push(const T& item);  
    // pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T& item);      // 弹出元素，并存于item
    // 增加了超时处理（毫秒）
    bool pop(T& item, int ms_timeout);  

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