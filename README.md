# SimpleFTP
简陋的模仿FTP协议进行通信的服务端和客户端。  

### Compile
* gcc ftpserver.c -o ftpserver  
* gcc ftpclient.c -o ftpclient  

### Usage
* ./ftpserver -p 7637  
* ./ftpclient -p 7637 127.0.0.1  
  username: admin  
  password: admin  
  Then try `help` to get more information.  

### Commands
* User 命令  
* Pass 命令  
  使用用户名和密码检验用户合法性  
* PASV 命令  
  获得服务器端空闲端口,发送给客户端,作为数据传输的端口  
* Size 命令  
  使用命令端口返回文件大小  
* REST 命令  
  记录文件偏移量,后续客户端下载文件时使用  
* RETR 命令  
  通过数据端口发送文件  
* STOR 命令  
  通过数据端口接收文件  
* QUIT 命令  
  断开该客户端的连接  
* CWD 命令  
  设置用户的工作目录,即上传和下载文件的位置  
* LIST 命令  
  列出工作目录下的文件信息,包括文件大小,文件创建时间,文件名称  
