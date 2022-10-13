/* SDSLib, A C dynamic strings library
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

#define SDS_ABORT_ON_OOM

#include "sds.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "zmalloc.h"  // zmalloc.c -> config.h -> malloc.c, malloc()  free()

static void sdsOomAbort(void) {
    fprintf(stderr,"SDS: Out Of Memory (SDS_ABORT_ON_OOM defined)\n");
    abort();
}

sds sdsnewlen(const void *init, size_t initlen) {
    struct sdshdr *sh;

    sh = zmalloc(sizeof(struct sdshdr)+initlen+1);  // initlen 是给 buf[] 的空间，1是结束符'\0'的空间
#ifdef SDS_ABORT_ON_OOM
    if (sh == NULL) sdsOomAbort();  // sds buf 空间溢出
#else
    if (sh == NULL) return NULL;
#endif
    sh->len = initlen;
    sh->free = 0;
    if (initlen) {
        if (init) memcpy(sh->buf, init, initlen);  // 内存拷贝，不包含结束符'\0'，如果init < initlen，则在buf后面补0
        else memset(sh->buf,0,initlen);  // '\0'
    }
    sh->buf[initlen] = '\0';  // 加上结束符
    return (char*)sh->buf;  // 返回对应字符串地址
}

sds sdsempty(void) {
    return sdsnewlen("",0);  // 初始化一个空的sds，即 ['\0']
}

sds sdsnew(const char *init) {  // 根据传入的字符串长度生成对应长度的sds
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}

size_t sdslen(const sds s) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));  // s为buf的指针地址，减去size后，得到len的指针地址，转换成sds结构体指针后，获取sds的len成员变量
    return sh->len;
}

sds sdsdup(const sds s) {  // 复制sds
    return sdsnewlen(s, sdslen(s));  // 根据已有的sds创建一个同样长度同内容的sds
}

void sdsfree(sds s) {  // 回收sds的内存
    if (s == NULL) return;
    zfree(s-sizeof(struct sdshdr));  // 释放内存
}

size_t sdsavail(sds s) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));  // s为buf的指针地址，减去size后，得到len的指针地址，转换成sds结构体指针后，获取sds的free成员变量
    return sh->free;
}

void sdsupdatelen(sds s) {
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));  // s为buf的指针地址，减去size后，得到len的指针地址，转换成sds结构体指针
    int reallen = strlen(s);  // s字符数组的真实长度
    sh->free += (sh->len-reallen);  // 更新free的值，如果s变大，则free+(-n)，即free的值变小
    sh->len = reallen;
}

static sds sdsMakeRoomFor(sds s, size_t addlen) {  //给sds增加的字符串内容分配空间
    struct sdshdr *sh, *newsh;
    size_t free = sdsavail(s);
    size_t len, newlen;

    if (free >= addlen) return s;  //free的值足够，直接返回
    len = sdslen(s);
    sh = (void*) (s-(sizeof(struct sdshdr)));
    newlen = (len+addlen)*2;  // 给sds计算出要扩充后的长度，x2是预留更多的空间，方便后续增加字符
    newsh = zrealloc(sh, sizeof(struct sdshdr)+newlen+1);  // 给新结构体分配空间
#ifdef SDS_ABORT_ON_OOM
    if (newsh == NULL) sdsOomAbort();
#else
    if (newsh == NULL) return NULL;
#endif

    newsh->free = newlen - len;  // 更新free的值，sds的len,free记录可用长度的值，实际的可用空间位置在buf的字符数组末尾，用0填充。所以len,free,buf的地址都不变
    return newsh->buf;
}

sds sdscatlen(sds s, void *t, size_t len) {  //给sds追加t字符串中的len个字符
    struct sdshdr *sh;
    size_t curlen = sdslen(s);

    s = sdsMakeRoomFor(s,len);  // 判断是否要为sds增加可用空间来容纳t
    if (s == NULL) return NULL;
    sh = (void*) (s-(sizeof(struct sdshdr)));
    memcpy(s+curlen, t, len);  // s+curlen 表示找到s的结束符'\0'的位置
    sh->len = curlen+len;
    sh->free = sh->free-len;  // 更新len和free的值
    s[curlen+len] = '\0';
    return s;
}

sds sdscat(sds s, char *t) {  // 将t全部追加到sds末尾
    return sdscatlen(s, t, strlen(t));  
}

sds sdscpylen(sds s, char *t, size_t len) {  // 将t字符串中的len个字符写入sds中，如果sds空间不足则扩容
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    size_t totlen = sh->free+sh->len;

    if (totlen < len) {  // sds空间不足以容纳t
        s = sdsMakeRoomFor(s,len-sh->len);  // 给sds扩容指定字节
        if (s == NULL) return NULL;
        sh = (void*) (s-(sizeof(struct sdshdr)));  // 获取sh的结构体指针，根据sds后面的未分配空间、内存分配机制，sh的地址可能会变更
        totlen = sh->free+sh->len;  // 扩容后sds的总长度
    }
    memcpy(s, t, len);
    s[len] = '\0';
    sh->len = len;  // 更新扩容后sds的字符数组长度
    sh->free = totlen-len;  // 更新扩容后sds的free长度
    return s;
}

sds sdscpy(sds s, char *t) {  // 将t字符串完整写入sds中，如果sds空间不足则扩容
    return sdscpylen(s, t, strlen(t));
}

sds sdscatprintf(sds s, const char *fmt, ...) {  // 必须传入s和fmt，后面可传入0~多个变量
    va_list ap;
    char *buf, *t;
    size_t buflen = 16;  //默认分配16字节空间

    while(1) {
        buf = zmalloc(buflen);
#ifdef SDS_ABORT_ON_OOM
        if (buf == NULL) sdsOomAbort();
#else
        if (buf == NULL) return NULL;
#endif
        buf[buflen-2] = '\0';  // 倒数第2个字符设置结束符，预留最后一个是确保buf能够完全容纳字符数组
        va_start(ap, fmt);  // 获得可变参数列表
        vsnprintf(buf, buflen, fmt, ap);  // buf是要打印的字符数组，buflen要取出字符数组的长度，fmt是打印格式，ap是打印的列表
        va_end(ap);  // 释放内存
        if (buf[buflen-2] != '\0') {  // 如果倒数第2的位置不是结束符，说明buf长度不足，需要扩容
            zfree(buf);  // 先释放内存
            buflen *= 2;  // 再分配2倍的内存空间
            continue;  // 继续循环，如果x2不够，下次会继续x2，直到buf的长度足够容纳字符串
        }
        break;
    }
    t = sdscat(s, buf);  // 向sds中追加字符数组
    zfree(buf);  // 释放内存
    return t;
}

sds sdstrim(sds s, const char *cset) {  // 去掉字符串首尾的空格，cset是要去掉的字符
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));  // 获取结构体指针
    char *start, *end, *sp, *ep;
    size_t len;

    sp = start = s;  // 字符数组buf的起始位置
    ep = end = s+sdslen(s)-1;  // 字符数组结尾，结束符前1个位置
    /* strchr() 查找某个字符在字符串中第1次出现的位置 */ 
    while(sp <= end && strchr(cset, *sp)) sp++;  // 找到第1个要保留字符位置
    while(ep > start && strchr(cset, *ep)) ep--;  //找到最后1个要保留字符位置
    len = (sp > ep) ? 0 : ((ep-sp)+1);
    if (sh->buf != sp) memmove(sh->buf, sp, len);  // 将buf中的字符数组向前移动
    sh->buf[len] = '\0';  // 设置新的结束符，此时结束符后边的字符仍然不变
    sh->free = sh->free+(sh->len-len);  //更新free的值
    sh->len = len;  // 更新len的值
    return s;
}

