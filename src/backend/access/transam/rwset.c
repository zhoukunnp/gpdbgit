#include "postgres.h"

#include "access/rwset.h"
#include "tdb/kv_struct.h"
#include "tdb/timestamp_transaction/lts_generate_key.h"
#include "storage/shmem.h"
#include "access/twophase.h"
#include "miscadmin.h"
#include "utils/guc.h"
#include "cdb/cdbtm.h"
#include "tdb/tdbkvam.h"
#include "tdb/storage_param.h"

static volatile ReadWriteSetStruct *readWriteSetArray;
volatile ReadWriteSetSlot *CurrentReadWriteSetSlot = NULL;

static Size slotCount = 0;
static Size slotSize = 0;

static HTAB *ShmemKeyXids = NULL; /* primary index hashtable for shmem */
static HTAB *ShmemRts = NULL;
static HTAB *ShmemFinishedGxids = NULL;

TransactionStatistics *transaction_statistics = NULL;

static char *ReadWriteSetDump(void);
static void KeyXidsEntRemove(Size len, char *data);

Size ReadWriteSetShmemSize(void)
{
	Size size;

	/* TODO: ensure maximum key number that
	 * stored in per transacton's read/write set */
	slotSize = sizeof(ReadWriteSetSlot);

	slotSize = add_size(slotSize, hash_estimate_size(SHMEM_READ_SET_SIZE,
											 sizeof(ShmemReadWriteSetKey)));
	slotSize = add_size(slotSize, hash_estimate_size(SHMEM_WRITE_SET_SIZE,
											 sizeof(ShmemReadWriteSetKey)));
	slotSize = MAXALIGN(slotSize);

	/* Maximum active transctions */
	slotCount = 2 * (MaxBackends + max_prepared_xacts);

	size = offsetof(ReadWriteSetStruct, keySet);
	size = add_size(size, mul_size(slotSize, slotCount));

	return MAXALIGN(size);
}

void CreateReadWriteSetArray(void)
{
	bool found;
	int i;
	char *keySet_base;

	readWriteSetArray = (ReadWriteSetStruct *)
		ShmemInitStruct("Shared Read/Write Set Buffer", ReadWriteSetShmemSize(), &found);

	Assert(slotCount != 0);

	if (!found)
	{
		/*
		 * We're the first - initialize.
		 */
		readWriteSetArray->numSlots = 0;
		readWriteSetArray->maxSlots = slotCount;
		readWriteSetArray->nextSlot = 0;

		readWriteSetArray->slots = (ReadWriteSetSlot *)&readWriteSetArray->keySet;

		keySet_base = (char *)&(readWriteSetArray->slots[readWriteSetArray->maxSlots]);

		for (i = 0; i < readWriteSetArray->maxSlots; i++)
		{
			ReadWriteSetSlot *tmpSlot = &readWriteSetArray->slots[i];

			tmpSlot->slotid = -1;
			tmpSlot->slotindex = i;
			tmpSlot->lower = 0;
			tmpSlot->upper = (uint32)-1;
			tmpSlot->gxid = 0;
			tmpSlot->status = PENDING;
			
			HASHCTL info;
			int hash_flags;
			info.keysize = SHMEM_KEYXIDS_KEYSIZE;
			info.match = memcmp;
			info.hash = tag_hash;
			info.keycopy = memcpy;
			hash_flags = HASH_ELEM | HASH_COMPARE;
			hash_flags |= HASH_FUNCTION | HASH_KEYCOPY;
			info.entrysize = sizeof(ShmemReadWriteSetKey);
			
			char hashname[30];
			sprintf(hashname,"ShmemHashReadSet%d",i);
			tmpSlot->readSet = ShmemInitHash(hashname,
								 SHMEM_KEYXIDS_INIT_SIZE, SHMEM_READ_SET_SIZE,
								 &info, hash_flags);

			sprintf(hashname,"ShmemHashWriteSet%d",i);
			tmpSlot->writeSet = ShmemInitHash(hashname,
								 SHMEM_KEYXIDS_INIT_SIZE, SHMEM_WRITE_SET_SIZE,
								 &info, hash_flags);
		}
	}
}

char *
ReadWriteSetDump(void)
{
	StringInfoData str;
	volatile ReadWriteSetStruct *arrayP = readWriteSetArray;
	initStringInfo(&str);

	appendStringInfo(&str, "ReadWriteSet Slot Dump: currSlots: %d maxSlots: %d ",
					 arrayP->numSlots, arrayP->maxSlots);

	/* TODO: missing some detail infomation */

	return str.data;
}

