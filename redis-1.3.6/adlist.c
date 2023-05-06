/* adlist.c - A generic doubly linked list implementation
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
list *listCreate(void)  /* 创建双向无环链表 */
{
    struct list *list;

    if ((list = zmalloc(sizeof(*list))) == NULL)  // 分配内存
        return NULL;
    list->head = list->tail = NULL;  // 初始化头节点前指针和尾节点后指针的地址为空，实现无环
    list->len = 0;  // 初始化长度为0
    list->dup = NULL;  // 链表dup方法初始化为空
    list->free = NULL;  // 链表free方法初始化为空
    list->match = NULL;  // 链表match方法初始化为空
    return list;
}

/* Free the whole list.
 *
 * This function can't fail. */
void listRelease(list *list)  /* 释放指定的链表及其中的所有节点 */
{
    unsigned int len;
    listNode *current, *next;  // 定义当前节点指针和下一个节点指针

    current = list->head;  // 找到头节点
    len = list->len;  // 获取链表长度
    while(len--) {
        next = current->next;  // 获取指向下一个节点的指针
        if (list->free) list->free(current->value);  // 如果node->value单独分配内存，需要free函数释放掉
        zfree(current);  // 释放掉当前节点空间
        current = next;  // 跳到下一个节点
    }
    zfree(list);  // 最后释放整个链表
}

/* Add a new node to the list, to head, contaning the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeHead(list *list, void *value)  /* 向链表头插入节点 */
{
    listNode *node;  // 定义节点结构体指针

    if ((node = zmalloc(sizeof(*node))) == NULL)  // 给节点结构体分配内存
        return NULL;
    node->value = value;  // 给节点的value成员赋值
    if (list->len == 0) {  // 如果链表长度为0
        list->head = list->tail = node;  // 则链表头尾指针都指向该节点
        node->prev = node->next = NULL;  // 该节点的前向指针、后向指针都为空
    } else {  // 如果链表长度不为0
        node->prev = NULL;  // 节点的前向指针为空
        node->next = list->head;  // 后向指针指向头指针指向的节点
        list->head->prev = node;  // 原头指针指向的节点，前向指针指向该节点
        list->head = node;  // 更新链表的头指针指向该节点
    }
    list->len++;  // 更新链表的长度
    return list;  // 返回链表
}

