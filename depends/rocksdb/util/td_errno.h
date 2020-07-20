/*
 * tdstore error
 * kangsun@tencent.com
 */

#pragma once

namespace tdstore {
namespace common {
  // tdstore service
  static const int TDS_SUCCESS = 0;
  static const int TDS_FATAL = 331000; // fatal
  static const int TDS_INTERNAL_ERROR = 331001; // internal error
  static const int TDS_ERROR_UNEXPECTED = 331002; // unexpected error
  static const int TDS_INVALID_ARGUMENT = 331003; // invalid argument
  static const int TDS_TIMEOUT = 331004; // timeout
  static const int TDS_EAGAIN = 331005; // eagain
  static const int TDS_NOT_SUPPORTED = 331006; // not support
  static const int TDS_NOT_INIT = 331007; // not init
  static const int TDS_INIT_TWICE = 331008; // init twice
  static const int TDS_OPEN_ROCKSDB_FAILED = 331009; // open rocksdb failed
  static const int TDS_OPEN_ROCKSDB_ENGINE_FAILED = 331010; // open rocksdb engine failed
  static const int TDS_ALLOCATE_MEMORY_FAILED = 331011; // allocate memory failed
  static const int TDS_ROCKSDB_PUT_FAILED = 331012; // put key/value failed
  static const int TDS_ROCKSDB_GET_FAILED = 331013; // get value failed
  static const int TDS_ROCKSDB_DELETE_FAILED = 331014; // delete key failed
  static const int TDS_ROCKSDB_INITIALIZE_OPTION_FAILED = 331015; // rocksdb option initialize failed
  static const int TDS_ROCKSDB_INITIALIZE_BLOCK_CACHE_FAILED = 331016; // rocksdb option initialize block cache
  static const int TDS_SERIALIZE_ERROR = 331017; // tdstore serialize error
  static const int TDS_DESERIALIZE_ERROR = 331018; // tdstore deserialize error
  static const int TDS_SIZE_OVERFLOW = 331019; // size overflow
  static const int TDS_BUF_NOT_ENOUGH = 331020; // buf not enough
  static const int TDS_ENTRY_NOT_EXIST = 331021; // entry not exist
  static const int TDS_ARRAY_OUT_OF_RANGE = 331022; // out of array range
  static const int TDS_THE_MEMBER_IS_NOT_IN_THE_TDSTORE = 331023;
  static const int TDS_ENTRY_TOO_LARGE = 331024; // entry too large
  static const int TDS_STR_TO_ENDPOINT_FAILED = 331025;

  // rpc and heartbeat
  static const int TDS_RPC_ERROR = 331101; // rpc error
  static const int TDS_RPC_SERIALIZE_FAILED = 331102; // serialize rpc failed
  static const int TDS_RPC_CONTENT_ERROR = 331103; // rpc content error
  static const int TDS_RPC_DESTROY = 331104; // rpc content destroy
  static const int TDS_RPC_ALLOCATE_MEMORY_FAILED = 331105; // rpc allocate memory failed
  static const int TDS_META_CHANNEL_INIT_FAILED = 331106; // init meta rpc channel failed
  static const int TDS_META_RPC_FAILED = 331107; // mc client meta rpc failed
  static const int TDS_META_RPC_EAGAIN = 331108; // mc client meta rpc eagain
  static const int TDS_META_RPC_TIMEOUT = 331109; // mc client meta rpc time out
  static const int TDS_STORE_CHANNEL_INIT_FAILED = 331110; // init store rpc channel failed
  static const int TDS_STORE_RPC_FAILED = 331111; // store rpc failed
  static const int TDS_STORE_RPC_EAGAIN = 331112; // store meta rpc eagain
  static const int TDS_STORE_RPC_TIMEOUT = 331113; // store rpc time out
  static const int TDS_RPC_STREAM_CREATE_FAILED = 331114; // stream rpc create failed
  static const int TDS_RPC_STREAM_RECEIVE_FAILED = 331115; // stream rpc receive failed
  static const int TDS_RPC_STREAM_WRITE_FAILED = 331116; // stream rpc write failed
  static const int TDS_RPC_STREAM_CLOSE_FAILED = 331117; // stream rpc close failed

