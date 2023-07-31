#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;      // 用户名 密码

void http_conn::initmysql_result(connection_pool *connPool)
{
    // 先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user")){
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);    // 获取fd的文件标记
    int new_option = old_option | O_NONBLOCK;   // 标记指定为非阻塞
    fcntl(fd, F_SETFL, new_option);         // 设置fd的文件标记
    return old_option;
}

// 将内核事件表注册读事件（ET模式，选择开启EPOLLONESHOT（防止多线程处理一个文件描述符））
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1)   // 边沿
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  // EPOLLRDHUP属性用于判断对端是否已经关闭
    else 
        event.events = EPOLLIN | EPOLLRDHUP;
    
    if(one_shot)        // 如果开启EPOLLONESHOT
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);      // 注册fd到epoll树
    setnonblocking(fd); // 设为非阻塞
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接（关闭一个连接，客户总量减一）
void http_conn::close_conn(bool real_close)
{
    if(real_close && (m_sockfd != -1)){
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, 
                    int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    // 将文件描述符加入epoll树
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    // 获取用户数据
    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

// 初始化新接收的连接
// check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 验证是否能完整解析后续的一行（只验证，不解析）
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx){
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r'){   // 回车    
            if((m_checked_idx + 1) == m_read_idx)   // 非完整一行
                return LINE_OPEN;
            else if(m_read_buf[m_checked_idx + 1] == '\n'){     // 读取了完整的一行
                // 将末尾的回车换行都用'\0'都替
                m_read_buf[m_checked_idx++] = '\0'; 
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n'){  // 换行
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r'){
                // 将末尾的回车换行都用'\0'都替
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性读完数据
bool http_conn::read_once()
{   
    // 空间溢出
    if(m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;

    // 1. LT读取数据
    if(m_TRIGMode == 0){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, 
                        READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if(bytes_read <= 0)
            return false;
        
        return true;
    }
    // 2. ET读取数据
    else{
        while(true){    // 边沿必须立马读取，因此在死循环中
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                            READ_BUFFER_SIZE - m_read_idx, 0);
            if(bytes_read == -1){
                /* 写数据时，若一次发送的数据超过TCP发送缓冲区，
                * 则返EAGAIN/EWOULDBLOCK，表示数据没用发送完，
                * 一定要继续注册检测可写事件，否则剩余的数据就再也没有机会发送了，
                * 因为 ET 模式的可写事件再也不会触发。 */
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if(bytes_read == 0)
                return false;
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 解析http请求行，获得请求方法，目标url及http版本号 （报文格式看readme）
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 返回text中第一个 和 字符串" \t"有相同字符匹配的地方
    m_url = strpbrk(text, " \t");   // 返回除HTTP协议版本之后的部分
    if(!m_url)
        return BAD_REQUEST;
    
    *m_url++ = '\0';    // 将前方空格或制表符置为结束符，同时后移指向正文
    char *method = text;  // 由于上方'\0'截断text，text="GET"或者"POST"   
    if(strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if(strcasecmp(method, "POST") == 0){
        m_method = POST;
        cgi = 1;        // 标志：启用POST
    }
    else return BAD_REQUEST;

    // 检索字符串m_rul中第一个不在字符串" \t"中出现的字符下标
    m_url += strspn(m_url, " \t");     // 即返回前驱无" \t"的正文
    m_version = strpbrk(m_url, " \t"); // 返回m_url中第一个匹配" \t"的字符

    if(!m_version)  return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");    // 解析出HTTP版本
    if(strcasecmp(m_version, "HTTP/1.1") != 0)  return BAD_REQUEST;
    
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(strncasecmp(m_url, "https://", 8) == 0){
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/')   
        return BAD_REQUEST;
    // 当url为/时，显示欢迎界面
    if(strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    // 请求行处理完毕，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{       
    if(text[0] == '\0'){    
        if(m_content_length != 0){     // 说明有消息体，是POST
            m_check_state = CHECK_STATE_CONTENT;    // 标记解析消息体
            return NO_REQUEST;
        }
        // 解析完了
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
            m_linger = true;
    }
    else if(strncasecmp(text, "Content-length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
        LOG_INFO("oop!unknow header: %s", text);
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{   
    // 判断buffer中是否读取了消息体（即消息头是否读完）
    if(m_read_idx >= (m_content_length + m_checked_idx)){
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;    // 获取消息体
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{   
    // 初始化从状态机状态、HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // 当 ((解析到了消息体 && 可以解析到完整的一行) || (下一行的解析结果==LINE_OK))
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) 
            || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();      // text指向未解析的部分
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);

        // 主状态机的三种状态转移逻辑
        switch(m_check_state){ 
            case CHECK_STATE_REQUESTLINE:{  // 解析请求行
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:{       // 解析请求头
                ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;

                // 完整头部解析请求后，跳转到报文响应函数
                else if(ret == GET_REQUEST) 
                    return do_request();
                
                break;
            }
            case CHECK_STATE_CONTENT:{      // 解析消息体
                ret = parse_content(text);
                // 完整解析POST请求后，跳转到报文响应函数
                if(ret == GET_REQUEST)  
                    return do_request();

                // 解析完消息体即完成报文解析，避免再次进入循环，更新line_status
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
            }
    }
    return NO_REQUEST;
}