void ReadWriteSetSlotAdd(char *creatorDescription, int32 slotId)
{
	ReadWriteSetSlot *slot;
	volatile ReadWriteSetStruct *arrayP = readWriteSetArray;
	int nextSlot = -1;
	int i;
	int retryCount = 10; /* TODO: set in cdb/cdbvars.c */

	LWLockAcquire(ReadWriteSetArrayLock, LW_EXCLUSIVE);
	slot = NULL;
	for (i = 0; i < arrayP->maxSlots; i++)
	{
		ReadWriteSetSlot *testSlot = &arrayP->slots[i];

		if (testSlot->slotindex > arrayP->maxSlots)
			elog(ERROR, "Read Write Set Array appears corrupted: %s", ReadWriteSetDump());

		if (testSlot->slotid == slotId)
		{
			slot = testSlot;
			break;
		}
	}

	if (slot != NULL)
	{
		LWLockRelease(ReadWriteSetArrayLock);
		elog(DEBUG1, "ReadWriteSetSlotAdd: found existing entry for our session-id. id %d retry %d pid %u", slotId, retryCount, (int)slot->pid);
		return;
	}

	if (arrayP->numSlots >= arrayP->maxSlots || arrayP->nextSlot == -1)
	{
		LWLockRelease(ReadWriteSetArrayLock);
		/*
		 * Ooops, no room.  this shouldn't happen as something else should have
		 * complained if we go over MaxBackends.
		 */
		ereport(FATAL,
				(errcode(ERRCODE_TOO_MANY_CONNECTIONS),
				 errmsg("sorry, too many clients already."),
				 errdetail("There are no more available slots in the readWriteSetArray."),
				 errhint("Another piece of code should have detected that we have too many clients."
						 " this probably means that someone isn't releasing their slot properly.")));
	}

	slot = &arrayP->slots[arrayP->nextSlot];

	slot->slotindex = arrayP->nextSlot;

	/*
	 * find the next available slot
	 */
	for (i = arrayP->nextSlot + 1; i < arrayP->maxSlots; i++)
	{
		ReadWriteSetSlot *tmpSlot = &arrayP->slots[i];

		if (tmpSlot->slotid == -1)
		{
			nextSlot = i;
			break;
		}
	}

	/* mark that there isn't a nextSlot if the above loop didn't find one */
	if (nextSlot == arrayP->nextSlot)
		arrayP->nextSlot = -1;
	else
		arrayP->nextSlot = nextSlot;

	arrayP->numSlots += 1;

	/* initialize some things */
	slot->slotid = slotId;
	slot->gxid = 0;
	slot->pid = MyProcPid;
	slot->upper = (uint32)-1;
	slot->lower = 0;
	slot->status = RUNNING;

	if (slot == NULL)
	{
		ereport(ERROR,
				(errmsg("%s could not set the ReadWriteSet Slot!",
						creatorDescription),
				 errdetail("Tried to set the read/write set slot with id: %d "
						   "and failed. ReadWriteSet dump: %s",
						   slotId,
						   ReadWriteSetDump())));
	}

	CurrentReadWriteSetSlot = slot;

	elog((Debug_print_full_dtm ? LOG : DEBUG5), "%s added ReadWriteSet slot for gp_session_id = %d (address %p)",
		 creatorDescription, slotId, CurrentReadWriteSetSlot);

	LWLockRelease(ReadWriteSetArrayLock);
}

static char*
MakeLTSKeyFromRWSet(ShmemReadWriteSetKey* key, Size* bufferlen)
{
	TupleKeySlice ltskey = {(TupleKey)key->key, key->keyLen};

	char* data = palloc0(SHMEM_KEYXIDS_KEYSIZE);
	memcpy(data, key->key, SHMEM_KEYXIDS_KEYSIZE);
	if (ltskey.data->type != 'l')
	{
		*bufferlen = key->keyLen;
		return data;
	}
	char *tmp = data + key->keyLen;
	*tmp = 'l';
	tmp ++;
	uint32 *lts = (uint32*)tmp;
	*lts = htonl(key->lts);
	tmp += 4;
	*tmp = 'l';
	*bufferlen = key->keyLen + 6;
	return data;
}

bool DTAScanCallBack(Size len, char *data, TupleKeySlice* newkey, TupleValueSlice* newvalue)
{
	TupleKeySlice tempkey = {(TupleKey)data, len};
	if (!IsLTSKey(tempkey))
		return false;
	if (IsPartLTSKey(tempkey))
		return false;
	volatile ReadWriteSetSlot *rwset = CurrentReadWriteSetSlot;
	Size rawlen = 0;
	TupleKey rawkey = (TupleKey)get_TupleKeySlice_primarykey_prefix_lts(tempkey, &rawlen);
	uint32 CurrentLts = get_lts_suffix(tempkey);
	char *rts_data = palloc0(SHMEM_KEYXIDS_KEYSIZE);
	memcpy(rts_data, rawkey, rawlen);

	ShmemReadWriteSetKey *readkey;
	bool found;
	readkey = (ShmemReadWriteSetKey *)hash_search(rwset->readSet, 
												  rts_data, HASH_FIND, &found);
	if (!readkey)
		return false;
	/* in this case, current key we just read is the key in the read set */
	if (CurrentLts == readkey->lts)
		return false;
	Size readsetkeylen = 0;
	char* readsetkeybuffer = MakeLTSKeyFromRWSet(readkey, &readsetkeylen);
	Assert(readsetkeybuffer != NULL);
	TupleKeySlice readsetkey = {(TupleKey)readsetkeybuffer, readsetkeylen};

	int retry = 0;
	for (retry = 0; retry < 10 && newvalue->len == 0; retry ++)
	{
		GetResponse *res = kvengine_send_get_req(readsetkey);
		*newvalue = get_tuple_value_from_buffer(res->value);
	}
	if (newvalue->len == 0)
		return false;

	newkey->data = palloc0(readsetkeylen);
	memcpy(newkey->data, readsetkeybuffer, readsetkeylen);
	newkey->len = readsetkeylen;
	pfree(readsetkeybuffer);
	pfree(rts_data);
	return true;
}

