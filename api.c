#include "readsb.h"


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

    if (list[j].lon < ref_from) {
        res.from = j + 1;
    } else if (list[i].lon < ref_from) {
        res.from = i + 1;
    } else {
        res.from = i;
    }


    // get upper bound (exclusive)
    i = min(res.from, len - 1);
    j = len - 1;
    while (j > i + 1) {

        int pivot = (i + j) / 2;
        if (list[pivot].lon <= ref_to)
            i = pivot;
        else
            j = pivot;
    }

    if (list[j].lon <= ref_to) {
        res.to = j + 1;
    } else if (list[i].lon <= ref_to) {
        res.to = i + 1;
    } else {
        res.to = i;
    }

    return res;
}

struct char_buffer apiReq(struct apiBuffer *buffer, double latMin, double latMax, double lonMin, double lonMax) {

    int32_t lat1 = (int32_t) (latMin * 1E6);
    int32_t lat2 = (int32_t) (latMax * 1E6);
    int32_t lon1 = (int32_t) (lonMin * 1E6);
    int32_t lon2 = (int32_t) (lonMax * 1E6);

    struct range r[2];
    memset(&r, 0, sizeof(r));

    if (lon1 <= lon2) {
        r[0] = findLonRange(lon1, lon2, buffer->list, buffer->len);
    } else if (lon1 > lon2) {
        r[0] = findLonRange(lon1, 180E6, buffer->list, buffer->len);
        r[1] = findLonRange(-180E6, lon2, buffer->list, buffer->len);
        //fprintf(stderr, "%.1f to 180 and -180 to %1.f\n", lon1 / 1E6, lon2 / 1E6);
    }

    struct char_buffer cb = { 0 };

    size_t alloc = API_REQ_PADSTART + 1024;

    struct offset *offsets = malloc(buffer->len * sizeof(struct offset));
    int count = 0;

    for (int k = 0; k < 2; k++) {
        for (int j = r[k].from; j < r[k].to; j++) {
            struct apiEntry *e = &buffer->list[j];
            if (e->lat >= lat1 && e->lat <= lat2) {
                offsets[count++] = e->jsonOffset;
                alloc += e->jsonOffset.len + 10;
            }
        }
    }

    cb.buffer = malloc(alloc);
    if (!cb.buffer)
        return cb;

    char *p = cb.buffer + API_REQ_PADSTART;

    char *end = cb.buffer + alloc;
    p = safe_snprintf(p, end, "{\"now\": %.1f,\n", buffer->timestamp / 1000.0);
    p = safe_snprintf(p, end, "\"resultCount\": %d,\n", count);
    p = safe_snprintf(p, end, "\"aircraft\":[");

    char *json = buffer->json;

    for (int i = 0; i < count; i++) {
        *p++ = '\n';
        struct offset *off = &offsets[i];
        if (p + off->len + 100 >= end) {
            fprintf(stderr, "search code ieva2aeV: need: %d alloc: %d\n", (int) ((p + off->len + 100) - cb.buffer), (int) alloc);
            break;
        }
        memcpy(p, json + off->offset, off->len);
        p += off->len;
        *p++ = ',';
    }

    free(offsets);

    if (*(p - 1) == ',')
        p--; // remove trailing comma if necessary
    p = safe_snprintf(p, end, "\n]}\n");

    cb.len = p - cb.buffer;

    if (cb.len >= alloc)
        fprintf(stderr, "apiReq buffer insufficient\n");

    return cb;
}

static inline void apiAdd(struct apiBuffer *buffer, struct aircraft *a, uint64_t now) {
    if (trackDataAge(now, &a->position_valid) > 5 * MINUTES)
        return;

    struct apiEntry entry;

    entry.addr = a->addr;
    entry.lat = (int32_t) (a->lat * 1E6);
    entry.lon = (int32_t) (a->lon * 1E6);
    if (trackDataValid(&a->altitude_baro_valid)) {
        entry.alt = a->altitude_baro;
    } else if (trackDataValid(&a->altitude_geom_valid)) {
        entry.alt = a->altitude_geom;
    } else {
        entry.alt = INT32_MIN;
    }
    memcpy(entry.typeCode, a->typeCode, sizeof(entry.typeCode));
    entry.dbFlags = a->dbFlags;

    buffer->list[buffer->len] = entry;
    buffer->len++;
}

