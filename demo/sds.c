#include <stdio.h>
#include <stdarg.h>

typedef char *sds;

struct sdshdr {
    long len;
	long free;
	char buf[];  // 可变长数组，使用sizeof不能计算出长度
} 

/* 在main函数外部初始化，否则编译报错 */
struct sdshdr sh = {6, 2, {'h', 'e', 'l', 'l', 'o', '\0'}};

/* 该自定义函数，与系统提供的snprintf()函数相同 */
int my_snprintf(char *s, int size, const char *fmt, ...) {
    va_list ap;
	int n = 0;
	va_start(ap, fmt);  // 获得可变参数列表
    n = vsnprintf(s, size, fmt, ap);  // 写入字符串s
    va_end(ap);  // 释放资源
    return n;  // 返回写入的字符个数
}

int main() {
    struct sdshdr *s = &sh;
    printf("%d\n", sizeof(struct sdshdr));  // 16
    printf("%d\n", sizeof(long));  // 8字节
    printf("%d, %p\n", s->len, &(s->len));
    printf("%d, %p\n", s->free, &(s->free));
    printf("%d, %p\n", s->buf, &(s->buf));
    
    sds sd = s->buf;  // 指向字符数组地址
    printf("%s\n", sd);
    
    struct sdshdr *new_sh = (void*) (sd - (sizeof(struct sdshdr)));
    printf("得到的当前字符串长度：%d，剩余长度：%d\n", new_sh->len, new_sh->free);
    
    char str[1024];
    my_snprintf(str, sizeof(str), "%d, %d, %d, %d\n", 5, 6, 7, 8);
    printf("%s\n", str);
    
    return 0;
}