void AppendKeyToReadWriteSet(Size len, char *data, CmdType type)
{
	if (!CurrentReadWriteSetSlot || !type || len == 0)
		return;

	TupleKeySlice tempkey = {(TupleKey)data, len};
	/* part lts key do not save into rwset*/
	if (IsPartLTSKey(tempkey))
		return;
	volatile ReadWriteSetSlot *rwset = CurrentReadWriteSetSlot;

	if (!rwset->gxid)
		rwset->gxid = getDistributedTransactionId();

	Size rawlen = 0;
	TupleKey rawkey = (TupleKey)get_TupleKeySlice_primarykey_prefix_lts(tempkey, &rawlen);

	char *rts_data = palloc0(SHMEM_KEYXIDS_KEYSIZE);
	memcpy(rts_data, rawkey, rawlen);

	if (type == CMD_SELECT)
	{
		Assert(len <= SHMEM_KEYXIDS_KEYSIZE);
		ShmemReadWriteSetKey *readkey;
		bool found;
		readkey = (ShmemReadWriteSetKey *)hash_search(CurrentReadWriteSetSlot->readSet, 
													  rts_data, HASH_ENTER_NULL, &found);
		if (readkey != NULL)
		{
			if (!found)
			{
				memcpy(readkey->key, rawkey, rawlen);
				readkey->keyLen = rawlen;
				uint32 lts = get_lts_suffix(tempkey);
				if (IsLTSKey(tempkey))
					readkey->lts = lts;
				else 
					readkey->lts = (uint32)-1;
			}
		}
	}
	else if (type == CMD_UPDATE)
	{
		Assert(len <= SHMEM_KEYXIDS_KEYSIZE);
		ShmemReadWriteSetKey *readkey;
		bool found;
		readkey = (ShmemReadWriteSetKey *)hash_search(CurrentReadWriteSetSlot->readSet, 
													  rts_data, HASH_ENTER_NULL, &found);
		if (readkey != NULL)
		{
			if (!found)
			{
				memcpy(readkey->key, rawkey, rawlen);
				readkey->keyLen = rawlen;
				uint32 lts = get_lts_suffix(tempkey);
				readkey->lts = lts;
			}
		}
		Assert(len <= SHMEM_KEYXIDS_KEYSIZE);
		ShmemReadWriteSetKey *writekey;
		writekey = (ShmemReadWriteSetKey *)hash_search(CurrentReadWriteSetSlot->writeSet, 
													   rts_data, HASH_ENTER_NULL, &found);
		if (writekey != NULL)
		{
			if (!found)
			{
				memcpy(writekey->key, rawkey, rawlen);
				writekey->keyLen = rawlen;
				uint32 lts = get_lts_suffix(tempkey);
				writekey->lts = lts;
			}
		}
	}
}

