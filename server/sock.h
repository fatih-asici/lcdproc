/*
 * sock.h
 * This file is part of LCDd, the lcdproc server.
 *
 * This file is released under the GNU General Public License. Refer to the
 * COPYING file distributed with this package.
 *
 * Copyright (c) 1999, William Ferrell, Scott Scriven
 *
 */

#ifndef SOCK_H
#define SOCK_H

#include "shared/sockets.h"

typedef struct sockaddr_in sockaddr_in;

/* Server functions...*/
int sock_init ();
int sock_shutdown ();
int sock_create_inet_socket (char * addr, unsigned int port);
int sock_poll_clients ();

#endif
