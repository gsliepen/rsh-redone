/*
    rsh.c - remote shell client
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

void usage(void) {
	fprintf(stderr, "Usage: rsh [-l user] [-p port] host command...\n");
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

int main(int argc, char **argv) {
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
	
	char opt;

	char hostaddr[NI_MAXHOST];
	char portnr[NI_MAXSERV];

	char buf[4096];
	int len;
	
	struct pollfd pfd[3];
	
	/* Lookup local username */
	
	if (!(pw = getpwuid(getuid()))) {
		fprintf(stderr, "%s: Could not lookup username: %s\n", argv[0], strerror(errno));
		return 1;
	}
	user = luser = pw->pw_name;
	
	/* Process options */
			
	while((opt = getopt(argc, argv, "+l:p:")) != -1) {
		switch(opt) {
			case 'l':
				luser = user = optarg;
				break;
			case 'p':
				port = optarg;
				break;
			default:
				fprintf(stderr, "%s: Unknown option!\n", argv[0]);
				usage();
				return 1;
		}
	}
	
	if(optind == argc) {
		fprintf(stderr, "%s: No host specified!\n", argv[0]);
		usage();
		return 1;
	}
	
	host = argv[optind++];
	
	/* Resolve hostname and try to make a connection */
	
	memset(&hint, '\0', sizeof(hint));
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	
	err = getaddrinfo(host, port, &hint, &ai);
	
	if(err) {
		fprintf(stderr, "%s: Error looking up host: %s\n", argv[0], gai_strerror(err));
		return 1;
	}
	
	hint.ai_flags = AI_PASSIVE;
	
	for(aip = ai; aip; aip = aip->ai_next) {
		if(getnameinfo(aip->ai_addr, aip->ai_addrlen, hostaddr, sizeof(hostaddr), portnr, sizeof(portnr), NI_NUMERICHOST | NI_NUMERICSERV)) {
			fprintf(stderr, "%s: Error resolving address: %s\n", argv[0], strerror(errno));
			return 1;
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
				return 1;
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
		fprintf(stderr, "%s: Could not make a connection.\n", argv[0]);
		return 1;
	}
	
	/* Create a socket for the incoming connection for stderr output */
	
	if((lsock = socket(aip->ai_family, aip->ai_socktype, aip->ai_protocol)) == -1) {
		fprintf(stderr, "%s: Could not open socket: %s\n", argv[0], strerror(errno));
		return 1;
	}
	
	hint.ai_family = aip->ai_family;
	
	freeaddrinfo(ai);
	
	for(i--; i >= 512; i--) {
		snprintf(lport, sizeof(lport), "%d", i);
		err = getaddrinfo(NULL, lport, &hint, &lai);
		if(err) {
			fprintf(stderr, "%s: Error looking up localhost: %s\n", argv[0], gai_strerror(err));
			return 1;
		}

		err = bind(lsock, lai->ai_addr, lai->ai_addrlen);

		freeaddrinfo(lai);

		if(err)
			continue;
		else
			break;
	}
	
	if(err) {
		fprintf(stderr, "%s: Could not bind to privileged port: %s\n", argv[0], strerror(errno));
		return 1;
	}
	
	if(listen(lsock, 1)) {
		fprintf(stderr, "%s: Could not listen: %s\n", argv[0], strerror(errno));
		return 1;
	}
	
	/* Send required information to the server */
	
	safewrite(sock, lport, strlen(lport) + 1);
	safewrite(sock, luser, strlen(luser) + 1);
	safewrite(sock, user, strlen(user) + 1);

	for(; optind < argc; optind++) {
		safewrite(sock, argv[optind], strlen(argv[optind]));
		if(optind < argc - 1)
			safewrite(sock, " ", 1);
	}
	safewrite(sock, "", 1);

	/* Wait for acknowledgement from server */
	
	errno = 0;
	
	if(read(sock, buf, 1) != 1 || *buf) {
		fprintf(stderr, "%s: Didn't receive NULL byte from server: %s\n", argv[0], strerror(errno));
		return 1;
	}

	/* Wait for incoming connection from server */
	
	if((esock = accept(lsock, &raddr, &raddrlen)) == -1) {
		fprintf(stderr, "%s: Could not accept stderr connection: %s\n", argv[0], strerror(errno));
		return 1;
	}
	
	close(lsock);
	
	/* Process input/output */
	
	pfd[0].fd = 0;
	pfd[0].events = POLLIN | POLLERR | POLLHUP;
	pfd[1].fd = sock;
	pfd[1].events = POLLIN | POLLERR | POLLHUP;
	pfd[2].fd = esock;
	pfd[2].events = POLLIN | POLLERR | POLLHUP;

	for(;;) {
		errno = 0;
		
		if(poll(pfd, 3, -1) == -1) {
			fprintf(stderr, "%s: Error while polling: %s\n", argv[0], strerror(errno));
			return 1;
		}

		if(pfd[0].revents) {
			len = read(0, buf, sizeof(buf));
			if(len <= 0) {
				fprintf(stderr, "%s: Error while reading from stdin: %s\n", argv[0], strerror(errno));
				return 1;
			}
			safewrite(sock, buf, len);
			pfd[0].revents = 0;
		}

		if(pfd[2].revents) {
			len = read(esock, buf, sizeof(buf));
			if(len <= 0) {
				fprintf(stderr, "%s: Error while reading from stderr socket: %s\n", argv[0], strerror(errno));
				return 1;
			}
			safewrite(2, buf, len);
			pfd[2].revents = 0;
		}

		if(pfd[1].revents) {
			len = read(sock, buf, sizeof(buf));
			if(len <= 0) {
				fprintf(stderr, "%s: Error while reading from stdout socket: %s\n", argv[0], strerror(errno));
				return 1;
			}
			safewrite(1, buf, len);
			pfd[1].revents = 0;
		}
	}
		
	return 0;
}
