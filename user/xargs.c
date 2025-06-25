#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void run(char *program, char **args){
    if(fork() == 0){
        // 子进程执行指定的程序，program 是要执行的程序名，args 是参数列表，调用成功后当前进程的代码、数据、堆栈等内容会被新程序完全替换
        exec(program,args);
        exit(0);
    }
    return;
}

int main(int argc,char *argv[]){
    char buf[2048];
    char* p = buf,* last_p = buf;
    char *argsbuf[128]; // 指针数组
    char** args = argsbuf;

    // 将命令行参数复制到 argsbuf 中
    for(int i = 1;i<argc;i++){
        *args = argv[i];
        args++;
    }
    // 当前参数位置
    char **pa = args;
    // 读取标准输入，每次读入1字符放入p指向内存
    while(read(0,p,1)!=0){
        if(*p == ' ' || *p == '\n'){
            *p = '\0';

            *(pa++) = last_p;
            last_p = p + 1;

            if(*p == '\n'){
                *pa = 0;
                run(argv[1],argsbuf);
                pa = args;

            }

        } 
        p++;
    }
    // 如果最后一行不是空行，同样的逻辑再处理一次
    if(pa != args){
        *p = '\0';
        *(pa++) = last_p;
        *pa = 0;
        run(argv[1],argsbuf);
    }
    // 等待所有子进程结束
    while(wait(0)!= -1){};
    exit(0);
   
}