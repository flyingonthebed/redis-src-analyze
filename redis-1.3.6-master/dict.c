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

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <limits.h>

#include "dict.h"
#include "zmalloc.h"

/* ---------------------------- Utility funcitons --------------------------- */

static void _dictPanic(const char *fmt, ...)  /* 发生异常时，打印字典错误信息 ... 表示可以接收多个参数 */
{
    va_list ap;  // 定义变量列表

    va_start(ap, fmt);  // 格式化变量
    fprintf(stderr, "\nDICT LIBRARY PANIC: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n\n");
    va_end(ap);  // 
}

/* ------------------------- Heap Management Wrappers ----------------------- */

static void *_dictAlloc(size_t size)  /* 给字典分配内存 */
{
    void *p = zmalloc(size);  // 分配内存，获取指针
    if (p == NULL)
        _dictPanic("Out of memory");  // 如果获取不到指针，则表示内存不足
    return p;
}

static void _dictFree(void *ptr) {  /* 释放字典内存 */
    zfree(ptr);  // 释放字典
}

/* -------------------------- private prototypes ---------------------------- */
/* 私有函数原型，定义了4个静态函数 */
static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht, const void *key);
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);

/* -------------------------- hash functions -------------------------------- */

/* Thomas Wang's 32 bit Mix Function */
unsigned int dictIntHashFunction(unsigned int key)  /* 传入key进行位运算获取整型哈希值 */
{
    key += ~(key << 15);
    key ^=  (key >> 10);
    key +=  (key << 3);
    key ^=  (key >> 6);
    key += ~(key << 11);
    key ^=  (key >> 16);
    return key;  // 返回哈希值
}

/* Identity hash function for integer keys */
unsigned int dictIdentityHashFunction(unsigned int key)  /* 验证传入的key是int型的值 */
{
    return key;
}

/* Generic hash function (a popular one from Bernstein).
 * I tested a few and this was the best. */
unsigned int dictGenHashFunction(const unsigned char *buf, int len) {  /* 通用哈希函数，传入字符数组和长度 */
    unsigned int hash = 5381;

    while (len--)
        hash = ((hash << 5) + hash) + (*buf++); /* hash * 33 + c，依次对字符数组中的每个字符进行哈希运算，生成整型哈希值 */
    return hash;
}

/* ----------------------------- API implementation ------------------------- */

/* Reset an hashtable already initialized with ht_init().
 * NOTE: This function should only called by ht_destroy(). */
static void _dictReset(dict *ht)  /* 初始化字典，传入字典指针 */
{
    ht->table = NULL;  // 初始化哈希表数组为空
    ht->size = 0;  // 初始化哈希表大小
    ht->sizemask = 0;  // 初始化哈希表大小掩码，用于计算索引值
    ht->used = 0;  // 初始化哈希表已有节点的个数0
}

/* Create a new hash table */
dict *dictCreate(dictType *type,  /* 创建1个哈希表，传入类型和私有数据指针 */
        void *privDataPtr)  /* 字典类型再文件末尾定义，分别为dictTypeHeapStringCopyKey、dictTypeHeapStrings、dictTypeHeapStringCopyKeyValue */
{
    dict *ht = _dictAlloc(sizeof(*ht));  // 给哈希表分配空间

    _dictInit(ht,type,privDataPtr);  // 初始化字典，方法内部调用了 _dictReset()
    return ht;  // 返回哈希表
}

/* Initialize the hash table */
int _dictInit(dict *ht, dictType *type,  /* 初始化哈希表，传入哈希表、类型、私有数据指针 */
        void *privDataPtr)
{
    _dictReset(ht);  // 调用 _dictReset()，初始化哈希表的4个成员变量
    ht->type = type;  // 初始化哈希表的类型
    ht->privdata = privDataPtr;  // 初始化哈希表的私有数据
    return DICT_OK;  // 返回成功标识0
}

/* Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USER/BUCKETS ration near to <= 1 */
int dictResize(dict *ht)  /* 扩容字典 */
{
    int minimal = ht->used;  // 获取字典中已有节点的数量

    if (minimal < DICT_HT_INITIAL_SIZE)  // 如果used小于等于4
        minimal = DICT_HT_INITIAL_SIZE;  // 将size扩展到4个
    return dictExpand(ht, minimal);
}

