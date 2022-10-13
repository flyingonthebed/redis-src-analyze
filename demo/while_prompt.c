#include <stdio.h>
#include <string.h>


static char *prompt(char *line, int size) {
   char *retval;

   do {
       printf(">> ");  // 提示符
       retval = fgets(line, size, stdin);  // 等待输入
    } while (retval && *line == '\n');  // 实现了回车后不退出循环的功能

    line[strlen(line) - 1] = '\0';

    return retval;
}


static void repl() {
    int size = 4096l, max = size >> 1, argc;
    char buffer[size];
    char *line = buffer;
    char **ap, *args[max];

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
        printf("参数个数：%d, 数组内容：\n", argc);
        int i;
        for(i = 0; i < argc; i++) {
            printf("%s ", args[i]);
        }
        line = buffer;
    }
    exit(0);
}


int main(int argc, char **argv) {
    repl();
    return 0;
}