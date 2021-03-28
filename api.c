#include "readsb.h"

static inline void apiAdd(struct apiBuffer *buffer, struct aircraft *a, uint64_t now) {
    if (!trackDataValid(&a->position_valid))
        return;

    struct apiEntry entry;

    entry.addr = a->addr;
    entry.lat = (int32_t) (a->lat * 1E6);
    entry.lon = (int32_t) (a->lon * 1E6);

    char *p = entry.json;
    char *end = p + sizeof(entry.json);

    p = sprintAircraftObject(p, end, a, now, 0, NULL);

    if (p >= end) {
        fprintf(stderr, "buffer full apiAdd %p %p\n", p, end);
        entry.jsonLen = 0;
    } else {
        entry.jsonLen = p - entry.json;
    }

    buffer->list[buffer->len] = entry;
    buffer->len++;
}

static int compareLon(const void *p1, const void *p2) {
    struct apiEntry *a1 = (struct apiEntry*) p1;
    struct apiEntry *a2 = (struct apiEntry*) p2;
    return (a1->lon > a2->lon) - (a1->lon < a2->lon);
}

static void apiSort(struct apiBuffer *buffer) {
    qsort(buffer->list, buffer->len, sizeof(struct apiEntry), compareLon);
}

static struct range findLonRange(int32_t ref_from, int32_t ref_to, struct apiEntry *list, int len) {
    struct range res = {0, 0};
    if (len == 0 || ref_from > ref_to)
        return res;

    // get lower bound
    int i = 0;
    int j = len - 1;
    while (j > i + 1) {

        int pivot = (i + j) / 2;

        if (list[pivot].lon < ref_from)
            i = pivot;
        else
            j = pivot;
    }
    if (list[j].lon < ref_from)
        res.from = j;
    else
        res.from = i;

    // get upper bound (exclusive)
    i = res.from;
    j = len - 1;
    while (j > i + 1) {

        int pivot = (i + j) / 2;

        if (list[pivot].lon <= ref_to)
            i = pivot;
        else
            j = pivot;
    }
    if (list[j].lon > ref_to)
        res.to = j + 1;
    else
        res.to = i + 1;

    return res;
}

struct char_buffer apiReq(struct apiBuffer *buffer, double latMin, double latMax, double lonMin, double lonMax) {

    int32_t lat1 = (int32_t) (latMin * 1E6);
    int32_t lat2 = (int32_t) (latMax * 1E6);
    int32_t lon1 = (int32_t) (lonMin * 1E6);
    int32_t lon2 = (int32_t) (lonMax * 1E6);

    struct range r1 = { 0 };
    struct range r2 = { 0 };

    if (lon1 <= lon2) {
        r1 = findLonRange(lon1, lon2, buffer->list, buffer->len);
    } else if (lon1 > lon2) {
        r1 = findLonRange(lon1, 180, buffer->list, buffer->len);
        r2 = findLonRange(-180, lon2, buffer->list, buffer->len);
    }

    struct char_buffer cb = { 0 };

    size_t alloc = API_REQ_PADSTART + 128;

    for (int j = r1.from; j < r1.to; j++) {
        struct apiEntry e = buffer->list[j];
        if (e.lat >= lat1 && e.lat <= lat2) {
            alloc += sizeof(struct apiEntry);
        }
    }
    for (int j = r2.from; j < r2.to; j++) {
        struct apiEntry e = buffer->list[j];
        if (e.lat >= lat1 && e.lat <= lat2) {
            alloc += sizeof(struct apiEntry);
        }
    }

    cb.buffer = malloc(alloc);
    if (!cb.buffer)
        return cb;

    char *p = cb.buffer + API_REQ_PADSTART;

    char *end = cb.buffer + alloc;
    p = safe_snprintf(p, end, "{\"aircraft\":[");

    for (int j = r1.from; j < r1.to; j++) {
        struct apiEntry e = buffer->list[j];
        if (e.lat >= lat1 && e.lat <= lat2) {
            *p++ = '\n';
            memcpy(p, e.json, e.jsonLen);
            p += e.jsonLen;
            *p++ = ',';
        }
    }
    for (int j = r2.from; j < r2.to; j++) {
        struct apiEntry e = buffer->list[j];
        if (e.lat >= lat1 && e.lat <= lat2) {
            *p++ = '\n';
            memcpy(p, e.json, e.jsonLen);
            p += e.jsonLen;
            *p++ = ',';
        }
    }

    if (p > cb.buffer + 1)
        p--; // remove last comma
    p = safe_snprintf(p, end, "\n]}\n");

    cb.len = p - cb.buffer;

    if (cb.len >= alloc)
        fprintf(stderr, "apiReq buffer insufficient\n");

