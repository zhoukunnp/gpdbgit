/*
 * $Header$
 *
 * pgpool: a language independent connection pool server for PostgreSQL
 * written by Tatsuo Ishii
 *
 * Copyright (c) 2003-2016	PgPool Global Development Group
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that copyright notice and this permission
 * notice appear in supporting documentation, and that the name of the
 * author not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission. The author makes no representations about the
 * suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * PCP client program to execute pcp commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "utils/fe_ports.h"
#include "utils/pool_path.h"
#include "pcp/pcp.h"

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "utils/getopt_long.h"
#endif

const char* progname = NULL;
const char * get_progname(const char *argv0);
char *last_dir_separator(const char *filename);

static void usage(void);
static inline bool app_require_nodeID(void);
static void output_watchdog_info_result(PCPResultInfo* pcpResInfo, bool verbose);
static void output_procinfo_result(PCPResultInfo* pcpResInfo, bool all, bool verbose);
static void output_proccount_result(PCPResultInfo* pcpResInfo, bool verbose);
static void output_poolstatus_result(PCPResultInfo* pcpResInfo, bool verbose);
static void output_nodeinfo_result(PCPResultInfo* pcpResInfo, bool verbose);
static void output_nodecount_result(PCPResultInfo* pcpResInfo, bool verbose);
static char* backend_status_to_string(BACKEND_STATUS status);

typedef enum
{
	PCP_ATTACH_NODE,
	PCP_DETACH_NODE,
	PCP_NODE_COUNT,
	PCP_NODE_INFO,
	PCP_POOL_STATUS,
	PCP_PROC_COUNT,
	PCP_PROC_INFO,
	PCP_PROMOTE_NODE,
	PCP_RECOVERY_NODE,
	PCP_STOP_PGPOOL,
	PCP_WATCHDOG_INFO,
	UNKNOWN,
} PCP_UTILITIES;

struct AppTypes
{
	const char *app_name;
	PCP_UTILITIES app_type;
	const char* allowed_options;
	const char* description;
};

struct AppTypes AllAppTypes[] =
	{
		{"pcp_attach_node", PCP_ATTACH_NODE,"n:h:p:U:wWvd","attach a node from pgpool-II"},
		{"pcp_detach_node", PCP_DETACH_NODE,"n:h:p:U:gwWvd","detach a node from pgpool-II"},
		{"pcp_node_count", PCP_NODE_COUNT,"h:p:U:wWvd","display the total number of nodes under pgpool-II's control"},
		{"pcp_node_info", PCP_NODE_INFO,"n:h:p:U:wWvd", "display a pgpool-II node's information"},
		{"pcp_pool_status", PCP_POOL_STATUS,"h:p:U:wWvd", "display pgpool configuration and status"},
		{"pcp_proc_count", PCP_PROC_COUNT,"h:p:U:wWvd", "display the list of pgpool-II child process PIDs"},
		{"pcp_proc_info", PCP_PROC_INFO,"h:p:P:U:awWvd","display a pgpool-II child process' information"},
		{"pcp_promote_node", PCP_PROMOTE_NODE,"n:h:p:U:gwWvd", "promote a node as new master from pgpool-II"},
		{"pcp_recovery_node", PCP_RECOVERY_NODE,"n:h:p:U:wWvd","recover a node"},
		{"pcp_stop_pgpool", PCP_STOP_PGPOOL,"m:h:p:U:wWvd", "terminate pgpool-II"},
		{"pcp_watchdog_info", PCP_WATCHDOG_INFO,"n:h:p:U:wWvd", "display a pgpool-II watchdog's information"},
		{NULL, UNKNOWN,NULL,NULL},
	};
struct AppTypes* current_app_type;

int
main(int argc, char **argv)
{
	char* host = NULL;
	int port = 9898;
	char* user = NULL;
	char* pass = NULL;
	int nodeID = -1;
	int processID = 0;
	int ch;
	char shutdown_mode = 's';
	int	optindex;
	int i;
	bool all = false;
	bool debug = false;
	bool need_password = true;
	bool gracefully = false;
	bool verbose = false;
	PCPConnInfo* pcpConn;
	PCPResultInfo* pcpResInfo;

	/* here we put all the allowed long options for all utilities */
	static struct option long_options[] = {
		{"help", no_argument, NULL, '?'},
		{"debug", no_argument, NULL, 'd'},
		{"version", no_argument, NULL, 'V'},
		{"host", required_argument, NULL, 'h'},
		{"port", required_argument, NULL, 'p'},
		{"process-id", required_argument, NULL, 'P'},
		{"username", required_argument, NULL, 'U'},
		{"no-password", no_argument, NULL, 'w'},
		{"password", no_argument, NULL, 'W'},
		{"mode", required_argument, NULL, 'm'},
		{"gracefully", no_argument, NULL, 'g'},
		{"verbose", no_argument, NULL, 'v'},
		{"all", no_argument, NULL, 'a'},
		{"node-id", required_argument, NULL, 'n'},
		{"watchdog-id", required_argument, NULL, 'n'},
		{NULL, 0, NULL, 0}
	};
	
	/* Identify the utility app */
	progname = get_progname(argv[0]);
	for( i =0; ;i++)
	{
		current_app_type = &AllAppTypes[i];
		if(current_app_type->app_type == UNKNOWN)
			break;
		if (strcmp(current_app_type->app_name, progname) == 0)
			break;
	}

	if(current_app_type->app_type == UNKNOWN)
	{
		fprintf(stderr, "%s is a invalid PCP utility\n",progname);
		exit(1);
	}

	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			usage();
			exit(0);
		}
		else if (strcmp(argv[1], "-V") == 0
				 || strcmp(argv[1], "--version") == 0)
		{
			fprintf(stderr, "%s (%s) %s\n",progname,PACKAGE, VERSION);
			exit(0);
		}
	}

	while ((ch = getopt_long(argc, argv, current_app_type->allowed_options, long_options, &optindex)) != -1) {
		switch (ch)
		{
			case 'd':
				debug = true;
				break;

			case 'a':
				all = true;
				break;

			case 'w':
				need_password = false;
				break;

			case 'W':
				need_password = true;
				break;

			case 'g':
				gracefully = true;
				break;

			case 'm':
				if (current_app_type->app_type == PCP_STOP_PGPOOL)
				{
					if (strcmp(optarg, "s") == 0 || strcmp(optarg, "smart") == 0)
						shutdown_mode = 's';
					else if (strcmp(optarg, "f") == 0 || strcmp(optarg, "fast") == 0)
						shutdown_mode = 'f';
					else if (strcmp(optarg, "i") == 0 || strcmp(optarg, "immediate") == 0)
						shutdown_mode = 'i';
					else
					{
						fprintf(stderr, "%s: Invalid shutdown mode \"%s\", must be either \"smart\" \"immediate\" or \"fast\" \n",progname,optarg);
						exit(1);
					}
				}
				else
				{
					fprintf(stderr, "Invalid argument \"%s\", Try \"%s --help\" for more information.\n",optarg,progname);
					exit(1);
				}
				break;

			case 'v':
				verbose = true;
				break;

			case 'n':
				nodeID = atoi(optarg);
				if (current_app_type->app_type == PCP_WATCHDOG_INFO)
				{
					if (nodeID < 0)
					{
						fprintf(stderr, "%s: Invalid watchdog-id \"%s\", must be a positive number or zero for a local watchdog node\n",progname,optarg);
						exit(0);
					}
				}
				else
				{
					if (nodeID < 0 || nodeID > MAX_NUM_BACKENDS)
					{
						fprintf(stderr, "%s: Invalid node-id \"%s\", must be between 0 and %d\n",progname,optarg,MAX_NUM_BACKENDS);
						exit(0);
					}
				}
				break;

			case 'p':
				port = atoi(optarg);
				if (port <= 1024 || port > 65535)
				{
					fprintf(stderr, "%s: Invalid port number \"%s\", must be between 1024 and 65535\n",progname,optarg);
					exit(0);
				}
				break;

			case 'P': /* PID */
				processID = atoi(optarg);
				if (processID <= 0 )
				{
					fprintf(stderr, "%s: Invalid process-id \"%s\", must be greater than 0\n",progname,optarg);
					exit(0);
				}
				break;

			case 'h':
				host = strdup(optarg);
				break;

			case 'U':
				user = strdup(optarg);
				break;

			case '?':
			default:
				/*
				 * getopt_long whould already have emitted a complaint
				 */
				fprintf(stderr, "Try \"%s --help\" for more information.\n\n",progname);
				exit(1);
		}
	}

	/*
	 * if we still have arguments, use it
	 */
	while (argc - optind >= 1)
	{
		if (current_app_type->app_type == PCP_PROC_INFO && processID <= 0)
		{
			processID = atoi(argv[optind]);
			if (processID <= 0 )
			{
				fprintf(stderr, "%s: Invalid process-id \"%s\", must be greater than 0\n",progname,optarg);
				exit(0);
			}
		}
		else if (current_app_type->app_type == PCP_WATCHDOG_INFO && nodeID < 0)
		{
			nodeID = atoi(argv[optind]);
			if (nodeID < 0 )
			{
				fprintf(stderr, "%s: Invalid watchdog-id \"%s\", must be a positive number or zero for local watchdog node\n",progname,optarg);
				exit(0);
			}
		}
		else if (app_require_nodeID() && nodeID < 0)
		{
			nodeID = atoi(argv[optind]);
			if (nodeID < 0 || nodeID > MAX_NUM_BACKENDS)
			{
				fprintf(stderr, "%s: Invalid node-id \"%s\", must be between 0 and %d\n",progname,optarg,MAX_NUM_BACKENDS);
				exit(0);
			}
		}
		else
			fprintf(stderr, "%s: Warning: extra command-line argument \"%s\" ignored\n",
					progname, argv[optind]);

		optind++;
	}

	if(nodeID < 0)
	{
		if (app_require_nodeID())
		{
			fprintf(stderr, "%s: missing node-id\n",progname);
			fprintf(stderr, "Try \"%s --help\" for more information.\n\n",progname);
			exit(1);
		}
		else if (current_app_type->app_type == PCP_WATCHDOG_INFO)
		{
			nodeID = -1;
		}
	}

	/* Get a new password if appropriate */
	if (need_password)
	{
		pass = simple_prompt("Password: ", 100, false);
		need_password = false;
	}

	pcpConn = pcp_connect(host, port, user, pass, debug?stdout:NULL);
	if(PCPConnectionStatus(pcpConn) != PCP_CONNECTION_OK)
	{
		fprintf(stderr, "%s\n",pcp_get_last_error(pcpConn)?pcp_get_last_error(pcpConn):"Unknown Error");
		exit(1);
	}

	/*
	 * Okay the connection is successful not call the actual PCP function
	 */
	if (current_app_type->app_type == PCP_ATTACH_NODE)
	{
		pcpResInfo = pcp_attach_node(pcpConn,nodeID);
	}

	else if (current_app_type->app_type == PCP_DETACH_NODE)
	{
		if (gracefully)
			pcpResInfo = pcp_detach_node_gracefully(pcpConn,nodeID);
		else
			pcpResInfo = pcp_detach_node(pcpConn,nodeID);
	}

	else if (current_app_type->app_type == PCP_NODE_COUNT)
	{
		pcpResInfo = pcp_node_count(pcpConn);
	}

	else if (current_app_type->app_type == PCP_NODE_INFO)
	{
		pcpResInfo = pcp_node_info(pcpConn,nodeID);
	}

	else if (current_app_type->app_type == PCP_POOL_STATUS)
	{
		pcpResInfo = pcp_pool_status(pcpConn);
	}

	else if (current_app_type->app_type == PCP_PROC_COUNT)
	{
		pcpResInfo = pcp_process_count(pcpConn);
	}

	else if (current_app_type->app_type == PCP_PROC_INFO)
	{
		pcpResInfo = pcp_process_info(pcpConn, processID);
	}

	else if (current_app_type->app_type == PCP_PROMOTE_NODE)
	{
		if (gracefully)
			pcpResInfo = pcp_promote_node_gracefully(pcpConn,nodeID);
		else
			pcpResInfo = pcp_promote_node(pcpConn,nodeID);
	}

	else if (current_app_type->app_type == PCP_RECOVERY_NODE)
	{
		pcpResInfo = pcp_recovery_node(pcpConn, nodeID);
	}

	else if (current_app_type->app_type == PCP_STOP_PGPOOL)
	{
		pcpResInfo = pcp_terminate_pgpool(pcpConn, shutdown_mode);
	}

	else if (current_app_type->app_type == PCP_WATCHDOG_INFO)
	{
		pcpResInfo = pcp_watchdog_info(pcpConn,nodeID);
	}

	else
	{
		/* should never happen */
		fprintf(stderr,"%s: Invalid pcp process\n",progname);
		goto DISCONNECT_AND_EXIT;
	}

	if(pcpResInfo == NULL || PCPResultStatus(pcpResInfo) != PCP_RES_COMMAND_OK)
	{
		fprintf(stderr, "%s\n",pcp_get_last_error(pcpConn)?pcp_get_last_error(pcpConn):"Unknown Error");
		goto DISCONNECT_AND_EXIT;
	}

	if (pcp_result_is_empty(pcpResInfo))
	{
		fprintf(stdout,"%s -- Command Successful\n",progname);
	}
	else
	{
		if (current_app_type->app_type == PCP_NODE_COUNT)
			output_nodecount_result(pcpResInfo, verbose);

		if (current_app_type->app_type == PCP_NODE_INFO)
			output_nodeinfo_result(pcpResInfo, verbose);

		if (current_app_type->app_type == PCP_POOL_STATUS)
			output_poolstatus_result(pcpResInfo, verbose);

		if (current_app_type->app_type == PCP_PROC_COUNT)
			output_proccount_result(pcpResInfo, verbose);

		if (current_app_type->app_type == PCP_PROC_INFO)
			output_procinfo_result(pcpResInfo, all, verbose);

		else if (current_app_type->app_type == PCP_WATCHDOG_INFO)
			output_watchdog_info_result(pcpResInfo, verbose);
	}

