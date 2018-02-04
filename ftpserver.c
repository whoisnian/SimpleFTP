/*************************************************************************
    > File Name: ftpserver.c
    > Author: nian
    > Blog: https://whoisnian.com
    > Mail: zhuchangbao2017@gmail.com
    > Created Time: Fri 19 Jan 2018 05:32:50 PM CST
 ************************************************************************/
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>

//引入Socket头文件
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<dirent.h>
#include<locale.h>
#include<linux/tcp.h>

#define BUF_SIZE 4096                           //缓存块大小
#define USERNAME_MAX 50                         //用户名长度限制
#define PASSWORD_MAX 50                         //密码长度限制
#define NAME_MAX 255                            //文件名长度限制
#define PATH_MAX 4096                           //路径长度限制

static char IP[30];                             //客户端IP地址
static unsigned long PORT = 21;                 //服务器命令端口，默认21
static char buf[BUF_SIZE];                      //缓存块
static char current_dir[PATH_MAX+1] = "./";     //当前工作目录
static int logged = 0;                          //是否登录
static unsigned long rest_size = 0;             //偏移量
static unsigned long data_port = 20;            //数据交流端口
static int data_socket = 0;                     //数据交流
static char username[] = "admin";               //用户名
static char password[] = "admin";               //密码

//用于存储文件或目录信息
struct fileinfo
{
    char    f_name[NAME_MAX+1];
    off_t   f_size;
    struct  timespec f_mtim;
};
static struct fileinfo *fileinfo_arr;               //存储文件信息的数组
static int fileinfo_maxnum = 128;                   //默认大小128
static int fileinfo_num = 0;                        //当前已用大小

void help(void);                                    //显示帮助并退出
int  verify(char *user, char *pass);                //验证用户名和密码
void sort(struct fileinfo *st, struct fileinfo *en);//文件排序
int  deal(char *str, int socket_fd);                //处理指令
int  command_cwd(char *str, int socket_fd);         //CWD
int  command_retr(char *str, int socket_fd);        //RETR
int  command_list(int socket_fd);                   //LIST
int  command_pasv(int socket_fd);                   //PASV
int  command_port(char *str, int socket_fd);        //PORT
int  command_stor(char *str, int socket_fd);        //STOR
int  command_quit(int socket_fd);                   //QUIT
int  command_rest(char *str, int socket_fd);        //REST
int  command_size(char *str, int socket_fd);        //SIZE

int main(int argc, char *argv[])
{
    //参数初始化
    struct sockaddr_in server_addr;
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;

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
    }

    //绑定并开始监听本地端口
    bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, 10);

    //持续监听
    while(1)
    {
        //接收客户端请求
        struct sockaddr_in client_addr;
        socklen_t client_addr_len;
        client_addr_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);

        //获取当前时间
		time_t Time;
		char time_of_now[50];
		time(&Time);
		strftime(time_of_now, 50, "%Y-%m-%d %H:%M:%S %Z", localtime(&Time));
        
        //显示连接记录
        strcpy(IP, inet_ntoa(client_addr.sin_addr));
        printf("%s: \033[1;36;40m%s\033[0m connect from port \033[1;36;40m%d\033[0m;\n", time_of_now, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        //创建新进程处理请求，主进程等待下一次接收
        pid_t new_pid;
        new_pid = fork();

        if(new_pid == 0)
        {
            int flag = 1;
            
            //请求登录
            dprintf(client_socket, "332 User needed\n");

            //处理指令
            while(flag)
            {
                memset(buf, 0, sizeof(buf));
                read(client_socket, buf, sizeof(buf));
                printf("%s", buf);
                flag = deal(buf, client_socket);
            }

            //显示断开记录
            time(&Time);
		    strftime(time_of_now, 50, "%Y-%m-%d %H:%M:%S %Z", localtime(&Time));
            printf("%s: \033[1;36;40m%s\033[0m disconnect\n", time_of_now, inet_ntoa(client_addr.sin_addr));
        }
        close(client_socket);
        if(data_socket > 0) close(data_socket);
        if(new_pid == 0) break;
    }
    close(server_socket);
    return 0;
}

//显示帮助并退出
void help(void)
{
    printf("Usage: ftpserver [options]\n\n");
    printf("Options:\n");
    printf("  -h, --help\t\t显示此帮助信息并退出\n");
    printf("  -p, --port\t\t设置服务器端口，默认端口21\n");
}

/*
 * 验证登录
 *
 * 返回值
 * return  0: 用户名和密码不匹配，验证失败
 * return  1: 用户名和密码验证成功
 */
