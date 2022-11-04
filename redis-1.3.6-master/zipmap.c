/* String -> String Map data structure optimized for size.
 * This file implements a data structure mapping strings to other strings
 * implementing an O(n) lookup data structure designed to be very memory
 * efficient.
 *
 * The Redis Hash type uses this data structure for hashes composed of a small
 * number of elements, to switch to an hash table once a given number of
 * elements is reached.
 *
 * Given that many times Redis Hashes are used to represent objects composed
 * of few fields, this is a very big win in terms of used memory.
 *
 * --------------------------------------------------------------------------
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

/* Memory layout of a zipmap, for the map "foo" => "bar", "hello" => "world":
 *
 * <status><len>"foo"<len><free>"bar"<len>"hello"<len><free>"world"<end>
 * len(key) > 252 -- 5个字节存储长度，0xfd 后面4个字节存储长度
 * 
 * <status> is 1 byte status. Currently only 1 bit is used: if the least
 * significant bit is set, it means the zipmap needs to be defragmented.
 * 
 * <status> 占用1个字节，当前只使用最低1位，如果是1表示需要进行碎片整理
 *
 * <len> is the length of the following string (key or value).
 * <len> lengths are encoded in a single value or in a 5 bytes value.
 * If the first byte value (as an unsigned 8 bit value) is between 0 and
 * 252, it's a single-byte length. If it is 253 then a four bytes unsigned
 * integer follows (in the host byte ordering). A value fo 255 is used to
 * signal the end of the hash. The special value 254 is used to mark
 * empty space that can be used to add new key/value pairs.
 * 
 * <len> 的长度为1或5，如果第1个字节值介于0～252之间，len是1；如果是253，len为5；255为结束标志，254表示可以添加新的(key,value)对
 *
 * <free> is the number of free unused bytes
 * after the string, resulting from modification of values associated to a
 * key (for instance if "foo" is set to "bar', and later "foo" will be se to
 * "hi", I'll have a free byte to use if the value will enlarge again later,
 * or even in order to add a key/value pair if it fits.
 *
 * <free> is always an unsigned 8 bit number, because if after an
 * update operation there are more than a few free bytes, they'll be converted
 * into empty space prefixed by the special value 254.
 * 
 * <free> 是空闲字节数，1个字节长度，如果更新后有多个空闲字节，它会被转换成以特殊值254为前缀的空白
 *
 * The most compact representation of the above two elements hash is actually:
 *
 * "\x00\x03foo\x03\x00bar\x05hello\x05\x00world\xff"
 * 
 * 2个(key,value)对实际是这样的 --- status:\x00 len:\x03 "foo" len:\x03 free:\x00 "bar" len:\x05 "hello" len:\x05 free:\x00 "world" end:\xff
 *
 * Empty space is marked using a 254 bytes + a <len> (coded as already
 * specified). The length includes the 254 bytes in the count and the
 * space taken by the <len> field. So for instance removing the "foo" key
 * from the zipmap above will lead to the following representation:
 *
 * "\x00\xfd\x10........\x05hello\x05\x00world\xff"
 *
 * Note that because empty space, keys, values, are all prefixed length
 * "objects", the lookup will take O(N) where N is the numeber of elements
 * in the zipmap and *not* the number of bytes needed to represent the zipmap.
 * This lowers the constant times considerably.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "zmalloc.h"

#define ZIPMAP_BIGLEN 253  // 长度0～252
#define ZIPMAP_EMPTY 254  // 0xfe
#define ZIPMAP_END 255  // 0xff zipmap结束的标志

#define ZIPMAP_STATUS_FRAGMENTED 1  // 碎片标识，1 表示有碎片

/* The following defines the max value for the <free> field described in the
 * comments above, that is, the max number of trailing bytes in a value. */
#define ZIPMAP_VALUE_MAX_FREE 5

/* The following macro returns the number of bytes needed to encode the length
 * for the integer value _l, that is, 1 byte for lengths < ZIPMAP_BIGLEN and
 * 5 bytes for all the other lengths. */
