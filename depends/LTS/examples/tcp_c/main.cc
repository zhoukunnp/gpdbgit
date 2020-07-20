#include "XTcp.h"
#include <iostream>
#include "string.h"

#include <sys/time.h>
#include <curl/curl.h>
#ifdef WIN32
#include <Windows.h>
#define socklen_t int
#else
#include <arpa/inet.h>

#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "ltsrpc.pb.h"
#include "XTcp.h"

#endif
using namespace std;
static XTcp xtcp = XTcp("127.0.0.1", 62389);
ltsrpc::GetTxnTimestampCtx lts;
void* recvTS(void* args)
{
    int headerLen = 2, bodyLen = 16;
    int maxBufLength = headerLen + bodyLen;
    char *allbuff = (char*)malloc(maxBufLength);
    int ind = 0;
    for (;;)
    {
        char *buffer = (char*)malloc(maxBufLength);
        int result = xtcp.Recv(buffer, maxBufLength);
        if (ind + result > maxBufLength)
        {
            ind = 0;
        }
        memcpy(allbuff + ind, buffer, result);
        ind += result;
        if (ind == maxBufLength)
        {
            ind = 0;
            uint16_t olen = *(uint16_t*)buffer;
            uint16_t respLen = ntohs(olen);
            lts.ParseFromArray(buffer + 2, respLen);
            //cout << lts.txn_ts() <<endl;
        }
    }
}

static uint64_t stouint64(char* ptr)
{
    uint64_t num = 0;
    int i = 0;
    while(ptr[i])
    {
        num = num * 10 + (ptr[i++] - '0');  
    }
    return num;
}

size_t WriteTimeStampCallBack(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize = size * nmemb;
    uint64_t *ts = (uint64_t*)data;
    *ts = stouint64((char*)ptr);
	//printf("%s", (char*)ptr);
    return realsize;
}

int main()
{

    if (!xtcp.isConnect())
    {
        xtcp.CreateSocket();
        xtcp.Connect(10);
    }
    int headerLen = 2, bodyLen = 16;
    int maxBufLength = headerLen + bodyLen;

    lts.set_txn_id(12345678);
    lts.set_txn_ts(0);

    std::string ltsmessage;
    lts.SerializeToString(&ltsmessage);
    uint16_t size = ltsmessage.length();
    uint16_t cpsize = htons(size);
    char* req = (char*)malloc(size + headerLen);
    char* ltsbuffer = const_cast<char*>(ltsmessage.c_str());
    memcpy(req, (void*)&cpsize, 2);
    memcpy(req + 2, ltsbuffer, size);

    pthread_t tids;
    int ret = pthread_create(&tids, NULL, recvTS, NULL);
    int result = 0;
    for (int i = 0; i < 1; i++)
        result = xtcp.Send(req, 24);
    struct timeval begintime, endtime;
    gettimeofday(&begintime, NULL);
    google::protobuf::ShutdownProtobufLibrary();
    
    while(lts.txn_ts() == 0);
    gettimeofday(&endtime, NULL);
    int timeuse =1000000 * ( endtime.tv_sec -begintime.tv_sec) + endtime.tv_usec -begintime.tv_usec;
    printf("all time:%dus\n",timeuse);
    return 0;

    // struct timeval begintime, endtime;
    // gettimeofday(&begintime, NULL);
    // CURL *curl;
    // CURLcode res;

    // curl = curl_easy_init();
    // if(!curl){
    //     fprintf(stderr, "curl_easy_init() error");
    //     return 1;
    // }
	// uint64_t ts;
	// curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ts);
	// curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteTimeStampCallBack);
    // curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:62379/lts-cluster/api/ts/1");
    // res = curl_easy_perform(curl);
    // if(res!=CURLE_OK){
    //     fprintf(stderr, "curl_easy_perform() error: %s\n", curl_easy_strerror(res));
    // }
    // curl_easy_cleanup(curl);
    // gettimeofday(&endtime, NULL);
    // int timeuse =1000000 * ( endtime.tv_sec -begintime.tv_sec) + endtime.tv_usec -begintime.tv_usec;
    // printf("all time:%dus\n",timeuse);
    // return 0;
}