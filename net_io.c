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


//#include <brotli/encode.h>


//
// ============================= Networking =============================
//
// Note: here we disregard any kind of good coding practice in favor of
// extreme simplicity, that is:
//
// 1) We only rely on the kernel buffers for our I/O without any kind of
//    user space buffering.
// 2) We don't register any kind of event handler, from time to time a
//    function gets called and we accept new connections. All the rest is
//    handled via non-blocking I/O and manually polling clients to see if
//    they have something new to share with us when reading is needed.

static int handle_gpsd(struct client *c, char *p, int remote, int64_t now);
static int handleCommandSocket(struct client *c, char *p, int remote, int64_t now);
static int handleBeastCommand(struct client *c, char *p, int remote, int64_t now);
static int decodeBinMessage(struct client *c, char *p, int remote, int64_t now);
static int decodeHexMessage(struct client *c, char *hex, int remote, int64_t now);
static int decodeSbsLine(struct client *c, char *line, int remote, int64_t now);
static int decodeSbsLineMlat(struct client *c, char *line, int remote, int64_t now) {
    MODES_NOTUSED(remote);
    return decodeSbsLine(c, line, 64 + SOURCE_MLAT, now);
}
static int decodeSbsLinePrio(struct client *c, char *line, int remote, int64_t now) {
    MODES_NOTUSED(remote);
    return decodeSbsLine(c, line, 64 + SOURCE_PRIO, now);
}
static int decodeSbsLineJaero(struct client *c, char *line, int remote, int64_t now) {
    MODES_NOTUSED(remote);
    return decodeSbsLine(c, line, 64 + SOURCE_JAERO, now);
}

static void send_raw_heartbeat(struct net_service *service);
static void send_beast_heartbeat(struct net_service *service);
static void send_sbs_heartbeat(struct net_service *service);

static void autoset_modeac();
static void *pthreadGetaddrinfo(void *param);

static void modesCloseClient(struct client *c);
static int flushClient(struct client *c, int64_t now);
static char *read_uuid(struct client *c, char *p, char *eod);
static void modesReadFromClient(struct client *c, int64_t start);

//
//=========================================================================
//
// Networking "stack" initialization
//

// Init a service with the given read/write characteristics, return the new service.
// Doesn't arrange for the service to listen or connect
struct net_service *serviceInit(const char *descr, struct net_writer *writer, heartbeat_fn hb, read_mode_t mode, const char *sep, read_fn handler) {
    struct net_service *service;
    if (!descr) {
        fprintf(stderr, "Fatal: no service description\n");
        exit(1);
    }

    if (!(service = cmalloc(sizeof (struct net_service)))) {
        fprintf(stderr, "Out of memory allocating service %s\n", descr);
        exit(1);
    }
    memset(service, 0, sizeof(struct net_service));

    service->next = Modes.services;
    Modes.services = service;

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

    if (service->writer) {
        if (service->writer->data) {
            fprintf(stderr, "FATAL: serviceInit() called twice on the same service: %s\n", descr);
            exit(1);
        }

        // set writer to zero
        memset(service->writer, 0, sizeof(struct net_writer));

        if (!(service->writer->data = cmalloc(MODES_OUT_BUF_SIZE))) {
            fprintf(stderr, "Out of memory allocating output buffer for service %s\n", descr);
            exit(1);
        }

        service->writer->service = service;
        service->writer->dataUsed = 0;
        service->writer->lastWrite = mstime();
        service->writer->send_heartbeat = hb;
        service->writer->lastReceiverId = 0;
        service->writer->connections = 0;
    }

    return service;
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

// Create a client attached to the given service using the provided socket FD
struct client *createSocketClient(struct net_service *service, int fd) {
    return createGenericClient(service, fd);
}

// Create a client attached to the given service using the provided FD (might not be a socket!)

struct client *createGenericClient(struct net_service *service, int fd) {
    struct client *c;
    int64_t now = mstime();