static inline void apiGenerateJson(struct apiBuffer *buffer, uint64_t now) {
    free(buffer->json);
    buffer->json = NULL;

    size_t alloc = buffer->len * 1024 + 2048; // The initial buffer is resized as needed
    buffer->json = (char *) malloc(alloc);
    char *p = buffer->json;
    char *end = buffer->json + alloc;

    for (int i = 0; i < buffer->len; i++) {
        if ((p + 2000) >= end) {
            int used = p - buffer->json;
            alloc *= 2;
            buffer->json = (char *) realloc(buffer->json, alloc);
            p = buffer->json + used;
            end = buffer->json + alloc;
        }

        struct apiEntry *entry = &buffer->list[i];
        struct aircraft *a = aircraftGet(entry->addr);
        struct offset *off = &entry->jsonOffset;
        if (!a) {
            off->offset = 0;
            off->len = 0;
            continue;
        }

        char *start = p;
        p = sprintAircraftObject(p, end, a, now, 0, NULL);

        off->offset = start - buffer->json;
        off->len = p - start;
    }

    if (p >= end) {
        fprintf(stderr, "buffer full apiAdd\n");
    }
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

    apiGenerateJson(buffer, now);

    buffer->timestamp = now;

    // doesn't matter which of the 2 buffers the api req will use they are both pretty current
    pthread_mutex_lock(&Modes.apiMutex);
    Modes.apiFlip = flip;
    pthread_mutex_unlock(&Modes.apiMutex);

    return buffer->len;
}

static void apiCloseConn(struct apiCon *con, struct apiThread *thread) {
    int fd = con->fd;
    epoll_ctl(thread->epfd, EPOLL_CTL_DEL, fd, NULL);
    anetCloseSocket(fd);
    if (Modes.debug_api)
        fprintf(stderr, "%d: clo c: %d\n", thread->index, fd);
    free(con);
}

static void send400(int fd) {
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf);

    p = safe_snprintf(p, end,
    "HTTP/1.1 400 Bad Request\r\n"
    "Server: readsb/3.1442\r\n"
    "Connection: close\r\n"
    "Content-Length: 0\r\n\r\n");

    int res = write(fd, buf, strlen(buf));
    res = res;
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

static void apiSendData(struct apiCon *con, struct apiThread *thread) {
    struct char_buffer *cb = &con->cb;
    int len = cb->len - con->cbOffset;
    char *dataStart = cb->buffer + con->cbOffset;

    int nwritten = write(con->fd, dataStart, len);
    int err = errno;

    if (nwritten < len || (nwritten < 0 && (err == EAGAIN || err == EWOULDBLOCK))) {
        //fprintf(stderr, "wrote only %d of %d\n", nwritten, len);

        con->cbOffset += nwritten;

        if (!(con->events & EPOLLOUT)) {
            // notify if fd is available for writing
            con->events ^= EPOLLOUT; // toggle xor
            struct epoll_event epollEvent = { .events = con->events };
            epollEvent.data.ptr = con;

            if (epoll_ctl(thread->epfd, EPOLL_CTL_MOD, con->fd, &epollEvent))
                perror("epoll_ctl MOD fail:");
        }

        return;
        // free stuff some other time
    }

    free(cb->buffer);
    cb->len = 0;
    cb->buffer = NULL;

    if (nwritten < 0) {
        fprintf(stderr, "%s\n", strerror(err));
        apiCloseConn(con, thread);
        return;
    }

    apiCloseConn(con, thread);
    return;

    // doing one request per connection for the moment, keep-alive maybe later
    if (con->events & EPOLLOUT) {
        // no more writing necessary for the moment, no longer get notified for EPOLLOUT
        con->events ^= EPOLLOUT; // toggle xor
        struct epoll_event epollEvent = { .events = con->events };
        epollEvent.data.ptr = con;

        if (epoll_ctl(thread->epfd, EPOLL_CTL_MOD, con->fd, &epollEvent))
            perror("epoll_ctl MOD fail:");
    }

    return;
}