  // transaction
  static const int TDS_TRANS_ERROR = 331201; // transaction error
  static const int TDS_TRANS_START_TRANS_FAILED = 331202; // start transaction failed
  static const int TDS_TRANS_START_STMT_FAILED = 331203; // start statement failed
  static const int TDS_TRANS_START_PARTICIPANT_FAILED = 331204; // start participant failed
  static const int TDS_TRANS_END_TRANS_FAILED = 331205; // end transaction failed
  static const int TDS_TRANS_END_STMT_FAILED = 331206; // end statment failed
  static const int TDS_TRANS_END_PARTICIPANT_FAILED = 331207; // end participant failed
  static const int TDS_TRANS_BEGIN_FAILED = 331208; // begin transaction failed
  static const int TDS_TRANS_PUT_FAILED = 331209; // put transaction failed
  static const int TDS_TRANS_GET_NOT_FOUND = 331210; // transaction get not found
  static const int TDS_TRANS_GET_FAILED = 331211; // transaction get failed
  static const int TDS_TRANS_SETUP_ITERATOR_FAILED = 331212; // setup iterator failed
  static const int TDS_TRANS_ITERATOR_INVALID = 331213; // iterator invalid
  static const int TDS_TRANS_ITERATOR_SEEK_NEXT_FAILED = 331214; // iterator seek failed
  static const int TDS_TRANS_ITERATOR_SEEK_PREV_FAILED = 331215; // iterator seek failed
  static const int TDS_TRANS_ITERATOR_NEXT_FAILED = 331216; // iterator next failed
  static const int TDS_TRANS_ITERATOR_END = 331217; // iterator end
  static const int TDS_TRANS_ITERATOR_PREV_FAILED = 331218; // iterator prev failed
  static const int TDS_TRANS_COMMIT_FAILED = 331219; // commit transaction failed
  static const int TDS_TRANS_ROLLBACK_FAILED = 331220; // rollback transaction failed
  static const int TDS_TRANS_CTX_INSERT_FAILED = 331221; // trans context insert failed
  static const int TDS_TRANS_CTX_NOT_EXIST = 331222; // trans context not exist
  static const int TDS_TRANS_CTX_GET_FAILED = 331223; // trans context get failed
  static const int TDS_TRANS_CTX_DELETE_FAILED = 331224; // trans context delete failed
  static const int TDS_TRANS_CTX_ALLOCATE_MEMORY_FAILED = 331225; // trans context allocate memory failed
  static const int TDS_TRANS_REGION_RANGE_MISMATCH = 331226;//trans region range mismatch
  static const int TDS_TRANS_REGION_VERSION_ERROR = 331227;//trans region version error
  static const int TDS_TRANS_REGION_CANT_WRITE = 331228;//trans region leader has change
  static const int TDS_TRANS_REGION_CANT_READ = 331229;//trans region leader has change
  static const int TDS_TRANS_NOT_NEED_COMMIT = 331230; // read-only transaction not need commit
  static const int TDS_TRANS_STATE_TRANSITION_FAILED = 331231;//trans state change failed
  static const int TDS_TRANS_STATE_NOT_MATCH = 331232;//trans state not match
  static const int TDS_TRANS_PREPARE_FAILED = 331233; // prepare transaction failed
  static const int TDS_TRANS_CONSTRUCT_CLOSURE_FAILED = 331234;//construct closure failed
  static const int TDS_TRANS_KEY_CONFLICT_FAILED = 331235;//trans key conflict
  static const int TDS_TRANS_LOCK_INSERT_FAILED = 331236; // trans lockmap insert failed
  static const int TDS_TRANS_LOCK_FIND_FAILED = 331237; // trans lockmap find failed
  static const int TDS_TRANS_CLEANUP_FAILED = 331238; // cleanup transaction failed
  static const int TDS_TRANS_TIMEOUT = 331239; // transaction timeout
  static const int TDS_TRANS_HAS_DECIDED = 331240; // transaction has decided
  static const int TDS_TRANS_IS_EXITING = 331241; // transaction is exiting
  static const int TDS_TRANS_INVALID_STATE = 331242; // transaction state is invalid
  static const int TDS_TRANS_INVALID_MSG_TYPE = 331243; // invalid 2pc rpc message
  static const int TDS_TRANS_ROLLBACK = 331244; // transaction  rollback
  static const int TDS_TRANS_STATE_UNKNOWN = 331245; // transaction state unknown
  static const int TDS_TRANS_PROTOCOL_ERROR = 331246; // transaction protocl error
  static const int TDS_TRANS_PREPARE_BUSY = 331247; // prepare transaction busy failed
  static const int TDS_TRANS_ALREADY_SUCCESS = 331248; // prepare transaction already success
  static const int TDS_TRANS_GET_LOCK_TIMEOUT = 331249; // transaction get key by lock timeout

