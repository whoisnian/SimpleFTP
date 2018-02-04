/*************************************************************************
    > File Name: ftpclient.c
    > Author: nian
    > Blog: https://whoisnian.com
    > Mail: zhuchangbao2017@gmail.com
    > Created Time: 2018年01月19日 星期五 17时31分26秒
 ************************************************************************/
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>

//引入Socket头文件
#include<sys/types.h> 
#include<sys/stat.h>
#include<sys/socket.h>
#include<sys/wait.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<dirent.h>
#include<linux/tcp.h>

#define BUF_SIZE 4096                           //缓存块大小
#define USERNAME_MAX 50                         //用户名长度限制
#define PASSWORD_MAX 50                         //密码长度限制
#define NAME_MAX 255                            //文件名长度限制
#define PATH_MAX 4096                           //路径长度限制

static char IP[30];                             //服务器IP地址
static unsigned long PORT = 21;                 //服务器命令端口，默认21
static char buf[BUF_SIZE];                      //缓存块
static unsigned long rest_size = 0;             //偏移量
static unsigned long data_port = 20;            //数据交流端口
static int data_socket = 0;                     //数据交流
static char username[USERNAME_MAX+1];           //用户名
static char password[PASSWORD_MAX+1];           //密码

void help(void);                                //显示帮助并退出
int  login(int socket_fd);                      //登录
int  deal(char *str, int socket_fd);            //处理指令
int  command_cwd(char *str, int socket_fd);     //CWD
int  command_retr(char *str, int socket_fd);    //RETR
int  command_list(int socket_fd);               //LIST
int  command_pasv(int socket_fd);               //PASV
int  command_port(char *str, int socket_fd);    //PORT
int  command_stor(char *str, int socket_fd);    //STOR
int  command_quit(int socket_fd);               //QUIT
int  command_rest(char *str, int socket_fd);    //REST
int  command_size(char *str, int socket_fd);    //SIZE

int main(int argc, char *argv[])
{
    //检查是否提供了地址参数
    if(argc < 2)
    {
        help();
        exit(0);
    }

    //参数初始化
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
    
    //获取提供的参数
    for(int i = 1;i < argc;i++)
    {
        if(!strcmp(argv[i], "-h")||!strcmp(argv[i], "--help"))
        {
            //显示帮助并退出
            help();
            exit(0);
        }
        else if(!strcmp(argv[i], "-p")||!strcmp(argv[i], "--port"))
        {
            //设置端口
            PORT = strtoul(argv[++i], NULL, 0);
            server_addr.sin_port = htons(PORT);
        }
        else
        {
            //设置ip
            strcpy(IP, argv[i]);
            server_addr.sin_addr.s_addr = inet_addr(IP);
        }
    }

    //连接FTP服务器
	connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));

    //获取服务器状态
    memset(buf, 0, sizeof(buf));
    read(socket_fd, buf, sizeof(buf)-1);
    if(strcmp(buf, "332 User needed\n"))
    {
        printf("Can't connect to your FTP server.\n");
        exit(1);
    }

	int flag = 1;

    //登录
    while(flag) flag = login(socket_fd);

    //处理指令
    flag = 1;
	while(flag)
	{
        //显示提示符
        printf("[\033[1;33;40m%s\033[0m@\033[0;32;40m%s\033[0m] $ ", username, IP);
		memset(buf, 0, sizeof(buf));
		fgets(buf, sizeof(buf), stdin);
        flag = deal(buf, socket_fd);
	}

    //关闭连接
    if(data_socket > 0) close(data_socket);
	close(socket_fd);
    return 0;
}

//显示帮助并退出
void help(void)
{
    printf("Usage: ftpclient [options] address\n\n");
    printf("Options:\n");
    printf("  -h, --help\t\t显示此帮助信息并退出\n");
    printf("  -p, --port\t\t设置服务器端口，默认端口21\n");
}

//登录
int login(int socket_fd)
{
    //输入用户名
    printf("%s login: ", IP);
    scanf("%s", username);
    dprintf(socket_fd, "USER %s\r\n", username);
    memset(buf, 0, sizeof(buf));
    read(socket_fd, buf, sizeof(buf)-1);

    //输入密码
    printf("Password: ");
    scanf("%s%*c", password);
    dprintf(socket_fd, "PASS %s\r\n", password);
    memset(buf, 0, sizeof(buf));
    read(socket_fd, buf, sizeof(buf)-1);

    //获取登录结果
    if(!strcmp(buf, "230 Logged on\n"))
    {
        printf("Logged on\n\n");
        return 0;
    }
    else
    {
        printf("Login incorrect\n\n");
        return 1;
    }
}

/*
 * 处理指令
 *
 * 返回值
 *   0: 用户发出了QUIT命令，准备退出客户端
 *   1: 命令转换正常，继续发送
 *
 * 命令列表（仿Linux）
 *
 * cd   CWD     ok
 * get  RETR    ok
 * help HELP    ok
 * ls   LIST    ok
 * pasv PASV    ok
 * port PORT    ok
 * put  STOR    ok
 * exit QUIT    ok
 * rest REST    ok      before download RETR
 * size SIZE    ok      before upload   STOR
 *
 */
