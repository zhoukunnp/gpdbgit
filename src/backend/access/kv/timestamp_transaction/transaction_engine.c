/*-------------------------------------------------------------------------
 *
 * transaction_engine.h
 *	  functions for rocksdb transaction_engine engine.
 *
 *
 * Portions Copyright (c) 2019-Present, TDSQL
 *
 * src/backend/tdb/rocks_engine.c
 *
 *-------------------------------------------------------------------------
 */
#include <stdlib.h>
#include <unistd.h>

#include "postgres.h"
#include "tdb/kvengine.h"
#include "tdb/rocks_engine.h"
#include "tdb/storage_param.h"
#include "rocksdb/c.h"

#include "catalog/pg_type.h"
#include "catalog/pg_proc.h"
#include "catalog/gp_fastsequence.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "utils/builtins.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "tdb/rocks_engine.h"
#include "tdb/timestamp_transaction/http.h"
#include "tdb/storage_param.h"

static rocksdb_optimistictransactiondb_t* open_opt_db(OptTransDBEngine *rocks_engine);
static void rocks_init_optengine_interface(KVEngineInterface* interface);
static void rocks_transaction_init_txn_interface(KVEngineTransactionInterface* interface);

static TupleValueSlice rocks_transaction_get(KVEngineTransactionInterface* interface, 
                                             TupleKeySlice key, int cf_name);
static void rocks_transaction_put(KVEngineTransactionInterface* interface, TupleKeySlice key, 
                                  TupleValueSlice value, int cf_name);
static void rocks_transaction_delete(KVEngineTransactionInterface* interface, 
                                     TupleKeySlice key, int cf_name);
static bool rocks_transaction_commit_and_destroy(KVEngineTransactionInterface* interface, 
                                                 DistributedTransactionId gxid, 
                                                 uint64 commit_ts);
static bool rocks_transaction_commit_with_lts_and_destroy(KVEngineTransactionInterface* interface, 
                                    DistributedTransactionId gxid, 
                                    uint64 commit_ts, 
                                    uint32 lts);
static void rocks_transaction_abort_and_destroy(KVEngineTransactionInterface* interface, 
                                    DistributedTransactionId gxid);
static void rocks_transaction_destroy(
                                    KVEngineTransactionInterface* interface, 
                                    DistributedTransactionId gxid);
static KVEngineTransactionInterface* open_txnengine_txn(KVEngineInterface* rocks_engine, 
                                    DistributedTransactionId gxid, 
                                    bool flag, uint64 start_ts);
static TupleValueSlice rocks_transaction_get_for_update(KVEngineTransactionInterface* interface, 
                                    TupleKeySlice key);
static void rocks_optengine_destroy(KVEngineInterface* interface);

KVEngineInterface*
rocks_create_optimistic_transactions_engine()
{
	OptTransDBEngine *rocks_engine = palloc0(sizeof(*rocks_engine));
	rocks_engine->engine.PRIVATE_opt = create_and_init_options();
	rocks_engine->otxn_db = open_opt_db(rocks_engine);
    rocks_engine->otxn_options = rocksdb_optimistictransaction_options_create();
	rocks_engine->engine.PRIVATE_be = open_backup_engine(rocks_engine->engine.PRIVATE_opt);
	rocks_init_optengine_interface((KVEngineInterface*) rocks_engine);
	return (KVEngineInterface*) rocks_engine;
}

static void 
insert_name(OptTransDBEngine *rocks_engine, int index, char* name, uint64_t name_len)
{
    rocks_engine->cf_name[index] = palloc(name_len + 1);
    strcpy(rocks_engine->cf_name[index], name);
}

static rocksdb_optimistictransactiondb_t*
open_opt_db(OptTransDBEngine *rocks_engine)
{
    rocksdb_options_t* options = rocks_engine->engine.PRIVATE_opt;
    rocks_engine->cf_num = 3;
    insert_name(rocks_engine, 0, ROCKS_DEFAULT_CF, strlen(ROCKS_DEFAULT_CF));
    insert_name(rocks_engine, 1, ROCKS_LTS_CF, strlen(ROCKS_LTS_CF));
    insert_name(rocks_engine, 2, RocksDBSystemCatalog, strlen(RocksDBSystemCatalog));

	const rocksdb_options_t* cf_options[3] = {options, options, options};

	char *err = NULL;
	rocksdb_optimistictransactiondb_t *db = 
            rocksdb_optimistictransactiondb_open_column_families
            (options, RocksDBPath, 3, rocks_engine->cf_name, cf_options, rocks_engine->cf_handle, &err);
	if (err)
	{
		ereport((ERROR),(errmsg_internal("engine open failed, (err message:%s)",
					err)));
	}
	return db;
}