/* Expand or create the hashtable 扩容或创建哈希表 */
int dictExpand(dict *ht, unsigned long size)  /* 扩容字典，传入哈希表、要扩展到的大小 */
{
    dict n; /* the new hashtable */  // 初始化1个临时字典
    unsigned long realsize = _dictNextPower(size), i;  // 

    /* the size is invalid if it is smaller than the number of
     * elements already inside the hashtable */
    if (ht->used > size)  // 如果要扩展到的大小小于已存在的个数
        return DICT_ERR;  // 返回错误

    _dictInit(&n, ht->type, ht->privdata);  // 初始化字典n
    n.size = realsize;  // 将n的size设置为扩容后的大小
    n.sizemask = realsize-1;  // 将n的大小掩码设置为扩容后大小减1
    n.table = _dictAlloc(realsize*sizeof(dictEntry*));  // 分配n的哈希表空间为扩容后的大小

    /* Initialize all the pointers to NULL */
    memset(n.table, 0, realsize*sizeof(dictEntry*));  // 将哈希表中的指针都初始化为空

    /* Copy all the elements from the old to the new table:  
     * note that if the old hash table is empty ht->size is zero,
     * so dictExpand just creates an hash table. */  /* 把旧哈希表的所有元素拷贝到新哈希表中, 注意：如果旧哈希表是空的、size是0，扩容只会创建一个新的哈希表 */
    n.used = ht->used;  // 初始化已有节点个数
    for (i = 0; i < ht->size && ht->used > 0; i++) {  // 遍历哈希表的元素
        dictEntry *he, *nextHe;  // 定义entry和下一个entry

        if (ht->table[i] == NULL) continue;  // 如果某个索引的entry为空，继续找下一个
        
        /* For each hash entry on this slot... */
        he = ht->table[i];  // 如果找到entry
        while(he) {
            unsigned int h;  // 初始化索引变量h

            nextHe = he->next;  // 保存下一个entry位置
            /* Get the new element index */
            h = dictHashKey(ht, he->key) & n.sizemask;  // 找到新元素索引
            he->next = n.table[h];
            n.table[h] = he;  // 将找到的entry保存到字典n中的哈希表对应的索引位置
            ht->used--;  // 将旧哈希表待迁移的节点数减1
            /* Pass to the next element */
            he = nextHe;  // 向后继续查找entry
        }
    }
    assert(ht->used == 0);  // 断言待迁移的节点数为0
    _dictFree(ht->table);  // 释放旧哈希表

    /* Remap the new hashtable in the old */
    *ht = n;  // 将哈希表指针指向新的哈希表n
    return DICT_OK;  // 返回成功标识0
}

/* Add an element to the target hash table */
int dictAdd(dict *ht, void *key, void *val)  /* 向字典表添加key,value，传入key,val */
{
    int index;  // 初始化索引值
    dictEntry *entry;  // 初始化entry

    /* Get the index of the new element, or -1 if
     * the element already exists. */  /* 获取元素的索引,如果元素存在则返回-1 */
    if ((index = _dictKeyIndex(ht, key)) == -1)  // 只允许添加不存在的key,如果key已存在返回error
        return DICT_ERR;

    /* Allocates the memory and stores key */ /* 分配内存存储key */
    entry = _dictAlloc(sizeof(*entry));  // 计算entry的大小
    entry->next = ht->table[index];  // 将entry的后向指针指向哈希表当前位置
    ht->table[index] = entry;  // 在哈希表当前位置存储entry

    /* Set the hash entry fields. */  /* 设置entry的成员变量key,val */
    dictSetHashKey(ht, entry, key);  // 设置key
    dictSetHashVal(ht, entry, val);  // 设置val
    ht->used++;  // 把哈希表的used+1
    return DICT_OK;  // 返回成功标识0
}

