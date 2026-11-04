#ifdef __APPLE__

#include "epoll_shim.h"
#include <unistd.h>
#include <fcntl.h>

int epoll_create1(int flags) {
    (void)flags; // Suppress unused parameter warning
    return kqueue();
}

int epoll_create(int size) {
    (void)size; // Suppress unused parameter warning
    return epoll_create1(0);
}

/* one filter per direction: at most a read and a write change per call */
enum { EPOLL_SHIM_MAX_CHANGES = 2 };

/* Submit the changes in one kevent() call.  EV_RECEIPT makes kevent() report
 * a result for every change instead of stopping at the first error, and since
 * the receipts fill the whole event list no pending events are dequeued.
 * Returns the number of deletions of a filter that was not registered
 * (ENOENT), or -1 with errno set on any other error.
 */
static int applyChanges(int epfd, struct kevent *changes, int n) {
    struct kevent results[EPOLL_SHIM_MAX_CHANGES];
    static const struct timespec noWait = { 0, 0 };

    for (int i = 0; i < n; i++)
        changes[i].flags |= EV_RECEIPT;

    int nres = kevent(epfd, changes, n, results, n, &noWait);
    if (nres < 0)
        return -1;

    int missing = 0;
    for (int i = 0; i < nres; i++) {
        if (!(results[i].flags & EV_ERROR) || results[i].data == 0)
            continue;
        if (results[i].data == ENOENT) {
            missing++;
            continue;
        }
        errno = (int) results[i].data;
        return -1;
    }
    return missing;
}

/* kqueue keeps one independent filter per read/write direction and EV_ADD
 * only ever adds, while epoll wants a single interest mask.  Always submit
 * both filters, and on EPOLL_CTL_MOD remove the ones that are no longer
 * requested: a leftover EVFILT_WRITE keeps reporting a writable socket and
 * makes epoll_wait() return immediately after a client drains its send queue.
 */
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    struct kevent changes[EPOLL_SHIM_MAX_CHANGES];
    int n = 0;

    if (op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD) {
        static const struct { uint32_t mask; int16_t filter; } dirs[] = {
            { EPOLLIN, EVFILT_READ },
            { EPOLLOUT, EVFILT_WRITE },
        };
        for (int i = 0; i < 2; i++) {
            if (event->events & dirs[i].mask)
                EV_SET(&changes[n++], fd, dirs[i].filter, EV_ADD, 0, 0, event->data.ptr);
            else if (op == EPOLL_CTL_MOD) /* MOD replaces the mask, it doesn't extend it */
                EV_SET(&changes[n++], fd, dirs[i].filter, EV_DELETE, 0, 0, NULL);
        }
        if (n == 0)
            return 0;
        return applyChanges(epfd, changes, n) < 0 ? -1 : 0;
    } else if (op == EPOLL_CTL_DEL) {
        EV_SET(&changes[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);

        int missing = applyChanges(epfd, changes, n);
        if (missing < 0)
            return -1;
        if (missing == n) {
            errno = ENOENT;
            return -1;
        }
        return 0;
    }

    errno = EINVAL;
    return -1;
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    struct kevent kevents[maxevents];
    struct timespec ts;
    
    if (timeout >= 0) {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000;
    }
    
    int nev = kevent(epfd, NULL, 0, kevents, maxevents,
                     timeout >= 0 ? &ts : NULL);
    
    for (int i = 0; i < nev; i++) {
        events[i].events = 0;
        if (kevents[i].filter == EVFILT_READ)
            events[i].events |= EPOLLIN;
        if (kevents[i].filter == EVFILT_WRITE)
            events[i].events |= EPOLLOUT;
        if (kevents[i].flags & EV_ERROR)
            events[i].events |= EPOLLERR;
        if (kevents[i].flags & EV_EOF)
            events[i].events |= EPOLLHUP;
        events[i].data.ptr = kevents[i].udata;
    }
    
    return nev;
}

#endif /* __APPLE__ */
