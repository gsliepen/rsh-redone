/*
    rcp.c - remote file copy program
    Copyright (C) 2003  Guus Sliepen <guus@sliepen.eu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as published
	by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

char *argv0;

void usage(void) {
	fprintf(stderr, "Usage: %s [-p] [-r] source... destination\n", argv0);
}

/* Make sure everything gets written */

ssize_t safewrite(int fd, const void *buf, size_t count) {
	int written = 0, result;
	
	while(count) {
		result = write(fd, buf, count);
		if(result == -1)
			return result;
		written += result;
		buf += result;
		count -= result;
	}
	
	return written;
}

ssize_t saferead(int fd, const void *buf, size_t count) {
	int written = 0, result;
	
	while(count) {
		result = read(fd, buf, count);
		if(result == -1)
			return result;
		written += result;
		buf += result;
		count -= result;
	}
	
	return written;
}

{
	char *user = NULL;
	char *luser = NULL;
	char *host = NULL;
	char *port = "shell";
	char lport[5];
	
	struct passwd *pw;
	
	struct addrinfo hint, *ai, *aip, *lai;
	struct sockaddr raddr;
	int raddrlen;
	int err, sock = -1, lsock = -1, esock, i;
	
	char hostaddr[NI_MAXHOST];
	char portnr[NI_MAXSERV];

	char buf[4096];
	int len;
	
	struct pollfd pfd[3];

	/* Lookup local username */
	
	if (!(pw = getpwuid(getuid()))) {
		fprintf(stderr, "%s: Could not lookup username: %s\n", argv0, strerror(errno));
		return 1;
	}
	user = luser = pw->pw_name;
	
}

ssize_t safesend(int dst, int src, ssize_t len) {
	off_t offset = 0;
	char *mbuf = NULL;
	ssize_t x;
	
	while(len) {
		x = sendfile(dst, src, &offset, len);
		if(x <= 0) {
			if(offset)
				return -1;
			else
				goto mmap;
		}
		len -= x;
	}

	return offset;

mmap:
	if(ftruncate(src, len))
		goto mmap2;
		
	mbuf = mmap(NULL, len, PROT_READ, MAP_PRIVATE, 0, src, 0);

	if(!mbuf)
		goto mmap2;
	
	if(safewrite(dst, mbuf, len) == -1)
		return -1;
	
	munmap(mbuf, len);
	
	return len;

mmap2:
	if(ftruncate(dst, len))
		goto oldway;
		
	mbuf = mmap(NULL, len, PROT_READ, MAP_PRIVATE, 0, dst, 0);

	if(!mbuf)
		goto oldway;
	
	if(saferead(src, mbuf, len) == -1)
		return -1;
	
	munmap(mbuf, len);
	
	return len;
		
oldway:
	while(len) {
		x = read(src, buf, sizeof(buf));
		if(x <= 0)
			return -1;
		if(safewrite(dst, buf, x) == -1)
			return -1;
		count -= x;
	}
	
	return len;
}

ssize_t send_file(int out, int file, char *name, struct stat stat, int preserve) {
	size_t size = stat.st_size;
	size_t len, offset = 0;
	
	if(preserve) {
		snprintf(buf, sizeof(buf), "T%li 0 %li 0\n", stat.st_mtime, stat.st_atime);
		if(safewrite(out, buf, strlen(buf)) == -1)
			return -1;
	}
	
	snprintf(buf, sizeof(buf), "C%04o %li %s\n", stat.st_mode&07777, size, safebasename(name));
	if(safewrite(out, buf, strlen(buf)) == -1)
		return -1;
	
	if(recvresponse())
		return -1;
	
	if(safesend(out, file, size) == -1)
		return -1;
	
	return recvresponse();
}

int send_dir(int out, char *name, struct stat stat, int preserve) {
	DIR *dir;
	struct dirent *ent;
	char buf[1024];
		
	if(preserve) {
		snprintf(buf, sizeof(buf), "T%li 0 %li 0\n", stat.st_mtime, stat.at_mtime);
		if(safewrite(out, buf, strlen(buf)) == -1)
			return -1;
	}
	
	snprintf(buf, sizeof(buf), "D%04o %li %s\n", stat.st_mode&07777, 0, safebasename(name));
	if(safewrite(out, buf, strlen(buf)) == -1)
		return -1;
	
	dir = opendir(name);
	
	if(!dir)
		return -1;
	
	while((ent = readdir(dir)) {
		if(!ent>d_ino)
			continue;
		if(!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		snprintf(buf, sizeof(buf), "%s/%s", name, ent->d_name);
		from(1, &buf, 1, preserve);
	}
	
	closedir(dir);

	snprintf(buf, sizeof(buf), "E\n");
	if(safewrite(out, buf, strlen(buf)) == -1)
		return -1;
	
	return recvresponse();
}

int from(int fd, int argc, char **argv, int recursive, int preserve) {
	int i;
	struct stat stat;
	int file;
	
	for(i = 0; i < argc; i++) {
		file = open(argv[i], O_RDONLY);
		
		if(file == -1) {
			senderror("%s: %s: %s\n", argv0, argv[i], strerror(errno));
			continue;
		}
		
		if(fstat(file, &stat)) {
			close(file);
			senderror("%s: %s: %s\n", argv0, argv[i], strerror(errno));
			continue;
		}
		
		switch(stat.st_modes & S_IFMT) {
			case S_IFREG:
				send_file(fd, file, stat, recurse, preserve);
				break;
			case SI_IFDIR:
				if(!recursive)
					send_dir(fd, argv[i], stat, preserve);
			default:
				senderror("%s: %s: not a regular file\n", argv0, argv[i]);
				continue;
		}
		
		close(file);
	}
	
	return recvresponse();
}

int to(char *dname, int preserve, int dir) {
	int i;
	struct stat stat;
	int file;
	int mode, size;
	struct utimbuf time;
	char name[1024];
	
	for(;;) {
		if(preserve) {
			if(readto(0, buf, sizeof(buf), '\n') <= 0)
				return -1;
			if(sscanf(buf, "T%li 0 %li 0", &time.modtime, &time.actime) != 2)
				return -1;
		}
		
		if(readto(0, buf, sizeof(buf), '\n') <= 0)
			return -1;
		
		switch(*buf) {
			case 'E':
				if(!dir || buf[1])
					return -1;
				safewrite(0, "", 1);
				return 0;
			case 'D':
				if(sscanf(buf, "D%04o %li %1024s", &type, &mode, &size, name) != 4)
					return -1;

				if(!name)
					return -1;
				
				if(mkdir(name, mode) || chdir(name)) {
					sendresponse(strerror(errno));
					continue;
				}

				safewrite(0, "", 1);

				from(NULL, preserve, 1);
				
				if(chdir(".."))
					return -1;
				
				if(preserve && utime(name, &time))
					return -1;
				
				free(fname);
				free(name);
				
				continue;
			case 'C':
				if(sscanf(buf, "C%04o %li %1024s", &type, &mode, &size, name) != 4)
					return -1;

				if(!name)
					return -1;
				
				file = open(name, O_WRONLY | O_CREAT, mode);

				if(!file) {
					sendresponse(strerror(errno));
					continue;
				}

				if(safewrite(0, "", 1) == -1)
					return -1;

				if(safesend(file, 0, size) == -1)
					return -1;

				if(preserve && utime(name, &time))
					return -1;
				
				close(file);
				
				if(recvresponse())
					return -1;

				continue;
			default:
				return -1;
		}
	}
}	

int split(char *name, char **user, char **host, char **file) {
	char *colon, *slash, *at;
	
	colon = strrchr(name, ':');
	slash = strrchr(name, '/');
	
	if(!colon || (slash && slash < colon))
		return 0;
	
	at = strrchr(name, '@');
	
	if(at && at > colon)
		at = NULL;
	
	*colon++ = '\0';
	*file = colon;
	
	if(at) {
		*at++ = '\0';
		*user = name;
		*host = at;
	} else
		*host = name;
	
	return 1;
}
	
int remote(char *user, char *host) {
	/* Resolve hostname and try to make a connection */
	
	memset(&hint, '\0', sizeof(hint));
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	
	err = getaddrinfo(host, port, &hint, &ai);
	
	if(err) {
		fprintf(stderr, "%s: Error looking up host: %s\n", argv0, gai_strerror(err));
		return -1;
	}
	
	hint.ai_flags = AI_PASSIVE;
	
	for(aip = ai; aip; aip = aip->ai_next) {
		if(getnameinfo(aip->ai_addr, aip->ai_addrlen, hostaddr, sizeof(hostaddr), portnr, sizeof(portnr), NI_NUMERICHOST | NI_NUMERICSERV)) {
			fprintf(stderr, "%s: Error resolving address: %s\n", argv0, strerror(errno));
			return -1;
		}
		fprintf(stderr, "Trying %s port %s...",	hostaddr, portnr);
		
		if((sock = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol)) == -1) {
			fprintf(stderr, " Could not open socket: %s\n", strerror(errno));
			continue;
		}

		hint.ai_family = aip->ai_family;

		/* Bind to a privileged port */
				
		for(i = 1023; i >= 512; i--) {
			snprintf(lport, sizeof(lport), "%d", i);
			err = getaddrinfo(NULL, lport, &hint, &lai);
			if(err) {
				fprintf(stderr, " Error looking up localhost: %s\n", gai_strerror(err));
				return -1;
			}
			
			err = bind(sock, lai->ai_addr, lai->ai_addrlen);
			
			freeaddrinfo(lai);
			
			if(err)
				continue;
			else
				break;
		}
		
		if(err) {
			fprintf(stderr, " Could not bind to privileged port: %s\n", strerror(errno));
			continue;
		}
		
		if(connect(sock, aip->ai_addr, aip->ai_addrlen) == -1) {
			fprintf(stderr, " Connection failed: %s\n", strerror(errno));
			continue;
		}
		fprintf(stderr, " Connected.\n");
		break;
	}
	
	if(!aip) {
		fprintf(stderr, "%s: Could not make a connection.\n", argv0);
		return -1;
	}
	
	/* Send required information to the server */
	
	if(safewrite(sock, "0", 2) == -1 || 
	   safewrite(sock, luser, strlen(luser) + 1) == -1 ||
	   safewrite(sock, user, strlen(user) + 1) == -1 ||
	   safewrite(sock, command, strlen(user) + 1) == -1) {
		fprintf(stderr, "%s: Unable to send required information: %s\n", argv0, strerror(errno));
		return -1;
	}

	/* Wait for acknowledgement from server */
	
	errno = 0;
	
	if(read(sock, buf, 1) != 1 || *buf) {
		fprintf(stderr, "%s: Didn't receive NULL byte from server: %s\n", argv0, strerror(errno));
		return -1;
	}
	
	return sock;
}

int main(int argc, char **argv) {
	char opt;

	int preserve = 0, recurse = 0, from = 0, to = 0, dir = 0;
	
	argv0 = argv[0];
	
	/* Process options */
			
	while((opt = getopt(argc, argv, "+rpdft")) != -1) {
		switch(opt) {
			case 'p':
				preserve = 1;
				break;
			case 'r':
				recurse = 1;
				break;
			case 'd':
				dir = 1;
				break;
			case 'f':
				from = 1;
				break;
			case 't':
				to = 1;
				break;
			default:
				fprintf(stderr, "%s: Unknown option!\n", argv0);
				usage();
				return 1;
		}
	}
	
	argc -= optind;
	argv += optind;
	
	if(from || to) {
		if(from && to) {
			fprintf(stderr, "%s: specify only one of -f and -t!\n", argv0);
			return 1;
		}
		
		/* Immediately drop privileges */
		
		if(setuid(getuid)) {
			fprintf(stderr, "%s: setuid() failed: %s\n", argv0, strerror(errno));
			return 1;
		}
		
		if(from)
			from(argc, argv, recurse, preserve, 0);
		else {
			if(dir && chdir(*argv))
					return -1;
			to(dir?NULL:*argv, preserve, dir);
		}
	}

	if(argc < 2) {
		fprintf(stderr, "%s: Not enough arguments!\n", argv0);
		usage();
		return 1;
	}
	
	dest = argv[--argc];
	
	if(split(dest, &user, &host, &file)) {
		asprintf(&command, "rcp%s%s%s -t %s", recursive?" -r":"", preserve?" -p":"", argc > 1?" -d":"", file);
		fd = remote(user, host, command);
		free(command);
		if(!fd) {
			fprintf(stderr, "%s: error connecting to %s: %s", argv0, host, strerror(errno));
			return -1;
		}
	} else {
	}
}




#if 0
/*
 * rcp
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#if defined(linux) && defined(FSUID_HACK)
#include <sys/fsuid.h>
#endif
#include <netinet/in.h>
#include <netinet/ip.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pathnames.h"

#define	OPTIONS "dfprt"

struct passwd *pwd;
u_short	port;
uid_t	userid;
int errs, rem;
int pflag, iamremote, iamrecursive, targetshouldbedirectory;
static char **saved_environ;

#define	CMDNEEDS	64
char cmd[CMDNEEDS];		/* must hold "rcp -r -p -d\0" */

typedef struct _buf {
	int	cnt;
	char	*buf;
} BUF;

static void lostconn(int);
static char *colon(char *);
static int response(void);
static void verifydir(const char *cp);
static int okname(const char *cp0);
static int susystem(const char *s);
static void source(int argc, char *argv[]);
static void rsource(char *name, struct stat *statp);
static void sink(int argc, char *argv[]);
static BUF *allocbuf(BUF *bp, int fd, int blksize);
static void nospace(void);
static void usage(void);
static void toremote(const char *targ, int argc, char *argv[]);
static void tolocal(int argc, char *argv[]);
static void error(const char *fmt, ...);

int
main(int argc, char *argv[])
{
	struct servent *sp;
	int ch, fflag, tflag;
	char *targ;
	const char *shell;
	char *null = NULL;

	saved_environ = __environ;
	__environ = &null;

	fflag = tflag = 0;
	while ((ch = getopt(argc, argv, OPTIONS)) != EOF)
		switch(ch) {
		/* user-visible flags */
		case 'p':			/* preserve access/mod times */
			++pflag;
			break;
		case 'r':
			++iamrecursive;
			break;
		/* rshd-invoked options (server) */
		case 'd':
			targetshouldbedirectory = 1;
			break;
		case 'f':			/* "from" */
			iamremote = 1;
			fflag = 1;
			break;
		case 't':			/* "to" */
			iamremote = 1;
			tflag = 1;
			break;

		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	sp = getservbyname(shell = "shell", "tcp");
	if (sp == NULL) {
		(void)fprintf(stderr, "rcp: %s/tcp: unknown service\n", shell);
		exit(1);
	}
	port = sp->s_port;

	if (!(pwd = getpwuid(userid = getuid()))) {
		(void)fprintf(stderr, "rcp: unknown user %d.\n", (int)userid);
		exit(1);
	}

	if (fflag) {
		/* follow "protocol", send data */
		(void)response();
		if (setuid(userid)) {
			fprintf(stderr, "rcp: setuid: %s\n", strerror(errno));
			exit(1);
		}
		source(argc, argv);
		exit(errs);
	}

	if (tflag) {
		/* receive data */
		if (setuid(userid)) {
			fprintf(stderr, "rcp: setuid: %s\n", strerror(errno));
			exit(1);
		}
		sink(argc, argv);
		exit(errs);
	}

	if (argc < 2)
		usage();
	if (argc > 2)
		targetshouldbedirectory = 1;

	rem = -1;
	/* command to be executed on remote system using "rsh" */
	(void)snprintf(cmd, sizeof(cmd), "rcp%s%s%s",
	    iamrecursive ? " -r" : "", pflag ? " -p" : "",
	    targetshouldbedirectory ? " -d" : "");

	(void)signal(SIGPIPE, lostconn);

	if ((targ = colon(argv[argc - 1]))!=NULL) {
		/* destination is remote host */
		*targ++ = 0;
		toremote(targ, argc, argv);
	}
	else {
		tolocal(argc, argv);		/* destination is local host */
		if (targetshouldbedirectory)
			verifydir(argv[argc - 1]);
	}
	exit(errs);
}

static void
toremote(const char *targ, int argc, char *argv[])
{
	int i, len, tos;
	char *bp, *host, *src, *suser, *thost, *tuser;

	if (*targ == 0)
		targ = ".";

	if ((thost = strchr(argv[argc - 1], '@'))!=NULL) {
		/* user@host */
		*thost++ = 0;
		tuser = argv[argc - 1];
		if (*tuser == '\0')
			tuser = NULL;
		else if (!okname(tuser))
			exit(1);
	} else {
		thost = argv[argc - 1];
		tuser = NULL;
	}

	for (i = 0; i < argc - 1; i++) {
		src = colon(argv[i]);
		if (src) {			/* remote to remote */
			static char dot[] = ".";
			*src++ = 0;
			if (*src == 0)
				src = dot;
			host = strchr(argv[i], '@');
			len = strlen(_PATH_RSH) + strlen(argv[i]) +
			    strlen(src) + (tuser ? strlen(tuser) : 0) +
			    strlen(thost) + strlen(targ) + CMDNEEDS + 20;
			if (!(bp = malloc(len)))
				nospace();
			if (host) {
				*host++ = 0;
				suser = argv[i];
				if (*suser == '\0')
					suser = pwd->pw_name;
				else if (!okname(suser))
					continue;
				(void)snprintf(bp, len,
				    "%s %s -l %s -n %s %s '%s%s%s:%s'",
				    _PATH_RSH, host, suser, cmd, src,
				    tuser ? tuser : "", tuser ? "@" : "",
				    thost, targ);
			} else
				(void)snprintf(bp, len,
				    "%s %s -n %s %s '%s%s%s:%s'",
				    _PATH_RSH, argv[i], cmd, src,
				    tuser ? tuser : "", tuser ? "@" : "",
				    thost, targ);
			(void)susystem(bp);
			(void)free(bp);
		} else {			/* local to remote */
			if (rem == -1) {
				len = strlen(targ) + CMDNEEDS + 20;
				if (!(bp = malloc(len)))
					nospace();
				(void)snprintf(bp, len, "%s -t %s", cmd, targ);
				host = thost;
#if defined(linux) && defined(FSUID_HACK)
				setfsuid(getuid());
#endif
					rem = rcmd(&host, port, pwd->pw_name,
					    tuser ? tuser : pwd->pw_name,
					    bp, 0);
#if defined(linux) && defined(FSUID_HACK)
				setfsuid(geteuid());
#endif
				if (rem < 0)
					exit(1);
#ifdef IP_TOS
				tos = IPTOS_THROUGHPUT;
				if (setsockopt(rem, IPPROTO_IP, IP_TOS,
				    (char *)&tos, sizeof(int)) < 0)
					perror("rcp: setsockopt TOS (ignored)");
#endif
				if (response() < 0)
					exit(1);
				(void)free(bp);
				if (setuid(userid)) {
					fprintf(stderr, "rcp: setuid: %s\n",
						strerror(errno));
				}
			}
			source(1, argv+i);
		}
	}
}

static void
tolocal(int argc, char *argv[])
{
 	static char dot[] = ".";
	int i, len, tos;
	char *bp, *host, *src, *suser;

	for (i = 0; i < argc - 1; i++) {
		if (!(src = colon(argv[i]))) {	/* local to local */
			len = strlen(_PATH_CP) + strlen(argv[i]) +
			    strlen(argv[argc - 1]) + 20;
			if (!(bp = malloc(len)))
				nospace();
			(void)snprintf(bp, len, "%s%s%s %s %s", _PATH_CP,
			    iamrecursive ? " -r" : "", pflag ? " -p" : "",
			    argv[i], argv[argc - 1]);
			(void)susystem(bp);
			(void)free(bp);
			continue;
		}
		*src++ = 0;
		if (*src == 0)
			src = dot;
		host = strchr(argv[i], '@');
		if (host) {
			*host++ = 0;
			suser = argv[i];
			if (*suser == '\0')
				suser = pwd->pw_name;
			else if (!okname(suser))
				continue;
		} else {
			host = argv[i];
			suser = pwd->pw_name;
		}
		len = strlen(src) + CMDNEEDS + 20;
		if (!(bp = malloc(len)))
			nospace();
		(void)snprintf(bp, len, "%s -f %s", cmd, src);
#if defined(linux) && defined(FSUID_HACK)
		setfsuid(getuid());
#endif
			rem = rcmd(&host, port, pwd->pw_name, suser, bp, 0);
#if defined(linux) && defined(FSUID_HACK)
		setfsuid(geteuid());
#endif
		(void)free(bp);
		if (rem < 0) {
			++errs;
			continue;
		}
		(void)seteuid(userid);
#ifdef IP_TOS
		tos = IPTOS_THROUGHPUT;
		if (setsockopt(rem, IPPROTO_IP, IP_TOS,
		    (char *)&tos, sizeof(int)) < 0)
			perror("rcp: setsockopt TOS (ignored)");
#endif
		sink(1, argv + argc - 1);
		(void)seteuid(0);
		(void)close(rem);
		rem = -1;
	}
}

static void
verifydir(const char *cp)
{
	struct stat stb;

	if (stat(cp, &stb) >= 0) {
		if ((stb.st_mode & S_IFMT) == S_IFDIR)
			return;
		errno = ENOTDIR;
	}
	error("rcp: %s: %s.\n", cp, strerror(errno));
	exit(1);
}

static char *
colon(char *cp)
{
	for (; *cp; ++cp) {
		if (*cp == ':')
			return(cp);
		if (*cp == '/')
			return NULL;
	}
	return NULL;
}

static int
okname(const char *cp0)
{
	const char *cp = cp0;
	int c;

	do {
		c = *cp;
		if (c & 0200)
			goto bad;
		if (!isalpha(c) && !isdigit(c) && c != '_' && c != '-')
			goto bad;
	} while (*++cp);
	return(1);
bad:
	(void)fprintf(stderr, "rcp: invalid user name %s\n", cp0);
	return 0;
}

typedef void (*sighandler)(int);

static int
susystem(const char *s)
{
	int status, pid, w;
	sighandler istat, qstat;

	if ((pid = vfork()) == 0) {
		const char *args[4];
		const char **argsfoo;
		char **argsbar;
		if (setuid(userid)) {
			fprintf(stderr, "rcp: child: setuid: %s\n", 
				strerror(errno));
			_exit(1);
		}
		args[0] = "sh";
		args[1] = "-c";
		args[2] = s;
		args[3] = NULL;
		/* Defeat C type system to permit passing char ** to execve */
		argsfoo = args;
		memcpy(&argsbar, &argsfoo, sizeof(argsfoo));
		execve(_PATH_BSHELL, argsbar, saved_environ);
		_exit(127);
	}
	istat = signal(SIGINT, SIG_IGN);
	qstat = signal(SIGQUIT, SIG_IGN);
	while ((w = wait(&status)) != pid && w != -1)
		;
	if (w == -1)
		status = -1;
	(void)signal(SIGINT, istat);
	(void)signal(SIGQUIT, qstat);
	return(status);
}

static void
source(int argc, char *argv[])
{
	struct stat stb;
	static BUF buffer;
	BUF *bp;
	off_t i;
	int x, readerr, f, amt;
	char *last, *name, buf[BUFSIZ];

	for (x = 0; x < argc; x++) {
		name = argv[x];
		if ((f = open(name, O_RDONLY)) < 0) {
			error("rcp: %s: %s\n", name, strerror(errno));
			continue;
		}
		if (fstat(f, &stb) < 0)
			goto notreg;
		switch (stb.st_mode&S_IFMT) {

		case S_IFREG:
			break;

		case S_IFDIR:
			if (iamrecursive) {
				(void)close(f);
				rsource(name, &stb);
				continue;
			}
			/* FALLTHROUGH */
		default:
notreg:			(void)close(f);
			error("rcp: %s: not a plain file\n", name);
			continue;
		}
		last = strrchr(name, '/');
		if (last == 0)
			last = name;
		else
			last++;
		if (pflag) {
			/*
			 * Make it compatible with possible future
			 * versions expecting microseconds.
			 */
			(void)snprintf(buf, sizeof(buf),
			    "T%ld 0 %ld 0\n", stb.st_tv[1], stb.st_tv[0]);
			(void)write(rem, buf, (int)strlen(buf));
			if (response() < 0) {
				(void)close(f);
				continue;
			}
		}
		(void)snprintf(buf, sizeof(buf),
		    "C%04o %ld %s\n", stb.st_mode&07777, stb.st_size, last);
		(void)write(rem, buf, (int)strlen(buf));
		if (response() < 0) {
			(void)close(f);
			continue;
		}
		if ((bp = allocbuf(&buffer, f, BUFSIZ)) == 0) {
			(void)close(f);
			continue;
		}
		readerr = 0;
		for (i = 0; i < stb.st_size; i += bp->cnt) {
			amt = bp->cnt;
			if (i + amt > stb.st_size)
				amt = stb.st_size - i;
			if (readerr == 0 && read(f, bp->buf, amt) != amt)
				readerr = errno;
			(void)write(rem, bp->buf, amt);
		}
		(void)close(f);
		if (readerr == 0)
			(void)write(rem, "", 1);
		else
			error("rcp: %s: %s\n", name, strerror(readerr));
		(void)response();
	}
}

static void
rsource(char *name, struct stat *statp)
{
	DIR *dirp;
	struct dirent *dp;
	char *last, *vect[1], *path;

	if (!(dirp = opendir(name))) {
		error("rcp: %s: %s\n", name, strerror(errno));
		return;
	}
	last = strrchr(name, '/');
	if (last == 0)
		last = name;
	else
		last++;
	if (pflag) {
		char buf[128];
		(void)snprintf(buf, sizeof(buf),
		    "T%ld 0 %ld 0\n", statp->st_mtime, statp->st_tv[0]);
		(void)write(rem, buf, (int)strlen(buf));
		if (response() < 0) {
			closedir(dirp);
			return;
		}
	}
	if (asprintf(&path, "D%04o %d %s\n", statp->st_mode&07777, 0, last) < 0) {
		error("out of memory\n");
		closedir(dirp);
		return;
	}
	(void)write(rem, path, (int)strlen(path));
	free(path);
	if (response() < 0) {
		closedir(dirp);
		return;
	}
	while ((dp = readdir(dirp))!=NULL) {
		if (dp->d_ino == 0)
			continue;
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;
		if (asprintf(&path, "%s/%s", name, dp->d_name) < 0) {
			error("out of memory\n");
			continue;
		}
		vect[0] = path;
		source(1, vect);
		free(path);
	}
	closedir(dirp);
	(void)write(rem, "E\n", 2);
	(void)response();
}

static int
response(void)
{
	register char *cp;
	char ch, resp, rbuf[BUFSIZ];

	if (read(rem, &resp, sizeof(resp)) != sizeof(resp))
		lostconn(0);

	cp = rbuf;
	switch(resp) {
	  case 0:			/* ok */
		return 0;
	  default:
		*cp++ = resp;
		/* FALLTHROUGH */
	  case 1:			/* error, followed by err msg */
	  case 2:			/* fatal error, "" */
		do {
			if (read(rem, &ch, sizeof(ch)) != sizeof(ch))
				lostconn(0);
			*cp++ = ch;
		} while (cp < &rbuf[BUFSIZ] && ch != '\n');

		if (!iamremote)
			write(2, rbuf, cp - rbuf);
		++errs;
		if (resp == 1)
			return -1;
		exit(1);
	}
	/*NOTREACHED*/
	return 0;
}

static void
lostconn(int ignore)
{
	(void)ignore;

	if (!iamremote)
		(void)fprintf(stderr, "rcp: lost connection\n");
	exit(1);
}

static void
sink(int argc, char *argv[])
{
	register char *cp;
	static BUF buffer;
	struct stat stb;
	struct timeval tv[2];
	enum { YES, NO, DISPLAYED } wrerr;
	BUF *bp;
	off_t i, j;
	char ch, *targ;
	const char *why;
	int amt, count, exists, first, mask, mode;
	int ofd, setimes, size, targisdir;
	char *np, *vect[1], buf[BUFSIZ];

#define	atime	tv[0]
#define	mtime	tv[1]
#define	SCREWUP(str)	{ why = str; goto screwup; }

	setimes = targisdir = 0;
	mask = umask(0);
	if (!pflag)
		(void)umask(mask);
	if (argc != 1) {
		error("rcp: ambiguous target\n");
		exit(1);
	}
	targ = *argv;
	if (targetshouldbedirectory)
		verifydir(targ);
	(void)write(rem, "", 1);
	if (stat(targ, &stb) == 0 && (stb.st_mode & S_IFMT) == S_IFDIR)
		targisdir = 1;
	for (first = 1;; first = 0) {
		cp = buf;
		if (read(rem, cp, 1) <= 0)
			return;
		if (*cp++ == '\n')
			SCREWUP("unexpected <newline>");
		do {
			if (read(rem, &ch, sizeof(ch)) != sizeof(ch))
				SCREWUP("lost connection");
			*cp++ = ch;
		} while (cp < &buf[BUFSIZ - 1] && ch != '\n');
		*cp = 0;

		if (buf[0] == '\01' || buf[0] == '\02') {
			if (iamremote == 0)
				(void)write(2, buf + 1, (int)strlen(buf + 1));
			if (buf[0] == '\02')
				exit(1);
			errs++;
			continue;
		}
		if (buf[0] == 'E') {
			(void)write(rem, "", 1);
			return;
		}

		if (ch == '\n')
			*--cp = 0;

#define getnum(t) (t) = 0; while (isdigit(*cp)) (t) = (t) * 10 + (*cp++ - '0');
		cp = buf;
		if (*cp == 'T') {
			setimes++;
			cp++;
			getnum(mtime.tv_sec);
			if (*cp++ != ' ')
				SCREWUP("mtime.sec not delimited");
			getnum(mtime.tv_usec);
			if (*cp++ != ' ')
				SCREWUP("mtime.usec not delimited");
			getnum(atime.tv_sec);
			if (*cp++ != ' ')
				SCREWUP("atime.sec not delimited");
			getnum(atime.tv_usec);
			if (*cp++ != '\0')
				SCREWUP("atime.usec not delimited");
			(void)write(rem, "", 1);
			continue;
		}
		if (*cp != 'C' && *cp != 'D') {
			/*
			 * Check for the case "rcp remote:foo\* local:bar".
			 * In this case, the line "No match." can be returned
			 * by the shell before the rcp command on the remote is
			 * executed so the ^Aerror_message convention isn't
			 * followed.
			 */
			if (first) {
				error("%s\n", cp);
				exit(1);
			}
			SCREWUP("expected control record");
		}
		mode = 0;
		for (++cp; cp < buf + 5; cp++) {
			if (*cp < '0' || *cp > '7')
				SCREWUP("bad mode");
			mode = (mode << 3) | (*cp - '0');
		}
		if (*cp++ != ' ')
			SCREWUP("mode not delimited");
		size = 0;
		while (isdigit(*cp))
			size = size * 10 + (*cp++ - '0');
		if (*cp++ != ' ')
			SCREWUP("size not delimited");
		if (targisdir) {
			static char *namebuf;
			static int cursize;
			int need;

			need = strlen(targ) + strlen(cp) + 250;
			if (need > cursize) {
				if (!(namebuf = malloc(need)))
					error("out of memory\n");
			}
			(void)snprintf(namebuf, need, "%s%s%s", targ,
			    *targ ? "/" : "", cp);
			np = namebuf;
		}
		else
			np = targ;
		exists = stat(np, &stb) == 0;
		if (buf[0] == 'D') {
			if (exists) {
				if ((stb.st_mode&S_IFMT) != S_IFDIR) {
					errno = ENOTDIR;
					goto bad;
				}
				if (pflag)
					(void)chmod(np, mode);
			} else if (mkdir(np, mode) < 0)
				goto bad;
			vect[0] = np;
			sink(1, vect);
			if (setimes) {
				setimes = 0;
				if (utimes(np, tv) < 0)
				    error("rcp: can't set times on %s: %s\n",
					np, strerror(errno));
			}
			continue;
		}
		if ((ofd = open(np, O_WRONLY|O_CREAT, mode)) < 0) {
bad:			error("rcp: %s: %s\n", np, strerror(errno));
			continue;
		}
		if (exists && pflag)
			(void)fchmod(ofd, mode);
		(void)write(rem, "", 1);
		if ((bp = allocbuf(&buffer, ofd, BUFSIZ)) == 0) {
			(void)close(ofd);
			continue;
		}
		cp = bp->buf;
		count = 0;
		wrerr = NO;
		for (i = 0; i < size; i += BUFSIZ) {
			amt = BUFSIZ;
			if (i + amt > size)
				amt = size - i;
			count += amt;
			do {
				j = read(rem, cp, amt);
				if (j <= 0) {
					error("rcp: %s\n",
					    j ? strerror(errno) :
					    "dropped connection");
					exit(1);
				}
				amt -= j;
				cp += j;
			} while (amt > 0);
			if (count == bp->cnt) {
				if (wrerr == NO &&
				    write(ofd, bp->buf, count) != count)
					wrerr = YES;
				count = 0;
				cp = bp->buf;
			}
		}
		if (count != 0 && wrerr == NO &&
		    write(ofd, bp->buf, count) != count)
			wrerr = YES;
		if (ftruncate(ofd, size)) {
			error("rcp: can't truncate %s: %s\n", np,
			    strerror(errno));
			wrerr = DISPLAYED;
		}
		(void)close(ofd);
		(void)response();
		if (setimes && wrerr == NO) {
			setimes = 0;
			if (utimes(np, tv) < 0) {
				error("rcp: can't set times on %s: %s\n",
				    np, strerror(errno));
				wrerr = DISPLAYED;
			}
		}
		switch(wrerr) {
		case YES:
			error("rcp: %s: %s\n", np, strerror(errno));
			break;
		case NO:
			(void)write(rem, "", 1);
			break;
		case DISPLAYED:
			break;
		}
	}
screwup:
	error("rcp: protocol screwup: %s\n", why);
	exit(1);
}

static BUF *
allocbuf(BUF *bp, int fd, int blksize)
{
	struct stat stb;
	int size;

	if (fstat(fd, &stb) < 0) {
		error("rcp: fstat: %s\n", strerror(errno));
		return(0);
	}
	size = roundup(stb.st_blksize, blksize);
	if (size == 0)
		size = blksize;
	if (bp->cnt < size) {
		if (bp->buf != 0)
			free(bp->buf);
		bp->buf = malloc(size);
		if (!bp->buf) {
			error("rcp: malloc: out of memory\n");
			return NULL;
		}
	}
	bp->cnt = size;
	return(bp);
}

void
error(const char *fmt, ...)
{
	static FILE *fp;
	va_list ap;

	va_start(ap, fmt);

	++errs;
	if (!fp && !(fp = fdopen(rem, "w")))
		return;
	fprintf(fp, "%c", 0x01);
	vfprintf(fp, fmt, ap);
	fflush(fp);
	if (!iamremote)	vfprintf(stderr, fmt, ap);

	va_end(ap);
}

static void 
nospace(void)
{
	(void)fprintf(stderr, "rcp: out of memory.\n");
	exit(1);
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: rcp [-p] f1 f2; or: rcp [-rp] f1 ... fn directory\n");
	exit(1);
}
#endif