void ReadWriteSetSlotRemove(char *creatorDescription)
{
	if (!CurrentReadWriteSetSlot)
		return;

	volatile ReadWriteSetSlot *slot = CurrentReadWriteSetSlot;
	int slotId = slot->slotid;

	if (transam_mode == TRANSAM_MODE_BOCC)
	{
		ShmemFinishedGxidEnt *result;
		bool found;
		LWLockAcquire(FinishedGxidsLock, LW_EXCLUSIVE);
		if (slot->gxid != 0)
		{
			// TODO: replace 'ShmemFinishedGxids' with queue
			DistributedTransactionId gxid = slot->gxid;
			result = (ShmemFinishedGxidEnt *)hash_search(ShmemFinishedGxids, &gxid, HASH_ENTER_NULL, &found);
			if (result)
			{
				if (!found)
				{
					result->gxid = slot->gxid;
					HASH_SEQ_STATUS status; 

					hash_seq_init(&status, slot->writeSet);
					ShmemReadWriteSetKey *writekey;
					result->writeSetCount = 0;
					result->writeSetLen = 0;
					while ((writekey = (ShmemReadWriteSetKey*)hash_seq_search(&status)) != NULL)
					{
						Size len = 0;
						char *key_data = MakeLTSKeyFromRWSet(writekey, &len);
						memcpy(result->writeSet + result->writeSetLen, &len, sizeof(Size));
						result->writeSetLen += sizeof(Size);
						memcpy(result->writeSet + result->writeSetLen, key_data, len);
						result->writeSetCount ++;
						result->writeSetLen += len;
					}
				}
			}
			else
			{
				elog(DEBUG5, "No space in shared memory now. Please restart the server");
				LWLockRelease(FinishedGxidsLock);
			}
		}

		DistributedTransactionId current_min = (uint32)-1;
		for (int i = 0; i < readWriteSetArray->numSlots; i++)
		{
			ReadWriteSetSlot *tmpSlot = &readWriteSetArray->slots[i];
			if (current_min > tmpSlot->gxid)
				current_min = tmpSlot->gxid;
		}

		HASH_SEQ_STATUS status; 
		hash_seq_init(&status, ShmemFinishedGxids);
		ShmemFinishedGxidEnt *finishgxid;
		while ((finishgxid = (ShmemFinishedGxidEnt*)hash_seq_search(&status)) != NULL)
		{
			if (finishgxid->gxid < current_min)
			{
				bool found;
				DistributedTransactionId removegxid = finishgxid->gxid;
				hash_search(ShmemFinishedGxids, &removegxid, HASH_REMOVE, &found);
			}
		}
		LWLockRelease(FinishedGxidsLock);
	}

	LWLockAcquire(ReadWriteSetArrayLock, LW_EXCLUSIVE);
	/**
	 * Determine if we need to modify the next available slot to use. 
	 * We only do this is our slotindex is lower then the existing one.
	 */
	if (readWriteSetArray->nextSlot == -1 || slot->slotindex < readWriteSetArray->nextSlot)
	{
		if (slot->slotindex > readWriteSetArray->maxSlots)
			elog(ERROR, "Read Write set slot has a bogus slotindex: %d. slot array dump: %s",
				 slot->slotindex, ReadWriteSetDump());

		readWriteSetArray->nextSlot = slot->slotindex;
	}

	/* reset the slotid which marks it as being unused. */
	slot->slotid = -1;
	slot->gxid = 0;
	slot->pid = 0;
	slot->lower = 0;
	slot->upper = (uint32)-1;
	slot->status = PENDING;
	clean_up_hash(slot->readSet);
	clean_up_hash(slot->writeSet);
	readWriteSetArray->numSlots -= 1;

	LWLockRelease(ReadWriteSetArrayLock);

	elog((Debug_print_full_dtm ? LOG : DEBUG5), "ReadWriteSetSlot removed slot for slotId = %d, creator = %s (address %p)",
		 slotId, creatorDescription, CurrentReadWriteSetSlot);
}

/*
 * Backward validation.
 * Follow the definition in "On Optimistic Methods for Concurrency Control(1981)".
 * The read set of the validating transaction(Tv) cannot intersects with
 * the write set of all transaction that is committed in [Tv.start_time+1, Tv.validate_time]
 */
int BoccValidation(void)
{
	if (!CurrentReadWriteSetSlot)
		return 1;

	volatile ReadWriteSetSlot *rwset = CurrentReadWriteSetSlot;

	DistributedTransactionId current_gxid = generateGID();

	LWLockAcquire(ReadWriteSetArrayLock, LW_EXCLUSIVE);
	// TODO: Replace the hash table named as 'ShmemFinishedGxids' with queue
	HASH_SEQ_STATUS status; 
	hash_seq_init(&status, ShmemFinishedGxids);
	ShmemFinishedGxidEnt *result;
	while ((result = (ShmemFinishedGxidEnt*)hash_seq_search(&status)) != NULL)
	{
		if (result->gxid >= current_gxid)
			continue;
		if (result->gxid <= rwset->gxid)
			continue;

		Size inner_offset = 0;
		for (int k = 0; k < result->writeSetCount; k++)
		{
			Size *inner_len = (Size *)(result->writeSet + inner_offset);
			inner_offset += sizeof(Size);

			TupleKeySlice innerkey = {(TupleKey)(result->writeSet + inner_offset), *inner_len};
			Size rawlen = 0;
			TupleKey rawkey = (TupleKey)get_TupleKeySlice_primarykey_prefix_lts(innerkey, &rawlen);
			char *rts_data = palloc0(SHMEM_KEYXIDS_KEYSIZE);
			memcpy(rts_data, rawkey, rawlen);

			ShmemReadWriteSetKey *readkey;
			bool found;
			readkey = (ShmemReadWriteSetKey *)hash_search(rwset->readSet, 
													  rts_data, HASH_FIND, &found);
			pfree(rts_data);
			if (readkey != NULL && found)
			{
				LWLockRelease(ReadWriteSetArrayLock);
				return 0;
			}
			inner_offset += *inner_len;
		}

	}
	LWLockRelease(ReadWriteSetArrayLock);
	rwset->val_gxid = current_gxid;
	return 1;
}

/*
 * Forward validation.
 * Follow the definition in .
 * Check write set of Tv does not overlap with read sets of all active Transactions.
 */
