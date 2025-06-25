#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void sieve(int pleft[2]){
    int p;
    read(pleft[0], &p, sizeof(p));//从左管道读出第一个数字保存于p中
    if(p == -1){// 结束标志 -1
        exit(0);
    }
    printf("prime %d\n", p);//因为只要输出第一位所以单独处理第一位

    int pright[2];// 创建右管道
    pipe(pright);

    if(fork() == 0){// 右邻居（子进程会复制父进程的管道，但是管道间独立）
        close(pright[1]);// 关闭右邻居右管道的写端
        close(pleft[0]);// 关闭左管道的读端
        sieve(pright);// 递归筛选
    }
    else{// 左邻居
        close(pright[0]);
        int buf;
        while(read(pleft[0],&buf,sizeof(buf)) && buf != -1){// 从左管道读取剩下的数字
            if(buf % p != 0){//不是第一位p的倍数就可能是素数（每次创建一个右邻居就会删除为p倍数的数）
                write(pright[1], &buf, sizeof(buf));//写入右管道
            }
        }
        // 重新放入结束标志-1
        buf = -1; 
        write(pright[1], &buf, sizeof(buf));
        wait(0);
        exit(0);
    }
}
int main(int argc,char **argv){
    // 初始管道
    int input_pipe[2];
    pipe(input_pipe);

    if(fork() == 0){// 子进程内（右邻居）
        close(input_pipe[1]); // 关闭写端
        sieve(input_pipe); // 筛选素数
        exit(0);
    }else{//父进程
        close(input_pipe[0]); // 关闭读端
        int i;
        for(i = 2; i < 35; i++){ // 写入2-35
            write(input_pipe[1], &i, sizeof(i));
        }
    // 结束信号
    i = -1;
    write(input_pipe[1], &i, sizeof(i));
    }
    wait(0);
    exit(0);
}