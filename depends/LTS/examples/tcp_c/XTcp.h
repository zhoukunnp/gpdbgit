#ifndef XTCP_H
#define XTCP_H

#include <string>

class XTcp
{
public:
	int CreateSocket();
	bool Bind(unsigned short port);
	bool Listen(unsigned short num);
	bool SetBlock(bool isblock);
	bool Connect(const char *ip = "192.168.0.123", unsigned short port = 8000, int sec = 3);
	bool Connect(int sec = 3);
    bool isConnect();
	XTcp Accept();
	void Close();
	int Recv(char *buf, int size);
	int Send(const char *buf, int size);
	int SetRecvTimeout(int sec = 1);
	int SetSendTimeout(int sec = 1);

    char clientip[16] = {0};
	unsigned short clientport = 0;

	XTcp(unsigned short port = 8000);
    XTcp(const char *ip, unsigned short port);
	virtual ~XTcp();


private:
    char tcp_serverip[16] = {0};
	int tsock = 0;
	unsigned short tport = 0;
};

#endif