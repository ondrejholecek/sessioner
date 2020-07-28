#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/epoll.h>
#include "hash.h"
#include "client.h"

int terminate = 0;
int debug     = 0;
int ping      = 0;

unsigned int hashSize = 10000;

struct direction *directionInit(char *remoteIp, unsigned short remotePort, unsigned int maxConnections) {
	struct direction *dir = (struct direction *)malloc(sizeof(struct direction));
	dir->remoteIp       = strdup(remoteIp);
	dir->remotePort     = remotePort;
	dir->maxConnections = maxConnections;
	dir->sessions       = hashInit(hashSize);
	dir->pingTime       = 30;
	dir->pingMax        = 1000;
}

int createConnection(char *ip, unsigned short port) {
	int sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd == -1) {
		fprintf(stderr, "Unable to create socket for %s:%u: %s\n", ip, port, strerror(errno));
		return -1;
	}

	struct sockaddr_in saddr;
	saddr.sin_family      = AF_INET;
	saddr.sin_port        = htons(port);
	saddr.sin_addr.s_addr = inet_addr(ip);

	if (connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
		fprintf(stderr, "Unable to connect to %s:%u: %s\n", ip, port, strerror(errno));
		return -1;
	}

	return sockfd;
}

void directionCheckup(int ep, struct direction *dir) {
	unsigned int currentConnections = hashGetActive(dir->sessions);
	unsigned int create = 0;
	unsigned int delete = 0;

	if (currentConnections > dir->maxConnections) {
		delete = currentConnections - dir->maxConnections;
	} else if (currentConnections < dir->maxConnections) {
		create = dir->maxConnections - currentConnections;
	}

	if (debug) fprintf(stderr, "Connections - current: %u, required: %u, delete: %u, create: %u\n",
		currentConnections, dir->maxConnections, delete, create);

	// first delete
	unsigned int allCount;
	struct connection **allCons = (struct connection **)hashGetAll(dir->sessions, &allCount);

	if (allCount < delete) {
		fprintf(stderr, "Want to delete %u sessions but there are only %u in direction hash table",
			delete, allCount);
		delete = allCount;
	}

	unsigned int deleted = 0;
	for (unsigned int i = 0; i < delete; i++) {
		if (debug) fprintf(stderr, "Deleting connection %s:%u->%s:%u\n",
			allCons[i]->localIp, allCons[i]->localPort, allCons[i]->remoteIp, allCons[i]->remotePort);

		closeConnection(ep, allCons[i]);
		deleted++;
	}

	free(allCons);

	// then create new ones
	unsigned int created = 0;
	for (unsigned int i = 0; i < create; i++) {
		int sockfd = createConnection(dir->remoteIp, dir->remotePort);
		if (sockfd == -1) continue;

		// get some information about this socket 
		struct sockaddr_in laddr, raddr;
		socklen_t addrLen;

		addrLen = sizeof(laddr);
		if (getsockname(sockfd, (struct sockaddr *)&laddr, &addrLen) == -1) {
			fprintf(stderr, "Unable to get local socket information for direction %s:%u: %s",
				dir->remoteIp, dir->remotePort, strerror(errno));
			exit(3);
		}

		addrLen = sizeof(raddr);
		if (getpeername(sockfd, (struct sockaddr *)&raddr, &addrLen) == -1) {
			fprintf(stderr, "Unable to get remote socket information for direction %s:%u: %s",
				dir->remoteIp, dir->remotePort, strerror(errno));
			exit(3);
		}

		// and save them
		struct connection *con = (struct connection *)malloc(sizeof(struct connection));
		con->sockfd       = sockfd;
		con->direction    = dir;
		con->localIp      = strdup(inet_ntoa(laddr.sin_addr));
		con->localPort    = ntohs(laddr.sin_port);
		con->remoteIp     = strdup(inet_ntoa(raddr.sin_addr));
		con->remotePort   = ntohs(raddr.sin_port);
		time(&con->connected);
		con->lastSent     = con->connected;
		con->lastReceived = con->connected;
		con->pingRandom   = rand() % dir->pingTime;
		hashAdd(dir->sessions, con->sockfd, con);

		// add to epoll
		struct epoll_event event;
		event.data.ptr = con;
		event.events   = EPOLLIN; 

		if (epoll_ctl(ep, EPOLL_CTL_ADD, con->sockfd, &event) == -1) {
			fprintf(stderr, "Unable to add client %s:%u->%s:%u to epoll: %s\n",
				con->localIp, con->localPort, con->remoteIp, con->remotePort,
				strerror(errno));
			exit(3);
		}

		created++;
		if (debug) fprintf(stderr, "Created connection %s:%u->%s:%u\n",
			con->localIp, con->localPort, con->remoteIp, con->remotePort);
	}

	if (debug) fprintf(stderr, "For direction %s:%u in this cycle deleted %u and created %u connections\n",
		dir->remoteIp, dir->remotePort, deleted, created);
}