#define ZIPMAP_LEN_BYTES(_l) (((_l) < ZIPMAP_BIGLEN) ? 1 : sizeof(unsigned int)+1)  // len 如果小于253，在内存中占1字节；大于等于253，在内存中占5字节

/* Create a new empty zipmap. */
unsigned char *zipmapNew(void) {  /* 初始化1个zipmap */
    unsigned char *zm = zmalloc(2);  // 分配1个2字节长度的zipmap

    zm[0] = 0; /* Status，0-没有empty block，1-有empty block */
    zm[1] = ZIPMAP_END;  // 结束标志
    return zm;
}

/* Decode the encoded length pointed by 'p' */
static unsigned int zipmapDecodeLength(unsigned char *p) {  /* 对zipmap长度进行解码，返回<len>的长度 */
    unsigned int len = *p;  // 拿到p指向的第1个字节的值，p是指向zipmap开头，如果首地址保存的值小于0xfe，则该字节表示该zipmap的长度

    if (len < ZIPMAP_BIGLEN) return len;  // 如果len小于253，直接返回
    memcpy(&len,p+1,sizeof(unsigned int));  // 如果len大于等于253，则p向后移动1个字节，读取4个字节，返回给len
    return len;
}

/* Encode the length 'l' writing it in 'p'. If p is NULL it just returns
 * the amount of bytes required to encode such a length. */
static unsigned int zipmapEncodeLength(unsigned char *p, unsigned int len) {  /* 对长度进行编码，返回该编码需要的字节数，传入指向字符数组中单个字符的指针和字符数组长度 */
    if (p == NULL) {  // 如果没有传入指针
        return ZIPMAP_LEN_BYTES(len);  // 是用宏定义方法返回字节占用的长度
    } else {
        if (len < ZIPMAP_BIGLEN) {  // 如果len小于253
            p[0] = len;  // 直接将len保存该值到首字节
            return 1;  // 内存占用1字节
        } else {  // len大于等于253
            p[0] = ZIPMAP_BIGLEN;
            memcpy(p+1,&len,sizeof(len));  // p+1 指针跳过首字节，&len 取len的地址，sizeof 长度4字节
            return 1+sizeof(len);  // 内存占用5字节
        }
    }
}

/* Search for a matching key, returning a pointer to the entry inside the
 * zipmap. Returns NULL if the key is not found.
 *
 * If NULL is returned, and totlen is not NULL, it is set to the entire
 * size of the zimap, so that the calling function will be able to
 * reallocate the original zipmap to make room for more entries.
 *
 * If NULL is returned, and freeoff and freelen are not NULL, they are set
 * to the offset of the first empty space that can hold '*freelen' bytes
 * (freelen is an integer pointer used both to signal the required length
 * and to get the reply from the function). If there is not a suitable
 * free space block to hold the requested bytes, *freelen is set to 0. */
