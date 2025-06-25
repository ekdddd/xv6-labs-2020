#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc,char **argv){
	int pp2c[2],pc2p[2];
	pipe(pp2c); // 用于父进程到子进程通信
	pipe(pc2p); // 用于子进程到父进程通信

	if(fork()!=0){ // 父进程内（子进程创建成功父进程返回子进程PID）
		// 发送字符
		write(pp2c[1],".",1);
		close(pp2c[1]);
		// 读取字符
		char buf;
		read(pc2p[0],&buf,1);
		printf("%d: received pong\n",getpid());
		// 等待子进程结束，并回收资源
		wait(0);
	}
	else{ // 子进程内（子进程创建成功子进程返回0）
		// 读取字符
		char buf;
		read(pp2c[0],&buf,1);
		printf("%d: received ping\n",getpid());
		// 发送字符
		write(pc2p[1],&buf,1);
		close(pc2p[1]);
	}
	//关闭管道读段
	close(pp2c[0]);
	close(pc2p[0]);

	exit(0);


}
