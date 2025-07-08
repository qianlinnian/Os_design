#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *target) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if(st.type == T_FILE){
        // 只比较最后一段
        p = path + strlen(path);
        while(p >= path && *p != '/')
            p--;
        p++;
        if(strcmp(p, target) == 0)
            printf("%s\n", path);
    } else if(st.type == T_DIR){
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0)
                continue;
            if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            int len = strlen(path);
            memmove(buf, path, len);
            if(path[len-1] != '/'){
                buf[len] = '/';
                len++;
            }
            memmove(buf + len, de.name, DIRSIZ);
            buf[len + DIRSIZ] = 0;
            if(stat(buf, &st) < 0)
                continue;
            find(buf, target);
        }
    }
    close(fd);
}


int main(int argc, char *argv[]) {
    if(argc != 3){
        fprintf(2, "Usage: find <directory> <filename>\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}