int verify(char *user, char *pass)
{
    if(!strcmp(user, username)&&!strcmp(pass, password))
        return 1;
    else
        return 0;
}

//排序时所用的比较函数
int cmp(struct fileinfo *a, struct fileinfo *b)
{
    return strcoll(a->f_name, b->f_name);
}

//快速排序
void sort(struct fileinfo *st, struct fileinfo *en)
{
    if(st < en)
    {
        struct fileinfo *low = st, *high = en - 1;
        struct fileinfo middle = *low;
        if(cmp((low+(high-low)/2), low)*cmp((low+(high-low)/2), high) < 0)
        {
            middle = *(low+(high-low)/2);
            *(low+(high-low)/2) = *low;
            *low = middle;
        }
        else if(cmp(high, low)*cmp(high, (low+(high-low)/2)) < 0)
        {
            middle = *high;
            *high = *low;
            *low = middle;
        }

        while(low < high)
        {
            while(low < high&&cmp(high, &middle) >= 0) high--;
                *low = *high;
            while(low < high&&cmp(low, &middle) <= 0) low++;
                *high = *low;
        }
        *low = middle;
        sort(st, low);
        sort(low+1, en);
    }
}

/*
 * 处理指令
 *
 * 返回值
 *   0: 用户发出了QUIT命令，准备结束本次连接
 *   1: 命令处理正常，继续接收
 *
 * 命令列表（仿Linux）
 *
 * cd   CWD
 * get  RETR
 * help HELP
 * ls   LIST
 * pasv PASV
 * port PORT
 * put  STOR
 * exit QUIT
 * rest REST
 * size SIZE
 *
 */
int deal(char *str, int socket_fd)
{
    char temp[PATH_MAX+1];
    static char user[USERNAME_MAX+1], pass[PASSWORD_MAX+1];
    sscanf(str, "%s", temp);

    //验证登录
    if(!strcmp(temp, "USER"))
    {
        sscanf(str, "%*s%s", user);
        dprintf(socket_fd, "331 Pass needed\n");
        return 1;
    }
    else if(!strcmp(temp, "PASS"))
    {
        sscanf(str, "%*s%s", pass);
        if(verify(user, pass))
        {
            dprintf(socket_fd, "230 Logged on\n");
            logged = 1;
        }
        else
        {
            dprintf(socket_fd, "530 Login failed\n");
        }
        return 1;
    }
    else if(!logged)
    {
        dprintf(socket_fd, "530 Not login\n");
        return 1;
    }

    if(!strcmp(temp, "CWD"))
    {
        sscanf(str, "%*s%s", temp);
        return command_cwd(temp, socket_fd);
    }
    else if(!strcmp(temp, "LIST"))
    {
        return command_list(socket_fd);
    }
    else if(!strcmp(temp, "PASV"))
    {
        return command_pasv(socket_fd);
    }
    else if(!strcmp(temp, "PORT"))
    {
        sscanf(str, "%*s%s", temp);
        return command_port(temp, socket_fd);
    }
    else if(!strcmp(temp, "QUIT"))
    {
        return command_quit(socket_fd);
    }
    else if(!strcmp(temp, "RETR"))
    {
        sscanf(str, "%*s%s", temp);
        return command_retr(temp, socket_fd);
    }
    else if(!strcmp(temp, "REST"))
    {
        sscanf(str, "%*s%s", temp);
        return command_rest(temp, socket_fd);
    }
    else if(!strcmp(temp, "SIZE"))
    {
        sscanf(str, "%*s%s", temp);
        return command_size(temp, socket_fd);
    }
    else if(!strcmp(temp, "STOR"))
    {
        sscanf(str, "%*s%s", temp);
        return command_stor(temp, socket_fd);
    }
    return 1;
}

