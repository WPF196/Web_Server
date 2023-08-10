#include "config.h"

Config::Config()
{
    PORT = 9006;
    LOGWrite = 0;       // 日志写入方式：同步
    TRIGMode = 0;       // LT + LT
    LISTENTrigmode = 0; // LT
    CONNTrigmode = 0;   // LT
    OPT_LINGER = 0;     // 不优雅断开
    sql_num = 8;
    thread_num = 8;
    close_log = 0;      // 不关闭日志
    actor_model = 0;    // poractor
}

void Config::parse_arg(int argc, char*argv[]){
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            PORT = atoi(optarg);
            break;
        }
        case 'l':
        {
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':
        {
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':
        {
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}