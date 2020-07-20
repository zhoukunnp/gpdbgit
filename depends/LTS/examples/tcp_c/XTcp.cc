#include "XTcp.h"
#include <iostream>
#include "string.h"

#ifdef WIN32
#include <Windows.h>
#define socklen_t int
#else
#include <arpa/inet.h>
#define closesocket close    //宏定义替换函数
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string>

#define strcpy_s strcpy
#endif

XTcp::XTcp(unsigned short port)
{
	//初始化动态链接库
	//引用lib库
#ifdef WIN32  //linux下不用初始化网络库
	static bool first = true;
	if (first)
	{
		first = false;						 //只在首次进入时初始化网络库
		WSADATA ws;					         //加载Socket库   项目属性-链接器-输入加上 ws2_32.lib
		WSAStartup(MAKEWORD(2, 2), &ws);     //动态库引用加1    
	}

#endif
	tport = port;  //接收main函数的参数   //
}

XTcp::XTcp(const char *ip,unsigned short port)
{
	//初始化动态链接库
	//引用lib库
#ifdef WIN32  //linux下不用初始化网络库
	static bool first = true;
	if (first)
	{
		first = false;						 //只在首次进入时初始化网络库
		WSADATA ws;					         //加载Socket库   项目属性-链接器-输入加上 ws2_32.lib
		WSAStartup(MAKEWORD(2, 2), &ws);     //动态库引用加1    
	}

#endif
	strcpy(tcp_serverip,ip);   			//tcp_serverip 服务端ip
	tport = port;  //接收main函数的参数   //
}



XTcp::~XTcp()
{

}


int XTcp::CreateSocket()				 //创建套接字
{
	//创建socket （TCP/IP协议 TCP）
	tsock = socket(AF_INET, SOCK_STREAM, 0);    //直接创建socket返回给XTcp的成员函数
	if (tsock == -1)
	{
		printf("create tcp socket failed.\n");
		return -1;
	}
	else
	{
		printf("create tcp socket successed.\n");
		return tsock;
	}
}

bool XTcp::Bind(unsigned short port)  //绑定并监听端口号(服务端用)
{
	sockaddr_in saddr;              //数据结构
	saddr.sin_family = AF_INET;     //协议
	saddr.sin_port = htons(port);   //端口，主机字节序（小端方式）转换成网络字节序（大端方式）             
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);   //绑定IP到广播地址INADDR_ANY 0.0.0.0  为了兼容linux   

	if (bind(tsock, (sockaddr*)&saddr, sizeof(saddr)) != 0)  
	{
		printf("tcp bind port %d failed.\n", port);
		return false;
	}
	printf("tcp bind port %d success.\n", port);
	return true;
}

bool XTcp::Listen(unsigned short num) //监听端口号
{
	int re = listen(tsock, 10);   //套接字，最大请求队列的长度   进入阻塞状态
	if(!re)
	{
		printf("tcp socket listen start.\n");
		return false;
	}
	else
	{
		printf("tcp socket listen failed.\n");
		return true;
	}
}

bool XTcp::SetBlock(bool isblock)  //设置阻塞模式  （希望只有在connect的时候是非阻塞的，而接收数据时候是阻塞的）
{
	if (tsock <= 0)
	{		
		printf("set tcp socket block failed.\n");
		return false;
	}
#ifdef WIN32
	unsigned long ul = 0;
	if (!isblock) ul = 1;
	ioctlsocket(tsock, FIONBIO, &ul);    //设置socket的模式(0 阻塞模式,1 非阻塞模式<connect就立即返回>)
#else
	int flags = fcntl(tsock, F_GETFL, 0);  //获取socket的属性
	if (flags < 0)return false; //获取属性出错
	if (isblock)
	{
		flags = flags&~O_NONBLOCK;  //把非阻塞这位设为0
	}
	else
	{
		flags = flags | O_NONBLOCK; //把非阻塞这位设为1
	}
	if (fcntl(tsock, F_SETFL, flags))return false;  //把标准位设回去
#endif

	if (isblock==0)
		printf("set tcp socket not block success.\n");
	if (isblock==1)
		printf("set tcp socket block success.\n");

	return true;
}

