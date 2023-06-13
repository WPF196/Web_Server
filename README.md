# TinyWebserver
linux下c++轻量级Web服务器
* 使用 **线程池** + **非阻塞socket** + **epoll** (ET + LT) + **事件处理**（Reactor + 模拟Proactor）的并发模型
* 使用**状态机**解析HTTP请求报文，支持解析**GET**和**POST**请求
* 访问服务器数据库实现web端用户**注册**、**登录**功能，可以请求服务器**图片**和**视频文件**
* 实现**同步/异步日志系统**，记录服务器运行状态
* 经Webbench压力测试可以实现**上万的并发连接**数据交换


[参考项目](https://github.com/qinguoyi/TinyWebServer#%E6%A6%82%E8%BF%B0)