sds sdsrange(sds s, long start, long end) {  // 截断操作
    struct sdshdr *sh = (void*) (s-(sizeof(struct sdshdr)));
    size_t newlen, len = sdslen(s);

    if (len == 0) return s;
    if (start < 0) {  // 处理负数位置
        start = len+start;
        if (start < 0) start = 0;
    }
    if (end < 0) {
        end = len+end;
        if (end < 0) end = 0;
    }
    newlen = (start > end) ? 0 : (end-start)+1;
    if (newlen != 0) {
        if (start >= (signed)len) start = len-1;
        if (end >= (signed)len) end = len-1;
        newlen = (start > end) ? 0 : (end-start)+1;
    } else {
        start = 0;
    }
    if (start != 0) memmove(sh->buf, sh->buf+start, newlen);  // 移动字符数组
    sh->buf[newlen] = 0;  // ASCII 码中的 '\0'
    sh->free = sh->free+(sh->len-newlen);
    sh->len = newlen;
    return s;
}

void sdstolower(sds s) {  // 小写转换
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = tolower(s[j]);
}

void sdstoupper(sds s) {  // 大写转换
    int len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = toupper(s[j]);
}

int sdscmp(sds s1, sds s2) {  // 比较2个字符数组是否一致
    size_t l1, l2, minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;  // 获取2个字符数组中，长度最小的字符数组长度
    cmp = memcmp(s1,s2,minlen);  // 比较2个字符数组的最小长度的内容是否一致，一致则cmp=0
    if (cmp == 0) return l1-l2;  // 最小长度的内容一致，判断剩余长度是否一致，一致返回0
    return cmp;
}

