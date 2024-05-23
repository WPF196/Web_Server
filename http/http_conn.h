#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    //报文的请求方法，本项目只用到GET和POST
    enum METHOD{ GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATH };
    
    // 报文解析结果
    enum HTTP_CODE{
        NO_REQUEST,             // 请求不完整，需要继续读取请求报文数据（跳转主线程继续监测读事件）
        GET_REQUEST,            // 获取了完整的HTTP请求（调用do_request完成请求资源映射）
        BAD_REQUEST,            // HTTP请求报文有语法错误或请求资源为目录（跳转process_write完成响应报文）
        NO_RESOURCE,            // 请求资源不存在（跳转process_write完成响应报文）
        FORBIDDEN_REQUEST,      // 请求资源禁止访问，没有读取权限（跳转process_write完成响应报文）
        FILE_REQUEST,           // 请求资源可以正常访问（跳转process_write完成响应报文）
        INTERNAL_ERROR,         // 服务器内部错误
        CLOSED_CONNECTION       // 关闭连接
    };
    // 主状态机的状态（CHECK）
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0,    // 解析请求行
        CHECK_STATE_HEADER,             // 解析请求头
        CHECK_STATE_CONTENT             // 解析消息体（仅用于解析POST请求）
    };
    // 从状态机的状态（LINE）
    enum LINE_STATUS{
        LINE_OK = 0,        // 完整读取一行
        LINE_BAD,           // 报文语法有误
        LINE_OPEN           // 读取的行不完整
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char*, int, 
                int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true);
    void process();     // 解析与响应报文
    bool read_once();   // 读取数据
    bool write();       // 响应报文写入函数
    sockaddr_in *get_address(){ return &m_address; }
    void initmysql_result(connection_pool *connPool);  // 获取数据库连接，同时获取用户信息
    int timer_flag;     // 读写是否超时：1是，0否
    int improv;         // 读写是否完成：1是，0否

private:
    void init();
    
    HTTP_CODE process_read();                 // 从 m_read_buf 读取，并处理请求报文
    bool process_write(HTTP_CODE ret);        // 生成响应报文，并写入 m_write_buf 
    
    HTTP_CODE parse_request_line(char *test); // 解析报文中的请求行数据（返回主状态机）
    HTTP_CODE parse_headers(char *test);      // 解析报文中的请求头数据（返回主状态机）
    HTTP_CODE parse_content(char *test);      // 解析报文中的请求内容（返回主状态机）
    HTTP_CODE do_request();                   // 生成响应报文

    // 用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();   // 返回行的读取状态（从状态机）
    void unmap();       // 解除m_file_address的映射关系

    // 根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);  // 添加文本content
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();    // 添加文本类型
    bool add_content_length(int content_length);
    bool add_linger();      // 添加连接状态，通知浏览器端是保持连接还是关闭
    bool add_blank_line();  // 添加空行

public:
    static int m_epollfd;       // epoll实例
    static int m_user_count;    // 用户数量
    MYSQL *mysql;
    int m_state;  // IO事件：读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];  // 存储读取的请求报文数据
    long m_read_idx;            // 缓冲区m_read_buf数据的最后一个字节的下一个位置（即m_read_buf元素数量）
    long m_checked_idx;         // m_read_buf 读取的位置 m_checked_idx
    int m_start_line;           // m_read_buf 中已经解析的字符个数

    char m_write_buf[WRITE_BUFFER_SIZE];// 存储发出的响应报文数据   
    int m_write_idx;            // 指示buffer中的长度
    
    CHECK_STATE m_check_state;  // 主状态机的状态
    METHOD m_method;            // 请求方法
    
    // 以下为解析请求报文中对应的6个变量
    char m_real_file[FILENAME_LEN]; // 存储读取文件的名称
    char *m_url;
    char *m_version;
    char *m_host;
    long m_content_length;
    bool m_linger;  // 是否开启keep_alive（如果开启keep-alive，则服务端在返回response后不关闭TCP连接）

    char *m_file_address;       // 读取服务器上的文件地址（mmap映射）
    struct stat m_file_stat;
    struct iovec m_iv[2];       // io向量机制iovec（iov_base指向数据地址，iov_len表示数据长度）
    int m_iv_count;             // iovec结构体个数
    int cgi;            // 是否启用的POST（1为POST）
    char *m_string;     // 存储请求头数据
    int bytes_to_send;  // 剩余发送字节数
    int bytes_have_send;// 已发送字节数
    char *doc_root;     // 网站根目录

    map<string, string> m_users;
    int m_TRIGMode;     // 触发模式（0水平、1边沿）
    int m_close_log;    // 日志开关

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif