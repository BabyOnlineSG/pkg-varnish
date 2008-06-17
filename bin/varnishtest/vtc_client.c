/*
 * Copyright (c) 2006-2008 Linpro AS
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "vtc.h"

#include "vqueue.h"
#include "miniobj.h"
#include "vss.h"
#include "libvarnish.h"

struct client {
	unsigned		magic;
#define CLIENT_MAGIC		0x6242397c
	char			*name;
	VTAILQ_ENTRY(client)	list;

	char			*spec;
	
	const char		*connect;

	pthread_t		tp;
};

static VTAILQ_HEAD(, client)	clients =
    VTAILQ_HEAD_INITIALIZER(clients);

/**********************************************************************
 * Server thread
 */

static void *
client_thread(void *priv)
{
	struct client *c;
	int fd = -1;

	CAST_OBJ_NOTNULL(c, priv, CLIENT_MAGIC);
	AN(c->connect);

	printf("##   %-4s started\n", c->name);
	printf("###  %-4s connect to %s\n", c->name, c->connect);
	fd = VSS_open(c->connect);
	assert(fd >= 0);
	printf("###  %-4s connected to %s fd is %d\n",
	    c->name, c->connect, fd);
	http_process(c->name, c->spec, fd, 1);
	AZ(close(fd));
	printf("##   %-4s ending\n", c->name);

	return (NULL);
}

/**********************************************************************
 * Allocate and initialize a client
 */

static struct client *
client_new(char *name)
{
	struct client *c;

	if (*name != 'c') {
		fprintf(stderr, "---- %-4s Client name must start with 'c'\n",
		    name);
		exit (1);
	}
	ALLOC_OBJ(c, CLIENT_MAGIC);
	AN(c);
	c->name = name;
	c->connect = ":9081";
	VTAILQ_INSERT_TAIL(&clients, c, list);
	return (c);
}

/**********************************************************************
 * Start the client thread
 */

static void
client_start(struct client *c)
{

	CHECK_OBJ_NOTNULL(c, CLIENT_MAGIC);
	printf("##   %-4s Starting client\n", c->name);
	AZ(pthread_create(&c->tp, NULL, client_thread, c));
}

/**********************************************************************
 * Wait for client thread to stop
 */

static void
client_wait(struct client *c)
{
	void *res;

	CHECK_OBJ_NOTNULL(c, CLIENT_MAGIC);
	printf("##   %-4s Waiting for client\n", c->name);
	AZ(pthread_join(c->tp, &res));
	if (res != NULL) {
		fprintf(stderr, "Server %s returned \"%s\"\n",
		    c->name, (char *)res);
		exit (1);
	}
	c->tp = NULL;
}

/**********************************************************************
 * Run the client thread
 */

static void
client_run(struct client *c)
{

	client_start(c);
	client_wait(c);
}


/**********************************************************************
 * Server command dispatch
 */

void
cmd_client(char **av, void *priv)
{
	struct client *c, *c2;

	(void)priv;

	if (av == NULL) {
		/* Reset and free */
		VTAILQ_FOREACH_SAFE(c, &clients, list, c2) {
			VTAILQ_REMOVE(&clients, c, list);
			if (c->tp != NULL)
				client_wait(c);
			FREE_OBJ(c);
			/* XXX: MEMLEAK */
		}
		return;
	}

	assert(!strcmp(av[0], "client"));
	av++;

	VTAILQ_FOREACH(c, &clients, list)
		if (!strcmp(c->name, av[0]))
			break;
	if (c == NULL) 
		c = client_new(av[0]);
	av++;

	for (; *av != NULL; av++) {
		if (!strcmp(*av, "-connect")) {
			c->connect = av[1];
			av++;
			continue;
		}
		if (!strcmp(*av, "-start")) {
			client_start(c);
			continue;
		}
		if (!strcmp(*av, "-wait")) {
			client_wait(c);
			continue;
		}
		if (!strcmp(*av, "-run")) {
			client_run(c);
			continue;
		}
		if (**av == '{') {
			c->spec = *av;
			continue;
		}
		fprintf(stderr, "Unknown client argument: %s\n", *av);
		exit (1);
	}
}