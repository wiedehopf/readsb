// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// net_io.c: network handling.
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2014-2016 Oliver Jowett <oliver@mutability.co.uk>
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
// Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//  *  Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
//
//  *  Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "readsb.h"

#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/sendfile.h>

#include "uat2esnt/uat2esnt.h"

#define DLE 0x10
#define ETX 0x03

// ============================= Networking =============================
//

// read_fn typedef read_handler functions
static int handle_gpsd(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb);
static int handleCommandSocket(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb);
static int handleBeastCommand(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb);
static int decodeBinMessage(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb);
static int processHexMessage(struct client *c, char *hex, int remote, int64_t now, struct messageBuffer *mb);
static int decodeUatMessage(struct client *c, char *msg, int remote, int64_t now, struct messageBuffer *mb);
static int decodeSbsLine(struct client *c, char *line, int remote, int64_t now, struct messageBuffer *mb);
static int decodeAsterixMessage(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb);
static int decodeSbsLineMlat(struct client *c, char *line, int remote, int64_t now, struct messageBuffer *mb) {
    MODES_NOTUSED(remote);
    return decodeSbsLine(c, line, 64 + SOURCE_MLAT, now, mb);
}
static int decodeSbsLinePrio(struct client *c, char *line, int remote, int64_t now, struct messageBuffer *mb) {
    MODES_NOTUSED(remote);
    return decodeSbsLine(c, line, 64 + SOURCE_PRIO, now, mb);
}
static int decodeSbsLineJaero(struct client *c, char *line, int remote, int64_t now, struct messageBuffer *mb) {
    MODES_NOTUSED(remote);
    return decodeSbsLine(c, line, 64 + SOURCE_JAERO, now, mb);
}
static int decodePfMessage(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb);
// end read handlers

static void send_heartbeat(struct net_service *service);

static void autoset_modeac();
static void *pthreadGetaddrinfo(void *param);

static void modesCloseClient(struct client *c);
static int flushClient(struct client *c, int64_t now);
static char *read_uuid(struct client *c, char *p, char *eod);
static void modesReadFromClient(struct client *c, struct messageBuffer *mb);

static void drainMessageBuffer(struct messageBuffer *buf);

// ModeAC all zero messag
static const char beast_heartbeat_msg[] = {0x1a, '1', 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const char raw_heartbeat_msg[] = "*0000;\n";
static const char sbs_heartbeat_msg[] = "\r\n"; // is there a better one?
// CAUTION: sizeof includes the trailing \0 byte

static const heartbeat_t beast_heartbeat = {
    .msg = beast_heartbeat_msg,
    .len = sizeof(beast_heartbeat_msg)
};
static const heartbeat_t raw_heartbeat = {
    .msg = raw_heartbeat_msg,
    .len = sizeof(raw_heartbeat_msg) - 1
};
static const heartbeat_t sbs_heartbeat = {
    .msg = sbs_heartbeat_msg,
    .len = sizeof(sbs_heartbeat_msg) - 1
};
static const heartbeat_t no_heartbeat = {
    .msg = NULL,
    .len = 0
};

//
//=========================================================================
//
// Networking "stack" initialization
//

// Init a service with the given read/write characteristics, return the new service.
// Doesn't arrange for the service to listen or connect
static struct net_service *serviceInit(struct net_service_group *group, const char *descr, struct net_writer *writer, heartbeat_t heartbeat_out, heartbeat_t heartbeat_in, read_mode_t mode, const char *sep, read_fn handler) {
    if (!descr) {
        fprintf(stderr, "Fatal: no service description\n");
        exit(1);
    }

    if (!group->services) {
        group->alloc = NET_SERVICE_GROUP_MAX;
        group->services = cmalloc(group->alloc * sizeof(struct net_service));
    }

    if (!group->services) {
    }

    group->len++;
    if (group->len + 1 > group->alloc) {
        fprintf(stderr, "FATAL: Increase NET_SERVICE_GROUP_MAX\n");
        exit(1);
    }

    struct net_service *service = &group->services[group->len - 1];
    memset(service, 0, 2 * sizeof(struct net_service));
    // also set the extra service to zero, zero terminatd array

    service->group = group;
    service->descr = descr;
    service->listener_count = 0;
    service->pusher_count = 0;
    service->connections = 0;
    service->writer = writer;
    service->read_sep = sep;
    service->read_sep_len = sep ? strlen(sep) : 0;
    service->read_mode = mode;
    service->read_handler = handler;
    service->clients = NULL;

    service->heartbeat_out = heartbeat_out;
    service->heartbeat_in = heartbeat_in;

    if (service->writer) {
        if (service->writer->data) {
            fprintf(stderr, "FATAL: serviceInit() called twice on the same service: %s\n", descr);
            exit(1);
        }

        // set writer to zero
        memset(service->writer, 0, sizeof(struct net_writer));

        service->writer->data = cmalloc(MODES_OUT_BUF_SIZE);

        service->writer->service = service;
        service->writer->dataUsed = 0;
        service->writer->lastWrite = mstime();
        service->writer->lastReceiverId = 0;
        service->writer->connections = 0;

        if (service->writer == &Modes.beast_reduce_out) {
            service->writer->flushInterval = Modes.net_output_flush_interval_beast_reduce;
        } else {
            service->writer->flushInterval = Modes.net_output_flush_interval;
        }
    }

    return service;
}

static char *ais_charset = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_ !\"#$%&'()*+,-./0123456789:;<=>?";
static uint8_t char_to_ais(int ch)
{
    char *match;
    if (!ch)
        return 32;

    match = strchr(ais_charset, ch);
    if (match)
        return (uint8_t)(match - ais_charset);
    else
        return 32;
}

static int sendFiveHeartbeats(struct client *c, int64_t now) {
    // send 5 heartbeats to signal that we are a client that can accomodate feedback .... some counterparts crash if they get stuff they don't understand
    // this is really a crutch, but there is no other good way to signal this without causing issues
    int repeats = 5;
    const char *heartbeat_msg = c->service->heartbeat_out.msg;
    int heartbeat_len = c->service->heartbeat_out.len;

    if (heartbeat_msg && c->sendq && c->sendq_len + repeats * heartbeat_len < c->sendq_max) {
        for (int k = 0; k < repeats; k++) {
            memcpy(c->sendq + c->sendq_len, heartbeat_msg, heartbeat_len);
            c->sendq_len += heartbeat_len;
        }
    }
    return flushClient(c, now);
}



static void setProxyString(struct client *c) {
    snprintf(c->proxy_string, sizeof(c->proxy_string), "%s port %s", c->host, c->port);
    c->receiverId = fasthash64(c->proxy_string, strlen(c->proxy_string), 0x2127599bf4325c37ULL);
}

static int getSNDBUF(struct net_service *service) {
    if (service->sendqOverrideSize) {
        return service->sendqOverrideSize;
    } else {
        return (MODES_NET_SNDBUF_SIZE << Modes.net_sndbuf_size);
    }
}
static int getRCVBUF(struct net_service *service) {
    if (service->recvqOverrideSize) {
        return service->recvqOverrideSize;
    } else {
        return (MODES_NET_SNDBUF_SIZE << Modes.net_sndbuf_size);
    }
}
static void setBuffers(int fd, int sndsize, int rcvsize) {
    if (sndsize > 0 && setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void*)&sndsize, sizeof(sndsize)) == -1) {
        fprintf(stderr, "setsockopt SO_SNDBUF: %s", strerror(errno));
    }

    if (rcvsize > 0 && setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void*)&rcvsize, sizeof(rcvsize)) == -1) {
        fprintf(stderr, "setsockopt SO_RCVBUF: %s", strerror(errno));
    }
}

// Create a client attached to the given service using the provided socket FD ... not a socket in some exceptions
static struct client *createSocketClient(struct net_service *service, int fd) {
    struct client *c;
    int64_t now = mstime();

    if (!service || fd == -1) {
        fprintf(stderr, "<3> FATAL: createSocketClient called with invalid parameters!\n");
        exit(1);
    }
    if (!(c = (struct client *) cmalloc(sizeof (struct client)))) {
        fprintf(stderr, "<3> FATAL: Out of memory allocating a new %s network client\n", service->descr);
        exit(1);
    }
    memset(c, 0, sizeof (struct client));

    c->service = service;
    c->fd = fd;
    c->last_flush = now;
    c->last_send = now;
    c->last_read = now;
    c->connectedSince = now;
    c->last_read_flush = now;


    c->proxy_string[0] = '\0';
    c->host[0] = '\0';
    c->port[0] = '\0';

    c->receiverId = random();
    c->receiverId <<= 22;
    c->receiverId ^= random();
    c->receiverId <<= 22;
    c->receiverId ^= random();

    c->receiverId2 = 0;
    c->receiverIdLocked = 0;

    c->recent_rtt = -1;

    c->remote = 1; // Messages will be marked remote by default
    if ((c->fd == Modes.beast_fd) && (Modes.sdr_type == SDR_MODESBEAST || Modes.sdr_type == SDR_GNS)) {
        /* Message from a local connected Modes-S beast or GNS5894 are passed off the internet */
        c->remote = 0;
    }

    //fprintf(stderr, "c->receiverId: %016"PRIx64"\n", c->receiverId);

    c->bufmax = MODES_NET_SNDBUF_SIZE << Modes.net_sndbuf_size;
    if (service->sendqOverrideSize) {
        c->bufmax = service->sendqOverrideSize;
    }

    c->buf = cmalloc(c->bufmax);

    if (service->writer) {
        c->sendq_max = MODES_NET_SNDBUF_SIZE << Modes.net_sndbuf_size;
        if (service->sendqOverrideSize) {
            c->sendq_max = service->sendqOverrideSize;
        }
        if (!(c->sendq = cmalloc(c->sendq_max))) {
            fprintf(stderr, "Out of memory allocating client SendQ\n");
            exit(1);
        }
        // Have to keep track of this manually
        service->writer->lastReceiverId = 0; // make sure to resend receiverId

        service->writer->connections++;
    }
    service->connections++;
    Modes.modesClientCount++;

    c->next = service->clients;
    service->clients = c;


    if (Modes.debug_net && service->connections % 50 == 0) {
        fprintf(stderr, "%s connection count: %d\n", service->descr, service->connections);
    }

    if (service->writer && service->connections == 1) {
        service->writer->lastWrite = now; // suppress heartbeat initially
    }

    epoll_data_t data;
    data.ptr = c;
    c->epollEvent.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
    c->epollEvent.data = data;
    if (epoll_ctl(Modes.net_epfd, EPOLL_CTL_ADD, c->fd, &c->epollEvent))
        perror("epoll_ctl fail:");

    return c;
}

static int sendUUID(struct client *c, int64_t now) {
    struct net_connector *con = c->con;
    // sending UUID if hostname matches adsbexchange or for beast_reduce_plus output
    char uuid[150];
    uuid[0] = '\0';
    if ((c->sendq && c->sendq_len + 256 < c->sendq_max) && con
            && (con->enable_uuid_ping || (strstr(con->address, "feed") && strstr(con->address, ".adsbexchange.com")) || Modes.debug_ping || Modes.debug_send_uuid)) {

        int res = -1;

        if (con->uuid) {
            strncpy(uuid, con->uuid, 135);
            res = strlen(uuid);
        } else {
            int fd = open(Modes.uuidFile, O_RDONLY);
            // try legacy / adsbexchange image path as hardcoded fallback
            if (fd == -1) {
                fd = open("/boot/adsbx-uuid", O_RDONLY);
            }
            if (fd != -1) {
                res = read(fd, uuid, 128);
                close(fd);
            }
        }

        if (res >= 28) {
            if (uuid[res - 1] == '\n') {
                // remove trailing newline
                res--;
            }
            uuid[res] = '\0';

            c->sendq[c->sendq_len++] = 0x1A;
            c->sendq[c->sendq_len++] = 0xE4;
            // uuid is padded with 'f', always send 36 chars
            memset(c->sendq + c->sendq_len, 'f', 36);
            strncpy(c->sendq + c->sendq_len, uuid, res);
            c->sendq_len += 36;
        } else {
            uuid[0] = '\0';
            fprintf(stderr, "ERROR: Not a valid UUID: %s\n", Modes.uuidFile);
            fprintf(stderr, "Use this command to fix: sudo uuidgen > %s\n", Modes.uuidFile);
        }

        // enable ping stuff
        // O for high resolution timer, both P and p already used for previous iterations
        c->sendq[c->sendq_len++] = 0x1a;
        c->sendq[c->sendq_len++] = 'W';
        c->sendq[c->sendq_len++] = 'O';
        return flushClient(c, now);
    }
    return -1;
}


static void checkServiceConnected(struct net_connector *con, int64_t now) {

    if (!con->connecting) {
        return;
    }

    //fprintf(stderr, "checkServiceConnected fd: %d\n", con->fd);
    // delete dummyClient epollEvent for connection that is being established
    epoll_ctl(Modes.net_epfd, EPOLL_CTL_DEL, con->fd, &con->dummyClient.epollEvent);
    // we'll register new epollEvents in createSocketClient

    // At this point, we need to check getsockopt() to see if we succeeded or failed...
    int optval = -1;
    socklen_t optlen = sizeof(optval);
    if (getsockopt(con->fd, SOL_SOCKET, SO_ERROR, &optval, &optlen) == -1) {
        fprintf(stderr, "getsockopt failed: %d (%s)\n", errno, strerror(errno));
        // Bad stuff going on, but clear this anyway
        con->connecting = 0;
        anetCloseSocket(con->fd);
        return;
    }

    if (optval != 0) {
        // only 0 means "connection ok"
        if (!con->silent_fail) {
            fprintf(stderr, "%s: Connection to %s%s port %s failed: %d (%s)\n",
                    con->service->descr, con->address, con->resolved_addr, con->port, optval, strerror(optval));
        }
        con->connecting = 0;
        anetCloseSocket(con->fd);
        return;
    }

    // If we're able to create this "client", save the sockaddr info and print a msg
    struct client *c;

    c = createSocketClient(con->service, con->fd);
    if (!c) {
        con->connecting = 0;
        fprintf(stderr, "createSocketClient failed on fd %d to %s%s port %s\n",
                con->fd, con->address, con->resolved_addr, con->port);
        anetCloseSocket(con->fd);
        return;
    }

    strncpy(c->host, con->address, sizeof(c->host) - 1);
    strncpy(c->port, con->port, sizeof(c->port) - 1);
    setProxyString(c);

    con->connecting = 0;
    con->connected = 1;
    con->lastConnect = now;
    // link connection and client so we have access from one to the other
    c->con = con;
    con->c = c;

    int uuid_sent = (sendUUID(c, now) == 0);

    sendFiveHeartbeats(c, now);

    if ((c->sendq && c->sendq_len + 256 < c->sendq_max)
                && strcmp(con->protocol, "gpsd_in") == 0) {
        if (Modes.debug_gps) {
            fprintTime(stderr, now);
            fprintf(stderr, " gpsdebug: sending \'?WATCH={\"enable\":true,\"json\":true};\\n\'\n");
        }
        c->sendq_len += snprintf(c->sendq, 256, "?WATCH={\"enable\":true,\"json\":true};\n");
        if (flushClient(c, now) < 0) {
            return;
        }
    }
    if (!Modes.interactive) {
        if (uuid_sent) {
            fprintf(stderr, "%s: Connection established: %s%s port %s (sent UUID)\n",
                    con->service->descr, con->address, con->resolved_addr, con->port);
        } else {
            fprintf(stderr, "%s: Connection established: %s%s port %s\n",
                    con->service->descr, con->address, con->resolved_addr, con->port);
        }
    }

}

// Initiate an outgoing connection.
static void serviceConnect(struct net_connector *con, int64_t now) {

    int fd;

    // make sure backoff is never too small
    con->backoff = imax(Modes.net_connector_delay_min, con->backoff);

    if (con->try_addr && con->try_addr->ai_next) {
        // we have another address to try,
        // iterate the address info linked list
        con->try_addr = con->try_addr->ai_next;
    } else {
        // get the address info
        if (!con->gai_request_in_progress)  {
            // launch a pthread for async getaddrinfo
            con->try_addr = NULL;
            if (con->addr_info) {
                freeaddrinfo(con->addr_info);
                con->addr_info = NULL;
            }

            con->gai_request_in_progress = 1;
            con->gai_request_done = 0;

            if (pthread_create(&con->thread, NULL, pthreadGetaddrinfo, con)) {
                con->next_reconnect = now + Modes.net_connector_delay;
                fprintf(stderr, "%s: pthread_create ERROR for %s port %s: %s\n", con->service->descr, con->address, con->port, strerror(errno));
                return;
            }

            con->gai_request_in_progress = 1;
            con->next_reconnect = now + 20;
            return;
        }

        // gai request is in progress, let's check if it's done

        pthread_mutex_lock(&con->mutex);
        if (!con->gai_request_done) {
            con->next_reconnect = now + 20;
            pthread_mutex_unlock(&con->mutex);
            return;
        }
        pthread_mutex_unlock(&con->mutex);

        // gai request is done, join the thread that performed it
        con->gai_request_in_progress = 0;

        if (pthread_join(con->thread, NULL)) {
            fprintf(stderr, "%s: pthread_join ERROR for %s port %s: %s\n", con->service->descr, con->address, con->port, strerror(errno));
            con->next_reconnect = now + Modes.net_connector_delay;
            return;
        }

        if (con->gai_error) {
            if (!con->silent_fail) {
                fprintf(stderr, "%s: Name resolution for %s failed: %s\n", con->service->descr, con->address, gai_strerror(con->gai_error));
            }
            // limit name resolution attempts via backoff
            con->next_reconnect = now + con->backoff;
            con->backoff = imin(Modes.net_connector_delay, 2 * con->backoff);
            return;
        }

        // SUCCESS, we got the address info
        // start with the first element of the linked list
        con->try_addr = con->addr_info;
    }

    getnameinfo(con->try_addr->ai_addr, con->try_addr->ai_addrlen,
            con->resolved_addr, sizeof(con->resolved_addr) - 3,
            NULL, 0,
            NI_NUMERICHOST | NI_NUMERICSERV);

    if (strcmp(con->resolved_addr, con->address) == 0) {
        con->resolved_addr[0] = '\0';
    } else {
        char tmp[sizeof(con->resolved_addr)+3]; // shut up gcc
        snprintf(tmp, sizeof(tmp), " (%s)", con->resolved_addr);
        memcpy(con->resolved_addr, tmp, sizeof(con->resolved_addr));
    }

    if (Modes.debug_net) {
        //fprintf(stderr, "%s: Attempting connection to %s port %s ... (gonna set SNDBUF %d RCVBUF %d)\n", con->service->descr, con->address, con->port, getSNDBUF(con->service), getRCVBUF(con->service));
        fprintf(stderr, "%s: Attempting connection to %s port %s ...\n", con->service->descr, con->address, con->port);
    }

    if (!con->try_addr->ai_next) {
        // limit tcp connection attemtps via backoff
        con->next_reconnect = now + con->backoff;
        con->backoff = imin(Modes.net_connector_delay, 2 * con->backoff);
    } else {
        // quickly try all IPs associated with a name if there are multiple
        con->next_reconnect = now + 20;
    }



    struct addrinfo *ai = con->try_addr;
    fd = anetCreateSocket(Modes.aneterr, ai->ai_family, SOCK_NONBLOCK);

    if (fd == ANET_ERR) {
        fprintf(stderr, "%s: Connection to %s%s port %s failed: %s\n",
                con->service->descr, con->address, con->resolved_addr, con->port, Modes.aneterr);
        return;
    }

    con->fd = fd;
    con->connecting = 1;
    con->connect_timeout = now + imin(Modes.net_connector_delay, 5000); // really if your connection won't establish after 5 seconds ... tough luck.
    //fprintf(stderr, "connect_timeout: %ld\n", (long) (con->connect_timeout - now));

    if (anetTcpKeepAlive(Modes.aneterr, fd) != ANET_OK) {
        fprintf(stderr, "%s: Unable to set keepalive: connection to %s port %s ...\n", con->service->descr, con->address, con->port);
    }

    // struct client for epoll purposes
    struct client *c = &con->dummyClient;

    c->service = con->service;
    c->fd = con->fd;
    c->net_connector_dummyClient = 1;
    c->epollEvent.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
    c->epollEvent.data.ptr = c;
    c->con = con;

    if (epoll_ctl(Modes.net_epfd, EPOLL_CTL_ADD, c->fd, &c->epollEvent)) {
        perror("epoll_ctl fail:");
    }

    // explicitely setting tcp buffers causes failure of linux tcp window auto tuning ... it just doesn't work well without the auto tuning
    if (0) {
        setBuffers(fd, getSNDBUF(con->service), getRCVBUF(con->service));
    }

    if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0 && errno != EINPROGRESS) {
        epoll_ctl(Modes.net_epfd, EPOLL_CTL_DEL, con->fd, &con->dummyClient.epollEvent);
        con->connecting = 0;
        anetCloseSocket(con->fd);
        fprintf(stderr, "%s: Connection to %s%s port %s failed: %s\n",
                con->service->descr, con->address, con->resolved_addr, con->port, strerror(errno));
    }
}

// Timer callback checking periodically whether the push service lost its server
// connection and requires a re-connect.
static void serviceReconnectCallback(int64_t now) {
    // Loop through the connectors, and
    //  - If it's not connected:
    //    - If it's "connecting", check to see if it timed out
    //    - Otherwise, if enough time has passed, try reconnecting

    for (int i = 0; i < Modes.net_connectors_count; i++) {
        struct net_connector *con = &Modes.net_connectors[i];
        if (!con->connected) {
            // If we've exceeded our connect timeout, close connection.
            if (con->connecting && now >= con->connect_timeout) {
                if (!con->silent_fail) {
                    fprintf(stderr, "%s: Connection to %s%s port %s timed out.\n",
                            con->service->descr, con->address, con->resolved_addr, con->port);
                }
                con->connecting = 0;
                // delete dummyClient epollEvent for connection that is being established
                epoll_ctl(Modes.net_epfd, EPOLL_CTL_DEL, con->fd, &con->dummyClient.epollEvent);
                anetCloseSocket(con->fd);
            }
            if (!con->connecting && (con->next_reconnect <= now || Modes.synthetic_now)) {
                serviceConnect(con, now);
            }
        } else {
            // check for idle connection, this server version requires data
            // or a heartbeat, otherwise it will force a reconnect
            struct client *c = con->c;
            if (Modes.net_heartbeat_interval && c
                    && now - c->last_read > 2 * Modes.net_heartbeat_interval
                    && c->service->heartbeat_in.msg != NULL
               ) {
                fprintf(stderr, "%s: No data or heartbeat received for %.0f seconds, reconnecting: %s port %s\n",
                        c->service->descr, (2 * Modes.net_heartbeat_interval) / 1000.0, c->host, c->port);
                modesCloseClient(c);
            }
        }
    }
}

