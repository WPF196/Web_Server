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
map<string, string> users;      // <name，password>

void http_conn::initmysql_result(connection_pool *connPool)
{
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    if (mysql_query(mysql, "SELECT username,passwd FROM user")){
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    MYSQL_RES *result = mysql_store_result(mysql);

    // 从结果集中取出所有结果，将对应的用户名和密码，存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 对文件描述符设置非阻塞
// 当无数据可读 || 不能立即写入，直接返回错误而不是阻塞等待
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK; 
    fcntl(fd, F_SETFL, new_option); 
    return old_option;
}

// 注册 fd 到 epollfd
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1)   // ET
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  // EPOLLRDHUP：高校检测对方是否关闭连接
    else 
        event.events = EPOLLIN | EPOLLRDHUP;
    
    if(one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd); 
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改 fd 事件类型
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

// 关闭一个连接
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

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    // 获取用户数据
    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

// 初始化新接收的连接（check_state默认为分析请求行状态）
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;    // 解析行
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

// 返回值为行的读取状态（只做状态判断，不进行解析）
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx){
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r'){   // 回车    
            if((m_checked_idx + 1) == m_read_idx) 
                return LINE_OPEN;
            else if(m_read_buf[m_checked_idx + 1] == '\n'){  
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

bool http_conn::read_once()
{   
    // 空间溢出
    if(m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;

    // 1. LT读取数据
    if(m_TRIGMode == 0){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if(bytes_read <= 0)
            return false;
        
        return true;
    }
    // 2. ET读取数据
    else{
        while(true){    // 一次性读完
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
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
    // 在第一个空格处匹配，如 GET /abcd.jpg HTTP/1.1
    m_url = strpbrk(text, " \t");   // 返回 "/abcd.jpg HTTP/1.1"
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
    
    // url是http请求
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    // url是https请求
    if(strncasecmp(m_url, "https://", 8) == 0){
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    // 没有url或者url不是 /
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
    // 解析到了空行
    if(text[0] == '\0'){    
        if(m_content_length != 0){     // 说明有消息体，是POST
            m_check_state = CHECK_STATE_CONTENT;    // 标记解析消息体
            return NO_REQUEST;
        }
        // 解析完了
        return GET_REQUEST;
    }
    // 解析到了Connection
    else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
            m_linger = true;
    }
    // 解析到了Content-length
    else if(strncasecmp(text, "Content-length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    // 解析到了Host
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
    // 判断buffer是否读到了消息头（即已经读完了请求行和请求头）
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

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/');    // 获取最后的'/xxx'

    // 处理cgi      2登录 3注册
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')){
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        // 获取文件具体路径
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // m_string：name=xxxx&passwd=Wiley
        char name[100], password[100];
        int i;
        for(i = 5; m_string[i] != '&'; ++i)     // i=5，过滤掉"name="，下方 i=i+10 同理
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for(i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        // 注册
        if(*(p + 1) == '3'){
            // 先检测数据库中是否有重名的。没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 没有重名的
            if(users.find(name) == users.end()){
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if(!res)    // 数据插入成功
                    strcpy(m_url, "/log.html");
                else        // 数据插入失败
                    strcpy(m_url, "/registerError.html");
            }
            else    // 重名了
                strcpy(m_url, "/registerError.html");
        }
        // 登录
        else if(*(p + 1) == '2'){
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    // 跳转注册界面
    if(*(p + 1) == '0'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 跳转登录界面
    else if(*(p + 1) == '1'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 图片显示界面
    else if(*(p + 1) == '5'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 视频显示界面
    else if(*(p + 1) == '6'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 微信公众号界面
    else if(*(p + 1) == '7'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 直接将url与网站目录拼接，这里的情况是welcome界面
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 通过stat获取请求资源文件信息，存于m_file_stat
    if(stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    // 判断文件的权限 是否可读
    if(!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;   // 不可读返回禁止请求状态

    // 判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if(S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    // 以只读方式获取文件描述符
    int fd = open(m_real_file, O_RDONLY);
    // 通过mmap将该文件映射到内存中
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;
    // 报文响应为空，一般不会出现此情况
    if(bytes_to_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while(1){
        // 将 m_iv_count 个非连续缓冲区 m_iv 中的内容写到 m_sockfd
        temp = writev(m_sockfd, m_iv, m_iv_count);

        // 写入失败
        if(temp < 0){
            if(errno == EAGAIN){   // 缓冲区满了
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);   // 此文件描述符仍然可写
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        // 第一个iovec头部信息的数据已发送完，发送第二个iovec数据
        if(bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        // 继续发送第一个iovec头部信息的数据
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        // 数据已全部发送完
        if(bytes_to_send <= 0){
            unmap();

            // 在epoll树上重置EPOLLONESHOT事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            // 浏览器的请求为长连接
            if(m_linger){
                init();     // 重新初始化HTTP对象
                return true;
            }
            else
                return false;
        }
    }
}

bool http_conn::add_response(const char *format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE)
        return false;

    va_list arg_list;       // 定义可变参数列表
    va_start(arg_list, format); // 将变量arg_list初始化为传入参数

    // 将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)){
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret){
    case INTERNAL_ERROR:{
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form))
            return false;
        break;
    }    
    case BAD_REQUEST:{
        add_status_line(404, error_404_form);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form));
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:{
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:{
        add_status_line(200, ok_200_title);
        if(m_file_stat.st_size != 0){
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else{
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if(!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);
    if(!write_ret)
        close_conn();
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}