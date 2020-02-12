/* See LICENSE file for license details. */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#define _WITH_DPRINTF
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>

#include "config.h"

#define IRC_MSG_MAX 512

static const char *prog;

static void
die(const char *fmt, ...)
{
	int sverr = errno;
	va_list ap;

	fprintf(stderr, "%s: error: ", prog);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s", strerror(sverr));

	fprintf(stderr, "\n");
	exit(1);
}

static int
dial(const char *host, const char *port)
{
	int fd = -1;
	struct addrinfo *res, *r, hints;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(host, port, &hints, &res))
		die("cannot resolve '%s:%s':", host, port);

	for (r = res; r; r = r->ai_next) {
		fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
		if (fd < 0)
			continue;

		if (connect(fd, r->ai_addr, r->ai_addrlen) == 0)
			break;

		close(fd);
	}
	freeaddrinfo(res);

	if (r == NULL)
		die("cannot connect to '%s:%s'", host, port);

	return fd;
}

static int
read_line(int fd, char *buf, size_t bufsize)
{
	char c;
	size_t i = 0;

	if (bufsize < 2) /* must have room for 1 character and \0 */
		abort();

	while (read(fd, &c, 1) == 1) {
		if (c == '\n') {
			/* take out \n or \r\n */
			if (i && buf[i - 1] == '\r')
				--i;
			buf[i] = '\0';
			return 0;
		}
		/* leave room for \0 */
		if (i < bufsize - 1)
			buf[i++] = c;
	}
	return -1; /* EOF or error */
}

static void
input_from_socket(int fd, int pipe_fd)
{
	char buf[IRC_MSG_MAX];

	if (read_line(fd, buf, sizeof buf))
		die("remote host closed the connection");

	if (dprintf(pipe_fd, "i %ju %s\n", (uintmax_t)time(NULL), buf) < 0)
		die("cannot write to client:");

}

static void
input_from_fifo(int fd, int pipe_fd)
{
	char buf[IRC_MSG_MAX];

	if (read_line(fd, buf, sizeof buf))
		die("failed reading fifo:");

	if (dprintf(pipe_fd, "u %ju %s\n", (uintmax_t)time(NULL), buf) < 0)
		die("cannot write to client:");
}

static void
handle_sig_child(int sig)
{
	if (sig != SIGCHLD)
		abort();

	die("child died\n");
}

/* fifo is opened read and write, so there is never EOF */
static int
fifo_setup(const char *dir, const char *host)
{
	const char *fifoname = FIFO_NAME;
	char *path;
	int fd;

	/* +3 for //\0 */
	path = malloc(strlen(dir) + strlen(host) + strlen(fifoname) + 3);
	if (path == NULL)
		die("malloc:");

	sprintf(path, "%s/%s", dir, host);

	if (mkdir(path, 0777) == -1 && errno != EEXIST)
		die("cannot create directory '%s':", path);

	sprintf(path, "%s/%s/%s", dir, host, fifoname);

	if (mkfifo(path, 0600) == -1 && errno != EEXIST)
		die("cannot create fifo '%s':", path);

	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd == -1)
		die("cannot open fifo '%s':", path);

	free(path);

	return (fd);
}

static const char *
get_username()
{
	struct passwd *pw = getpwuid(geteuid());
	if (pw == NULL)
		die("cannot get username:");

	return pw->pw_name;
	
}

static const char *
set_var(const char *name, const char *def)
{
	if (setenv(name, def, 0) == -1)
		die("cannot set environment variable '%s':", name);

	return getenv(name);
}

int
main(int argc, char **argv)
{
	int sock, fifo_fd, max_fd;
	int child_pipe[2];
	time_t trespond;
	fd_set rdset;
	prog = argc ? argv[0] : "jjd";

	const char *ircdir = set_var("IRC_DIR",    DEFAULT_DIR);
	const char *host   = set_var("IRC_HOST",   DEFAULT_HOST);
	const char *port   = set_var("IRC_PORT",   DEFAULT_PORT);
	const char *cmd    = set_var("IRC_CLIENT", DEFAULT_CMD);
	const char *nick   = set_var("IRC_NICK",   get_username());
	set_var("IRC_USER", nick);
	set_var("IRC_REALNAME", nick);

	fifo_fd = fifo_setup(ircdir, host);

	/* stdin, stdout or stderr was closed. too funny. */
	if (fifo_fd <= 2)
		abort();

	signal(SIGCHLD, handle_sig_child);

	if (pipe(child_pipe))
		die("pipe:");

	sock = dial(host, port); /* dies if cannot connect */

	pid_t child_pid = fork();
	if (child_pid < 0)
		die("fork:");

	if (child_pid == 0) {
		dup2(child_pipe[0], 0); /* stdin */
		dup2(sock, 1);          /* stdout */

		close(child_pipe[0]);
		close(child_pipe[1]);
		close(sock);
		close(fifo_fd);

		execlp(cmd, cmd, NULL);
		die("execlp '%s':", cmd);
	}
	close(child_pipe[0]);

	trespond = time(NULL);

	FD_ZERO(&rdset);

	max_fd = fifo_fd;
	if (max_fd < sock)
		max_fd = sock;

	for (;;) {
		int n;
		struct timeval tv;

		tv.tv_sec = 120;
		tv.tv_usec = 0;

		FD_SET(sock,    &rdset);
		FD_SET(fifo_fd, &rdset);

		n = select(max_fd + 1, &rdset, NULL, NULL, &tv);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			die("select:");
		}
		if (n == 0) {
			if (time(NULL) - trespond >= 300)
				die("shutting down: ping timeout");

			dprintf(sock, "PING %s\r\n", host);
			continue;
		}

		if (FD_ISSET(sock, &rdset)) {
			trespond = time(NULL);
			input_from_socket(sock, child_pipe[1]);
		}

		if (FD_ISSET(fifo_fd, &rdset)) {
			input_from_fifo(fifo_fd, child_pipe[1]);
		}
	}
}
