#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// 去除参数两侧成对的引号（单引号或双引号）
void strip_quotes(char *s) {
    int len = strlen(s);
    if(len >= 2) {
        if((s[0] == '"' && s[len-1] == '"') || (s[0] == '\'' && s[len-1] == '\'')) {
            // 向前拷贝
            memmove(s, s+1, len-2);
            s[len-2] = 0;
        }
    }
}
// 将“\n”转译为换行符
void unescape_newline(char *s) {
    char *src = s, *dst = s; 
    while (*src) {
        if (src[0] == '\\' && src[1] == 'n') {
            *dst++ = ' ';  // 写入' '
            src += 2;       // 跳过 \n
        } else {
            *dst++ = *src++; // 正常拷贝
        }
    }
    *dst = '\0'; // 结尾加上结束符 
}



int main(int argc, char *argv[])
{
    char buf[512];
    char *args[MAXARG];
    int i;
    int n_flag = 0; // -n选项的值，0表示没有-n选项
    int cmd_start = 1; // 命令开始的索引


    // 解析命令行参数
    if(argc < 2) {
        fprintf(2, "Usage: xargs [-n num] command [args...]\n");
        exit(1);
    }

    // 检查是否有-n选项
    if(argc >= 3 && strcmp(argv[1], "-n") == 0) {
        n_flag = atoi(argv[2]);
        cmd_start = 3;
        if(argc < 4) {
            fprintf(2, "Usage: xargs [-n num] command [args...]\n");
            exit(1);
        }
    }

    // 复制命令行参数到args数组
    int base_arg_count = 0;
    for(i = cmd_start; i < argc; i++) {
        args[base_arg_count++] = argv[i];
    }

    // 逐行处理标准输入
    int buf_pos = 0;
    char c;

    while(read(0, &c, 1) == 1) {
        if(c == '\n') {
            // 处理一行输入
            buf[buf_pos] = 0; // 字符串结束符

            // 跳过空行
            if(buf_pos == 0) {
                buf_pos = 0;
                continue;
            }

             // 对每个输入参数做 unescape_newline
            for(int x = 0; x < buf_pos; x++) {
                //printf("处理前 %s\n ", buf);
                strip_quotes(buf);
                unescape_newline(buf);
                //printf("处理后 %s\n ", buf);
            }

            // 将输入行按空格分割
            char *input_args[MAXARG];
            int input_count = 0;

            char *p = buf;
            while(*p && input_count < MAXARG - base_arg_count - 1) {
                // 跳过前导空格
                while(*p == ' ' || *p == '\t') p++;
                if(*p == 0) break;

                input_args[input_count++] = p;

                // 找到下一个空格或字符串结尾
                while(*p && *p != ' ' && *p != '\t') p++;
                if(*p) {
                    *p = 0;
                    p++;
                }
            }
           

            // 根据-n选项决定如何执行
            if(n_flag > 0) {
                // 有-n选项，按指定数量分组执行
                for(int j = 0; j < input_count; j += n_flag) {
                    // 构建参数列表
                    int arg_count = base_arg_count;
                    for(int k = j; k < j + n_flag && k < input_count; k++) {
                        args[arg_count++] = input_args[k];
                    }
                    args[arg_count] = 0;

                    // fork并执行
                    if(fork() == 0) {
                        exec(args[0], args);
                        fprintf(2, "xargs: exec %s failed\n", args[0]);
                        exit(1);
                    } else {
                        wait(0);
                    }
                }
            } else {
                // 没有-n选项，将所有输入参数一次性传递
                int arg_count = base_arg_count;
                for(int j = 0; j < input_count; j++) {
                    args[arg_count++] = input_args[j];
                }
                args[arg_count] = 0;

                // fork并执行
                if(fork() == 0) {
                    exec(args[0], args);
                    fprintf(2, "xargs: exec %s failed\n", args[0]);
                    exit(1);
                } else {
                    wait(0);
                }
            }

            buf_pos = 0; // 重置缓冲区
        } else {
            // 将字符添加到缓冲区
            if(buf_pos < sizeof(buf) - 1) {
                buf[buf_pos++] = c;
            }
        }
    }

    exit(0);
}
