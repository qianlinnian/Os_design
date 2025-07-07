#include "kernel/types.h"
#include "user/user.h"

int
main()
{
  int p1[2], p2[2];
  pipe(p1); // p1[1]父写，p1[0]子读
  pipe(p2); // p2[1]子写，p2[0]父读

  int pid = fork();
  if(pid < 0){
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // 子进程
    char buf;
    close(p1[1]); // 关闭写端
    close(p2[0]); // 关闭读端

    read(p1[0], &buf, 1);
    printf("%d: received ping\n", getpid());
    write(p2[1], "a", 1);

    close(p1[0]);
    close(p2[1]);
    exit(0);
  } else {
    // 父进程
    char buf;
    close(p1[0]); // 关闭读端
    close(p2[1]); // 关闭写端

    write(p1[1], "a", 1);
    read(p2[0], &buf, 1);
    printf("%d: received pong\n", getpid());

    close(p1[1]);
    close(p2[0]);
    wait(0);
    exit(0);
  }
}
