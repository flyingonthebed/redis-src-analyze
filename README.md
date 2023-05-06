# redis-src-analyze
> 分析 Redis 1.3.6, 2.8.0, 4.0.0 版本源码结构，深入理解 Redis 内部工作机制
## 1. 学习资料
- 书籍：推荐仔细阅读 黄建宏先生编写的《Redis设计与实现》机械工业出版社, ISBN: 978-7-111-46474-7
- 视频：推荐B站up主【沈奇才的编程之旅】的系列视频《从零开始学Reids源码》https://www.bilibili.com/video/BV1tR4y1M75K

## 2. 学习方法
- 先阅读《Redis设计与实现》了解各功能模块的设计思路和实现效果
- 再下载redis源码学习对应的模块文件，分析其中代码实现细节与思路

## 3. 更新日志
- 2022-09-21 完成 [Makefile](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/Makefile) 文件解析
- 2022-09-21 完成 [redis-cli.c](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/redis-cli.c) 文件解析
- 2022-09-22 完成 [sds.h](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/sds.h) 文件解析
- 2022-09-24 完成 [sds.c](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/sds.c) 文件解析
- 2022-09-26 完成 [config.h](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/config.h), [zmalloc.c](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/zmalloc.c) 文件解析
- 2022-10-04 完成 [adlist.h](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/adlist.h) 文件解析
- 2022-10-13 完成 [adlist.c](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/adlist.c) 文件解析
- 2022-10-16 完成 [zipmap.h](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/zipmap.h) 文件解析
- 2022-11-04 完成 [zipmap.c](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/zipmap.c) 文件解析
- 2022-11-05 完成 [dict.h](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/dict.h) 文件解析
- 2023-05-04 完成 [dict.c](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/dict.c) 文件解析
- 2023-05-05 完成 [anet.h](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/anet.h) 文件解析
- 2023-05-05 完成 [anet.c](https://github.com/flyingonthebed/redis-src-analyze/blob/main/redis-1.3.6/anet.c) 文件解析