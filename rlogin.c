/*
    rlogin.c - remote login client
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
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <fcntl.h>

void usage(void) {
	fprintf(stderr, "Usage: rlogin [-l user] [-p port] host\n");
}

/* Make sure everything gets written */

ssize_t safewrite(int fd, const void *buf, size_t count) {
	int written = 0, result;
	
	while(count) {
		result = write(fd, buf, count);
		if(result == -1) {
			if(errno == EINTR)
				continue;
			else
				return result;
		}
		written += result;
		buf += result;
		count -= result;
	}
	
	return written;
}

/* Convert termios speed to a string */

char *termspeed(speed_t speed) {
	switch(speed) {
		case B0: return "0";
		case B50: return "50";
		case B75: return "75";
		case B110: return "110";
		case B134: return "134";
		case B150: return "150";
		case B200: return "200";
		case B300: return "300";
		case B600: return "600";
		case B1200: return "1200";
		case B1800: return "1800";
		case B2400: return "2400";
		case B4800: return "4800";
		case B9600: return "9600";
		case B19200: return "19200";
		case B38400: return "38400";
		case B57600: return "57600";
		case B115200: return "115200";
		case B230400: return "230400";
		case B460800: return "460800";
		case B500000: return "500000";
		case B576000: return "576000";
		case B921600: return "921600";
		case B1000000: return "1000000";
		case B1152000: return "1152000";
		case B1500000: return "1500000";
		case B2000000: return "2000000";
		case B2500000: return "2500000";
		case B3000000: return "3000000";
		case B3500000: return "3500000";
		case B4000000: return "4000000";
		default: return "9600";
	}
}

/* Catch SIGWINCH */

int winchpipe[2];

void sigwinch_h(int signal) {
	write(winchpipe[1], "", 1);
}

int main(int argc, char **argv) {
	char *user = NULL;
	char *luser = NULL;
	char *host = NULL;
	char *port = "login";
	char lport[5];
	
	struct passwd *pw;
	
	struct addrinfo hint, *ai, *aip, *lai;
	int err, sock = -1, i;
	
	char opt;

	char hostaddr[NI_MAXHOST];
	char portnr[NI_MAXSERV];

	struct termios tios, oldtios;
	char *term, *speed;
	
	char buf[4096];
	int len;
	
	struct pollfd pfd[3];
	
	struct winsize winsize;
	
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
	
	if(optind != argc) {
		fprintf(stderr, "%s: Too many arguments!\n", argv[0]);
		usage();
		return 1;
	}
	
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
			
	freeaddrinfo(ai);

	/* Drop privileges */
	
	if(setuid(getuid())) {
		fprintf(stderr, "%s: Unable to drop privileges: %s\n", argv[0], strerror(errno));
		return 1;
	}
	
	/* Send required information to the server */

	term = getenv("TERM")?:"network";
	
	if(tcgetattr(0, &tios)) {
		fprintf(stderr, "%s: Unable to get terminal attributes: %s\n", argv[0], strerror(errno));
		return 1;
	}
	
	speed = termspeed(cfgetispeed(&tios));
	
	if(safewrite(sock, "", 1) <= 0 || 
	   safewrite(sock, luser, strlen(luser) + 1) <= 0 ||
	   safewrite(sock, user, strlen(user) + 1) <= 0 ||
	   safewrite(sock, term, strlen(term)) <= 0 ||
	   safewrite(sock, "/", 1) <= 0 ||
	   safewrite(sock, speed, strlen(speed) + 1) <= 0) {
		fprintf(stderr, "%s: Unable to send required information: %s\n", argv[0], strerror(errno));
		return 1;
	}

	/* Wait for acknowledgement from server */
	
	errno = 0;
	
	if(read(sock, buf, 1) != 1 || *buf) {
		fprintf(stderr, "%s: Didn't receive NULL byte from server: %s\n", argv[0], strerror(errno));
		return 1;
	}

	/* Set up terminal on the client */
	
	oldtios = tios;
	tios.c_oflag &= ~(ONLCR|OCRNL);
	tios.c_lflag &= ~(ECHO|ICANON|ISIG);
	tios.c_iflag &= ~(ICRNL|ISTRIP|IXON);
	
	/* How much of the stuff below is really needed?
	tios.c_cc[VTIME] = 1;
	tios.c_cc[VMIN] = 1;
	tios.c_cc[VSUSP] = 255;
	tios.c_cc[VEOL] = 255;
	tios.c_cc[VREPRINT] = 255;
	tios.c_cc[VDISCARD] = 255;
	tios.c_cc[VWERASE] = 255;
	tios.c_cc[VLNEXT] = 255;
	tios.c_cc[VEOL2] = 255;
	*/

	tcsetattr(0, TCSADRAIN, &tios);

	/* Receive SIGWINCH notifications through a file descriptor */
	
	if(pipe(winchpipe)) {
		fprintf(stderr, "%s: pipe() failed: %s\n", argv[0], strerror(errno));
		return 1;
	}
	
	if(signal(SIGWINCH, sigwinch_h) == SIG_ERR) {
		fprintf(stderr, "%s: signal() failed: %s\n", argv[0], strerror(errno));
		return 1;
	}
	
	if(write(winchpipe[1], "", 1) <= 0){
		fprintf(stderr, "%s: write() failed: %s\n", argv[0], strerror(errno));
		return 1;
	}
					
	/* Process input/output */
	
	pfd[0].fd = 0;
	pfd[0].events = POLLIN | POLLERR | POLLHUP;
	pfd[1].fd = sock;
	pfd[1].events = POLLIN | POLLERR | POLLHUP;
	pfd[2].fd = winchpipe[0];
	pfd[2].events = POLLIN | POLLERR | POLLHUP;

	for(;;) {
		errno = 0;
		
		if(poll(pfd, 3, -1) == -1) {
			if(errno == EINTR)
				continue;
			break;
		}

		if(pfd[0].revents) {
			len = read(0, buf, sizeof(buf));
			if(len <= 0)
				break;
			if(safewrite(sock, buf, len) <= 0)
				break;
			pfd[0].revents = 0;
		}

		if(pfd[1].revents) {
			len = read(sock, buf, sizeof(buf));
			if(len <= 0)
				break;
			if(safewrite(1, buf, len) <= 0)
				break;
			pfd[1].revents = 0;
		}

		/* If we got a SIGWINCH, send new window size to server */
		
		if(pfd[2].revents) {
			len = read(winchpipe[0], buf, sizeof(buf));
			if(len <= 0)
				break;
			
			buf[0] = buf[1] = 0xFF;
			buf[2] = buf[3] = 's';
			
			ioctl(0, TIOCGWINSZ, &winsize);
			*(int16_t *)(buf + 4) = htons(winsize.ws_row);
			*(int16_t *)(buf + 6) = htons(winsize.ws_col);
			*(int16_t *)(buf + 8) = htons(winsize.ws_xpixel);
			*(int16_t *)(buf + 10) = htons(winsize.ws_ypixel);
			
			if(safewrite(sock, buf, 12) <= 0)
				break;

			pfd[1].revents = 0;
		}
	}

	/* Clean up */

	tcsetattr(0, TCSADRAIN, &oldtios);

	if(errno) {
		fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
		return 1;
	}
	
	return 0;
}