  // region
  static const int TDS_REGION_ERROR = 331301; // region error;
  static const int TDS_REGION_ALLOC_NODE_FAILED = 331302; // alloc raft node failed
  static const int TDS_REGION_ADD_FAILED = 331303; // add region failed
  static const int TDS_REGION_GET_FAILED = 331304; // get region failed
  static const int TDS_REGION_NOT_EXIST = 331305; //  region not exist
  static const int TDS_REGION_DELETE_FAILED = 331306; // delete region failed
  static const int TDS_REGION_INSERT_FAILED = 331307; // insert region to map/set failed
  static const int TDS_REGION_REGISTER_FAILED = 331308; // register failed
  static const int TDS_REGION_HEARTBEAT_FAILED = 331309; // heartbeat failed
  static const int TDS_REGION_TDSTORE_ID_FAILED = 331310; // tdstore id unconsistency
  static const int TDS_REGION_EXIST_ALREADY = 331311; //region exist
  static const int TDS_REGION_KEY_NOT_EXIST = 331312; //key not in region
  static const int TDS_REGION_ID_ERROR = 331313; // region id error;
  static const int TDS_REGION_CTX_INSERT_FAILED = 331314;//region job context insert failed
  static const int TDS_REGION_CTX_NOT_EXIST = 331315; // region job context not exist
  static const int TDS_REGION_STATE_TRANSITION_FAILED = 331316; // region state transition failed
  static const int TDS_REGION_STATE_NOT_MATCH = 331317; // region state not match
  static const int TDS_REGION_CONSTRUCT_CLOSURE_FAILED = 331318;//construct closure failed;

  // raft node
  static const int TDS_RAFT_ERROR = 331401; // raft error
  static const int TDS_RAFT_ALLOC_NODE_FAILED = 331402; // alloc raft node failed
  static const int TDS_RAFT_CREATE_DIRECTORY_FAILED = 331403; // raft directory create failed
  static const int TDS_RAFT_OPTION_INIT_FAILED = 331404; // raft option init failed
  static const int TDS_RAFT_NODE_INIT_FAILED = 331405; // raft node init failed
  static const int TDS_RAFT_NOT_LEADER = 331406; // raft node is not leader
  static const int TDS_RAFT_GET_SNAPSHOT_FILE_FAILED = 331407; // raft snapshot get file failed
  static const int TDS_RAFT_LOAD_SNAPSHOT_FILE_FAILED = 331408; // raft snapshot load file failed
  static const int TDS_RAFT_TRANSFER_LEADERSHIP_FAILED = 331409; // raft transfer leadership failed
  static const int TDS_RAFT_NOT_LEADER_FAILED = 331410; // raft not leader failed
  static const int TDS_RAFT_REST_PEERS_FAILED = 331411; // raft reset peers failed

