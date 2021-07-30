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
static const int *pipe_fd;

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
read_line(FILE *fp, char *buf, size_t bufsize)
{
	int c;
	size_t i = 0;

	if (bufsize < 2) /* must have room for 1 character and \0 */
		abort();

	while ((c = fgetc(fp)) != EOF) {
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
input_from_socket(FILE *fp)
{
	char buf[IRC_MSG_MAX];

	if (read_line(fp, buf, sizeof buf))
		die("remote host closed the connection");

	if (dprintf(*pipe_fd, "i %ju %s\n", (uintmax_t)time(NULL), buf) < 0)
		die("cannot write to client:");

}

static void
input_from_fifo(FILE *fp)
{
	char buf[IRC_MSG_MAX];

	if (read_line(fp, buf, sizeof buf))
		die("failed reading fifo:");

	if (dprintf(*pipe_fd, "u %ju %s\n", (uintmax_t)time(NULL), buf) < 0)
		die("cannot write to client:");
}

static void
handle_sig_child(int sig)
{
	if (sig != SIGCHLD)
		abort();

	die("child died\n");
}

static void
handle_sig_usr1(int sig)
{
	if (dprintf(*pipe_fd, "s %ju SIGUSR1\n", (uintmax_t)time(NULL)) < 0)
		die("cannot write to client:");
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
	int sock_in, sock_out, fifo_fd, max_fd;
	FILE *sock_fp, *fifo_fp;
	int child_pipe[2];
	time_t trespond;
	fd_set rdset;
	sigset_t masked, not_masked;
	struct sigaction sa;

	prog = argc ? argv[0] : "jjd";

	/* Signals will be caught sequentially,
	 * never interrupting each other. */

	sigemptyset(&masked);
	sigaddset(&masked, SIGCHLD);
	sigaddset(&masked, SIGUSR1);

	/* Enter safe region and get "vulnerable" sigset. */

	sigprocmask(SIG_BLOCK, &masked, &not_masked);

	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = handle_sig_child;
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sa.sa_mask = masked;
	sigaction(SIGCHLD, &sa, NULL);

	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = handle_sig_usr1;
	sa.sa_flags = SA_RESTART;
	sa.sa_mask = masked;
	sigaction(SIGUSR1, &sa, NULL);

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

	if (pipe(child_pipe))
		die("pipe:");

	if (getenv("PROTO") == 0) {
		/* Dies if cannot connect. */
		sock_in = sock_out = dial(host, port);
	} else {
		/* UCSPI sockets */
		sock_in  = 6;
		sock_out = 7;
	}

	pid_t child_pid = fork();
	if (child_pid < 0)
		die("fork:");

	if (child_pid == 0) {
		dup2(child_pipe[0], 0); /* stdin */
		dup2(sock_out, 1);      /* stdout */

		close(child_pipe[0]);
		close(child_pipe[1]);
		close(sock_in);
		close(sock_out);
		close(fifo_fd);

		execlp(cmd, cmd, NULL);
		die("execlp '%s':", cmd);
	}
	close(child_pipe[0]);
	pipe_fd = &child_pipe[1];

	trespond = time(NULL);

	FD_ZERO(&rdset);

	sock_fp = fdopen(sock_in, "r");
	if (sock_fp == NULL)
		die("could not setup buffering on input socket fd%d", sock_in);

	fifo_fp = fdopen(fifo_fd, "r");
	if (fifo_fp == NULL)
		die("could not setup buffering on fifo fd%d", fifo_fd);

	max_fd = fifo_fd;
	if (max_fd < sock_out)
		max_fd = sock_out;

	for (;;) {
		int n;
		struct timespec tv;

		tv.tv_sec = 120;
		tv.tv_nsec = 0;

		FD_SET(sock_in,  &rdset);
		FD_SET(fifo_fd,  &rdset);

		n = pselect(max_fd + 1, &rdset, NULL, NULL, &tv, &not_masked);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			die("select:");
		}
		if (n == 0) {
			if (time(NULL) - trespond >= 300)
				die("shutting down: ping timeout");

			dprintf(sock_out, "PING %s\r\n", host);
			continue;
		}

		if (FD_ISSET(sock_in, &rdset)) {
			trespond = time(NULL);
			input_from_socket(sock_fp);
		}

		if (FD_ISSET(fifo_fd, &rdset)) {
			input_from_fifo(fifo_fp);
		}
	}
}
