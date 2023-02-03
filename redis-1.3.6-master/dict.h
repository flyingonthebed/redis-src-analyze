/* Hash Tables Implementation.
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
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

#ifndef __DICT_H  /* 定义字典头文件 */
#define __DICT_H

#define DICT_OK 0  /* 字典正确标志 */
#define DICT_ERR 1  /* 字典错误标志 */

/* Unused arguments generate annoying warnings... */
#define DICT_NOTUSED(V) ((void) V)

typedef struct dictEntry {  /* 字典entry结构体 */
    void *key;  // 指向key的指针
    void *val;  // 指向value的指针
    struct dictEntry *next;  // 指向下一个字典entry结构体的指针
} dictEntry;

typedef struct dictType {  /* 字典（哈希表）类型结构体，文件尾部定义了3个字典类型 */
    unsigned int (*hashFunction)(const void *key);  // 对key进行hash计算的方法
    void *(*keyDup)(void *privdata, const void *key);  // 复制key的方法
    void *(*valDup)(void *privdata, const void *obj);  // 复制value的方法
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);  // 比较2个key的方法
    void (*keyDestructor)(void *privdata, void *key);  // 释放key的方法
    void (*valDestructor)(void *privdata, void *obj);  // 释放value的方法
} dictType;

typedef struct dict {  /* 字典结构体 */
    dictEntry **table;  // 保存entry的哈希表数组
    dictType *type;  // 字典类型指针
    unsigned long size;  // 字典中元素的长度
    unsigned long sizemask;  // 用于计算key的哈希值
    unsigned long used;  // 已使用的长度
    void *privdata;  // 私有数据
} dict;

typedef struct dictIterator {  /* 字典迭代器结构体 */
    dict *ht;  // 哈希表指针
    int index;  // 索引值
    dictEntry *entry, *nextEntry;  // 指向当前entry和下一个entry的指针
} dictIterator;

/* This is the initial size of every hash table，哈希表初始大小，可以保存4个entry */
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros 宏定义函数------------------------------------*/
#define dictFreeEntryVal(ht, entry)  /* 释放entry的value */ \
    if ((ht)->type->valDestructor) \
        (ht)->type->valDestructor((ht)->privdata, (entry)->val)

#define dictSetHashVal(ht, entry, _val_) do {  /* 设置entry的value */ \
    if ((ht)->type->valDup) \
        entry->val = (ht)->type->valDup((ht)->privdata, _val_); \
    else \
        entry->val = (_val_); \
} while(0)

#define dictFreeEntryKey(ht, entry)  /* 释放entry的key */ \
    if ((ht)->type->keyDestructor) \
        (ht)->type->keyDestructor((ht)->privdata, (entry)->key)

#define dictSetHashKey(ht, entry, _key_) do {  /* 设置entry的key */ \
    if ((ht)->type->keyDup) \
        entry->key = (ht)->type->keyDup((ht)->privdata, _key_); \
    else \
        entry->key = (_key_); \
} while(0)

#define dictCompareHashKeys(ht, key1, key2)  /* 比较2个key是否相等 */ \
    (((ht)->type->keyCompare) ? \
        (ht)->type->keyCompare((ht)->privdata, key1, key2) : \
        (key1) == (key2))

#define dictHashKey(ht, key) (ht)->type->hashFunction(key)  /* 对key进行哈希计算 */

#define dictGetEntryKey(he) ((he)->key)  /* 获取entry的key值 */
#define dictGetEntryVal(he) ((he)->val)  /* 获取entry的value值 */
#define dictSlots(ht) ((ht)->size)  /* 获取哈希表的槽位（可存储entry的最大值） */
#define dictSize(ht) ((ht)->used)  /* 获取哈希表已使用的长度 */

/* API */
dict *dictCreate(dictType *type, void *privDataPtr); /* 创建字典 */
int dictExpand(dict *ht, unsigned long size);  /* 扩大字典 */
int dictAdd(dict *ht, void *key, void *val);  /* 向指定字典中添加entry */
int dictReplace(dict *ht, void *key, void *val);  /* 修改字典中指定key的value值 */
int dictDelete(dict *ht, const void *key);  /* 从指定字典中删除entry */
int dictDeleteNoFree(dict *ht, const void *key);  /* 从指定字典中标记删除entry（不释放内存） */
void dictRelease(dict *ht);  /* 释放指定字典 */
dictEntry * dictFind(dict *ht, const void *key);  /* 从指定字典中查找指定key的entry */
int dictResize(dict *ht);  /* 最小化扩展，将字典扩展4个entry槽位 */
dictIterator *dictGetIterator(dict *ht);  /* 获取字典迭代器（索引指向-1） */
dictEntry *dictNext(dictIterator *iter);  /* 对字典迭代器进行迭代（开始迭代时索引+1指向0） */
void dictReleaseIterator(dictIterator *iter);  /* 释放字典迭代器 */
dictEntry *dictGetRandomKey(dict *ht);  /* 获取字典随机key */
void dictPrintStats(dict *ht);  /* 打印字典的统计信息 */
unsigned int dictGenHashFunction(const unsigned char *buf, int len);  /* 通用哈希函数 */
void dictEmpty(dict *ht);  /* 清空字典 */

/* Hash table types，3个哈希表类型的具体定义在dict.c文件尾部 */
extern dictType dictTypeHeapStringCopyKey;  /* 复制key */
extern dictType dictTypeHeapStrings;  /* 复制字符串 */
extern dictType dictTypeHeapStringCopyKeyValue;  /* 复制key和value */

#endif /* __DICT_H */
