#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

// 数据库连接池类（单例）
class connection_pool
{
public:
    MYSQL* GetConnection();
    bool ReleaseConnection(MYSQL* con);    
    int GetFreeConn();    
    void DestoryPool();

    static connection_pool* GetInstance();

    void init(string url, string User, string PassWord, string DBName, 
                int Port, int MaxConn, int close_log); 

private:
    connection_pool();
    ~connection_pool();

    int m_MaxConn;
    int m_CurConn;
    int m_FreeConn;
    locker lock;
    list<MYSQL*> connList;
    sem reserve;            // 空闲的数据库连接

public:
    string m_url;
    string m_Port;
    string m_User;
    string m_PassWord;
    string m_DatabaseName;
    int m_close_log;
};

// 将 数据库连接（池中资源） 的获取与释放通过RAII机制封装，避免手动释放。
// RAII：构造时初始化，析构时释放
class connectionRAII
{
public:
    connectionRAII(MYSQL** con, connection_pool* connPool);
    ~connectionRAII();

private:
    MYSQL* conRAII;             // 从数据库连接池中获取的连接
    connection_pool* poolRAII;  // 数据库连接池
};

#endif