static unsigned char *zipmapLookupRaw(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned int *totlen, unsigned int *freeoff, unsigned int *freelen) {
    /* 搜索指定长度的key，返回key的首地址；如果没有找到合适的key，返回NULL
    另外：返回NULL时，如果传入的totlen指针非NULL，则该值会设置成zipmap的总长度，并根据这个长度对zipmap扩容
    返回NULL时，如果传入的freeoff和freelen指针非NULL，则将它们设置到第一个可以容纳freelen值字节数的干净的空间。
    如果没有合适的空间保存请求的字节，则freelen被设置成0 */
    unsigned char *p = zm+1;  // zipmap指针右移1个字节指向，第1个<len>
    unsigned int l;  
    unsigned int reqfreelen = 0; /* initialized just to prevent warning 初始化1个值，防止没有接收到值报警 */

    if (freelen) {  // 如果freelen非NULL
        reqfreelen = *freelen;  // 将freelen的值保存起来
        *freelen = 0;  // 将freelen置0
        assert(reqfreelen != 0);  // 断言，如果表达式为假，向stderr输出1条error，并调用abort
    }
    while(*p != ZIPMAP_END) {  // 遍历zipmap直到指针指向end地址
        /* <status><len>"foo"<len><free>bar<empty><len>.....<end> */
        if (*p == ZIPMAP_EMPTY) {  // 如果指针找到了empty地址
            l = zipmapDecodeLength(p+1);  // 指针右移1字节，指向最后1个<len>，即zipmap中<empty>的首地址
            /* if the user want a free space report, and this space is
             * enough, and we did't already found a suitable space... */
            if (freelen && l >= reqfreelen && *freelen == 0) {  // 如果freelen指针存在，且空格长度满足需求
                *freelen = l;  // 更新freelen的指针指向空格的起始地址
                *freeoff = p-zm;  // 得到zipmap首地址到空格的偏移量
            }
            p += l;  // 跳到end位置
            zm[0] |= ZIPMAP_STATUS_FRAGMENTED;  // 更新<status>为1，表示zipmap有碎片
        } else {  // 如果没有找到empty
            unsigned char free;

            /* Match or skip the key */
            l = zipmapDecodeLength(p);  // 获取<len>长度
            if (l == klen && !memcmp(p+1,key,l)) return p;  // 找到符合条件的key并返回。p+1这里有bug，<len>可能是1也可以是5，这个bug在2.0版本修复
            p += zipmapEncodeLength(NULL,l) + l;  // 如果key不同，跳过<len>key
            /* Skip the value as well */
            l = zipmapDecodeLength(p); // 获取<len>长度
            p += zipmapEncodeLength(NULL,l);  // 跳过<len>
            free = p[0];  // free的字节长度
            p += l+1+free; /* +1 to skip the free byte 跳过<len>key<len><free>value */
        }
    }
    if (totlen != NULL) *totlen = (unsigned int)(p-zm)+1;  // 将totlen设置成zipmap的总长度
    return NULL;
}

static unsigned long zipmapRequiredLength(unsigned int klen, unsigned int vlen) {  /* 通过计算key和value的长度，得到保存该元素所需要的最小长度 -- <len>"key"<len><free>"value"的总长度 */
    unsigned int l;  // 定义保存所需的最短长度

    l = klen+vlen+3;  // <len><len><free>最少占用3字节
    if (klen >= ZIPMAP_BIGLEN) l += 4;  // 如果大于等于253，加4
    if (vlen >= ZIPMAP_BIGLEN) l += 4;  // 同上
    return l;
}

/* Return the total amount used by a key (encoded length + payload) */
static unsigned int zipmapRawKeyLength(unsigned char *p) {  /* 计算<len>"key"总的占用字节 */
    unsigned int l = zipmapDecodeLength(p);  // key的长度
    
    return zipmapEncodeLength(NULL,l) + l;  // <len>的长度「最终调用了 ZIPMAP_LEN_BYTES(len) 」+ key的长度
}

/* Return the total amount used by a value
 * (encoded length + single byte free count + payload) */
static unsigned int zipmapRawValueLength(unsigned char *p) {  /* 计算<len><free>"value"总的占用字节 */
    unsigned int l = zipmapDecodeLength(p);  // "value"占用的字节
    unsigned int used;
    
    used = zipmapEncodeLength(NULL,l);  // <len>占用的字节
    used += p[used] + 1 + l;  // p[used] 取出<free>的值，p([5]即 *(p + 5)
    return used;  // <len>占用的字节 + <free>占用的字节 + <free>的值 + "value"占用的字节
}

/* If 'p' points to a key, this function returns the total amount of
 * bytes used to store this entry (entry = key + associated value + trailing
 * free space if any). */
static unsigned int zipmapRawEntryLength(unsigned char *p) {  /* <len>"key"<len><free>"value"占用的字节 */
    unsigned int l = zipmapRawKeyLength(p);  // <len>"key"占用的字节

    return l + zipmapRawValueLength(p+l);  // p+l指向"value"值的地址
}

/* Set key to value, creating the key if it does not already exist.
 * If 'update' is not NULL, *update is set to 1 if the key was
 * already preset, otherwise to 0. */