int FoccValidation(void)
{
	if (!CurrentReadWriteSetSlot)
		return 1;

	volatile ReadWriteSetSlot *rwset = CurrentReadWriteSetSlot;

	LWLockAcquire(ReadWriteSetArrayLock, LW_EXCLUSIVE);

	for (int i = 0; i < readWriteSetArray->numSlots; i++)
	{
		ReadWriteSetSlot *tmpSlot = &readWriteSetArray->slots[i];

		if (tmpSlot->slotid == CurrentReadWriteSetSlot->slotid)
			continue;

		HASH_SEQ_STATUS status; 
		hash_seq_init(&status, rwset->writeSet);
		ShmemReadWriteSetKey *writekey;
		while ((writekey = (ShmemReadWriteSetKey*)hash_seq_search(&status)) != NULL)
		{
			ShmemReadWriteSetKey *readkey;
			bool found;
			readkey = (ShmemReadWriteSetKey *)hash_search(tmpSlot->readSet, 
													  writekey->key, HASH_FIND, &found);
			if (readkey != NULL && found)
			{
				LWLockRelease(ReadWriteSetArrayLock);
				return 0;
			}
		}
	}
	LWLockRelease(ReadWriteSetArrayLock);

	return 1;
}

Size KeyXidsHashTableShmemSize(void)
{
	Size size = 0;
	size = add_size(size, hash_estimate_size(SHMEM_KEYXIDS_SIZE,
											 sizeof(ShmemXidsEnt)));
	size = add_size(size, hash_estimate_size(SHMEM_KEYXIDS_SIZE,
											 sizeof(ShmemRtsEnt)));
	size = add_size(size, hash_estimate_size(SHMEM_KEYXIDS_SIZE,
											 sizeof(ShmemFinishedGxidEnt)));
	return MAXALIGN(size);
}

/**
 * For the read/write txn list in dynamic timestamp allocation occ function
 */
void InitKeyXidsHashTable(void)
{
	HASHCTL info;
	int hash_flags;

	info.keysize = SHMEM_KEYXIDS_KEYSIZE;
	info.entrysize = sizeof(ShmemXidsEnt);
	info.match = memcmp;
	info.hash = tag_hash;
	info.keycopy = memcpy;
	hash_flags = HASH_ELEM | HASH_COMPARE;
	hash_flags |= HASH_FUNCTION | HASH_KEYCOPY;
	ShmemKeyXids = ShmemInitHash("ShmemKeyXidsHashTable",
								 SHMEM_KEYXIDS_INIT_SIZE, SHMEM_KEYXIDS_SIZE,
								 &info, hash_flags);

	info.entrysize = sizeof(ShmemRtsEnt);
	ShmemRts = ShmemInitHash("ShmemKeyRtsHashTable",
							 SHMEM_KEYXIDS_INIT_SIZE, SHMEM_KEYXIDS_SIZE,
							 &info, hash_flags);

	HASHCTL gxidinfo;
	int gxid_hash_flags = HASH_ELEM;
	gxid_hash_flags |= HASH_FUNCTION;
	gxidinfo.hash = oid_hash;
	gxidinfo.keysize = sizeof(DistributedTransactionId);
	gxidinfo.entrysize = sizeof(ShmemFinishedGxidEnt);
	ShmemFinishedGxids = ShmemInitHash("ShmemFinishedGxidsHashTable",
									   SHMEM_KEYXIDS_INIT_SIZE, SHMEM_KEYXIDS_SIZE,
									   &gxidinfo, gxid_hash_flags);
}