/* Split 's' with separator in 'sep'. An array
 * of sds strings is returned. *count will be set
 * by reference to the number of tokens returned.
 *
 * On out of memory, zero length string, zero length
 * separator, NULL is returned.
 *
 * Note that 'sep' is able to split a string using
 * a multi-character separator. For example
 * sdssplit("foo_-_bar","_-_"); will return two
 * elements "foo" and "bar".
 *
 * This version of the function is binary-safe but
 * requires length arguments. sdssplit() is just the
 * same function but for zero-terminated strings.
 */
sds *sdssplitlen(char *s, int len, char *sep, int seplen, int *count) {  // 切分字符数组，count为切分后的数组元素个数
    int elements = 0, slots = 5, start = 0, j;  // element 是 tokens 的索引位置

    sds *tokens = zmalloc(sizeof(sds)*slots);  // 分配内存空间，存储切分后的字符串，默认可以存储5个字符串
#ifdef SDS_ABORT_ON_OOM
    if (tokens == NULL) sdsOomAbort();  // 没有剩余内存可以分配给tokens，报错oom
#endif
    if (seplen < 1 || len < 0 || tokens == NULL) return NULL;  // 切分字符或sds长度为空或tokens没有空间则不操作
    if (len == 0) {
        *count = 0;
        return tokens;  // sds长度为0，切分后的数组元素个数为0，返回空字符数组
    }
    for (j = 0; j < (len-(seplen-1)); j++) {
        /* make sure there is room for the next element and the final one */
        if (slots < elements+2) {  // 槽位不足则扩展槽位，因为索引从0开始，故+2
            sds *newtokens;

            slots *= 2;
            newtokens = zrealloc(tokens,sizeof(sds)*slots);  // 分配新的空间给tokens
            if (newtokens == NULL) {  // 没有剩余内存可以分配给tokens，报错oom
#ifdef SDS_ABORT_ON_OOM
                sdsOomAbort();
#else
                goto cleanup;
#endif
            }
            tokens = newtokens;
        }
        /* search the separator */
        if ((seplen == 1 && *(s+j) == sep[0]) || (memcmp(s+j,sep,seplen) == 0)) {  // 分割字符为1个时或分割字符为多个时为真则继续，多个时使用memcmp比较
            tokens[elements] = sdsnewlen(s+start,j-start);  // (起始位置,长度). 以"hello__world__china"为例，分隔符为'__'，j=5，从h到o为5个字符，故j无需-1
            if (tokens[elements] == NULL) {
#ifdef SDS_ABORT_ON_OOM
                sdsOomAbort();
#else
                goto cleanup;
#endif
            }
            elements++;
            start = j+seplen;
            j = j+seplen-1; /* skip the separator */ // j++，下次循环会+1，所以先-1，确保下次循环时start=j
        }
    }
    /* Add the final element. We are sure there is room in the tokens array. */
    tokens[elements] = sdsnewlen(s+start,len-start);  // (起始位置,长度). 以"hello__world__china"为例，分隔符为'__'，j=5，从h到o为5个字符，故j无需-1
    if (tokens[elements] == NULL) {
#ifdef SDS_ABORT_ON_OOM
                sdsOomAbort();
#else
                goto cleanup;
#endif
    }
    elements++;  // tokens索引位置+1
    *count = elements;  // 切分后的数组元素个数
    return tokens;

#ifndef SDS_ABORT_ON_OOM
cleanup:
    {
        int i;
        for (i = 0; i < elements; i++) sdsfree(tokens[i]);
        zfree(tokens);
        return NULL;
    }
#endif
}