/* Add an element, discarding the old if the key already exists.
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation. */  /* 如果元素已存在则添加一个元素并丢弃旧值。如果新增元素返回1，替换元素返回0 */
int dictReplace(dict *ht, void *key, void *val)  /* 字典替换操作,传入哈希表,key,val */
{
    dictEntry *entry, auxentry;  // 初始化entry和辅助entry

    /* Try to add the element. If the key
     * does not exists dictAdd will suceed. */    /* 尝试添加元素，如果不存在则会成功 */
    if (dictAdd(ht, key, val) == DICT_OK)  // 添加entry成功结果为0
        return 1;  // 返回1
    /* It already exists, get the entry */  /* 如果已存在则获取entry */
    entry = dictFind(ht, key);  // 如果找到则返回已找到的entry的指针地址
    /* Free the old value and set the new one */  /* 释放旧值且设置新值 */
    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse. */
    auxentry = *entry;  // 将entry指针地址转存到辅助entry中
    dictSetHashVal(ht, entry, val);  // 替换entry的val值
    dictFreeEntryVal(ht, &auxentry);  // 释放entry的旧val值
    return 0;  // 替换成功返回0
}

/* Search and remove an element 查找并删除1个元素 */
static int dictGenericDelete(dict *ht, const void *key, int nofree)  /* 通用删除,传入哈希表,key,nofree内存释放标志 */
{
    unsigned int h;  // 初始化h
    dictEntry *he, *prevHe;  // 初始化2个指针,前向指针和当前指针

    if (ht->size == 0)  // 如果哈希表没有元素
        return DICT_ERR;  // 返回error
    h = dictHashKey(ht, key) & ht->sizemask;  // 计算key的哈希值,找到哈希表中key的位置
    he = ht->table[h];  // 获取哈希表中哈希值为h的entry

    prevHe = NULL;  // 设置前向指针为空
    while(he) {  // 如果找到了entry
        if (dictCompareHashKeys(ht, key, he->key)) {  // 则比较哈希表中找到的key和输入的key是否相等
            /* Unlink the element from the list */
            if (prevHe)  // 如果前向指针有值,即哈希表中找到的key不是第1个key
                prevHe->next = he->next;  // 则将前1个元素的后向指针指向下一个entry
            else  // 如果在哈希表中找到的key是第1个key
                ht->table[h] = he->next;  // 则将哈希表的头指针跳过当前entry，后移1个entry
            if (!nofree) {  // 如果nofree为空
                dictFreeEntryKey(ht, he);  // 释放key的内存
                dictFreeEntryVal(ht, he);  // 释放val的内存
            }
            _dictFree(he);  // 释放指针指向的内存
            ht->used--;  // used -1
            return DICT_OK;  // 返回成功标识0
        }
        prevHe = he;  // 后移指针
        he = he->next;  // 查找下一个entry
    }
    return DICT_ERR; /* not found 没找到要删除的entry返回error */
}

int dictDelete(dict *ht, const void *key) {  // 删除key且释放内存
    return dictGenericDelete(ht,key,0);  // nofree传入0
}

int dictDeleteNoFree(dict *ht, const void *key) {  // 删除key不释放内存
    return dictGenericDelete(ht,key,1);  // nofree传入1
}

/* Destroy an entire hash table 销毁哈希表 */
int _dictClear(dict *ht)  /* 清空字典,传入哈希表 */
{
    unsigned long i;  // 初始化i

    /* Free all the elements 释放所有元素 */
    for (i = 0; i < ht->size && ht->used > 0; i++) {  // 遍历哈希表中的所有元素
        dictEntry *he, *nextHe;  // 初始化entry指针和后向指针

        if ((he = ht->table[i]) == NULL) continue;  // 如果某个哈希(索引)值为null,则判断下个哈希(索引)值
        while(he) {  // 如果entry存在
            nextHe = he->next;  // 把后一个entry的指针地址赋值给后向指针
            dictFreeEntryKey(ht, he);  // 释放当前entry的key
            dictFreeEntryVal(ht, he);  // 释放当前entry的val
            _dictFree(he);  // 释放指针指向的内存
            ht->used--;  // used -1
            he = nextHe;  // 向后遍历一个entry
        }
    }
    /* Free the table and the allocated cache structure 释放哈希表和已分配的缓存结构(entry槽位) */
    _dictFree(ht->table);  // 释放哈希表的内存
    /* Re-initialize the table */
    _dictReset(ht);  // 初始化哈希表
    return DICT_OK; /* never fails */
}