void closeConnection(int ep, struct connection *con) {
	if (epoll_ctl(ep, EPOLL_CTL_DEL, con->sockfd, NULL) == -1) {
		fprintf(stderr, "Unable to remove client %s:%u->%s:%u from epoll: %s\n",
			con->localIp, con->localPort, con->remoteIp, con->remotePort,
			strerror(errno));
		exit(4);
	}

	shutdown(con->sockfd, SHUT_RDWR);
	close(con->sockfd);

	hashDelete(con->direction->sessions, con->sockfd);

	free(con->localIp);
	free(con->remoteIp);
	free(con);
}

void closeAllConnections(int ep, struct direction *dir) {
		unsigned int allCount;
		struct connection **allCons = (struct connection **)hashGetAll(dir->sessions, &allCount);

		for (unsigned int i = 0; i < allCount; i++) {
			if (debug) fprintf(stderr, "Closing connection %s:%d->%s:%d\n",
				allCons[i]->localIp, allCons[i]->localPort,
				allCons[i]->remoteIp, allCons[i]->remotePort);

			closeConnection(ep, allCons[i]);
		}

		free(allCons);
}


void directionPing(int ep, struct direction *dir) {
	unsigned int allCount;
	struct connection **allCons = (struct connection **)hashGetAll(dir->sessions, &allCount);
	unsigned int count = 0;

	time_t now;
	time(&now);

	char ping[1024];
	snprintf(ping, 1024-1, "PING");

	for (unsigned int i = 0; i < allCount; i++) {
		if ((now-allCons[i]->lastSent) > (dir->pingTime + allCons[i]->pingRandom)) {

			if (send(allCons[i]->sockfd, ping, strlen(ping), 0) != strlen(ping)) {
				fprintf(stderr, "Cannot ping client %s:%u->%s:%u: %s\n",
					allCons[i]->localIp, allCons[i]->localPort, allCons[i]->remoteIp, allCons[i]->remotePort,
					strerror(errno));
			} else {
				if (debug) fprintf(stderr, "Pinged client %s:%u->%s:%u\n",
					allCons[i]->localIp, allCons[i]->localPort, allCons[i]->remoteIp, allCons[i]->remotePort);

				time(&allCons[i]->lastSent);
				count++;

				// do not spend to much CPU time with ping in one cycle - finish next time
				if (count > dir->pingMax) break;
			}
		}
	}

	free(allCons);

	if (debug) fprintf(stderr, "For direction %s:%u in this cycle pinged %u connections\n",
		dir->remoteIp, dir->remotePort, count);
}

void handleEpoll(int ep) {
	struct epoll_event events[32768];

	for (;;) {
		if (terminate) { break; }

		int nfds = epoll_wait(ep, events, 32768, 250);
		if (nfds == -1) {
			fprintf(stderr, "Cannot wait for epoll events: %s\n", strerror(errno));
			exit(4);
		}
	
		for (int i = 0; i < nfds; i++) {
			struct connection *con = (struct connection *)events[i].data.ptr;
			char buffer[1024];
	
			if ((events[i].events & EPOLLIN) == EPOLLIN) {
				int r = recv(con->sockfd, buffer, 1024-1, 0);
				if (r == -1) {
					fprintf(stderr, "Error on connection %s:%u->%s:%u: %s\n",
						con->localIp, con->localPort, con->remoteIp, con->remotePort,
						strerror(errno));
	
					closeConnection(ep, con);
					continue;
	
				} else if (r == 0) {
					if (debug) fprintf(stderr, "Connection %s:%u->%s:%u closed by remote side\n",
						con->localIp, con->localPort, con->remoteIp, con->remotePort);
	
					closeConnection(ep, con);
					continue;
				}
	
				// we have got some data
				buffer[r] = 0;
				time(&con->lastReceived);
				if (debug) fprintf(stderr, "Incoming data from %s:%u->%s:%u: %s\n",
						con->localIp, con->localPort, con->remoteIp, con->remotePort,
						buffer);
	
			} else {
				fprintf(stderr, "Unknown event from client socket: 0x%x\n", events[i].events);
				exit(4);
			}
		}
	}
}

struct workerParam {
	int               ep;
	struct direction  **directions;
	unsigned int      directionCount;
};