int deal(char *str, int socket_fd)
{
    int ret;
    char temp[PATH_MAX+1];
    sscanf(str, "%s", temp);

    if(!strcmp(temp, "cd")||!strcmp(temp, "CWD"))
    {
        ret = sscanf(str, "%*s%s", temp);
        if(ret <= 0)
        {
            printf("Invalid parameter\n");
            return 1;
        }
        return command_cwd(temp, socket_fd);
    }
    else if(!strcmp(temp, "get")||!strcmp(temp, "RETR"))
    {
        ret = sscanf(str, "%*s%s", temp);
        if(ret <= 0)
        {
            printf("Invalid parameter\n");
            return 1;
        }
        return command_retr(temp, socket_fd);
    }
    else if(!strcmp(temp, "help")||!strcmp(temp, "HELP"))
    {
        printf("Commands:\n");
        printf("  cd,   CWD\t\t改变工作目录\n");
        printf("  get,  RETR\t\t下载文件\n");
        printf("  help, HELP\t\t显示此帮助信息并退出\n");
        printf("  ls,   LIST\t\t列出工作目录下文件信息\n");
        printf("  pasv, PASV\t\t获取服务器监听端口，进入被动模式\n");
        printf("  port, PORT\t\t发送客户端监听端口，进入主动模式\n");
        printf("  put,  STOR\t\t上传文件\n");
        printf("  exit, QUIT\t\t关闭与服务器的连接\n");
        printf("  rest, REST\t\t指定文件偏移量\n");
        printf("  size, SIZE\t\t获取指定文件大小\n");
        return 1;
    }
    else if(!strcmp(temp, "ls")||!strcmp(temp, "LIST"))
    {
        return command_list(socket_fd);
    }
    else if(!strcmp(temp, "pasv")||!strcmp(temp, "PASV"))
    {
        return command_pasv(socket_fd);
    }
    else if(!strcmp(temp, "port")||!strcmp(temp, "PORT"))
    {
        ret = sscanf(str, "%*s%s", temp);
        if(ret <= 0)
        {
            printf("Invalid parameter\n");
            return 1;
        }
        return command_port(temp, socket_fd);
    }
    else if(!strcmp(temp, "put")||!strcmp(temp, "STOR"))
    {
        ret = sscanf(str, "%*s%s", temp);
        if(ret <= 0)
        {
            printf("Invalid parameter\n");
            return 1;
        }
        return command_stor(temp, socket_fd);
    }
    else if(!strcmp(temp, "exit")||!strcmp(temp, "QUIT"))
    {
        return command_quit(socket_fd);
    }
    else if(!strcmp(temp, "rest")||!strcmp(temp, "REST"))
    {
        ret = sscanf(str, "%*s%s", temp);
        if(ret <= 0)
        {
            printf("Invalid parameter\n");
            return 1;
        }
        return command_rest(temp, socket_fd);
    }
    else if(!strcmp(temp, "size")||!strcmp(temp, "SIZE"))
    {
        ret = sscanf(str, "%*s%s", temp);
        if(ret <= 0)
        {
            printf("Invalid parameter\n");
            return 1;
        }
        return command_size(temp, socket_fd);
    }
    else
    {
        printf("Unknown command\n");
        printf("Try 'help' or 'HELP' for more commands\n");
        return 1;
    }
}