static void apiReadRequest(struct apiCon *con, struct apiBuffer *buffer, struct apiThread *thread) {
    int nread, err;
    int fd = con->fd;

    char req[1024];
    nread = read(fd, req, 1023);
    err = errno;

    if (nread < 0 && (err == EAGAIN || err == EWOULDBLOCK)) {
        return;
    }
    if (nread <= 0) {
        apiCloseConn(con, thread);
        return;
    }

    // we already have a response that hasn't been sent yet, discard request.
    if (con->cb.buffer)
        return;

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
            "Connection: close\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n\r\n",
            plen);

    int hlen = p - header;
    if (hlen == API_REQ_PADSTART)
        fprintf(stderr, "API_REQ_PADSTART insufficient\n");

    con->cbOffset = API_REQ_PADSTART - hlen;
    memcpy(cb.buffer + con->cbOffset, header, hlen);

    con->cb = cb;
    apiSendData(con, thread);
}
static void acceptConn(struct apiCon *con, struct apiThread *thread) {
    int listen_fd = con->fd;
    struct sockaddr_storage storage;
    struct sockaddr *saddr = (struct sockaddr *) &storage;
    socklen_t slen = sizeof(storage);
    int fd = -1;

    while ((fd = anetGenericAccept(Modes.aneterr, listen_fd, saddr, &slen, SOCK_NONBLOCK)) >= 0) {
        struct apiCon *con = calloc(sizeof(struct apiCon), 1);
        if (!con) fprintf(stderr, "EMEM, how much is the fish?\n"), exit(1);

        con->fd = fd;
        con->events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        struct epoll_event epollEvent = { .events = con->events };
        epollEvent.data.ptr = con;


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
        struct apiCon *con = calloc(sizeof(struct apiCon), 1);
        if (!con) fprintf(stderr, "EMEM, how much is the fish?\n"), exit(1);


        Modes.apiService.read_sep = (void*) con; // ugly .... park it there so we can free

        con->fd = Modes.apiService.listener_fds[i];
        con->accept = 1;
        con->events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        struct epoll_event epollEvent = { .events = con->events };
        epollEvent.data.ptr = con;

        if (epoll_ctl(thread->epfd, EPOLL_CTL_ADD, con->fd, &epollEvent))
            perror("epoll_ctl fail:");
    }

    pthread_mutex_init(&thread->mutex, NULL);
    pthread_mutex_lock(&thread->mutex);
    int count = 0;
    struct epoll_event *events = NULL;
    int maxEvents = 0;

    struct timespec cpu_timer;
    start_cpu_timing(&cpu_timer);
    while (!Modes.exit) {
        int flip;

        if (count == maxEvents) {
            epollAllocEvents(&events, &maxEvents);
        }
        count = epoll_wait(thread->epfd, events, maxEvents, 1000);

        pthread_mutex_lock(&Modes.apiMutex);

        flip = Modes.apiFlip;
        end_cpu_timing(&cpu_timer, &Modes.stats_current.api_worker_cpu);

        pthread_mutex_unlock(&Modes.apiMutex);

        struct apiBuffer *buffer = &Modes.apiBuffer[flip];
        start_cpu_timing(&cpu_timer);

        for (int i = 0; i < count; i++) {
            struct epoll_event event = events[i];
            if (event.data.ptr == &Modes.exitEventfd)
                break;

            struct apiCon *con = event.data.ptr;
            if (con->accept) {
                acceptConn(con, thread);
            } else {
                if (event.events & EPOLLOUT) {
                    apiSendData(con, thread);
                } else {
                    apiReadRequest(con, buffer, thread);
                }
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
    struct timespec cpu_timer;
    while (!Modes.exit) {
        incTimedwait(&ts, 500);

        //struct timespec watch;
        //startWatch(&watch);

        start_cpu_timing(&cpu_timer);

        apiUpdate(&Modes.aircraftActive);

        end_cpu_timing(&cpu_timer, &Modes.stats_current.api_update_cpu);

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
    pthread_cond_init(&Modes.apiUpdateCond, NULL);
    pthread_create(&Modes.apiUpdateThread, NULL, apiUpdateEntryPoint, NULL);
    for (int i = 0; i < API_THREADS; i++) {
        Modes.apiThread[i].index = i;
        pthread_create(&Modes.apiThread[i].thread, NULL, apiThreadEntryPoint, &Modes.apiThread[i]);
    }
}
void apiCleanup() {
    pthread_join(Modes.apiUpdateThread, NULL);
    pthread_mutex_destroy(&Modes.apiUpdateMutex);
    pthread_cond_destroy(&Modes.apiUpdateCond);

    for (int i = 0; i < API_THREADS; i++) {
        pthread_join(Modes.apiThread[i].thread, NULL);
    }
    for (int i = 0; i < Modes.apiService.listener_count; ++i) {
        anetCloseSocket(Modes.apiService.listener_fds[i]);
    }
    free((void *) Modes.apiService.read_sep);
    free(Modes.apiService.listener_fds);
    free(Modes.apiBuffer[0].list);
    free(Modes.apiBuffer[1].list);
    free(Modes.apiBuffer[0].json);
    free(Modes.apiBuffer[1].json);
    free((void *) Modes.apiService.descr);
}