void AppendReadXidWithKey(Size len, char *data, CmdType type)
{
	if (!ShmemKeyXids || !ShmemRts || !type || len == 0)
		return;

	char *key_data = palloc0(SHMEM_KEYXIDS_KEYSIZE);
	memcpy(key_data, data, len);

	TupleKeySlice tempkey = {(TupleKey)data, len};
	Size rts_len = 0;
	TupleKey key = (TupleKey)get_TupleKeySlice_primarykey_prefix_lts(tempkey, &rts_len);
	char *rts_data = palloc0(SHMEM_KEYXIDS_KEYSIZE);
	memcpy(rts_data, key, rts_len);
	if (type == CMD_SELECT || type == CMD_UPDATE)
	{
		bool found;

		LWLockAcquire(RtsHashTableLock, LW_EXCLUSIVE);
		/**
		 * update rts of this key
		 */
		ShmemRtsEnt *rts_result;
		rts_result = (ShmemRtsEnt *)hash_search(ShmemRts, rts_data, HASH_ENTER_NULL, &found);

		if (rts_result != NULL)
		{
			if (!found)
			{
				memcpy(rts_result->key, key, rts_len);
				rts_result->keyLen = rts_len;
				uint32 lts = get_lts_suffix(tempkey);
				rts_result->rts = lts;
			}
		}
		else
		{
			LWLockRelease(RtsHashTableLock);
			return;
		}
		LWLockRelease(RtsHashTableLock);

		LWLockAcquire(KeyXidsHashTableLock, LW_EXCLUSIVE);
		/**
		 * append current gxid into read txn of this key
		 */
		ShmemXidsEnt *result;
		result = (ShmemXidsEnt *)hash_search(ShmemKeyXids, key_data, HASH_ENTER_NULL, &found);

		if (result != NULL)
		{
			if (!found)
			{
				memcpy(result->key, key_data, len);
				result->keyLen = len;
				result->ptr = 0;
				result->writeTxn = 0;
			}

			volatile ReadWriteSetSlot *rwset = CurrentReadWriteSetSlot;

			if (!rwset->gxid)
				rwset->gxid = getDistributedTransactionId();

			if (result->ptr < SHMEM_READXIDS_SIZE)
			{
				result->readTxn[result->ptr] = rwset->gxid;
				result->ptr++;
			}

			// Adjust the lower > data.wts
			TupleKeySlice tempkey = {(TupleKey)data, len};
			uint32 lts = get_lts_suffix(tempkey);

			if (lts > rwset->lower)
				rwset->lower = lts;
			else if (lts == rwset->lower)
				rwset->lower = lts + 1;

			if (type == CMD_UPDATE && transaction_op_type == TRANSACTION_TYPE_P)
			{
				if (!result->writeTxn || result->writeTxn == rwset->gxid)
					result->writeTxn = rwset->gxid;
			}
			// Adjust the upper of current txn < writetrx.lower
			if (result->writeTxn)
			{
				for (int i = 0; i < readWriteSetArray->numSlots; i++)
				{
					ReadWriteSetSlot *tmpSlot = &readWriteSetArray->slots[i];
					if (result->writeTxn == tmpSlot->gxid)
					{
						if (tmpSlot->lower < rwset->upper)
						{
							rwset->upper = tmpSlot->lower;
							if (rwset->upper == 0)
								core_dump();
						}
						break;
					}
				}
			}
		}
		else
		{
			LWLockRelease(KeyXidsHashTableLock);
			elog(DEBUG5, "No space in shared memory now. Please restart the server");
			return;
		}
		LWLockRelease(KeyXidsHashTableLock);
	}
	pfree(key_data);
	pfree(rts_data);
}

static void
KeyXidsEntRemove(Size len, char *data)
{
	if (!ShmemKeyXids || len == 0)
		return;

	DistributedTransactionId current_min = (uint32)-1;
	LWLockAcquire(ReadWriteSetArrayLock, LW_EXCLUSIVE);
	for (int i = 0; i < readWriteSetArray->numSlots; i++)
	{
		ReadWriteSetSlot *tmpSlot = &readWriteSetArray->slots[i];
		if (current_min > tmpSlot->gxid)
			current_min = tmpSlot->gxid;
	}
	LWLockRelease(ReadWriteSetArrayLock);

	LWLockAcquire(KeyXidsHashTableLock, LW_EXCLUSIVE);

	ShmemXidsEnt *result;
	bool foundPtr;
	result = (ShmemXidsEnt *)
		hash_search(ShmemKeyXids, data, HASH_FIND, &foundPtr);
	if (!result)
	{
		LWLockRelease(KeyXidsHashTableLock);
		return;
	}
	for (int i = 0; i < result->ptr; i++)
	{
		if (CurrentReadWriteSetSlot->gxid == result->readTxn[i] || current_min > result->readTxn[i])
		{
			result->readTxn[i] = result->readTxn[result->ptr - 1];
			result->readTxn[result->ptr - 1] = 0;
			result->ptr--;
			break;
		}
		// if readTxn is validated and committed, we should also clean this xid.
	}
	LWLockRelease(KeyXidsHashTableLock);
}

bool CheckLogicalTsIntervalValid(uint32 lower, uint32 upper)
{
	if (!CurrentReadWriteSetSlot)
		return false;

	volatile ReadWriteSetSlot *rwset = CurrentReadWriteSetSlot;

	if (rwset->upper > upper)
		rwset->upper = upper;

	if (rwset->lower < lower)
		rwset->lower = lower;

	if (rwset->lower < rwset->upper)
		return true;
	else
	{
		rwset->status = ABORTED;
		return false;
	}
}

inline bool
CheckTxnStatus()
{
	return !CurrentReadWriteSetSlot ? true : (CurrentReadWriteSetSlot->status == ABORTED ? false : true);
}

uint32
LogicalCommitTsAllocation()
{
	if (CurrentReadWriteSetSlot->lower < CurrentReadWriteSetSlot->upper)
	{
		if (hash_get_num_entries(CurrentReadWriteSetSlot->writeSet) > 0)
		{
			CurrentReadWriteSetSlot->upper = CurrentReadWriteSetSlot->lower;
		}
		return CurrentReadWriteSetSlot->lower;
	}
	else
		return 0;
}

