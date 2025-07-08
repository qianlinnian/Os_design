#include "kernel/types.h"
#include "user/user.h"

void primes(int fd) {
    int prime;
    int n;
    int p[2];

    // 读取第一个数，作为本轮素数
    if (read(fd, &prime, sizeof(prime)) != sizeof(prime)) {
        // 没有数字了，递归出口
        close(fd);
        exit(0);
    }
    printf("prime %d\n", prime);

    // 创建新管道
    pipe(p);

    if (fork() == 0) {
        // 子进程：递归处理新管道
        close(p[1]);      // 只读
        primes(p[0]);
        // primes会exit
    } else {
        // 父进程：筛选并写入新管道
        close(p[0]);      // 只写
        while (read(fd, &n, sizeof(n)) == sizeof(n)) {
            if (n % prime != 0) {
                write(p[1], &n, sizeof(n));
            }
        }
        // 关闭所有用过的fd
        close(fd);
        close(p[1]);
        wait(0); // 等待子进程
        exit(0);
    }
}

int main(void) {
    int p[2];
    pipe(p);

    if (fork() == 0) {
        // 子进程：负责递归筛选
        close(p[1]);
        primes(p[0]);
        // primes会exit
    } else {
        // 父进程：写2~35到管道
        close(p[0]);
        for (int i = 2; i <= 35; i++) {
            write(p[1], &i, sizeof(i));
        }
        close(p[1]);
        wait(0); // 等待子进程
    }
    exit(0);
}
