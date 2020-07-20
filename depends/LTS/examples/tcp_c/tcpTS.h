#ifndef TCPTS_H
#define TCPTS_H

#include <string>
#include "XTcp.h"
#include "ltsrpc.pb.h"
class LtsTimeStampGet
{
public:
    void initConnection();
    uint64_t GetTimeStamp();
    void closeConnection();
    static void* gettimestamp(void* args);
    LtsTimeStampGet(const char *ip, unsigned short port);
	virtual ~LtsTimeStampGet();
private:
    XTcp xtcp;
    pthread_t tids;
    bool close;
    ltsrpc::GetTxnTimestampCtx lts;
};

#endif