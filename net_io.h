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
struct net_service_group;
struct messageBuffer;

typedef int (*read_fn)(struct client *, char *, int, int64_t, struct messageBuffer *);

typedef enum
{
    READ_MODE_IGNORE,
    READ_MODE_BEAST,
    READ_MODE_BEAST_COMMAND,
    READ_MODE_ASCII,
    READ_MODE_ASTERIX,
    READ_MODE_PLANEFINDER
} read_mode_t;

typedef struct {
    const char *msg;
    int32_t len;
} heartbeat_t;

// Describes one network service (a group of clients with common behaviour)

struct net_service
{
    const char *descr;
    struct net_service_group *group;
    struct net_writer *writer; // shared writer state
    int listener_count; // number of listeners
    int pusher_count; // Number of push servers connected to
    int connections; // number of active clients
    int serial_service; // 1 if this is a service for serial devices
    read_mode_t read_mode;
    read_fn read_handler;
    int read_sep_len;
    const char *read_sep; // hander details for input data
    struct client *clients; // linked list of clients connected to this service
    int *listener_fds; // listening FDs
    struct client *listenSockets; // dummy client structs for all open sockets for epoll commonality
    char* unixSocket; // path of unix socket
    int sendqOverrideSize;
    int recvqOverrideSize;
    heartbeat_t heartbeat_in;
    heartbeat_t heartbeat_out;
};

#define NET_SERVICE_GROUP_MAX 16

struct net_service_group {
    struct net_service *services;
    int len;
    int alloc;
    int event_progress;
};

// Structure used to describe a networking client

struct client
{
    struct client* next; // Pointer to next client
    struct net_service *service; // Service this client is part of
    char *buf; // read buffer
    char *som;
    char *eod;
    int buflen; // Amount of data on read buffer
    int bufmax; // size of the read buffer
    int fd; // File descriptor
    int8_t bufferToProcess;
    int8_t remote;
    int8_t bContinue;
    int8_t discard;
    int8_t processing;
    int8_t acceptSocket; // not really a client but rather an accept Socket ... only fd and epollEvent will be valid
    int8_t net_connector_dummyClient; // dummy client used by net_connector
    int8_t pingEnabled;
    int8_t modeac_requested; // 1 if this Beast output connection has asked for A/C
    int8_t receiverIdLocked; // receiverId has been transmitted by other side.
    int8_t unreasonable_messagerate;
    char *sendq;  // Write buffer - allocated later
    int sendq_len; // Amount of data in SendQ
    int sendq_max; // Max size of SendQ
    uint32_t ping; // only 24 bit are ever sent
    uint32_t pong; // only 24 bit are ever sent
    int32_t recentMessages;
    int64_t recentMessagesReset;
    int64_t pingReceived;
    int64_t pongReceived;
    uint64_t bytesReceived;
    uint64_t receiverId;
    uint64_t receiverId2;
    int64_t last_flush;
    int64_t last_send;
    int64_t last_read;  // This is used on write-only clients to help check for dead connections
    int64_t last_read_flush;
    int64_t connectedSince;
    uint64_t messageCounter; // counter for incoming data
    uint64_t positionCounter; // counter for incoming data
    uint64_t garbage; // amount of garbage we have received from this client
    int64_t rtt; // last reported rtt in milliseconds
    double latest_rtt; // in milliseconds, pseudo average with factor 0.9
    // crude IIR pseudo rolling average, old value factor 0.995
    // cumulative weigth of last 100 packets is 0.39
    // cumulative weigth of last 300 packets is 0.78
    // cumulative weigth of last 600 packets is 0.95
    // usually around 300 packets a minute for an ro-interval of 0.2
    double recent_rtt; // in milliseconds
    struct epoll_event epollEvent;
    struct net_connector *con;
    char proxy_string[256]; // store string received from PROXY protocol v1 (v2 not supported currently)
    char host[NI_MAXHOST]; // For logging
    char port[NI_MAXSERV];
};

// Client connection
struct net_connector
{
    char *connect_string;
    char *address;
    char *address0;
    char *address1;
    char *port;
    char *port0;
    char *port1;
    char *protocol;
    struct net_service *service;
    struct client* c;
    int use_addr;
    int connected;
    int connecting;
    int silent_fail;
    int fd;
    int64_t next_reconnect;
    int64_t connect_timeout;
    int64_t lastConnect; // timestamp for last connection establish
    int64_t backoff;
    char resolved_addr[NI_MAXHOST+3];
    struct addrinfo *addr_info;
    struct addrinfo *try_addr; // pointer walking addr_info list
    int gai_error;
    int gai_request_in_progress;
    int gai_request_done;
    pthread_t thread;
    pthread_mutex_t mutex;
    struct client dummyClient; // client struct for epoll connection handling before we have a fully established connection
    int enable_uuid_ping;
    char *uuid;
};

// Common writer state for all output sockets of one type

struct net_writer
{
    void *data; // shared write buffer, sized MODES_OUT_BUF_SIZE
    int dataUsed; // number of bytes of write buffer currently used
    int connections; // number of active clients
    struct net_service *service; // owning service
    int64_t lastWrite; // time of last write to clients
    uint64_t lastReceiverId;
    int noTimestamps;
};

void serviceListen (struct net_service *service, char *bind_addr, char *bind_ports, int epfd);
void serviceClose(struct net_service *s);

void sendBeastSettings (int fd, const char *settings);
void sendData(struct net_writer *output, char *data, int len);

void modesInitNet (void);
void modesQueueOutput (struct modesMessage *mm);
void jsonPositionOutput(struct modesMessage *mm, struct aircraft *a);
void modesNetPeriodicWork (void);
void cleanupNetwork(void);

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

void netUseMessage(struct modesMessage *mm);
void netDrainMessageBuffers();
struct modesMessage *netGetMM(struct messageBuffer *buf);

#endif
