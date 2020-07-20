/* -*-pgsql-c-*- */
/*
 * $Header$
 *
 * pgpool: a language independent connection pool server for PostgreSQL
 * written by Tatsuo Ishii
 *
 * Copyright (c) 2003-2015	PgPool Global Development Group
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
 * pcp_child.c: PCP child process main
 *
 */
#include "config.h"
#include "pool.h"
#include "utils/palloc.h"
#include "utils/memutils.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <signal.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>


#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "pool.h"
#include "pool_config.h"
#include "utils/elog.h"
#include "parser/pg_list.h"

static int pcp_unix_fd, pcp_inet_fd;
volatile bool *pcp_recovery_in_progress;
static volatile sig_atomic_t pcp_got_sighup = 0;
static volatile sig_atomic_t pcp_restart_request = 0;
List *pcp_worker_children = NULL;
static volatile sig_atomic_t sigchld_request = 0;

static RETSIGTYPE pcp_exit_handler(int sig);
static RETSIGTYPE wakeup_handler_parent(int sig);
static RETSIGTYPE reload_config_handler(int sig);
static RETSIGTYPE restart_handler(int sig);
static RETSIGTYPE reap_handler(int sig);

static int pcp_do_accept(int unix_fd, int inet_fd);
static void start_pcp_command_processor_process(int port);
static void pcp_child_will_die(int code, Datum arg);
static void pcp_kill_all_children(int sig);
static void reaper(void);


#define CHECK_RESTART_REQUEST \
	do { \
		if (sigchld_request) \
		{ \
			reaper(); \
		} \
		if (pcp_restart_request) \
		{ \
			ereport(LOG,(errmsg("restart request received in pcp child process"))); \
			pcp_exit_handler(SIGTERM); \
		} \
    } while (0)


/*
 * main entry point for pcp child process
 */
void pcp_main(int unix_fd, int inet_fd)
{
	sigjmp_buf	local_sigjmp_buf;
	struct timeval uptime;

	ereport(DEBUG1,
			(errmsg("I am PCP child with pid:%d",getpid())));

	/* Identify myself via ps */
	init_ps_display("", "", "", "");

	gettimeofday(&uptime, NULL);
	pcp_unix_fd = unix_fd;
	pcp_inet_fd = inet_fd;

	pcp_recovery_in_progress = pool_shared_memory_create(sizeof(bool));
	*pcp_recovery_in_progress = false;
	/*
	 * install the call back for preparation of exit
	 */
	on_system_exit(pcp_child_will_die, (Datum)NULL);

	/* set up signal handlers */
	pool_signal(SIGTERM, pcp_exit_handler);
	pool_signal(SIGINT, pcp_exit_handler);
	pool_signal(SIGQUIT, pcp_exit_handler);
	pool_signal(SIGHUP, reload_config_handler);
	pool_signal(SIGCHLD, reap_handler);

	pool_signal(SIGUSR1, restart_handler);
	pool_signal(SIGUSR2, wakeup_handler_parent);
	pool_signal(SIGPIPE, SIG_IGN);
	pool_signal(SIGALRM, SIG_IGN);

	MemoryContextSwitchTo(TopMemoryContext);

	if (sigsetjmp(local_sigjmp_buf, 1) != 0)
	{
		error_context_stack = NULL;
		EmitErrorReport();

		pool_signal(SIGALRM, SIG_IGN);
		MemoryContextSwitchTo(TopMemoryContext);
		FlushErrorState();
	}
	/* We can now handle ereport(ERROR) */
	PG_exception_stack = &local_sigjmp_buf;
	for(;;)
	{
		int port;
		errno = 0;
		CHECK_RESTART_REQUEST;

		port = pcp_do_accept(unix_fd, inet_fd);
		if(port > 0)
		{
			start_pcp_command_processor_process(port);
		}
	}
}