static void
rocks_init_optengine_interface(KVEngineInterface* interface)
{
	interface->destroy = rocks_optengine_destroy;
    interface->create_txn = open_txnengine_txn;
}

static void
rocks_optengine_destroy(KVEngineInterface* interface)
{
	OptTransDBEngine *rocks_engine = (OptTransDBEngine*) interface;
	char *err = NULL;
	rocksdb_backup_engine_create_new_backup(rocks_engine->engine.PRIVATE_be,
                                    rocks_engine->otxn_db, &err);
	if (err)
		ereport(ERROR,
			(errmsg("Rocksdb: destory exec failed, err = %s",
					err)));
    rocksdb_compact_range(rocks_engine->otxn_db, NULL, 0, NULL, 0);
    rocksdb_options_destroy(rocks_engine->engine.PRIVATE_opt);
	rocksdb_backup_engine_close(rocks_engine->engine.PRIVATE_be);
    rocksdb_optimistictransaction_options_destroy(rocks_engine->otxn_options);
    rocksdb_column_family_handle_destroy(rocks_engine->cf_handle[0]);
    rocksdb_column_family_handle_destroy(rocks_engine->cf_handle[1]);
    rocksdb_column_family_handle_destroy(rocks_engine->cf_handle[2]);
	rocksdb_close(rocks_engine->otxn_db);
	pfree(rocks_engine);
    rocks_engine = NULL;
}

static KVEngineTransactionInterface*
open_txnengine_txn(KVEngineInterface* rocks_engine, DistributedTransactionId gxid,
                   bool flag, uint64 start_ts)
{
    bool found;
	if (flag)
	{
		ADD_THREAD_LOCK_EXEC(TxnHTAB);
		TXNEngineEntry *rocks_batch_entry = hash_search(transaction_env_htab, (void*) &gxid, HASH_ENTER_NULL, &found);
		
		if (!found)
		{
			MemSet(&rocks_batch_entry->val, 0, sizeof(rocks_batch_entry->val));
			SpinLockInit(&rocks_batch_entry->val.mutex);
            OptTransDBEngine* engine = (OptTransDBEngine*)rocks_engine;
            rocksdb_writeoptions_t *woptions = rocksdb_writeoptions_create();
			rocksdb_transaction_t* txn_cf = rocksdb_optimistictransaction_begin(
                                engine->otxn_db, woptions, engine->otxn_options, NULL);
            rocks_batch_entry->val.PRIVATE_txn = txn_cf;
			rocks_batch_entry->val.start_ts = start_ts;	
            for (int i = 0; i < 10; i++)
            {
                rocks_batch_entry->val.cf_handle[i] = engine->cf_handle[i];
            }
            rocks_batch_entry->val.cf_name = engine->cf_name;
            rocks_batch_entry->val.cf_num = engine->cf_num;

			rocks_transaction_init_txn_interface((KVEngineTransactionInterface*) &rocks_batch_entry->val);
			rocksdb_writeoptions_destroy(woptions);
		}
		else if (!rocks_batch_entry)
        {
            core_dump();
        }
		REMOVE_THREAD_LOCK_EXEC(TxnHTAB);
		SpinLockAcquire(&rocks_batch_entry->val.mutex);
		return (KVEngineTransactionInterface*) &rocks_batch_entry->val;	
	}
	else
	{
		ADD_THREAD_LOCK_EXEC(TxnHTAB);
		TXNEngineEntry *rocks_batch_entry = hash_search(transaction_env_htab, (void*) &gxid, HASH_FIND, &found);
		REMOVE_THREAD_LOCK_EXEC(TxnHTAB);

		if (!found)
			return NULL;
		else
		{
			SpinLockAcquire(&rocks_batch_entry->val.mutex);
			return (KVEngineTransactionInterface*) &rocks_batch_entry->val;
		}
	}
}

