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
    listNode *current, *next;

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
void listDelNode(list *list, listNode *node)  /* 从链表删除节点 */
{
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;
    if (list->free) list->free(node->value);
    zfree(node);
    list->len--;
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */
listIter *listGetIterator(list *list, int direction)  /* 生成指向特定方向的迭代器 */
{
    listIter *iter;
    
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else
        iter->next = list->tail;
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory */
void listReleaseIterator(listIter *iter) {  /* 释放链表迭代器 */
    zfree(iter);  // 释放迭代器
}

/* Create an iterator in the list private iterator structure */
void listRewind(list *list, listIter *li) {  /* 链表迭代器倒带 */
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

void listRewindTail(list *list, listIter *li) {  /* 链表迭代器倒带到尾部 */
    li->next = list->tail;
    li->direction = AL_START_TAIL;
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
listNode *listNext(listIter *iter)
{
    listNode *current = iter->next;

    if (current != NULL) {
        if (iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }
    return current;
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
    list *copy;
    listIter *iter;
    listNode *node;

    if ((copy = listCreate()) == NULL)
        return NULL;
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    iter = listGetIterator(orig, AL_START_HEAD);
    while((node = listNext(iter)) != NULL) {
        void *value;

        if (copy->dup) {
            value = copy->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        } else
            value = node->value;
        if (listAddNodeTail(copy, value) == NULL) {
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }
    listReleaseIterator(iter);
    return copy;
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
listNode *listSearchKey(list *list, void *key)
{
    listIter *iter;
    listNode *node;

    iter = listGetIterator(list, AL_START_HEAD);
    while((node = listNext(iter)) != NULL) {
        if (list->match) {
            if (list->match(node->value, key)) {
                listReleaseIterator(iter);
                return node;
            }
        } else {
            if (key == node->value) {
                listReleaseIterator(iter);
                return node;
            }
        }
    }
    listReleaseIterator(iter);
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimante
 * and so on. If the index is out of range NULL is returned. */
listNode *listIndex(list *list, int index) {  /* 适用链表索引查找节点 */
    listNode *n;

    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }
    return n;
}
