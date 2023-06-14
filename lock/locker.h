#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 信号量类
class sem
{
public:
    sem();
    sem(int num);
    ~sem();

    bool wait();    // 信号量-1
    bool post();    // 信号量+1

private:
    sem_t m_sem;
};


// 互斥锁类
class locker
{
public:
    locker();
    ~locker();

    bool lock();
    bool unlock();
    pthread_mutex_t *get();  // 获取互斥锁变量的地址，从而操作锁本体

private:
    pthread_mutex_t m_mutex;
};


// 条件变量类（常与互斥量一起使用）
class cond
{
public:
    cond();
    ~cond();

    bool wait(pthread_mutex_t *m_mutex);
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t);
    bool signal();      // 唤醒一个线程（阻塞队列顺序唤醒）
    bool broadcast();   // 唤醒所有被阻塞的线程

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif