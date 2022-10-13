/* Redis CLI (command line interface)
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "anet.h"
#include "sds.h"
#include "adlist.h"
#include "zmalloc.h"

#define REDIS_CMD_INLINE 1
#define REDIS_CMD_BULK 2
#define REDIS_CMD_MULTIBULK 4

#define REDIS_NOTUSED(V) ((void) V)

static struct config {  // 保存客户端连接配置的结构体
    char *hostip;  // 服务器ip
    int hostport;  // 服务器端口
    long repeat;  // 重复执行命令的间隔
    int dbnum;  // db号，0~15
    int interactive;  // 交互模式
    char *auth;  // 实例口令
} config;

/* 命令结构体 */
struct redisCommand {
    char *name;
    int arity;  // 参数个数，包括命令本身，可以传多个参数的命令定义为负值，这样在判断时更容易
    int flags;  // 标识，只有3个值 [REDIS_CMD_INLINE, REDIS_CMD_BULK, REDIS_CMD_MULTIBULK]
};

/* 合法的命令数组 */
static struct redisCommand cmdTable[] = {
    {"auth",2,REDIS_CMD_INLINE},
    {"get",2,REDIS_CMD_INLINE},
    {"set",3,REDIS_CMD_BULK},
    {"setnx",3,REDIS_CMD_BULK},
    {"append",3,REDIS_CMD_BULK},
    {"substr",4,REDIS_CMD_INLINE},
    {"del",-2,REDIS_CMD_INLINE},
    {"exists",2,REDIS_CMD_INLINE},
    {"incr",2,REDIS_CMD_INLINE},
    {"decr",2,REDIS_CMD_INLINE},
    {"rpush",3,REDIS_CMD_BULK},
    {"lpush",3,REDIS_CMD_BULK},
    {"rpop",2,REDIS_CMD_INLINE},
    {"lpop",2,REDIS_CMD_INLINE},
    {"brpop",-3,REDIS_CMD_INLINE},
    {"blpop",-3,REDIS_CMD_INLINE},
    {"llen",2,REDIS_CMD_INLINE},
    {"lindex",3,REDIS_CMD_INLINE},
    {"lset",4,REDIS_CMD_BULK},
    {"lrange",4,REDIS_CMD_INLINE},
    {"ltrim",4,REDIS_CMD_INLINE},
    {"lrem",4,REDIS_CMD_BULK},
    {"rpoplpush",3,REDIS_CMD_BULK},
    {"sadd",3,REDIS_CMD_BULK},
    {"srem",3,REDIS_CMD_BULK},
    {"smove",4,REDIS_CMD_BULK},
    {"sismember",3,REDIS_CMD_BULK},
    {"scard",2,REDIS_CMD_INLINE},
    {"spop",2,REDIS_CMD_INLINE},
    {"srandmember",2,REDIS_CMD_INLINE},
    {"sinter",-2,REDIS_CMD_INLINE},
    {"sinterstore",-3,REDIS_CMD_INLINE},
    {"sunion",-2,REDIS_CMD_INLINE},
    {"sunionstore",-3,REDIS_CMD_INLINE},
    {"sdiff",-2,REDIS_CMD_INLINE},
    {"sdiffstore",-3,REDIS_CMD_INLINE},
    {"smembers",2,REDIS_CMD_INLINE},
    {"zadd",4,REDIS_CMD_BULK},
    {"zincrby",4,REDIS_CMD_BULK},
    {"zrem",3,REDIS_CMD_BULK},
    {"zremrangebyscore",4,REDIS_CMD_INLINE},
    {"zmerge",-3,REDIS_CMD_INLINE},
    {"zmergeweighed",-4,REDIS_CMD_INLINE},
    {"zrange",-4,REDIS_CMD_INLINE},
    {"zrank",3,REDIS_CMD_BULK},
    {"zrevrank",3,REDIS_CMD_BULK},
    {"zrangebyscore",-4,REDIS_CMD_INLINE},
    {"zcount",4,REDIS_CMD_INLINE},
    {"zrevrange",-4,REDIS_CMD_INLINE},
    {"zcard",2,REDIS_CMD_INLINE},
    {"zscore",3,REDIS_CMD_BULK},
    {"incrby",3,REDIS_CMD_INLINE},
    {"decrby",3,REDIS_CMD_INLINE},
    {"getset",3,REDIS_CMD_BULK},
    {"randomkey",1,REDIS_CMD_INLINE},
    {"select",2,REDIS_CMD_INLINE},
    {"move",3,REDIS_CMD_INLINE},
    {"rename",3,REDIS_CMD_INLINE},
    {"renamenx",3,REDIS_CMD_INLINE},
    {"keys",2,REDIS_CMD_INLINE},
    {"dbsize",1,REDIS_CMD_INLINE},
    {"ping",1,REDIS_CMD_INLINE},
    {"echo",2,REDIS_CMD_BULK},
    {"save",1,REDIS_CMD_INLINE},
    {"bgsave",1,REDIS_CMD_INLINE},
    {"rewriteaof",1,REDIS_CMD_INLINE},
    {"bgrewriteaof",1,REDIS_CMD_INLINE},
    {"shutdown",1,REDIS_CMD_INLINE},
    {"lastsave",1,REDIS_CMD_INLINE},
    {"type",2,REDIS_CMD_INLINE},
    {"flushdb",1,REDIS_CMD_INLINE},
    {"flushall",1,REDIS_CMD_INLINE},
    {"sort",-2,REDIS_CMD_INLINE},
    {"info",1,REDIS_CMD_INLINE},
    {"mget",-2,REDIS_CMD_INLINE},
    {"expire",3,REDIS_CMD_INLINE},
    {"expireat",3,REDIS_CMD_INLINE},
    {"ttl",2,REDIS_CMD_INLINE},
    {"slaveof",3,REDIS_CMD_INLINE},
    {"debug",-2,REDIS_CMD_INLINE},
    {"mset",-3,REDIS_CMD_MULTIBULK},
    {"msetnx",-3,REDIS_CMD_MULTIBULK},
    {"monitor",1,REDIS_CMD_INLINE},
    {"multi",1,REDIS_CMD_INLINE},
    {"exec",1,REDIS_CMD_INLINE},
    {"discard",1,REDIS_CMD_INLINE},
    {"hset",4,REDIS_CMD_MULTIBULK},
    {"hget",3,REDIS_CMD_BULK},
    {"hdel",3,REDIS_CMD_BULK},
    {"hlen",2,REDIS_CMD_INLINE},
    {"hkeys",2,REDIS_CMD_INLINE},
    {"hvals",2,REDIS_CMD_INLINE},
    {"hgetall",2,REDIS_CMD_INLINE},
    {"hexists",3,REDIS_CMD_BULK},
    {NULL,0,0}
};

static int cliReadReply(int fd);
static void usage();

/* 检查传入的命令名是否合法，合法返回命令名，不合法返回 NULL */
static struct redisCommand *lookupCommand(char *name) {
    int j = 0;
    while(cmdTable[j].name != NULL) {  // 遍历已有的命令
        if (!strcasecmp(name,cmdTable[j].name)) return &cmdTable[j];  // 合法返回命令名
        j++;
    }
    return NULL;  // 不合法返回 NULL
}

static int cliConnect(void) {
    char err[ANET_ERR_LEN];
    static int fd = ANET_ERR;

    if (fd == ANET_ERR) {
        fd = anetTcpConnect(err,config.hostip,config.hostport);  // tcp连接server，成功返回socket套接字
        if (fd == ANET_ERR) {
            fprintf(stderr, "Could not connect to Redis at %s:%d: %s", config.hostip, config.hostport, err);
            return -1;
        }
        anetTcpNoDelay(NULL,fd);
    }
    return fd;
}

static sds cliReadLine(int fd) {
    sds line = sdsempty();

    while(1) {
        char c;
        ssize_t ret;

        ret = read(fd,&c,1);
        if (ret == -1) {
            sdsfree(line);
            return NULL;
        } else if ((ret == 0) || (c == '\n')) {
            break;
        } else {
            line = sdscatlen(line,&c,1);
        }
    }
    return sdstrim(line,"\r\n");
}

static int cliReadSingleLineReply(int fd, int quiet) {
    sds reply = cliReadLine(fd);  // 通过socket读取reply

    if (reply == NULL) return 1;  // 如果reply为空，则返回错误
    if (!quiet)
        printf("%s\n", reply);  // 如果 quiet=0，则打印reply
    sdsfree(reply);  // 回收reply的内存空间
    return 0;
}

static int cliReadBulkReply(int fd) {
    sds replylen = cliReadLine(fd);
    char *reply, crlf[2];
    int bulklen;

    if (replylen == NULL) return 1;  // reply长度为空，则返回错误
    bulklen = atoi(replylen);  // 转换为int
    if (bulklen == -1) {
        sdsfree(replylen);
        printf("(nil)\n");  // reply=-1，则打印nil
        return 0;
    }
    reply = zmalloc(bulklen);  // 分配bulklen大小的内存
    anetRead(fd,reply,bulklen);
    anetRead(fd,crlf,2);
    if (bulklen && fwrite(reply,bulklen,1,stdout) == 0) {  // 输出reply
        zfree(reply);
        return 1;
    }
    if (isatty(fileno(stdout)) && reply[bulklen-1] != '\n')  // reply最后一个字符是\n则换行
        printf("\n");
    zfree(reply);
    return 0;
}

static int cliReadMultiBulkReply(int fd) {
    sds replylen = cliReadLine(fd);
    int elements, c = 1;

    if (replylen == NULL) return 1;
    elements = atoi(replylen);
    if (elements == -1) {
        sdsfree(replylen);
        printf("(nil)\n");
        return 0;
    }
    if (elements == 0) {
        printf("(empty list or set)\n");
    }
    while(elements--) {
        printf("%d. ", c);
        if (cliReadReply(fd)) return 1;  // 遍历 multi bulk 中的元素输出
        c++;
    }
    return 0;
}

static int cliReadReply(int fd) {
    char type;

    if (anetRead(fd,&type,1) <= 0) exit(1);  // 第一个字符小于0，即没有reply，退出
    switch(type) {
    case '-':
        printf("(error) ");    // 第一个字符是 -，则是单行命令的 reply，输出错误信息
        cliReadSingleLineReply(fd,0);
        return 1;
    case '+':
        return cliReadSingleLineReply(fd,0);  // 第一个字符是 +，则是单行命令的 reply
    case ':':
        printf("(integer) ");
        return cliReadSingleLineReply(fd,0);  // 第一个字符是 :，则是单行命令的 reply
    case '$':
        return cliReadBulkReply(fd);  // 第一个字符是 $，则是 bulk 命令的 reply
    case '*':
        return cliReadMultiBulkReply(fd);  // 第一个字符是 *，则是 multi bulk 命令的 reply
    default:
        printf("protocol error, got '%c' as reply type byte\n", type);
        return 1;
    }
}

static int selectDb(int fd) {
    int retval;
    sds cmd;
    char type;

    if (config.dbnum == 0)
        return 0;

    cmd = sdsempty();  // 初始化cmd为一个空sds字符串
    cmd = sdscatprintf(cmd,"SELECT %d\r\n",config.dbnum);  // 将select n命令作为传入cmd
    anetWrite(fd,cmd,sdslen(cmd));  // 向server发送sds字符串
    anetRead(fd,&type,1);  // 接收redis server返回的消息
    if (type <= 0 || type != '+') return 1;  // type > 0 && type = '+' 表示连接正常，继续
    retval = cliReadSingleLineReply(fd,1);  // quiet=1，即!quiet=0，打印reply，retval=0
    if (retval) {  // 如果retval != 0,则返回错误
        return retval;
    }
    return 0;
}

static int cliSendCommand(int argc, char **argv) {
    struct redisCommand *rc = lookupCommand(argv[0]);  // 查看命令是否符合要求，argv[0]就是命令名
    int fd, j, retval = 0;
    int read_forever = 0;
    sds cmd;

    if (!rc) {  // 命令名不存在
        fprintf(stderr,"Unknown command '%s'\n",argv[0]);
        return 1;
    }

    if ((rc->arity > 0 && argc != rc->arity) ||  // 可以传固定数量参数的命令 || 可以传多个数量参数的命令
        (rc->arity < 0 && argc < -rc->arity)) {
            fprintf(stderr,"Wrong number of arguments for '%s'\n",rc->name);  // 都为false则报错
            return 1;
    }
    if (!strcasecmp(rc->name,"monitor")) read_forever = 1;  // 如果传入参数为monitor，则进入监听模式
    if ((fd = cliConnect()) == -1) return 1;  // tcp连接server，成功返回socket套接字

    /* Select db number */
    retval = selectDb(fd);  
    if (retval) {
        fprintf(stderr,"Error setting DB num\n");
        return 1;
    }

    while(config.repeat--) {  // 如果指定-r n循环次数，则执行
        /* Build the command to send */
        cmd = sdsempty();
        if (rc->flags & REDIS_CMD_MULTIBULK) {  // 标签为可以包含多个参数对的命令，以mset hi hello bj beijing bd baoding 为例
            cmd = sdscatprintf(cmd,"*%d\r\n",argc);  // 拼接打印字符串，cmd=*7\r\n
            for (j = 0; j < argc; j++) {
                cmd = sdscatprintf(cmd,"$%lu\r\n",  // 增加 $4\r\n，cmd=*7\r\n$4\r\n
                    (unsigned long)sdslen(argv[j]));
                cmd = sdscatlen(cmd,argv[j],sdslen(argv[j]));  // 拼接命令字符串，增加 mset，cmd=*7\r\n$4\r\nmset
                cmd = sdscatlen(cmd,"\r\n",2);  // 增加 \r\n, cmd=*7\r\n$4\r\nmset\r\n
            }
        } else {  // REDIS_CMD_INLINE, REDIS_CMD_BULK
            for (j = 0; j < argc; j++) {
                if (j != 0) cmd = sdscat(cmd," ");  // 每2个字符串中间添加空格
                if (j == argc-1 && rc->flags & REDIS_CMD_BULK) {  // REDIS_CMD_BULK，例如 hget
                    cmd = sdscatprintf(cmd,"%lu",
                        (unsigned long)sdslen(argv[j]));
                } else {  // REDIS_CMD_INLINE，例如 set, get
                    cmd = sdscatlen(cmd,argv[j],sdslen(argv[j]));
                }
            }
            cmd = sdscat(cmd,"\r\n");
            if (rc->flags & REDIS_CMD_BULK) {  // REDIS_CMD_BULK
                cmd = sdscatlen(cmd,argv[argc-1],sdslen(argv[argc-1]));
                cmd = sdscatlen(cmd,"\r\n",2);
            }
        }
        anetWrite(fd,cmd,sdslen(cmd));  // 向redis server写入数据
        sdsfree(cmd);  // 释放cmd内存

        while (read_forever) {  // monitor模式
            cliReadSingleLineReply(fd,0);
        }

        retval = cliReadReply(fd);  
        if (retval) {
            return retval;
        }
    }
    return 0;
}

static int parseOptions(int argc, char **argv) {
    int i;

    for (i = 1; i < argc; i++) {  // 布尔值。判断是否有传参，因为程序名也会读取参数，所以-1才能确定后面真的有传参
        int lastarg = i==argc-1;

        if (!strcmp(argv[i],"-h") && !lastarg) {  // strcmp函数判断str1,str2相同返回0，!strcmp表示非0（真），即传参为-h且-h后面还有参数
            char *ip = zmalloc(32);  //给ip指针变量分配内存
            if (anetResolve(NULL,argv[i+1],ip) == ANET_ERR) {  // 解析ip变量是否是合法的ip地址，如果不是则报错并推出，argv[i]='-h'，argv[i+1]=服务器ip
                printf("Can't resolve %s\n", argv[i]);  
                exit(1);
            }
            config.hostip = ip;  // ip合法则传入config结构体
            i++;
        } else if (!strcmp(argv[i],"-h") && lastarg) {  // 传参是-h且后面没有ip地址，则调用usage，打印帮助
            usage();
        } else if (!strcmp(argv[i],"-p") && !lastarg) {  // 传参是-p且后面有port传参
            config.hostport = atoi(argv[i+1]);  // port传参合法则传入config结构体
            i++;
        } else if (!strcmp(argv[i],"-r") && !lastarg) {  // 传参是-r且后面有port传参
            config.repeat = strtoll(argv[i+1],NULL,10);
            i++;
        } else if (!strcmp(argv[i],"-n") && !lastarg) {  // 传参是-n且后面有dbnum传参
            config.dbnum = atoi(argv[i+1]);
            i++;
        } else if (!strcmp(argv[i],"-a") && !lastarg) {  // 传参是-a且后面有auth传参
            config.auth = argv[i+1];
            i++;
        } else if (!strcmp(argv[i],"-i")) {  // 传参是-i，表示交互模式，后续版本模式就是交互模式
            config.interactive = 1;
        } else {
            break;
        }
    }
    return i;  // 返回argc的值
}

static sds readArgFromStdin(void) {
    char buf[1024];
    sds arg = sdsempty();

    while(1) {
        int nread = read(fileno(stdin),buf,1024);

        if (nread == 0) break;
        else if (nread == -1) {
            perror("Reading from standard input");
            exit(1);
        }
        arg = sdscatlen(arg,buf,nread);
    }
    return arg;
}

static void usage() {  // 打印帮助的函数
    fprintf(stderr, "usage: redis-cli [-h host] [-p port] [-a authpw] [-r repeat_times] [-n db_num] [-i] cmd arg1 arg2 arg3 ... argN\n");
    fprintf(stderr, "usage: echo \"argN\" | redis-cli [-h host] [-a authpw] [-p port] [-r repeat_times] [-n db_num] cmd arg1 arg2 ... arg(N-1)\n");
    fprintf(stderr, "\nIf a pipe from standard input is detected this data is used as last argument.\n\n");
    fprintf(stderr, "example: cat /etc/passwd | redis-cli set my_passwd\n");
    fprintf(stderr, "example: redis-cli get my_passwd\n");
    fprintf(stderr, "example: redis-cli -r 100 lpush mylist x\n");
    fprintf(stderr, "\nRun in interactive mode: redis-cli -i or just don't pass any command\n");
    exit(1);
}

/* Turn the plain C strings into Sds strings */
/* 将普通的C字符串转换成Sds字符串 */
static char **convertToSds(int count, char** args) {
  int j;
  char **sds = zmalloc(sizeof(char*)*count+1);

  for(j = 0; j < count; j++)
    sds[j] = sdsnew(args[j]);

  return sds;
}

static char *prompt(char *line, int size) {
    char *retval;

    do {
        printf(">> ");
        retval = fgets(line, size, stdin);
    } while (retval && *line == '\n');  // 如果没有输入，直接回车继续循环 *line=line[0]
    line[strlen(line) - 1] = '\0';  // line 的换行符替换成了 '\0'

    return retval;
}

static void repl() {
    int size = 4096, max = size >> 1, argc;
    char buffer[size];
    char *line = buffer;
    char **ap, *args[max];

    if (config.auth != NULL) {  // 传入了密钥
        char *authargv[2];  // authargv 先与[]结合，数组中的每一个元素都是 char *

        authargv[0] = "AUTH";
        authargv[1] = config.auth;  // 此时 authargv 数组是 ["AUTH", "密钥"]
        cliSendCommand(2, convertToSds(2, authargv));  // 将 authargv 转换成 sds 并发送
    }

    while (prompt(line, size)) {  
        argc = 0;

        for (ap = args; (*ap = strsep(&line, " \t")) != NULL;) {
            if (**ap != '\0') {
                if (argc >= max) break;
                if (strcasecmp(*ap,"quit") == 0 || strcasecmp(*ap,"exit") == 0)
                    exit(0);
                ap++;
                argc++;
            }
        }

        config.repeat = 1;
        cliSendCommand(argc, convertToSds(argc, args));  // 将 cli 接收到的命令转换成 sds 后发送给 server
        line = buffer;
    }

    exit(0);
}

int main(int argc, char **argv) {
    int firstarg;
    char **argvcopy;
    struct redisCommand *rc;

    config.hostip = "127.0.0.1";
    config.hostport = 6379;
    config.repeat = 1;
    config.dbnum = 0;
    config.interactive = 0;
    config.auth = NULL;

    firstarg = parseOptions(argc,argv);
    argc -= firstarg;  // 第1个参数的位置
    argv += firstarg;  // 

    if (argc == 0 || config.interactive == 1) repl();

    argvcopy = convertToSds(argc, argv);

    /* Read the last argument from stdandard input if needed */
    if ((rc = lookupCommand(argv[0])) != NULL) {
      if (rc->arity > 0 && argc == rc->arity-1) {
        sds lastarg = readArgFromStdin();
        argvcopy[argc] = lastarg;
        argc++;
      }
    }

    return cliSendCommand(argc, argvcopy);
}