unsigned char *zipmapSet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char *val, unsigned int vlen, int *update) {  /* zipmap中set key操作，即插入key。参数：zipmap、key字符串、key长度、value字符串、value长度、更新操作标志：1更新，0新增 */
    unsigned int oldlen = 0, freeoff = 0, freelen;  // zipmap的长度  <free>的偏移  需要的可用长度
    unsigned int reqlen = zipmapRequiredLength(klen,vlen);  // 要设置的key需要的字节
    unsigned int empty, vempty;
    unsigned char *p;  // 定义指针p
   
    freelen = reqlen;  // 设置zipmap需要预留的free空间大小
    if (update) *update = 0;  // 将update设置为0
    p = zipmapLookupRaw(zm,key,klen,&oldlen,&freeoff,&freelen);  // 查找key，如果查找结果为NULL，
    if (p == NULL && freelen == 0) {  // 如果zipmap中没有这个key，且没有可用空间可以存放key、value
        /* Key not found, and not space for the new key. Enlarge */
        zm = zrealloc(zm,oldlen+reqlen);  // 对zipmap进行扩容
        p = zm+oldlen-1;  // zm+oldlen 指向<end>尾部，-1是为了指向<end>头部
        zm[oldlen+reqlen-1] = ZIPMAP_END;  // 扩容后，将<end>标记右移
        freelen = reqlen;  // 此时预留的字节和需求的字节长度相等
    } else if (p == NULL) {  // 没有找到key，但是有足够的空间可以存储key、value
        /* Key not found, but there is enough free space. */
        p = zm+freeoff;  // 直接将指针p移动到zipmap中的<empty>的首位置
        /* note: freelen is already set in this case */
    } else {  // 如果key找到了，同时有足够的空间可以存储key、value
        unsigned char *b = p;  // 定义指针b，定位value，执行更新操作

        /* Key found. Is there enough space for the new value? */
        /* Compute the total length: */
        if (update) *update = 1;  // 如果update不为NULL，设置为1
        freelen = zipmapRawKeyLength(b);  // 获取zipmap中<len>key的长度
        b += freelen;  // 右移指针跳过key，到<len><free>value中<len>的首位置
        freelen += zipmapRawValueLength(b);  // 增加<len><free>value的长度，此时freelen存储的是一个entry的总长度
        if (freelen < reqlen) {  // 如果entry不足以容纳新的value，将entry标记为free，并进行递归调用；足够则跳到283行
            /* Mark this entry as free and recurse */
            p[0] = ZIPMAP_EMPTY;  // 将entry的第1个字节设置为empty标志位
            zipmapEncodeLength(p+1,freelen);  // 将entry的第2个字节设置为entry的总长度
            zm[0] |= ZIPMAP_STATUS_FRAGMENTED;  // 再将zipmap最前面的<status>设置为1，表示zipmap有碎片——此时该entry的空间设置为empty
            return zipmapSet(zm,key,klen,val,vlen,NULL);  // 递归调用zipmapSet方法，此时会在zipmap尾部保存key、value
        }
    }

    /* Ok we have a suitable block where to write the new key/value
     * entry. */
    empty = freelen-reqlen;  // 剩余的可用空间长度
    /* If there is too much free space mark it as a free block instead
     * of adding it as trailing empty space for the value, as we want
     * zipmaps to be very space efficient. */
    if (empty > ZIPMAP_VALUE_MAX_FREE) {  // empty大于5字节
        unsigned char *e;  // 定义empty指针e

        e = p+reqlen;  // 右移指针到<empty>开始位置
        e[0] = ZIPMAP_EMPTY;  // 将<empty>的第1个字节设置empty标志
        zipmapEncodeLength(e+1,empty);  // 将<empty>的第2个字节位置记录empty的长度
        vempty = 0;  // 更新<free>的值为0，因为此时空闲位置都标记成了empty
        zm[0] |= ZIPMAP_STATUS_FRAGMENTED;  // 再将zipmap最前面的<status>设置为1，表示zipmap有碎片
    } else {
        vempty = empty;  // 更新zipmap中<free>的值
    }

    /* Just write the key + value and we are done. */
    /* Key: */
    p += zipmapEncodeLength(p,klen);  // 设置key的<len>的值，同时右移指针p
    memcpy(p,key,klen);  // 将key的值保存到p指针的位置
    p += klen;  // 再次右移指针p
    /* Value: */
    p += zipmapEncodeLength(p,vlen);  // 设置value的<len>的值，同时右移指针p
    *p++ = vempty;  // 右移指针p，跳过<free>的位置
    memcpy(p,val,vlen);  // 将value的值保存到p指针的位置
    return zm;  // 返回zipmap
}

