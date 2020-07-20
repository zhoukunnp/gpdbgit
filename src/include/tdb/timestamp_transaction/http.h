#include <stdlib.h>
#include <unistd.h>
#include "postgres.h"

#include <curl/curl.h>
#include "storage/spin.h"
#ifndef HTTP_TS_H
#define HTTP_TS_H
typedef struct Lts_timestamp
{
    uint64 lts;
    slock_t lts_lock;
}Lts_timestamp;
extern uint64 GetTimeStamp(void);
extern void getltsShmemInit(void);
extern Size getltsShmemSize(void);
extern Lts_timestamp* lts_timestamp;

extern void CloseToLts(void);
extern void ConnectToLts(void);
#endif
