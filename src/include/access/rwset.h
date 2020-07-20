/*-------------------------------------------------------------------------
 *
 * rwset.h
 *	  read and write set maintain for optimistic concurrency control
 *
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/rwset.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef RWSET_H
#define RWSET_H
#include "nodes/nodes.h"
#include "tdb/kv_universal.h"
#include "utils/hsearch.h"
/* maximum key size */
#define SHMEM_KEYXIDS_KEYSIZE		(128)
/* maximum count of read set*/
#define SHMEM_READ_SET_SIZE			(10240)
/* maximum count of read set*/
#define SHMEM_WRITE_SET_SIZE		(1024)
/* maximum count of read set*/
#define SHMEM_WRITE_SET_LENGTH		(1024)
/* init size of keys' hash table */
#define SHMEM_KEYXIDS_INIT_SIZE		(1024)
/* maximum size of keys' hash table */
#define SHMEM_KEYXIDS_SIZE			(5000000)
/* maximum size of keys' read txn list */
#define SHMEM_READXIDS_SIZE			(3000)

typedef	enum
{
	PENDING,
	RUNNING,
	VALIDATING,
	VALIDATED,
	COMMITTED,
	ABORTING,
	ABORTED
} ShmTransactionStatus;

typedef struct ReadWriteSetSlot
{
	int32	slotindex;
	int32	slotid;
	pid_t	pid;
	DistributedTransactionId gxid;
	DistributedTransactionId val_gxid;
	uint32	lower;
	uint32	upper;
	ShmTransactionStatus status;
	HTAB 	*readSet;
	HTAB	*writeSet;
} ReadWriteSetSlot;

typedef struct ReadWriteSetStruct
{
	int	numSlots;
	int	maxSlots;
	int	nextSlot;

	ReadWriteSetSlot	*slots;
	char	*keySet;

} ReadWriteSetStruct;

typedef struct ShmemReadWriteSetKey
{
	char	key[SHMEM_KEYXIDS_KEYSIZE];
	Size	keyLen;
	uint32	lts;
} ShmemReadWriteSetKey;

/* this is a hash bucket in the shmem key-xids hash table */
typedef struct ShmemXidsEnt
{
	char	key[SHMEM_KEYXIDS_KEYSIZE];
	Size	keyLen;
	int		ptr;
	DistributedTransactionId	readTxn[SHMEM_READXIDS_SIZE];
	DistributedTransactionId	writeTxn;
} ShmemXidsEnt;

typedef struct ShmemRtsEnt
{
	char	key[SHMEM_KEYXIDS_KEYSIZE];
	Size	keyLen;
	uint32	rts;
} ShmemRtsEnt;

typedef struct ShmemFinishedGxidEnt
{
	DistributedTransactionId gxid;
	char	writeSet[SHMEM_WRITE_SET_LENGTH];
	Size	writeSetCount;
	Size	writeSetLen;
} ShmemFinishedGxidEnt;


extern volatile ReadWriteSetSlot *CurrentReadWriteSetSlot;

extern Size ReadWriteSetShmemSize(void);
extern void CreateReadWriteSetArray(void);
extern void ReadWriteSetSlotAdd(char *creatorDescription, int32 slotId);
extern bool DTAScanCallBack(Size len, char *data, TupleKeySlice* newkey, TupleValueSlice* newvalue);
extern void AppendKeyToReadWriteSet(Size len, char *data, CmdType type);
extern void ReadWriteSetSlotRemove(char *creatorDescription);
extern int BoccValidation(void);
extern int FoccValidation(void);

extern Size KeyXidsHashTableShmemSize(void);
extern void InitKeyXidsHashTable(void);
extern void AppendReadXidWithKey(Size len, char *data, CmdType type);

extern bool CheckLogicalTsIntervalValid(uint32 lower, uint32 upper);
extern bool CheckTxnStatus(void);
extern void AssembleRTS(void);
extern void AbortCleanUp(void);
extern uint32 LogicalCommitTsAllocation(void);
extern int DTAValidation(void);

typedef struct TransactionStatistics
{
	slock_t txn_mutex;
    int allTransactionNum;
	int RollBackTransactionNum;
    int CommitTransactionNum;
	int Loop;
} TransactionStatistics;

extern TransactionStatistics *transaction_statistics;
extern void TransactionStatisticsShmemInit(void);
extern Size TransactionStatisticsShmemSize(void);
extern void RollBackCountAdd(void);
extern void AllTransCountAdd(void);
extern int GetRollBackCount(void);
extern void CommitTranCountAdd(void);
#endif   /* RWSET_H */