int command_cwd(char *str, int socket_fd)
{
    dprintf(socket_fd, "CWD %s\r\n", str);
    memset(buf, 0, sizeof(buf));
    read(socket_fd, buf, sizeof(buf)-1);
    printf("%s", buf);
    return 1;
}
int command_retr(char *str, int socket_fd)
{
    //判断是否需要断点续传
    if(rest_size == 0)
    {
        struct stat sb;
        if(stat(str, &sb) == 0)
            rest_size = sb.st_size;
    }
    if(rest_size > 0)
    {
        dprintf(socket_fd, "REST %lu\r\n", rest_size);
        memset(buf, 0, sizeof(buf));
        read(socket_fd, buf, sizeof(buf)-1);
        printf("自动续传\n");
    }
    dprintf(socket_fd, "RETR %s\r\n", str);
    memset(buf, 0, sizeof(buf));
    int res = read(socket_fd, buf, sizeof(buf)-1);
    buf[res] = 0;
    printf("%s", buf);
    if(buf[0] != '2'||buf[1] != '2'||buf[2] != '5')
    {
        return 1;
    }

    //打开文件
    FILE *fp = fopen(str, "a");
    res = 1;
    while(res)
    {
        memset(buf, 0, sizeof(buf));
        res = read(data_socket, buf, sizeof(buf)-1);
        fwrite(buf, 1, res, fp);
        if(res < (int)sizeof(buf)-1) break;
    }
    fclose(fp);
    rest_size = 0;
    res = read(socket_fd, buf, sizeof(buf)-1);
    buf[res] = 0;
    printf("%s", buf);
    return 1;
}
int command_list(int socket_fd)
{
    dprintf(socket_fd, "LIST\r\n");
    memset(buf, 0, sizeof(buf));
    int res = 1;
    while(res)
    {
        res = read(socket_fd, buf, sizeof(buf)-1);
        if(buf[0] == '4'&&buf[1] == '5'&&buf[2] == '0')
        {
            buf[res] = 0;
            res = 0;
        }
        else if(buf[res-1] == '\n'&&buf[res-2] == 'D'&&buf[res-3] == 'N'&&buf[res-4] == 'E'&&buf[res-5] == '\n')
        {
            buf[res-5] = 0;
            res = 0;
        }
        else
            buf[res] = 0;
        printf("%s", buf);
    }
    return 1;
}
int command_pasv(int socket_fd)
{
    dprintf(socket_fd, "PASV\r\n");
    memset(buf, 0, sizeof(buf));
    read(socket_fd, buf, sizeof(buf)-1);
    printf("%s", buf);
    if(buf[0] != '2'||buf[1] != '2'||buf[2] != '7')
        return 1;

    //连接服务器数据端口
    data_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
	data_addr.sin_family = AF_INET;
    int h1, h2, h3, h4, p1, p2;
    sscanf(buf, "%*s%*s%*s%*s (%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2);
    //sprintf(IP, "%d.%d.%d.%d", h1, h2, h3, h4);
    data_port = p1*256+p2;
    data_addr.sin_port = htons(data_port);
    data_addr.sin_addr.s_addr = inet_addr(IP);
	connect(data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr));
    int res = read(data_socket, buf, sizeof(buf)-1);
    buf[res] = 0;
    printf("%s", buf);
    return 1;
}
int command_port(char *str, int socket_fd)
{
    int h1, h2, h3, h4, p1, p2;
    sscanf(str, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2);
    data_port = p1*256+p2;

    //监听本地端口
    struct sockaddr_in local_addr;
    int local_socket = socket(AF_INET, SOCK_STREAM, 0);
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(data_port);
    bind(local_socket, (struct sockaddr*)&local_addr, sizeof(local_addr));
    listen(local_socket, 10);

    dprintf(socket_fd, "PORT %s\r\n", str);
    memset(buf, 0, sizeof(buf));
    read(socket_fd, buf, sizeof(buf)-1);
    if(buf[0] != '2'||buf[1] != '2'||buf[2] != '0')
        return 1;

    //接受服务端连接
    struct sockaddr_in data_addr;
    socklen_t data_addr_len;
    data_addr_len = sizeof(data_addr);
    data_socket = accept(local_socket, (struct sockaddr*)&data_addr, &data_addr_len);
    int res = read(data_socket, buf, sizeof(buf)-1);
    buf[res] = 0;
    printf("%s", buf);
    return 1;
}
int command_stor(char *str, int socket_fd)
{
    //判断本地文件是否可以打开
    FILE *fp = fopen(str, "r");
    if(fp == NULL)
    {
        printf("Can't open %s\n", str);
        return 1;
    }

    //判断是否需要断点续传
    if(rest_size == 0)
    {
        dprintf(socket_fd, "SIZE %s\r\n", str);
        memset(buf, 0, sizeof(buf));
        read(socket_fd, buf, sizeof(buf)-1);
        if(buf[0] == '2'&&buf[1] == '5'&&buf[2] == '0')
            sscanf(buf, "%*s%lu", &rest_size);
    }
    if(rest_size > 0)
    {
        fseek(fp, rest_size, SEEK_SET);
        printf("自动续传\n");
    }

    dprintf(socket_fd, "STOR %s\r\n", str);
    int res = read(socket_fd, buf, sizeof(buf)-1);
    buf[res] = 0;
    printf("%s", buf);
    if(buf[0] != '2'||buf[1] != '2'||buf[2] != '5')
    {
        fclose(fp);
        return 1;
    }

    res = 1;
    while(res)
    {
        memset(buf, 0, sizeof(buf));
        res = fread(buf, 1, sizeof(buf)-1, fp);
        write(data_socket, buf, res);
    }
    fclose(fp);
    rest_size = 0;
    res = read(socket_fd, buf, sizeof(buf)-1);
    buf[res] = 0;
    printf("%s", buf);
    return 1;
}
int command_quit(int socket_fd)
{
    //退出
    dprintf(socket_fd, "QUIT\r\n");
    memset(buf, 0, sizeof(buf));
    int res = read(socket_fd, buf, sizeof(buf)-1);
    buf[res] = 0;
    printf("%s", buf);
    return 0;
}
int command_rest(char *str, int socket_fd)
{
    dprintf(socket_fd, "REST %s\r\n", str);
    memset(buf, 0, sizeof(buf));
    int res = read(socket_fd, buf, sizeof(buf)-1);
    buf[res] = 0;
    printf("%s", buf);
    return 1;
}
int command_size(char *str, int socket_fd)
{
    dprintf(socket_fd, "SIZE %s\r\n", str);
    memset(buf, 0, sizeof(buf));
    int res = read(socket_fd, buf, sizeof(buf)-1);
    buf[res] = 0;
    printf("%s", buf);
    return 1;
}
