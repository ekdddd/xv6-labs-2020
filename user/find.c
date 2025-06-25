#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *target){
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    // printf("find: %s\n", path); // 输出当前查找的路径
    if((fd = open(path, 0)) < 0){ //只读方式打开（0），返回-1表示失败
        fprintf(2,"find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){ // 获取由文件描述符 fd 所代表的文件的信息，将这些信息填充到 struct stat 结构体中，成功返回0、失败返回-1
        fprintf(2,"find: cannot stat %s\n", path); // fd==2 标准错误输出
        close(fd);
        return;
    }

    switch(st.type){ // 获取文件类型
        
        // 文件：检查文件名与目标文件是否匹配
        case T_FILE:
            // printf("%d, %d,%s\n",st.type,st.ino, path);
            // 匹配到路径中的目标文件名可能存在的位置
            if(strcmp(path+strlen(path)-strlen(target),target)==0){
                printf("%s\n", path);
            }break;
        case T_DIR:
            // printf("%d, %d,%s\n",st.type,st.ino, path);
            // 判断将要构造的完整路径名是否会超出缓冲区 path+/+DIRSIZ（文件名最大长度）+\0
            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
                printf("find: path too long\n");
                break;
            }
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';
            //  从文件描述符 fd 中读取一个目录项，成功时返回实际读取的字节数,遍历目录下的所有条目，并对每个子目录进行递归搜索
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0) continue; // 表示该目录项为空/未使用（无效条目）
                memmove(p, de.name, DIRSIZ);// 将目录项 de.name 中的文件名内容复制到指针 p 所指向的位置，复制长度为 DIRSIZ 个字节
                p[DIRSIZ] = 0; // 确保字符串以 \0 结尾
                // printf("%s\n",buf);
                
                if(stat(buf, &st) < 0){
                    printf("find: cannot stat %s\n", buf);
                    continue;
                }
                // 排除 .和.. 这两个特殊目录项
                if(strcmp(buf+strlen(buf)-2,"/.")!=0 && strcmp(buf+strlen(buf)-3,"/..")!=0){
                    find(buf, target); // 递归查找子目录
                }
            }
            break;
    }
    close(fd); //* 关闭文件描述符,每调用一次find就会open(path, 0)打开一个目录并产生一次文件描述符，如果没有关闭，随着递归的深入，文件描述符不断累积，最终描述符表满了，后续的 open(path, 0) 开始失败
}

int main(int argc,char *argv[])
{
  if(argc < 3){
    exit(0);
  }
  char target[512];
  target[0] = '/';
  strcpy(target + 1, argv[2]);
  find(argv[1], target);
  exit(0);
}