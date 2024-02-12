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
    static void* flush_log_thread(void *args);

    // 日志文件名、日志关闭标记、日志缓冲区大小、最大行数、最长日志条队列
    bool init(const char* file_name, int close_log, int log_buf_size = 8192, 
            int split_lines = 5000000, int max_queue_size = 0);
    
    void write_log(int level, const char* format, ...);
    void flush(void);

private:
    Log();
    virtual ~Log();
    void *async_write_log();

    char dir_name[128];
    char log_name[128];
    int m_split_lines;      // 日志最大行数
    int m_log_buf_size;     // 日志缓冲区大小
    long long m_count;      // 日志行数记录
    int m_today;            // 当前是哪一天（日志按天分类）
    FILE* m_fp;             // 打开log的文件指针（某时刻只开一个文件）
    char* m_buf;
    block_queue<string>* m_log_queue;
    bool m_is_async;
    locker m_mutex;
    int m_close_log;
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