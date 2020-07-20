#include "XTcp.h"
#include <iostream>
#include "string.h"
#include "ltsrpc.pb.h"
#include "XTcp.h"
#include "tcpTS.h"

#ifdef WIN32
#include <Windows.h>
#define socklen_t int
#else
#include <arpa/inet.h>

#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <thread>
#endif

static int headerLen = 2, bodyLen = 16;
static int maxBufLength = headerLen + bodyLen;

LtsTimeStampGet::LtsTimeStampGet(const char *ip, unsigned short port)
{
    xtcp = XTcp(ip, port);
    tids = 0;
    close = false;
    lts.set_txn_id(1);
    lts.set_txn_ts(0);
    initConnection();
}

LtsTimeStampGet::~LtsTimeStampGet()
{
	delete this;
}

void* LtsTimeStampGet::gettimestamp(void *arg)
{
    LtsTimeStampGet *get = (LtsTimeStampGet*)arg;
    while (get->close)
    {
        char *buffer = (char*)malloc(maxBufLength);
        int result = get->xtcp.Recv(buffer, maxBufLength);
        uint16_t olen = *(uint16_t*)buffer;
        uint16_t respLen = ntohs(olen);
        get->lts.ParseFromArray(buffer + 2, respLen);
    }
}

void LtsTimeStampGet::initConnection()
{
    xtcp.CreateSocket();
    xtcp.Connect(3);
    close = false;
    int ret = pthread_create(&tids, NULL, gettimestamp, this);
}

uint64_t LtsTimeStampGet::GetTimeStamp(void)
{
    lts.set_txn_id(1);
    lts.set_txn_ts(0);
    std::string ltsmessage;
    lts.SerializeToString(&ltsmessage);
    
    uint16_t size = ltsmessage.length();
    uint16_t cpsize = htons(size);
    char* req = (char*)malloc(size + headerLen);
    char* ltsbuffer = const_cast<char*>(ltsmessage.c_str());
    memcpy(req, (void*)&cpsize, 2);
    memcpy(req + 2, ltsbuffer, size);

    int result = xtcp.Send(req, size + headerLen);

    google::protobuf::ShutdownProtobufLibrary();

    while (lts.txn_ts() == 0);
    return lts.txn_ts();
}

void LtsTimeStampGet::closeConnection()
{
    close = true;
    xtcp.Close();
}