static int
pcp_do_accept(int unix_fd, int inet_fd)
{
	fd_set readmask;
	int fds;
	struct sockaddr addr;
	socklen_t addrlen;
	int fd = 0;
	int afd;
	int inet = 0;

	set_ps_display("PCP: wait for connection request", false);

	FD_ZERO(&readmask);
	FD_SET(unix_fd, &readmask);
	if (inet_fd)
		FD_SET(inet_fd, &readmask);

	fds = select(Max(unix_fd, inet_fd)+1, &readmask, NULL, NULL, NULL);
	if (fds == -1)
	{
		if (errno == EAGAIN || errno == EINTR)
			return -1;
		ereport(ERROR,
				(errmsg("unable to accept new pcp connection"),
				 errdetail("select system call failed with error : \"%s\"",strerror(errno))));
	}
	if (FD_ISSET(unix_fd, &readmask))
	{
		fd = unix_fd;
	}
	if (FD_ISSET(inet_fd, &readmask))
	{
		fd = inet_fd;
		inet++;
	}

	addrlen = sizeof(addr);

	afd = accept(fd, &addr, &addrlen);
	if (afd < 0)
	{
		/*
		 * "Resource temporarily unavailable" (EAGAIN or EWOULDBLOCK)
		 * can be silently ignored.
		 */
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			ereport(ERROR,
					(errmsg("unable to accept new pcp connection"),
					 errdetail("socket accept system call failed with error : \"%s\"",strerror(errno))));
	}
	if (pcp_got_sighup)
	{
		MemoryContext oldContext = MemoryContextSwitchTo(TopMemoryContext);
		pool_get_config(get_config_file_name(), CFGCXT_RELOAD);
		MemoryContextSwitchTo(oldContext);
		pcp_got_sighup = 0;
	}
	ereport(DEBUG2,
			(errmsg("I am PCP child with PID:%d and accept fd:%d", getpid(), afd)));
	if (inet)
	{
		int on = 1;
		if (setsockopt(afd, IPPROTO_TCP, TCP_NODELAY,
					   (char *) &on,
					   sizeof(on)) < 0)
		{
			close(afd);
			ereport(ERROR,
					(errmsg("unable to accept new pcp connection"),
					 errdetail("setsockopt system call failed with error : \"%s\"",strerror(errno))));
		}
		if (setsockopt(afd, SOL_SOCKET, SO_KEEPALIVE,
					   (char *) &on,
					   sizeof(on)) < 0)
		{
			close(afd);
			ereport(ERROR,
					(errmsg("unable to accept new pcp connection"),
					 errdetail("setsockopt system call failed with error : \"%s\"",strerror(errno))));
		}
	}
	return afd;
}

/*
 * forks a new pcp worker child
 */
static void start_pcp_command_processor_process(int port)
{
	pid_t pid = fork();
	if (pid == 0)/* child */
	{
		/* Set the process type variable */
		processType = PT_PCP_WORKER;
		on_exit_reset();
		/* Close the listen sockets sockets */
		close(pcp_unix_fd);
		close(pcp_inet_fd);
		/* call PCP child main */
		if (pcp_worker_children)
			list_free(pcp_worker_children);
		pcp_worker_children= NULL;
		POOL_SETMASK(&UnBlockSig);
		pcp_worker_main(port); /* Never returns */
	}
	else if (pid == -1)
	{
		ereport(FATAL,
				(errmsg("fork() failed. reason: %s", strerror(errno))));
	}
	else /* parent */
	{
		ereport(LOG,
				(errmsg("forked new pcp worker, pid=%d socket=%d",
						(int) pid, (int) port)));
		/* close the port in parent process. It is only consumed by child */
		close(port);
		/* Add it to the list */
		pcp_worker_children = lappend_int(pcp_worker_children, (int)pid);
	}
}

/*
 * sends the signal to all children of pcp child process
 */
static void
pcp_kill_all_children(int sig)
{
	/* forward wakeup requests to children */
	ListCell* lc;
	foreach(lc, pcp_worker_children)
	{
		pid_t pid = (pid_t)lfirst_int(lc);
		kill(pid,sig);
	}
}
/*
 * handle SIGCHLD
 */
static RETSIGTYPE reap_handler(int sig)
{
	POOL_SETMASK(&BlockSig);
	sigchld_request = 1;
	POOL_SETMASK(&UnBlockSig);
}

static void reaper(void)
{
	pid_t pid;
	int status;

	ereport(DEBUG1,
			(errmsg("PCP child reaper handler")));

	/* clear SIGCHLD request */
	sigchld_request = 0;

	while ((pid = pool_waitpid(&status)) > 0)
	{
		if(WIFEXITED(status))
		{
			if(WEXITSTATUS(status) == POOL_EXIT_FATAL)
				ereport(LOG,
						(errmsg("PCP worker process with pid: %d exit with FATAL ERROR.", pid)));
			else
				ereport(LOG,
						(errmsg("PCP process with pid: %d exit with SUCCESS.", pid)));
		}
		if (WIFSIGNALED(status))
		{
			/* Child terminated by segmentation fault. Report it */
			if(WTERMSIG(status) == SIGSEGV)
				ereport(WARNING,
						(errmsg("PCP process with pid: %d was terminated by segmentation fault",pid)));
			else
				ereport(LOG,
						(errmsg("PCP process with pid: %d exits with status %d by signal %d", pid, status, WTERMSIG(status))));
		}
		else
			ereport(LOG,
					(errmsg("PCP process with pid: %d exits with status %d",pid, status)));
		ereport(DEBUG2,
				(errmsg("going to remove pid: %d from pid list having %d elements",pid, list_length(pcp_worker_children))));
		/* remove the pid of process from the list */
		pcp_worker_children = list_delete_int(pcp_worker_children,pid);
		ereport(DEBUG2,
				(errmsg("new list have %d elements",list_length(pcp_worker_children))));
	}
}