    return cb;
}

int apiUpdate(struct craftArray *ca) {

    // always clear and update the inactive apiBuffer
    int flip = (Modes.apiFlip + 1) % 2;
    struct apiBuffer *buffer = &Modes.apiBuffer[flip];

    int acCount = ca->len;
    buffer->len = 0;
    if (buffer->len < acCount) {
        if (acCount > 50000) {
            fprintf(stderr, "api bailing, too many aircraft!\n");
            buffer->len = 0;
            return buffer->len;
        }
        buffer->alloc = acCount + 128;
        free(buffer->list);
        buffer->list = malloc(buffer->alloc * sizeof(struct apiEntry));
        if (!buffer->list) {
            fprintf(stderr, "apiList alloc: out of memory!\n");
            exit(1);
        }
    }

    uint64_t now = mstime();
    for (int i = 0; i < ca->len; i++) {
        struct aircraft *a = ca->list[i];

        if (a == NULL)
            continue;
        apiAdd(buffer, a, now);
    }

    apiSort(buffer);

    buffer->timestamp = now;

    // doesn't matter which of the 2 buffers the api req will use they are both pretty current
    pthread_mutex_lock(&Modes.apiFlipMutex);
    Modes.apiFlip = flip;
    pthread_mutex_unlock(&Modes.apiFlipMutex);

    return buffer->len;
}

static void apiCloseConn(int fd, struct apiThread *thread) {
    epoll_ctl(thread->epfd, EPOLL_CTL_DEL, fd, NULL);
    anetCloseSocket(fd);
    if (Modes.debug_api)
        fprintf(stderr, "%d: clo c: %d\n", thread->index, fd);
}

static void send400(int fd) {
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf);

    p = safe_snprintf(p, end,
    "HTTP/1.1 400 Bad Request\r\n"
    "Server: readsb/3.1442\r\n"
    "Content-Length: 0\r\n\r\n");

    write(fd, buf, strlen(buf));
}

static struct char_buffer parseFetch(char *req, struct apiBuffer *buffer) {
    struct char_buffer cb = { 0 };
    char *p, *needle, *saveptr;

    needle = "box=";
    p = strcasestr(req, needle);
    if (!p)
        return cb;
    p += strlen(needle);
    double box[4];
    char *boxcar[4];
    boxcar[0] = strtok_r(p, ",", &saveptr);
    boxcar[1] = strtok_r(NULL, ",", &saveptr);
    boxcar[2] = strtok_r(NULL, ",", &saveptr);
    boxcar[3] = strtok_r(NULL, ",", &saveptr);

    for (int i = 0; i < 4; i++) {
        if (!boxcar[i])
            return cb;
        errno = 0;
        box[i] = strtod(boxcar[i], NULL);
        if (errno)
            return cb;
        if (box[i] > 180 || box[i] < -180)
            return cb;
    }
    if (box[0] > box[1])
        return cb;

    cb = apiReq(buffer, box[0], box[1], box[2], box[3]);
    return cb;
}

static void readRequest(int fd, struct apiBuffer *buffer, struct apiThread *thread) {
    int nread, nwritten, err;

    char req[1024];
    nread = read(fd, req, 1023);
    err = errno;

    if (nread < 0 && (err == EAGAIN || err == EWOULDBLOCK)) {
        return;
    }
    if (nread <= 0) {
        apiCloseConn(fd, thread);
        return;
    }

    req[nread] = 0;
    //fprintf(stderr, "%s\n", req);

    struct char_buffer cb = parseFetch(req, buffer);
    if (cb.len == 0) {
        send400(fd);
        return;
    }

    // at header before payload
    char header[API_REQ_PADSTART];
    char *p = header;
    char *end = header + API_REQ_PADSTART;

    int plen = cb.len - API_REQ_PADSTART;

    p = safe_snprintf(p, end,
            "HTTP/1.1 200 OK\r\n"
            "Server: readsb/3.1442\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n\r\n",
            plen);

    int hlen = p - header;
    if (hlen == API_REQ_PADSTART)
        fprintf(stderr, "API_REQ_PADSTART insufficient\n");

    int unusedPad = API_REQ_PADSTART - hlen;
    char *dataStart = cb.buffer + unusedPad;
    memcpy(dataStart, header, hlen);

    int len = hlen + plen;

    nwritten = write(fd, dataStart, len);
    err = errno;

    if (nwritten < len) {
        fprintf(stderr, "wrote only %d of %d\n", nwritten, len);
    }
    if (nwritten < 0) {
        fprintf(stderr, "%s\n", strerror(err));
    }

    free(cb.buffer);
}
static void acceptConn(int listen_fd, struct apiThread *thread) {
    struct sockaddr_storage storage;
    struct sockaddr *saddr = (struct sockaddr *) &storage;
    socklen_t slen = sizeof(storage);
    int fd = -1;

    while ((fd = anetGenericAccept(Modes.aneterr, listen_fd, saddr, &slen, SOCK_NONBLOCK)) >= 0) {
        struct epoll_event epollEvent = { 0 };
        epollEvent.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        struct twofds t = { .read = fd };

        memcpy(&epollEvent.data, &t, sizeof(uint64_t));

        if (Modes.debug_api)
            fprintf(stderr, "%d: new c: %d\n", thread->index, fd);

        if (epoll_ctl(thread->epfd, EPOLL_CTL_ADD, fd, &epollEvent))
            perror("epoll_ctl fail:");
    }
}