int command_cwd(char *str, int socket_fd)
{
    //判断绝对路径还是相对路径，并更新
    char ori_dir[PATH_MAX+1], temp_dir[PATH_MAX+1];
    strcpy(ori_dir, current_dir);
    if(str[0] == '/')
    {
        strcpy(current_dir, str);
    }
    else
    {
        if(current_dir[(int)strlen(current_dir)-1] != '/')
        {
            current_dir[(int)strlen(current_dir)+1] = '\0';
            current_dir[(int)strlen(current_dir)] = '/';
        }
        strcat(current_dir, str);
    }

    //检查路径是否合法
    if(strlen(current_dir) > PATH_MAX)
        dprintf(socket_fd, "552 PATH too long\n");
    else if(realpath(current_dir, temp_dir) == NULL)
    {
        strcpy(current_dir, ori_dir);
        dprintf(socket_fd, "450 CWD %s failed\n", temp_dir);
    }
    else
    {
        strcpy(current_dir, temp_dir);
        dprintf(socket_fd, "250 CWD %s successful\n", current_dir);
    }
    return 1;
}
int command_retr(char *str, int socket_fd)
{
    //检查数据连接是否建立
    if(data_socket == 0||data_port == 20)
    {
        dprintf(socket_fd, "425 please connect\n");
        return 1;
    }

    //获取文件路径
    char request_file[PATH_MAX+1], temp_file[PATH_MAX+1];
    if(str[0] == '/')
    {
        strcpy(request_file, str);
    }
    else
    {
        if(current_dir[(int)strlen(current_dir)-1] != '/')
        {
            current_dir[(int)strlen(current_dir)+1] = '\0';
            current_dir[(int)strlen(current_dir)] = '/';
        }
        strcpy(request_file, current_dir);
        strcat(request_file, str);
    }

    //检查路径是否合法
    if(strlen(request_file) > PATH_MAX)
    {
        dprintf(socket_fd, "552 PATH too long\n");
        return 1;
    }
    else if(realpath(request_file, temp_file) == NULL)
    {
        dprintf(socket_fd, "450 Can't find %s\n", temp_file);
        return 1;
    }

    //只读打开文件
    FILE *fp = fopen(temp_file, "r");
    if(fp == NULL) 
    {
        dprintf(socket_fd, "450 Can't open %s\n", temp_file);
        return 1;
    }
    dprintf(socket_fd, "225 %s ready\n", temp_file);
    
    //设置偏移，用于断点续传
    if(rest_size > 0)
    {
        fseek(fp, rest_size, SEEK_SET);
    }
    int res = 1;
    while(res)
    {
        memset(buf, 0, sizeof(buf));
        res = fread(buf, 1, sizeof(buf)-1, fp);
        write(data_socket, buf, res);
    }
    fclose(fp);
    rest_size = 0;
    dprintf(socket_fd, "%s download finished\n", temp_file);
    return 1;
}
int command_list(int socket_fd)
{
    //打开指定的目录
    DIR *dir_p = opendir(current_dir);
    if(dir_p == NULL)
    {
        dprintf(socket_fd, "450 Can't open %s\n", current_dir);
        return 1;
    }
    chdir(current_dir);

    //读取目录内容
    struct dirent *dirent_p;
    fileinfo_arr = (struct fileinfo *)malloc(fileinfo_maxnum * sizeof(struct fileinfo));
    fileinfo_num = 0;
    for(dirent_p = readdir(dir_p);dirent_p != NULL;dirent_p = readdir(dir_p))
    {
        //忽略.和..
        if(!strcmp(dirent_p->d_name, ".")||!strcmp(dirent_p->d_name, ".."))
            continue;
        if(fileinfo_num >= fileinfo_maxnum)
        {
            fileinfo_maxnum *= 2;
            fileinfo_arr = (struct fileinfo *)realloc(fileinfo_arr, fileinfo_maxnum * sizeof(struct fileinfo));
        }

        //获取文件或目录信息
        struct stat sb;
        lstat(dirent_p->d_name, &sb);

        //存储信息
        strcpy(fileinfo_arr[fileinfo_num].f_name, dirent_p->d_name);
        fileinfo_arr[fileinfo_num].f_size = sb.st_size;
        fileinfo_arr[fileinfo_num].f_mtim = sb.st_mtim;
        fileinfo_num++;
    }
    closedir(dir_p);

    //目录下文件排序
    setlocale(LC_ALL, "");
    sort(fileinfo_arr, fileinfo_arr + fileinfo_num);

    for(int i = 0;i < fileinfo_num;i++)
    {
        //依次显示创建时间，文件大小，文件名称
        char time_display[50];
        time_t Time = fileinfo_arr[i].f_mtim.tv_sec;
        strftime(time_display, 50, "%Y-%m-%d %H:%M", localtime(&Time));

        char sizeinfo[20];
        double size = (double)fileinfo_arr[i].f_size;
        if(size > 1073741824)
            sprintf(sizeinfo, "%5.1fG", size/1073741824);
        else if(size > 1048576)
            sprintf(sizeinfo, "%5.1fM", size/1048576);
        else if(size > 1024)
            sprintf(sizeinfo, "%5.1fK", size/1024);
        else
            sprintf(sizeinfo, "%5.1fB", size);
        
        dprintf(socket_fd, "%s %s %s\n%s", time_display, sizeinfo, fileinfo_arr[i].f_name, (i==fileinfo_num-1?"\nEND\n":""));
    }
    return 1;
}
int command_pasv(int socket_fd)
{
    //判断数据接口是否已打开
    if(data_socket&&data_port != 20)
    {
        dprintf(socket_fd, "425 Already connected\n");
        return 1;
    }

    //监听本地随机端口
    struct sockaddr_in local_addr;
    socklen_t local_addr_len;
    local_addr_len = sizeof(local_addr);
    memset(&local_addr, 0, sizeof(local_addr));
    int local_socket = socket(AF_INET, SOCK_STREAM, 0);
    listen(local_socket, 10);

    //PASV响应本地IP和数据端口
    int h1, h2, h3, h4;
    getsockname(local_socket, (struct sockaddr *)&local_addr, &local_addr_len);
    sscanf(inet_ntoa(local_addr.sin_addr), "%d.%d.%d.%d", &h1, &h2, &h3, &h4);
    data_port = ntohs(local_addr.sin_port);
    dprintf(socket_fd, "227 entering passive mode (%d,%d,%d,%d,%lu,%lu)\n", h1, h2, h3, h4, data_port/256, data_port%256);

    //接受客户端连接
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    client_addr_len = sizeof(client_addr);
    memset(&client_addr, 0, sizeof(client_addr));
    data_socket = accept(local_socket, (struct sockaddr*)&client_addr, &client_addr_len);
    dprintf(data_socket, "Connected\n");
    return 1;
}
int command_port(char *str, int socket_fd)
{
    //判断数据接口是否已打开
    if(data_socket&&data_port != 20)
    {
        dprintf(socket_fd, "425 Already connected\n");
        return 1;
    }
    dprintf(socket_fd, "220 ready to connect\n");

    //连接客户端数据端口
    data_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in data_addr;
    memset(&data_addr, 0, sizeof(data_addr));
	data_addr.sin_family = AF_INET;

    int h1, h2, h3, h4, p1, p2;
    sscanf(str, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2);
    //sprintf(IP, "%d.%d.%d.%d", h1, h2, h3, h4);
    data_port = p1*256+p2;
    data_addr.sin_port = htons(data_port);
    data_addr.sin_addr.s_addr = inet_addr(IP);
	connect(data_socket, (struct sockaddr*)&data_addr, sizeof(data_addr));

    //测试是否连接
    dprintf(data_socket, "Connected\n");
    return 1;
}
int command_stor(char *str, int socket_fd)
{
    //检查数据连接是否建立
    if(data_socket == 0||data_port == 20)
    {
        dprintf(socket_fd, "425 please connect\n");
        return 1;
    }
    dprintf(socket_fd, "225 %s ready\r\n", str);

    //获取文件名
    char *upload_file;
    upload_file = strrchr(str, '/');
    if(upload_file == NULL) upload_file = str;
    else upload_file++;

    //打开文件
    FILE *fp = fopen(upload_file, "a");
    int res = 1;
    while(res)
    {
        memset(buf, 0, sizeof(buf));
        res = read(data_socket, buf, sizeof(buf)-1);
        fwrite(buf, 1, res, fp);
        if(res < (int)sizeof(buf)-1) break;
    }
    fclose(fp);
    dprintf(socket_fd, "%s upload finished\n", str);
    return 1;
}
int command_quit(int socket_fd)
{
    //退出
    dprintf(socket_fd, "221 EXIT\n");
    return 0;
}
int command_rest(char *str, int socket_fd)
{
    //设置rest_size
    rest_size = strtoul(str, NULL, 0);
    dprintf(socket_fd, "250 REST successful\n");
    return 1;
}
int command_size(char *str, int socket_fd)
{
    //获取文件绝对路径
    char request_file[PATH_MAX+1], temp_file[PATH_MAX+1];
    if(str[0] == '/')
    {
        strcpy(request_file, str);
    }
    else
    {
        if(current_dir[(int)strlen(current_dir)-1] != '/')
        {
            current_dir[(int)strlen(current_dir)+1] = '\0';
            current_dir[(int)strlen(current_dir)] = '/';
        }
        strcpy(request_file, current_dir);
        strcat(request_file, str);
    }

    //检查路径合法
    if(strlen(request_file) > PATH_MAX)
    {
        dprintf(socket_fd, "552 PATH too long\n");
        return 1;
    }
    else if(realpath(request_file, temp_file) == NULL)
    {
        dprintf(socket_fd, "450 Can't find %s\n", temp_file);
        return 1;
    }

    //获取文件大小
    struct stat sb;
    if(stat(request_file, &sb) < 0)
        dprintf(socket_fd, "450 Can't get size\n");
    else
        dprintf(socket_fd, "250 %ld\n", sb.st_size);
    return 1;
}
