// Part of readsb, a Mode-S/ADSB/TIS message decoder.
//
// net_io.h: network handling.
//
// Copyright (c) 2019 Michael Wolf <michael@mictronics.de>
//
// This code is based on a detached fork of dump1090-fa.
//
// Copyright (c) 2014,2015 Oliver Jowett <oliver@mutability.co.uk>
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

#ifndef NETIO_H
#define NETIO_H

#include <sys/socket.h>

// Describes a networking service (group of connections)

struct aircraft;
struct modesMessage;
struct client;
struct net_service;
typedef int (*read_fn)(struct client *, char *, int, uint64_t);
typedef void (*heartbeat_fn)(struct net_service *);

typedef enum
{
    READ_MODE_IGNORE,
    READ_MODE_BEAST,
    READ_MODE_BEAST_COMMAND,
    READ_MODE_ASCII
} read_mode_t;

/* Data mode to feed push server */
typedef enum
{
    PUSH_MODE_RAW,
    PUSH_MODE_BEAST,
    PUSH_MODE_SBS,
} push_mode_t;

// Describes one network service (a group of clients with common behaviour)

struct net_service
{
    int listener_count; // number of listeners
    int pusher_count; // Number of push servers connected to
    int connections; // number of active clients
    int serial_service; // 1 if this is a service for serial devices
    read_mode_t read_mode;
    read_fn read_handler;
    struct net_writer *writer; // shared writer state
    struct net_service* next;
    const char *descr;
    int read_sep_len;
    const char *read_sep; // hander details for input data
    struct client *clients; // linked list of clients connected to this service
    int *listener_fds; // listening FDs
    struct client *listenSockets; // dummy client structs for all open sockets for epoll commonality
};

// Client connection
struct net_connector
{
    char *address;
    char *address0;
    char *address1;
    int use_addr;
    char *port;
    char *port0;
    char *port1;
    char *protocol;
    struct net_service *service;
    int8_t connected;
    int8_t connecting;
    int fd;
    struct client* c;
    uint64_t next_reconnect;
    uint64_t connect_timeout;
    uint64_t lastConnect; // timestamp for last connection establish
    int64_t backoff;
    char resolved_addr[NI_MAXHOST+3];
    struct addrinfo *addr_info;
    struct addrinfo *try_addr; // pointer walking addr_info list
    int gai_error;
    int8_t gai_request_in_progress;
    int8_t gai_request_done;
    pthread_t thread;
    pthread_mutex_t mutex;
};

// Structure used to describe a networking client

struct client
{
    struct client* next; // Pointer to next client
    struct net_service *service; // Service this client is part of
    int fd; // File descriptor
    int buflen; // Amount of data on buffer
    int8_t acceptSocket; // not really a client but rather an accept Socket ... only fd and epollEvent will be valid
    int8_t receiverIdLocked; // receiverId has been transmitted by other side.
    char *sendq;  // Write buffer - allocated later
    int sendq_len; // Amount of data in SendQ
    int sendq_max; // Max size of SendQ
    int pingEnabled;
    uint32_t ping; // only 24 bit are ever sent
    uint32_t pong; // only 24 bit are ever sent
    uint64_t pingReceived;
    uint64_t pongReceived;
    uint64_t bytesReceived;
    uint64_t receiverId;
    uint64_t receiverId2;
    uint64_t last_flush;
    uint64_t last_send;
    uint64_t last_read;  // This is used on write-only clients to help check for dead connections
    uint64_t connectedSince;
    uint64_t messageCounter; // counter for incoming data
    uint64_t positionCounter; // counter for incoming data
    uint64_t garbage; // amount of garbage we have received from this client
    int32_t rtt; // last reported rtt in milliseconds
    double latest_rtt; // in milliseconds, pseudo average with factor 0.9
    // crude IIR pseudo rolling average, old value factor 0.995
    // cumulative weigth of last 100 packets is 0.39
    // cumulative weigth of last 300 packets is 0.78
    // cumulative weigth of last 600 packets is 0.95
    // usually around 300 packets a minute for an ro-interval of 0.2
    double recent_rtt; // in milliseconds
    struct epoll_event epollEvent;
    struct net_connector *con;
    int8_t noTimestamps;
    int8_t noTimestampsSignaled;
    int8_t modeac_requested; // 1 if this Beast output connection has asked for A/C
    char proxy_string[256]; // store string received from PROXY protocol v1 (v2 not supported currently)
    char host[NI_MAXHOST]; // For logging
    char port[NI_MAXSERV];
    char buf[MODES_CLIENT_BUF_SIZE + 4]; // Read buffer+padding
};

// Common writer state for all output sockets of one type

struct net_writer
{
    void *data; // shared write buffer, sized MODES_OUT_BUF_SIZE
    int dataUsed; // number of bytes of write buffer currently used
    int connections; // number of active clients
    struct net_service *service; // owning service
    heartbeat_fn send_heartbeat; // function that queues a heartbeat if needed
    uint64_t lastWrite; // time of last write to clients
    uint64_t lastReceiverId;
    int8_t noTimestamps;
};

struct net_service *serviceInit (const char *descr, struct net_writer *writer, heartbeat_fn hb_handler, read_mode_t mode, const char *sep, read_fn read_handler);
void serviceListen (struct net_service *service, char *bind_addr, char *bind_ports, int epfd);
void serviceClose(struct net_service *s);
struct client *createSocketClient (struct net_service *service, int fd);
struct client *createGenericClient (struct net_service *service, int fd);

void sendBeastSettings (int fd, const char *settings);

void modesInitNet (void);
void modesQueueOutput (struct modesMessage *mm, struct aircraft *a);
void jsonPositionOutput(struct modesMessage *mm, struct aircraft *a);
void modesNetPeriodicWork (void);
void modesReadSerialClient(void);
void cleanupNetwork(void);
void netFreeClients();

void writeJsonToNet(struct net_writer *writer, struct char_buffer cb);

// GNS HULC status message

typedef union __packed {
    unsigned char buf[24];

    struct _packed {
        uint32_t serial;
        uint16_t flags;
        uint16_t reserved;
        uint32_t epoch;
        uint32_t latitude;
        uint32_t longitude;
        uint16_t altitude;
        uint8_t satellites;
        uint8_t hdop;
    } status;
} hulc_status_msg_t;

#endif
