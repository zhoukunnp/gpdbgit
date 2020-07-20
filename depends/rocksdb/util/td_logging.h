/*
 * tdsql logging
 * kangsun@tencent.com
 */

#pragma once

#include "rocksdb/env.h"
#include "util/logging.h"
//#include "util/mutexlock.h"

namespace tdstore {

extern rocksdb::Logger* GetTDLogger();
extern void SetTDLogger(rocksdb::Logger *td_logger);

#define TD_MOD_LOG_NEED_TO_PRINT(logger, level, mod) \
  (logger && logger->NeedPrint(rocksdb::InfoLogLevel::level##_LEVEL, \
                               rocksdb::ModLogFlag::mod##_FLAG))

#define TD_MOD_LOG(level, mod, fmt, args...) \
  ((TD_MOD_LOG_NEED_TO_PRINT(::tdstore::GetTDLogger(), level, mod))?  \
  MOD_ROCKS_LOG_##level(::tdstore::GetTDLogger(), mod, fmt, ##args) : void(0))

#define TDSTORE_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, TDSTORE, fmt, ##args)
#define TDSTORE_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, TDSTORE, fmt, ##args)
#define TDSTORE_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, TDSTORE, fmt, ##args)
#define TDSTORE_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, TDSTORE, fmt, ##args)
#define TDSTORE_LOG(errcode, fmt, args...)  \
  if(errcode == ret) { \
    TDSTORE_LOG_WARN(fmt, ##args); \
  } else { \
    TDSTORE_LOG_ERROR(fmt, ##args); \
  } \

#define REGION_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, REGION, fmt, ##args)
#define REGION_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, REGION, fmt, ##args)
#define REGION_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, REGION, fmt, ##args)
#define REGION_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, REGION, fmt, ##args)
#define REGION_LOG(errcode, fmt, args...)  \
  if(errcode == ret) { \
    REGION_LOG_WARN(fmt, ##args); \
  } else { \
    REGION_LOG_ERROR(fmt, ##args); \
  } \

#define TRANS_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, TRANS, fmt, ##args)
#define TRANS_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, TRANS, fmt, ##args)
#define TRANS_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, TRANS, fmt, ##args)
#define TRANS_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, TRANS, fmt, ##args)
#define TRANS_LOG(errcode, fmt, args...)  \
  if(errcode == ret) { \
    TRANS_LOG_WARN(fmt, ##args); \
  } else { \
    TRANS_LOG_ERROR(fmt, ##args); \
  } \

#define ELECT_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, ELECT, fmt, ##args)
#define ELECT_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, ELECT, fmt, ##args)
#define ELECT_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, ELECT, fmt, ##args)
#define ELECT_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, ELECT, fmt, ##args)

#define RAFT_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, RAFT, fmt, ##args)
#define RAFT_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, RAFT, fmt, ##args)
#define RAFT_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, RAFT, fmt, ##args)
#define RAFT_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, RAFT, fmt, ##args)

#define REPLAY_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, REPLAY, fmt, ##args)
#define REPLAY_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, REPLAY, fmt, ##args)
#define REPLAY_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, REPLAY, fmt, ##args)
#define REPLAY_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, REPLAY, fmt, ##args)

#define RLOG_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, RLOG, fmt, ##args)
#define RLOG_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, RLOG, fmt, ##args)
#define RLOG_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, RLOG, fmt, ##args)
#define RLOG_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, RLOG, fmt, ##args)

#define MEM_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, MEMTABLE, fmt, ##args)
#define MEM_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, MEMTABLE, fmt, ##args)
#define MEM_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, MEMTABLE, fmt, ##args)
#define MEM_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, MEMTABLE, fmt, ##args)

#define STORAGE_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, STORAGE, fmt, ##args)
#define STORAGE_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, STORAGE, fmt, ##args)
#define STORAGE_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, STORAGE, fmt, ##args)
#define STORAGE_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, STORAGE, fmt, ##args)

#define MIGRATOR_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, MIGRATOR, fmt, ##args)
#define MIGRATOR_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, MIGRATOR, fmt, ##args)
#define MIGRATOR_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, MIGRATOR, fmt, ##args)
#define MIGRATOR_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, MIGRATOR, fmt, ##args)

#define FLUSH_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, FLUSH, fmt, ##args)
#define FLUSH_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, FLUSH, fmt, ##args)
#define FLUSH_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, FLUSH, fmt, ##args)
#define FLUSH_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, FLUSH, fmt, ##args)

#define COMPACT_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, COMPACT, fmt, ##args)
#define COMPACT_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, COMPACT, fmt, ##args)
#define COMPACT_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, COMPACT, fmt, ##args)
#define COMPACT_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, COMPACT, fmt, ##args)

#define SCHEMA_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, SCHEMA, fmt, ##args)
#define SCHEMA_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, SCHEMA, fmt, ##args)
#define SCHEMA_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, SCHEMA, fmt, ##args)
#define SCHEMA_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, SCHEMA, fmt, ##args)

#define LIB_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, LIB, fmt, ##args)
#define LIB_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, LIB, fmt, ##args)
#define LIB_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, LIB, fmt, ##args)
#define LIB_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, LIB, fmt, ##args)

#define COMMON_LOG_DEBUG(fmt, args...) TD_MOD_LOG(DEBUG, COMMON, fmt, ##args)
#define COMMON_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, COMMON, fmt, ##args)
#define COMMON_LOG_WARN(fmt, args...) TD_MOD_LOG(WARN, COMMON, fmt, ##args)
#define COMMON_LOG_ERROR(fmt, args...) TD_MOD_LOG(ERROR, COMMON, fmt, ##args)

//SPECIAL_LOG: tdstore interface trace log
#define SPECIAL_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, SPECIAL, fmt, ##args)

//MEMORY_TRACE: memory statistics or hook malloc info
#define MEMORY_TRACE_LOG_INFO(fmt, args...) TD_MOD_LOG(INFO, MEMORY_TRACE, fmt, ##args)

#define TD_LOG_FLUSH() LogFlush(::tdstore::GetTDLogger())
#define TD_DEBUG_LOG_FLUSH() LogFlush(::tdstore::GetTDLogger())

}