static RETSIGTYPE
pcp_exit_handler(int sig)
{
	pid_t wpid;

	POOL_SETMASK(&AuthBlockSig);
	ereport(DEBUG1,
			(errmsg("PCP child receives shutdown request signal %d, Forwarding to all children", sig)));

	pcp_kill_all_children(sig);

	if (sig == SIGTERM) /* smart shutdown */
	{
		ereport(DEBUG1,
				(errmsg("PCP child receives smart shutdown request")));
		/* close the listening sockets */
		close(pcp_unix_fd);
		close(pcp_inet_fd);
	}
	else if (sig == SIGINT)
	{
		ereport(DEBUG1,
				(errmsg("PCP child receives fast shutdown request")));
	}
	else if (sig == SIGQUIT)
	{
		ereport(DEBUG1,
				(errmsg("PCP child receives immediate shutdown request")));
	}

	POOL_SETMASK(&UnBlockSig);

	if (list_length(pcp_worker_children) > 0)
	{
		do
		{
			wpid = wait(NULL);
		}while (wpid > 0 || (wpid == -1 && errno == EINTR));
		
		if (wpid == -1 && errno != ECHILD)
			ereport(WARNING,
					(errmsg("wait() on pcp worker children failed. reason:%s", strerror(errno))));
		list_free(pcp_worker_children);
	}
	pcp_worker_children = NULL;

	exit(0);
}

/* Wakeup signal handler for pcp parent process */
static RETSIGTYPE
wakeup_handler_parent(int sig)
{
	/* forward wakeup signal to all children */
	pcp_kill_all_children(SIGUSR2);
}

static RETSIGTYPE
restart_handler(int sig)
{
	pcp_restart_request = 1;
}

/* SIGHUP handler */
static RETSIGTYPE reload_config_handler(int sig)
{
	pcp_got_sighup = 1;
}

static void pcp_child_will_die(int code, Datum arg)
{
	pid_t wpid;
	/*
	 * This is supposed to be called from main process
	 */
	if(processType != PT_PCP)
		return;
	if (list_length(pcp_worker_children) <= 0)
		return;
	pcp_kill_all_children(SIGINT);
	/* wait for all children to exit */
	do
	{
		wpid = wait(NULL);
	}while (wpid > 0 || (wpid == -1 && errno == EINTR));
	
	if (wpid == -1 && errno != ECHILD)
		ereport(WARNING,
				(errmsg("wait() on pcp worker children failed. reason:%s", strerror(errno))));
	
	POOL_SETMASK(&UnBlockSig);
}

/*
 * sets the shared memory flag to indicate pcp recovery command
 * is in progress. If the flag is already set the function returns
 * false.
 */
bool pcp_mark_recovery_in_progress(void)
{
	bool command_already_inprogress;
	pool_sigset_t oldmask;
	/* 
	 * only pcp worker is allowd to make this call
	 */
	if(processType != PT_PCP_WORKER)
		return false;

	POOL_SETMASK2(&BlockSig, &oldmask);
	pool_semaphore_lock(PCP_REQUEST_SEM);
	command_already_inprogress = *pcp_recovery_in_progress;
	*pcp_recovery_in_progress = true;
	pool_semaphore_unlock(PCP_REQUEST_SEM);
	POOL_SETMASK(&oldmask);
	return (command_already_inprogress == false);
}

/*
 * unsets the shared memory flag to indicate pcp recovery command
 * is finsihed.
 */
void pcp_mark_recovery_finished(void)
{
	pool_sigset_t oldmask;
	/*
	 * only pcp worker is allowd to make this call
	 */
	if(processType != PT_PCP_WORKER)
		return;

	POOL_SETMASK2(&BlockSig, &oldmask);
	pool_semaphore_lock(PCP_REQUEST_SEM);
	*pcp_recovery_in_progress = false;
	pool_semaphore_unlock(PCP_REQUEST_SEM);
	POOL_SETMASK(&oldmask);
}

