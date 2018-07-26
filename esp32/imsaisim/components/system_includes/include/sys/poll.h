struct pollfd {
    int fd;
    int events;
    int revents;
};

#if !defined(CIVETWEB_HEADER_INCLUDED)
#define poll(a, b, c)  (void)0
#endif

#define POLLIN (1)  /* Data ready - read will not block. */
#define POLLPRI (2) /* Priority data ready. */
#define POLLOUT (4) /* Send queue not full - write will not block. */

/*
	p[0].fd = fileno(stdin);
	p[0].events = POLLIN | POLLOUT;
	p[0].revents = 0;
	poll(p, 1, 0);
	if (p[0].revents & POLLIN)
		status |= 2;
	if (p[0].revents & POLLOUT)
		status |= 1;
	}
*/
