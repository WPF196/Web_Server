#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <pthread.h>

#include "log.h"

using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;     // 默认同步
}

Log::~Log()
{   
    if(m_fp != NULL)
        fclose(m_fp);
}

Log* Log::get_instance()
{
    static Log instance;
    return &instance;
}

void* Log::flush_log_thread(void *args)
{
    Log::get_instance()->async_write_log();
}

bool Log::init(const char* file_name, int close_log, int log_buf_size, 
            int split_lines, int max_queue_size)
{   
    // 异步
    if(max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        // 创建线程，异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char* p = strrchr(file_name, '/');    // 指向file_name最后出现'/'的位置
    char log_full_name[256] = {0};

    // 创建文件（新文件名=时间）
    if (p == NULL){
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", 
                my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }else{
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, 
                my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    
    m_fp = fopen(log_full_name, "a");   // 追加
    if (m_fp == NULL){
        return false;
    }

    return true;
}

void Log::write_log(int level, const char* format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};               // 存储日志等级

    // 日志等级 debug -> info -> warn -> error，默认info
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    m_mutex.lock();
    m_count++;   // 写一个log，另起一行

    // 时间（天）改变 || 文件已经写满
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0){
        char new_log[256] = {0};    
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, 
                    my_tm.tm_mon + 1, my_tm.tm_mday);

        if(m_today != my_tm.tm_mday){
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }else{  // 写满的话，则添加后缀，表示是当天第几个文件
            snprintf(new_log, 255, "%s%s%s.%lld", 
                    dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    m_mutex.unlock();

    // 不定参数
    va_list valst;
    va_start(valst, format);    // 指向可变参数列表

    string log_str;
    m_mutex.lock();
    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    // 按照 format 格式，将内容写到 m_buf 尾部，并将占位符置换为可变参数 valst
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    // 异步
    if (m_is_async && !m_log_queue->full()){
        m_log_queue->push(log_str);
    }
    else{   // 同步
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);      
}

void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}

void *Log::async_write_log()
{
    string single_log;
    
    while(m_log_queue->pop(single_log)){
        m_mutex.lock();
        fputs(single_log.c_str(), m_fp);
        m_mutex.unlock();
    }
}