DISCONNECT_AND_EXIT:

	pcp_disconnect(pcpConn);
	pcp_free_connection(pcpConn);

	return 0;
}

static void
output_nodecount_result(PCPResultInfo* pcpResInfo, bool verbose)
{
	if(verbose)
	{
		printf("Node Count\n");
		printf("____________\n");
		printf(" %d\n", pcp_get_int_data(pcpResInfo, 0));
	}
	else
		printf("%d\n", pcp_get_int_data(pcpResInfo, 0));
}

static void
output_nodeinfo_result(PCPResultInfo* pcpResInfo, bool verbose)
{
	BackendInfo *backend_info = (BackendInfo *) pcp_get_binary_data(pcpResInfo,0);

	if (verbose)
	{
		printf("Hostname   : %s\nPort       : %d\nStatus     : %d\nWeight     : %f\nStatus Name: %s\n",
			   backend_info->backend_hostname,
			   backend_info->backend_port,
			   backend_info->backend_status,
			   backend_info->backend_weight/RAND_MAX,
			   backend_status_to_string(backend_info->backend_status));
	} else {
		printf("%s %d %d %f %s\n",
			   backend_info->backend_hostname,
			   backend_info->backend_port,
			   backend_info->backend_status,
			   backend_info->backend_weight/RAND_MAX,
			   backend_status_to_string(backend_info->backend_status));
	}
}