    if (!service || fd == -1) {
        fprintf(stderr, "<3> FATAL: createGenericClient called with invalid parameters!\n");
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

    //fprintf(stderr, "c->receiverId: %016"PRIx64"\n", c->receiverId);

    c->bufmax = MODES_NET_SNDBUF_SIZE << Modes.net_sndbuf_size;
    // hard limit RCVBUF for ingest server, lots of connections
    if (Modes.netIngest) {
        c->bufmax = MODES_NET_SNDBUF_SIZE;
    }
    if (!(c->buf = cmalloc(c->bufmax))) {
        fprintf(stderr, "Out of memory allocating client SendQ\n");
        exit(1);
    }

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

static void checkServiceConnected(struct net_connector *con, int64_t now) {

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
        fprintf(stderr, "%s: Connection to %s%s port %s failed: %d (%s)\n",
                con->service->descr, con->address, con->resolved_addr, con->port, optval, strerror(optval));
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

    // sending UUID if hostname matches adsbexchange
    char uuid[130];
    uuid[0] = '\0';
    if ((c->sendq && c->sendq_len + 256 < c->sendq_max)
                && ( (strstr(con->address, "feed") && strstr(con->address, ".adsbexchange.com")) || Modes.debug_ping )) {
        int fd = open(Modes.uuidFile, O_RDONLY);
        // try legacy / adsbexchange image path as hardcoded fallback
        if (fd == -1) {
            fd = open("/boot/adsbx-uuid", O_RDONLY);
        }
        int res = -1;
        if (fd != -1) {
            res = read(fd, uuid, 128);
            close(fd);
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
        if (flushClient(c, now) < 0) {
            return;
        }
    }
    if ((c->sendq && c->sendq_len + 256 < c->sendq_max)
                && strcmp(con->protocol, "gpsd_in") == 0) {
        c->sendq_len += snprintf(c->sendq, 256, "?WATCH={\"enable\":true,\"json\":true};\n");
        if (flushClient(c, now) < 0) {
            return;
        }
    }
    if (!Modes.interactive) {
        if (uuid[0]) {
            fprintf(stderr, "%s: Connection established: %s%s port %s (sending UUID: %s)\n",
                    con->service->descr, con->address, con->resolved_addr, con->port, uuid);
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
    con->backoff = imax(Modes.net_connector_delay / 32, con->backoff);

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
            fprintf(stderr, "%s: Name resolution for %s failed: %s\n", con->service->descr, con->address, gai_strerror(con->gai_error));
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
        struct net_connector *con = Modes.net_connectors[i];
        if (!con->connected) {
            // If we've exceeded our connect timeout, close connection.
            if (con->connecting && now >= con->connect_timeout) {
                fprintf(stderr, "%s: Connection timed out: %s:%s port %s\n",
                        con->service->descr, con->address, con->port, con->resolved_addr);
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
                    && c->service->read_mode != READ_MODE_IGNORE && c->service->read_mode != READ_MODE_BEAST_COMMAND
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

        int unix = 0;
        if (strncmp(p, "unix:", 5) == 0) {
            unix = 1;
            p += 5;
        }

        end = strpbrk(p, ", ");

        if (!end) {
            strncpy(buf, p, sizeof (buf));
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
        if (unix) {
            if (strstr(buf, ".sock")) {
                // unlink files which have sock in the path ... some rudimentary protection against users shooting themselves in the foot
                unlink(buf);
            }
            int fd = anetUnixSocket(Modes.aneterr, buf, SOCK_NONBLOCK);
            if (fd == ANET_ERR) {
                fprintf(stderr, "Error opening the listening port %s (%s): %s\n",
                        buf, service->descr, Modes.aneterr);
                exit(1);
            }
            sfree(service->unixSocket);
            service->unixSocket = strdup(buf);
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
        if (listenerCount && listenerCount % 2 == 0)
            fprintf(stderr, "\n");
        listenerCount++;
        char listenString[1024];
        snprintf(listenString, 1023, "%5s: %s port", buf, service->descr);
        fprintf(stderr, "%-38s", listenString);

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

void serviceClose(struct net_service *s) {
    if (!s)
        return;
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
    sfree(s);
}

void modesInitNet(void) {
    if (!Modes.net)
        return;
    struct net_service *beast_out;
    struct net_service *beast_reduce_out;
    struct net_service *garbage_out;
    struct net_service *raw_out;
    struct net_service *raw_in;
    struct net_service *vrs_out;
    struct net_service *json_out;
    struct net_service *sbs_out;
    struct net_service *sbs_out_replay;
    struct net_service *sbs_out_mlat;
    struct net_service *sbs_out_jaero;
    struct net_service *sbs_out_prio;
    struct net_service *sbs_in;
    struct net_service *sbs_in_mlat;
    struct net_service *sbs_in_jaero;
    struct net_service *sbs_in_prio;
    struct net_service *gpsd_in;

    signal(SIGPIPE, SIG_IGN);
    Modes.services = NULL;

    Modes.net_epfd = my_epoll_create();

    // set up listeners
    raw_out = serviceInit("Raw TCP output", &Modes.raw_out, send_raw_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(raw_out, Modes.net_bind_address, Modes.net_output_raw_ports, Modes.net_epfd);

    beast_out = serviceInit("Beast TCP output", &Modes.beast_out, send_beast_heartbeat, READ_MODE_BEAST_COMMAND, NULL, handleBeastCommand);
    serviceListen(beast_out, Modes.net_bind_address, Modes.net_output_beast_ports, Modes.net_epfd);

    beast_reduce_out = serviceInit("BeastReduce TCP output", &Modes.beast_reduce_out, send_beast_heartbeat, READ_MODE_BEAST_COMMAND, NULL, handleBeastCommand);

    serviceListen(beast_reduce_out, Modes.net_bind_address, Modes.net_output_beast_reduce_ports, Modes.net_epfd);

    garbage_out = serviceInit("Garbage TCP output", &Modes.garbage_out, send_beast_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(garbage_out, Modes.net_bind_address, Modes.garbage_ports, Modes.net_epfd);

    vrs_out = serviceInit("VRS json output", &Modes.vrs_out, NULL, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(vrs_out, Modes.net_bind_address, Modes.net_output_vrs_ports, Modes.net_epfd);

    json_out = serviceInit("Position json output", &Modes.json_out, NULL, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(json_out, Modes.net_bind_address, Modes.net_output_json_ports, Modes.net_epfd);

    sbs_out = serviceInit("SBS TCP output ALL", &Modes.sbs_out, send_sbs_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    serviceListen(sbs_out, Modes.net_bind_address, Modes.net_output_sbs_ports, Modes.net_epfd);

    sbs_out_replay = serviceInit("SBS TCP output MAIN", &Modes.sbs_out_replay, send_sbs_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    sbs_out_prio = serviceInit("SBS TCP output PRIO", &Modes.sbs_out_prio, send_sbs_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    sbs_out_mlat = serviceInit("SBS TCP output MLAT", &Modes.sbs_out_mlat, send_sbs_heartbeat, READ_MODE_IGNORE, NULL, NULL);
    sbs_out_jaero = serviceInit("SBS TCP output JAERO", &Modes.sbs_out_jaero, send_sbs_heartbeat, READ_MODE_IGNORE, NULL, NULL);

    serviceListen(sbs_out_jaero, Modes.net_bind_address, Modes.net_output_jaero_ports, Modes.net_epfd);

    if (strlen(Modes.net_output_sbs_ports) == 5 && Modes.net_output_sbs_ports[4] == '5') {

        char *replay = strdup(Modes.net_output_sbs_ports);
        replay[4] = '6';
        serviceListen(sbs_out_replay, Modes.net_bind_address, replay, Modes.net_epfd);

        char *mlat = strdup(Modes.net_output_sbs_ports);
        mlat[4] = '7';
        serviceListen(sbs_out_mlat, Modes.net_bind_address, mlat, Modes.net_epfd);

        char *prio = strdup(Modes.net_output_sbs_ports);
        prio[4] = '8';
        serviceListen(sbs_out_prio, Modes.net_bind_address, prio, Modes.net_epfd);

        char *jaero = strdup(Modes.net_output_sbs_ports);
        jaero[4] = '9';
        if (sbs_out_jaero->listener_count == 0)
            serviceListen(sbs_out_jaero, Modes.net_bind_address, jaero, Modes.net_epfd);

        sfree(replay);
        sfree(mlat);
        sfree(prio);
        sfree(jaero);
    }

    sbs_in = serviceInit("SBS TCP input MAIN", NULL, NULL, READ_MODE_ASCII, "\n",  decodeSbsLine);
    serviceListen(sbs_in, Modes.net_bind_address, Modes.net_input_sbs_ports, Modes.net_epfd);

    sbs_in_mlat = serviceInit("SBS TCP input MLAT", NULL, NULL, READ_MODE_ASCII, "\n",  decodeSbsLineMlat);
    sbs_in_prio = serviceInit("SBS TCP input PRIO", NULL, NULL, READ_MODE_ASCII, "\n",  decodeSbsLinePrio);
    sbs_in_jaero = serviceInit("SBS TCP input JAERO", NULL, NULL, READ_MODE_ASCII, "\n",  decodeSbsLineJaero);


    serviceListen(sbs_in_jaero, Modes.net_bind_address, Modes.net_input_jaero_ports, Modes.net_epfd);

    if (strlen(Modes.net_input_sbs_ports) == 5 && Modes.net_input_sbs_ports[4] == '6') {
        char *mlat = strdup(Modes.net_input_sbs_ports);
        mlat[4] = '7';
        serviceListen(sbs_in_mlat, Modes.net_bind_address, mlat, Modes.net_epfd);

        char *prio = strdup(Modes.net_input_sbs_ports);
        prio[4] = '8';
        serviceListen(sbs_in_prio, Modes.net_bind_address, prio, Modes.net_epfd);

        char *jaero = strdup(Modes.net_input_sbs_ports);
        jaero[4] = '9';
        if (sbs_in_jaero->listener_count == 0)
            serviceListen(sbs_in_jaero, Modes.net_bind_address, jaero, Modes.net_epfd);

        sfree(mlat);
        sfree(prio);
        sfree(jaero);
    }

    gpsd_in = serviceInit("GPSD TCP input", &Modes.gpsd_in, NULL, READ_MODE_ASCII, "\n", handle_gpsd);

    if (Modes.json_dir && Modes.json_globe_index && Modes.globe_history_dir) {
        /* command input */
        struct net_service *commandService = serviceInit("command input", NULL, NULL, READ_MODE_ASCII, "\n", handleCommandSocket);
        char commandSocketFile[PATH_MAX];
        char commandSocket[PATH_MAX];
        snprintf(commandSocketFile, PATH_MAX, "%s/cmd.sock", Modes.json_dir);
        unlink(commandSocketFile);
        snprintf(commandSocket, PATH_MAX, "unix:%s/cmd.sock", Modes.json_dir);
        serviceListen(commandService, Modes.net_bind_address, commandSocket, Modes.net_epfd);
        chmod(commandSocket, 0600);
    }

    raw_in = serviceInit("Raw TCP input", NULL, NULL, READ_MODE_ASCII, "\n", decodeHexMessage);
    serviceListen(raw_in, Modes.net_bind_address, Modes.net_input_raw_ports, Modes.net_epfd);

    /* Beast input via network */
    Modes.beast_in_service = serviceInit("Beast TCP input", &Modes.beast_in, NULL, READ_MODE_BEAST, NULL, decodeBinMessage);
    if (Modes.netIngest) {
        Modes.beast_in_service->sendqOverrideSize = MODES_NET_SNDBUF_SIZE;
        Modes.beast_in_service->recvqOverrideSize = MODES_NET_SNDBUF_SIZE;
        // --net-buffer won't increase receive buffer for ingest server to avoid running out of memory using lots of connections
    }
    serviceListen(Modes.beast_in_service, Modes.net_bind_address, Modes.net_input_beast_ports, Modes.net_epfd);

    // print newline after all the listen announcements in serviceListen
    fprintf(stderr, "\n");

    /* Beast input from local Modes-S Beast via USB */
    if (Modes.sdr_type == SDR_MODESBEAST || Modes.sdr_type == SDR_GNS) {
        createGenericClient(Modes.beast_in_service, Modes.beast_fd);
    }

    for (int i = 0; i < Modes.net_connectors_count; i++) {
        struct net_connector *con = Modes.net_connectors[i];
        if (strcmp(con->protocol, "beast_out") == 0)
            con->service = beast_out;
        else if (strcmp(con->protocol, "beast_in") == 0)
            con->service = Modes.beast_in_service;
        if (strcmp(con->protocol, "beast_reduce_out") == 0)
            con->service = beast_reduce_out;
        else if (strcmp(con->protocol, "raw_out") == 0)
            con->service = raw_out;
        else if (strcmp(con->protocol, "raw_in") == 0)
            con->service = raw_in;
        else if (strcmp(con->protocol, "vrs_out") == 0)
            con->service = vrs_out;
        else if (strcmp(con->protocol, "json_out") == 0)
            con->service = json_out;
        else if (strcmp(con->protocol, "sbs_out") == 0)
            con->service = sbs_out;
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

        int maxModesClients = Modes.max_fds * 7 / 8;
        if (Modes.modesClientCount > maxModesClients) {
            // drop new modes clients if the count nears max_fds ... we want some extra fds for other stuff
            anetCloseSocket(c->fd);
            static int64_t antiSpam;
            if (now > antiSpam) {
                antiSpam = now + 30 * SECONDS;
                fprintf(stderr, "<3> Can't accept new connection, limited to %d clients, consider increasing ulimit!\n", maxModesClients);
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
            if (!Modes.netIngest && Modes.debug_net) {
                fprintf(stderr, "%s: new c from %s port %s (fd %d)\n",
                        c->service->descr, c->host, c->port, fd);
            }
            if (anetTcpKeepAlive(Modes.aneterr, fd) != ANET_OK)
                fprintf(stderr, "%s: Unable to set keepalive on connection from %s port %s (fd %d)\n", c->service->descr, c->host, c->port, fd);
        } else {
            fprintf(stderr, "%s: Fatal: createSocketClient shouldn't fail!\n", s->descr);
            exit(1);
        }
    }

    if (!(errno & (EMFILE | EINTR | EAGAIN | EWOULDBLOCK))) {
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
    if (Modes.netIngest && c->garbage < GARBAGE_THRESHOLD) {
        double elapsed = (mstime() - c->connectedSince + 1) / 1000.0;
        double kbitpersecond = c->bytesReceived / 128.0 / elapsed;

        char uuid[64]; // needs 36 chars and null byte
        sprint_uuid(c->receiverId, c->receiverId2, uuid);
        fprintf(stderr, "disc: %6.1f s %6.2f kbit/s %s \n",
                elapsed, kbitpersecond, c->proxy_string);
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
        con->backoff = imax(Modes.net_connector_delay / 32, con->backoff);

        // if we were connected for some time, an immediate reconnect is expected
        con->next_reconnect = con->lastConnect + con->backoff;
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
        fprintf(stderr, "new client:                  rId %s %s\n",
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
    if ((c->latest_rtt > PING_REJECT && now > antiSpam) || c->rtt > PING_DISCONNECT) {
            char uuid[64]; // needs 36 chars and null byte
            sprint_uuid(c->receiverId, c->receiverId2, uuid);
            if (Modes.netIngest) {
                antiSpam = now + 250; // limit to 4 messages a second
                fprintf(stderr, "reject: %6.0f ms %6.0f ms  rId %s %s\n",
                        c->latest_rtt, c->recent_rtt, uuid, c->proxy_string);
            } else {
                antiSpam = now + 15 * SECONDS;
                fprintf(stderr, "<3>high network delay: %6.0f ms %6.0f ms  rId %s %s current: %ld pong: %ld\n",
                        c->latest_rtt, c->recent_rtt, uuid, c->proxy_string, (long) current, (long) pong);
            }
    }
    if (c->latest_rtt > PING_REDUCE) {
        // tell the client to slow down via beast command
        // misuse pingReceived as a timeout variable
        if (now > c->pingReceived + PING_REDUCE_IVAL / 2) {
            if (now > antiSpam) {
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
    for (struct client *c = writer->service->clients; c; c = c->next) {
        if (!c->service)
            continue;
        if (c->service->writer == writer->service->writer) {
            if (c->pingEnabled) {
                pong(c, now);
            }
            if ((c->sendq_len + writer->dataUsed) >= c->sendq_max) {
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
    writer->dataUsed = endptr - writer->data;

    if (writer->dataUsed >= Modes.net_output_flush_size) {
        flushWrites(writer);
    }
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
    *p++ = (ch = (mm->timestampMsg >> 40));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (mm->timestampMsg >> 32));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (mm->timestampMsg >> 24));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (mm->timestampMsg >> 16));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (mm->timestampMsg >> 8));
    if (0x1A == ch) {
        *p++ = ch;
    }
    *p++ = (ch = (mm->timestampMsg));
    if (0x1A == ch) {
        *p++ = ch;
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

    completeWrite(writer, p);
}

static void send_beast_heartbeat(struct net_service *service) {
    static char heartbeat_message[] = {0x1a, '1', 0, 0, 0, 0, 0, 0, 0, 0, 0};
    char *data;

    if (!service->writer)
        return;

    data = prepareWrite(service->writer, sizeof (heartbeat_message));
    if (!data)
        return;

    memcpy(data, heartbeat_message, sizeof (heartbeat_message));
    completeWrite(service->writer, data + sizeof (heartbeat_message));
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

    if (Modes.mlat && mm->timestampMsg) {
        /* timestamp, big-endian */
        sprintf(p, "@%012" PRIX64,
                mm->timestampMsg);
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

static void send_raw_heartbeat(struct net_service *service) {
    static char *heartbeat_message = "*0000;\n";
    char *data;
    int len = strlen(heartbeat_message);

    if (!service->writer)
        return;

    data = prepareWrite(service->writer, len);
    if (!data)
        return;

    memcpy(data, heartbeat_message, len);
    completeWrite(service->writer, data + len);
}

//
//=========================================================================
//
// Read SBS input from TCP clients
//
static int decodeSbsLine(struct client *c, char *line, int remote, int64_t now) {
    struct modesMessage mm;
    size_t line_len = strlen(line);
    size_t max_len = 200;

    if (Modes.receiver_focus && c->receiverId != Modes.receiver_focus)
        return 0;
    if (line_len < 2) // heartbeat
        return 0;
    if (line_len < 20 || line_len >= max_len)
        goto basestation_invalid;

    memset(&mm, 0, sizeof(mm));
    mm.client = c;

    char *p = line;
    char *t[23]; // leave 0 indexed entry empty, place 22 tokens into array

    MODES_NOTUSED(c);
    if (remote >= 64)
        mm.source = remote - 64;
    else
        mm.source = SOURCE_SBS;

    char *out = NULL;

    out = prepareWrite(&Modes.sbs_out, max_len);
    if (out) {
        memcpy(out, line, line_len);
        //fprintf(stderr, "%s", out);
        out += line_len;
        out += sprintf(out, "\r\n");
        completeWrite(&Modes.sbs_out, out);
    }

    out = NULL;
    switch(mm.source) {
        case SOURCE_SBS:
            out = prepareWrite(&Modes.sbs_out_replay, max_len);
            mm.addrtype = ADDR_OTHER;
            break;
        case SOURCE_MLAT:
            out = prepareWrite(&Modes.sbs_out_mlat, max_len);
            mm.addrtype = ADDR_MLAT;
            break;
        case SOURCE_JAERO:
            out = prepareWrite(&Modes.sbs_out_jaero, max_len);
            mm.addrtype = ADDR_JAERO;
            break;
        case SOURCE_PRIO:
            out = prepareWrite(&Modes.sbs_out_prio, max_len);
            mm.addrtype = ADDR_OTHER;
            break;

        default:
            mm.addrtype = ADDR_OTHER;
    }

    if (out) {
        memcpy(out, line, line_len);
        // completeWrite happens at end of function
    }


    // Mark messages received over the internet as remote so that we don't try to
    // pass them off as being received by this instance when forwarding them
    mm.remote = 1;
    mm.signalLevel = 0;
    mm.sbs_in = 1;

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
    //int msg_type = atoi(t[2]);

    if (!t[5] || strlen(t[5]) != 6) // icao must be 6 characters
        goto basestation_invalid;

    char *icao = t[5];
    unsigned char *chars = (unsigned char *) &(mm.addr);
    for (int j = 0; j < 6; j += 2) {
        int high = hexDigitVal(icao[j]);
        int low = hexDigitVal(icao[j + 1]);

        if (high == -1 || low == -1)
            goto basestation_invalid;

        chars[2 - j / 2] = (high << 4) | low;
    }

    //fprintf(stderr, "%x type %s: ", mm.addr, t[2]);
    //fprintf(stderr, "%x: %d, %0.5f, %0.5f\n", mm.addr, mm.baro_alt, mm.decoded_lat, mm.decoded_lon);
    //field 11, callsign
    if (t[11] && strlen(t[11]) > 0) {
        strncpy(mm.callsign, t[11], 9);
        mm.callsign[8] = '\0';
        mm.callsign_valid = 1;
        for (unsigned i = 0; i < 8; ++i) {
            if (mm.callsign[i] == '\0')
                mm.callsign[i] = ' ';
            if (!(mm.callsign[i] >= 'A' && mm.callsign[i] <= 'Z') &&
                    !(mm.callsign[i] >= '0' && mm.callsign[i] <= '9') &&
                    mm.callsign[i] != ' ') {
                // Bad callsign, ignore it
                mm.callsign_valid = 0;
                break;
            }
        }
        //fprintf(stderr, "call: %s, ", mm.callsign);
    }
    // field 12, altitude
    if (t[12] && strlen(t[12]) > 0) {
        mm.baro_alt = atoi(t[12]);
        if (mm.baro_alt > -5000 && mm.baro_alt < 100000) {
            mm.baro_alt_valid = 1;
            mm.baro_alt_unit = UNIT_FEET;
        }
        //fprintf(stderr, "alt: %d, ", mm.baro_alt);
    }
    // field 13, groundspeed
    if (t[13] && strlen(t[13]) > 0) {
        mm.gs.v0 = strtod(t[13], NULL);
        if (mm.gs.v0 > 0)
            mm.gs_valid = 1;
        //fprintf(stderr, "gs: %.1f, ", mm.gs.selected);
    }
    //field 14, heading
    if (t[14] && strlen(t[14]) > 0) {
        mm.heading_valid = 1;
        mm.heading = strtod(t[14], NULL);
        mm.heading_type = HEADING_GROUND_TRACK;
        //fprintf(stderr, "track: %.1f, ", mm.heading);
    }
    // field 15 and 16, position
    if (t[15] && strlen(t[15]) && t[16] && strlen(t[16])) {
        mm.decoded_lat = strtod(t[15], NULL);
        mm.decoded_lon = strtod(t[16], NULL);
        if (mm.decoded_lat != 0 && mm.decoded_lon != 0)
            mm.sbs_pos_valid = 1;
        //fprintf(stderr, "pos: (%.2f, %.2f)\n", mm.decoded_lat, mm.decoded_lon);
    }
    // field 17 vertical rate, assume baro
    if (t[17] && strlen(t[17]) > 0) {
        mm.baro_rate = atoi(t[17]);
        mm.baro_rate_valid = 1;
        //fprintf(stderr, "vRate: %d, ", mm.baro_rate);
    }
    // field 18 squawk
    if (t[18] && strlen(t[18]) > 0) {
        long int tmp = strtol(t[18], NULL, 10);
        if (tmp > 0) {
            mm.squawk = (tmp / 1000) * 16*16*16 + (tmp / 100 % 10) * 16*16 + (tmp / 10 % 10) * 16 + (tmp % 10);
            mm.squawk_valid = 1;
            //fprintf(stderr, "squawk: %04x %s, ", mm.squawk, t[18]);
        }
    }
    // field 19 (originally squawk change) used to indicate by some versions of mlat-server the number of receivers which contributed to the postiions
    if (mm.source == SOURCE_MLAT && t[19] && strlen(t[19]) > 0) {
        long int tmp = strtol(t[19], NULL, 10);
        if (tmp > 0) {
            mm.receiverCountMlat = tmp;
        }
    }
    // field 20 (originally emergency status) used to indicate by some versions of mlat-server the estimated error in km
    if (mm.source == SOURCE_MLAT && t[20] && strlen(t[20]) > 0) {
        long tmp = strtol(t[20], NULL, 10);
        if (tmp > 0) {
            mm.mlatEPU = tmp;
            if (tmp > UINT16_MAX)
                mm.mlatEPU = UINT16_MAX;

            //fprintf(stderr, "mlatEPU: %d\n", mm.mlatEPU);
        }
    }

    // field 22 ground status
    if (t[22] && strlen(t[22]) > 0) {
        if (atoi(t[22]) > 0) {
            mm.airground = AG_GROUND;
        } else if (strcmp(t[22], "0")) {
            mm.airground = AG_AIRBORNE;
        }
        //fprintf(stderr, "onground, ");
    }


    // set nic / rc to 0 / unknown
    mm.decoded_nic = 0;
    mm.decoded_rc = RC_UNKNOWN;

    //fprintf(stderr, "\n");

    // record reception time as the time we read it.
    mm.sysTimestampMsg = now;

    useModesMessage(&mm);

    Modes.stats_current.remote_received_basestation_valid++;

    // don't write SBS message if sbsReduce is active and this message didn't have useful data
    if (out && (!Modes.sbsReduce || mm.reduce_forward)) {
        out += line_len;
        *out++ = '\r';
        *out++ = '\n';

        if (mm.source == SOURCE_SBS)
            completeWrite(&Modes.sbs_out_replay, out);
        if (mm.source == SOURCE_MLAT)
            completeWrite(&Modes.sbs_out_mlat, out);
        if (mm.source == SOURCE_JAERO)
            completeWrite(&Modes.sbs_out_jaero, out);
        if (mm.source == SOURCE_PRIO)
            completeWrite(&Modes.sbs_out_prio, out);
    }

    return 0;

basestation_invalid:

    for (size_t i = 0; i < line_len; i++)
        line[i] = (line[i] == '\0' ? ',' : line[i]);

    if (Modes.debug_garbage)
        fprintf(stderr, "SBS invalid: %.*s\n", (int) line_len, line);
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

    // Fields 1 to 6 : SBS message type and ICAO address of the aircraft and some other stuff
    p += sprintf(p, "MSG,%d,1,1,%06X,1,", msgType, mm->addr);

    // Find current system time
    clock_gettime(CLOCK_REALTIME, &now);
    gmtime_r(&now.tv_sec, &stTime_now);

    // Find message reception time
    time_t received = (time_t) (mm->sysTimestampMsg / 1000);
    gmtime_r(&received, &stTime_receive);

    // Fields 7 & 8 are the message reception time and date
    p += sprintf(p, "%04d/%02d/%02d,", (stTime_receive.tm_year + 1900), (stTime_receive.tm_mon + 1), stTime_receive.tm_mday);
    p += sprintf(p, "%02d:%02d:%02d.%03u,", stTime_receive.tm_hour, stTime_receive.tm_min, stTime_receive.tm_sec, (unsigned) (mm->sysTimestampMsg % 1000));

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
    if (mm->cpr_decoded) {
        p += sprintf(p, ",%1.5f,%1.5f", mm->decoded_lat, mm->decoded_lon);
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

    // Field 19 is the Squawk Changing Alert flag (if we have it)
    if (mm->alert_valid) {
        if (mm->alert) {
            p += sprintf(p, ",-1");
        } else {
            p += sprintf(p, ",0");
        }
    } else {
        p += sprintf(p, ",");
    }

    // Field 20 is the Squawk Emergency flag (if we have it)
    if (mm->squawk_valid) {
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

static void send_sbs_heartbeat(struct net_service *service) {
    static char *heartbeat_message = "\r\n"; // is there a better one?
    char *data;
    int len = strlen(heartbeat_message);

    if (!service->writer)
        return;

    data = prepareWrite(service->writer, len);
    if (!data)
        return;

    memcpy(data, heartbeat_message, len);
    completeWrite(service->writer, data + len);
}

void jsonPositionOutput(struct modesMessage *mm, struct aircraft *a) {
    MODES_NOTUSED(mm);
    char *p;

    int buflen = 8192;

    p = prepareWrite(&Modes.json_out, buflen);
    if (!p)
        return;

    char *end = p + buflen;

    p = sprintAircraftObject(p, end, a, mm->sysTimestampMsg, 2, NULL);

    if (p + 1 < end) {
        *p++ = '\n';
        completeWrite(&Modes.json_out, p);
        if (Modes.json_out.dataUsed > 0) {
            // flush unconditionally for this output
            flushWrites(&Modes.json_out);
        }
    } else {
        fprintf(stderr, "buffer insufficient jsonPositionOutput()\n");
    }
}
//
//=========================================================================
//
void modesQueueOutput(struct modesMessage *mm, struct aircraft *a) {
    int is_mlat = (mm->source == SOURCE_MLAT);

    if (Modes.garbage_ports && (mm->garbage || mm->pos_bad) && !mm->pos_old && Modes.garbage_out.connections) {
        modesSendBeastOutput(mm, &Modes.garbage_out);
    }

    if (a && (!Modes.sbsReduce || mm->reduce_forward)) {
        if (Modes.sbs_out.connections)
            modesSendSBSOutput(mm, a, &Modes.sbs_out);
        if (is_mlat && Modes.sbs_out_mlat.connections)
            modesSendSBSOutput(mm, a, &Modes.sbs_out_mlat);
    }

    if (!is_mlat && (Modes.net_verbatim || mm->correctedbits < 2) && Modes.raw_out.connections) {
        // Forward 2-bit-corrected messages via raw output only if --net-verbatim is set
        // Don't ever forward mlat messages via raw output.
        modesSendRawOutput(mm);
    }

    if ((!is_mlat || Modes.forward_mlat) && (Modes.net_verbatim || mm->correctedbits < 2)) {
        // Forward 2-bit-corrected messages via beast output only if --net-verbatim is set
        // Forward mlat messages via beast output only if --forward-mlat is set
        if (Modes.beast_out.connections)
            modesSendBeastOutput(mm, &Modes.beast_out);
        if (mm->reduce_forward && Modes.beast_reduce_out.connections) {
            modesSendBeastOutput(mm, &Modes.beast_reduce_out);
        }
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
    struct net_service *s;
    struct client *c;

    if (!Modes.mode_ac_auto)
        return;

    Modes.mode_ac = 0;
    for (s = Modes.services; s; s = s->next) {
        for (c = s->clients; c; c = c->next) {
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

static int handle_gpsd(struct client *c, char *p, int remote, int64_t now) {
    MODES_NOTUSED(c);
    MODES_NOTUSED(remote);
    MODES_NOTUSED(now);
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
    if (!strstr(p, "\"class\":\"TPV\"")) {
        return 0;
    }
    // filter all messages which don't have lat / lon
    char *latp = strstr(p, "\"lat\":");
    char *lonp = strstr(p, "\"lon\":");
    if (!latp || !lonp) {
        return 0;
    }
    latp += 6;
    lonp += 6;

    char *saveptr = NULL;
    strtok_r(latp, ",", &saveptr);
    strtok_r(lonp, ",", &saveptr);

    double lat = strtod(latp, NULL);
    double lon = strtod(lonp, NULL);

    //fprintf(stderr, "%11.6f %11.6f\n", lat, lon);


    if (!isfinite(lat) || lat < -89.9 || lat > 89.9 || !isfinite(lon) || lon < -180 || lon > 180) {
        return 0;
    }
    if (fabs(lat) < 0.1 && fabs(lon) < 0.1) {
        return 0;
    }

    Modes.fUserLat = lat;
    Modes.fUserLon = lon;
    Modes.userLocationValid = 1;

    if (Modes.json_dir) {
        free(writeJsonToFile(Modes.json_dir, "receiver.json", generateReceiverJson()).buffer); // location changed
    }

    return 0;
}

static int handleCommandSocket(struct client *c, char *p, int remote, int64_t now) {
    MODES_NOTUSED(c);
    MODES_NOTUSED(remote);
    MODES_NOTUSED(now);
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
        struct hexInterval* new = malloc(sizeof(struct hexInterval));
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
static int handleBeastCommand(struct client *c, char *p, int remote, int64_t now) {
    MODES_NOTUSED(remote);
    MODES_NOTUSED(now);
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
                    // only log this at most every 15 minutes and only if it's already active
                    if (now < Modes.doubleBeastReduceIntervalUntil && now > antiSpam) {
                        antiSpam = now + 900 * SECONDS;
                        fprintf(stderr, "%s: High latency, reducing data usage temporarily.\n", c->service->descr);
                    }
                }
                Modes.doubleBeastReduceIntervalUntil = now + PING_REDUCE_IVAL;
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
static int decodeBinMessage(struct client *c, char *p, int remote, int64_t now) {
    int msgLen = 0;
    int j;
    unsigned char ch;
    struct modesMessage mm;
    unsigned char *msg = mm.msg;

    memset(&mm, 0, sizeof(mm));
    mm.client = c;

    ch = *p++; /// Get the message type

    mm.receiverId = c->receiverId;

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
    mm.remote = remote;

    mm.timestampMsg = 0;
    // Grab the timestamp (big endian format)
    for (j = 0; j < 6; j++) {
        ch = *p++;
        mm.timestampMsg = mm.timestampMsg << 8 | (ch & 255);
    }

    // record reception time as the time we read it.
    mm.sysTimestampMsg = now;


    ch = *p++; // Grab the signal level
    mm.signalLevel = ((unsigned char) ch / 255.0);
    mm.signalLevel = mm.signalLevel * mm.signalLevel;

    /* In case of Mode-S Beast use the signal level per message for statistics */
    if (Modes.sdr_type == SDR_MODESBEAST) {
        Modes.stats_current.signal_power_sum += mm.signalLevel;
        Modes.stats_current.signal_power_count += 1;

        if (mm.signalLevel > Modes.stats_current.peak_signal_power)
            Modes.stats_current.peak_signal_power = mm.signalLevel;
        if (mm.signalLevel > 0.50119)
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
        decodeModeAMessage(&mm, ((msg[0] << 8) | msg[1]));
        result = 0;
    } else {
        if (remote) {
            Modes.stats_current.remote_received_modes++;
        } else {
            Modes.stats_current.demod_preambles++;
        }
        result = decodeModesMessage(&mm);
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
                Modes.stats_current.remote_accepted[mm.correctedbits]++;
            } else {
                Modes.stats_current.demod_accepted[mm.correctedbits]++;
            }
        }
    }
    if (c->pongReceived && c->pongReceived > now + 100) {
        // if messages are received with more than 100 ms delay after a pong, recalculate c->rtt
        pongReceived(c, now);
    }
    if (c->rtt && c->rtt > PING_REJECT && Modes.netIngest) {
        // don't discard CPRs, if we have better data speed_check generally will take care of delayed CPR messages
        // this way we get basic data even from high latency receivers
        // super high latency receivers are getting disconnected in pongReceived()
        if (!mm.cpr_valid) {
            Modes.stats_current.remote_rejected_delayed++;
            return 0; // discard
        }
    }

    if ((Modes.garbage_ports || Modes.netReceiverId) && receiverCheckBad(mm.receiverId, now)) {
        mm.garbage = 1;
    }

    useModesMessage(&mm);
    return 0;
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
static int decodeHexMessage(struct client *c, char *hex, int remote, int64_t now) {
    int l = strlen(hex), j;
    struct modesMessage mm;
    unsigned char *msg = mm.msg;

    MODES_NOTUSED(remote);
    MODES_NOTUSED(c);

    memset(&mm, 0, sizeof(mm));
    mm.client = c;

    // Mark messages received over the internet as remote so that we don't try to
    // pass them off as being received by this instance when forwarding them
    mm.remote = 1;
    mm.signalLevel = 0;

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
        mm.signalLevel = strtol(token, NULL, 10);
        mm.signalLevel /= 1000; // let's assume 1000 mV max .. i only have a small sample, specification isn't clear
        mm.signalLevel = mm.signalLevel * mm.signalLevel; // square it to get power
        mm.signalLevel = fmin(1.0, mm.signalLevel); // cap at 1
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
            mm.timestampMsg = (seconds * (1000 * 1000) + after_pps) * 12;
        } else {
            mm.timestampMsg = now * 12e3; // make 12 MHz timestamp from microseconds
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
                mm.timestampMsg = (mm.timestampMsg << 4) | hexDigitVal(*hex);
                hex++;
                l--;
            }
            if (l < 2)
                return (0);
            mm.signalLevel = ((hexDigitVal(hex[0]) << 4) | hexDigitVal(hex[1])) / 255.0;
            hex += 2;
            l -= 2;
            mm.signalLevel = mm.signalLevel * mm.signalLevel;
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
                mm.timestampMsg = (mm.timestampMsg << 4) | hexDigitVal(*hex);
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
    mm.sysTimestampMsg = now;

    if (l == (MODEAC_MSG_BYTES * 2)) { // ModeA or ModeC
        Modes.stats_current.remote_received_modeac++;
        decodeModeAMessage(&mm, ((msg[0] << 8) | msg[1]));
    } else { // Assume ModeS
        int result;

        Modes.stats_current.remote_received_modes++;
        result = decodeModesMessage(&mm);
        if (result < 0) {
            if (result == -1)
                Modes.stats_current.remote_rejected_unknown_icao++;
            else
                Modes.stats_current.remote_rejected_bad++;
            return 0;
        } else {
            Modes.stats_current.remote_accepted[mm.correctedbits]++;
        }
    }

    useModesMessage(&mm);
    return (0);
}

static const char *hexEscapeString(const char *str, char *buf, int len) {
    const char *in = str;
    char *out = buf, *end = buf + len - 10;

    for (; out < end; ++in) {
        unsigned char ch = *in;
        *out++ = '.';
        if (ch == '"' || ch == '\\') {
            *out++ = '\\';
            *out++ = ch;
        } else if (ch < 32 || ch > 126) {
            out = safe_snprintf(out, end, "%02x", ch);
        } else {
            *out++ = ' ';
            *out++ = ch;
        }
    }

    *out++ = 0;
    return buf;
}


//
//=========================================================================
//
// This function polls the clients using read() in order to receive new
// messages from the net.
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
static void modesReadFromClient(struct client *c, int64_t start) {
    if (!c->service) {
        fprintf(stderr, "c->service null jahFuN3e\n");
        return;
    }

    int left;
    int nread;
    int bContinue = 1;
    int discard = 0;

    int64_t now = start;

    for (int loop = 0; bContinue && loop < 32; loop++, now = mstime()) {

        if (!discard && now > start + 200) {
            discard = 1;
            static int64_t antiSpam;
            if (now > antiSpam + 5 * SECONDS) {
                antiSpam = now;
                if (Modes.netIngest && c->proxy_string[0] != '\0')
                    fprintf(stderr, "<3>ERROR, not enough CPU: Discarding data from: %s (suppressing for 5 seconds)\n", c->proxy_string);
                else
                    fprintf(stderr, "<3>%s: ERROR, not enough CPU: Discarding data from: %s port %s (fd %d) (suppressing for 5 seconds)\n",
                            c->service->descr, c->host, c->port, c->fd);
            }
        }
        if (discard)
            c->buflen = 0;

        left = c->bufmax - c->buflen - 4; // leave 4 extra byte for NUL termination in the ASCII case

        // If our buffer is full discard it, this is some badly formatted shit
        if (left <= 0) {
            c->garbage += c->buflen;
            Modes.stats_current.remote_malformed_beast += c->buflen;
            c->buflen = 0;
            left = c->bufmax - c->buflen - 4; // leave 4 extra byte for NUL termination in the ASCII case
            // If there is garbage, read more to discard it ASAP
        }
        // read instead of recv for modesbeast / gns-hulc ....
        nread = read(c->fd, c->buf + c->buflen, left);
        int err = errno;

        // If we didn't get all the data we asked for, then return once we've processed what we did get.
        if (nread != left) {
            bContinue = 0;
        }

        if (nread > 0) {
            c->last_read = now;
        }

        if (nread < 0) {
            if (err == EAGAIN || err == EWOULDBLOCK) {
                // No data available, check later!
                return;
            }
            // Other errors
            if (Modes.debug_net) {
                fprintf(stderr, "%s: Socket Error: %s: %s port %s (fd %d, SendQ %d, RecvQ %d)\n",
                        c->service->descr, strerror(err), c->host, c->port,
                        c->fd, c->sendq_len, c->buflen);
            }
            modesCloseClient(c);
            return;
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
            return;
        }

        if (discard)
            continue;

        c->buflen += nread;
        c->bytesReceived += nread;

        char *som = c->buf; // first byte of next message
        char *eod = c->buf + c->buflen; // one byte past end of data
        // NUL-terminate so we are free to use strstr()
        // nb: we never fill the last byte of the buffer with read data (see above) so this is safe
        *eod = '\0';
        char *p;
        int remote = 1; // Messages will be marked remote by default
        if ((c->fd == Modes.beast_fd) && (Modes.sdr_type == SDR_MODESBEAST || Modes.sdr_type == SDR_GNS)) {
            /* Message from a local connected Modes-S beast or GNS5894 are passed off the internet */
            remote = 0;
        }

        //char *lastSom = som;

        // check for PROXY v1 header if connection is new / low bytes received
        if (Modes.netIngest && c->bytesReceived <= 128 * 1024) {
            // disable this for the time being
            if (c->buflen > 5 && som[0] == 'P' && som[1] == 'R') {
                char *proxy = strstr(som, "PROXY ");
                char *eop = strstr(som, "\r\n");
                if (proxy && proxy == som) {
                    if (!eop) // incomplete proxy string (shouldn't happen but let's check anyhow)
                        break;
                    *eop = '\0';
                    strncpy(c->proxy_string, proxy + 6, sizeof(c->proxy_string));
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

                    som = eop + 2;
                }
            }
        }

        if (c->service->read_mode == READ_MODE_BEAST) {
            // This is the Beast Binary scanning case.
            // If there is a complete message still in the buffer, there must be the separator 'sep'
            // in the buffer, note that we full-scan the buffer at every read for simplicity.


            // disconnect garbage feeds
            if (c->garbage >= GARBAGE_THRESHOLD) {
                if (1 || !Modes.netIngest || Modes.debug_receiver) {
                    *eod = '\0';
                    char sample[64];
                    hexEscapeString(som, sample, sizeof(sample));
                    sample[sizeof(sample) - 1] = '\0';
                    if (Modes.netIngest && c->proxy_string[0] != '\0')
                        fprintf(stderr, "Garbage: Close: %s sample: %s\n", c->proxy_string, sample);
                    else
                        fprintf(stderr, "Garbage: Close: %s port %s sample: %s\n", c->host, c->port, sample);
                }
                modesCloseClient(c);
                return;
            }
            while (som < eod && ((p = memchr(som, (char) 0x1a, eod - som)) != NULL)) { // The first byte of buffer 'should' be 0x1a

                c->garbage += p - som;
                Modes.stats_current.remote_malformed_beast += p - som;

                //lastSom = p;
                som = p; // consume garbage up to the 0x1a
                ++p; // skip 0x1a

                if (p >= eod) {
                    // Incomplete message in buffer, retry later
                    break;
                }

                char *eom; // one byte past end of message
                unsigned char ch;

                if (!c->service) { fprintf(stderr, "c->service null ohThee9u\n"); }

                // Check for message with receiverId prepended
                ch = *p;
                if (ch == 0xe3) {
                    p++;
                    uint64_t receiverId = 0;
                    eom = p + 8;
                    // we need to be careful of double escape characters in the receiverId
                    for (int j = 0; j < 8 && p < eod && p < eom; j++) {
                        ch = *p++;
                        if (ch == 0x1A) {
                            ch = *p++;
                            eom++;
                            if (p < eod && ch != 0x1A) { // check that it's indeed a double escape
                                // might be start of message rather than double escape.
                                c->garbage += p - 1 - som;
                                Modes.stats_current.remote_malformed_beast += p - 1 - som;
                                som = p - 1;
                                goto beastWhileContinue;
                            }
                        }
                        // Grab the receiver id (big endian format)
                        receiverId = receiverId << 8 | (ch & 255);
                    }
                    if (!Modes.netIngest) {
                        c->receiverId = receiverId;
                    }

                    if (eom + 2 > eod)// Incomplete message in buffer, retry later
                        break;

                    som = p; // set start of next message
                    p++; // skip 0x1a
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
                        char *sampleStart = som - 32;
                        if (sampleStart < c->buf)
                            sampleStart = c->buf;
                        *som = 'X';
                        hexEscapeString(sampleStart, sample, sizeof(sample));
                        *som = 0x1a;
                        sample[sizeof(sample) - 1] = '\0';
                        fprintf(stderr, "modeAC: som pos %d, sample %s, eom > eod %d\n", (int) (som - c->buf), sample, eom > eod);
                    }
                } else if (ch == '5') {
                    eom = p + MODES_LONG_MSG_BYTES + 8;
                } else if (ch == 0xe4) {
                    // read UUID and continue with next message
                    p++;
                    som = read_uuid(c, p, eod);
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
                            return;
                        }
                        if (flushClient(c, now) < 0) {
                            return;
                        }
                        if (!c->service) {
                            fprintf(stderr, "c->service null EshaeC7n\n");
                            return;
                        }
                    }
                    som += 2;
                    continue;
                } else {
                    // Not a valid beast message, skip 0x1a
                    // Skip following byte as well:
                    // either: 0x1a (likely not a start of message but rather escaped 0x1a)
                    // or: any other char is skipped anyhow when looking for the next 0x1a
                    som += 2;
                    Modes.stats_current.remote_malformed_beast += 2;
                    c->garbage += 2;
                    continue;
                }

                if (!c->service) { fprintf(stderr, "c->service null quooJ1ea\n"); return; }

                if (eom > eod) // Incomplete message in buffer, retry later
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
                            if (eom > eod) { // Incomplete message in buffer, retry later
                                goto beastWhileBreak;
                            }
                            if (*p != (char) 0x1A) { // check that it's indeed a double escape
                                // might be start of message rather than double escape.
                                //
                                c->garbage += p - 1 - som;
                                Modes.stats_current.remote_malformed_beast += p - 1 - som;
                                som = p - 1;

                                if (0) {
                                    char sample[256];
                                    char *sampleStart = som - 32;
                                    if (sampleStart < c->buf)
                                        sampleStart = c->buf;
                                    *som = 'X';
                                    hexEscapeString(sampleStart, sample, sizeof(sample));
                                    *som = 0x1a;
                                    sample[sizeof(sample) - 1] = '\0';
                                    fprintf(stderr, "not a double Escape: som pos %d, sample %s, eom - som %d\n", (int) (som - c->buf), sample, (int) (eom - som));

                                }

                                goto beastWhileContinue;
                            }
                        }
                        *t++ = *p++;
                    }
                    noEscape = noEscapeStorage;
                }

                if (eom > eod) // Incomplete message in buffer, retry later
                    break;

                if (Modes.receiver_focus && c->receiverId != Modes.receiver_focus && noEscape[0] != 'P') {
                    // advance to next message
                    som = eom;
                    continue;
                }

                if (!c->service) {
                    fprintf(stderr, "c->service null hahGh1Sh\n");
                    return;
                }
                // Have a 0x1a followed by 1/2/3/4/5 - pass message to handler.
                int res = c->service->read_handler(c, noEscape, remote, now);
                if (!c->service) {
                    return;
                }
                if (res) {
                    modesCloseClient(c);
                    return;
                }


                // if we get some valid data, reduce the garbage counter.
                if (c->garbage > 128)
                    c->garbage -= 128;

                // advance to next message
                som = eom;

beastWhileContinue:
                ;
            }
beastWhileBreak:

            if (eod - som > 256) {
                //fprintf(stderr, "beastWhile too much data remaining, garbage?!\n");
                c->garbage += eod - som;
                Modes.stats_current.remote_malformed_beast += eod - som;
                som = eod;
            }

        } else if (c->service->read_mode == READ_MODE_IGNORE) {
            // drop the bytes on the floor
            som = eod;

        } else if (c->service->read_mode == READ_MODE_BEAST_COMMAND) {
            while (som < eod && ((p = memchr(som, (char) 0x1a, eod - som)) != NULL)) { // The first byte of buffer 'should' be 0x1a
                char *eom; // one byte past end of message

                som = p; // consume garbage up to the 0x1a
                ++p; // skip 0x1a

                if (p >= eod) {
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
                    ++som;
                    continue;
                }

                // we need to be careful of double escape characters in the message body
                for (p = som + 1; p < eod && p < eom; p++) {
                    if (0x1A == *p) {
                        p++;
                        eom++;
                    }
                }

                if (eom > eod) { // Incomplete message in buffer, retry later
                    break;
                }

                // Pass message to handler.
                if (c->service->read_handler(c, som + 1, remote, now)) {
                    modesCloseClient(c);
                    return;
                }

                // advance to next message
                som = eom;
            }

        } else if (c->service->read_mode == READ_MODE_ASCII) {
            //
            // This is the ASCII scanning case, AVR RAW or HTTP at present
            // If there is a complete message still in the buffer, there must be the separator 'sep'
            // in the buffer, note that we full-scan the buffer at every read for simplicity.

            // Always NUL-terminate so we are free to use strstr()
            // nb: we never fill the last byte of the buffer with read data (see above) so this is safe
            *eod = '\0';

            while (som < eod && (p = strstr(som, c->service->read_sep)) != NULL) { // end of first message if found
                *p = '\0'; // The handler expects null terminated strings
                if (c->service->read_handler(c, som, remote, now)) { // Pass message to handler.
                    if (Modes.debug_net) {
                        fprintf(stderr, "%s: Closing connection from %s port %s\n", c->service->descr, c->host, c->port);
                    }
                    modesCloseClient(c); // Handler returns 1 on error to signal we .
                    return; // should close the client connection
                }
                som = p + c->service->read_sep_len; // Move to start of next message
            }
        }

        if (!c->receiverIdLocked && (c->bytesReceived > 512 || now > c->connectedSince + 10000)) {
            lockReceiverId(c);
        }

        if (som > c->buf) { // We processed something - so
            //som = lastSom;
            c->buflen = eod - som; //     Update the unprocessed buffer length
            if (c->buflen <= 0) {
                if (c->buflen < 0)
                    fprintf(stderr, "codepoint Si0wereH\n");
                c->buflen = 0;
            } else {
                memmove(c->buf, som, c->buflen); //     Move what's remaining to the start of the buffer
            }
        } else { // If no message was decoded process the next client
            return;
        }
    }
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

// Unlink and free closed clients
void netFreeClients() {
    struct client *c, **prev;
    struct net_service *s;

    for (s = Modes.services; s; s = s->next) {
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
}

static void handleEpoll(int count) {
    int64_t now = mstime();

    int i;
    for (i = 0; i < count; i++) {
        struct epoll_event event = Modes.net_events[i];
        if (event.data.ptr == &Modes.exitEventfd)
            return;

        struct client *cl = (struct client *) Modes.net_events[i].data.ptr;
        if (!cl) { fprintf(stderr, "handleEpoll: epollEvent.data.ptr == NULL\n"); continue; }

        if (cl->acceptSocket || cl->net_connector_dummyClient) {
            if (cl->acceptSocket) {
                modesAcceptClients(cl, now);
            }
            if (cl->net_connector_dummyClient) {
                checkServiceConnected(cl->con, now);
            }
        } else {
            if ((event.events & EPOLLOUT)) {
                // check if we need to flush a client because the send buffer was full previously
                if (cl->service) {
                    if (flushClient(cl, now) < 0) {
                        continue;
                    }
                }
            }

            if ((event.events & (EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP))) {
                if (cl->service) {
                    modesReadFromClient(cl, now);
                }
            }
        }
    }
}
//
// Perform periodic network work
//
void modesNetPeriodicWork(void) {
    static int64_t next_tcp_json;
    static struct timespec watch;

    if (!Modes.net_events) {
        epollAllocEvents(&Modes.net_events, &Modes.net_maxEvents);
    }

    int64_t wait_ms;
    if (Modes.net_only) {
        // wait in net-only mode, but never less than 1 ms (unless we get network packets, that wakes the wait immediately)
        wait_ms = imax(1, Modes.net_output_flush_interval);
    } else {
        // NO WAIT WHEN USING AN SDR !! IMPORTANT !!
        wait_ms = 0;
    }

    // unlock decode mutex for waiting in handleEpoll
    pthread_mutex_unlock(&Threads.decode.mutex);

    int count = epoll_wait(Modes.net_epfd, Modes.net_events, Modes.net_maxEvents, wait_ms);

    pthread_mutex_lock(&Threads.decode.mutex);

    int64_t interval = lapWatch(&watch);

    handleEpoll(count);

    if (count == Modes.net_maxEvents) {
        epollAllocEvents(&Modes.net_events, &Modes.net_maxEvents);
    }

    int64_t elapsed1 = lapWatch(&watch);

    int64_t now = mstime();

    pingSenders(Modes.beast_in_service, now);

    int64_t elapsed2 = lapWatch(&watch);

    static int64_t next_flush_interval;
    if (now > next_flush_interval) {
        next_flush_interval = now + Modes.net_output_flush_interval / 4;

        serviceReconnectCallback(now);
        // If we have data that has been waiting to be written for a while,
        // write it now.
        for (struct net_service *s = Modes.services; s; s = s->next) {
            if (!s->writer) {
                continue;
            }
            int force_flush = 0;
            if (Modes.net_heartbeat_interval && s->writer->send_heartbeat
                    && now - s->writer->lastWrite >= Modes.net_heartbeat_interval) {
                // If we have generated no messages for a while, send a heartbeat
                s->writer->send_heartbeat(s);
                force_flush = 1;
            }
            if (s->writer->dataUsed && (force_flush || now > s->writer->lastWrite + Modes.net_output_flush_interval)) {
                flushWrites(s->writer);
                //fprintf(stderr, "%s: interval flush\n", s->descr);
            }
        }
    }

    int64_t elapsed3 = lapWatch(&watch);

    static int64_t antiSpam;
    if ((elapsed1 > 150 || elapsed2 > 150 || elapsed3 > 150 || interval > 1100) && now > antiSpam + 5 * SECONDS) {
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


void cleanupNetwork(void) {
    if (!Modes.net)
        return;
    for (struct net_service *s = Modes.services; s; s = s->next) {
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
    }

    close(Modes.net_epfd);

    struct net_service *s = Modes.services, *ns;
    while (s) {
        ns = s->next;
        serviceClose(s);
        s = ns;
    }

    for (int i = 0; i < Modes.net_connectors_count; i++) {
        struct net_connector *con = Modes.net_connectors[i];
        if (con->gai_request_in_progress) {
            pthread_join(con->thread, NULL);
        }
        sfree(con->address0);
        freeaddrinfo(con->addr_info);
        pthread_mutex_destroy(&con->mutex);
        sfree(con);
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

