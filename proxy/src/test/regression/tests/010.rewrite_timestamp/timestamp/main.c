#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pool.h"
#include "pool_config.h"
#include "utils/pool_relcache.h"
#include "rewrite/pool_timestamp.h"
#include "parser/parser.h"

/* for get_current_timestamp() (MASTER() macro) */
POOL_REQUEST_INFO		_req_info;
POOL_REQUEST_INFO *Req_info = &_req_info;
POOL_CONFIG _pool_config;
POOL_CONFIG *pool_config = &_pool_config;
ProcessType processType;

typedef struct {
	char	*attrname;	/* attribute name */
	char	*adsrc;		/* default value expression */
	int		 use_timestamp;
} TSAttr;

typedef struct {
	int		relnatts;
	TSAttr	attr[4];
} TSRel;


TSRel	 rc[2] = {
	{ 4, {
		{ "c1", "", 0 },
		{ "c2", "", 1 },
		{ "c3", "", 0 },
		{ "c4", "", 1 }
	} },
	{ 4, {
		{ "c1", "", 0 },
		{ "c2", "", 0 },
		{ "c3", "", 0 },
		{ "c4", "", 0 }
	} }
};

int pool_virtual_master_db_node_id(void)
{
	return 0;
}

bool pool_has_pgpool_regclass(void)
{
	return false;
}

bool pool_has_to_regclass(void)
{
	return false;
}

char *remove_quotes_and_schema_from_relname(char *table)
{
	return table;
}

int pool_get_major_version(void)
{
	return PROTO_MAJOR_V3;
}

POOL_RELCACHE *
pool_create_relcache(int cachesize, char *sql, func_ptr register_func, func_ptr unregister_func, bool issessionlocal)
{
	return (POOL_RELCACHE *) 1;
}

/* dummy result of relcache (attrname, adsrc, usetimestamp)*/
void *
pool_search_relcache(POOL_RELCACHE *relcache, POOL_CONNECTION_POOL *backend, char *table)
{
	if (strcmp(table, "\"rel1\"") == 0)
		return (void *) &(rc[0]);
	else
		return (void *) &(rc[1]);
}

/* dummy result of "SELECT now()" */
void do_query(POOL_CONNECTION *backend, char *query, POOL_SELECT_RESULT **result, int major) {
	static POOL_SELECT_RESULT res;
	static char *data[1] = {
		"2009-01-01 23:59:59.123456+09"
	};

	res.numrows = 1;
	res.data = data;

	*result = &res;
}

char *make_table_name_from_rangevar(RangeVar *rangevar)
{
	/* XXX: alias (AS ...) is left */
	return nodeToString(rangevar);
}

int
main(int argc, char **argv)
{
	char		*query;
	List 		*tree;
	ListCell 	*l;
	StartupPacket	 sp;
	POOL_CONNECTION_POOL	backend;
	POOL_CONNECTION_POOL_SLOT slot;
	POOL_SENT_MESSAGE	msg;
	POOL_QUERY_CONTEXT	ctx;
	backend.slots[0] = &slot;
	slot.sp = &sp;
	bool error;

	MemoryContextInit();

	pool_config->replication_mode = 1;

	if (argc != 2)
	{
		fprintf(stderr, "./timestmp-test query\n");
		exit(1);
	}

	tree = raw_parser(argv[1], &error);
	if (tree == NULL)
	{
		printf("syntax error: %s\n", argv[1]);
	}
	else
	{
		foreach(l, tree)
		{
			msg.num_tsparams = 0;
			msg.query_context = &ctx;
			Node *node = (Node *) lfirst(l);
			query = rewrite_timestamp(&backend, node, false, &msg);
			if (query)
				printf("%s\n", query);
			else
				printf("%s\n", argv[1]);

		}
	}
	return 0;
}

void free_select_result(POOL_SELECT_RESULT *result) {}
POOL_SESSION_CONTEXT *pool_get_session_context(bool noerror) {return NULL;}
int pg_frontend_exists(void) {return 0;}
int get_frontend_protocol_version(void) {return 0;}
int set_pg_frontend_blocking(bool blocking) {return 0;}
int send_to_pg_frontend(char* data, int len, bool flush) {return 0;}
int pool_send_to_frontend(char* data, int len, bool flush) {return 0;}
int pool_frontend_exists(void) {return 0;}
void ExceptionalCondition
	(const char *conditionName,const char *errorType,
	const char *fileName, int lineNumber) {}