void
release_txnengine_mutex(KVEngineTransactionInterface* interface)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;
	SpinLockRelease(&txn_engine->mutex);
}

void
rocks_transaction_init_txn_interface(KVEngineTransactionInterface* interface)
{
	interface->abort_and_destroy = rocks_transaction_abort_and_destroy;
	interface->commit_and_destroy = rocks_transaction_commit_and_destroy;
    interface->commit_with_lts_and_destroy = rocks_transaction_commit_with_lts_and_destroy;
    interface->destroy = rocks_transaction_destroy;
	interface->create_iterator = rocks_transaction_create_iterator;
	interface->get = rocks_transaction_get;
	interface->put = rocks_transaction_put;
	interface->delete = rocks_transaction_delete;
	interface->get_for_update = rocks_transaction_get_for_update;
}

KVEngineIteratorInterface*
rocks_transaction_create_iterator(KVEngineTransactionInterface* interface, 
                                  bool isforward, int cf_name)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;
	RocksEngineIterator *rocks_it = palloc0(sizeof(*rocks_it));
	rocks_it->PRIVATE_readopt = rocksdb_readoptions_create();
	rocksdb_readoptions_set_sequence(rocks_it->PRIVATE_readopt, 
                                     txn_engine->start_ts);

    rocksdb_column_family_handle_t* cfh = txn_engine->cf_handle[cf_name];

    if (cfh == NULL)
	{
        return NULL;
	}

	rocks_it->PRIVATE_it = 
                rocksdb_transaction_create_iterator_cf(txn_engine->PRIVATE_txn, 
                                                rocks_it->PRIVATE_readopt, cfh);

	if (isforward)
		rocks_engine_init_iterator_interface((KVEngineIteratorInterface*) rocks_it);
	else
		rocks_engine_init_iterator_interface_pre((KVEngineIteratorInterface*) rocks_it);
    
	return (KVEngineIteratorInterface*) rocks_it;
}

TupleValueSlice
rocks_transaction_get(KVEngineTransactionInterface* interface, 
                      TupleKeySlice key, int cf_name)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;
	TupleValueSlice value = {NULL, 0};
	rocksdb_readoptions_t *readopt = rocksdb_readoptions_create();
	rocksdb_readoptions_set_sequence(readopt, txn_engine->start_ts);
	char *err = NULL;
    rocksdb_column_family_handle_t** cff = txn_engine->cf_handle;
    rocksdb_column_family_handle_t* cfh = cff[cf_name];

    Assert(cfh != NULL);

	value.data = (TupleValue) 
                  rocksdb_transaction_get_cf(txn_engine->PRIVATE_txn, readopt, cfh, 
                                        (char*) key.data, key.len, &value.len, &err);

	if (err)
		ereport(ERROR,
			(errmsg("Rocksdb: get exec failed, err = %s", err)));
	rocksdb_readoptions_destroy(readopt);
	return value;
}

TupleValueSlice
rocks_transaction_get_for_update(KVEngineTransactionInterface* interface, TupleKeySlice key)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;
	TupleValueSlice value = {NULL, 0};
	rocksdb_readoptions_t *readopt = rocksdb_readoptions_create();
	rocksdb_readoptions_set_sequence(readopt, txn_engine->start_ts);
	char *err = NULL;

	value.data = (TupleValue) rocksdb_transaction_get_for_update(txn_engine->PRIVATE_txn, readopt, 
                    (char*) key.data, key.len, &value.len, (unsigned char)1, &err);

	if (err)
		ereport(ERROR,
			(errmsg("Rocksdb: get for exec failed, err = %s",
					err)));
	rocksdb_readoptions_destroy(readopt);
	return value;
}

void
rocks_transaction_put(KVEngineTransactionInterface* interface, 
                      TupleKeySlice key, TupleValueSlice value, int cf_name)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;

	char *err = NULL;
    rocksdb_column_family_handle_t** cff = txn_engine->cf_handle;
    rocksdb_column_family_handle_t* cfh = cff[cf_name];

    Assert(cfh != NULL);

	rocksdb_transaction_put_cf(txn_engine->PRIVATE_txn, cfh, (char*) key.data, key.len, 
                                                (char*) value.data, value.len, &err);
	if (err)
		ereport(ERROR,
			(errmsg("Rocksdb: put exec failed, err = %s",
					err)));
}

