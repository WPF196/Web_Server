
校验 & 数据库连接池
===============
### 数据库连接池
原理：事先创建多个可用的数据库连接（连到目标数据库，需要时取用即可）
> * 单例模式，保证唯一
> * list实现连接池
> * 连接池为静态大小
> * 互斥锁实现线程安全

### 校验  
> * HTTP请求采用POST方式
> * 登录用户名和密码校验
> * 用户注册及多线程注册安全 

### 单个数据库连接的生成过程
>1. `mysql_init()` 初始化连接
>2. `mysql_real_connect()` 建立一个到mysql数据库的连接
>3. `mysql_query()` 执行查询语句
>4. `result = mysql_store_result(mysql)` 获取结果集
>5. `mysql_num_fields(result)` 获取查询的列数，`mysql_num_rows(result)` 获取结果集的行数
>6. `mysql_fetch_row(result)` 不断获取下一行，然后循环输出
>7. `mysql_free_result(result)` 释放结果集所占内存
>8. `mysql_close(conn)` 关闭连接