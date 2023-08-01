#include "lst_timer.h"

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    // 循环清空链表
    while(tmp){
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if(!timer)  return;
    if(!head){
        head = tail = timer;
        return;
    }

    // 当前节点超时时长小于头节点超时时长，头插
    if(timer->expire < head->expire){
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    // 否则调用私有成员，调整内部结点
    add_timer(timer, head);
}

void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if(!timer)  return;
    
    // 被调整的定时器在链表尾部 || 定时器超时值仍然小于下一个定时器超时值
    if(!timer->next || (timer->expire < timer->next->expire))
        return;

    // 被调整定时器是链表头结点，将定时器取出，重新插入
    if(timer == head){
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    // 被调整定时器在内部，将定时器取出，重新插入
    else{
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer *timer)
{
    if(!timer)  return;
    // 只有timer一个节点
    if((timer == head) && (timer == tail)){
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if(timer == head){
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if(timer == tail){
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_timer_lst::tick()
{
    if(!head)   return;

    time_t cur = time(NULL);
    util_timer *tmp = head;
    
    while(tmp){
        if(cur < tmp->expire)
            break;
        // 以下处理超出超时时长的情况
        tmp->cb_func(tmp->user_data);   // 调用回调函数，执行定时时间
        head = tmp->next;               // 处理过的定时器从链表中移除，并更新表头
        if(head)
            head->prev = NULL;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;   // 从头节点的下一个节点开始查

    // 遍历当前结点之后的链表，按照超时时间找到目标定时器对应的位置，常规双向链表插入操作
    while(tmp){
        if(timer->expire < tmp->expire){
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    // 遍历完，目标定时器需要放到尾结点处
    if(!tmp){
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}


void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

     if(one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的errno
    int save_error = errno;  // 系统变量，存储就近发生的错误，即下一次的错误码会覆盖掉上一次的错误
    int msg = sig;
    send(u_pipefd[1], (char *)msg, 1, 0);
    errno = save_error;
}

void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    // 创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof sa);

    // 信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;  // 使被信号打断的系统调用自动重新发起
    
    // 将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);
    // 执行sigaction函数
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;