/* Remove the specified key. If 'deleted' is not NULL the pointed integer is
 * set to 0 if the key was not found, to 1 if it was found and deleted. */
unsigned char *zipmapDel(unsigned char *zm, unsigned char *key, unsigned int klen, int *deleted) {  /* 删除zipmap中的key、value键值对 */
    unsigned char *p = zipmapLookupRaw(zm,key,klen,NULL,NULL,NULL);  // 获取包含该key的zipmap结构体，<len>key的首地址
    if (p) {  // 如果zipmap中包含key
        unsigned int freelen = zipmapRawEntryLength(p);  // 计算key、value的总字节

        p[0] = ZIPMAP_EMPTY;  // 将<len>key的第1个字节设置empty标志
        zipmapEncodeLength(p+1,freelen);  // 将第2个字节记录empty长度
        zm[0] |= ZIPMAP_STATUS_FRAGMENTED;  // 再将zipmap最前面的<status>设置为1，表示zipmap有碎片
        if (deleted) *deleted = 1;  // 如果传入了deleted指针，则将其指向的位置值为1
    } else {
        if (deleted) *deleted = 0;  // 标记未找到对应key
    }
    return zm;  // 返回zipmap
}

/* Call it before to iterate trought elements via zipmapNext() */
unsigned char *zipmapRewind(unsigned char *zm) {  /* 找到zipmap中第1个entry的首地址 */
    return zm+1;  // 跳过<status>位置，指向第1个entry的首地址 
}

/* This function is used to iterate through all the zipmap elements.
 * In the first call the first argument is the pointer to the zipmap + 1.
 * In the next calls what zipmapNext returns is used as first argument.
 * Example:
 *
 * unsigned char *i = zipmapRewind(my_zipmap);
 * while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
 *     printf("%d bytes key at $p\n", klen, key);
 *     printf("%d bytes value at $p\n", vlen, value);
 * }
 */
unsigned char *zipmapNext(unsigned char *zm, unsigned char **key, unsigned int *klen, unsigned char **value, unsigned int *vlen) {  /* 遍历zipmap中的entry（**key二维指针，一维指针存放变量的地址，二维指针存放一维指针的地址） */
    while(zm[0] == ZIPMAP_EMPTY)  // 如果遍历到的entry首地址是empty标志
        zm += zipmapDecodeLength(zm+1);  // 则获取empty的长度，并右移指针到下一个entry的首地址
    if (zm[0] == ZIPMAP_END) return NULL;  // 如果遍历到的entry首地址是end标志，则遍历结束
    if (key) {  // 如果遍历到的entry中key存在（不是empty也不是end标志）
        *key = zm;  // key指针指向<len>key的首地址
        *klen = zipmapDecodeLength(zm);  // 获取<len>key中<len>存储的值，即key的长度
        *key += ZIPMAP_LEN_BYTES(*klen);  // 获取<len>所占的字节，1字节或5字节
    }
    zm += zipmapRawKeyLength(zm);  // 右移指针，跳过<len>key，指向<len><free>value的首地址
    if (value) {  // 如果value存在
        *value = zm+1;  // 右移指针1字节，跳过<free>。PS：这个+1写到357行更容易理解
        *vlen = zipmapDecodeLength(zm);  // 获取<len><free>value中<len>的值，即value的长度
        *value += ZIPMAP_LEN_BYTES(*vlen);  // 右移指针，跳过<len>
    }
    zm += zipmapRawValueLength(zm);  // 右移指针，跳过value的长度
    return zm;  // 返回zipmap
}