/* Clear & Release the hash table 清空并释放哈希表 */
void dictRelease(dict *ht)  /* 释放字典 */
{
    _dictClear(ht);  // 清空字典
    _dictFree(ht);  // 释放字典
}

dictEntry *dictFind(dict *ht, const void *key)  /* 在字典中查找key,传入哈希表,key */
{
    dictEntry *he;  // 初始化entry指针变量
    unsigned int h;  // 初始化h

    if (ht->size == 0) return NULL;  // 如果哈希表中没有元素返回null
    h = dictHashKey(ht, key) & ht->sizemask;  // 获取传入key的哈希(索引)值
    he = ht->table[h];  // 获取头指针指向的entry
    while(he) {  // 如果entry存在
        if (dictCompareHashKeys(ht, key, he->key))  // 比较哈希表中的key和传入的key是否相同
            return he;  // 相同则返回哈希表中的entry
        he = he->next;  // 向后遍历一个entry
    }
    return NULL;  // 遍历完成后还未找到则返回null
}

dictIterator *dictGetIterator(dict *ht)  /* 生成字典迭代器 */
{
    dictIterator *iter = _dictAlloc(sizeof(*iter));  // 给迭代器分配内存

    iter->ht = ht;  // 初始化迭代器的哈希表
    iter->index = -1;  // 初始化迭代器的索引:-1,为了+1后为0,能够在不增加代码复杂度的前提下,遍历所有元素
    iter->entry = NULL;  // 初始化entry为null
    iter->nextEntry = NULL;  // 初始化后向指针为null
    return iter;  // 返回迭代器指针变量
}

dictEntry *dictNext(dictIterator *iter)  /* 遍历字典迭代器 */
{
    while (1) {  // 循环执行
        if (iter->entry == NULL) {  // 如果当前哈希索引值遍历结束
            iter->index++;  // 则将索引值+1,遍历下一个哈希值的entry
            if (iter->index >=
                    (signed)iter->ht->size) break;  // 直到哈希索引值超出了哈希表的索引值个数，跳出死循环
            iter->entry = iter->ht->table[iter->index];
        } else {  // 当前索引值遍历未结束时
            iter->entry = iter->nextEntry;  // 向后遍历一个entry
        }
        if (iter->entry) {  // 如果当前entry存在
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */  /* 我们需要保存下一个entry的地址,因为迭代器用户可能会删除我们要返回的条目 */
            iter->nextEntry = iter->entry->next;  // 则保存下一个entry的地址到后向指针变量中
            return iter->entry;  // 返回当前entry
        }
    }
    return NULL;  // 跳出后返回null
}