// Set up the given service to listen on an address/port.
// _exits_ on failure!
void serviceListen(struct net_service *service, char *bind_addr, char *bind_ports, int epfd) {
    int *fds = NULL;
    int n = 0;
    char *p, *end;
    char buf[128];

    if (service->listener_count > 0) {
        fprintf(stderr, "Tried to set up the service %s twice!\n", service->descr);
        exit(1);
    }

    if (!bind_ports || !strcmp(bind_ports, "") || !strcmp(bind_ports, "0"))
        return;

    if (0 && Modes.debug_net) {
        fprintf(stderr, "serviceListen: %s with SNDBUF %d RCVBUF %d)\n", service->descr,  getSNDBUF(service), getRCVBUF(service));
    }

    p = bind_ports;
    while (p && *p) {
        int newfds[16];
        int nfds, i;

        int is_unix = 0;
        if (strncmp(p, "unix:", 5) == 0) {
            is_unix = 1;
            p += 5;
        }

        end = strpbrk(p, ", ");

        if (!end) {
            strncpy(buf, p, sizeof (buf) - 1);
            buf[sizeof (buf) - 1] = 0;
            p = NULL;
        } else {
            size_t len = end - p;
            if (len >= sizeof (buf))
                len = sizeof (buf) - 1;
            memcpy(buf, p, len);
            buf[len] = 0;
            p = end + 1;
        }
        if (is_unix) {
            if (service->unixSocket) {
                fprintf(stderr, "Multiple unix sockets per service are not supported! %s (%s): %s\n",
                        buf, service->descr, Modes.aneterr);
                exit(1);
            }

            sfree(service->unixSocket);
            service->unixSocket = strdup(buf);

            unlink(service->unixSocket);
            int fd = anetUnixSocket(Modes.aneterr, buf, SOCK_NONBLOCK);
            if (fd == ANET_ERR) {
                fprintf(stderr, "Error opening the listening port %s (%s): %s\n",
                        buf, service->descr, Modes.aneterr);
                exit(1);
            }
            if (chmod(service->unixSocket, 0666) != 0) {
                perror("serviceListen: couldn't set permissions for unix socket due to:");
            }
            newfds[0] = fd;
            nfds = 1;
        } else {
            //nfds = anetTcpServer(Modes.aneterr, buf, bind_addr, newfds, sizeof (newfds), SOCK_NONBLOCK, getSNDBUF(service), getRCVBUF(service));
            // explicitely setting tcp buffers causes failure of linux tcp window auto tuning ... it just doesn't work well without the auto tuning
            nfds = anetTcpServer(Modes.aneterr, buf, bind_addr, newfds, sizeof (newfds), SOCK_NONBLOCK, -1, -1);
            if (nfds == ANET_ERR) {
                fprintf(stderr, "Error opening the listening port %s (%s): %s\n",
                        buf, service->descr, Modes.aneterr);
                exit(1);
            }
        }
        static int listenerCount;
        listenerCount++;
        char listenString[1024];
        snprintf(listenString, 1023, "%5s: %s port", buf, service->descr);
        fprintf(stderr, "%-38s\n", listenString);

        fds = realloc(fds, (n + nfds) * sizeof (int));
        if (!fds) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }

        for (i = 0; i < nfds; ++i) {
            fds[n++] = newfds[i];
        }
    }

    service->listener_count = n;
    service->listener_fds = fds;

    if (epfd >= 0) {
        service->listenSockets = cmalloc(service->listener_count * sizeof(struct client));
        memset(service->listenSockets, 0, service->listener_count * sizeof(struct client));
        for (int i = 0; i < service->listener_count; ++i) {

            // struct client for epoll purposes for each listen socket.
            struct client *c = &service->listenSockets[i];

            c->service = service;
            c->fd = service->listener_fds[i];
            c->acceptSocket = 1;
            c->epollEvent.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
            c->epollEvent.data.ptr = c;

            if (epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &c->epollEvent))
                perror("epoll_ctl fail:");

        }
    }
}
static void initMessageBuffers() {
    if (Modes.decodeThreads > 1) {
        pthread_mutex_init(&Modes.decodeLock, NULL);
        pthread_mutex_init(&Modes.trackLock, NULL);
        pthread_mutex_init(&Modes.outputLock, NULL);

        Modes.decodeTasks = allocate_task_group(Modes.decodeThreads);
        Modes.decodePool = threadpool_create(Modes.decodeThreads, 0);
    }

    Modes.netMessageBuffer = cmalloc(Modes.decodeThreads * sizeof(struct messageBuffer));
    memset(Modes.netMessageBuffer, 0x0, Modes.decodeThreads * sizeof(struct messageBuffer));
    for (int k = 0; k < Modes.decodeThreads; k++) {
        struct messageBuffer *buf = &Modes.netMessageBuffer[k];
        buf->alloc = 128 * imin(3, Modes.net_sndbuf_size);
        buf->len = 0;
        buf->id = k;
        buf->activeClient = NULL;
        int bytes = buf->alloc * sizeof(struct modesMessage);
        buf->msg = cmalloc(bytes);
        //fprintf(stderr, "netMessageBuffer alloc: %d size: %d\n", buf->alloc, bytes);
    }
}