/* Search a key and retrieve the pointer and len of the associated value.
 * If the key is found the function returns 1, otherwise 0. */
int zipmapGet(unsigned char *zm, unsigned char *key, unsigned int klen, unsigned char **value, unsigned int *vlen) {
    unsigned char *p;

    if ((p = zipmapLookupRaw(zm,key,klen,NULL,NULL,NULL)) == NULL) return 0;
    p += zipmapRawKeyLength(p);
    *vlen = zipmapDecodeLength(p);
    *value = p + ZIPMAP_LEN_BYTES(*vlen) + 1;
    return 1;
}

/* Return 1 if the key exists, otherwise 0 is returned. */
int zipmapExists(unsigned char *zm, unsigned char *key, unsigned int klen) {
    return zipmapLookupRaw(zm,key,klen,NULL,NULL,NULL) != NULL;
}

/* Return the number of entries inside a zipmap */
unsigned int zipmapLen(unsigned char *zm) {
    unsigned char *p = zipmapRewind(zm);
    unsigned int len = 0;

    while((p = zipmapNext(p,NULL,NULL,NULL,NULL)) != NULL) len++;
    return len;
}

void zipmapRepr(unsigned char *p) {
    unsigned int l;

    printf("{status %u}",*p++);
    while(1) {
        if (p[0] == ZIPMAP_END) {
            printf("{end}");
            break;
        } else if (p[0] == ZIPMAP_EMPTY) {
            l = zipmapDecodeLength(p+1);
            printf("{%u empty block}", l);
            p += l;
        } else {
            unsigned char e;

            l = zipmapDecodeLength(p);
            printf("{key %u}",l);
            p += zipmapEncodeLength(NULL,l);
            fwrite(p,l,1,stdout);
            p += l;

            l = zipmapDecodeLength(p);
            printf("{value %u}",l);
            p += zipmapEncodeLength(NULL,l);
            e = *p++;
            fwrite(p,l,1,stdout);
            p += l+e;
            if (e) {
                printf("[");
                while(e--) printf(".");
                printf("]");
            }
        }
    }
    printf("\n");
}

#ifdef ZIPMAP_TEST_MAIN
int main(void) {
    unsigned char *zm;

    zm = zipmapNew();

    zm = zipmapSet(zm,(unsigned char*) "name",4, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "surname",7, (unsigned char*) "foo",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "age",3, (unsigned char*) "foo",3,NULL);
    zipmapRepr(zm);
    exit(1);

    zm = zipmapSet(zm,(unsigned char*) "hello",5, (unsigned char*) "world!",6,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "bar",3,NULL);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "!",1,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "foo",3, (unsigned char*) "12345",5,NULL);
    zipmapRepr(zm);
    zm = zipmapSet(zm,(unsigned char*) "new",3, (unsigned char*) "xx",2,NULL);
    zm = zipmapSet(zm,(unsigned char*) "noval",5, (unsigned char*) "",0,NULL);
    zipmapRepr(zm);
    zm = zipmapDel(zm,(unsigned char*) "new",3,NULL);
    zipmapRepr(zm);
    printf("\nPerform a direct lookup:\n");
    {
        unsigned char *value;
        unsigned int vlen;

        if (zipmapGet(zm,(unsigned char*) "foo",3,&value,&vlen)) {
            printf("  foo is associated to the %d bytes value: %.*s\n",
                vlen, vlen, value);
        }
    }
    printf("\nIterate trought elements:\n");
    {
        unsigned char *i = zipmapRewind(zm);
        unsigned char *key, *value;
        unsigned int klen, vlen;

        while((i = zipmapNext(i,&key,&klen,&value,&vlen)) != NULL) {
            printf("  %d:%.*s => %d:%.*s\n", klen, klen, key, vlen, vlen, value);
        }
    }
    return 0;
}
#endif