bool XTcp::Connect(const char *ip, unsigned short port , int sec)
{
	if (tsock <= 0)	return false;

	sockaddr_in saddr;   //设置连接对象的结构体
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = inet_addr(ip);  //字符串转整型

	SetBlock(false);    //将socket改成非阻塞模式，此时它会立即返回  所以通过fd_set
	fd_set rfds, wfds;	    //文件句柄数组，在这个数组中，存放当前每个文件句柄的状态

	if (connect(tsock, (sockaddr*)&saddr, sizeof(saddr)) != 0)   //此时connect马上返回，状态为未成功连接
	{

	    FD_ZERO(&rfds);  //首先把文件句柄的数组置空
	    FD_ZERO(&wfds);  
	    FD_SET(tsock, &rfds);   //把sock的网络句柄加入到该句柄数组中
	    FD_SET(tsock, &wfds); 


		timeval tm;  //超时参数的结构体
		tm.tv_sec = sec;
		tm.tv_usec = 0;

		int selres = select(tsock + 1, &rfds, &wfds, NULL, &tm);   //（阻塞函数）(监听的文件句柄的最大值加1，可读序列文件列表，可写的序列文件列表，错误处理，超时)使用select监听文件序列set是否有可读可写，这里监听set数组（里面只有sock），只要其中的句柄有一个变得可写（在这里是sock连接成功了以后就会变得可写，就返回），就返回
		switch (selres)
		{

			case -1:
                    printf("select error\n");  
					return false;
            case 0:  
               		printf("select time out\n");  
					return false;
			default:
					if (FD_ISSET(tsock, &rfds) || FD_ISSET(tsock, &wfds)) 
					{
							connect(tsock, (sockaddr*)&saddr, sizeof(saddr));    //再次连接一次进行确认
							int err = errno;  
							if  (err == EISCONN||err == EINPROGRESS)     //已经连接到该套接字 或 套接字为非阻塞套接字，且连接请求没有立即完成
							{  
					    		printf("connect %s : %d finished(success).\n",ip,port);  
							    SetBlock(true);   //成功之后重新把sock改成阻塞模式，以便后面发送/接收数据
							    return true;
							}  
							else  
							{  
							    printf("connect %s : %d finished(failed). errno = %d\n",ip,port,errno);  
							   // printf("FD_ISSET(sock_fd, &rfds): %d\n FD_ISSET(sock_fd, &wfds): %d\n", FD_ISSET(sock_fd, &rfds) , FD_ISSET(sock_fd, &wfds));  
							    return false;
							}  
					}
					else
						{
							    printf("connect %s : %d finished(failed).",ip,port);  
							    return false;
						}
		}

	}
	else  //连接正常
	{
		SetBlock(true);   //成功之后重新把sock改成阻塞模式，以便后面发送/接收数据
		printf("connect %s : %d finished(success).\n",ip,port);  
		return true;
		}
}

bool XTcp::Connect(int sec)
{
	if (tsock <= 0)	return false;

	sockaddr_in saddr;   //设置连接对象的结构体
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(tport);
	saddr.sin_addr.s_addr = inet_addr(tcp_serverip);  //字符串转整型

	SetBlock(false);    //将socket改成非阻塞模式，此时它会立即返回  所以通过fd_set
	fd_set rfds, wfds;	    //文件句柄数组，在这个数组中，存放当前每个文件句柄的状态

	if (connect(tsock, (sockaddr*)&saddr, sizeof(saddr)) != 0)   //此时connect马上返回，状态为未成功连接
	{

	    FD_ZERO(&rfds);  //首先把文件句柄的数组置空
	    FD_ZERO(&wfds);  
	    FD_SET(tsock, &rfds);   //把sock的网络句柄加入到该句柄数组中
	    FD_SET(tsock, &wfds); 


		timeval tm;  //超时参数的结构体
		tm.tv_sec = sec;
		tm.tv_usec = 0;

		int selres = select(tsock + 1, &rfds, &wfds, NULL, &tm);   //（阻塞函数）(监听的文件句柄的最大值加1，可读序列文件列表，可写的序列文件列表，错误处理，超时)使用select监听文件序列set是否有可读可写，这里监听set数组（里面只有sock），只要其中的句柄有一个变得可写（在这里是sock连接成功了以后就会变得可写，就返回），就返回
		switch (selres)
		{

			case -1:
                printf("select error\n");  
				return false;
            case 0:  
                printf("select time out\n");  
                return false;
			default:
                if (FD_ISSET(tsock, &rfds) || FD_ISSET(tsock, &wfds)) 
                {
                    connect(tsock, (sockaddr*)&saddr, sizeof(saddr));    //再次连接一次进行确认
                    int err = errno;  
                    if  (err == EISCONN||err == EINPROGRESS)     //已经连接到该套接字 或 套接字为非阻塞套接字，且连接请求没有立即完成
                    {  
                        printf("connect %s : %d finished(success).\n",tcp_serverip,tport);  
                        SetBlock(true);   //成功之后重新把sock改成阻塞模式，以便后面发送/接收数据
                        return true;
                    }  
                    else  
                    {  
                        printf("connect %s : %d finished(failed). errno = %d\n",tcp_serverip,tport,errno);  
                        // printf("FD_ISSET(sock_fd, &rfds): %d\n FD_ISSET(sock_fd, &wfds): %d\n", FD_ISSET(sock_fd, &rfds) , FD_ISSET(sock_fd, &wfds));  
                        return false;
                    }  
                }
                else
                {
                    printf("connect %s : %d finished(failed).",tcp_serverip,tport);  
                    return false;
                }
		}

	}
	else  //连接正常
	{
		printf("connect %s : %d finished(success).\n",tcp_serverip,tport);  
		SetBlock(true);   //成功之后重新把sock改成阻塞模式，以便后面发送/接收数据
		return true;
	}
}