static void *apiThreadEntryPoint(void *arg) {
    struct apiThread *thread = (struct apiThread *) arg;
    srandom(get_seed());

    thread->epfd = my_epoll_create();

    for (int i = 0; i < Modes.apiService.listener_count; ++i) {
        struct epoll_event epollEvent = { 0 };
        epollEvent.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        struct twofds t = { 0 };
        t.accept = Modes.apiService.listener_fds[i];
        t.read = INT32_MIN;

        memcpy(&epollEvent.data, &t, sizeof(uint64_t));

        if (epoll_ctl(thread->epfd, EPOLL_CTL_ADD, t.accept, &epollEvent))
            perror("epoll_ctl fail:");
    }

    pthread_mutex_init(&thread->mutex, NULL);
    pthread_mutex_lock(&thread->mutex);
    int count = 0;
    struct epoll_event *events = NULL;
    int maxEvents = 0;

    while (!Modes.exit) {
        int flip;

        if (count == maxEvents) {
            epollAllocEvents(&events, &maxEvents);
        }
        count = epoll_wait(thread->epfd, events, maxEvents, 1000);

        pthread_mutex_lock(&Modes.apiFlipMutex);
        flip = Modes.apiFlip;
        pthread_mutex_unlock(&Modes.apiFlipMutex);
        struct apiBuffer *buffer = &Modes.apiBuffer[flip];

        for (int i = 0; i < count; i++) {
            struct epoll_event event = events[i];
            if (event.data.fd == Modes.exitEventfd)
                break;
            struct twofds t;
            memcpy(&t, &event.data.u64, sizeof(uint64_t));
            if (t.read == INT32_MIN) {
                acceptConn(t.accept, thread);
            } else {
                readRequest(t.read, buffer, thread);
            }
        }
    }
    close(thread->epfd);
    free(events);
    pthread_mutex_unlock(&thread->mutex);
    pthread_mutex_destroy(&thread->mutex);
    pthread_exit(NULL);
}

static void *apiUpdateEntryPoint(void *arg) {
    MODES_NOTUSED(arg);
    srandom(get_seed());
    pthread_mutex_lock(&Modes.apiUpdateMutex);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    while (!Modes.exit) {
        incTimedwait(&ts, 500);

        //struct timespec watch;
        //startWatch(&watch);

        apiUpdate(&Modes.aircraftActive);

        //uint64_t elapsed = stopWatch(&watch);
        //fprintf(stderr, "api req took: %.5f s, got %d aircraft!\n", elapsed / 1000.0, n);

        int err = pthread_cond_timedwait(&Modes.apiUpdateCond, &Modes.apiUpdateMutex, &ts);
        if (err && err != ETIMEDOUT)
            fprintf(stderr, "main thread: pthread_cond_timedwait unexpected error: %s\n", strerror(err));
    }
    pthread_mutex_unlock(&Modes.apiUpdateMutex);
    pthread_exit(NULL);
}

void apiInit() {
    Modes.apiService.descr = strdup("API output");
    serviceListen(&Modes.apiService, Modes.net_bind_address, Modes.net_output_api_ports);
    if (Modes.apiService.listener_count <= 0) {
        Modes.api = 0;
        return;
    }

    pthread_mutex_init(&Modes.apiUpdateMutex, NULL);
    pthread_create(&Modes.apiUpdateThread, NULL, apiUpdateEntryPoint, NULL);
    for (int i = 0; i < API_THREADS; i++) {
        Modes.apiThread[i].index = i;
        pthread_create(&Modes.apiThread[i].thread, NULL, apiThreadEntryPoint, &Modes.apiThread[i]);
    }
}
void apiCleanup() {
    for (int i = 0; i < API_THREADS; i++) {
        pthread_join(Modes.apiThread[i].thread, NULL);
    }
    free(Modes.apiBuffer[0].list);
    free(Modes.apiBuffer[1].list);
    free((void *) Modes.apiService.descr);
}