static void
output_poolstatus_result(PCPResultInfo* pcpResInfo, bool verbose)
{
	POOL_REPORT_CONFIG *status;
	int i;
	int array_size = pcp_result_slot_count(pcpResInfo);

	if(verbose)
	{
		for (i=0; i < array_size; i++)
		{
			status = (POOL_REPORT_CONFIG *)pcp_get_binary_data(pcpResInfo, i);
			printf("Name [%3d]:\t%s\n",i,status?status->name:"NULL");
			printf("Value:      \t%s\n",status?status->value:"NULL");
			printf("Description:\t%s\n\n",status?status->desc:"NULL");
		}
	}
	else
	{
		for (i=0; i < array_size; i++) {
			status = (POOL_REPORT_CONFIG *)pcp_get_binary_data(pcpResInfo, i);
			if(status == NULL)
			{
				printf("****Data at %d slot is NULL\n",i);
				continue;
			}
			printf("name : %s\nvalue: %s\ndesc : %s\n\n", status->name, status->value, status->desc);
		}
	}
}

static void
output_proccount_result(PCPResultInfo* pcpResInfo, bool verbose)
{
	int i;
	int process_count = pcp_get_data_length(pcpResInfo, 0) / sizeof(int);
	int *process_list = (int *)pcp_get_binary_data(pcpResInfo, 0);

	if (verbose)
	{
		printf("No \t | \t PID\n");
		printf("_____________________\n");
		for (i = 0; i < process_count; i++)
			printf("%d \t | \t %d\n", i,process_list[i]);
		printf("\nTotal Processes:%d\n",process_count);
	}
	else
	{
		for (i = 0; i < process_count; i++)
			printf("%d ", process_list[i]);
		printf("\n");
	}
}