void *worker1(void *param) {
	struct workerParam *wp = (struct workerParam *)param;
	while (!terminate) {
		for (int i = 0; i < wp->directionCount; i++) {
			directionCheckup(wp->ep, wp->directions[i]);
			if (ping) directionPing(wp->ep, wp->directions[i]);
		}	
		sleep(1);
	}
}

void *worker2(void *param) {
	struct workerParam *wp = (struct workerParam *)param;
	handleEpoll(wp->ep);
}

int main(int argc, char *argv[]) {
	//
	struct workerParam wp;
	wp.ep              = epoll_create(100000);
	wp.directions      = NULL;
	wp.directionCount  = 0;

	int ulimit_fd = 1;

	while (1) {
		static struct option long_options[] = {
			{"target",     required_argument, 0,  't' },
			{"hashsize",   required_argument, 0,  's' },
			{"debug",      no_argument,       0,  'd' },
			{"ping",       no_argument,       0,  'p' },
			{"help",       no_argument,       0,  'h' },
			{"no-ulimit",  no_argument,       0,  'u' },
		};

		int c = getopt_long(argc, argv, "t:s:dphu", long_options, NULL);
		if (c == -1) break;
		
		switch (c) {
		case 't':;
			// first split by colon
			char *colon = strchr(optarg, ':');
			if (colon == NULL) {
				fprintf(stderr, "Error: option --connect parameter must be in the format of \"ip:port\" or \"ip:port-port\"!\n");
				exit(1);
			}

			char *lAddr = strndup(optarg, colon-optarg);
			unsigned short lPortStart, lPortEnd;

			// check if it is a range
			char *dash = strchr(colon+1, '-');
			if (dash == NULL) {
				lPortStart = atoi(colon+1);
				lPortEnd   = lPortStart;
			} else {
				lPortEnd   = atoi(dash+1);
				dash[0] = 0;
				lPortStart = atoi(colon+1);
			}

			if (lPortStart > lPortEnd) {
				fprintf(stderr, "Error: end port must not be lower than start port!\n");
				exit(1);
			}

			if (debug) fprintf(stderr, "Preparing direction to %s with remote ports %d-%d\n", lAddr, lPortStart, lPortEnd);
			for (unsigned short port = lPortStart; port <= lPortEnd; port++) {
				wp.directions = realloc(wp.directions, sizeof(struct direction **)*(wp.directionCount+1));
				wp.directions[wp.directionCount] = directionInit(lAddr, port, 1);
				wp.directionCount++;
			}
			free(lAddr);

			break;
		case 's':
			hashSize = atoi(optarg);
			break;

		case 'd':
			debug = 1;
			break;

		case 'u':
			ulimit_fd = 0;
			break;

		case 'p':
			ping = 1;
			break;

		case 'h':
			fprintf(stderr, "Usage: %s [-dph] --target ip:port[-port] [--hashsize NUM]\n");
			fprintf(stderr, "       -h         ... show this help\n");
			fprintf(stderr, "       -d         ... enable debug outputs\n");
			fprintf(stderr, "       -p         ... ping through connections periodically\n");
			fprintf(stderr, "       -u         ... do not try to change resource limits on file descriptors\n");
			fprintf(stderr, "       --target   ... connect on IP address and port\n");
			fprintf(stderr, "                      optionally port range can be specified\n");
			fprintf(stderr, "                      this option can appear multiple times\n");
			fprintf(stderr, "       --hashsize ... size of primary hashing table for following targets\n");
			return 0;
			break;
		}
	}

	if (wp.directionCount == 0) {
		fprintf(stderr, "No remote ends specified.\nUse --target parameter to specify at least one or -h for more information.\n");
		exit(1);
	}

	if (ulimit_fd) {
		// disable fd limits
		FILE *nropen = fopen("/proc/sys/fs/nr_open", "r");
		if (nropen == NULL) {
			fprintf(stderr, "Unable to disable descriptors limit: %s\n", strerror(errno));
		}
	
		unsigned int maxfiles;
		if (fscanf(nropen, "%u", &maxfiles) != 1) {
			fprintf(stderr, "Unable to disable descriptors limit: /proc/sys/fs/nr_open format error\n");
		}
		fclose(nropen);
	
		struct rlimit limits;
		limits.rlim_cur = maxfiles;
		limits.rlim_max = maxfiles;
	
		if (setrlimit(RLIMIT_NOFILE, &limits) == -1) {
			fprintf(stderr, "Unable to disable descriptors limit: %s\n", strerror(errno));
		}
	}
	
	struct rlimit limits;
	if (getrlimit(RLIMIT_NOFILE, &limits) == -1) {
		fprintf(stderr, "Unable to get descriptors limit: %s\n", strerror(errno));
	}

	pthread_t workerThread1, workerThread2;
	if (pthread_create(&workerThread1, NULL, worker1, &wp) == -1) {
		fprintf(stderr, "Unable to start worker thread1: %s\n", strerror(errno));
		exit(5);
	}
	if (pthread_create(&workerThread2, NULL, worker2, &wp) == -1) {
		fprintf(stderr, "Unable to start worker thread2: %s\n", strerror(errno));
		exit(5);
	}

	char last[1024];
	bzero(last, 1024);

	for (;;) {
		fprintf(stdout, "initiator $ ");

		char line[1024];
		if (fgets(line, 1024, stdin) == NULL) {
			fprintf(stdout, "\n");
			strcpy(line, "quit\n");
		}
		while (line[strlen(line)-1] == '\n') { line[strlen(line)-1] = 0; }

		if (strlen(line) == 0) {
			strcpy(line, last);
		}

		if (strcmp(line, "status") == 0) {
			unsigned int currentConnections = 0;
			unsigned int currentRequest     = 0;
			for (unsigned int i = 0; i < wp.directionCount; i++) {
				currentConnections += hashGetActive(wp.directions[i]->sessions);
				currentRequest     += wp.directions[i]->maxConnections;
			}

			fprintf(stdout, "Connections requested    : %u\n", currentRequest);
			fprintf(stdout, "Connected clients        : %u\n", currentConnections);
			fprintf(stdout, "Configured targets       : %u\n", wp.directionCount);
			fprintf(stdout, "Max descriptors limit    : %u/%u\n", limits.rlim_cur, limits.rlim_max);
			if (ping) 
				fprintf(stdout, "Ping through connections : yes\n");
			else
				fprintf(stdout, "Ping through connections : no\n");
			if (debug) 
				fprintf(stdout, "Debug output             : yes\n");
			else
				fprintf(stdout, "Debug output             : no\n");

		} else if (strcmp(line, "details") == 0) {
			for (unsigned int i = 0; i < wp.directionCount; i++) {
				fprintf(stdout, "Direction to %s:%u:\n", wp.directions[i]->remoteIp, wp.directions[i]->remotePort);
				fprintf(stdout, "   Maximum allowed connections       : %u\n", wp.directions[i]->maxConnections);
				fprintf(stdout, "   Currently established connections : %u\n", hashGetActive(wp.directions[i]->sessions));
				fprintf(stdout, "   Session hash max depth            : %u\n", hashGetDepth(wp.directions[i]->sessions));
			}

		} else if (strncmp(line, "set", 3) == 0) {
				if (strlen(line) <= 5) {	
					fprintf(stdout, "You need to specify the number to requested connections!\n");
				} else {
					unsigned int request = atoi(line+4);
					if (debug) fprintf(stderr, "Setting total number of requested connections to %u\n", request);

					unsigned int requestForOne = request / wp.directionCount;
					for (unsigned int i = 0; i < wp.directionCount; i++) {
						if (i == 0)
							wp.directions[i]->maxConnections = request - ((wp.directionCount-1) * requestForOne);
						else
							wp.directions[i]->maxConnections = requestForOne;
					}
				}

		} else if (strcmp(line, "debug") == 0) {
			if (debug == 0) {
				debug = 1;
				fprintf(stdout, "Debug output enabed\n");
			} else {
				debug = 0;
				fprintf(stdout, "Debug output disabled\n");
			}

		} else if (strcmp(line, "ping") == 0) {
			if (ping == 0) {
				ping = 1;
				fprintf(stdout, "Ping enabed\n");
			} else {
				ping = 0;
				fprintf(stdout, "Ping disabled\n");
			}

		} else if (strcmp(line, "quit") == 0) {
			terminate = 1;
			pthread_join(workerThread1, NULL);
			pthread_join(workerThread2, NULL);
			break;

		} else {
			fprintf(stdout, "Unknown command \"%s\", valid commands are:\n", line);
			fprintf(stdout, "- status          ... show number of currently connected clients\n");
			fprintf(stdout, "- details         ... show more information about each target\n");
			fprintf(stdout, "- set NUM         ... set the number of required connections\n");
			fprintf(stdout, "- debug           ... toggle the debug log\n");
			fprintf(stdout, "- ping            ... toggle the ping through the established connections\n");
			fprintf(stdout, "- quit            ... close all clients and exit\n");
			continue;
		}

		strcpy(last, line);
	}

	for (unsigned int i = 0; i < wp.directionCount; i++) {
		closeAllConnections(wp.ep, wp.directions[i]);
		hashDestroy(wp.directions[i]->sessions);
		free(wp.directions[i]->remoteIp);
		free(wp.directions[i]);
	}
	close(wp.ep);
	free(wp.directions);
}
