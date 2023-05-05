/* anet.c -- Basic TCP socket stuff made a bit less boring
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fmacros.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "anet.h"

static void anetSetError(char *err, const char *fmt, ...)  /* 打印错误信息,传入错误信息和格式,可传入多个参数 */
{
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, ANET_ERR_LEN, fmt, ap);
    va_end(ap);
}

int anetNonBlock(char *err, int fd)  /* 非阻塞socket,传入错误信息和socket文件描述符 */
{
    int flags;  // 初始化标识

    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */  /* 设置非阻塞socket.注意，针对 F_GETFL 和 F_SETFL 的 fcntl(2) 无法被信号中断 */
    if ((flags = fcntl(fd, F_GETFL)) == -1) {  // F_GETFL 获取文件状态标志
        anetSetError(err, "fcntl(F_GETFL): %s\n", strerror(errno));
        return ANET_ERR;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {  // 设置文件状态标志,O_NONBLOCK为非阻塞模式
        anetSetError(err, "fcntl(F_SETFL,O_NONBLOCK): %s\n", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetTcpNoDelay(char *err, int fd)  /* 设置非延迟TCP传输 */
{
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1)  // setsockopt() 用于she'zhi与某个套接字关联的选项.开启TCP_NODELAY将禁用 Nagle's Algorithm
    {
        anetSetError(err, "setsockopt TCP_NODELAY: %s\n", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetSetSendBuffer(char *err, int fd, int buffsize)  /* 设置发送缓冲区 */
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffsize, sizeof(buffsize)) == -1)
    {
        anetSetError(err, "setsockopt SO_SNDBUF: %s\n", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetTcpKeepAlive(char *err, int fd)  /* 设置TCP连接保持 */
{
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt SO_KEEPALIVE: %s\n", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anetResolve(char *err, char *host, char *ipbuf)  /* 解析主机名,返回ip地址 */
{
    struct sockaddr_in sa; // 初始化socket地址结构体

    sa.sin_family = AF_INET;  // 地址族,AF_INET表示ipv4
    if (inet_aton(host, &sa.sin_addr) == 0) {  // inet_aton() 将主机名或ip地址转换为32位整型值存入sa.sin_addr,返回1表示成功(host为ip地址时成功)
        struct hostent *he;  // 如果转换不成功则初始化结构体(host为主机名时失败)

        he = gethostbyname(host);  // 此时host不是ip地址,尝试将host作为主机名,再次解析为32位整数
        if (he == NULL) {  // 如果结果为空
            anetSetError(err, "can't resolve: %s\n", host);  // 说明host即不是ip地址也不是主机名,无法解析
            return ANET_ERR;  // 返回error
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));  // h_addr是1个宏定义,同h_addr_list[0].将h_addr的32位整型值拷贝到sin_addr中
    }
    strcpy(ipbuf,inet_ntoa(sa.sin_addr));  // inet_ntoa() 将32位整型值转换为ip地址,存到ipbuf中
    return ANET_OK;  // 返回ok
}

#define ANET_CONNECT_NONE 0  // 阻塞标识
#define ANET_CONNECT_NONBLOCK 1  //非阻塞标识
static int anetTcpGenericConnect(char *err, char *addr, int port, int flags)  /* TCP通用连接,传入错误信息,主机名或ip地址,端口,阻塞标识 */
{
    int s, on = 1;  // 初始化socket文件描述符和缓冲区指针
    struct sockaddr_in sa;  // 初始化socket地址结构体

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {  // 创建TCP socket,SOCK_STREAM表示TCP连接
        anetSetError(err, "creating socket: %s\n", strerror(errno));
        return ANET_ERR;  // 创建失败返回error
    }
    /* Make sure connection-intensive things like the redis benckmark
     * will be able to close/open sockets a zillion of times */  /* 确保连接密集型的任务（例如Redis基准测试）能够开关套接字数十亿次 */
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));  // 告诉操作系统允许多个套接字绑定到同一个端口号,并在关闭套接字后立即释放该端口号,从而使得服务器程序更加健壮和高效.SO_REUSEADDR,允许将套接字绑定到已在使用中的地址

    sa.sin_family = AF_INET;  // AF_INET=2
    sa.sin_port = htons(port);  // 传入端口给socket结构体
    if (inet_aton(addr, &sa.sin_addr) == 0) {  // inet_aton() 将主机名或ip地址转换为32位整型值存入sa.sin_addr,返回1表示成功(host为ip地址时成功)
        struct hostent *he;  // 如果转换不成功则初始化结构体(host为主机名时失败)

        he = gethostbyname(addr);  // 此时host不是ip地址,尝试将host作为主机名,再次解析为32位整数
        if (he == NULL) {  // 如果结果为空
            anetSetError(err, "can't resolve: %s\n", addr);  // 说明host即不是ip地址也不是主机名,无法解析
            close(s);  // 关闭socket文件描述符
            return ANET_ERR;  // 返回error
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));  // h_addr是1个宏定义,同h_addr_list[0].将h_addr的32位整型值拷贝到sin_addr中
    }
    if (flags & ANET_CONNECT_NONBLOCK) {  // 如果传入标识为非阻塞标识1
        if (anetNonBlock(err,s) != ANET_OK)  // 则设置socket为非阻塞模式
            return ANET_ERR;  // 如果设置失败返回error
    }
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {  // 调用connect()连接socket
        if (errno == EINPROGRESS &&  // EINPROGRESS是一个表示非阻塞套接字正在连接过程中的错误号,意味着当前的连接请求已经启动,但是仍在进行中,尚未完成
            flags & ANET_CONNECT_NONBLOCK)
            return s;

        anetSetError(err, "connect: %s\n", strerror(errno));  // 其他错误号返回错误信息
        close(s);  // 关闭socket文件描述符
        return ANET_ERR;  // 返回error
    }
    return s;  // 连接成功,返回socket文件描述符
}

int anetTcpConnect(char *err, char *addr, int port)  /* TCP协议阻塞连接 */
{
    return anetTcpGenericConnect(err,addr,port,ANET_CONNECT_NONE);  // 传入了ANET_CONNECT_NONE阻塞标识
}

int anetTcpNonBlockConnect(char *err, char *addr, int port)  /* TCP协议非阻塞连接 */
{
    return anetTcpGenericConnect(err,addr,port,ANET_CONNECT_NONBLOCK);  // 传入了ANET_CONNECT_NONBLOCK非阻塞标识
}

/* Like read(2) but make sure 'count' is read before to return
 * (unless error or EOF condition is encountered) */  /* 类似于 read(2),但要确保在返回之前读取 'count'(除非遇到错误或 EOF 条件) */
int anetRead(int fd, char *buf, int count)  /* 从socket读取count个字节写入到缓冲区 */
{
    int nread, totlen = 0;  // 初始化单次读取到的个数和读取到的总个数
    while(totlen != count) {  // 如果读取到的总数比传入的个数少则继续读取
        nread = read(fd,buf,count-totlen);  // 单次读取到的个数
        if (nread == 0) return totlen;  // nread返回0标识已读取完
        if (nread == -1) return -1;  // 读取失败返回-1
        totlen += nread;  // 累加读取的总个数
        buf += nread;  // 后移缓冲区指针,跳过已写入的位置
    }
    return totlen;  // 返回读取到的总个数
}

/* Like write(2) but make sure 'count' is read before to return
 * (unless error is encountered) */  /* 与 write(2) 函数类似，但是它会确保在返回之前读取完 count 所指定的字节数(除非遇到错误) */
int anetWrite(int fd, char *buf, int count)  /* 从缓冲区读取count个字节写入到socket */
{
    int nwritten, totlen = 0;  // 含义与anetRead()相同
    while(totlen != count) {
        nwritten = write(fd,buf,count-totlen);
        if (nwritten == 0) return totlen;
        if (nwritten == -1) return -1;
        totlen += nwritten;
        buf += nwritten;
    }
    return totlen;
}

int anetTcpServer(char *err, int port, char *bindaddr)  /* TCP协议服务端,传入server的bind地址 */
{
    int s, on = 1;  // 初始化socket和缓冲区指针
    struct sockaddr_in sa;  // 初始化socket地址结构体
    
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {  // 创建socket
        anetSetError(err, "socket: %s\n", strerror(errno));
        return ANET_ERR;
    }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {  // 告诉操作系统允许多个套接字绑定到同一个端口号,并在关闭套接字后立即释放该端口号,从而使得服务器程序更加健壮和高效
        anetSetError(err, "setsockopt SO_REUSEADDR: %s\n", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    memset(&sa,0,sizeof(sa));  // 给sa结构体分配内存,以存放连接信息
    sa.sin_family = AF_INET;  // ipv4
    sa.sin_port = htons(port);  // port
    sa.sin_addr.s_addr = htonl(INADDR_ANY);  // 将sockaddr_in结构体的sin_addr成员变量设置为本机的任意一个可用IP地址(INADDR_ANY),以便socket监听该IP地址接收发送数据.INADDR_ANY表示0.0.0.0
    if (bindaddr) {  // 如果定义了服务端的bind地址
        if (inet_aton(bindaddr, &sa.sin_addr) == 0) {  // 转换ipv4地址为32位整型值,此处没有he结构体和gethostbyname函数逻辑,不能传入主机名
            anetSetError(err, "Invalid bind address\n");
            close(s);
            return ANET_ERR;
        }
    }
    if (bind(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {  // 绑定传入的ip地址
        anetSetError(err, "bind: %s\n", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    if (listen(s, 511) == -1) { /* the magic 511 constant is from nginx */  /* 借用了nignx的魔法数,最大监听511个连接 */
        anetSetError(err, "listen: %s\n", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return s;  // 返回socket
}

int anetAccept(char *err, int serversock, char *ip, int *port)  /* 接受客户端的连接生成socket */
{
    int fd;  // 初始化文件描述符
    struct sockaddr_in sa;  // 初始化socket结构体,保存客户端ip,port
    unsigned int saLen;  // 初始化socket的长度

    while(1) {
        saLen = sizeof(sa);
        fd = accept(serversock, (struct sockaddr*)&sa, &saLen);  // 把每1个客户端的socket生成不同的文件描述符
        if (fd == -1) {  // 如果生成失败
            if (errno == EINTR)  // 在socket编程中,如果调用recv或send等函数时出现EINTR错误,则应该重新调用该函数,所以继续
                continue;
            else {
                anetSetError(err, "accept: %s\n", strerror(errno));
                return ANET_ERR;
            }
        }
        break;
    }
    if (ip) strcpy(ip,inet_ntoa(sa.sin_addr));
    if (port) *port = ntohs(sa.sin_port);
    return fd;  // 返回socket
}