static void
output_procinfo_result(PCPResultInfo* pcpResInfo, bool all, bool verbose)
{
	bool printed = false;
	int i;
	char * frmt;
	char strcreatetime[128];
	char strstarttime[128];
	int array_size = pcp_result_slot_count(pcpResInfo);
	if (verbose)
	{
		if (all)
			frmt =	"Database     : %s\n"
			"Username     : %s\n"
			"Start time   : %s\n"
			"Creation time: %s\n"
			"Major        : %d\n"
			"Minor        : %d\n"
			"Counter      : %d\n"
			"Backend PID  : %d\n"
			"Connected    : %d\n"
			"PID          : %d\n"
			"Backend ID   : %d\n";
		else
			frmt =	"Database     : %s\n"
			"Username     : %s\n"
			"Start time   : %s\n"
			"Creation time: %s\n"
			"Major        : %d\n"
			"Minor        : %d\n"
			"Counter      : %d\n"
			"Backend PID  : %d\n"
			"Connected    : %d\n";
	}
	else
	{
		if (all)
			frmt = "%s %s %s %s %d %d %d %d %d %d %d\n";
		else
			frmt = "%s %s %s %s %d %d %d %d %d\n";
	}

	for (i = 0; i < array_size; i++)
	{

		ProcessInfo *process_info = (ProcessInfo*) pcp_get_binary_data(pcpResInfo, i);
		if(process_info == NULL)
			break;
		if ((!all) && (process_info->connection_info->database[0] == '\0'))
			continue;
		printed = true;
		*strcreatetime = *strstarttime = '\0';
		
		if (process_info->start_time)
			strftime(strstarttime, 128, "%Y-%m-%d %H:%M:%S", localtime(&process_info->start_time));
		if (process_info->connection_info->create_time)
			strftime(strcreatetime, 128, "%Y-%m-%d %H:%M:%S", localtime(&process_info->connection_info->create_time));
		
		printf(frmt,
			   process_info->connection_info->database,
			   process_info->connection_info->user,
			   strstarttime,
			   strcreatetime,
			   process_info->connection_info->major,
			   process_info->connection_info->minor,
			   process_info->connection_info->counter,
			   process_info->connection_info->pid,
			   process_info->connection_info->connected,
			   process_info->pid,
			   process_info->connection_info->backend_id);
	}
	if(printed == false)
		printf("No process information available\n\n");
}

