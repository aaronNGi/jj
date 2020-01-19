/* See LICENSE file for license details. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "config.h"

#define PARENT_READ read_pipe[0]
#define PARENT_WRITE write_pipe[1]
#define CHILD_READ write_pipe[0]
#define CHILD_WRITE read_pipe[1]
#define IRC_MSG_MAX 512

static char bufin[IRC_MSG_MAX];
static char bufout[IRC_MSG_MAX+14];
static FILE *srv;
static int read_pipe[2];
static int write_pipe[2];

static char* prog;

static void eprint(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(bufout, sizeof bufout, fmt, ap);
	va_end(ap);
	fprintf(stderr, "%s: %s", prog, bufout);
	if(fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s\n", strerror(errno));
	exit(1);
}

static int dial(char *host, char *port) {
	static struct addrinfo hints;
	int srv;
	struct addrinfo *res, *r;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if(getaddrinfo(host, port, &hints, &res) != 0)
		eprint("error: cannot resolve hostname '%s':", host);
	for(r = res; r; r = r->ai_next) {
		if((srv = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1)
			continue;
		if(connect(srv, r->ai_addr, r->ai_addrlen) == 0)
			break;
		close(srv);
	}
	freeaddrinfo(res);
	if(!r)
		eprint("error: cannot connect to host '%s'\n", host);

	return srv;
}

static int safe_fd_set(int fd, fd_set* fds, int* max_fd) {
	assert(max_fd != NULL);

	FD_SET(fd, fds);
	if (fd > *max_fd)
		*max_fd = fd;
	return 0;
}

static int read_line(int fd, char *buf, size_t bufsize)
{
	size_t i;
	char c;

	for (i = 0; i < bufsize - 1; i++) {
		if (read(fd, &c, sizeof(char)) != sizeof(char))
			return -1;

		if (c == '\n') {
			buf[i] = '\0';
			return 0;
		}

		buf[i] = c;
	}
	buf[i] = '\0';
	return 0;
}

static void handle_server_output()
{
	if(fgets(bufin, sizeof bufin, srv) == NULL)
		eprint("remote host closed the connection\n");

	snprintf(bufout, sizeof bufout, "i %d %s", (int)time(NULL), bufin);
	if (write(PARENT_WRITE, bufout, strlen(bufout)) == -1)
		eprint("cannot write to client:");
}

static void handle_fifo_input(int fd)
{
	if (read_line(fd, bufin, sizeof bufin) == -1)
		return;
	snprintf(bufout, sizeof bufout, "u %d %s\n", (int)time(NULL), bufin);
	if (write(PARENT_WRITE, bufout, strlen(bufout)) == -1)
		eprint("cannot write to client:");
}

static void handle_child_output(int fd)
{
	if (read_line(fd, bufin, sizeof bufin) == -1)
		return;
	fprintf(srv, "%s\n", bufin);
	fflush(srv);
}

static void handle_sig_child(int sig)
{
	exit(1);
}

int main(int argc, char *argv[]) {
	struct timeval tv;
	fd_set master, rd;
	int fifo_fd, max_fd, n;
	time_t trespond;

	prog = argv[0];

	char *val;
	char *ircdir = (val = getenv("IRC_DIR")) != NULL
		? val : DEFAULT_DIR;
	char *host = (val = getenv("IRC_HOST")) != NULL
		? val : DEFAULT_HOST;
	char *port = (val = getenv("IRC_PORT")) != NULL
		? val : DEFAULT_PORT;
	char *cmd = (val = getenv("IRC_CLIENT")) != NULL
		? val : DEFAULT_CMD;
	char *fifoname = FIFO_NAME;

	char *path = malloc(
		strlen(ircdir) + strlen(host) + strlen(fifoname) + 3);

	int ret = 1;
	if (!path) {
		fprintf(stderr, "%s: malloc error: %s\n",
			prog, strerror(errno));
		goto free;
	}

	sprintf(path, "%s/%s", ircdir, host);
	if (mkdir(path, 0777) == -1 && errno != EEXIST) {
		fprintf(stderr, "%s: cannot create directory '%s': %s\n",
			prog, path, strerror(errno));
		goto free;
	}

	sprintf(path, "%s/%s/%s", ircdir, host, fifoname);
	if (mkfifo(path, 0600) == -1 && errno != EEXIST) {
		fprintf(stderr, "%s: cannot create fifo '%s': %s\n",
			prog, path, strerror(errno));
		goto free;
	}

	fifo_fd = open(path, O_RDWR | O_NONBLOCK);
	if (fifo_fd == -1) {
		fprintf(stderr, "%s: cannot open fifo '%s': %s\n",
			prog, path, strerror(errno));
		goto free;
	}

	ret = 0;

	free:
		free(path);
		if (ret)
			exit(ret);

	extern char **environ;
	signal(SIGCHLD, handle_sig_child);

	if (pipe(read_pipe) < 0 || pipe(write_pipe) < 0)
		eprint("pipe failure:");

	pid_t child_pid = fork();
	if (child_pid < 0)
		eprint("fork failure:");
	else if (child_pid == 0) {
		close(0);
		close(1);
		close(PARENT_READ);
		close(PARENT_WRITE);
		dup2(CHILD_READ, 0);
		dup2(CHILD_WRITE, 1);

		execlp(cmd, (char *)NULL);
		eprint("cannot execute client:");
	} else {
		close(CHILD_READ);
		close(CHILD_WRITE);
	}

	srv = fdopen(dial(host, port), "r+");
	if (!srv)
		eprint("fdopen failure:");
	setbuf(srv, NULL);

	max_fd = fifo_fd;

	FD_ZERO(&master);
	safe_fd_set(fileno(srv), &master, &max_fd);
	safe_fd_set(fifo_fd, &master, &max_fd);
	safe_fd_set(PARENT_READ, &master, &max_fd);

	for(;;) {
		rd = master;

		tv.tv_sec = 120;
		tv.tv_usec = 0;

		n = select(max_fd + 1, &rd, 0, 0, &tv);

		if(n < 0) {
			if (errno == EINTR)
				continue;
			eprint("select failure:");
		} else if (n == 0) {
			if (time(NULL) - trespond >= 300)
				eprint("shutting down: ping timeout\n");
			fprintf(srv, "PING %s\r\n", host);
			continue;
		}

		if (FD_ISSET(fileno(srv), &rd)) {
			handle_server_output();
			trespond = time(NULL);
		}
		if (FD_ISSET(fifo_fd, &rd)) {
			handle_fifo_input(fifo_fd);
		}
		if (FD_ISSET(PARENT_READ, &rd)) {
			handle_child_output(PARENT_READ);
		}
	}
	return 0;
}