void AssembleRTS()
{
	if (!CurrentReadWriteSetSlot)
		return;

	uint32 logical_commit_ts = LogicalCommitTsAllocation();
	volatile ReadWriteSetSlot *rwset = CurrentReadWriteSetSlot;

	char *key_data;

	HASH_SEQ_STATUS status; 
	hash_seq_init(&status, rwset->readSet);
	ShmemReadWriteSetKey *readkey;
	while ((readkey = (ShmemReadWriteSetKey*)hash_seq_search(&status)) != NULL)
	{
		ShmemRtsEnt *result;
		bool found;

		LWLockAcquire(RtsHashTableLock, LW_EXCLUSIVE);
		result = (ShmemRtsEnt *)hash_search(ShmemRts, readkey->key, HASH_FIND, &found);

		if (result != NULL)
		{
			if (!found)
			{
				memcpy(result->key, readkey->key, SHMEM_KEYXIDS_KEYSIZE);
				result->keyLen = readkey->keyLen;
				result->rts = readkey->lts;
			}
		}
		else
		{
			LWLockRelease(RtsHashTableLock);

			hash_seq_term(&status);
			return;
		}

		if (result->rts < logical_commit_ts)
			result->rts = logical_commit_ts;
		LWLockRelease(RtsHashTableLock);
		Size len = 0;
		key_data = MakeLTSKeyFromRWSet(readkey, &len);
		KeyXidsEntRemove(len, key_data);
		pfree(key_data);
	}
	
	hash_seq_init(&status, rwset->writeSet);
	ShmemReadWriteSetKey *writekey;
	// reset the wts to 0

	while ((writekey = (ShmemReadWriteSetKey*)hash_seq_search(&status)) != NULL)
	{
		Size len = 0;
		key_data = MakeLTSKeyFromRWSet(writekey, &len);
		ShmemXidsEnt *result;
		bool foundPtr;
		LWLockAcquire(KeyXidsHashTableLock, LW_EXCLUSIVE);
		result = (ShmemXidsEnt *)hash_search(ShmemKeyXids, key_data, HASH_REMOVE, &foundPtr);
		LWLockRelease(KeyXidsHashTableLock);
		pfree(key_data);
	}
	CurrentReadWriteSetSlot->status = COMMITTED;
}

void AbortCleanUp()
{
	if (!CurrentReadWriteSetSlot)
		return;

	// uint32 logical_commit_ts = LogicalCommitTsAllocation();
	volatile ReadWriteSetSlot *rwset = CurrentReadWriteSetSlot;

	char *key_data;

	HASH_SEQ_STATUS status;
	hash_seq_init(&status, rwset->readSet);
	ShmemReadWriteSetKey *readkey;
	while ((readkey = (ShmemReadWriteSetKey*)hash_seq_search(&status)) != NULL)
	{
		Size len = 0;
		key_data = MakeLTSKeyFromRWSet(readkey, &len);
		KeyXidsEntRemove(len, key_data);
		pfree(key_data);
	}

	hash_seq_init(&status, rwset->writeSet);
	ShmemReadWriteSetKey *writekey;
	// reset the wts to 0
	while ((writekey = (ShmemReadWriteSetKey*)hash_seq_search(&status)) != NULL)
	{
		Size len = 0;
		key_data = MakeLTSKeyFromRWSet(writekey, &len);

		ShmemXidsEnt *result;
		bool foundPtr;
		LWLockAcquire(KeyXidsHashTableLock, LW_EXCLUSIVE);
		result = (ShmemXidsEnt *)hash_search(ShmemKeyXids, key_data, HASH_FIND, &foundPtr);
		if (result != NULL)
			result->writeTxn = 0;
		LWLockRelease(KeyXidsHashTableLock);
		pfree(key_data);
	}
	CurrentReadWriteSetSlot->status = ABORTED;
}