void
rocks_transaction_delete(KVEngineTransactionInterface* interface, 
                         TupleKeySlice key, int cf_name)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;

	char *err = NULL;
    rocksdb_column_family_handle_t** cff = txn_engine->cf_handle;
    rocksdb_column_family_handle_t* cfh = cff[cf_name];

    Assert(cfh != NULL);
    rocksdb_transaction_delete_cf(txn_engine->PRIVATE_txn, cfh, 
                                  (char*) key.data, key.len, &err);

	if (err)
		ereport(WARNING,
			(errmsg("Rocksdb: delete direct exec failed, err = %s", err)));
}

bool
rocks_transaction_commit_and_destroy(KVEngineTransactionInterface* interface, 
                                     DistributedTransactionId gxid, 
                                     uint64 commit_ts)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;
	char *err = NULL;
	bool success = true;
    rocksdb_transaction_commit_with_ts(txn_engine->PRIVATE_txn, &err, commit_ts);
    if (err)
    {
		success = false;
        int test = 0;
        if (test)
            ereport(WARNING,
			(errmsg("Rocksdb: commit exec failed, err = %s", err)));
    }
    rocksdb_transaction_destroy(txn_engine->PRIVATE_txn);
	release_txnengine_mutex(interface);
	ADD_THREAD_LOCK_EXEC(TxnHTAB);
	hash_search(transaction_env_htab, (void*) &gxid, HASH_REMOVE, NULL);
	REMOVE_THREAD_LOCK_EXEC(TxnHTAB);
	return success;
}

bool
rocks_transaction_commit_with_lts_and_destroy(KVEngineTransactionInterface* interface, 
                                        DistributedTransactionId gxid, 
                                        uint64 commit_ts, uint32 lts)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;
	char *err = NULL;
	bool success = true;
    rocksdb_transaction_commit_with_lts(txn_engine->PRIVATE_txn, &err, commit_ts, lts);
	if (err)
    {
		success = false;
        int test = 0;
        if (test)
            ereport(WARNING,
			(errmsg("Rocksdb: commit lts exec failed, err = %s", err)));
    }
    rocksdb_transaction_destroy(txn_engine->PRIVATE_txn);
	release_txnengine_mutex(interface);
	ADD_THREAD_LOCK_EXEC(TxnHTAB);
	hash_search(transaction_env_htab, (void*) &gxid, HASH_REMOVE, NULL);
	REMOVE_THREAD_LOCK_EXEC(TxnHTAB);
	return success;
}

void
rocks_transaction_abort_and_destroy(KVEngineTransactionInterface* interface, 
                                    DistributedTransactionId gxid)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;
	char *err = NULL;

    rocksdb_transaction_rollback(txn_engine->PRIVATE_txn, &err);
	if (err)
    {
        int test = 0;
        if (test)
            ereport(WARNING,
			(errmsg("Rocksdb: abort exec failed, err = %s", err)));
    }
    rocksdb_transaction_destroy(txn_engine->PRIVATE_txn);
	release_txnengine_mutex(interface);
	ADD_THREAD_LOCK_EXEC(TxnHTAB);
	hash_search(transaction_env_htab, (void*) &gxid, HASH_REMOVE, NULL);
	REMOVE_THREAD_LOCK_EXEC(TxnHTAB);

}

void
rocks_transaction_destroy(KVEngineTransactionInterface* interface, 
                          DistributedTransactionId gxid)
{
	TXNEngine *txn_engine = (TXNEngine*) interface;

    rocksdb_transaction_destroy(txn_engine->PRIVATE_txn);
	release_txnengine_mutex(interface);
	ADD_THREAD_LOCK_EXEC(TxnHTAB);
	hash_search(transaction_env_htab, (void*) &gxid, HASH_REMOVE, NULL);
	REMOVE_THREAD_LOCK_EXEC(TxnHTAB);
}