void modesInitNet(void) {
    initMessageBuffers();

    uat2esnt_initCrcTables();

    if (0) {
        char *msg[4] = { "-00a974f135362f522fc408c9122e1b015900;",
             "-08a78bea35705f5283880459010227605809e00d40a2040be2a5c2a00004a0000000;rs=2;",
             "-10ad7233358a9d528bc40aa900be3120880000000000000000000000000b10000000;rs=4;",
             "-10a78bea3570b152830c0449010626e04800000000000000000000000004a0000000;rs=2;" };

        for (int i = 0; i < 4; i++) {
            char output[2048];
            uat2esnt_convert_message(msg[i], msg[i] + strlen(msg[i]), output, output + sizeof(output));
            fprintf(stderr, "%s\n", output);
        }
        exit(1);
    }

    Modes.net_connector_delay_min = imax(100, Modes.net_connector_delay / 64);
    Modes.last_connector_fail = Modes.next_reconnect_callback = mstime();

    if (!Modes.net)
        return;
    struct net_service *beast_out;
    struct net_service *beast_reduce_out;
    struct net_service *garbage_out;
    struct net_service *raw_out;
    struct net_service *raw_in;
    struct net_service *vrs_out;
    struct net_service *json_out;
    struct net_service *feedmap_out;
    struct net_service *sbs_out;
    struct net_service *sbs_out_replay;
    struct net_service *sbs_out_mlat;
    struct net_service *sbs_out_jaero;
    struct net_service *sbs_out_prio;
    struct net_service *asterix_out;
    struct net_service *asterix_in;
    struct net_service *sbs_in;
    struct net_service *sbs_in_mlat;
    struct net_service *sbs_in_jaero;
    struct net_service *sbs_in_prio;
    struct net_service *gpsd_in;
    struct net_service *planefinder_in;

    signal(SIGPIPE, SIG_IGN);

    Modes.net_epfd = my_epoll_create(&Modes.exitNowEventfd);

    // set up listeners
    raw_out = serviceInit(&Modes.services_out, "Raw TCP output", &Modes.raw_out, raw_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(raw_out, Modes.net_bind_address, Modes.net_output_raw_ports, Modes.net_epfd);

    beast_out = serviceInit(&Modes.services_out, "Beast TCP output", &Modes.beast_out, beast_heartbeat, no_heartbeat, READ_MODE_BEAST_COMMAND, NULL, handleBeastCommand);
    serviceListen(beast_out, Modes.net_bind_address, Modes.net_output_beast_ports, Modes.net_epfd);

    beast_reduce_out = serviceInit(&Modes.services_out, "BeastReduce TCP output", &Modes.beast_reduce_out, beast_heartbeat, no_heartbeat, READ_MODE_BEAST_COMMAND, NULL, handleBeastCommand);
    serviceListen(beast_reduce_out, Modes.net_bind_address, Modes.net_output_beast_reduce_ports, Modes.net_epfd);

    garbage_out = serviceInit(&Modes.services_out, "Garbage TCP output", &Modes.garbage_out, beast_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(garbage_out, Modes.net_bind_address, Modes.garbage_ports, Modes.net_epfd);

    vrs_out = serviceInit(&Modes.services_out, "VRS json output", &Modes.vrs_out, no_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(vrs_out, Modes.net_bind_address, Modes.net_output_vrs_ports, Modes.net_epfd);

    json_out = serviceInit(&Modes.services_out, "Position json output", &Modes.json_out, no_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(json_out, Modes.net_bind_address, Modes.net_output_json_ports, Modes.net_epfd);

    feedmap_out = serviceInit(&Modes.services_out, "Forward feed map data", &Modes.feedmap_out, no_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);

    sbs_out = serviceInit(&Modes.services_out, "SBS TCP output ALL", &Modes.sbs_out, sbs_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(sbs_out, Modes.net_bind_address, Modes.net_output_sbs_ports, Modes.net_epfd);

    sbs_out_replay = serviceInit(&Modes.services_out, "SBS TCP output MAIN", &Modes.sbs_out_replay, sbs_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    sbs_out_prio = serviceInit(&Modes.services_out, "SBS TCP output PRIO", &Modes.sbs_out_prio, sbs_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    sbs_out_mlat = serviceInit(&Modes.services_out, "SBS TCP output MLAT", &Modes.sbs_out_mlat, sbs_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    sbs_out_jaero = serviceInit(&Modes.services_out, "SBS TCP output JAERO", &Modes.sbs_out_jaero, sbs_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);

    serviceListen(sbs_out_jaero, Modes.net_bind_address, Modes.net_output_jaero_ports, Modes.net_epfd);


    int sbs_port_len = strlen(Modes.net_output_sbs_ports);
    int pos = sbs_port_len - 1;
    if (sbs_port_len <= 5 && Modes.net_output_sbs_ports[pos] == '5') {

        char *replay = strdup(Modes.net_output_sbs_ports);
        replay[pos] = '6';
        serviceListen(sbs_out_replay, Modes.net_bind_address, replay, Modes.net_epfd);

        char *mlat = strdup(Modes.net_output_sbs_ports);
        mlat[pos] = '7';
        serviceListen(sbs_out_mlat, Modes.net_bind_address, mlat, Modes.net_epfd);

        char *prio = strdup(Modes.net_output_sbs_ports);
        prio[pos] = '8';
        serviceListen(sbs_out_prio, Modes.net_bind_address, prio, Modes.net_epfd);

        char *jaero = strdup(Modes.net_output_sbs_ports);
        jaero[pos] = '9';
        if (sbs_out_jaero->listener_count == 0)
            serviceListen(sbs_out_jaero, Modes.net_bind_address, jaero, Modes.net_epfd);

        sfree(replay);
        sfree(mlat);
        sfree(prio);
        sfree(jaero);
    }

    sbs_in = serviceInit(&Modes.services_in, "SBS TCP input MAIN", NULL, no_heartbeat, sbs_heartbeat, READ_MODE_ASCII, "\n",  decodeSbsLine);
    serviceListen(sbs_in, Modes.net_bind_address, Modes.net_input_sbs_ports, Modes.net_epfd);

    sbs_in_mlat = serviceInit(&Modes.services_in, "SBS TCP input MLAT", NULL, no_heartbeat, sbs_heartbeat, READ_MODE_ASCII, "\n",  decodeSbsLineMlat);
    sbs_in_prio = serviceInit(&Modes.services_in, "SBS TCP input PRIO", NULL, no_heartbeat, sbs_heartbeat, READ_MODE_ASCII, "\n",  decodeSbsLinePrio);
    sbs_in_jaero = serviceInit(&Modes.services_in, "SBS TCP input JAERO", NULL, no_heartbeat, sbs_heartbeat, READ_MODE_ASCII, "\n",  decodeSbsLineJaero);


    serviceListen(sbs_in_jaero, Modes.net_bind_address, Modes.net_input_jaero_ports, Modes.net_epfd);

    sbs_port_len = strlen(Modes.net_input_sbs_ports);
    pos = sbs_port_len - 1;
    if (sbs_port_len <= 5 && Modes.net_input_sbs_ports[pos] == '6') {
        char *mlat = strdup(Modes.net_input_sbs_ports);
        mlat[pos] = '7';
        serviceListen(sbs_in_mlat, Modes.net_bind_address, mlat, Modes.net_epfd);

        char *prio = strdup(Modes.net_input_sbs_ports);
        prio[pos] = '8';
        serviceListen(sbs_in_prio, Modes.net_bind_address, prio, Modes.net_epfd);

        char *jaero = strdup(Modes.net_input_sbs_ports);
        jaero[pos] = '9';
        if (sbs_in_jaero->listener_count == 0)
            serviceListen(sbs_in_jaero, Modes.net_bind_address, jaero, Modes.net_epfd);

        sfree(mlat);
        sfree(prio);
        sfree(jaero);
    }

    asterix_out = serviceInit(&Modes.services_out, "ASTERIX output", &Modes.asterix_out, no_heartbeat, no_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(asterix_out, Modes.net_bind_address, Modes.net_output_asterix_ports, Modes.net_epfd);

    asterix_in = serviceInit(&Modes.services_in, "ASTERIX TCP input", NULL, no_heartbeat, no_heartbeat, READ_MODE_ASTERIX, NULL, decodeAsterixMessage);
    serviceListen(asterix_in, Modes.net_bind_address, Modes.net_input_asterix_ports, Modes.net_epfd);

    gpsd_in = serviceInit(&Modes.services_in, "GPSD TCP input", &Modes.gpsd_in, no_heartbeat, no_heartbeat, READ_MODE_ASCII, "\n", handle_gpsd);

    if (Modes.json_dir && Modes.json_globe_index && Modes.globe_history_dir) {
        /* command input */
        struct net_service *commandService = serviceInit(&Modes.services_in, "command input", NULL, no_heartbeat, no_heartbeat, READ_MODE_ASCII, "\n", handleCommandSocket);
        char commandSocketFile[PATH_MAX];
        char commandSocket[PATH_MAX];
        snprintf(commandSocketFile, PATH_MAX, "%s/cmd.sock", Modes.json_dir);
        unlink(commandSocketFile);
        snprintf(commandSocket, PATH_MAX, "unix:%s/cmd.sock", Modes.json_dir);
        serviceListen(commandService, Modes.net_bind_address, commandSocket, Modes.net_epfd);
        chmod(commandSocket, 0600);
    }

    raw_in = serviceInit(&Modes.services_in, "Raw TCP input", NULL, no_heartbeat, raw_heartbeat, READ_MODE_ASCII, "\n", processHexMessage);
    serviceListen(raw_in, Modes.net_bind_address, Modes.net_input_raw_ports, Modes.net_epfd);

    /* Beast input via network */
    Modes.beast_in_service = serviceInit(&Modes.services_in, "Beast TCP input", &Modes.beast_in, no_heartbeat, beast_heartbeat, READ_MODE_BEAST, NULL, decodeBinMessage);
    if (Modes.netIngest) {
        Modes.beast_in_service->sendqOverrideSize = MODES_NET_SNDBUF_SIZE;
        Modes.beast_in_service->recvqOverrideSize = MODES_NET_SNDBUF_SIZE;
        // --net-buffer won't increase receive buffer for ingest server to avoid running out of memory using lots of connections
    }
    serviceListen(Modes.beast_in_service, Modes.net_bind_address, Modes.net_input_beast_ports, Modes.net_epfd);

    /* Beast input from local Modes-S Beast via USB */
    if (Modes.sdr_type == SDR_MODESBEAST || Modes.sdr_type == SDR_GNS) {
        Modes.serial_client = createSocketClient(Modes.beast_in_service, Modes.beast_fd);
    }

    /* Planefinder input via network */
    planefinder_in = serviceInit(&Modes.services_in, "Planefinder TCP input", NULL, no_heartbeat, no_heartbeat, READ_MODE_PLANEFINDER, NULL, decodePfMessage);
    serviceListen(planefinder_in, Modes.net_bind_address, Modes.net_input_planefinder_ports, Modes.net_epfd);

    Modes.uat_in_service = serviceInit(&Modes.services_in, "UAT TCP input", NULL, no_heartbeat, no_heartbeat, READ_MODE_ASCII, "\n", decodeUatMessage);
    // for testing ... don't care to create an argument to open this port
    // serviceListen(Modes.uat_in_service, Modes.net_bind_address, "1234", Modes.net_epfd);

    for (int i = 0; i < Modes.net_connectors_count; i++) {
        struct net_connector *con = &Modes.net_connectors[i];
        if (strcmp(con->protocol, "beast_out") == 0)
            con->service = beast_out;
        else if (strcmp(con->protocol, "beast_in") == 0)
            con->service = Modes.beast_in_service;
        else if (strcmp(con->protocol, "beast_reduce_out") == 0)
            con->service = beast_reduce_out;
        else if (strcmp(con->protocol, "beast_reduce_plus_out") == 0) {
            con->service = beast_reduce_out;
            con->enable_uuid_ping = 1;
        } else if (strcmp(con->protocol, "raw_out") == 0)
            con->service = raw_out;
        else if (strcmp(con->protocol, "raw_in") == 0)
            con->service = raw_in;
        else if (strcmp(con->protocol, "planefinder_in") == 0)
            con->service = planefinder_in;
        else if (strcmp(con->protocol, "vrs_out") == 0)
            con->service = vrs_out;
        else if (strcmp(con->protocol, "json_out") == 0)
            con->service = json_out;
        else if (strcmp(con->protocol, "feedmap_out") == 0)
            con->service = feedmap_out;
        else if (strcmp(con->protocol, "sbs_out") == 0)
            con->service = sbs_out;
        else if (strcmp(con->protocol, "asterix_out") == 0)
            con->service = asterix_out;
        else if (strcmp(con->protocol, "asterix_in") == 0)
            con->service = asterix_in;
        else if (strcmp(con->protocol, "sbs_in") == 0)
            con->service = sbs_in;
        else if (strcmp(con->protocol, "sbs_in_mlat") == 0)
            con->service = sbs_in_mlat;
        else if (strcmp(con->protocol, "sbs_in_jaero") == 0)
            con->service = sbs_in_jaero;
        else if (strcmp(con->protocol, "sbs_in_prio") == 0)
            con->service = sbs_in_prio;
        else if (strcmp(con->protocol, "sbs_out_mlat") == 0)
            con->service = sbs_out_mlat;
        else if (strcmp(con->protocol, "sbs_out_jaero") == 0)
            con->service = sbs_out_jaero;
        else if (strcmp(con->protocol, "sbs_out_prio") == 0)
            con->service = sbs_out_prio;
        else if (strcmp(con->protocol, "sbs_out_replay") == 0)
            con->service = sbs_out_replay;
        else if (strcmp(con->protocol, "gpsd_in") == 0)
            con->service = gpsd_in;
        else if (strcmp(con->protocol, "uat_in") == 0)
            con->service = Modes.uat_in_service;

    }

    if (Modes.dump_beast_dir) {
        int res = mkdir(Modes.dump_beast_dir, 0755);
        if (res != 0 && errno != EEXIST) {
            perror("issue creating dump-beast-dir");
        } else {
            Modes.dump_fw = createZstdFw(4 * 1024 * 1024);
            Modes.dump_beast_index = -1;
            dump_beast_check(mstime());
        }
    }
}


//
//=========================================================================
// Accept new connections
static void modesAcceptClients(struct client *c, int64_t now) {
    if (!c || !c->acceptSocket)
        return;

    int listen_fd = c->fd;
    struct net_service *s = c->service;

    struct sockaddr_storage storage;
    struct sockaddr *saddr = (struct sockaddr *) &storage;
    socklen_t slen = sizeof(storage);

    int fd;
    errno = 0;
    while ((fd = anetGenericAccept(Modes.aneterr, listen_fd, saddr, &slen, SOCK_NONBLOCK)) >= 0) {

        if (Modes.modesClientCount > Modes.max_fds_net) {
            // drop new modes clients if the count gets near resource limits
            anetCloseSocket(c->fd);
            static int64_t antiSpam;
            if (now > antiSpam) {
                antiSpam = now + 30 * SECONDS;
                fprintf(stderr, "<3> Can't accept new connection, limited to %d clients, consider increasing ulimit!\n", Modes.max_fds_net);
            }
        }

        c = createSocketClient(s, fd);
        if (s->unixSocket && c) {
            strcpy(c->host, s->unixSocket);
            fprintf(stderr, "%s: new c at %s\n", c->service->descr, s->unixSocket);
        } else if (c) {
            // We created the client, save the sockaddr info and 'hostport'
            getnameinfo(saddr, slen,
                    c->host, sizeof(c->host),
                    c->port, sizeof(c->port),
                    NI_NUMERICHOST | NI_NUMERICSERV);

            setProxyString(c);
            if (Modes.debug_net && (!Modes.netIngest || c->service->group == &Modes.services_out)) {
                fprintf(stderr, "%s: new c from %s port %s (fd %d)\n",
                        c->service->descr, c->host, c->port, fd);
            }
            if (anetTcpKeepAlive(Modes.aneterr, fd) != ANET_OK)
                fprintf(stderr, "%s: Unable to set keepalive on connection from %s port %s (fd %d)\n", c->service->descr, c->host, c->port, fd);
        } else {
            fprintf(stderr, "%s: Fatal: createSocketClient shouldn't fail!\n", s->descr);
            exit(1);
        }


        sendFiveHeartbeats(c, now);
    }

    if (errno != EMFILE && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
        fprintf(stderr, "%s: Error accepting new connection: %s\n", s->descr, Modes.aneterr);
    }
}

//
//=========================================================================
//
// On error free the client, collect the structure, adjust maxfd if needed.
//
static void modesCloseClient(struct client *c) {
    if (!c->service) {
        fprintf(stderr, "warning: double close of net client\n");
        return;
    }
    if (Modes.netIngest || Modes.netReceiverId || Modes.debug_no_discard) {
        double elapsed = (mstime() - c->connectedSince + 1) / 1000.0;
        double kbitpersecond = c->bytesReceived / 128.0 / elapsed;

        char uuid[64]; // needs 36 chars and null byte
        sprint_uuid(c->receiverId, c->receiverId2, uuid);
        fprintf(stderr, "disc: %s %6.1f s %6.2f kbit/s rId %s %s\n",
                (c->rtt > PING_DISCONNECT) ? "RTT  " : ((c->garbage >= GARBAGE_THRESHOLD) ? "garb." : "     "),
                elapsed, kbitpersecond,
                uuid, c->proxy_string);
    }

    epoll_ctl(Modes.net_epfd, EPOLL_CTL_DEL, c->fd, &c->epollEvent);
    anetCloseSocket(c->fd);
    c->service->connections--;
    Modes.modesClientCount--;
    if (c->service->writer) {
        c->service->writer->connections--;
    }
    struct net_connector *con = c->con;
    if (con) {
        int64_t now = mstime();
        // Clean this up and set the next_reconnect timer for another try.
        con->connecting = 0;
        con->connected = 0;
        con->c = NULL;

        int64_t sinceLastConnect = now - con->lastConnect;
        // successfull connection, decrement backoff by connection time
        if (sinceLastConnect > con->backoff) {
            con->backoff = 0;
        } else {
            con->backoff -= sinceLastConnect;
        }
        // make sure it's not too small
        con->backoff = imax(Modes.net_connector_delay_min, con->backoff);

        // if we were connected for some time, an immediate reconnect is expected
        con->next_reconnect = con->lastConnect + con->backoff;

        Modes.next_reconnect_callback = now;
        Modes.last_connector_fail = now;
    }

    // mark it as inactive and ready to be freed
    c->fd = -1;
    c->service = NULL;
    c->modeac_requested = 0;

    if (Modes.mode_ac_auto)
        autoset_modeac();
}

static void lockReceiverId(struct client *c) {
    if (c->receiverIdLocked)
        return;
    c->receiverIdLocked = 1;
    if (Modes.netIngest && Modes.debug_net && c->garbage < 50) {
        char uuid[64]; // needs 36 chars and null byte
        sprint_uuid(c->receiverId, c->receiverId2, uuid);
        fprintf(stderr, "new client:                        rId %s %s\n",
                uuid, c->proxy_string);
    }
}

static inline uint32_t readPingEscaped(char *p) {
    unsigned char *pu = (unsigned char *) p;
    uint32_t res = 0;
    res += *pu++ << 16;
    if (*pu == 0x1a)
        pu++;
    res += *pu++ << 8;
    if (*pu == 0x1a)
        pu++;
    res += *pu++;
    if (*pu == 0x1a)
        pu++;
    if (0 && Modes.debug_ping)
        fprintf(stderr, "readPing: %d\n", res);
    return res;
}
static inline uint32_t readPing(char *p) {
    unsigned char *pu = (unsigned char *) p;
    uint32_t res = 0;
    res += *pu++ << 16;
    res += *pu++ << 8;
    res += *pu++;
    if (0 && Modes.debug_ping)
        fprintf(stderr, "readPing: %d\n", res);
    return res;
}

static void pingClient(struct client *c, uint32_t ping) {
    // truncate to 24 bit for clarity
    ping = ping & ((1 << 24) - 1);
    if (c->sendq_len + 8 >= c->sendq_max)
        return;
    char *p = c->sendq + c->sendq_len;

    *p++ = 0x1a;
    *p++ = 'P';
    *p++ = (uint8_t) (ping >> 16);
    if (*(p-1) == 0x1a)
        *p++ = 0x1a;
    *p++ = (uint8_t) (ping >> 8);
    if (*(p-1) == 0x1a)
        *p++ = 0x1a;
    *p++ = (uint8_t) (ping >> 0);
    if (*(p-1) == 0x1a)
        *p++ = 0x1a;

    c->sendq_len = p - c->sendq;
    if (0 && Modes.debug_ping)
        fprintf(stderr, "Sending Ping c: %d\n", ping);
}
static void pong(struct client *c, int64_t now) {
    if (now < c->pingReceived)
        fprintf(stderr, "WAT?! now < c->pingReceived\n");
    pingClient(c, c->ping + (now - c->pingReceived));
}
static void pingSenders(struct net_service *service, int64_t now) {
    if (!Modes.ping)
        return;
    uint32_t newPing = now & ((1 << 24) - 1);
    // only send a ping every 50th interval or every 5 seconds
    // the respoder will interpolate using the local clock
    if (newPing >= Modes.currentPing + 5000 || newPing < Modes.currentPing) {
        Modes.currentPing = newPing;
        if (Modes.debug_ping)
            fprintf(stderr, "Sending Ping: %d\n", newPing);
        for (struct client *c = service->clients; c; c = c->next) {
            if (!c->service)
                continue;
            // some devices can't deal with any data on the backchannel
            // for the time being only send to receivers that enable it on connect
            if (Modes.netIngest && !c->pingEnabled)
                continue;
            if (!c->pongReceived && now > c->connectedSince + 20 * SECONDS)
                continue;
            pingClient(c, newPing);
            if (flushClient(c, now) < 0) {
                continue;
            }
        }
    }
}
static int pongReceived(struct client *c, int64_t now) {
    c->pongReceived = now;
    static int64_t antiSpam;

    // times in milliseconds


    int64_t pong = c->pong;
    int64_t current = now & ((1LL << 24) - 1);

    // handle 24 bit overflow by making the 2 numbers comparable
    if (labs((long) (current - pong)) > (1LL << 24) * 7 / 8) {
        if (current < pong) {
            current += (1 << 24);
        } else {
            pong += (1 << 24);
        }
    }

    if (current < pong) {
        // even without overflow, current can be smaller than pong due to
        // the other clock ticking up a ms just after receiving the ping (with sub ms latency)
        // but this clock ticked up just before sending the ping
        // other clock anomalies can make this happen as well,
        // even when debugging let's not log it unless the it's more than 3 ms
        if (Modes.debug_ping && pong - current > 3 && now > antiSpam) {
            antiSpam = now + 100;
            fprintf(stderr, "pongReceived strange: current < pong by %ld\n", (long) (pong - current));
        }
    }

    c->rtt = current - pong;

    if (c->rtt < 0) {
        c->rtt = 0;
    }

    int32_t bucket = 0;
    float bucketsize = PING_BUCKETBASE;
    float bucketmax = 0;
    for (int i = 0; i < PING_BUCKETS; i++) {
        bucketmax += bucketsize;
        bucketmax = nearbyint(bucketmax / 10) * 10;
        bucketsize *= PING_BUCKETMULT;

        bucket = i;

        if (c->rtt <= bucketmax) {
            break;
        }
    }
    Modes.stats_current.remote_ping_rtt[bucket]++;

    // more quickly arrive at a sensible average
    if (c->recent_rtt <= 0) {
        c->recent_rtt = c->rtt;
    } else if (c->bytesReceived < 5000) {
        c->recent_rtt = c->recent_rtt * 0.9 +  c->rtt * 0.1;
    } else {
        c->recent_rtt = c->recent_rtt * 0.995 +  c->rtt * 0.005;
    }
    if (c->latest_rtt <= 0) {
        c->latest_rtt = c->rtt;
    } else {
        c->latest_rtt = c->latest_rtt * 0.9 +  c->rtt * 0.1;
    }

    if (Modes.debug_ping && 0) {
        char uuid[64]; // needs 36 chars and null byte
        sprint_uuid(c->receiverId, c->receiverId2, uuid);
        fprintf(stderr, "rId %s %ld %4.0f %s current: %ld pong: %ld\n",
                uuid, (long) c->rtt, c->recent_rtt, c->proxy_string, (long) current, (long) pong);
    }

    // only log if the average is greater the rejection threshold, don't log for single packet events
    // actual discard / rejection happens elsewhere int the code
    if (c->rtt > Modes.ping_reject && now > antiSpam) {
        char uuid[64]; // needs 36 chars and null byte
        sprint_uuid(c->receiverId, c->receiverId2, uuid);
        if (Modes.debug_nextra) {
            antiSpam = now + 250; // limit to 4 messages a second
        } else {
            antiSpam = now + 30 * SECONDS;
        }
        if (Modes.netIngest) {
            fprintf(stderr, "reject: %6.0fms %6.0fms %6.0fms rId %s %s\n",
                    (double) c->rtt, c->latest_rtt, c->recent_rtt, uuid, c->proxy_string);
        } else {
            fprintf(stderr, "high network delay: %6lld ms; discarding data: %s rId %s\n",
                    (long long) c->rtt, c->proxy_string, uuid);
        }
    }
    if (Modes.netIngest && c->latest_rtt > Modes.ping_reduce) {
        // tell the client to slow down via beast command
        // misuse pingReceived as a timeout variable
        if (now > c->pingReceived + PING_REDUCE_DURATION / 2) {
            if (Modes.debug_nextra && now > antiSpam) {
                antiSpam = now + 250; // limit to 4 messages a second
                char uuid[64]; // needs 36 chars and null byte
                sprint_uuid(c->receiverId, c->receiverId2, uuid);
                fprintf(stderr, "reduce: %6.0f ms %6.0f ms  rId %s %s\n",
                        c->latest_rtt, c->recent_rtt, uuid, c->proxy_string);
            }
            if (c->sendq_len + 3 < c->sendq_max) {
                c->sendq[c->sendq_len++] = 0x1a;
                c->sendq[c->sendq_len++] = 'W';
                c->sendq[c->sendq_len++] = 'S';
                c->pingReceived = now;
            }
            if (flushClient(c, now) < 0) {
                return 1;
            }
        }
    }
    if (Modes.netIngest && c->rtt > PING_DISCONNECT) {
        return 1; // disconnect the client if the messages are delayed too much
    }
    return 0;
}


static inline int flushClient(struct client *c, int64_t now) {
    if (!c->service) { fprintf(stderr, "report error: Ahlu8pie\n"); return -1; }
    int toWrite = c->sendq_len;
    char *psendq = c->sendq;

    if (toWrite == 0) {
        c->last_flush = now;
        return 0;
    }

    int bytesWritten = send(c->fd, psendq, toWrite, 0);
    int err = errno;

    // If we get -1, it's only fatal if it's not EAGAIN/EWOULDBLOCK
    if (bytesWritten < 0 && err != EAGAIN && err != EWOULDBLOCK) {
        fprintf(stderr, "%s: Send Error: %s: %s port %s (fd %d, SendQ %d, RecvQ %d)\n",
                c->service->descr, strerror(err), c->host, c->port,
                c->fd, c->sendq_len, c->buflen);
        modesCloseClient(c);
        return -1;
    }
    if (bytesWritten > 0) {
        Modes.stats_current.network_bytes_out += bytesWritten;
        // Advance buffer
        psendq += bytesWritten;
        toWrite -= bytesWritten;

        c->last_send = now;	// If we wrote anything, update this.
        if (bytesWritten == c->sendq_len) {
            c->sendq_len = 0;
            c->last_flush = now;
        } else {
            c->sendq_len -= bytesWritten;
            memmove((void*)c->sendq, c->sendq + bytesWritten, toWrite);
        }
    }
    if (c->last_flush != now && !(c->epollEvent.events & EPOLLOUT)) {
        // if we couldn't flush our buffer, make epoll tell us when we can write again
        c->epollEvent.events |= EPOLLOUT;
        if (epoll_ctl(Modes.net_epfd, EPOLL_CTL_MOD, c->fd, &c->epollEvent))
            perror("epoll_ctl fail:");
    }
    if ((c->epollEvent.events & EPOLLOUT) && c->last_flush == now) {
        // if set, remove EPOLLOUT from epoll if flush was successful
        c->epollEvent.events ^= EPOLLOUT;
        if (epoll_ctl(Modes.net_epfd, EPOLL_CTL_MOD, c->fd, &c->epollEvent))
            perror("epoll_ctl fail:");
    }

    // If we haven't been able to empty the buffer for longer than 8 * flush_interval, disconnect.
    // give the connection 10 seconds to ramp up --> automatic TCP window scaling in Linux ...
    int64_t flushTimeout = imax(1 * SECONDS, 8 * Modes.net_output_flush_interval);
    if (now - c->last_flush > flushTimeout && now - c->connectedSince > 10 * SECONDS) {
        fprintf(stderr, "%s: Couldn't flush data for %.2fs (Insufficient bandwidth?): disconnecting: %s port %s (fd %d, SendQ %d)\n", c->service->descr, flushTimeout / 1000.0, c->host, c->port, c->fd, c->sendq_len);
        modesCloseClient(c);
        return -1;
    }
    return bytesWritten;
}

//
//=========================================================================
//
// Send the write buffer for the specified writer to all connected clients
//
static void flushWrites(struct net_writer *writer) {
    int64_t now = mstime();
    //fprintTimePrecise(stderr, now); fprintf(stderr, "flushing %s %5d bytes\n", writer->service->descr, writer->dataUsed);
    for (struct client *c = writer->service->clients; c; c = c->next) {
        if (!c->service)
            continue;
        if (c->service->writer == writer->service->writer) {
            if (c->pingEnabled) {
                pong(c, now);
            }
            // give the connection 10 seconds to ramp up --> automatic TCP window scaling in Linux ...
            if ((c->sendq_len + writer->dataUsed) >= c->sendq_max) {
                if (now - c->connectedSince < 10 * SECONDS) {
                    fprintf(stderr, "%s: Discarding full SendQ: %s port %s (fd %d, SendQ %d, RecvQ %d)\n",
                            c->service->descr, c->host, c->port,
                            c->fd, c->sendq_len, c->buflen);
                    c->sendq_len = 0;
                    flushClient(c, now);
                    continue;
                }
                // Too much data in client SendQ.  Drop client - SendQ exceeded.
                fprintf(stderr, "%s: Dropped due to full SendQ: %s port %s (fd %d, SendQ %d, RecvQ %d)\n",
                        c->service->descr, c->host, c->port,
                        c->fd, c->sendq_len, c->buflen);
                modesCloseClient(c);
                continue;	// Go to the next client
            }
            // Append the data to the end of the queue, increment len
            memcpy(c->sendq + c->sendq_len, writer->data, writer->dataUsed);
            c->sendq_len += writer->dataUsed;
            // Try flushing...
            if (flushClient(c, now) < 0) {
                continue;
            }
            if (!c->service) {
                continue;
            }
        }
    }
    writer->dataUsed = 0;
    writer->lastWrite = now;
    return;
}

// Prepare to write up to 'len' bytes to the given net_writer.
// Returns a pointer to write to, or NULL to skip this write.
static void *prepareWrite(struct net_writer *writer, int len) {
    if (!writer->connections) {
        return NULL;
    }

    if (writer->dataUsed && writer->dataUsed + len >= Modes.net_output_flush_size) {
        flushWrites(writer);
        if (writer->dataUsed + len > MODES_OUT_BUF_SIZE) {
            // this shouldn't happen due to flushWrites only writing to internal client buffers
            fprintf(stderr, "prepareWrite: not enough space in writer buffer, requested len: %d\n", len);
            return NULL;
        }
    }

    return writer->data + writer->dataUsed;
}

// Complete a write previously begun by prepareWrite.
// endptr should point one byte past the last byte written
// to the buffer returned from prepareWrite.
static void completeWrite(struct net_writer *writer, void *endptr) {
    if (writer->dataUsed == 0 && endptr - writer->data > 0) {
        int64_t now = mstime();
        //fprintTimePrecise(stderr, now); fprintf(stderr, "completeWrite starting packet for %s\n", writer->service->descr);
        writer->nextFlush = now + writer->flushInterval;
    }

    writer->dataUsed = endptr - writer->data;

    if (writer->dataUsed >= Modes.net_output_flush_size) {
        flushWrites(writer);
    }
}

static char *netTimestamp(char *p, int64_t timestamp) {
    unsigned char ch;
    /* timestamp, big-endian */
    *p++ = (ch = (timestamp >> 40));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (timestamp >> 32));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (timestamp >> 24));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (timestamp >> 16));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (timestamp >> 8));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (timestamp));
    if (0x1A == ch) {
        *p++ = ch;
    }
    return p;
}

//
//=========================================================================
//
// Write raw output in Beast Binary format with Timestamp to TCP clients
//
static void modesSendBeastOutput(struct modesMessage *mm, struct net_writer *writer) {
    int msgLen = mm->msgbits / 8;
    // 0x1a 0xe3 receiverId(2*8) 0x1a msgType timestamp+signal(2*7) message(2*msgLen)
    char *p = prepareWrite(writer, (2 + 2 * 8 + 2 + 2 * 7) + 2 * msgLen);
    unsigned char ch;
    int j;
    int sig;
    unsigned char *msg = (Modes.net_verbatim ? mm->verbatim : mm->msg);

    if (!p)
        return;

    // receiverId, big-endian, in own message to make it backwards compatible
    // only send the receiverId when it changes
    if (Modes.netReceiverId && writer->lastReceiverId != mm->receiverId) {
        writer->lastReceiverId = mm->receiverId;
        *p++ = 0x1a;
        // other dump1090 / readsb versions or beast implementations should discard unknown message types
        *p++ = 0xe3; // good enough guess no one is using this.
        for (int i = 7; i >= 0; i--) {
            *p++ = (ch = ((mm->receiverId >> (8 * i)) & 0xFF));
            if (0x1A == ch) {
                *p++ = ch;
            }
        }
    }

    *p++ = 0x1a;
    if (msgLen == MODES_SHORT_MSG_BYTES) {
        *p++ = '2';
    } else if (msgLen == MODES_LONG_MSG_BYTES) {
        *p++ = '3';
    } else if (msgLen == MODEAC_MSG_BYTES) {
        *p++ = '1';
    } else {
        return;
    }

    /* timestamp, big-endian */
    p = netTimestamp(p, mm->timestamp);

    sig = nearbyint(sqrt(mm->signalLevel) * 255);
    if (mm->signalLevel > 0 && sig < 1)
        sig = 1;
    if (sig > 255)
        sig = 255;
    *p++ = ch = (char) sig;
    if (0x1A == ch) {
        *p++ = ch;
    }

    for (j = 0; j < msgLen; j++) {
        *p++ = (ch = msg[j]);
        if (0x1A == ch) {
            *p++ = ch;
        }
    }

    completeWrite(writer, p);
}

static void modesDumpBeastData(struct modesMessage *mm) {
    if (!Modes.dump_fw) {
        return;
    }
    int msgLen = mm->msgbits / 8;
    // 0x1a 0xe3 receiverId(2*8) 0x1a msgType timestamp+signal(2*7) message(2*msgLen)
    char store[(2 + 2 * 8 + 2 + 2 * 7) + 2 * MODES_LONG_MSG_BYTES];
    char *p = store;
    unsigned char ch;
    int j;
    int sig;
    unsigned char *msg = (Modes.net_verbatim ? mm->verbatim : mm->msg);

    char *start = p;

    // receiverId, big-endian, in own message to make it backwards compatible
    // only send the receiverId when it changes
    if (Modes.netReceiverId && Modes.dump_lastReceiverId != mm->receiverId) {
        Modes.dump_lastReceiverId = mm->receiverId;
        *p++ = 0x1a;
        // other dump1090 / readsb versions or beast implementations should discard unknown message types
        *p++ = 0xe3; // good enough guess no one is using this.
        for (int i = 7; i >= 0; i--) {
            *p++ = (ch = ((mm->receiverId >> (8 * i)) & 0xFF));
            if (0x1A == ch) {
                *p++ = ch;
            }
        }
    }

    *p++ = 0x1a;
    if (msgLen == MODES_SHORT_MSG_BYTES) {
        *p++ = '2';
    } else if (msgLen == MODES_LONG_MSG_BYTES) {
        *p++ = '3';
    } else if (msgLen == MODEAC_MSG_BYTES) {
        *p++ = '1';
    } else {
        return;
    }

    /* timestamp, big-endian */
    if (Modes.dump_reduce && mm->timestamp && !(mm->timestamp >= MAGIC_MLAT_TIMESTAMP && mm->timestamp <= MAGIC_MLAT_TIMESTAMP + 10)) {
        // clobber timestamp for better compression
        p = netTimestamp(p, MAGIC_ANY_TIMESTAMP);
    } else {
        p = netTimestamp(p, mm->timestamp);
    }

    sig = nearbyint(sqrt(mm->signalLevel) * 255);
    if (mm->signalLevel > 0 && sig < 1)
        sig = 1;
    if (sig > 255)
        sig = 255;
    *p++ = ch = (char) sig;
    if (0x1A == ch) {
        *p++ = ch;
    }

    for (j = 0; j < msgLen; j++) {
        *p++ = (ch = msg[j]);
        if (0x1A == ch) {
            *p++ = ch;
        }
    }

    int64_t now = mm->sysTimestamp;
    if (now > Modes.dump_next_ts) {
        //fprintf(stderr, "%ld\n", (long) now);
        Modes.dump_next_ts = now + 1;
        const char dump_ts_prefix[] = { 0x1A, 0xe8 };
        zstdFwPutData(Modes.dump_fw, (uint8_t *) dump_ts_prefix, sizeof(dump_ts_prefix));
        zstdFwPutData(Modes.dump_fw, (uint8_t *) &now, sizeof(int64_t));
    }

    zstdFwPutData(Modes.dump_fw, (uint8_t *) start, p - start);
}

static void send_heartbeat(struct net_service *service) {
    if (!service->writer || !service->heartbeat_out.msg) {
        return;
    }

    char *p = prepareWrite(service->writer, service->heartbeat_out.len);
    if (!p) {
        return;
    }

    memcpy(p, service->heartbeat_out.msg, service->heartbeat_out.len);
    p += service->heartbeat_out.len;
    completeWrite(service->writer, p);
}

//
//=========================================================================
//
// Turn an hex digit into its 4 bit decimal value.
// Returns -1 if the digit is not in the 0-F range.
//
static inline __attribute__((always_inline)) int hexDigitVal(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    else if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    else return -1;
}
//
//=========================================================================
//
// Print the two hex digits to a string for a single byte.
//
static inline __attribute__((always_inline)) void printHexDigit(char *p, unsigned char c) {
    const char hex_lookup[] = "0123456789ABCDEF";
    p[0] = hex_lookup[(c >> 4) & 0x0F];
    p[1] = hex_lookup[c & 0x0F];
}

//
//=========================================================================
//
// Write raw output to TCP clients
//
static void modesSendRawOutput(struct modesMessage *mm) {
    int msgLen = mm->msgbits / 8;
    char *p = prepareWrite(&Modes.raw_out, msgLen * 2 + 15);
    int j;
    unsigned char *msg = (Modes.net_verbatim ? mm->verbatim : mm->msg);

    if (!p)
        return;

    if (Modes.mlat && mm->timestamp) {
        /* timestamp, big-endian */
        sprintf(p, "@%012" PRIX64,
                mm->timestamp);
        p += 13;
    } else
        *p++ = '*';

    for (j = 0; j < msgLen; j++) {
        printHexDigit(p, msg[j]);
        p += 2;
    }

    *p++ = ';';
    *p++ = '\n';

    completeWrite(&Modes.raw_out, p);
}
//
// Read Asterix FSPEC
//
static uint8_t * readFspec(char **p){
    uint8_t* fspec = malloc(24*sizeof(uint8_t*));
    for (int i = 1; i < 24; i++) {
    	fspec[i] = 0;
    }
    fspec[0] = **p;
    (*p)++;
    for (int i = 1; *(*p - 1) & 0x1; i++){
    	fspec[i] = *(*p);
	(*p)++;
    }
    return fspec;
}

//
// Read Asterix Time
//
static uint64_t readAsterixTime(char **p) {
    int rawtime = ((*(*p) & 0xff) << 16) + ((*(*p + 1) & 0xff) << 8) + (*(*p + 2) & 0xff);
    int mssm = (int)(rawtime / .128);
    long midnight = (long)(mstime() / 86400000) * 86400000;
    int diff = (int)(midnight + mssm - mstime());
    (*p) += 3;
    if (abs(diff) > 43200000){
    	return midnight - 86400000 + mssm;
    }
    return midnight + mssm;
}

//
// Read Asterix High Precision Time
//
static void readAsterixHighPrecisionTime(uint64_t *timeStamp, char **p) {
    uint8_t fsi = (**p & 0xc0) >> 6;
    double offset = ((**p & 0x3f) << 24) + ((*(*p + 1) & 0xff) << 16) + ((*(*p + 2) & 0xff) << 8) + (*(*p + 3) & 0xff);
    (*p) += 4;
    offset = offset * pow(2, -27);
    uint64_t wholesecond = (int)(*timeStamp / 1000) * 1000;
    switch(fsi)
    {
    	case 1:
	    wholesecond += 1;
	    break;
	case 2:
	    wholesecond -= 1;
	    break;
    }
    *timeStamp = wholesecond + offset;
}

//
//=========================================================================
//
// Read ASTERIX input from TCP clients
//
static int decodeAsterixMessage(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb) {
    //uint16_t msgLen = (*(p + 1) << 8) + *(p + 2);
    //int j;
    unsigned char category;
    struct modesMessage *mm = netGetMM(mb);
    mm->client = c;
    MODES_NOTUSED(c);
    if (remote >= 64)
        mm->source = remote - 64;
    else
        mm->source = SOURCE_INDIRECT;
    mm->remote = 1;
    mm->sbs_in = 1;
    mm->signalLevel = 0;
    category = *p; // Get the category
    p += 3;
    uint8_t *fspec = readFspec(&p);
    mm->receiverId = c->receiverId;
    if (unlikely(Modes.incrementId)) {
        mm->receiverId += now / (10 * MINUTES);
    }
    mm->sysTimestamp = -1;
    switch(category){
        case 21: // ADS-B Message
            if(!(fspec[1] & 0x10)){ // no address. this is useless to us
                free(fspec);
                return -1;
            }
            if (fspec[0] & 0x80){ // ID021/010 Data Source Identification
                p += 2;
            }
            uint8_t addrtype = 3;
            if (fspec[0] & 0x40){ // ID021/040 Target Report Descriptor
                uint8_t *trd = readFspec(&p);
                addrtype = (trd[0] & 0xE0) >> 5;
                if (!(trd[0] & 0x18)){
                    mm->alt_q_bit = 1;
                }
                if (trd[1] & 0x40){
                    mm->airground = AG_GROUND;
                }
                else {
                    mm->airground = AG_AIRBORNE;
                }
                free(trd);
            }
            if (fspec[0] & 0x20){ // I021/161 Track Number
                p += 2;
            }
            if (fspec[0] & 0x10){ // I021/015 Service Identification
                p += 1;
            }
            if (fspec[0] & 0x8){ // I021/071 Time of Applicability for Position 3
                mm->sysTimestamp = readAsterixTime(&p);
            }
            if (fspec[0] & 0x4){ // I021/130 Position in WGS-84 co-ordinates
                int lat = (*p & 0xff) << 16;
                lat += (*(p + 1) & 0xff) << 8;
                lat += (*(p + 2) & 0xff);
                p += 3;
                int lon = (*p & 0xff) << 16;
                lon += (*(p + 1) & 0xff) << 8;
                lon += (*(p + 2) & 0xff);
                p += 3;
                if (lat >= 0x800000){
                    lat -= 0x1000000;
                }
                if (lon >= 0x800000){
                    lon -= 0x1000000;
                }
                double latitude = lat * (180 / pow(2, 23));
                double longitude = lon * (180 / pow(2, 23));
                if (latitude <= 90 && latitude >= -90 && longitude >= -180 && longitude <= 180){
                    mm->sbs_pos_valid = true;
                    mm->decoded_lat = latitude;
                    mm->decoded_lon = longitude;
                }
            }
            if (fspec[0] & 0x2){ // I021/131 Position in WGS-84 co-ordinates, high res.
                int lat = (*p & 0xff) << 24;
                lat += (*(p + 1) & 0xff) << 16;
                lat += (*(p + 2) & 0xff) << 8;
                lat += (*(p + 3) & 0xff);
                p += 4;
                int lon = *p << 24;
                lon += (*(p + 1) & 0xff) << 16;
                lon += (*(p + 2) & 0xff) << 8;
                lon += (*(p + 3) & 0xff);
                p += 4;
                double latitude = lat * (180 / pow(2, 30));
                double longitude = lon * (180 / pow(2, 30));
                if (latitude <= 90 && latitude >= -90 && longitude >= -180 && longitude <= 180){
                    mm->sbs_pos_valid = true;
                    mm->decoded_lat = latitude;
                    mm->decoded_lon = longitude;
                }
            }
            if (fspec[1] & 0x80){ // I021/072 Time of Applicability for Velocity
                if (mm->sysTimestamp == -1){
                    mm->sysTimestamp = readAsterixTime(&p);
                }
                else {
                    p += 3;
                }
            }
            if (fspec[1] & 0x40){ // I021/150 Air Speed
                uint16_t raw_speed = (*p & 0x7f) << 8;
                raw_speed += *(p + 1) & 0xff;
                if (*p & 0x80){ //Mach
                    mm->mach = raw_speed * 0.001;
                    mm->mach_valid = true;
                }
                else{ // IAS
                    mm->ias = (raw_speed * pow(2, -14)) * 3600;
                    mm->ias_valid = true;
                }
                p += 2;
            }
            if (fspec[1] & 0x20){ // I021/151 True Airspeed
                uint16_t raw_speed = (*p & 0x7f) << 8;
                raw_speed += *(p + 1) & 0xff;
                if (!(*p & 0x80)){
                    mm->tas_valid = true;
                    mm->tas = raw_speed;
                }
                p += 2;
            }
            // I021/080 Target Address
            mm->addr = (((*p & 0xff) << 16) + ((*(p + 1) & 0xff) << 8) + (*(p + 2) & 0xff)) & 0xffffff;
            if (addrtype == 3){
                mm->addr |= MODES_NON_ICAO_ADDRESS;
            }
            p += 3;
            if (fspec[1] & 0x8){ // I021/073 Time of Message Reception of Position
                if (mm->cpr_decoded || mm->sbs_pos_valid){
                    uint64_t ts = readAsterixTime(&p);
                    if (fspec[1] & 0x4){ // I021/074 Time of Message Reception of Position=High Precision
                        readAsterixHighPrecisionTime(&ts, &p);
                    }
                    if (mm->sysTimestamp == -1){
                        mm->sysTimestamp = ts;
                    }
                }
                else if (fspec[1] & 0x4) {
                    p += 7;
                }
                else {
                    p += 3;
                }
            }
            if (fspec[1] & 0x2){ // I021/075 Time of Message Reception of Velocity
                if (mm->ias_valid || mm->mach_valid || mm->gs_valid){
                    uint64_t ts = readAsterixTime(&p);
                    if (fspec[2] & 0x80){ // I021/076 Time of Message Reception of Velocity=High Precision
                        readAsterixHighPrecisionTime(&ts, &p);
                    }
                    if (mm->sysTimestamp == -1){
                        mm->sysTimestamp = ts;
                    }
                }
                else if (fspec[2] & 0x80) {
                    p += 7;
                }
                else {
                    p += 3;
                }
            }
            if (fspec[2] & 0x40){ // I021/140 Geometric Height
                int16_t raw_alt = (((*p & 0xff) << 8) + (*(p + 1) & 0xff));
                double alt = raw_alt * 6.25;
                if (alt >= -1500 && alt <= 150000){
                    mm->geom_alt_valid = true;
                    mm->geom_alt_unit = UNIT_FEET;
                    mm->geom_alt = alt;
                }
                p += 2;
            }
            uint8_t *qi;
            //uint8_t nucp_or_nic;
            uint8_t nucr_or_nacv;
            uint8_t nicbaro = 0;
            uint8_t sil;
            uint8_t nacp = 0;
            uint8_t sils;
            uint8_t sda = 0;
            uint8_t gva = 0;
            //uint8_t pic;
            if (fspec[2] & 0x20){ // I021/090 Quality Indicators
                qi = readFspec(&p);
                //nucp_or_nic = (qi[0] & 0x1e) >> 1;
                nucr_or_nacv = (qi[0] & 0xe0) >> 5;
                mm->accuracy.nac_v_valid = true;
                mm->accuracy.nac_v = nucr_or_nacv;
                if (qi[0] & 0x1){
                    nicbaro = (qi[1] & 0x80) >> 7;
                    sil = (qi[1] & 0x60) >> 5;
                    mm->accuracy.sil = sil;
                    nacp = (qi[1] & 0x1e) >> 1;
                    if (qi[1] & 0x1){
                        sils = (qi[2] & 0x20) >> 5;
                        if (sils){
                            mm->accuracy.sil_type = SIL_PER_SAMPLE;
                        }
                        else{
                            mm->accuracy.sil_type = SIL_PER_HOUR;
                        }
                        sda = (qi[2] & 0x18) >> 3;
                        gva = (qi[2] & 0x6) >> 1;
                        //pic = (qi[3] & 0xf0) >> 4;
                    }
                    else{
                        mm->accuracy.sil_type = SIL_UNKNOWN;
                    }
                }
                free(qi);
            }
            if (fspec[2] & 0x10){ // I021/210 MOPS Version
                mm->opstatus.valid = true;
                mm->opstatus.version = ((*p) & 0x38) >> 3;
                uint8_t ltt = (*p) & 0x7;
                p++;
                switch(mm->opstatus.version){
                    case 1:
                        mm->accuracy.nac_p_valid = true;
                        mm->accuracy.nac_p = nacp;
                        mm->accuracy.nic_baro_valid = true;
                        mm->accuracy.nic_baro = nicbaro;
                        mm->accuracy.sil_type = SIL_UNKNOWN;
                        break;
                    case 2:
                        mm->accuracy.nac_p_valid = true;
                        mm->accuracy.nac_p = nacp;
                        mm->accuracy.gva_valid = true;
                        mm->accuracy.gva = gva;
                        mm->accuracy.sda_valid = true;
                        mm->accuracy.sda = sda;
                        mm->accuracy.nic_baro_valid = true;
                        mm->accuracy.nic_baro = nicbaro;
                        break;
                }
                switch (ltt) {
                    case 0:
                        if (!addrtype){
                            mm->addrtype = ADDR_TISB_ICAO;
                        }
                        else{
                            mm->addrtype = ADDR_TISB_OTHER;
                        }
                        break;
                    case 1:
                        if (!addrtype){
                            mm->addrtype = ADDR_ADSR_ICAO;
                        }
                        else{
                            mm->addrtype = ADDR_ADSR_OTHER;
                        }
                        break;
                    case 2:
                        if (!addrtype){
                            mm->addrtype = ADDR_ADSB_ICAO;
                        }
                        else{
                            mm->addrtype = ADDR_ADSB_OTHER;
                        }
                        break;
                    default:
                        mm->addrtype = ADDR_UNKNOWN;
                        break;
                }
            }
            if (fspec[2] & 0x8){ // I021/070 Mode 3/A Code
                mm->squawk += (((*p & 0xe) << 11) + ((*p & 0x1) << 10) + ((*(p + 1) & 0xC0) << 2) + ((*(p + 1) & 0x38) << 1) + ((*(p + 1) & 0x7))) ;
                mm->squawk_valid = true;
                p += 2;
            }
            if (fspec[2] & 0x4){ // I021/230 Roll Angle
                int16_t roll = ((*p & 0xff) << 8) + (*(p + 1) & 0xff);
                mm->roll = roll * 0.01;
                mm->roll_valid = true;
                p += 2;
            }
            if (fspec[2] & 0x2){ // I021/145 Flight Level
                int16_t alt = ((*p & 0xff) << 8) + (*(p + 1) & 0xff);
                mm->baro_alt_valid = true;
                mm->baro_alt = alt * 25;
                mm->baro_alt_unit = UNIT_FEET;
                p += 2;
            }
            if (fspec[3] & 0x80){ // I021/152 Magnetic Heading
                mm->heading_valid = true;
                mm->heading_type = HEADING_MAGNETIC;
                uint16_t heading = ((*p & 0xff) << 8) + (*(p + 1) & 0xff);
                mm->heading = heading * (360 / pow(2, 16));
                p += 2;
            }
            if (fspec[3] & 0x40){ // I021/200 Target Status
                mm->spi_valid = true;
                mm->alert_valid = true;
                mm->emergency_valid = true;
                mm->nav.modes_valid = true;
                mm->nav.modes |= (*p & 0b01000000) >> 4;
                mm->emergency = (*p & 0b00011100) >> 2;
                mm->alert = (*p & 0b11);
                mm->spi = (*p & 0b11) == 3;
                p++;
            }
            if (fspec[3] & 0x20){ // ID021/155 Barometric Vertical Rate
                if (*p & 0x80){ //range exceeded
                    p += 2;
                }
                else{
                    int16_t vr = ((*p & 0x7f) << 9) + ((*(p + 1) & 0xff) << 1);
                    mm->baro_rate_valid = true;
                    mm->baro_rate = vr * 3.125;
                    p += 2;
                }
            }
            if (fspec[3] & 0x10){ // ID021/157 Geometric Vertical Rate
                if (*p & 0x80){ //range exceeded
                    p += 2;
                }
                else{
                    int16_t vr = ((*p & 0x7f) << 9) + ((*(p + 1) & 0xff) << 1);
                    mm->geom_rate_valid = true;
                    mm->geom_rate = vr * 3.125;
                    p += 2;
                }
            }
            if (fspec[3] & 0x8){ // ID021/160 Airborne Ground Vector
                if (*p & 0x80){ //range exceeded
                    p += 4;
                }
                else{
                    uint16_t gs = ((*p & 0x7f) << 8) + ((*(p + 1) & 0xff));
                    p += 2;
                    uint16_t ta = ((*p & 0xff) << 8) + ((*(p + 1) & 0xff));
                    p += 2;
                    mm->gs_valid = true;
                    mm->heading_valid = true;
                    mm->heading_type = HEADING_GROUND_TRACK;
                    mm->gs.v0 = gs * pow(2, -14) * 3600;
                    mm->heading = ta * (360 / pow(2, 16));
                }
            }
            if (fspec[3] & 0x4){ // ID021/165 Track Angle Rate
                p += 2;
            }
            if (fspec[3] & 0x2){ // ID021/077 Time of Report Transmission
                uint64_t tt = readAsterixTime(&p);
                if (mm->sysTimestamp == -1){
                    mm->sysTimestamp = tt;
                }
            }
            if (fspec[4] & 0x80){ // ID021/170 Target Identification
                uint64_t cs = ((uint64_t)(*p & 0xff) << 40) + ((uint64_t)(*(p + 1) & 0xff) << 32) + ((uint64_t)(*(p + 2) & 0xff) << 24) + ((uint64_t)(*(p + 3) & 0xff) << 16) + ((uint64_t)(*(p + 4) & 0xff) << 8) + (uint64_t)(*(p + 5) & 0xff);
                char *callsign = mm->callsign;
                callsign[0] = ais_charset[((cs & 0xFC0000000000) >> 42)];
                callsign[1] = ais_charset[((cs & 0x3F000000000) >> 36)];
                callsign[2] = ais_charset[((cs & 0xFC0000000) >> 30)];
                callsign[3] = ais_charset[((cs & 0x3F000000) >> 24)];
                callsign[4] = ais_charset[((cs & 0xFC0000) >> 18)];
                callsign[5] = ais_charset[((cs & 0x3F000) >> 12)];
                callsign[6] = ais_charset[((cs & 0xFC0) >> 6)];
                callsign[7] = ais_charset[(cs & 0x3F)];
                callsign[8] = 0;
                mm->callsign_valid = 1;
                for (int i = 0; i < 8; ++i) {
                    if (
                            (callsign[i] >= 'A' && callsign[i] <= 'Z')
                            // -./0123456789
                            || (callsign[i] >= '-' && callsign[i] <= '9')
                            || callsign[i] == ' '
                            || callsign[i] == '@'
                       ) {
                        // valid chars
                    } else {
                        mm->callsign_valid = 0;
                    }
                }
                p += 6;
            }
            if (fspec[4] & 0x40){ // ID021/020 Emitter Category
                int tc = 0;
                int ca = 0;
                uint8_t ecat = *p++ & 0xFF;
                switch (ecat) {
                    case 0:
                        tc = 0x0e;
                        ca = 0;
                        break;
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                        tc = 4;
                        ca = ecat;
                        break;
                    case 10:
                        tc = 4;
                        ca = 7;
                        break;
                    case 11:
                        tc = 3;
                        ca = 1;
                        break;
                    case 12:
                        tc = 3;
                        ca = 2;
                        break;
                    case 13:
                        tc = 3;
                        ca = 6;
                        break;
                    case 14:
                        tc = 3;
                        ca = 7;
                        break;
                    case 15:
                        tc = 3;
                        ca = 4;
                        break;
                    case 16:
                        tc = 3;
                        ca = 3;
                        break;
                    case 20:
                        tc = 2;
                        ca = 1;
                        break;
                    case 21:
                        tc = 2;
                        ca = 3;
                        break;
                    case 22:
                        tc = 2;
                        ca = 4;
                        break;
                    case 23:
                        tc = 2;
                        ca = 5;
                        break;
                    case 24:
                        tc = 2;
                        ca = 6;
                        break;
                }
                mm->category = ((0x0E - tc) << 4) | ca;
                mm->category_valid = 1;
            }

            if (fspec[4] & 0x20) { // I021/220 Met Information
                uint8_t *met = readFspec(&p);
                free(met);
            }

            if (fspec[4] & 0x10) { // I021/146 Selected Altitude
                if (*p & 0x80 && *p & 0x60) {
                    int16_t alt = (*p & 0x1F) << 8;
                    alt += *(p + 1) & 0xFF;
                    if (alt < 0x1000){
                        if ((*p & 0x60) == 0x40) { // MCP
                            mm->nav.mcp_altitude_valid = 1;
                            mm->nav.mcp_altitude = alt * 25;
                        }
                        else if ((*p & 0x60) == 0x60) { //FMS
                            mm->nav.fms_altitude_valid = 1;
                            mm->nav.fms_altitude = alt * 25;
                        }
                    }
                }
                p += 2;
            }
            netUseMessage(mm);
            break;
    }
    free(fspec);
    if (mm->sysTimestamp == -1){
        mm->sysTimestamp = mstime();
    }
    //mm->decoded_nic = 0;
    //mm->decoded_rc = RC_UNKNOWN;
    return 0;
}



//
//=========================================================================
//
// Write ASTERIX output to TCP clients
//
static void modesSendAsterixOutput(struct modesMessage *mm, struct net_writer *writer) {
    int64_t now = mstime();
    uint8_t category;
    unsigned char bytes[Modes.net_output_flush_size * 2];
    for (int i = 0; i < Modes.net_output_flush_size * 2; i++){
        bytes[i] = 0;
    }
    uint8_t fspec[7];
    for (size_t i = 0; i < 7; i++)
    {
        fspec[i] = 0;
    }
    int p = 0;
    if (mm->from_mlat) // CAT 20
        return;
    if (mm->from_tisb)
        return;
    else { // CAT 21
        category = 21;

        // I021/010 Data Source Identification
        fspec[0] |= 1 << 7;
        bytes[p++] = 000; //SAC
        bytes[p++] = 001; //SIC

        // I021/040 Target Report Descriptor
        fspec[0] |= 1 << 6;
        if (mm->addr & MODES_NON_ICAO_ADDRESS){
            bytes[p] |= (3 << 5);
        }
        else if (mm->addrtype == ADDR_ADSB_OTHER || mm->addrtype == ADDR_TISB_OTHER || mm->addrtype == ADDR_ADSR_OTHER){
            bytes[p] |= (2 << 5);
        }

        if (mm->alt_q_bit == 0)
            bytes[p] |= (1 << 3);

        if (mm->airground == AG_GROUND)
            bytes[p + 1] |= 1 << 6;
        if (bytes[p + 1]){
            bytes[p] |= 1;
            p++;
        }
        p++;

        // I021/130 Position in WGS-84 co-ordinates
        if (mm->cpr_decoded || mm->sbs_pos_valid){
            fspec[0] |= 1 << 2;
            int32_t lat;
            int32_t lon;
            lat = mm->decoded_lat / (180 / pow(2,23));
            lon = mm->decoded_lon / (180 / pow(2,23));
            if (lat < 0){
                lat += 0x1000000;
            }
            if (lon < 0){
                lon += 0x1000000;
            }
            bytes[p++] = (lat & 0xFF0000) >> 16;
            bytes[p++] = (lat & 0xFF00) >> 8;
            bytes[p++] = (lat & 0xFF);
            bytes[p++] = (lon & 0xFF0000) >> 16;
            bytes[p++] = (lon & 0xFF00) >> 8;
            bytes[p++] = (lon & 0xFF);
        }

        // I021/131 Position in WGS-84 co-ordinates, high res.
        /*
           if (mm->cpr_decoded || mm->sbs_pos_valid){
           fspec[0] |= 1 << 1;
           int32_t lat;
           int32_t lon;
           lat = mm->decoded_lat / (180 / pow(2,30));
           lon = mm->decoded_lon / (180 / pow(2,30));
           bytes[p++] = (lat & 0xFF000000) >> 24;
           bytes[p++] = (lat & 0xFF0000) >> 16;
           bytes[p++] = (lat & 0xFF00) >> 8;
           bytes[p++] = (lat & 0xFF);
           bytes[p++] = (lon & 0xFF000000) >> 24;
           bytes[p++] = (lon & 0xFF0000) >> 16;
           bytes[p++] = (lon & 0xFF00) >> 8;
           bytes[p++] = (lon & 0xFF);
           }
           */
        // I021/150 Air Speed
        if(mm->ias_valid || mm->mach_valid){
            fspec[1] |= 1 << 6;
            uint16_t speedval;
            if (mm->mach_valid){
                bytes[p] = (1 << 7);
                speedval = mm->mach * 1000;
            }
            else{
                speedval = (mm->ias / 3600.0) * pow(2,14);
            }
            bytes[p++] |= (speedval & 0x7f00) >> 8;
            bytes[p++] = (speedval & 0xff);
        }

        // I021/151 True Air Speed
        if(mm->tas_valid){
            fspec[1] |= 1 << 5;
            bytes[p++] = (mm->tas & 0x7f00) >> 8;
            bytes[p++] = (mm->tas & 0xff);
        }

        // I021/080 Target Address
        fspec[1] |= 1 << 4;
        bytes[p++] = (mm->addr & 0xff0000) >> 16;
        bytes[p++] = (mm->addr & 0xff00) >> 8;
        bytes[p++] = (mm->addr & 0xff);
        struct aircraft *a = aircraftGet(mm->addr);
        if (!a) { // If it's a currently unknown aircraft....
            a = aircraftCreate(mm->addr); // ., create a new record for it,
        }

        // I021/073 Time of Message Reception of Position
        if (fspec[0] & 0b110){
            fspec[1] |= 1 << 3;
            long midnight = (long)(time(NULL) / 86400) * 86400000;
            int tsm = (mm->sysTimestamp) - midnight;
            if (tsm < 0)
                tsm += 86400000;
            tsm = (int)(tsm * 0.128);
            bytes[p++] = (tsm & 0xff0000) >> 16;
            bytes[p++] = (tsm & 0xff00) >> 8;
            bytes[p++] = tsm & 0xff;
        }

        //  I021/075 Time of Message Reception of Velocity
        if (mm->gs_valid && mm->heading_valid && mm->heading_type == HEADING_GROUND_TRACK){
            fspec[1] |= 1 << 1;
            long midnight = (long)(time(NULL) / 86400) * 86400000;
            int tsm = (mm->sysTimestamp) - midnight;
            if (tsm < 0)
                tsm += 86400000;
            tsm = (int)(tsm * 0.128);
            bytes[p++] = (tsm & 0xff0000) >> 16;
            bytes[p++] = (tsm & 0xff00) >> 8;
            bytes[p++] = tsm & 0xff;
        }

        // I021/140 Geometric Height
        if (mm->geom_alt_valid){
            fspec[2] |= 1 << 6;
            int16_t alt;
            if(mm->geom_alt_unit == UNIT_FEET)
                alt = mm->geom_alt / 6.25;
            else
                alt = mm->geom_alt / 20.5053;
            bytes[p++] = (alt & 0xff00) >> 8;
            bytes[p++] = alt & 0xff;
        }
        else if (mm->geom_delta_valid){
            fspec[2] |= 1 << 6;
            int16_t alt = (int)((a->baro_alt + mm->geom_delta) / 6.25);
            bytes[p++] = (alt & 0xff00) >> 8;
            bytes[p++] = alt & 0xff;
        }
        // I021/090 Quality Indicators
        fspec[2] |= 1 << 5;
        if (mm->accuracy.nac_v_valid)
            bytes[p] += mm->accuracy.nac_v << 5;
        if (mm->cpr_decoded)
            bytes[p] |= mm->cpr_nucp << 1;
        if (mm->accuracy.nic_baro_valid)
            bytes[p + 1] |= mm->accuracy.nic_baro << 7;
        if (mm->accuracy.sil_type != SIL_INVALID)
            bytes[p + 1] |= mm->accuracy.sil << 5;
        if (mm->accuracy.nac_p_valid)
            bytes[p + 1] |= mm->accuracy.nac_p << 1;
        if (bytes[p + 1]){
            bytes[p] |= 1;
            p++;
        }
        if (mm->accuracy.sil_type == SIL_PER_SAMPLE)
            bytes[p + 1] |= 1 << 5;
        if (mm->accuracy.sda_valid)
            bytes[p + 1] |= mm->accuracy.sda << 3;
        if (mm->accuracy.gva_valid)
            bytes[p + 1] |= mm->accuracy.gva << 1;
        if (bytes[p + 1]){
            bytes[p] |= 1;
            p++;
        }
        p++;

        // I021/210 MOPS Version
        if (mm->opstatus.valid){
            fspec[2] |= 1 << 4;

            if (mm->remote) {
                switch (mm->addrtype){
                    case ADDR_ADSB_ICAO:
                    case ADDR_ADSB_OTHER:
                        bytes[p] = 2;
                        break;
                    case ADDR_ADSR_ICAO:
                    case ADDR_ADSR_OTHER:
                        bytes[p] = 1;
                        break;
                    default:
                        bytes[p] = 0;
                        break;
                }
            }
            else {
                switch (mm->source){
                    case SOURCE_ADSB:
                        bytes[p] = 2;
                        break;
                    case SOURCE_ADSR:
                        bytes[p] = 1;
                        break;
                    default:
                        bytes[p] = 0;
                        break;
                }
            }
            bytes[p++] |= (mm->opstatus.version) << 3;
        }

        // I021/070 Mode 3/A Code
        if(mm->squawk_valid){
            fspec[2] |= 1 << 3;
            uint16_t squawk = mm->squawk;
            bytes[p]   |= ((squawk & 0x7000)) >> 11;
            bytes[p++] |= ((squawk & 0x0400)) >> 10;
            bytes[p]   |= ((squawk & 0x0300)) >> 2;
            bytes[p]   |= ((squawk & 0x0070)) >> 1;
            bytes[p++] |= ((squawk & 0x0007));
        }

        // I021/230 Roll Angle
        if(mm->roll_valid){
            fspec[2] |= 1 << 2;
            int16_t roll = mm->roll * 100;
            bytes[p++] = (roll & 0xFF00) >> 8;
            bytes[p++] = (roll & 0xFF);
        }

        // I021/145 Flight Level
        if(mm->baro_alt_valid){
            fspec[2] |= 1 << 1;
            int16_t value = mm->baro_alt / 25;
            if (mm->baro_alt_unit == UNIT_METERS)
                value = (int)(mm-> baro_alt * 3.2808);
            bytes[p++] = (value & 0xff00) >> 8;
            bytes[p++] = value & 0xff;
        }

        // I021/152 Magnetic Heading
        if(mm->heading_valid && mm->heading_type == HEADING_MAGNETIC){
            fspec[3] |= 1 << 7;
            double adj_trk = mm->heading * 182.0444;
            uint16_t trk = (int)adj_trk;
            bytes[p++] = (trk & 0xff00) >> 8;
            bytes[p++] = trk & 0xff;
        }

        // I021/200 Target Status
        if (mm->spi_valid || mm->alert_valid || mm->emergency_valid || mm->nav.modes_valid){
            fspec[3] |= 1 << 6;
            if (mm->nav.modes_valid){
                if (mm->nav.modes & 0b00000010)
                    bytes[p] |= 1 << 6;
            }
            if (mm->emergency_valid)
                bytes[p] |= (mm->emergency << 2);
            if (mm->alert_valid)
                bytes[p] |= (mm->alert);
            else if (mm->spi_valid && mm->spi)
                bytes[p] |=3;
            p++;
        }

        // I021/155 Barometric Vertical Rate
        if (mm->baro_rate_valid){
            fspec[3] |= 1 << 5;
            int value = ((int16_t)(mm->baro_rate / 3.125)) >> 1;
            bytes[p++] = (value & 0x7f00) >> 8;
            bytes[p++] = value & 0xff;
        }

        // I021/157 Geometric Vertical Rate
        if (mm->geom_rate_valid){
            fspec[3] |= 1 << 4;
            int value = ((int16_t)(mm->geom_rate / 3.125)) >> 1;
            bytes[p++] = (value & 0x7f00) >> 8;
            bytes[p++] = value & 0xff;
        }

        // I021/160 Airborne Ground Vector
        if (mm->gs_valid && mm->heading_valid && mm->heading_type == HEADING_GROUND_TRACK){
            fspec[3] |= 1 << 3;
            double adj_gs = mm->gs.v0 * 4.5511;
            bytes[p++] = ((int)adj_gs & 0x7f00) >> 8;
            bytes[p++] = (int)adj_gs & 0xff;
            double adj_trk = mm->heading * (pow(2,16) / 360.0);
            uint16_t trk = (int)adj_trk;
            bytes[p++] = (trk & 0xff00) >> 8;
            bytes[p++] = trk & 0xff;
        }

        // I021/077 Time of Report Transmission
        {
            fspec[3] |= 1 << 1;
            long midnight = (long)(time(NULL) / 86400) * 86400000;
            int tsm = now - midnight;
            if (tsm < 0)
                tsm += 86400000;
            tsm = (int)(tsm * 0.128);
            bytes[p++] = (tsm & 0xff0000) >> 16;
            bytes[p++] = (tsm & 0xff00) >> 8;
            bytes[p++] = tsm & 0xff;
        }

        // I021/170 Target Identification
        if(mm->callsign_valid){
            fspec[4] |= 1 << 7;
            uint64_t enc_callsign = 0;
            for (int i = 0; i <= 7; i++)
            {
                uint8_t ch = char_to_ais(mm->callsign[i]);
                enc_callsign = (enc_callsign << 6) + (ch & 0x3F);
            }
            bytes[p++] = (enc_callsign & 0xff0000000000) >> 40;
            bytes[p++] = (enc_callsign & 0xff00000000) >> 32;
            bytes[p++] = (enc_callsign & 0xff000000) >> 24;
            bytes[p++] = (enc_callsign & 0xff0000) >> 16;
            bytes[p++] = (enc_callsign & 0xff00) >> 8;
            bytes[p++] = (enc_callsign & 0xff);
        }

        // I021/020 Emitter Category
        if (mm->category_valid){
            fspec[4] |= 1 << 6;
            int tc = 0x0e - ((mm->category & 0x1F0) >> 4);
            int ca = mm->category & 7;
            if (ca){
                switch (tc) {
                    case 1:
                        break;
                    case 2:
                        switch (ca){
                            case 1:
                                bytes[p++] = 20;
                                break;
                            case 3:
                                bytes[p++] = 21;
                                break;
                            case 4:
                            case 5:
                            case 6:
                            case 7:
                                bytes[p++] = 22;
                                break;
                        }
                        break;
                    case 3:
                        switch (ca){
                            case 1:
                                bytes[p++] = 11;
                                break;
                            case 2:
                                bytes[p++] = 12;
                                break;
                            case 3:
                                bytes[p++] = 16;
                                break;
                            case 4:
                                bytes[p++] = 15;
                                break;
                            case 6:
                                bytes[p++] = 13;
                                break;
                            case 7:
                                bytes[p++] = 14;
                                break;
                        }
                        break;
                    case 4:
                        switch (ca){
                            case 1:
                            case 2:
                            case 3:
                            case 4:
                            case 5:
                            case 6:
                                bytes[p++] = ca;
                                break;
                            case 7:
                                bytes[p++] = 10;
                        }
                        break;
                }
            } else {
                bytes[p++] = 0;
            }
        }
        else if (!(a->category)){
            fspec[4] |= 1 << 6;
            bytes[p++] = 0;
        }
        // I021/220 Met Information
        //if (ac && ((now < ac->oat_updated + TRACK_EXPIRE) || (now < ac->wind_updated + TRACK_EXPIRE && abs(ac->wind_altitude - ac->baro_alt) < 500))){}
        if (mm->wind_valid || mm->oat_valid || mm->turbulence_valid || mm->static_pressure_valid || mm->humidity_valid) {
            fspec[4] |= 1 << 5;
            bool wind = false;
            bool temp = false;
            //if (now < ac->wind_updated + TRACK_EXPIRE && abs(ac->wind_altitude - ac->baro_alt) < 500){}
            if (mm->wind_valid) {
                bytes[p] |= 0xC0;
                wind = true;
            }
            //if (now < ac->oat_updated + TRACK_EXPIRE){}
            if (mm->oat_valid) {
                bytes[p] |= 0x20;
                temp = true;
            }
            p++;
            if (wind){
                uint16_t ws = (int)(mm->wind_speed);
                uint16_t wd = (int)(mm->wind_direction);
                bytes[p++] = (ws & 0xFF00) >> 8;
                bytes[p++] = ws & 0xFF;
                bytes[p++] = (wd & 0xFF00) >> 8;
                bytes[p++] = wd & 0xFF;
            }
            if (temp){
                int16_t oat = (int16_t)((mm->oat) * 4);
                bytes[p++] = (oat & 0xFF00) >> 8;
                bytes[p++] = oat & 0xFF;
            }
        }

        // I021/146 Selected Altitude
        if (mm->nav.fms_altitude_valid || mm->nav.mcp_altitude_valid){
            fspec[4] |= 1 << 4;
            int alt = 0;
            if (mm->nav.mcp_altitude_valid){
                alt = mm->nav.mcp_altitude;
                bytes[p] |= 0xC0;
            }
            else if (mm->nav.fms_altitude_valid){
                alt = mm->nav.fms_altitude;
                bytes[p] |= 0xE0;
            }
            alt /= 25;
            bytes[p++] |= (alt & 0x1F00) >> 8;
            bytes[p++] = (alt & 0xFF);
        }

        // I021/008 Aircraft Operational Status
        if (mm->opstatus.valid){
            if (mm->opstatus.om_acas_ra || mm->opstatus.cc_tc ||
                    mm->opstatus.cc_ts || mm->opstatus.cc_arv || mm->opstatus.cc_cdti
                    || (!mm->opstatus.cc_acas)){
                fspec[5] |= 1 << 7;
                bytes[p] |= (mm->opstatus.om_acas_ra & 0x1) << 7;
                bytes[p] |= (mm->opstatus.cc_tc & 0x3) << 5;
                bytes[p] |= (mm->opstatus.cc_ts & 0x1) << 4;
                bytes[p] |= (mm->opstatus.cc_arv & 0x1) << 3;
                bytes[p] |= (mm->opstatus.cc_cdti & 0x1) << 2;
                bytes[p] |= ((!mm->opstatus.cc_acas) & 0x1) << 1;
                p++;
            }
        }

        // I021/400 Receiver ID
        if (mm->receiverId){
            fspec[5] |= 1 << 2;
            bytes[p++] = (mm->receiverId) & 0xFF;
        }

        // I021/295 Data Ages
        /*
           if (fspec[4] == 0b100000){
           fspec[5] |= 1 << 1;
           bytes[p++] |= 1;
           bytes[p++] |= 1;
           bytes[p++] |= 1 << 2;
           if((now < ac->wind_updated + TRACK_EXPIRE && abs(ac->wind_altitude - ac->baro_alt) < 500) &&
           (now < ac->oat_updated + TRACK_EXPIRE)) {
           uint64_t wind_age = now - ac->wind_updated;
           uint64_t oat_age = now - ac->oat_updated;
           if (wind_age > oat_age){
           bytes[p++] = (int)(wind_age / 100);
           }
           else {
           bytes[p++] = (int)(oat_age / 100);
           }
           }
           else if (now < ac->wind_updated + TRACK_EXPIRE && abs(ac->wind_altitude - ac->baro_alt) < 500){
           uint64_t wind_age = now - ac->wind_updated;
           bytes[p++] = (int)(wind_age / 100);
           }
           else if (now < ac->oat_updated + TRACK_EXPIRE){
           uint64_t oat_age = now - ac->oat_updated;
           bytes[p++] = (int)(oat_age / 100);
           }
           }
           */

        int fspec_len = 1;
        for (int i = 5; i >= 0; i--)
        {
            if (fspec[i + 1]){
                fspec[i] |= 1;
                fspec_len++;
            }
        }

        uint16_t msgLen = p + 3 + fspec_len;
        uint8_t msgLenA = (msgLen & 0xFF00) >> 8;
        uint8_t msgLenB = msgLen & 0xFF;
        char *w = prepareWrite(writer, msgLen);
        memcpy(w, &category, 1);
        w++;
        memcpy(w, &msgLenA, 1);
        memcpy(w + 1, &msgLenB, 1);
        w+=2;
        memcpy(w, &fspec, fspec_len);
        w += fspec_len;
        memcpy(w, &bytes, p);
        w += p;
        completeWrite(writer, w);

    }
}

//
//=========================================================================
//
// Read SBS input from TCP clients
//
static int decodeSbsLine(struct client *c, char *line, int remote, int64_t now, struct messageBuffer *mb) {
    size_t line_len = strlen(line);
    size_t max_len = 200;

    if (Modes.receiver_focus && c->receiverId != Modes.receiver_focus)
        return 0;
    if (line_len < 2) // heartbeat
        return 0;
    if (line_len < 20 || line_len >= max_len)
        goto basestation_invalid;

    struct modesMessage *mm = netGetMM(mb);
    mm->client = c;

    char *p = line;
    char *t[23]; // leave 0 indexed entry empty, place 22 tokens into array

    MODES_NOTUSED(c);
    if (remote >= 64)
        mm->source = remote - 64;
    else
        mm->source = SOURCE_SBS;

    switch(mm->source) {
        case SOURCE_SBS:
            mm->addrtype = ADDR_OTHER;
            break;
        case SOURCE_MLAT:
            mm->addrtype = ADDR_MLAT;
            break;
        case SOURCE_JAERO:
            mm->addrtype = ADDR_JAERO;
            break;
        case SOURCE_PRIO:
            mm->addrtype = ADDR_OTHER;
            break;

        default:
            mm->addrtype = ADDR_OTHER;
    }

    // Mark messages received over the internet as remote so that we don't try to
    // pass them off as being received by this instance when forwarding them
    mm->remote = 1;
    mm->signalLevel = 0;
    mm->sbs_in = 1;

    // sample message from mlat-client basestation output
    //MSG,3,1,1,4AC8B3,1,2019/12/10,19:10:46.320,2019/12/10,19:10:47.789,,36017,,,51.1001,10.1915,,,,,,
    //
    for (int i = 1; i < 23; i++) {
        t[i] = strsep(&p, ",");
        if (!p && i < 22)
            goto basestation_invalid;
    }

    // check field 1
    if (!t[1] || strcmp(t[1], "MSG") != 0)
        goto basestation_invalid;

    if (!t[2] || strlen(t[2]) != 1)
        goto basestation_invalid;

    mm->sbsMsgType = atoi(t[2]);

    if (!t[5] || strlen(t[5]) != 6) // icao must be 6 characters
        goto basestation_invalid;

    char *icao = t[5];
    unsigned char *chars = (unsigned char *) &(mm->addr);
    for (int j = 0; j < 6; j += 2) {
        int high = hexDigitVal(icao[j]);
        int low = hexDigitVal(icao[j + 1]);

        if (high == -1 || low == -1)
            goto basestation_invalid;

        chars[2 - j / 2] = (high << 4) | low;
    }

    //fprintf(stderr, "%x type %s: ", mm->addr, t[2]);
    //fprintf(stderr, "%x: %d, %0.5f, %0.5f\n", mm->addr, mm->baro_alt, mm->decoded_lat, mm->decoded_lon);
    //field 11, callsign
    if (t[11] && strlen(t[11]) > 0) {
        strncpy(mm->callsign, t[11], 9);
        mm->callsign[8] = '\0';
        mm->callsign_valid = 1;
        for (unsigned i = 0; i < 8; ++i) {
            if (mm->callsign[i] == '\0')
                mm->callsign[i] = ' ';
            if (!(mm->callsign[i] >= 'A' && mm->callsign[i] <= 'Z') &&
                    !(mm->callsign[i] >= '0' && mm->callsign[i] <= '9') &&
                    mm->callsign[i] != ' ') {
                // Bad callsign, ignore it
                mm->callsign_valid = 0;
                break;
            }
        }
        //fprintf(stderr, "call: %s, ", mm->callsign);
    }
    // field 12, altitude
    if (t[12] && strlen(t[12]) > 0) {
        mm->baro_alt = atoi(t[12]);
        if (mm->baro_alt > -5000 && mm->baro_alt < 100000) {
            mm->baro_alt_valid = 1;
            mm->baro_alt_unit = UNIT_FEET;
        }
        //fprintf(stderr, "alt: %d, ", mm->baro_alt);
    }
    // field 13, groundspeed
    if (t[13] && strlen(t[13]) > 0) {
        mm->gs.v0 = strtod(t[13], NULL);
        if (mm->gs.v0 > 0)
            mm->gs_valid = 1;
        //fprintf(stderr, "gs: %.1f, ", mm->gs.selected);
    }
    //field 14, heading
    if (t[14] && strlen(t[14]) > 0) {
        mm->heading_valid = 1;
        mm->heading = strtod(t[14], NULL);
        mm->heading_type = HEADING_GROUND_TRACK;
        //fprintf(stderr, "track: %.1f, ", mm->heading);
    }
    // field 15 and 16, position
    if (t[15] && strlen(t[15]) && t[16] && strlen(t[16])) {
        mm->decoded_lat = strtod(t[15], NULL);
        mm->decoded_lon = strtod(t[16], NULL);
        if (mm->decoded_lat != 0 && mm->decoded_lon != 0)
            mm->sbs_pos_valid = 1;
        //fprintf(stderr, "pos: (%.2f, %.2f)\n", mm->decoded_lat, mm->decoded_lon);
    }
    // field 17 vertical rate, assume baro
    if (t[17] && strlen(t[17]) > 0) {
        mm->baro_rate = atoi(t[17]);
        mm->baro_rate_valid = 1;
        //fprintf(stderr, "vRate: %d, ", mm->baro_rate);
    }
    // field 18 squawk
    if (t[18] && strlen(t[18]) > 0) {
        long int tmp = strtol(t[18], NULL, 10);
        if (tmp > 0) {
            mm->squawk = (tmp / 1000) * 16*16*16 + (tmp / 100 % 10) * 16*16 + (tmp / 10 % 10) * 16 + (tmp % 10);
            mm->squawk_valid = 1;
            //fprintf(stderr, "squawk: %04x %s, ", mm->squawk, t[18]);
        }
    }
    // field 19 (originally squawk change) used to indicate by some versions of mlat-server the number of receivers which contributed to the postiions
    if (t[19] && strlen(t[19]) > 0) {
        long int tmp = strtol(t[19], NULL, 10);
        if (tmp > 0 && mm->source == SOURCE_MLAT) {
            mm->receiverCountMlat = tmp;
        } else if (!strcmp(t[19], "0")) {
            mm->alert_valid = 1;
            mm->alert = 0;
        } else if (!strcmp(t[19], "-1")) {
            mm->alert_valid = 1;
            mm->alert = 1;
        }
    }

    // field 20 (originally emergency status) used to indicate by some versions of mlat-server the estimated error in km
    if (t[20] && strlen(t[20]) > 0) {
        long tmp = strtol(t[20], NULL, 10);
        if (tmp > 0 && mm->source == SOURCE_MLAT) {
            mm->mlatEPU = tmp;
            if (tmp > UINT16_MAX)
                mm->mlatEPU = UINT16_MAX;

            //fprintf(stderr, "mlatEPU: %d\n", mm->mlatEPU);
        } else if (!strcmp(t[21], "0")) {
            mm->squawk_emergency_valid = 1;
            mm->squawk_emergency = 0;
        } else if (!strcmp(t[21], "-1")) {
            mm->squawk_emergency_valid = 1;
            mm->squawk_emergency = 1;
        }
    }

    // Field 21 is the Squawk Ident flag
    if (t[21] && strlen(t[21]) > 0) {
        if (!strcmp(t[21], "1")) {
            mm->spi_valid = 1;
            mm->spi = 1;
        } else if (!strcmp(t[21], "0")) {
            mm->spi_valid = 1;
            mm->spi = 0;
        }
    }

    // field 22 ground status
    if (t[22] && strlen(t[22]) > 0) {
        if (!strncmp(t[22], "-1", 2)) {
            mm->airground = AG_GROUND;
        } else if (!strncmp(t[22], "0", 1)) {
            mm->airground = AG_AIRBORNE;
        }
        //fprintf(stderr, "onground, ");
    }


    // set nic / rc to 0 / unknown
    mm->decoded_nic = 0;
    mm->decoded_rc = RC_UNKNOWN;

    //fprintf(stderr, "\n");

    // record reception time as the time we read it.
    mm->sysTimestamp = now;

    netUseMessage(mm);

    Modes.stats_current.remote_received_basestation_valid++;

    return 0;

basestation_invalid:

    if (Modes.debug_garbage) {
        for (size_t i = 0; i < line_len; i++) {
            line[i] = (line[i] == '\0' ? ',' : line[i]);
        }
        fprintf(stderr, "SBS invalid: %.*s (anything over 200 characters cut)\n", (int) imin(200, line_len), line);
    }
    Modes.stats_current.remote_received_basestation_invalid++;
    return 0;
}
//
//=========================================================================
//
// Write SBS output to TCP clients
//
static void modesSendSBSOutput(struct modesMessage *mm, struct aircraft *a, struct net_writer *writer) {
    char *p;
    struct timespec now;
    struct tm stTime_receive, stTime_now;
    int msgType;

    // For now, suppress non-ICAO addresses
    if (mm->addr & MODES_NON_ICAO_ADDRESS)
        return;

    p = prepareWrite(writer, 200);
    if (!p)
        return;

    //
    // SBS BS style output checked against the following reference
    // http://www.homepages.mcb.net/bones/SBS/Article/Barebones42_Socket_Data.htm - seems comprehensive
    //

    if (mm->sbs_in) {
        msgType = mm->sbsMsgType;
    } else {
        // Decide on the basic SBS Message Type
        switch (mm->msgtype) {
            case 4:
            case 20:
                msgType = 5;
                break;
                break;

            case 5:
            case 21:
                msgType = 6;
                break;

            case 0:
            case 16:
                msgType = 7;
                break;

            case 11:
                msgType = 8;
                break;

            case 17:
            case 18:
                if (mm->metype >= 1 && mm->metype <= 4) {
                    msgType = 1;
                } else if (mm->metype >= 5 && mm->metype <= 8) {
                    msgType = 2;
                } else if (mm->metype >= 9 && mm->metype <= 18) {
                    msgType = 3;
                } else if (mm->metype == 19) {
                    msgType = 4;
                } else {
                    return;
                }
                break;

            default:
                return;
        }
    }

    // Fields 1 to 6 : SBS message type and ICAO address of the aircraft and some other stuff
    p += sprintf(p, "MSG,%d,1,1,%06X,1,", msgType, mm->addr);

    // Find current system time
    clock_gettime(CLOCK_REALTIME, &now);
    gmtime_r(&now.tv_sec, &stTime_now);

    // Find message reception time
    time_t received = (time_t) (mm->sysTimestamp / 1000);
    gmtime_r(&received, &stTime_receive);

    // Fields 7 & 8 are the message reception time and date
    p += sprintf(p, "%04d/%02d/%02d,", (stTime_receive.tm_year + 1900), (stTime_receive.tm_mon + 1), stTime_receive.tm_mday);
    p += sprintf(p, "%02d:%02d:%02d.%03u,", stTime_receive.tm_hour, stTime_receive.tm_min, stTime_receive.tm_sec, (unsigned) (mm->sysTimestamp % 1000));

    // Fields 9 & 10 are the current time and date
    p += sprintf(p, "%04d/%02d/%02d,", (stTime_now.tm_year + 1900), (stTime_now.tm_mon + 1), stTime_now.tm_mday);
    p += sprintf(p, "%02d:%02d:%02d.%03u", stTime_now.tm_hour, stTime_now.tm_min, stTime_now.tm_sec, (unsigned) (now.tv_nsec / 1000000U));

    // Field 11 is the callsign (if we have it)
    if (mm->callsign_valid) {
        p += sprintf(p, ",%s", mm->callsign);
    } else {
        p += sprintf(p, ",");
    }

    // Field 12 is the altitude (if we have it)
    if (Modes.use_gnss) {
        if (mm->geom_alt_valid) {
            p += sprintf(p, ",%dH", mm->geom_alt);
        } else if (mm->baro_alt_valid && trackDataValid(&a->geom_delta_valid)) {
            p += sprintf(p, ",%dH", mm->baro_alt + a->geom_delta);
        } else if (mm->baro_alt_valid) {
            p += sprintf(p, ",%d", mm->baro_alt);
        } else {
            p += sprintf(p, ",");
        }
    } else {
        if (mm->baro_alt_valid) {
            p += sprintf(p, ",%d", mm->baro_alt);
        } else if (mm->geom_alt_valid && trackDataValid(&a->geom_delta_valid)) {
            p += sprintf(p, ",%d", mm->geom_alt - a->geom_delta);
        } else {
            p += sprintf(p, ",");
        }
    }

    // Field 13 is the ground Speed (if we have it)
    if (mm->gs_valid) {
        p += sprintf(p, ",%.0f", mm->gs.selected);
    } else {
        p += sprintf(p, ",");
    }

    // Field 14 is the ground Heading (if we have it)
    if (mm->heading_valid && mm->heading_type == HEADING_GROUND_TRACK) {
        p += sprintf(p, ",%.0f", mm->heading);
    } else {
        p += sprintf(p, ",");
    }

    // Fields 15 and 16 are the Lat/Lon (if we have it)
    if (mm->cpr_decoded || mm->sbs_pos_valid) {
        p += sprintf(p, ",%1.6f,%1.6f", mm->decoded_lat, mm->decoded_lon);
    } else {
        p += sprintf(p, ",,");
    }

    // Field 17 is the VerticalRate (if we have it)
    if (Modes.use_gnss) {
        if (mm->geom_rate_valid) {
            p += sprintf(p, ",%dH", mm->geom_rate);
        } else if (mm->baro_rate_valid) {
            p += sprintf(p, ",%d", mm->baro_rate);
        } else {
            p += sprintf(p, ",");
        }
    } else {
        if (mm->baro_rate_valid) {
            p += sprintf(p, ",%d", mm->baro_rate);
        } else if (mm->geom_rate_valid) {
            p += sprintf(p, ",%d", mm->geom_rate);
        } else {
            p += sprintf(p, ",");
        }
    }

    // Field 18 is  the Squawk (if we have it)
    if (mm->squawk_valid) {
        p += sprintf(p, ",%04x", mm->squawk);
    } else {
        p += sprintf(p, ",");
    }

    if (mm->receiverCountMlat) {
        p += sprintf(p, ",%d", mm->receiverCountMlat);
    } else if (mm->alert_valid) {
        // Field 19 is the Squawk Changing Alert flag (if we have it)
        if (mm->alert) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    if (mm->mlatEPU) {
        p += sprintf(p, ",%d", mm->mlatEPU);
    } else if (mm->squawk_emergency_valid) {
        // Field 20 is the Squawk Emergency flag (if we have it)
        if (mm->squawk_emergency) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else if (mm->squawk_valid) {
        // Field 20 is the Squawk Emergency flag (if we have it)
        if ((mm->squawk == 0x7500) || (mm->squawk == 0x7600) || (mm->squawk == 0x7700)) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 21 is the Squawk Ident flag (if we have it)
    if (mm->spi_valid) {
        if (mm->spi) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 22 is the OnTheGround flag (if we have it)
    switch (mm->airground) {
        case AG_GROUND:
            p += sprintf(p, ",-1");
            break;
        case AG_AIRBORNE:
            p += sprintf(p, ",0");
            break;
        default:
            p += sprintf(p, ",");
            break;
    }

    p += sprintf(p, "\r\n");

    completeWrite(writer, p);
}


void jsonPositionOutput(struct modesMessage *mm, struct aircraft *a) {
    MODES_NOTUSED(mm);
    char *p;

    int buflen = 8192;

    p = prepareWrite(&Modes.json_out, buflen);
    if (!p)
        return;

    char *end = p + buflen;

    p = sprintAircraftObject(p, end, a, mm->sysTimestamp, 2, NULL);

    if (p + 1 < end) {
        *p++ = '\n';
        completeWrite(&Modes.json_out, p);
    } else {
        fprintf(stderr, "buffer insufficient jsonPositionOutput()\n");
    }
}

void sendData(struct net_writer *output, char *data, int len) {
    char *p;

    int buflen = MODES_OUT_BUF_SIZE;

    while (len > 0) {
        p = prepareWrite(output, buflen);
        if (!p)
            return;

        int tsize = imin(len, buflen);
        memcpy(p, data, tsize);
        len -= tsize;
        p += tsize;
        completeWrite(output, p);
    }
}

// Decode a little-endian IEEE754 float (binary32)
float ieee754_binary32_le_to_float(uint8_t *data) {
    double sign = (data[3] & 0x80) ? -1.0 : 1.0;
    int16_t raw_exponent = ((data[3] & 0x7f) << 1) | ((data[2] & 0x80) >> 7);
    uint32_t raw_significand = ((data[2] & 0x7f) << 16) | (data[1] << 8) | data[0];

    if (raw_exponent == 0) {
        if (raw_significand == 0) {
            /* -0 is treated like +0 */
            return 0;
        } else {
            /* denormal */
            return ldexp(sign * raw_significand, -126 - 23);
        }
    }

    if (raw_exponent == 255) {
        if (raw_significand == 0) {
            /* +/-infinity */
            return sign < 0 ? -INFINITY : INFINITY;
        } else {
            /* NaN */
#ifdef NAN
            return NAN;
#else
            return 0.0f;
#endif
        }
    }

    /* normalized value */
    return ldexp(sign * ((1 << 23) | raw_significand), raw_exponent - 127 - 23);
}

static void handle_radarcape_position(float lat, float lon, float alt) {
    if (Modes.netIngest || Modes.netReceiverId) {
        return;
    }

    if (!isfinite(lat) || lat < -90 || lat > 90 || !isfinite(lon) || lon < -180 || lon > 180 || !isfinite(alt)) {
        return;
    }

    if (!Modes.userLocationValid) {
        Modes.fUserLat = lat;
        Modes.fUserLon = lon;
        Modes.userLocationValid = 1;
        receiverPositionChanged(lat, lon, alt);
    }
}

/**
 * Convert 32bit binary angular measure to double degree.
 * See https://www.globalspec.com/reference/14722/160210/Chapter-7-5-3-Binary-Angular-Measure
 * @param data Data buffer start (MSB first)
 * @return Angular degree.
 */
static double bam32ToDouble(uint32_t bam) {
    return (double) ((int32_t) ntohl(bam) * 8.38190317153931E-08);
}

//
//=========================================================================
//
// This function decodes a GNS HULC protocol message

static void decodeHulcMessage(char *p) {
    // silently ignore these messages if proper SDR isn't set
    if (Modes.sdr_type != SDR_GNS)
        return;

    int alt = 0;
    double lat = 0.0;
    double lon = 0.0;
    char id = *p++; //Get message id
    unsigned char len = *p++; // Get message length
    hulc_status_msg_t hsm;

    if (id == 0x01 && len == 0x18) {
        // HULC Status message
        for (int j = 0; j < len; j++) {
            hsm.buf[j] = *p++;
            // unescape
            if (*p == 0x1A) {
                p++;
            }
        }
        /*
        // Antenna serial
        Modes.receiver.antenna_serial = ntohl(hsm.status.serial);
        // Antenna status flags
        Modes.receiver.antenna_flags = ntohs(hsm.status.flags);
        // Reserved for internal use
        Modes.receiver.antenna_reserved = ntohs(hsm.status.reserved);
        // Antenna Unix epoch (not used)
        // Antenna GPS satellites used for fix
        Modes.receiver.antenna_gps_sats = hsm.status.satellites;
        // Antenna GPS HDOP*10, thus 12 is HDOP 1.2
        Modes.receiver.antenna_gps_hdop = hsm.status.hdop;
        */

        // Antenna GPS latitude
        lat = bam32ToDouble(hsm.status.latitude);
        // Antenna GPS longitude
        lon = bam32ToDouble(hsm.status.longitude);
        // Antenna GPS altitude
        alt = ntohs(hsm.status.altitude);
        uint32_t antenna_flags = ntohs(hsm.status.flags);
        // Use only valid GPS position
        if ((antenna_flags & 0xE000) == 0xE000) {
            if (!isfinite(lat) || lat < -90 || lat > 90 || !isfinite(lon) || lon < -180 || lon > 180) {
                return;
            }
            // only use when no fixed location is defined
            if (!Modes.userLocationValid) {
                Modes.fUserLat = lat;
                Modes.fUserLon = lon;
                Modes.userLocationValid = 1;
                receiverPositionChanged(lat, lon, alt);
            }
        }
    } else if (id == 0x01 && len > 0x18) {
        // Future use planed.
    } else if (id == 0x24 && len == 0x10) {
        // Response to command #00
        fprintf(stderr, "Firmware: v%0u.%0u.%0u\n", *(p + 5), *(p + 6), *(p + 7));
    }
}

// recompute global Mode A/C setting
static void autoset_modeac() {
    if (!Modes.mode_ac_auto)
        return;

    Modes.mode_ac = 0;
    for (struct net_service *service = Modes.services_out.services; service->descr; service++) {
        for (struct client *c = service->clients; c; c = c->next) {
            if (c->modeac_requested) {
                Modes.mode_ac = 1;
                break;
            }
        }
    }
}

// Send some Beast settings commands to a client
void sendBeastSettings(int fd, const char *settings) {
    int len;
    char *buf, *p;

    len = strlen(settings) * 3;
    buf = p = alloca(len);

    while (*settings) {
        *p++ = 0x1a;
        *p++ = '1';
        *p++ = *settings++;
    }

    anetWrite(fd, buf, len);
}

static int handle_gpsd(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb) {
    MODES_NOTUSED(c);
    MODES_NOTUSED(remote);
    MODES_NOTUSED(now);
    MODES_NOTUSED(mb);

    if (Modes.debug_gps) {
        fprintTime(stderr, now);
        fprintf(stderr, " gpsdebug: received from GPSD: \'%s\'\n", p);
    }

    // remove spaces in place
    char *d = p;
    char *s = p;
    do {
        while (*s == ' ') {
            s++;
        }
        *d = *s++;
    } while (*d++);

    // filter all messages but TPV type
    if (0 && !strstr(p, "\"class\":\"TPV\"")) {
        if (Modes.debug_gps) {
            fprintf(stderr, "gpsdebug: class \"TPV\" : ignoring message.\n");
        }
        return 0;
    }
    // filter all messages which don't have lat / lon
    char *latp = strstr(p, "\"lat\":");
    char *lonp = strstr(p, "\"lon\":");
    if (!latp || !lonp) {
        if (Modes.debug_gps) {
            fprintf(stderr, "gpsdebug: lat / lon not present: ignoring message.\n");
        }
        return 0;
    }
    latp += 6;
    lonp += 6;

    char *saveptr = NULL;
    strtok_r(latp, ",", &saveptr);
    strtok_r(lonp, ",", &saveptr);

    double lat = strtod(latp, NULL);
    double lon = strtod(lonp, NULL);

    if (Modes.debug_gps) {
        fprintf(stderr, "gpsdebug: parsed lat,lon: %11.6f,%11.6f\n", lat, lon);
    }
    //fprintf(stderr, "%11.6f %11.6f\n", lat, lon);


    if (!isfinite(lat) || lat < -89.9 || lat > 89.9 || !isfinite(lon) || lon < -180 || lon > 180) {
        if (Modes.debug_gps) {
            fprintf(stderr, "gpsdebug: lat lon implausible, ignoring\n");
        }
        return 0;
    }
    if (fabs(lat) < 0.1 && fabs(lon) < 0.1) {
        if (Modes.debug_gps) {
            fprintf(stderr, "gpsdebug: lat lon implausible, ignoring\n");
        }
        return 0;
    }

    if (Modes.debug_gps) {
        fprintf(stderr, "gpsdebug: Updating position, writing receiver.json\n");
    }

    Modes.fUserLat = lat;
    Modes.fUserLon = lon;
    Modes.userLocationValid = 1;

    if (Modes.json_dir) {
        free(writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson()).buffer); // location changed
    }

    return 0;
}

static int handleCommandSocket(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb) {
    MODES_NOTUSED(c);
    MODES_NOTUSED(remote);
    MODES_NOTUSED(now);
    MODES_NOTUSED(mb);
    char *saveptr = NULL;
    char *cmd = strtok_r(p, " ", &saveptr);
    if (strcmp(cmd, "deleteTrace") == 0) {
        char *t1 = strtok_r(NULL, " ", &saveptr);
        char *t2 = strtok_r(NULL, " ", &saveptr);
        char *t3 = strtok_r(NULL, " ", &saveptr);
        if (!t1 || !t2 || !t3) {
            fprintf(stderr, "commandSocket deleteTrace: not enough tokens\n");
            return 0;
        }
        struct hexInterval* new = cmalloc(sizeof(struct hexInterval));
        new->hex = (uint32_t) strtol(t1, NULL, 16);
        new->from = (int64_t) strtol(t2, NULL, 10);
        new->to = (int64_t) strtol(t3, NULL, 10);
        new->next = Modes.deleteTrace;
        Modes.deleteTrace = new;
        fprintf(stderr, "Deleting %06x from %lld to %lld\n", new->hex, (long long) new->from, (long long) new->to);
    } else {
        fprintf(stderr, "commandSocket: unrecognized command\n");
    }
    return 0;
}
//
// Handle a Beast command message.
// Currently, we just look for the Mode A/C command message
// and ignore everything else.
//
static int handleBeastCommand(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb) {
    MODES_NOTUSED(remote);
    MODES_NOTUSED(now);
    MODES_NOTUSED(mb);
    if (p[0] == 'P') {
        // got ping
        c->ping = readPingEscaped(p+1);
        c->pingReceived = now;
        c->pingEnabled = 1;
        if (0 && Modes.debug_ping)
            fprintf(stderr, "Got Ping: %d\n", c->ping);
    } else if (p[0] == '1') {
        switch (p[1]) {
            case 'j':
                c->modeac_requested = 0;
                break;
            case 'J':
                c->modeac_requested = 1;
                break;
        }
        autoset_modeac();
    } else if (p[0] == 'W') {
        switch (p[1]) {
            // reduce data rate, double beast reduce interval for 30 seconds
            case 'S':
                {
                    static int64_t antiSpam;
                    // only log this at most every 10 minutes and only if it's already active
                    if (now < Modes.doubleBeastReduceIntervalUntil && now > antiSpam) {
                        antiSpam = now + 600 * SECONDS;
                        fprintf(stderr, "%s: High latency, reducing data usage temporarily. (%s port %s)\n", c->service->descr, c->host, c->port);
                    }
                }
                Modes.doubleBeastReduceIntervalUntil = now + PING_REDUCE_DURATION;
                break;
        }
    }
    return 0;
}

//
//=========================================================================
//
// This function decodes a Beast binary format message
//
// The message is passed to the higher level layers, so it feeds
// the selected screen output, the network output and so forth.
//
// If the message looks invalid it is silently discarded.
//
// The function always returns 0 (success) to the caller as there is no
// case where we want broken messages here to close the client connection.
//
// to save a couple cycles we remove the escapes in the calling function and expect nonescaped messages here
static int decodeBinMessage(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb) {
    uint16_t msgLen = 0;
    int j;
    unsigned char ch;
    struct modesMessage *mm = netGetMM(mb);
    unsigned char *msg = mm->msg;

    mm->client = c;

    ch = *p++; /// Get the message type

    mm->receiverId = c->receiverId;
    if (unlikely(Modes.incrementId)) {
        mm->receiverId += now / (10 * MINUTES);
    }

    if (ch == '2') {
        msgLen = MODES_SHORT_MSG_BYTES;
    } else if (ch == '3') {
        msgLen = MODES_LONG_MSG_BYTES;
    } else if (ch == '1') {
        if (!Modes.mode_ac) {
            if (remote) {
                Modes.stats_current.remote_received_modeac++;
            } else {
                Modes.stats_current.demod_modeac++;
            }
            return 0;
        }
        msgLen = MODEAC_MSG_BYTES;
    } else if (ch == '5') {
        // Special case for Radarcape position messages.
        float lat, lon, alt;
        unsigned char msg[21];
        for (j = 0; j < 21; j++) { // and the data
            msg[j] = ch = *p++;
        }

        lat = ieee754_binary32_le_to_float(msg + 4);
        lon = ieee754_binary32_le_to_float(msg + 8);
        alt = ieee754_binary32_le_to_float(msg + 12);

        handle_radarcape_position(lat, lon, alt);
        return 0;
    } else if (ch == 'H') {
        decodeHulcMessage(p);
        return 0;
    } else if (ch == 'P') {
        // pong message
        // only accept pong message if not ingest or client ping "enabled"
        if (!Modes.netIngest || c->pingEnabled) {
            c->pong = readPing(p);
            return pongReceived(c, now);
        }
        return 0;
    } else {
        // unknown msg type
        return 0;
    }

    /* Beast messages are marked depending on their source. From internet they are marked
     * remote so that we don't try to pass them off as being received by this instance
     * when forwarding them.
     */
    mm->remote = remote;

    mm->timestamp = 0;
    // Grab the timestamp (big endian format)
    for (j = 0; j < 6; j++) {
        ch = *p++;
        mm->timestamp = mm->timestamp << 8 | (ch & 255);
    }

    // record reception time as the time we read it.
    mm->sysTimestamp = now;
    //fprintf(stderr, "epoch: %.6f\n", mm->sysTimestamp / 1000.0);


    ch = *p++; // Grab the signal level
    mm->signalLevel = ((unsigned char) ch / 255.0);
    mm->signalLevel = mm->signalLevel * mm->signalLevel;

    /* In case of Mode-S Beast use the signal level per message for statistics */
    if (c == Modes.serial_client) {
        Modes.stats_current.signal_power_sum += mm->signalLevel;
        Modes.stats_current.signal_power_count += 1;

        if (mm->signalLevel > Modes.stats_current.peak_signal_power)
            Modes.stats_current.peak_signal_power = mm->signalLevel;
        if (mm->signalLevel > 0.50119)
            Modes.stats_current.strong_signal_count++; // signal power above -3dBFS
    }

    for (j = 0; j < msgLen; j++) { // and the data
        msg[j] = ch = *p++;
    }

    int result = -10;
    if (msgLen == MODEAC_MSG_BYTES) { // ModeA or ModeC
        if (remote) {
            Modes.stats_current.remote_received_modeac++;
        } else {
            Modes.stats_current.demod_modeac++;
        }
        decodeModeAMessage(mm, ((msg[0] << 8) | msg[1]));
        result = 0;
    } else {
        if (remote) {
            Modes.stats_current.remote_received_modes++;
        } else {
            Modes.stats_current.demod_preambles++;
        }
        result = decodeModesMessage(mm);
        if (result < 0) {
            if (result == -1) {
                if (remote) {
                    Modes.stats_current.remote_rejected_unknown_icao++;
                } else {
                    Modes.stats_current.demod_rejected_unknown_icao++;
                }
            } else {
                if (remote) {
                    Modes.stats_current.remote_rejected_bad++;
                } else {
                    Modes.stats_current.demod_rejected_bad++;
                }
            }
        } else {
            if (remote) {
                Modes.stats_current.remote_accepted[mm->correctedbits]++;
            } else {
                Modes.stats_current.demod_accepted[mm->correctedbits]++;
            }
        }
    }
    if (c->pongReceived && c->pongReceived > now + 100) {
        // if messages are received with more than 100 ms delay after a pong, recalculate c->rtt
        pongReceived(c, now);
    }
    if (c->rtt > Modes.ping_reject && Modes.netIngest) {
        // don't discard CPRs, if we have better data speed_check generally will take care of delayed CPR messages
        // this way we get basic data even from high latency receivers
        // super high latency receivers are getting disconnected in pongReceived()
        if (!mm->cpr_valid) {
            Modes.stats_current.remote_rejected_delayed++;
            return 0; // discard
        }
    }
    if (c->unreasonable_messagerate) {
        mm->garbage = 1;
    }
    if ((Modes.garbage_ports || Modes.netReceiverId) && receiverCheckBad(mm->receiverId, now)) {
        mm->garbage = 1;
    }

    netUseMessage(mm);
    return 0;
}


// Planefinder uses bit stuffing, so if we see a DLE byte, we need the next byte
static inline unsigned char getNextPfUnstuffedByte(char **p) {
    if (**p == DLE) {
        (*p)++;
    }
    return *(*p)++;
}
//
//
//=========================================================================
//
// This function decodes a planefinder binary format message
//
// The message is passed to the higher level layers, so it feeds
// the selected screen output, the network output and so forth.
//
// If the message looks invalid it is silently discarded.
//
// The function always returns 0 (success) to the caller as there is no
// case where we want broken messages here to close the client connection.
//
// For packet ID 0x41, the format is:
// Byte     Value       Notes
// 0        <DLE>
// 1        ID          0x41
// 2        padding     always 0
// 3        byte        the lower 4 bits map to: 0 = mode AC, 1 = mode S short, 2 = mode S long. Bit 4 indicates CRC. 5-7 is undefined in the spec I received, although bit 5 is in use (it can be ignored).
// 4        byte        signal strength
// 5-8      long        epoch time
// 9-12     long        nanoseconds
// 13-27    byte        data, mode AC/S
static int decodePfMessage(struct client *c, char *p, int remote, int64_t now, struct messageBuffer *mb) {
    MODES_NOTUSED(remote);

    int msgLen = 0;
    int j;
    unsigned char ch;
    struct modesMessage *mm = netGetMM(mb);
    unsigned char *msg = mm->msg;

    mm->client = c;
    mm->remote = 1;

    // Skip the DLE in the beginning
    p++;

    // Packet ID / type
    ch = getNextPfUnstuffedByte(&p); /// Get the message type
    // This shouldn't happen because we check it in the readPlanefinder() function
    if (ch != 0xc1) {
        return 0;
    }

    // Padding
    getNextPfUnstuffedByte(&p);

    // Packet type
    ch = getNextPfUnstuffedByte(&p);
    if (ch & 0x10) {
        // CRC: ignore field
    }
    if ((ch & 0xF) == 0) {
        if (!Modes.mode_ac) {
            return 0;
        }
        msgLen = MODEAC_MSG_BYTES;
    } else if ((ch & 0xF) == 1) {
        msgLen = MODES_SHORT_MSG_BYTES;
    } else if ((ch & 0xF) == 2) {
        msgLen = MODES_LONG_MSG_BYTES;
    } else {
        if (Modes.debug_planefinder) {
            fprintf(stderr, "Unknown message type: %d\n", ch);
        }
        return 0;
    }

    // Signal strength
    ch = getNextPfUnstuffedByte(&p);
    mm->signalLevel = ((unsigned char) ch / 255.0);
    mm->signalLevel = mm->signalLevel * mm->signalLevel; // square it to get power

    mm->timestamp = 0;
    int64_t seconds = 0;
    for (j = 0; j < 4; j++) {
        ch = getNextPfUnstuffedByte(&p);
        seconds = seconds << 8 | (ch & 255);
    }

    int64_t nanoseconds = 0;
    for (j = 0; j < 4; j++) {
        ch = getNextPfUnstuffedByte(&p);
        nanoseconds = nanoseconds << 8 | (ch & 255);
    }

    if (Modes.debug_planefinder) {
        fprintf(stderr, "sec: %12lld ns: %12lld\n", (long long) seconds, (long long) nanoseconds);
    }
    mm->timestamp = seconds * 1000000000LL + nanoseconds;

    // record reception time as the time we read it.
    mm->sysTimestamp = now;

    for (j = 0; j < msgLen; j++) { // and the data
        msg[j] = getNextPfUnstuffedByte(&p);
    }

    int result = -10;
    if (msgLen == MODEAC_MSG_BYTES) { // ModeA or ModeC
        Modes.stats_current.remote_received_modeac++;
        decodeModeAMessage(mm, ((msg[0] << 8) | msg[1]));
        result = 0;
    } else {
        Modes.stats_current.remote_received_modes++;
        result = decodeModesMessage(mm);
        if (result < 0) {
            if (result == -1) {
                Modes.stats_current.remote_rejected_unknown_icao++;
            } else {
                Modes.stats_current.remote_rejected_bad++;
            }
        } else {
            Modes.stats_current.remote_accepted[mm->correctedbits]++;
        }
    }
    if (c->unreasonable_messagerate) {
        mm->garbage = 1;
    }
    if ((Modes.garbage_ports || Modes.netReceiverId) && receiverCheckBad(mm->receiverId, now)) {
        mm->garbage = 1;
    }
    if (Modes.debug_planefinder && (Modes.mode_ac || msgLen != MODEAC_MSG_BYTES)) {
        displayModesMessage(mm);
    }

    netUseMessage(mm);
    return 0;
}

// exception decoding subroutine, return 1 for success, 0 for failure
static int decodeHexMessage(struct client *c, char *hex, int64_t now, struct modesMessage *mm) {
    int l = strlen(hex), j;
    unsigned char *msg = mm->msg;

    mm->client = c;

    // Mark messages received over the internet as remote so that we don't try to
    // pass them off as being received by this instance when forwarding them
    mm->remote = 1;
    mm->signalLevel = 0;

    // Remove spaces on the left and on the right
    while (l && isspace(hex[l - 1])) {
        hex[l - 1] = '\0';
        l--;
    }
    while (isspace(*hex)) {
        hex++;
        l--;
    }

    // https://www.aerobits.pl/wp-content/uploads/2021/07/OEM_MC_Datasheet.pdf
    // *RAW_FRAME;(SIGS,SIGQ,TS)\r\n
    // signal strength mV, signal quality mV, time from last PPS pulse in hex (microseconds?, document doesn't say)
    // let's just create a bogus timestamp and parse the signal strength ....
    if (hex[l - 1] == ')') {
        hex[l - 1] = '\0';
        int pos = l - 1;
        while (pos && hex[pos] != '(') {
            pos--;
        } // find opening (
        if (pos) {
            hex[pos] = '\0';
            l = pos;
        } else {
            return 0;
        } // incomplete
        char *saveptr = NULL;
        char *token = strtok_r(&hex[pos + 1], ",", &saveptr);
        if (!token) return 0;
        mm->signalLevel = strtol(token, NULL, 10);
        mm->signalLevel /= 1000; // let's assume 1000 mV max .. i only have a small sample, specification isn't clear
        mm->signalLevel = mm->signalLevel * mm->signalLevel; // square it to get power
        mm->signalLevel = fmin(1.0, mm->signalLevel); // cap at 1
                                                    //
        token = strtok_r(NULL, ",", &saveptr); // discard signal quality
        if (!token) return 0;

        token = strtok_r(NULL, ",", &saveptr);
        if (token) {
            int after_pps = strtol(token, NULL, 16);
            // round down to current second, go to microseconds and add after_pps, go to 12 MHz clock
            int64_t seconds = now / 1000;
            if (after_pps / 1000 > (now % 1000) + 500) {
                seconds -= 1;
                // assume our clock is one second in front of the GPS clock, go back one second before adding the after pps time
            }
            mm->timestamp = (seconds * (1000 * 1000) + after_pps) * 12;
        } else {
            mm->timestamp = now * 12e3; // make 12 MHz timestamp from microseconds
        }
    }
    // Turn the message into binary.
    // Accept
    // *-AVR: raw
    // @-AVR: beast_ts+raw
    // %-AVR: timeS+raw (CRC good)
    // <-AVR: beast_ts+sigL+raw
    // and some AVR records that we can understand
    if (hex[l - 1] != ';') {
        return (0);
    } // not complete - abort

    if (l <= 2 * MODEAC_MSG_BYTES)
        return (0); // too short

    switch (hex[0]) {
        // <TTTTTTTTTTTTSS
        case '<':
            {
                // skip <
                hex++;
                l--;
                // skip ;
                l--;

                if (l < 12)
                    return (0);
                for (j = 0; j < 12; j++) {
                    mm->timestamp = (mm->timestamp << 4) | hexDigitVal(*hex);
                    hex++;
                    l--;
                }
                if (l < 2)
                    return (0);
                mm->signalLevel = ((hexDigitVal(hex[0]) << 4) | hexDigitVal(hex[1])) / 255.0;
                hex += 2;
                l -= 2;
                mm->signalLevel = mm->signalLevel * mm->signalLevel;
                break;
            }

        case '@': // No CRC check
                  // example timestamp 03BA2A7C1DD1, should be 12 MHz treat it as such
                  // example message: @03BA2A7C1DD15D4CA7F9A0B84B;
            { // CRC is OK
                hex++;
                l -= 2; // Skip @ and ;

                if (l <= 12) // if we have only enough hex for the timestamp or less it's invalid
                    return (0);
                for (j = 0; j < 12; j++) {
                    mm->timestamp = (mm->timestamp << 4) | hexDigitVal(*hex);
                    hex++;
                }

                l -= 12; // timestamp now processed
                break;
            }
        case '%':
            { // CRC is OK
                hex += 13;
                l -= 14; // Skip @,%, and timestamp, and ;
                break;
            }

        case '*':
        case ':':
            {
                hex++;
                l -= 2; // Skip * and ;
                break;
            }

        default:
            {
                return (0); // We don't know what this is, so abort
                break;
            }
    }

    if ((l != (MODEAC_MSG_BYTES * 2))
            && (l != (MODES_SHORT_MSG_BYTES * 2))
            && (l != (MODES_LONG_MSG_BYTES * 2))) {
        return (0);
    } // Too short or long message... broken

    if ((0 == Modes.mode_ac)
            && (l == (MODEAC_MSG_BYTES * 2))) {
        return (0);
    } // Right length for ModeA/C, but not enabled

    for (j = 0; j < l; j += 2) {
        int high = hexDigitVal(hex[j]);
        int low = hexDigitVal(hex[j + 1]);

        if (high == -1 || low == -1) return 0;
        msg[j / 2] = (high << 4) | low;
    }

    // record reception time as the time we read it.
    mm->sysTimestamp = now;

    if (l == (MODEAC_MSG_BYTES * 2)) { // ModeA or ModeC
        Modes.stats_current.remote_received_modeac++;
        decodeModeAMessage(mm, ((msg[0] << 8) | msg[1]));
    } else { // Assume ModeS
        int result;

        Modes.stats_current.remote_received_modes++;
        result = decodeModesMessage(mm);
        if (result < 0) {
            if (result == -1)
                Modes.stats_current.remote_rejected_unknown_icao++;
            else
                Modes.stats_current.remote_rejected_bad++;
            return 0;
        } else {
            Modes.stats_current.remote_accepted[mm->correctedbits]++;
        }
    }

    return 1;
}

//
//
//=========================================================================
//
// This function decodes a string representing message in raw hex format
// like: *8D4B969699155600E87406F5B69F; The string is null-terminated.
//
// The message is passed to the higher level layers, so it feeds
// the selected screen output, the network output and so forth.
//
// If the message looks invalid it is silently discarded.
//
// The function always returns 0 (success) to the caller as there is no
// case where we want broken messages here to close the client connection.
//

static int processHexMessage(struct client *c, char *hex, int remote, int64_t now, struct messageBuffer *mb) {
    MODES_NOTUSED(remote);

    struct modesMessage *mm = netGetMM(mb);

    int success = decodeHexMessage(c, hex, now, mm);

    if (success) {
        netUseMessage(mm);
    }

    return (0);
}

static int decodeUatMessage(struct client *c, char *msg, int remote, int64_t now, struct messageBuffer *mb) {
    MODES_NOTUSED(remote);

    char *end = msg + strlen(msg);
    char output[512];

    uat2esnt_convert_message(msg, end, output, output + sizeof(output));

    char *som = output;
    char *eod = som + strlen(som);
    char *p;

    while (((p = memchr(som, '\n', eod - som)) != NULL)) {
        *p = '\0';

        struct modesMessage *mm = netGetMM(mb);

        int success = decodeHexMessage(c, som, now, mm);

        if (success) {
            struct aircraft *a = aircraftGet(mm->addr);
            if (!a) { // If it's a currently unknown aircraft....
                a = aircraftCreate(mm->addr); // ., create a new record for it,
            }
            // ignore the first UAT message
            if (now > a->seen + 300 * SECONDS) {
                //fprintf(stderr, "IGNORING first UAT message from: %06x\n", a->addr);
                a->seen = now;
                return 0;
            }
            netUseMessage(mm);
        }
        som = p + 1;
    }
    return 0;
}

static const char *hexDumpString(const char *str, int strlen, char *buf, int buflen) {
    int max = buflen / 4 - 4;
    if (max <= 0) {
        // fail silently
        buf[0] = 0;
        return buf;
    }

    char *out = buf;
    char *end = buf + buflen;

    for (int k = 0; k < max && k < strlen; k++) {
        out = safe_snprintf(out, end, "%02x ", (unsigned char) str[k]);
    }

    out = safe_snprintf(out, end, "|");

    for (int k = 0; k < max && k < strlen; k++) {
        unsigned char ch = str[k];
        if (ch < 32 || ch > 126) {
            out = safe_snprintf(out, end, ".");
        } else {
            out = safe_snprintf(out, end, "%c", (unsigned char) ch);
        }
    }

    out = safe_snprintf(out, end, "|");

    if (out >= end) {
        fprintf(stderr, "hexDumpstring: check logic\n");
    }

    return buf;
}


//
//=========================================================================
//
// This function polls the clients using read() in order to receive new
// messages from the net.
//
static int readClient(struct client *c, int64_t now) {
    int nread = 0;
    if (c->discard)
        c->buflen = 0;

    int left = c->bufmax - c->buflen - 4; // leave 4 extra byte for NUL termination in the ASCII case


    // If our buffer is full discard it, this is some badly formatted shit
    if (left <= 0) {
        c->garbage += c->buflen;
        Modes.stats_current.remote_malformed_beast += c->buflen;

        c->buflen = 0;
        c->som = c->buf;
        c->eod = c->buf + c->buflen;

        left = c->bufmax - c->buflen - 4; // leave 4 extra byte for NUL termination in the ASCII case
                                          // If there is garbage, read more to discard it ASAP
    }

    if (c->remote) {
        nread = recv(c->fd, c->buf + c->buflen, left, 0);
    } else {
        // read instead of recv for modesbeast / gns-hulc ....
        nread = read(c->fd, c->buf + c->buflen, left);
    }
    int err = errno;

    // If we didn't get all the data we asked for, then return once we've processed what we did get.
    if (nread != left) {
        c->bContinue = 0;

        // also note that we (likely) emptied the system network buffer
        c->last_read_flush = now;
    }

    if (nread < 0) {
        if (err == EAGAIN || err == EWOULDBLOCK) {
            // No data available, check later!
            return 0;
        }
        // Other errors
        if (Modes.debug_net) {
            fprintf(stderr, "%s: Socket Error: %s: %s port %s (fd %d, SendQ %d, RecvQ %d)\n",
                    c->service->descr, strerror(err), c->host, c->port,
                    c->fd, c->sendq_len, c->buflen);
        }
        modesCloseClient(c);
        return 0;
    }

    // End of file
    if (nread == 0) {
        if (c->con) {
            if (Modes.synthetic_now) {
                Modes.synthetic_now = 0;
            }
            fprintf(stderr, "%s: Remote server disconnected: %s port %s (fd %d, SendQ %d, RecvQ %d)\n",
                    c->service->descr, c->con->address, c->con->port, c->fd, c->sendq_len, c->buflen);
        } else if (Modes.debug_net && !Modes.netIngest) {
            fprintf(stderr, "%s: Listen client disconnected: %s port %s (fd %d, SendQ %d, RecvQ %d)\n",
                    c->service->descr, c->host, c->port, c->fd, c->sendq_len, c->buflen);
        }
        if (!c->con && Modes.debug_bogus) {
            setExit(1);
        }
        modesCloseClient(c);
        return 0;
    }

    // nread > 0 here
    Modes.stats_current.network_bytes_in += nread;

    // disable for the time being
    if (0 && Modes.netIngest && !Modes.debug_no_discard) {
        if (now - c->recentMessagesReset > 1 * SECONDS) {
            c->recentMessagesReset = now;
            c->recentMessages = 0;
            c->unreasonable_messagerate = 0;
        }

        if (c->recentMessages > 4000) {
            c->unreasonable_messagerate = 1;
            if (now > c->recentMessagesReset) {
                c->recentMessagesReset = now + 30 * SECONDS; // don't reset for 60 seconds to keep discarding this client
                char uuid[64]; // needs 36 chars and null byte
                sprint_uuid(c->receiverId, c->receiverId2, uuid);
                fprintf(stderr, "GARBAGE for 60 seconds: message rate > 4000 rId %s %s\n", uuid, c->proxy_string);
            }
        }
    }

    if (!Modes.debug_no_discard && !c->discard && now - c->last_read < 800 && now - c->last_read_flush > 2400 && !Modes.synthetic_now) {
        c->discard = 1;
        if (Modes.netIngest && c->proxy_string[0] != '\0') {
            fprintf(stderr, "<3>ERROR, not enough CPU: Discarding data from: %s\n", c->proxy_string);
        } else {
            fprintf(stderr, "<3>%s: ERROR, not enough CPU: Discarding data from: %s port %s (fd %d)\n",
                    c->service->descr, c->host, c->port, c->fd);
        }
    }

    c->last_read = now;

    if (c->discard) {
        return nread;
    }

    c->buflen += nread;
    c->bytesReceived += nread;

    return nread;
}

static int readBeastcommand(struct client *c, int64_t now, struct messageBuffer *mb) {
    char *p;

    while (c->som < c->eod && ((p = memchr(c->som, (char) 0x1a, c->eod - c->som)) != NULL)) { // The first byte of buffer 'should' be 0x1a
        char *eom; // one byte past end of message

        c->som = p; // consume garbage up to the 0x1a
        ++p; // skip 0x1a

        if (p >= c->eod) {
            // Incomplete message in buffer, retry later
            break;
        }

        if (*p == '1') {
            eom = p + 2;
        } else if (*p == 'W') { // W command
            eom = p + 2;
        } else if (*p == 'P') { // ping from the receiver
            eom = p + 4;
        } else {
            // Not a valid beast command, skip 0x1a and try again
            ++c->som;
            continue;
        }

        // we need to be careful of double escape characters in the message body
        for (p = c->som + 1; p < c->eod && p < eom; p++) {
            if (0x1A == *p) {
                p++;
                eom++;
            }
        }

        if (eom > c->eod) { // Incomplete message in buffer, retry later
            break;
        }

        char *start = c->som + 1;

        // advance to next message
        c->som = eom;

        // Pass message to handler.
        if (c->service->read_handler(c, start, c->remote, now, mb)) {
            modesCloseClient(c);
            return -1;
        }
    }
    return 0;
}

static int readAscii(struct client *c, int64_t now, struct messageBuffer *mb) {
    //
    // This is the ASCII scanning case, AVR RAW or HTTP at present
    // If there is a complete message still in the buffer, there must be the separator 'sep'
    // in the buffer, note that we full-scan the buffer at every read for simplicity.
    //
    char *p;

    // replace null bytes with newlines so the routine doesn't stop working
    // while null bytes are an illegal input, let's deal with them regardless
    if (memchr(c->som, '\0', c->eod - c->som)) {
        p = c->som;
        while (p < c->eod) {
            if (*p == '\0') {
                *p = '\n';
            }
            p++;
        }
        // warn about illegal input
        static int64_t antiSpam;
        if (Modes.debug_garbage && now > antiSpam) {
            antiSpam = now + 30 * SECONDS;
            fprintf(stderr, "%s from %s port %s: Bad format, at least one null byte in input data!\n", c->service->descr, c->host, c->port);
        }
    }
    while (c->som < c->eod && (p = strstr(c->som, c->service->read_sep)) != NULL) { // end of first message if found
        *p = '\0'; // The handler expects null terminated strings
                   // remove \r for strings that still have it at the end
        if (p - 1 > c->som && *(p - 1) == '\r') {
            *(p - 1) = '\0';
        }
        char *start = c->som;
        c->som = p + c->service->read_sep_len; // Move to start of next message
        if (c->service->read_handler(c, start, c->remote, now, mb)) { // Pass message to handler.
            if (Modes.debug_net) {
                fprintf(stderr, "%s: Closing connection from %s port %s\n", c->service->descr, c->host, c->port);
            }
            modesCloseClient(c); // Handler returns 1 on error to signal we .
            return -1; // should close the client connection
        }
    }
    return 0;
}

static int readAsterix(struct client *c, int64_t now, struct messageBuffer *mb) {

    while (c->som < c->eod) {
        char *p = c->som;
        uint16_t msgLen = (*(p + 1) << 8) + *(p + 2);
        char *end = c->som + msgLen;
        c->som = end;
        if (c->service->read_handler(c, p, c->remote, now, mb)) {
            if (Modes.debug_net) {
                fprintf(stderr, "%s: Closing connection from %s port %s\n", c->service->descr, c->host, c->port);
            }
            modesCloseClient(c);
            return -1;
        }
    }
    return 0;
}

// Spec for Planefinder message.
// All messages begin with a DLE and end with a DLE, ETX. DLE cannot appear in the middle of a message unless it's escaped with another DLE (i.e., bit stuffing)
// Message format:
// Byte     Value       Notes
// 0        <DLE>       header
// 1        ID          Only packet id 0xc1 is recognized here
// 2 - n    Data        Depends on the packet type
// n+1      <DLE>       escape
// n+2      <ETX>       footer
static int readPlanefinder(struct client *c, int64_t now, struct messageBuffer *mb) {
    char *p;
    unsigned char pid;

    char *start;
    char *end;

    // Scan the entire buffer, see if we can find one or more messages.
    while (c->som < c->eod && ((p = memchr(c->som, DLE, c->eod - c->som)) != NULL)) {
        end = NULL;

        // Make sure we didn't jump to a DLE that's in the middle of a message. TBD if we need this
        if (p+1 < c->eod && *(p+1) != DLE && *(p+1) != ETX) {
            // Good to go!
        } else {
            c->som = p+1;
            continue;
        }

        // Now, check if we have the end of the message in the buffer
        start = p;
        p++; // Skip start DLE
        p++; // Skip packet ID

        while (p < c->eod) {
            if (*p  == DLE) {
                // Potential message end found; it's either a DLE, ETX sequence or a DLE, DLE (the first is an escape for the second)
                if (p+1 < c->eod && *(p+1) == ETX) {
                    // We found an actual end!
                    end = p+1;
                    break;
                }
            }
            p++;
        }

        if (p >= c->eod) {
            // We reached the end of the buffer and didn't find a message. We'll call this function again when there's more data available
            return 0;
        }

#if 0
        fprintf(stderr, "Message found from 0x%p to 0x%p: ", c->som, end);
        for (char * byte = start; byte<=end; byte++) {
            fprintf(stderr, "%02x", (unsigned char)*byte & 0xFF);
        }
        fprintf(stderr, "\n");
#endif

        // Next time we loop through this, start from the next message
        c->som = end+1;

        // We only process messages with ID 0xc1. Others are valid, but not relevant for us
        pid = *(start+1);
        if (pid != 0xc1) {
            continue;
        }

        // Pass message to handler.
        if (c->service->read_handler(c, start, c->remote, now, mb)) {
            modesCloseClient(c);
            return -1;
        }
    }
    return 0;
}

static int readBeast(struct client *c, int64_t now, struct messageBuffer *mb) {
    // This is the Beast Binary scanning case.
    // If there is a complete message still in the buffer, there must be the separator 'sep'
    // in the buffer, note that we full-scan the buffer at every read for simplicity.

    char *p;

    //fprintf(stderr, "readBeast\n");

    while (c->som < c->eod && ((p = memchr(c->som, (char) 0x1a, c->eod - c->som)) != NULL)) { // The first byte of buffer 'should' be 0x1a

        c->garbage += p - c->som;
        Modes.stats_current.remote_malformed_beast += p - c->som;

        //lastSom = p;
        c->som = p; // consume garbage up to the 0x1a
        ++p; // skip 0x1a

        if (p >= c->eod) {
            // Incomplete message in buffer, retry later
            break;
        }

        char *eom; // one byte past end of message
        unsigned char ch;

        if (!c->service) { fprintf(stderr, "c->service null ohThee9u\n"); }


        if (Modes.synthetic_now) {
            now = Modes.synthetic_now;
            Modes.syntethic_now_suppress_errors = 0;
        }

        // Check for message with receiverId prepended
        ch = *p;
        if (ch == 0xe8) {
            p++;

            int64_t ts;
            if (p + sizeof(int64_t) > c->eod) {
                break;
            }

            memcpy(&ts, p, sizeof(int64_t));
            p += sizeof(int64_t);

            int64_t old_now = now;

            if (Modes.dump_accept_synthetic_now) {
                now = Modes.synthetic_now = ts;
            } else {
                fprintf(stderr, "%s: Synthetic timestamp detected without --devel=accept_synthetic specified, disconnecting client: %s port %s (fd %d)\n",
                        c->service->descr, c->host, c->port, c->fd);
                modesCloseClient(c);
                return -1;
            }

            //fprintf(stderr, "%ld %ld\n", (long) now, (long) (c->eod - c->som));

            c->som = p; // set start of next message
            if (*p != 0x1A) {
                //fprintf(stderr, "..\n");
                continue;
            }
            p++; // skip 0x1a
            if (p >= c->eod) {
                // Incomplete message in buffer, retry later
                break;
            }

            if (Modes.synthetic_now) {
                if (priorityTasksPending()) {
                    if (now - old_now > 5 * SECONDS) {
                        Modes.syntethic_now_suppress_errors = 1;
                    }
                    pthread_mutex_unlock(&Threads.decode.mutex);
                    priorityTasksRun();
                    pthread_mutex_lock(&Threads.decode.mutex);
                    Modes.syntethic_now_suppress_errors = 0;
                }
            }
        } else if (ch == 0xe3) {
            p++;
            uint64_t receiverId = 0;
            eom = p + 8;
            // we need to be careful of double escape characters in the receiverId
            for (int j = 0; j < 8 && p < c->eod && p < eom; j++) {
                ch = *p++;
                if (ch == 0x1A) {
                    ch = *p++;
                    eom++;
                    if (p < c->eod && ch != 0x1A) { // check that it's indeed a double escape
                                                 // might be start of message rather than double escape.
                        c->garbage += p - 1 - c->som;
                        Modes.stats_current.remote_malformed_beast += p - 1 - c->som;
                        c->som = p - 1;
                        goto beastWhileContinue;
                    }
                }
                // Grab the receiver id (big endian format)
                receiverId = receiverId << 8 | (ch & 255);
            }

            if (eom + 2 > c->eod)// Incomplete message in buffer, retry later
                break;

            if (!Modes.netIngest) {
                c->receiverId = receiverId;
            }

            c->som = p; // set start of next message
            if (*p != 0x1A) {
                continue;
            }
            p++; // skip 0x1a
            if (p >= c->eod) {
                // Incomplete message in buffer, retry later
                break;
            }
        }

        if (!c->service) { fprintf(stderr, "c->service null waevem0E\n"); }

        ch = *p;
        if (ch == '2') {
            eom = p + 1 + 6 + 1 + MODES_SHORT_MSG_BYTES;
        } else if (ch == '3') {
            eom = p + 1 + 6 + 1 + MODES_LONG_MSG_BYTES;
        } else if (ch == '1') {
            eom = p + 1 + 6 + 1 + MODEAC_MSG_BYTES;
            if (0) {
                char sample[256];
                char *sampleStart = c->som - 32;
                if (sampleStart < c->buf)
                    sampleStart = c->buf;
                *c->som = 'X';
                hexDumpString(sampleStart, c->eod - sampleStart, sample, sizeof(sample));
                *c->som = 0x1a;
                sample[sizeof(sample) - 1] = '\0';
                fprintf(stderr, "modeAC: som pos %d, sample %s, eom > c->eod %d\n", (int) (c->som - c->buf), sample, eom > c->eod);
            }
        } else if (ch == '5') {
            eom = p + MODES_LONG_MSG_BYTES + 8;
        } else if (ch == 0xe4) {
            // read UUID and continue with next message
            p++;
            c->som = read_uuid(c, p, c->eod);
            continue;
        } else if (ch == 'P') {
            //unsigned char *pu = (unsigned char*) p;
            //fprintf(stderr, "%x %x %x %x %x\n", pu[0], pu[1], pu[2], pu[3], pu[4]);
            eom = p + 4;
        } else if (ch == 'W') {
            // read command
            p++;
            ch = *p;
            if (ch == 'O') {
                // O for high resolution timer, both P and p already used for previous iterations
                // explicitely enable ping for this client
                c->pingEnabled = 1;
                uint32_t newPing = now & ((1 << 24) - 1);
                if (Modes.debug_ping)
                    fprintf(stderr, "Initial Ping: %d\n", newPing);
                pingClient(c, newPing);
                if (!c->service) {
                    fprintf(stderr, "c->service null Ieseey5s\n");
                    return -1;
                }
                if (flushClient(c, now) < 0) {
                    return -1;
                }
                if (!c->service) {
                    fprintf(stderr, "c->service null EshaeC7n\n");
                    return -1;
                }
            }
            c->som += 2;
            continue;
        } else {
            // Not a valid beast message, skip 0x1a
            // Skip following byte as well:
            // either: 0x1a (likely not a start of message but rather escaped 0x1a)
            // or: any other char is skipped anyhow when looking for the next 0x1a
            c->som += 2;
            Modes.stats_current.remote_malformed_beast += 2;
            c->garbage += 2;
            continue;
        }

        if (!c->service) { fprintf(stderr, "c->service null quooJ1ea\n"); return -1; }

        if (eom > c->eod) // Incomplete message in buffer, retry later
            break;

        char noEscapeStorage[MODES_LONG_MSG_BYTES + 8 + 16]; // 16 extra for good measure
        char *noEscape = p;

        // we need to be careful of double escape characters in the message body
        if (memchr(p, (char) 0x1A, eom - p)) {
            char *t = noEscapeStorage;
            while (p < eom) {
                if (*p == (char) 0x1A) {
                    p++;
                    eom++;
                    if (eom > c->eod) { // Incomplete message in buffer, retry later
                        break;
                    }
                    if (*p != (char) 0x1A) { // check that it's indeed a double escape
                                             // might be start of message rather than double escape.
                                             //
                        c->garbage += p - 1 - c->som;
                        Modes.stats_current.remote_malformed_beast += p - 1 - c->som;
                        c->som = p - 1;

                        if (0) {
                            char sample[256];
                            char *sampleStart = c->som - 32;
                            if (sampleStart < c->buf)
                                sampleStart = c->buf;
                            *c->som = 'X';
                            hexDumpString(sampleStart, c->eod - sampleStart, sample, sizeof(sample));
                            *c->som = 0x1a;
                            sample[sizeof(sample) - 1] = '\0';
                            fprintf(stderr, "not a double Escape: som pos %d, sample %s, eom - som %d\n", (int) (c->som - c->buf), sample, (int) (eom - c->som));

                        }

                        goto beastWhileContinue;
                    }
                }
                *t++ = *p++;
            }
            noEscape = noEscapeStorage;
        }

        if (eom > c->eod) // Incomplete message in buffer, retry later
            break;

        if (Modes.receiver_focus && c->receiverId != Modes.receiver_focus && noEscape[0] != 'P') {
            // advance to next message
            c->som = eom;
            continue;
        }

        if (!c->service) {
            fprintf(stderr, "c->service null hahGh1Sh\n");
            return -1;
        }

        // if we get some valid data, reduce the garbage counter.
        if (c->garbage > 128)
            c->garbage -= 128;

        // advance to next message
        c->som = eom;

        // Have a 0x1a followed by 1/2/3/4/5 - pass message to handler.
        int res = c->service->read_handler(c, noEscape, c->remote, now, mb);

        if (!c->service) {
            return -1;
        }
        if (res) {
            modesCloseClient(c);
            return -1;
        }


beastWhileContinue:
        ;
    }

    if (c->eod - c->som > 256) {
        //fprintf(stderr, "beastWhile too much data remaining, garbage?!\n");
        c->garbage += c->eod - c->som;
        Modes.stats_current.remote_malformed_beast += c->eod - c->som;
        c->som = c->eod;
    }

    return 0;
}

static int readProxy(struct client *c) {
    char *proxy = strstr(c->som, "PROXY ");
    char *eop = strstr(c->som, "\r\n");
    if (proxy && proxy == c->som) {
        if (!eop) {
            // incomplete proxy string (shouldn't happen but let's check anyhow)
            return -2;
        }
        *eop = '\0';
        strncpy(c->proxy_string, proxy + 6, sizeof(c->proxy_string) - 1);
        c->proxy_string[sizeof(c->proxy_string) - 1] = '\0'; // make sure it's null terminated
                                                             //fprintf(stderr, "%s\n", c->proxy_string);
        *eop = '\r';

        // expected string example: "PROXY TCP4 172.12.2.132 172.191.123.45 40223 30005"

        char *space = proxy;
        space = memchr(space + 1, ' ', eop - space - 1);
        space = memchr(space + 1, ' ', eop - space - 1);
        space = memchr(space + 1, ' ', eop - space - 1);
        // hash up to 3rd space
        if (eop - proxy > 10) {
            //fprintf(stderr, "%ld %ld %s\n", eop - proxy, space - proxy, space);
            c->receiverId = fasthash64(proxy, space - proxy, 0x2127599bf4325c37ULL);
        }

        c->som = eop + 2;
    }
    return 0;
}

void requestCompression(struct client *c, int64_t now) {
    //memcpy(c->sendq + c->sendq_len, heartbeat_msg, heartbeat_len);
    //c->sendq_len += heartbeat_len;
    flushClient(c, now);
}

//
//=========================================================================
//
// The message is supposed to be separated from the next message by the
// separator 'sep', which is a null-terminated C string.
//
// Every full message received is decoded and passed to the higher layers
// calling the function's 'handler'.
//
// The handler returns 0 on success, or 1 to signal this function we should
// close the connection with the client in case of non-recoverable errors.
//
//
//
//

static int processClient(struct client *c, int64_t now, struct messageBuffer *mb) {

    struct net_service *service = c->service;
    read_mode_t read_mode = service->read_mode;

    //fprintf(stderr, "processing count %d, buf->id %d\n", c->processing, mb->id);

    if (now - c->connectedSince < 5 * SECONDS) {
        // check for PROXY v1 header if connection is new / low bytes received
        if (Modes.netIngest && c->som == c->buf) {
            if (c->eod - c->som >= 6 && c->som[0] == 'P' && c->som[1] == 'R') {
                int res = readProxy(c);
                if (res != 0) {
                    return res;
                }
            }
        }
        const char *hb = service->heartbeat_in.msg;
        int hb_len = service->heartbeat_in.len;
        if (hb && c->eod - c->som >= 5 * hb_len) {
            int res = 0;
            for (int k = 0; k < 5; k++) {
                res += memcmp(hb, c->som + k * hb_len, hb_len);
            }
            if (res == 0) {
                requestCompression(c, now);
            }
        }
    }

    if (read_mode == READ_MODE_BEAST) {
        int res = readBeast(c, now, mb);
        if (res != 0) {
            return res;
        }

    } else if (read_mode == READ_MODE_ASCII) {
        int res = readAscii(c, now, mb);
        if (res != 0) {
            return res;
        }

    } else if (read_mode == READ_MODE_IGNORE) {
        // drop the bytes on the floor
        c->som = c->eod;

    } else if (read_mode == READ_MODE_BEAST_COMMAND) {
        int res = readBeastcommand(c, now, mb);
        if (res != 0) {
            return res;
        }
    } else if (read_mode == READ_MODE_ASTERIX) {
        int res = readAsterix(c, now, mb);
        if (res != 0) {
            return res;
	    }
    } else if (read_mode == READ_MODE_PLANEFINDER) {
        int res = readPlanefinder(c, now, mb);
        if (res != 0) {
            return res;
        }
    }

    if (!c->receiverIdLocked && (c->bytesReceived > 512 || now > c->connectedSince + 10000)) {
        lockReceiverId(c);
    }

    return 0;
}
static void modesReadFromClient(struct client *c, struct messageBuffer *mb) {
    if (!c->service) {
        fprintf(stderr, "c->service null jahFuN3e\n");
        return;
    }

    if (!c->bufferToProcess) {
        c->bContinue = 1;
    }
    for (int k = 0; c->bContinue; k++) {
        int64_t now = mstime();

        // guarantee at least one read before obeying the network time limit
        if (k > 0 && now > Modes.network_time_limit) {
            return;
        }

        if (Modes.synthetic_now && priorityTasksPending()) {
            return;
        }

        if (!c->service) {
            return;
        }

        if (!c->bufferToProcess) {
            // get more buffer to process
            int read = readClient(c, now);
            //fprintTimePrecise(stderr, now); fprintf(stderr, "readClient returned: %d\n", read);
            if (!read) {
                return;
            }
            if (c->discard) {
                continue;
            }
            c->som = c->buf;
            c->eod = c->buf + c->buflen; // one byte past end of data
            // Always NUL-terminate so we are free to use strstr()
            // nb: we never fill the last byte of the buffer with read data (see above) so this is safe
            if (likely(c->buflen < c->bufmax)) {
                *c->eod = '\0';
            } else {
                fprintf(stderr, "wtf Dieh2hau\n");
            }
            c->bufferToProcess = 1;

            mb->activeClient = c;
        }

        // process buffer

        c->processing++;
        //fprintf(stderr, "%d", c->processing);
        int res = processClient(c, now, mb);
        c->processing--;
        c->bufferToProcess = 0;

        mb->activeClient = NULL;

        if (res < 0) {
            return;
        }

        if (c->som > c->buf) { // We processed something - so
            c->buflen = c->eod - c->som; //     Update the unprocessed buffer length
            if (c->buflen > 0) {
                memmove(c->buf, c->som, c->buflen); //     Move what's remaining to the start of the buffer
            } else {
                if (c->buflen < 0) {
                    c->buflen = 0;
                    fprintf(stderr, "codepoint Si0wereH\n");
                }
            }
            c->som = c->buf;
            c->eod = c->buf + c->buflen; // one byte past end of data
                                         // Always NUL-terminate so we are free to use strstr()
                                         // nb: we never fill the last byte of the buffer with read data (see above) so this is safe
        }

        // disconnect garbage feeds
        if (c->garbage >= GARBAGE_THRESHOLD) {

            *c->eod = '\0';
            char sample[256];
            hexDumpString(c->som, c->eod - c->som, sample, sizeof(sample));
            sample[sizeof(sample) - 1] = '\0';
            if (c->proxy_string[0] != '\0') {
                fprintf(stderr, "Garbage: Close: %s sample: %s\n", c->proxy_string, sample);
            } else {
                fprintf(stderr, "Garbage: Close: %s port %s sample: %s\n", c->host, c->port, sample);
            }

            modesCloseClient(c);
            return;
        }
    }

    // reset discard status
    c->discard = 0;
}

/*
static inline unsigned unsigned_difference(unsigned v1, unsigned v2) {
    return (v1 > v2) ? (v1 - v2) : (v2 - v1);
}

static inline float heading_difference(float h1, float h2) {
    float d = fabs(h1 - h2);
    return (d < 180) ? d : (360 - d);
}
*/

const char *airground_enum_string(airground_t ag) {
    switch (ag) {
        case AG_AIRBORNE:
            return "A+";
        case AG_GROUND:
            return "G+";
        default:
            return "?";
    }
}

static void serviceFreeClients(struct net_service *s) {
    struct client *c, **prev;
    for (prev = &s->clients, c = *prev; c; c = *prev) {
        if (c->fd == -1) {
            // Recently closed, prune from list
            *prev = c->next;
            sfree(c->sendq);
            sfree(c->buf);
            sfree(c);
        } else {
            prev = &c->next;
        }
    }
}

// Unlink and free closed clients
static void netFreeClients() {
    for (struct net_service *service = Modes.services_out.services; service->descr; service++) {
        serviceFreeClients(service);
    }
    for (struct net_service *service = Modes.services_in.services; service->descr; service++) {
        serviceFreeClients(service);
    }
}

static void handleEpoll(struct net_service_group *group, struct messageBuffer *mb) {
    // Only process each epoll even in one thread
    // the variables for this are specific to the service group,
    // using locking each group can only be processed by one thread at a time
    // modesReadFromClient can unlock this lock which is fine as the while head
    // is lock protected
    while (group->event_progress < Modes.net_event_count) {
        int k = group->event_progress;
        group->event_progress += 1;

        struct epoll_event event = Modes.net_events[k];
        if (event.data.ptr == &Modes.exitNowEventfd) {
            return;
        }

        struct client *cl = (struct client *) Modes.net_events[k].data.ptr;
        if (!cl) { fprintf(stderr, "handleEpoll: epollEvent.data.ptr == NULL\n"); continue; }

        if (!cl->service) {
            // client is closed
            //fprintf(stderr, "handleEpoll(): client closed\n");
            continue;
        }

        if (cl->service->group != group) {
            //fprintf(stderr, "handleEpoll(): wrong group\n");
            continue;
        }

        if (cl->acceptSocket || cl->net_connector_dummyClient) {
            if (cl->acceptSocket) {
                modesAcceptClients(cl, mstime());
            }
            if (cl->net_connector_dummyClient) {
                checkServiceConnected(cl->con, mstime());
            }
        } else {
            if ((event.events & EPOLLOUT)) {
                // check if we need to flush a client because the send buffer was full previously
                if (flushClient(cl, mstime()) < 0) {
                    continue;
                }
            }

            if ((event.events & (EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP))) {
                modesReadFromClient(cl, mb);
            }
        }
    }
}

static int64_t checkFlushService(struct net_service *service, int64_t now) {
    int64_t default_wait = 1000;
    if (!service->writer) {
        return now + default_wait;
    }
    struct net_writer *writer = service->writer;
    if (!writer->connections) {
        return now + default_wait;
    }
    if (Modes.net_heartbeat_interval && service->heartbeat_out.msg
            && now - writer->lastWrite >= Modes.net_heartbeat_interval) {
        // If we have generated no messages for a while, send a heartbeat
        send_heartbeat(service);
    }
    if (writer->dataUsed && now >= writer->nextFlush) {
        flushWrites(writer);
    }
    if (writer->dataUsed) {
        return writer->nextFlush;
    } else {
        return now + default_wait;
    }
}

static void decodeTask(void *arg, threadpool_threadbuffers_t *buffer_group) {
    MODES_NOTUSED(buffer_group);

    task_info_t *info = (task_info_t *) arg;
    struct messageBuffer *mb = &Modes.netMessageBuffer[info->from];

    //fprintf(stderr, "%.3f decodeTask %d\n", mstime()/1000.0, mb->id);

    pthread_mutex_lock(&Modes.decodeLock);
    //fprintf(stderr, "%.3f decoding %d\n", mstime()/1000.0, mb->id);

    handleEpoll(&Modes.services_in, mb);

    for (int kt = 0; kt < Modes.decodeThreads; kt++) {
        struct messageBuffer *otherbuf = &Modes.netMessageBuffer[kt];
        struct client *cl = otherbuf->activeClient;
        if (cl && cl->service) {
            modesReadFromClient(cl, mb);
        }
    }
    drainMessageBuffer(mb);

    pthread_mutex_unlock(&Modes.decodeLock);

    pthread_mutex_lock(&Modes.outputLock);
    handleEpoll(&Modes.services_out, mb);
    pthread_mutex_unlock(&Modes.outputLock);
}

//
// Perform periodic network work
//
void modesNetPeriodicWork(void) {
    static int64_t check_flush;
    static int64_t next_tcp_json;
    static struct timespec watch;

    if (!Modes.net_events) {
        epollAllocEvents(&Modes.net_events, &Modes.net_maxEvents);
    }

    int64_t now = mstime();

    dump_beast_check(now);

    int64_t wait_ms;
    if (Modes.serial_client) {
        wait_ms = 20;
    } else if (Modes.sdr_type != SDR_NONE) {
        // NO WAIT WHEN USING AN SDR !! IMPORTANT !!
        wait_ms = 0;
    } else if (Modes.net_only) {
        // wait in net-only mode (unless we get network packets, that wakes the wait immediately)
        wait_ms = imax(0, check_flush - now); // modify wait for next flush timer
        wait_ms = imin(wait_ms, Modes.next_reconnect_callback - now); // modify wait for reconnect callback timer
        wait_ms = imax(wait_ms, 0); // don't allow negative values
    } else {
        wait_ms = 0;
    }

    // unlock decode mutex for waiting in handleEpoll
    pthread_mutex_unlock(&Threads.decode.mutex);

    if (priorityTasksPending()) {
        sched_yield();
    }
    Modes.net_event_count = epoll_wait(Modes.net_epfd, Modes.net_events, Modes.net_maxEvents, (int) wait_ms);
    Modes.services_in.event_progress = 0;
    Modes.services_out.event_progress = 0;

    //fprintTimePrecise(stderr, now); fprintf(stderr, " event count %d wait_ms %d\n", Modes.net_event_count, (int) wait_ms);

    if (0 && Modes.net_event_count > 0) {
        fprintTimePrecise(stderr, now); fprintf(stderr, " event count %d wait_ms %d\n", Modes.net_event_count, (int) wait_ms);
    }

    pthread_mutex_lock(&Threads.decode.mutex);

    int64_t interval = lapWatch(&watch);

    now = mstime();
    Modes.network_time_limit = now + 100;

    struct messageBuffer *mb = &Modes.netMessageBuffer[0];
    if (Modes.decodeThreads == 1) {
        handleEpoll(&Modes.services_in, mb);
        drainMessageBuffer(mb);
        handleEpoll(&Modes.services_out, mb);
    } else {
        task_info_t *infos = Modes.decodeTasks->infos;
        threadpool_task_t *tasks = Modes.decodeTasks->tasks;
        int taskCount = 0;

        for (int kt = 0; kt < Modes.decodeThreads; kt++) {
            threadpool_task_t *task = &tasks[kt];
            task_info_t *range = &infos[kt];

            range->from = kt;

            task->function = decodeTask;
            task->argument = range;
            taskCount++;
        }

        struct timespec before = threadpool_get_cumulative_thread_time(Modes.decodePool);
        threadpool_run(Modes.decodePool, tasks, taskCount);
        struct timespec after = threadpool_get_cumulative_thread_time(Modes.decodePool);
        timespec_add_elapsed(&before, &after, &Modes.stats_current.background_cpu);
    }

    if (Modes.serial_client) {
        if (Modes.serial_client->service) {
            modesReadFromClient(Modes.serial_client, mb);
        } else {
            fprintf(stderr, "Serial client closed unexpectedly, exiting!\n");
            setExit(2);
        }
    }

    if (Modes.net_event_count == Modes.net_maxEvents) {
        epollAllocEvents(&Modes.net_events, &Modes.net_maxEvents);
    }

    int64_t elapsed1 = lapWatch(&watch);

    now = mstime();

    pingSenders(Modes.beast_in_service, now);

    int64_t elapsed2 = lapWatch(&watch);

    // If we have data that has been waiting to be written for a while, write it now.
    if (Modes.sdr_type != SDR_NONE || now >= check_flush || Modes.net_event_count > 0) {
        //fprintTimePrecise(stderr, now); fprintf(stderr, " checkFlush\n");

        check_flush = now + 200;

        for (struct net_service *service = Modes.services_out.services; service->descr; service++) {
            int64_t nextFlush = checkFlushService(service, now);
            check_flush = imin(check_flush, nextFlush);
        }
        for (struct net_service *service = Modes.services_in.services; service->descr; service++) {
            int64_t nextFlush = checkFlushService(service, now);
            check_flush = imin(check_flush, nextFlush);
        }
    }

    if (now >= Modes.next_reconnect_callback) {
        //fprintTimePrecise(stderr, now); fprintf(stderr, " reconnectCallback\n");

        int64_t since_fail = now - Modes.last_connector_fail;
        if (since_fail < 2 * SECONDS) {
            Modes.next_reconnect_callback = now + 20 + since_fail * Modes.net_connector_delay_min / ( 3 * SECONDS );
        } else {
            Modes.next_reconnect_callback = now + Modes.net_connector_delay_min;
        }
        serviceReconnectCallback(now);
    }

    static int64_t next_free_clients;
    int64_t free_client_interval = 1 * SECONDS;
    if (now > next_free_clients) {
        next_free_clients = now + free_client_interval;


        netFreeClients();

        if (Modes.receiverTable) {
            static uint32_t upcount;
            int nParts = 5 * MINUTES / free_client_interval;
            receiverTimeout((upcount % nParts), nParts, now);
            upcount++;
        }
    }

    int64_t elapsed3 = lapWatch(&watch);

    static int64_t antiSpam;
    if ((elapsed1 > 2 * SECONDS || elapsed2 > 150 || elapsed3 > 150 || interval > 1 * SECONDS + Modes.net_output_flush_interval) && now > antiSpam + 5 * SECONDS) {
        antiSpam = now;
        fprintf(stderr, "<3>High load: modesNetPeriodicWork() elapsed1/2/3/interval %"PRId64"/%"PRId64"/%"PRId64"/%"PRId64" ms, suppressing for 5 seconds!\n",
                elapsed1, elapsed2, elapsed3, interval);
    }

    // supply JSON to vrs_out writer
    if (Modes.vrs_out.service && Modes.vrs_out.service->connections && now >= next_tcp_json) {
        static uint32_t part;
        static uint32_t count;
        uint32_t n_parts = 16; // must be 16 :)

        next_tcp_json = now + Modes.net_output_vrs_interval / n_parts;

        writeJsonToNet(&Modes.vrs_out, generateVRS(part, n_parts, (count % n_parts / 2 != part % 8)));
        if (++part == n_parts) {
            part = 0;
            count += 2;
        }
    }
}

void writeJsonToNet(struct net_writer *writer, struct char_buffer cb) {
    int len = cb.len;
    int written = 0;
    char *content = cb.buffer;
    char *pos;
    int bytes = MODES_OUT_BUF_SIZE;

    char *p = prepareWrite(writer, bytes);
    if (!p) {
        sfree(content);
        return;
    }

    pos = content;

    while (p && written < len) {
        if (bytes > len - written) {
            bytes = len - written;
        }
        memcpy(p, pos, bytes);
        p += bytes;
        pos += bytes;
        written += bytes;
        completeWrite(writer, p);

        p = prepareWrite(writer, bytes);
    }

    flushWrites(writer);
    sfree(content);
}


//
// =============================== Network IO ===========================
//

static void *pthreadGetaddrinfo(void *param) {
    struct net_connector *con = (struct net_connector *) param;

    struct addrinfo gai_hints;

    gai_hints.ai_family = AF_UNSPEC;
    gai_hints.ai_socktype = SOCK_STREAM;
    gai_hints.ai_protocol = 0;
    gai_hints.ai_flags = 0;
    gai_hints.ai_addrlen = 0;
    gai_hints.ai_addr = NULL;
    gai_hints.ai_canonname = NULL;
    gai_hints.ai_next = NULL;

    if (con->use_addr && con->address1) {
        con->address = con->address1;
        if (con->port1)
            con->port = con->port1;
        con->use_addr = 0;
    } else {
        con->address = con->address0;
        con->port = con->port0;
        con->use_addr = 1;
    }
    con->gai_error = getaddrinfo(con->address, con->port, &gai_hints, &con->addr_info);

    pthread_mutex_lock(&con->mutex);
    con->gai_request_done = 1;
    pthread_mutex_unlock(&con->mutex);
    return NULL;
}

static void cleanupService(struct net_service *s) {
    //fprintf(stderr, "cleanupService %s\n", s->descr);

    struct client *c = s->clients, *nc;
    while (c) {
        nc = c->next;

        anetCloseSocket(c->fd);
        c->sendq_len = 0;
        sfree(c->sendq);
        sfree(c->buf);
        sfree(c);

        c = nc;
    }

    if (s->listenSockets) {
        for (int i = 0; i < s->listener_count; ++i) {
            struct client *c = &s->listenSockets[i]; // not really a client
            epoll_ctl(Modes.net_epfd, EPOLL_CTL_DEL, c->fd, &c->epollEvent);
            anetCloseSocket(s->listener_fds[i]);
        }
        sfree(s->listenSockets);
    }
    sfree(s->listener_fds);
    if (s->writer && s->writer->data) {
        sfree(s->writer->data);
    }
    if (s->unixSocket) {
        unlink(s->unixSocket);
        sfree(s->unixSocket);
    }

    memset(s, 0, sizeof(struct net_service));
}

static void serviceGroupCleanup(struct net_service_group *group) {
    for (struct net_service *service = group->services; service->descr != NULL; service++) {
        cleanupService(service);
    }
    sfree(group->services);
    memset(group, 0x0, sizeof(struct net_service_group));
}

static void cleanupMessageBuffers() {

    if (Modes.decodeThreads > 1) {
        pthread_mutex_destroy(&Modes.decodeLock);
        pthread_mutex_destroy(&Modes.trackLock);
        pthread_mutex_destroy(&Modes.outputLock);

        threadpool_destroy(Modes.decodePool);
        destroy_task_group(Modes.decodeTasks);
    }

    for (int k = 0; k < Modes.decodeThreads; k++) {
        struct messageBuffer *buf = &Modes.netMessageBuffer[k];
        sfree(buf->msg);
        buf->len = 0;
        buf->alloc = 0;
    }
    sfree(Modes.netMessageBuffer);
}

void cleanupNetwork(void) {
    cleanupMessageBuffers();

    if (Modes.dump_fw) {
        zstdFwFinishFile(Modes.dump_fw);
        destroyZstdFw(Modes.dump_fw);
    }

    if (!Modes.net) {
        return;
    }
    serviceGroupCleanup(&Modes.services_out);
    serviceGroupCleanup(&Modes.services_in);

    close(Modes.net_epfd);

    for (int i = 0; i < Modes.net_connectors_count; i++) {
        struct net_connector *con = &Modes.net_connectors[i];
        if (con->gai_request_in_progress) {
            pthread_join(con->thread, NULL);
        }
        sfree(con->connect_string);
        freeaddrinfo(con->addr_info);
        pthread_mutex_destroy(&con->mutex);
    }
    sfree(Modes.net_connectors);
    sfree(Modes.net_events);

    Modes.net_connectors_count = 0;

}

static char *read_uuid(struct client *c, char *p, char *eod) {
    if (c->receiverIdLocked) { // only allow the receiverId to be set once
        return p + 32;
    }

    unsigned char ch;
    char *start = p;
    uint64_t receiverId = 0;
    uint64_t receiverId2 = 0;
    // read ascii to binary
    int j = 0;
    char *breakReason = "";
    for (int i = 0; i < 128 && j < 32; i++) {
        if (p >= eod) {
            breakReason = "eod";
            break;
        }
        ch = *p++;
        //fprintf(stderr, "%c", ch);
        if (0x1A == ch) {
            breakReason = "0x1a";
            break;
        }
        if ('-' == ch || ' ' == ch) {
            continue;
        }

        unsigned char x = 0xff;

        if (ch <= 'f' && ch >= 'a') {
            x = ch - 'a' + 10;
        } else if (ch <= '9' && ch >= '0') {
            x = ch - '0';
        } else if (ch <= 'F' && ch >= 'A') {
            x = ch - 'A' + 10;
        } else {
            breakReason = "ill";
            break;
        }

        if (j < 16)
            receiverId = receiverId << 4 | x; // set 4 bits and shift them up
        else if (j < 32)
            receiverId2 = receiverId2 << 4 | x; // set 4 bits and shift them up
        j++;
    }
    int valid = j;
    if (j < 32) {
        while (j < 32) {
            if (j < 16)
                receiverId = receiverId << 4 | 0;
            else if (j < 32)
                receiverId2 = receiverId2 << 4 | 0;
            j++;
        }
        if (1 || valid > 5) {
            char uuid[64]; // needs 36 chars and null byte
            sprint_uuid(receiverId, receiverId2, uuid);
            fprintf(stderr, "read_uuid() incomplete (%s): UUID |%.*s| -> |%s|\n", breakReason, (int) imin(36, eod - start), start, uuid);
        }
    }

    if (valid >= 16) {

        c->receiverId = receiverId;
        c->receiverId2 = receiverId2;

        if (Modes.debug_uuid) {
            char uuid[64]; // needs 36 chars and null byte
            sprint_uuid(receiverId, receiverId2, uuid);
            fprintf(stderr, "reading UUID |%.*s| -> |%s|\n", (int) imin(36, eod - start), start, uuid);
            //fprintf(stderr, "ADDR %s,%s rId %016"PRIx64" UUID %.*s\n", c->host, c->port, c->receiverId, (int) imin(eod - start, 36), start);
        }
        lockReceiverId(c);
    }
    return p;
}

static void outputMessage(struct modesMessage *mm) {
    // filter messages with unwanted DF types (sbs_in are unknown DF type, filter them all, this is arbitrary but no one cares anyway)
    if (Modes.filterDF && (mm->sbs_in || !(Modes.filterDFbitset & (1 << mm->msgtype)))) {
        return;
    }
    int noforward = (mm->timestamp == MAGIC_NOFORWARD_TIMESTAMP) && !Modes.beast_forward_noforward;
    int64_t orig_ts = mm->timestamp;
    if (Modes.beast_set_noforward_timestamp) {
        mm->timestamp = MAGIC_NOFORWARD_TIMESTAMP;
    }

    struct aircraft *ac = mm->aircraft;

    // Suppress the first message when using an SDR
    // messages with crc 0 have an explicit checksum and are more reliable, don't suppress them when there was no CRC fix performed
    if (Modes.net && !mm->sbs_in && (Modes.net_only || Modes.net_verbatim || (mm->crc == 0 && mm->correctedbits == 0) || (ac && ac->messages > 1) || mm->msgtype == DFTYPE_MODEAC)) {
        int is_mlat = (mm->source == SOURCE_MLAT);

        if (mm->jsonPositionOutputEmit && Modes.json_out.connections) {
            jsonPositionOutput(mm, ac);
        }

        if (Modes.garbage_ports && (mm->garbage || mm->pos_bad) && !mm->pos_old && Modes.garbage_out.connections) {
            modesSendBeastOutput(mm, &Modes.garbage_out);
        }

        if (ac && (!Modes.sbsReduce || mm->reduce_forward)) {
            if ((!is_mlat || Modes.forward_mlat_sbs) && Modes.sbs_out.connections) {
                modesSendSBSOutput(mm, ac, &Modes.sbs_out);
            }
            if (is_mlat && Modes.sbs_out_mlat.connections) {
                modesSendSBSOutput(mm, ac, &Modes.sbs_out_mlat);
            }
        }

        if (!noforward && !is_mlat && (Modes.net_verbatim || mm->correctedbits < 2) && Modes.raw_out.connections) {
            // Forward 2-bit-corrected messages via raw output only if --net-verbatim is set
            // Don't ever forward mlat messages via raw output.
            modesSendRawOutput(mm);
        }

        if (!noforward && (!is_mlat || Modes.forward_mlat) && (mm->correctedbits < 2 || Modes.net_verbatim)) {
            // Forward 2-bit-corrected messages via beast output only if --net-verbatim is set
            // Forward mlat messages via beast output only if --forward-mlat is set
            if (Modes.beast_out.connections) {
                modesSendBeastOutput(mm, &Modes.beast_out);
            }
            if (mm->reduce_forward && Modes.beast_reduce_out.connections) {
                modesSendBeastOutput(mm, &Modes.beast_reduce_out);
            }
        }
        if (Modes.dump_fw && (!Modes.dump_reduce || mm->reduce_forward)) {
            modesDumpBeastData(mm);
        }
        if (Modes.asterix_out.connections && (!Modes.asterixReduce || mm->reduce_forward)){
            modesSendAsterixOutput(mm, &Modes.asterix_out);
        }
    }

    if (mm->sbs_in && Modes.net && ac) {
        if (mm->reduce_forward || !Modes.sbsReduce) {
            if (Modes.sbs_out.connections) {
                modesSendSBSOutput(mm, ac, &Modes.sbs_out);
            }
            struct net_writer *extra_writer = NULL;
            switch(mm->source) {
                case SOURCE_SBS:
                    extra_writer = &Modes.sbs_out_replay;
                    break;
                case SOURCE_MLAT:
                    extra_writer = &Modes.sbs_out_mlat;
                    break;
                case SOURCE_JAERO:
                    extra_writer = &Modes.sbs_out_jaero;
                    break;
                case SOURCE_PRIO:
                    extra_writer = &Modes.sbs_out_prio;
                    break;

                default:
                    extra_writer = NULL;
            }
            if (extra_writer && extra_writer->connections) {
                modesSendSBSOutput(mm, ac, extra_writer);
            }
        }
    }

    mm->timestamp = orig_ts;

}

static void drainMessageBuffer(struct messageBuffer *buf) {
    if (Modes.decodeThreads < 2) {
        for (int k = 0; k < buf->len; k++) {
            struct modesMessage *mm = &buf->msg[k];
            if (Modes.debug_yeet && mm->addr % 0x100 != 0xd) {
                continue;
            }
            trackUpdateFromMessage(mm);
        }
        for (int k = 0; k < buf->len; k++) {
            struct modesMessage *mm = &buf->msg[k];
            if (Modes.debug_yeet && mm->addr % 0x100 != 0xd) {
                continue;
            }
            outputMessage(mm);
        }
        buf->len = 0;
    } else {

        pthread_mutex_unlock(&Modes.decodeLock);


        sched_yield();
        //fprintf(stderr, "thread %d draining\n", buf->id);

        pthread_mutex_lock(&Modes.trackLock);
        for (int k = 0; k < buf->len; k++) {
            struct modesMessage *mm = &buf->msg[k];
            if (Modes.debug_yeet && mm->addr % 0x100 != 0xd) {
                continue;
            }
            trackUpdateFromMessage(mm);
        }
        pthread_mutex_unlock(&Modes.trackLock);

        pthread_mutex_lock(&Modes.outputLock);
        for (int k = 0; k < buf->len; k++) {
            struct modesMessage *mm = &buf->msg[k];
            if (Modes.debug_yeet && mm->addr % 0x100 != 0xd) {
                continue;
            }
            outputMessage(mm);
        }
        pthread_mutex_unlock(&Modes.outputLock);

        buf->len = 0;

        pthread_mutex_lock(&Modes.decodeLock);
        //fprintf(stderr, "thread %d drain done, back to decoding\n", buf->id);
    }
}

// get a zeroed spot in in the message buffer, only messages from this buffer may be passed to netUseMessage

struct modesMessage *netGetMM(struct messageBuffer *buf) {
    struct modesMessage *mm = &buf->msg[buf->len];
    memset(mm, 0x0, sizeof(struct modesMessage));
    mm->messageBuffer = buf;
    return mm;
}

//
//=========================================================================
//
// When a new message is available, because it was decoded from the RTL device,
// file, or received in the TCP input port, or any other way we can receive a
// decoded message, we call this function in order to use the message.
//
// Basically this function passes a raw message to the upper layers for further
// processing and visualization
//

void netUseMessage(struct modesMessage *mm) {
    struct messageBuffer *buf = mm->messageBuffer;
    if (mm != &buf->msg[buf->len]) {
        fprintf(stderr, "FATAL: fix netUseMessage / get_mm\n");
        exit(1);
    }
    buf->len++;
    if (buf->len == buf->alloc) {
        drainMessageBuffer(buf);
    }
}

void netDrainMessageBuffers() {
    for (int kt = 0; kt < Modes.decodeThreads; kt++) {
        struct messageBuffer *mb = &Modes.netMessageBuffer[0];
        drainMessageBuffer(mb);
    }
}
