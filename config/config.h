#ifndef CONFIG_H
#define CONFIG_H

#include "./webserver.h"

using namespace std;

class Config
{
public:
    Config();
    ~Config(){};

    // 解析命令行参数
    void parse_arg(int argc, char *argv[]);

    int PORT;               // 端口号
    int LOGWrite;           // 日志写入方式
    int TRIGMode;           // 触发组合模式
    int LISTENTrigmode;     // Listenfd触发模式
    int CONNTrigmode;       // connfd触发模式
    int OPT_LINGER;         // 优雅关闭连接
    int sql_num;            // 数据库连接池数量
    int thread_num;         // 线程池内线程数量
    int close_log;          // 是否关闭日志
    int actor_model;        // 并发模型选择
};

#endif