static void
output_watchdog_info_result(PCPResultInfo* pcpResInfo, bool verbose)
{
	int i;
	PCPWDClusterInfo *cluster = (PCPWDClusterInfo *)pcp_get_binary_data(pcpResInfo,0);
	if (verbose)
	{
		char* quorumStatus;
		if (cluster->quorumStatus == 0)
			quorumStatus = "QUORUM IS ON THE EDGE";
		else if (cluster->quorumStatus == 1)
			quorumStatus = "QUORUM EXIST";
		else if (cluster->quorumStatus == -1)
			quorumStatus = "QUORUM ABSENT";
		else if (cluster->quorumStatus == -2)
			quorumStatus = "NO MASTER NODE";
		else
			quorumStatus = "UNKNOWN";

		printf("Watchdog Cluster Information \n");
		printf("Total Nodes          : %d\n",cluster->remoteNodeCount +1);
		printf("Remote Nodes         : %d\n",cluster->remoteNodeCount);
		printf("Quorum state         : %s\n",quorumStatus);
		printf("Alive Remote Nodes   : %d\n",cluster->aliveNodeCount);
		printf("VIP up on local node : %s\n",cluster->escalated?"YES":"NO");
		printf("Master Node Name     : %s\n",cluster->masterNodeName);
		printf("Master Host Name     : %s\n\n",cluster->masterHostName);

		printf("Watchdog Node Information \n");
		for (i=0; i< cluster->nodeCount; i++)
		{
			PCPWDNodeInfo* watchdog_info = &cluster->nodeList[i];
			printf("Node Name      : %s\n",watchdog_info->nodeName);
			printf("Host Name      : %s\n",watchdog_info->hostName);
			printf("Delegate IP    : %s\n",watchdog_info->delegate_ip);
			printf("Pgpool port    : %d\n",watchdog_info->pgpool_port);
			printf("Watchdog port  : %d\n",watchdog_info->wd_port);
			printf("Node priority  : %d\n",watchdog_info->wd_priority);
			printf("Status         : %d\n",watchdog_info->state);
			printf("Status Name    : %s\n\n",watchdog_info->stateName);
		}
	}
	else
	{
		printf("%d %s %s %s\n\n",
			   cluster->remoteNodeCount +1,
			   cluster->escalated?"YES":"NO",
			   cluster->masterNodeName,
			   cluster->masterHostName);

		for (i=0; i< cluster->nodeCount; i++)
		{
			PCPWDNodeInfo* watchdog_info = &cluster->nodeList[i];
			printf("%s %s %d %d %d %s\n",
				   watchdog_info->nodeName,
				   watchdog_info->hostName,
				   watchdog_info->pgpool_port,
				   watchdog_info->wd_port,
				   watchdog_info->state,
				   watchdog_info->stateName);
		}
	}
}