/* Add a new node to the list, to tail, contaning the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeTail(list *list, void *value)  /* 向链表尾插入节点 */
{
    listNode *node;  // 定义节点结构体指针

    if ((node = zmalloc(sizeof(*node))) == NULL)  //给节点结构体分配内存
        return NULL;
    node->value = value;  // 给节点的value成员赋值
    if (list->len == 0) {  // 同上
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {  // 如果链表长度不为0
        node->prev = list->tail;  // 将该节点的前向指针，指向链表的尾指针指向的节点
        node->next = NULL;  // 该节点的后向指针为空
        list->tail->next = node;  // 链表尾指针指向的节点，后向指针指向该节点
        list->tail = node;  // 更新链表尾指针指向该节点
    }
    list->len++;  // 更新链表的长度
    return list;  // 返回链表
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
void listDelNode(list *list, listNode *node)  /* 从链表删除节点 1-2-3 */
{
    if (node->prev)  // 如果当前节点2前边存在节点1
        node->prev->next = node->next;  // 则节点1的后向指针指向节点3
    else  // 如果当前节点1前边不存在节点
        list->head = node->next;  // 则链表头指针指向节点2
    if (node->next)  // 如果当前节点2后边存在节点2
        node->next->prev = node->prev;  // 则节点3的前向节点指向节点1
    else  //如果当前节点3后边不存在节点
        list->tail = node->prev;  // 则链表尾指针指向节点2
    if (list->free) list->free(node->value);  // 如果链表有free方法，则释放当前节点的value成员
    zfree(node);  // 释放节点
    list->len--;  // 更新链表长度
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */
listIter *listGetIterator(list *list, int direction)  /* 生成指向特定方向的迭代器 */
{
    listIter *iter;  // 定义链表迭代器结构体指针
    
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;  // 给迭代器结构体分配内存
    if (direction == AL_START_HEAD)  // 如果迭代器的方向时从头开始
        iter->next = list->head;  // 迭代器下一个节点为链表头指针指向的节点
    else  // 如果迭代器的方向时从尾开始
        iter->next = list->tail;  // 迭代器下一个节点为链表尾指针指向的节点
    iter->direction = direction;  // iter结构体direction成员保存方向
    return iter;  // 返回迭代器
}

/* Release the iterator memory */
void listReleaseIterator(listIter *iter) {  /* 释放链表迭代器 */
    zfree(iter);  // 释放迭代器
}

/* Create an iterator in the list private iterator structure */
void listRewind(list *list, listIter *li) {  /* 链表迭代器倒带 */
    li->next = list->head;  // 迭代器下一个节点为链表头指针指向的节点
    li->direction = AL_START_HEAD;  // 迭代器的方向时从头开始
}

void listRewindTail(list *list, listIter *li) {  /* 链表迭代器倒带到尾部 */
    li->next = list->tail;  // 迭代器下一个节点为链表尾指针指向的节点
    li->direction = AL_START_TAIL;  // 迭代器的方向时从尾开始
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetItarotr(list,<direction>);
 * while ((node = listNextIterator(iter)) != NULL) {
 *     DoSomethingWith(listNodeValue(node));
 * }
 *
 * */
listNode *listNext(listIter *iter)  /* 对迭代器进行迭代 */
{
    listNode *current = iter->next;  // 定义当前链表节点结构体指针，指向迭代器的下一个节点

    if (current != NULL) {  // 如果迭代器没有迭代完
        if (iter->direction == AL_START_HEAD)  // 如果迭代器的方向是从头开始
            iter->next = current->next;  // 迭代器将从当前节点的右边继续迭代
        else
            iter->next = current->prev;  // 反之则从当前节点的左边继续迭代
    }
    return current;  // 返回迭代到的当前节点
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */
list *listDup(list *orig)  /* 复制链表 */
{
    list *copy;  // 定义链表复制结构体指针
    listIter *iter;  // 定义链表迭代器结构体指针
    listNode *node;  // 定义链表节点结构体指针

    if ((copy = listCreate()) == NULL)  // 给链表复制结构体指针分配内存
        return NULL;  // 无可用内存返回null
    copy->dup = orig->dup;  // 复制原链表的dup方法
    copy->free = orig->free;  // 复制原链表的free方法
    copy->match = orig->match;  // 复制原链表的match方法
    iter = listGetIterator(orig, AL_START_HEAD);  // 生成1个正向原链表迭代器
    while((node = listNext(iter)) != NULL) {  // 如果原链表迭代器没有迭代完
        void *value;  // 定义value指针

        if (copy->dup) {  // 如果存在dup方法
            value = copy->dup(node->value);  // 则通过dup方法将原链表中节点的value赋值到新链表中节点
            if (value == NULL) {  // 如果复制完成
                listRelease(copy);  // 释放copy结构体指针
                listReleaseIterator(iter);  // 释放原链表迭代器结构体指针
                return NULL;
            }
        } else  // 如果没有dup方法
            value = node->value;  // 则采用直接赋值的方法复制value
        if (listAddNodeTail(copy, value) == NULL) {  // 如果原链表中所有节点都赋值完成
            listRelease(copy);
            listReleaseIterator(iter);  // 同上
            return NULL;
        }
    }
    listReleaseIterator(iter);  // 释放迭代器结构体指针
    return copy;  // 返回复制后生成的新链表
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
listNode *listSearchKey(list *list, void *key)  /* 从链表中查询key */
{
    listIter *iter;  // 定义迭代器结构体指针
    listNode *node;  // 定义链表节点指针

    iter = listGetIterator(list, AL_START_HEAD);  // 生成1个正向链表迭代器
    while((node = listNext(iter)) != NULL) {  // 如果迭代器没有迭代完
        if (list->match) {  // 如果存在match方法
            if (list->match(node->value, key)) {  // 则通过match方法匹配key和value是否一致
                listReleaseIterator(iter);  // 释放迭代器结构体指针
                return node;  // 返回节点
            }
        } else {  // 如果不存在match方法
            if (key == node->value) {  // 使用等值运算符比较
                listReleaseIterator(iter);  // 同上
                return node;
            }
        }
    }
    listReleaseIterator(iter);  // 释放迭代器结构体指针
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimante
 * and so on. If the index is out of range NULL is returned. */
listNode *listIndex(list *list, int index) {  /* 适用链表索引查找节点 */
    listNode *n;  // 定义链表节点指针

    if (index < 0) {  // 反向查找
        index = (-index)-1;  // 将负数索引转换为正数索引
        n = list->tail;  // 节点指向链表尾节点
        while(index-- && n) n = n->prev;  // 索引值自减，指针向前逐个滚动
    } else {  // 正向查找
        n = list->head;  // 节点指向链表头指针
        while(index-- && n) n = n->next;  // 索引值自减，指针向后逐个滚动
    }
    return n;  // 返回目标节点
}
