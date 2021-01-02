// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// anet.h: Basic TCP socket stuff made a bit less boring (header)
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2016 Oliver Jowett <oliver@mutability.co.uk>
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// This file incorporates work covered by the following copyright and
// license:
//
// Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//   * Neither the name of Redis nor the names of its contributors may be used
//     to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef ANET_H
#define ANET_H

#define ANET_OK 0
#define ANET_ERR -1
#define ANET_ERR_LEN 256

#if defined(__sun)
#define AF_LOCAL AF_UNIX
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int anetTcpConnect(char *err, char *addr, char *service, struct sockaddr_storage *ss);
int anetTcpNonBlockConnect(char *err, char *addr, char *service, struct sockaddr_storage *ss);
int anetTcpNonBlockConnectAddr(char *err, struct addrinfo *p);
int anetGetaddrinfo(char *err, char *addr, char *service, struct addrinfo **gai_result);
int anetRead(int fd, char *buf, int count);
int anetTcpServer(char *err, char *service, char *bindaddr, int *fds, int nfds);
int anetGenericAccept(char *err, int s, struct sockaddr *sa, socklen_t *len);
int anetWrite(int fd, char *buf, int count);
int anetNonBlock(char *err, int fd);
int anetTcpNoDelay(char *err, int fd);
int anetTcpKeepAlive(char *err, int fd);
int anetSetSendBuffer(char *err, int fd, int buffsize);
void anetCloseSocket(int fd);

#endif