int DTAValidation()
{
	if (!CurrentReadWriteSetSlot)
		return 1;

	volatile ReadWriteSetSlot *rwset = CurrentReadWriteSetSlot;
	rwset->status = VALIDATING;

	char *searchkey;

	HASH_SEQ_STATUS status; 
	hash_seq_init(&status, rwset->writeSet);
	ShmemReadWriteSetKey *writekey;
	// reset the wts to 0
	while ((writekey = (ShmemReadWriteSetKey*)hash_seq_search(&status)) != NULL)
	{
		Size len = 0;
		searchkey = MakeLTSKeyFromRWSet(writekey, &len);
		
		ShmemRtsEnt *rts_result;
		bool found = false;
		LWLockAcquire(RtsHashTableLock, LW_EXCLUSIVE);
		rts_result = (ShmemRtsEnt *)hash_search(ShmemRts, writekey->key, HASH_FIND, &found);

		if (!rts_result)
		{
    		hash_seq_term(&status);
			rwset->status = ABORTED;
			pfree(searchkey);
			LWLockRelease(RtsHashTableLock);
			return 0;
		}

		if (rwset->lower < rts_result->rts)
			rwset->lower = rts_result->rts;
		else if (rwset->lower == rts_result->rts)
			rwset->lower = rts_result->rts + 1;
		LWLockRelease(RtsHashTableLock);

		/*
         * Modify the timestamps of other transactions based on the read-write set.
         */
		ShmemXidsEnt *result;
		LWLockAcquire(KeyXidsHashTableLock, LW_EXCLUSIVE);
		result = (ShmemXidsEnt *)hash_search(ShmemKeyXids, searchkey, HASH_FIND, &found);

		if (!found)
		{

    		hash_seq_term(&status);
			rwset->status = ABORTED;
			pfree(searchkey);
			LWLockRelease(KeyXidsHashTableLock);
			return 0;
		}
		// set this key is locked
		if (transaction_op_type == TRANSACTION_TYPE_O &&
			(!result->writeTxn || result->writeTxn == rwset->gxid))
			result->writeTxn = rwset->gxid;
		else
		{

    		hash_seq_term(&status);
			rwset->status = ABORTED;
			pfree(searchkey);
			LWLockRelease(KeyXidsHashTableLock);
			return 0;
		}

		for (int k = 0; k < result->ptr; k++)
		{
			for (int i = 0; i < readWriteSetArray->numSlots; i++)
			{
				ReadWriteSetSlot *tmpSlot = &readWriteSetArray->slots[i];

				if (tmpSlot->gxid == result->readTxn[k])
				{
					if (tmpSlot->status == COMMITTED || tmpSlot->status == VALIDATED)
					{
						if (rwset->lower < tmpSlot->upper)
							rwset->lower = tmpSlot->upper;
					}
					else if (tmpSlot->status == RUNNING)
					{
						if (rwset->lower < tmpSlot->upper)
						{
							if (rwset->lower <= tmpSlot->lower)
							{
								if ((tmpSlot->lower + 1) < tmpSlot->upper)
								{
									rwset->lower = tmpSlot->lower + 1;
									tmpSlot->upper = rwset->lower;
									if (tmpSlot->upper == 0)
										core_dump();
								}
							}
							else
							{
								tmpSlot->upper = rwset->lower;
								if (tmpSlot->upper == 0)
									core_dump();
							}
						}
					}
					break;
				}
			}
		}
		LWLockRelease(KeyXidsHashTableLock);
	}

	if (rwset->lower < rwset->upper)
	{
		rwset->status = VALIDATED;
		return 1;
	}
	else
	{
		rwset->status = ABORTED;
		return 0;
	}
}

/* Request a piece of space in static shared memory to hold all the statistics of the current node. */
void TransactionStatisticsShmemInit(void)
{
	bool found;
	transaction_statistics = (TransactionStatistics *)
		ShmemInitStruct("Transaction Statistics", sizeof(TransactionStatistics), &found);

	if (!IsUnderPostmaster)
	{
		Assert(!found);
		transaction_statistics->allTransactionNum = 0;
		transaction_statistics->RollBackTransactionNum = 0;
		transaction_statistics->CommitTransactionNum = 0;
		switch (dta_rollback_count)
		{
		case ROLLBACKCOUNT_10000:
			transaction_statistics->Loop = 10000;
			break;
		case ROLLBACKCOUNT_100000:
			transaction_statistics->Loop = 100000;
			break;
		case ROLLBACKCOUNT_1000000:
			transaction_statistics->Loop = 1000000;
			break;
		case ROLLBACKCOUNT_10000000:
			transaction_statistics->Loop = 10000000;
			break;
		default:
			break;
		}
		SpinLockInit(&transaction_statistics->txn_mutex);
	}
	else
	{
		Assert(found);
	}
}

Size TransactionStatisticsShmemSize(void)
{
	return sizeof(TransactionStatistics);
}

void RollBackCountAdd(void)
{
	transaction_statistics->RollBackTransactionNum++;
}

void CommitTranCountAdd(void)
{
	transaction_statistics->CommitTransactionNum++;
}

void AllTransCountAdd(void)
{
	transaction_statistics->allTransactionNum++;
	SpinLockAcquire(&transaction_statistics->txn_mutex);
	if (transaction_statistics->allTransactionNum >= transaction_statistics->Loop)
	{
		ereport(WARNING,
			(errmsg("RUCC ROLLBACK: current %d transaction, roll back %d, commit %d, rollback rate %lf",
					transaction_statistics->allTransactionNum, 
					transaction_statistics->RollBackTransactionNum, 
					transaction_statistics->CommitTransactionNum,
					(double)transaction_statistics->RollBackTransactionNum / (double)transaction_statistics->allTransactionNum)));
		transaction_statistics->allTransactionNum = 0;
		transaction_statistics->RollBackTransactionNum = 0;
		transaction_statistics->CommitTransactionNum = 0;
		switch (dta_rollback_count)
		{
		case ROLLBACKCOUNT_10000:
			transaction_statistics->Loop = 10000;
			break;
		case ROLLBACKCOUNT_100000:
			transaction_statistics->Loop = 100000;
			break;
		case ROLLBACKCOUNT_1000000:
			transaction_statistics->Loop = 1000000;
			break;
		case ROLLBACKCOUNT_10000000:
			transaction_statistics->Loop = 10000000;
			break;
		default:
			break;
		}
	}
	SpinLockRelease(&transaction_statistics->txn_mutex);
}

int GetRollBackCount(void)
{
	return transaction_statistics->RollBackTransactionNum;
}