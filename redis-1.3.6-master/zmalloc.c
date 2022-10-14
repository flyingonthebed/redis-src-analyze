/* zmalloc - total amount of allocated memory aware version of malloc()
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

#include <stdio.h>
#include <stdlib.h>  /* malloc() free() realloc() */
#include <string.h>
#include <pthread.h>
#include "config.h"  // 配置信息

#if defined(__sun)  // SunOS
#define PREFIX_SIZE sizeof(long long)  /* prefix_size 对应 sds->free */
#else
#define PREFIX_SIZE sizeof(size_t)
#endif

#define increment_used_memory(_n) do { /* 增加使用的内存 */ \
    if (zmalloc_thread_safe) { /* 启用线程安全 */ \
        pthread_mutex_lock(&used_memory_mutex); /* 加锁 */  \
        used_memory += _n; /* 增加使用的内存 */ \
        pthread_mutex_unlock(&used_memory_mutex); /* 开锁 */ \
    } else { \
        used_memory += _n; /* 不加锁，直接增加内存 */ \
    } \
} while(0)

#define decrement_used_memory(_n) do { /* 减少使用的内存 */ \
    if (zmalloc_thread_safe) { \
        pthread_mutex_lock(&used_memory_mutex);  \
        used_memory -= _n; \
        pthread_mutex_unlock(&used_memory_mutex); \
    } else { \
        used_memory -= _n; \
    } \
} while(0)

static size_t used_memory = 0;  // 已分配的总内存长度
static int zmalloc_thread_safe = 0;  // 默认未开启内存安全
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;  // 初始化一个互斥锁变量

static void zmalloc_oom(size_t size) {  /* 可用内存不足，分配报错 */
    fprintf(stderr, "zmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    fflush(stderr);
    abort();
}

void *zmalloc(size_t size) {  /* 分配内存 */
    void *ptr = malloc(size+PREFIX_SIZE);  // size对应sds->len，prefix_size对应sds->free

    if (!ptr) zmalloc_oom(size);
#ifdef HAVE_MALLOC_SIZE
    increment_used_memory(redis_malloc_size(ptr));
    return ptr;
#else
    *((size_t*)ptr) = size;  //将size的值存入转换为size_t类型的指针中
    increment_used_memory(size+PREFIX_SIZE);  // 更新总已分配内存长度
    return (char*)ptr+PREFIX_SIZE;  // 返回sds->buf的指针
#endif
}

void *zrealloc(void *ptr, size_t size) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
#endif
    size_t oldsize;
    void *newptr;

    if (ptr == NULL) return zmalloc(size);  // 之前没有分配过内存，直接调用zamalloc()
#ifdef HAVE_MALLOC_SIZE
    oldsize = redis_malloc_size(ptr);
    newptr = realloc(ptr,size);
    if (!newptr) zmalloc_oom(size);

    decrement_used_memory(oldsize);
    increment_used_memory(redis_malloc_size(newptr));
    return newptr;
#else
    realptr = (char*)ptr-PREFIX_SIZE;  // sds结构体指针
    oldsize = *((size_t*)realptr);  // sds->len的值
    newptr = realloc(realptr,size+PREFIX_SIZE);  // 给sds->buf分配新指针
    if (!newptr) zmalloc_oom(size);

    *((size_t*)newptr) = size;  //将size的值存入转换为size_t类型的指针中
    decrement_used_memory(oldsize);
    increment_used_memory(size);  // 将旧的sds->len更新
    return (char*)newptr+PREFIX_SIZE;  // 返回新的sds->buf指针
#endif
}

void zfree(void *ptr) {  /* 释放内存 */
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
    size_t oldsize;
#endif

    if (ptr == NULL) return;
#ifdef HAVE_MALLOC_SIZE
    decrement_used_memory(redis_malloc_size(ptr));
    free(ptr);
#else
    realptr = (char*)ptr-PREFIX_SIZE;  // sds结构体指针
    oldsize = *((size_t*)realptr);
    decrement_used_memory(oldsize+PREFIX_SIZE);
    free(realptr);  // 释放内存
#endif
}

char *zstrdup(const char *s) {  /* 复制字符串数组 */
    size_t l = strlen(s)+1;  // sds->len，结束符长度
    char *p = zmalloc(l);  // 申请内存

    memcpy(p,s,l);  // 指针，sds->buf，sds->len
    return p;
}

size_t zmalloc_used_memory(void) {  /* 更新总分配内存长度 */
    size_t um;

    if (zmalloc_thread_safe) pthread_mutex_lock(&used_memory_mutex);
    um = used_memory;
    if (zmalloc_thread_safe) pthread_mutex_unlock(&used_memory_mutex);
    return um;
}

void zmalloc_enable_thread_safeness(void) {  /* 启用线程安全 */
    zmalloc_thread_safe = 1;
}