  // raft log
  static const int TDS_RLOG_ERROR = 331501; // raft log error
  static const int TDS_RLOG_REDO_SIZE_OVERFLOW = 331502; // redo log size overflow
  static const int TDS_RLOG_REDO_EMPTY = 331503; // redo log empty
  static const int TDS_RLOG_ALLOC_LOG_BUFFER_FAILED = 331504; // alloc redo log failed
  static const int TDS_RLOG_GET_LOG_INDEX_FAILED = 331505; // get raft log index failed
  static const int TDS_LOG_MAGIC_NUMBER_FAILED = 331506; // check log header maigic number failed

  // replay
  static const int TDS_LOG_REPLAY_ERROR = 331601; // replay error
  static const int TDS_TRANS_REPLAY_LOG_CTX_NOT_ENOUGH = 331602; // log content too small
  static const int TDS_TRANS_REPLAY_READ_LOG_CTX_FAILED = 331603; // parse log ctx failed
  static const int TDS_TRANS_REPLAY_REDO_LOG_FAILED = 331604; // replay redo log failed
  static const int TDS_TRANS_REPLAY_PREPARE_LOG_FAILED = 331605; // replay prepare log failed
  static const int TDS_TRANS_REPLAY_COMMIT_LOG_FAILED = 331606; // replay commit log failed
  static const int TDS_TRANS_REPLAY_ABORT_LOG_FAILED = 331607; // replay abort log failed

  // election
  // storage & flush & compaction
  static const int TDS_STORAGE_ERROR = 331701; // storage error
  static const int TDS_STORAGE_CHECKSUM_ERROR = 331702; // checksum error
  static const int TDS_STORAGE_READ_WRITE_BATCH_FAILED = 331703; // read record from write batch failed
  static const int TDS_STORAGE_CF_INSERT_FAILED = 331704; // column family insert failed
  static const int TDS_CFH_NOT_EXIST = 331705; // column family handle not exist
  static const int TDS_STORAGE_REGION_LOG_ID_NOT_EXIST = 331706; // region log id not exist
  static const int TDS_MIGRATE_ERROR = 331707; // migrate error
  static const int TDS_MIGRATE_CANT_EXEC = 331708; // cann't execute migrate
  static const int TDS_STORAGE_KEY_NOT_FOUND_IN_FILE = 331709; // key not found in the file
  static const int TDS_STORAGE_SST_WRITE_FAILED = 331710; //write sstable file failed
  static const int TDS_STORAGE_SST_OPEN_FAILED = 331711; // open sstable file failed
  static const int TDS_STORAGE_SST_CLOSE_FAILED = 331712; // close sstable file failed
  static const int TDS_STORAGE_PARSE_KV_FAILED = 331713; // parse kv from data failed
  static const int TDS_STORAGE_FLUSH_FAILED = 331714; // manual flush memtable failed
  static const int TDS_STORAGE_COMPACT_FAILED = 331715; // manual compact sstables failed

  //common & lib
  static const int TDS_LIB_ITERATOR_INVALID = 331801;
  static const int TDS_LIB_VECTOR_ITERATOR_RANGE_ERROR = 331802;//range error
  static const int TDS_LIB_CONTAINER_EMPTY = 331803; //container is empty
  static const int TDS_COMMON_CREATE_DIR_FAILED = 331804; // create dir failed
  static const int TDS_COMMON_DELETE_DIR_FAILED = 331805; // delete dir failed

  //replica
  static const int TDS_REPLICA_CONSTRUCT_CLOSURE_FAILED = 331901;
  static const int TDS_REPLICA_ALREADY_EXIST = 331902;

  //region && replica job
  static const int TDS_REGION_JOB_ALREADY_EXIST = 332001;
  static const int TDS_REPLICA_JOB_ALREADY_EXIST = 332002;

} // namespace common
} // namespace tdstore
