#include "webserver.h"

// 主要完成服务器初始化：http连接、设置根目录、开启定时器对象
WebServer::WebServer()
{
    // http_conn类对象
    users = new http_conn[MAX_FD];

    // 获取root路径 m_root
    char server_path[200];
    getcwd(server_path, 200);   // 将当前工作目录的绝对路径复制到server_path
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, 
                    int log_write, int opt_linger, int trigmode, int sql_num, 
                    int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode()
{
    // LT + LT
    if(m_TRIGMode == 0){
        m_LISTENTrigmode = 0;
        m_CONNTringmode = 0;
    }
    // LT + ET
    else if(m_TRIGMode == 1){
        m_LISTENTrigmode = 0;
        m_CONNTringmode = 1;
    }
    // ET + LT
    else if(m_TRIGMode == 2){
        m_LISTENTrigmode = 1;
        m_CONNTringmode = 0;
    }
    else if(m_TRIGMode == 3){
        m_LISTENTrigmode = 1;
        m_CONNTringmode = 1;
    }
}

void WebServer::log_write()
{
    // 不关闭日志
    if(m_close_log == 0){
        // 初始化日志
        if (1 == m_log_write)   // 异步写  缓冲区大小2000 最大行数800000 等待队列800
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else    // 同步写
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    // 线程池（元素类型为http_conn）
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if(m_OPT_LINGER == 0){
        struct linger tmp = {0, 1}; // l_onoff=0, l_linger!=0，默认close
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if(m_OPT_LINGER == 1){     
        struct linger tmp = {1, 1}; // l_onoff!=0, l_linger!=0，进程进入睡眠，内核在定时间内发送残余数据
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;       // 设置地址复用（取消TIME_WAIT）
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof flag);
    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof address);
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    // 设置服务器的最小时间间隙
    utils.init(TIMESLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != 1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    // 设置全双工套接字组（协议族，协议，类型(只能为0)，套接字柄对）
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN); // 当一个进程向某个已收到RST的套接字执行写操作时，内核向该进程发送SIGPIPE信号
    utils.addsig(SIGALRM, utils.sig_handler, false);    // 被信号打断的系统调用不重启
    utils.addsig(SIGTERM, utils.sig_handler, false);    // 当前线程被kill，产生SIGTERM信号

    alarm(TIMESLOT);

    // 工具类全局变量：信号和描述符的基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTringmode, 
                       m_close_log, m_user, m_passWord, m_databaseName);
    
    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    // 回调函数，进行http的删除操作
    timer->cb_func(&users_timer[sockfd]);
    if(timer)
        utils.m_timer_lst.del_timer(timer);

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    
    // LT
    if(m_LISTENTrigmode == 0){
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if(connfd < 0){
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if(http_conn::m_user_count >= MAX_FD){
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    // ET
    else{
        // 边沿触发需要一直读到空
        while(1){
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
            if(connfd < 0){
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if(http_conn::m_user_count >= MAX_FD){
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if(ret <= 0)
        return false;
    else{
        for(int i = 0; i < ret; ++i){
            switch(signals[i]){
            case SIGALRM:{      // 定时时间到
                timeout = true;
                break;
            } 
            case SIGTERM:{      // 当前线程被kill
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    // 创建定时器临时变量，将该连接对应的定时器取出来
    util_timer *timer = users_timer[sockfd].timer;

    // reactor
    if(m_actormodel == 1){
        // 主线程只加入timer和事件，不处理IO
        if(timer)
            adjust_timer(timer);
        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);  // users+sockfd，找到users数组的sockfd偏移处
        
        // 循环等待，直到数据被处理完
        while(true){
            // read操作已经完成
            if(users[sockfd].improv == 1){
                // read操作出现问题
                if(users[sockfd].timer_flag == 1){
                    deal_timer(timer, sockfd);      // 删除sockfd的http
                    users[sockfd].timer_flag = 0;   // 重新打开连接
                }
                users[sockfd].improv = 0; 
                break;
            }
        }
    }
    // proactor
    else{
        // 先读取数据，再放进请求队列（IO操作交给主线程）
        if(users[sockfd].read_once()){
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 若检测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if(timer)
                adjust_timer(timer);
        }
        else
            deal_timer(timer, sockfd);
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // reactor
    if(m_actormodel == 1){
        if(timer)
            adjust_timer(timer);
        m_pool->append(users + sockfd, 1);

        while(true){
            if(users[sockfd].improv == 1){
                if(users[sockfd].timer_flag == 1){
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    // proactor
    else{
        if(users[sockfd].write()){
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
                adjust_timer(timer);
        }
        else
            deal_timer(timer, sockfd);
    }
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while(!stop_server){
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        /**
         * EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
         * 例如：在socket服务器端，设置了信号捕获机制，有子进程，
         * 当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)。
         * 在epoll_wait时，因为设置了alarm定时触发警告，导致每次返回-1，errno为EINTR，对于这种错误返回
         * 忽略这种错误，让epoll报错误号为4时，再次做一次epoll_wait
        */ 
        if(number < 0 && errno != EINTR){
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for(int i = 0; i < number; ++i){
            int sockfd = events[i].data.fd;

            // 处理新客户的连接
            if(sockfd == m_listenfd){
                bool flag = dealclinetdata(); // 数据是否成功处理
                if(flag = false)
                    continue;
            }
            // 如果（客户端 &（读关闭 | 读写都关闭 | 系统错误） ）
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号（客户端读口 & 读事件）
            else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)){
                bool flag = dealwithsignal(timeout, stop_server);   // 是否成功处理信号
                if(flag == false)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理客户连接上接收到的数据
            else if(events[i].events & EPOLLIN)
                dealwithread(sockfd);
            else if(events[i].events & EPOLLOUT)
                dealwithwrite(sockfd);
        }

        // 处理定时器为非必须事件，收到信号并不是立马处理
        // 完成读写事件后，再进行处理
        if(timeout){
            utils.timer_handler();
            
            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}