/* returns true if the current application requires node id argument */
static inline bool
app_require_nodeID(void)
{
	return(current_app_type->app_type == PCP_ATTACH_NODE  ||
		   current_app_type->app_type == PCP_DETACH_NODE  ||
		   current_app_type->app_type == PCP_NODE_INFO    ||
		   current_app_type->app_type == PCP_PROMOTE_NODE ||
		   current_app_type->app_type == PCP_RECOVERY_NODE);
}


static void
usage(void)
{
	fprintf(stderr, "%s - %s\n",progname,current_app_type->description);
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "%s [OPTION...] %s\n",progname,
			app_require_nodeID()?"[node-id]":
			(current_app_type->app_type == PCP_WATCHDOG_INFO)?"[watchdog-id]":
			(current_app_type->app_type == PCP_PROC_INFO)?"[process-id]":"");

	fprintf(stderr, "Options:\n");
	/*
	 * print the command options
	 */
	fprintf(stderr, "  -U, --username=NAME    username for PCP authentication\n");
	fprintf(stderr, "  -h, --host=HOSTNAME    pgpool-II host\n");
	fprintf(stderr, "  -p, --port=PORT        PCP port number\n");
	fprintf(stderr, "  -w, --no-password      never prompt for password\n");
	fprintf(stderr, "  -W, --password         force password prompt (should happen automatically)\n");
	/*
	 * Now the options not available for all utilities
	 */
	if (app_require_nodeID())
	{
		fprintf(stderr, "  -n, --node-id=NODEID   ID of a backend node\n");
	}

	if (current_app_type->app_type == PCP_STOP_PGPOOL)
	{
		fprintf(stderr, "  -m, --mode=MODE        MODE can be \"smart\", \"fast\", or \"immediate\"\n");
	}
	if (current_app_type->app_type == PCP_PROMOTE_NODE ||
		current_app_type->app_type == PCP_DETACH_NODE)
	{
		fprintf(stderr, "  -g, --gracefully       promote gracefully(optional)\n");
	}

	if (current_app_type->app_type == PCP_WATCHDOG_INFO)
	{
		fprintf(stderr, "  -n, --watchdog-id=ID   ID of a other pgpool to get information for\n");
		fprintf(stderr, "                         ID 0 for the local watchdog\n");
		fprintf(stderr, "                         If omitted then get information of all watchdog nodes\n");
	}
	if (current_app_type->app_type == PCP_PROC_INFO)
	{
		fprintf(stderr, "  -P, --process-id=PID   PID of the child process to get information for (optional)\n");
		fprintf(stderr, "  -a, --all              display all child processes and their available connection slots\n");
	}

	fprintf(stderr, "  -d, --debug            enable debug message (optional)\n");
	fprintf(stderr, "  -v, --verbose          output verbose messages\n");
	fprintf(stderr, "  -?, --help             print this help\n\n");
}


/*
 * Extracts the actual name of the program as called -
 * stripped of .exe suffix if any
 */
const char *
get_progname(const char *argv0)
{
	const char *nodir_name;
	char       *progname;
	
	nodir_name = last_dir_separator(argv0);
	if (nodir_name)
		nodir_name++;
	else
		nodir_name = argv0;
	
	/*
	 * Make a copy in case argv[0] is modified by ps_status. Leaks memory, but
	 * called only once.
	 */
	progname = strdup(nodir_name);
	if (progname == NULL)
	{
		fprintf(stderr, "%s: out of memory\n", nodir_name);
		exit(1);                                /* This could exit the postmaster */
	}
	
	return progname;
}

/*
 * Translate the BACKEND_STATUS enum value to string.
 * the function returns the constant string so should not be freed
 */
static char* backend_status_to_string(BACKEND_STATUS status)
{
	char *statusName;

	switch (status) {

		case CON_UNUSED:
			statusName = BACKEND_STATUS_CON_UNUSED;
			break;

		case CON_CONNECT_WAIT:
			statusName = BACKEND_STATUS_CON_CONNECT_WAIT;
			break;

		case CON_UP:
			statusName = BACKEND_STATUS_CON_UP;
			break;

		case CON_DOWN:
			statusName = BACKEND_STATUS_CON_DOWN;
			break;

		default:
			statusName = "unknown";
			break;
	}
	return statusName;
}
