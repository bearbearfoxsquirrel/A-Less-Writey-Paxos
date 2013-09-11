/*
	Copyright (C) 2013 University of Lugano

	This file is part of LibPaxos.

	LibPaxos is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Libpaxos is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with LibPaxos.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef _TCP_SENDBUF_H_
#define _TCP_SENDBUF_H_

#include "evpaxos.h"
#include <event2/buffer.h>
#include <event2/bufferevent.h>

void send_paxos_prepare(struct bufferevent* bev, paxos_prepare* msg);
void send_paxos_promise(struct bufferevent* bev, paxos_promise* msg);
void send_paxos_accept(struct bufferevent* bev, paxos_accept* msg);
void send_paxos_accepted(struct bufferevent* bev, paxos_accepted* msg);
void send_paxos_repeat(struct bufferevent* bev, paxos_repeat* msg);
int recv_paxos_message(struct evbuffer* in, paxos_message* out);

#endif