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

// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char* file_name, int close_log, int log_buf_size, 
            int split_lines, int max_queue_size)
{   
    // 如果设置了max_queue_size，则设置为异步
    if(max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        // 创建线程调用flush_log_thread，异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);                      // 获取本机时间
    struct tm* sys_tm = localtime(&t);          // 时间结构体指向本机时间变量
    struct tm my_tm = *sys_tm;                  // 解引用

    const char* p = strrchr(file_name, '/');    // 返回file_name最后出现'/'的位置
    char log_full_name[256] = {0};

    // 给文件命名（即创建文件）
    if (p == NULL){     // 说明最后以'/'结尾，即路径是个目录，新文件名=时间
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", 
                my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }else{      // 说明是个具体文件，新文件名=传入文件名+时间
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, 
                my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    
    m_fp = fopen(log_full_name, "a");   // 以追加方式打开日志文件
    if (m_fp == NULL){
        return false;
    }

    return true;
}

void Log::write_log(int level, const char* format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);       // 获取精确时间，精确到微秒
    time_t t = now.tv_sec;          // 获取s，通过s转换得到具体时间
    struct tm* sys_tm = localtime(&t);  // 强制类型转换，返回tm*
    struct tm my_tm = *sys_tm;      // 解引用，获取当前时间
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

    // 开始写一个log，另起一行
    m_mutex.lock();
    m_count++;

    // 如果时间（天）改变 || 文件已经写满，则开新文件
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0){
        char new_log[256] = {0};    
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        // tail存储时间，用来作为新日志名称的一部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, 
                    my_tm.tm_mon + 1, my_tm.tm_mday);

        // 新一天直接创建
        if(m_today != my_tm.tm_mday){
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }else{  // 写满的话，则添加后缀，表示是当天第几个文件
            snprintf(new_log, 255, "%s%s%s.%lld", 
                    dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");     // 打开新日志
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

    // 如果是异步 且 队列未满，将任务加入阻塞队列
    if (m_is_async && !m_log_queue->full()){
        m_log_queue->push(log_str);
    }
    else{   // 同步 || 队列已满，则直接写，不用入队
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);      
}

void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp);       // 将内容从 buf 冲到 m_fp 中
    m_mutex.unlock();
}

void *Log::async_write_log()
{
    string single_log;
    // 从阻塞队列中取出一个日志string，写入文件
    while(m_log_queue->pop(single_log)){
        m_mutex.lock();
        fputs(single_log.c_str(), m_fp);
        m_mutex.unlock();
    }
}