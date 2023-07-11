#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

// 日志类（单例）
class Log
{
public:
    // C++11以后，使用局部变量懒汉不用加锁
    static Log* get_instance();
    // 创建线程异步写日志时的回调函数
    static void* flush_log_thread(void *args);
    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char* file_name, int close_log, int log_buf_size = 8192, 
            int split_lines = 5000000, int max_queue_size = 0);
    
    void write_log(int level, const char* format, ...);
    void flush(void);           // 强制刷新写入流缓冲区

private:
    Log();
    virtual ~Log();
    void *async_write_log();    // 从队列中取出任务异步写日志

    char dir_name[128];     // 路径名
    char log_name[128];     // log文件名
    int m_split_lines;      // 日志最大行数
    int m_log_buf_size;     // 日志缓冲区大小
    long long m_count;      // 日志行数记录
    int m_today;            // 当前是哪一天（日志按天分类）
    FILE* m_fp;             // 打开log的文件指针
    char* m_buf;
    block_queue<string>* m_log_queue;   // 阻塞队列
    bool m_is_async;        // 是否异步标志位
    locker m_mutex;         // 互斥锁
    int m_close_log;        // 关闭日志
};

#define LOG_DEBUG(format, ...)\
    if(0 == m_close_log) {\
        Log::get_instance()->write_log(0, format, ##__VA_ARGS__);\
        Log::get_instance()->flush();\
    }
#define LOG_INFO(format, ...)\
    if(0 == m_close_log) {\
        Log::get_instance()->write_log(1, format, ##__VA_ARGS__);\
        Log::get_instance()->flush();\
    }
#define LOG_WARN(format, ...)\
    if(0 == m_close_log) {\
        Log::get_instance()->write_log(2, format, ##__VA_ARGS__);\
        Log::get_instance()->flush();\
    }
#define LOG_ERROR(format, ...)\
    if(0 == m_close_log) {\
        Log::get_instance()->write_log(3, format, ##__VA_ARGS__);\
        Log::get_instance()->flush();\
    }

#endif