bool XTcp::isConnect()
{
    if (tsock <= 0)
        return false;
    else 
        return true; 
}

XTcp XTcp::Accept()                   //返回XTcp对象，接收连接
{
	XTcp tcp;     //先定义一个XTcp对象，一会返回它

	sockaddr_in caddr;
	socklen_t len = sizeof(caddr);

	tcp.tsock = accept(tsock, (sockaddr*)&caddr, &len);  //（阻塞）接收连接  ，会创建一个新的socket，一般扔到一个单独线程与这个客户端进行单独通信，之前的sock只用来建立连接
	if (tcp.tsock <= 0)	return tcp;   //出错
	printf("accept client %d\n", tcp.tsock);
	char *ip = inet_ntoa(caddr.sin_addr);				 //解析出IP地址  ，转换到字符串

	strcpy_s(tcp.clientip, ip);
	tcp.clientport = ntohs(caddr.sin_port);		 //解析出端口，转换成主机字节序
	printf("client ip is %s,port is %d\n", tcp.clientip, tcp.clientport);  //打印ip和端口
	return tcp;

}

void XTcp::Close()                    //关闭连接
{
	if (tsock <= 0) return;  //socket出错
	closesocket(tsock);		//已宏定义
	tsock = 0;
}

int XTcp::Recv(char *buf, int size)                      //接收数据
{
	return recv(tsock, buf, size, 0);
}

int XTcp::Send(const char *buf, int size)					     //发送数据
{
	int sendedSize = 0;   //已发送成功的长度
	while (sendedSize != size)   //若没发送完成，则从断点开始继续发送 直到完成
	{
		int len = send(tsock, buf + sendedSize, size - sendedSize, 0);
		if (len <= 0)break;
		sendedSize += len;
	}
	return sendedSize;
}

int XTcp::SetRecvTimeout(int sec)   //设置tcp接收超时
{
#ifdef WIN32
	int tcp_rev_time = sec * 1000;
	if (setsockopt(tsock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tcp_rev_time, sizeof(int))<0)
	{
		printf("set tcp receive failed.\n");
		return -1;
	}
	printf("set tcp recv timeout success. %d seconds.\n", sec);
	return 0;
#else
	struct timeval tcp_rev_time;
	tcp_rev_time.tv_sec = sec;
	tcp_rev_time.tv_usec = 0;
	if (setsockopt(tsock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tcp_rev_time, sizeof(tcp_rev_time))<0)
	{
		printf("set tcp receive failed.\n");
		return -1;
	}
	printf("set tcp recv timeout success. %d seconds.\n", sec);
	return 0;
#endif
}

int XTcp::SetSendTimeout(int sec)   //设置tcp发送超时
{
#ifdef WIN32
	int tcp_send_time = sec;
	if (setsockopt(tsock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tcp_send_time, sizeof(int))<0)
	{
		printf("set tcp send failed.\n");
		return -1;
	}
	return 0;
	printf("set tcp recv timeout success. %d seconds.\n", sec);
#else
	struct timeval tcp_send_time;
	tcp_send_time.tv_sec = sec;
	tcp_send_time.tv_usec = 0;
	if (setsockopt(tsock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tcp_send_time, sizeof(tcp_send_time))<0)
	{
		printf("set tcp send failed.\n");
		return -1;
	}
	printf("set tcp recv timeout success. %d seconds.\n", sec);
	return 0;
#endif
}