void dictReleaseIterator(dictIterator *iter)  /* 释放字典迭代器 */
{
    _dictFree(iter);  // 释放迭代器指针指向的内存
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms */  /* 返回哈希表中的随机条目。对于实现随机化算法很有用 */
dictEntry *dictGetRandomKey(dict *ht)  /* 获取字典中1个随机key */
{
    dictEntry *he;  // 初始化entry指针变量
    unsigned int h;  // 初始化哈希索引值h
    int listlen, listele;  // 初始化listlen,listle

    if (ht->used == 0) return NULL;  // 如果哈希表的used为0,返回null
    do {
        h = random() & ht->sizemask;  // 获取随机哈希索引值
        he = ht->table[h];  // 找到1个头指针指向的entry,即指向了1个随机的哈希桶
    } while(he == NULL);  // entry为null时继续遍历,知道找到1个非空的entry

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is to count the element and
     * select a random index. */  /* 此时我们找到了1个非空桶,但它指向了1个链表,我们需要从这个链表中找到1个随机元素。唯一明智的方法是计算元素数量并选择一个随机索引。 */
    listlen = 0;  // 初始化链表长度为0
    while(he) {  // 当entry非空时
        he = he->next;  // 向后遍历一个entry
        listlen++;  // 累加得到链表的元素总数
    }
    listele = random() % listlen;  // 生成链表的随机索引值
    he = ht->table[h];  // 重新指向刚才找到的随机链表
    while(listele--) he = he->next;  // 遍历链表,直到返回随机entry
    return he;  // 返回entry
}

/* ------------------------- private functions ------------------------------ */
/* 私有方法 */
/* Expand the hash table if needed 如果需要的话,扩展哈希表 */
static int _dictExpandIfNeeded(dict *ht)  /* 按需扩展字典,传入哈希表 */
{
    /* If the hash table is empty expand it to the intial size,
     * if the table is "full" dobule its size. */  /* 如果哈希表为空扩展到初始化大小,如果哈希表满了则扩展到2倍大小 */
    if (ht->size == 0)  // 如果size为0
        return dictExpand(ht, DICT_HT_INITIAL_SIZE);  // 扩展到 DICT_HT_INITIAL_SIZE
    if (ht->used == ht->size)  // 如果size用完了
        return dictExpand(ht, ht->size*2);  // 扩展到size的2倍
    return DICT_OK;  // 返回成功标识0
}

/* Our hash table capability is a power of two */  /* 哈希表entry的容量是2的幂 */
static unsigned long _dictNextPower(unsigned long size)  /* 获取字典size的下一个幂值 */
{
    unsigned long i = DICT_HT_INITIAL_SIZE;  // 初始化i为4

    if (size >= LONG_MAX) return LONG_MAX;  // 如果size大于等于long型数的最大值，则返回最大值
    while(1) {
        if (i >= size)
            return i;
        i *= 2;  // 如果i小于size，则乘以2
    }
}

/* Returns the index of a free slot that can be populated with
 * an hash entry for the given 'key'.
 * If the key already exists, -1 is returned. */  /* 返回一个可用的空槽位的索引，该槽位可以用于存储给定“key”的哈希表条目。如果key存在则返回-1 */
static int _dictKeyIndex(dict *ht, const void *key)
{
    unsigned int h;  // 初始化哈希索引值
    dictEntry *he;  // 初始化entry指针变量

    /* Expand the hashtable if needed */
    if (_dictExpandIfNeeded(ht) == DICT_ERR)  // 按需扩展哈希表,如果结果为1
        return -1;  // 返回-1
    /* Compute the key hash value 比较key的哈希值 */
    h = dictHashKey(ht, key) & ht->sizemask;  // 获取哈希表中的key的哈希值
    /* Search if this slot does not already contain the given key 查找这个槽是否已经包含传入的key */
    he = ht->table[h];  // 找到1个头指针指向的entry,即指向了哈希桶
    while(he) {  // entry非空时遍历
        if (dictCompareHashKeys(ht, key, he->key))  // 比较传入的key和哈希表中的key是否相等
            return -1;  // 相等返回-1,即不用存储该key
        he = he->next;  // 向后遍历一个entry
    }
    return h;  // 返回哈希索引值
}

void dictEmpty(dict *ht) {  /* 清空字典,传入哈希表 */
    _dictClear(ht);  // 调用私有方法清空哈希表
}

#define DICT_STATS_VECTLEN 50  // 定义字典统计信息中链表的个数
void dictPrintStats(dict *ht) {  /* 打印字典统计信息,方便调试,传入哈希表 */
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;  // 初始化i,链表长度. 初始化哈希槽个数为0,最大链表长度为0
    unsigned long totchainlen = 0;  // 初始化链表总长度为0
    unsigned long clvector[DICT_STATS_VECTLEN];  // 初始化链表向量数组,长度50(即统计直方图,包含N个元素的链表有几个,N<=49)

    if (ht->used == 0) {  // 如果哈希表used为0
        printf("No stats available for empty dictionaries\n");  // 输出:空字典没有可用的统计数据
        return;  // 直接返回
    }

    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;  // 初始化向量数组中各元素为0
    for (i = 0; i < ht->size; i++) {  // 遍历哈希表
        dictEntry *he;  // 初始化entry

        if (ht->table[i] == NULL) {  // 如果哈希表中某个链表为空
            clvector[0]++;  // 则统计直方图中0个元素的链表个数+1
            continue;
        }
        slots++;  // 哈希槽个数+1
        /* For each hash entry on this slot... 继续遍历这个哈希槽上的每个entry */
        chainlen = 0;  // 初始化链表长度为0
        he = ht->table[i];  // 找到1个头指针指向的entry,即指向了哈希桶
        while(he) {  // entry非空时遍历
            chainlen++;  // 链表长度+1,得到链表长度
            he = he->next;  // 向后遍历一个entry
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;  // 统计直方图中N个元素的链表个数+1.如果N<50则N=链表长度,如果N>=50,则N=49
        if (chainlen > maxchainlen) maxchainlen = chainlen;  // 如果链表长度>最大链表长度,则更新最大链表长度
        totchainlen += chainlen;  // 累加链表长度,得到链表总长度
    }
    printf("Hash table stats:\n");  // 输出:哈希表统计信息
    printf(" table size: %ld\n", ht->size);  // 表的大小
    printf(" number of elements: %ld\n", ht->used);  // 元素的个数
    printf(" different slots: %ld\n", slots);  // 哈希槽个数
    printf(" max chain length: %ld\n", maxchainlen);  // 最大链表长度
    printf(" avg chain length (counted): %.02f\n", (float)totchainlen/slots);  // 平均链表长度
    printf(" avg chain length (computed): %.02f\n", (float)ht->used/slots);  // 
    printf(" Chain length distribution:\n");  // 链长度分布情况
    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {  // 遍历直方图数组
        if (clvector[i] == 0) continue;  // 元素个数为0的链表不输出统计信息
        printf("   %s%ld: %ld (%.02f%%)\n",(i == DICT_STATS_VECTLEN-1)?">= ":"", i, clvector[i], ((float)clvector[i]/ht->size)*100);  // 输出每个非空的链表的元素个数占哈希表的百分比
    }
}

/* ----------------------- StringCopy Hash Table Type ------------------------*/
/* 哈希表类型结构体的字符串拷贝方法 */
static unsigned int _dictStringCopyHTHashFunction(const void *key)
{
    return dictGenHashFunction(key, strlen(key));
}

static void *_dictStringCopyHTKeyDup(void *privdata, const void *key)
{
    int len = strlen(key);
    char *copy = _dictAlloc(len+1);
    DICT_NOTUSED(privdata);

    memcpy(copy, key, len);
    copy[len] = '\0';
    return copy;
}

static void *_dictStringKeyValCopyHTValDup(void *privdata, const void *val)
{
    int len = strlen(val);
    char *copy = _dictAlloc(len+1);
    DICT_NOTUSED(privdata);

    memcpy(copy, val, len);
    copy[len] = '\0';
    return copy;
}

static int _dictStringCopyHTKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcmp(key1, key2) == 0;
}

static void _dictStringCopyHTKeyDestructor(void *privdata, void *key)
{
    DICT_NOTUSED(privdata);

    _dictFree((void*)key); /* ATTENTION: const cast */
}

static void _dictStringKeyValCopyHTValDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    _dictFree((void*)val); /* ATTENTION: const cast */
}
/* 定义了3种字典类型 */
dictType dictTypeHeapStringCopyKey = {  /* 字典类型 */
    _dictStringCopyHTHashFunction,        /* hash function */
    _dictStringCopyHTKeyDup,              /* key dup */
    NULL,                               /* val dup */
    _dictStringCopyHTKeyCompare,          /* key compare */
    _dictStringCopyHTKeyDestructor,       /* key destructor */
    NULL                                /* val destructor */
};

/* This is like StringCopy but does not auto-duplicate the key.
 * It's used for intepreter's shared strings. */
dictType dictTypeHeapStrings = {
    _dictStringCopyHTHashFunction,        /* hash function */
    NULL,                               /* key dup */
    NULL,                               /* val dup */
    _dictStringCopyHTKeyCompare,          /* key compare */
    _dictStringCopyHTKeyDestructor,       /* key destructor */
    NULL                                /* val destructor */
};

/* This is like StringCopy but also automatically handle dynamic
 * allocated C strings as values. */
dictType dictTypeHeapStringCopyKeyValue = {
    _dictStringCopyHTHashFunction,        /* hash function */
    _dictStringCopyHTKeyDup,              /* key dup */
    _dictStringKeyValCopyHTValDup,        /* val dup */
    _dictStringCopyHTKeyCompare,          /* key compare */
    _dictStringCopyHTKeyDestructor,       /* key destructor */
    _dictStringKeyValCopyHTValDestructor, /* val destructor */
};
