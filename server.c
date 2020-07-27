#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <getopt.h>
#include "server.h"
#include "hash.h"


int debug = 0;
int terminate = 0;

struct intHash *listeners;
struct intHash *clients;

int createListener(unsigned short port, char *ip) {
	int sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd == -1) {
		fprintf(stderr, "Unable to create listening socket: %s\n", strerror(errno));
		exit(1);
	}

	struct sockaddr_in saddr;
	saddr.sin_family      = AF_INET;
	saddr.sin_port        = htons(port);
	saddr.sin_addr.s_addr = inet_addr(ip);

	int enable = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == -1) {
		fprintf(stderr, "Unable to enable address reusing on %s:%u: %s\n", ip, port, strerror(errno));
	}

	if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
		fprintf(stderr, "Unable to bind to %s:%u: %s\n", ip, port, strerror(errno));
		exit(1);
	}

	if (listen(sockfd, 1000) == -1) {
		fprintf(stderr, "Unable to listen: %s\n", strerror(errno));
		exit(1);
	}

	return sockfd;
}

void registerEpollListeners(int ep, char *ip, unsigned short portStart, unsigned short portEnd) {

	for (unsigned short i = portStart; i <= portEnd; i++) {
		int listener = createListener(i, ip);

		struct socketInfo *sInfo = (struct socketInfo *)malloc(sizeof(struct socketInfo));
		sInfo->sockfd     = listener;
		sInfo->type       = LISTENER;
		time(&sInfo->lastAction);

		sInfo->data.listener                      = (struct listenerInfo *)malloc(sizeof(struct listenerInfo));
		sInfo->data.listener->listenIp            = strdup(ip);
		sInfo->data.listener->listenPort          = i;
		sInfo->data.listener->acceptedConnections = 0;

		hashAdd(listeners, sInfo->sockfd, sInfo);

		struct epoll_event event;
		event.data.ptr = sInfo;
		event.events   = EPOLLIN; 

		if (epoll_ctl(ep, EPOLL_CTL_ADD, listener, &event) == -1) {
			fprintf(stderr, "Unable to add listener %s:%u to epoll: %s\n", ip, i, strerror(errno));
			exit(2);
		}

		if (debug) { fprintf(stderr, "New listener %s:%u registered\n",
			sInfo->data.listener->listenIp, sInfo->data.listener->listenPort); }
	}

}

void handleEpoll(int ep) {
	struct epoll_event events[32768];

	for (;;) {
		if (terminate) { break; }

		int nfds = epoll_wait(ep, events, 32768, 250);
		if (nfds == -1) {
			fprintf(stderr, "Cannot wait for epoll events: %s\n", strerror(errno));
			exit(3);
		}

		for (int i = 0; i < nfds; i++) {
			struct socketInfo *sInfo = (struct socketInfo *)events[i].data.ptr;

			if (sInfo->type == LISTENER) {
				if ((events[i].events & EPOLLIN) == EPOLLIN) {
					handleNewClient(ep, sInfo);
				} else {
					fprintf(stderr, "Unknown event from listener socket: 0x%x\n", events[i].events);
					exit(3);
				}

			} else if (sInfo->type == CLIENT) {
				if ((events[i].events & EPOLLIN) == EPOLLIN) {
					handleDataFromClient(ep, sInfo);
				} else {
					fprintf(stderr, "Unknown event from client socket: 0x%x\n", events[i].events);
					exit(3);
				}

			} else {
				fprintf(stderr, "Unknown socket type: %i\n", sInfo->type);
				exit(3);
			}
		}
	}
}

void handleNewClient(int ep, struct socketInfo *sInfo) {
	struct sockaddr_in caddr;
	socklen_t caddrLen = sizeof(caddr);

	int csock = accept(sInfo->sockfd, (struct sockaddr *)&caddr, &caddrLen);
	if (csock == -1) {
		fprintf(stderr, "Unable to accept new connection on %s:%d: %s\n",
			sInfo->data.listener->listenIp, sInfo->data.listener->listenPort, strerror(errno));
		exit(3);
	}

	sInfo->data.listener->acceptedConnections++;
	time(&sInfo->lastAction);

	struct sockaddr_in laddr;
	socklen_t lAddrLen = sizeof(laddr);
	if (getsockname(csock, (struct sockaddr *)&laddr, &lAddrLen) == -1) {
		fprintf(stderr, "Unable to get local socket info: %s\n", strerror(errno));
		exit(3);
	}

	struct socketInfo *cInfo = (struct socketInfo *)malloc(sizeof(struct socketInfo));
	cInfo->sockfd      = csock;
	cInfo->type        = CLIENT;
	time(&cInfo->lastAction);
	cInfo->data.client              = (struct clientInfo *)malloc(sizeof(struct clientInfo));
	cInfo->data.client->localIp     = strdup(inet_ntoa(laddr.sin_addr));
	cInfo->data.client->localPort   = ntohs(laddr.sin_port);
	cInfo->data.client->remoteIp    = strdup(inet_ntoa(caddr.sin_addr));
	cInfo->data.client->remotePort  = ntohs(caddr.sin_port);

	struct epoll_event cEvent;
	cEvent.data.ptr = cInfo;
	cEvent.events   = EPOLLIN;

	if (epoll_ctl(ep, EPOLL_CTL_ADD, csock, &cEvent) == -1) {
		fprintf(stderr, "Unable to add client %s:%u->%s:%u to epoll: %s\n",
			cInfo->data.client->remoteIp, cInfo->data.client->remotePort,
			cInfo->data.client->localIp,  cInfo->data.client->localPort,
			strerror(errno));
		exit(3);
	}

	hashAdd(clients, cInfo->sockfd, cInfo);
	if (debug) fprintf(stderr, "New client %s:%u->%s:%u registered\n",
			cInfo->data.client->remoteIp, cInfo->data.client->remotePort,
			cInfo->data.client->localIp,  cInfo->data.client->localPort);

}

void handleDataFromClient(int ep, struct socketInfo *sInfo) {
	char buffer[1024];
	int  r;

	r = recv(sInfo->sockfd, buffer, 1024-1, 0);
	if (r == -1) {
		fprintf(stderr, "Unable to receive data from client %s:%d->%s:%d: %s\n",
			sInfo->data.client->remoteIp, sInfo->data.client->remotePort,
			sInfo->data.client->localIp,  sInfo->data.client->localPort,
			strerror(errno));

		if (epoll_ctl(ep, EPOLL_CTL_DEL, sInfo->sockfd, NULL) == -1) {
			fprintf(stderr, "Unable to remove client %s:%u->%s:%u from epoll: %s\n",
				sInfo->data.client->remoteIp, sInfo->data.client->remotePort,
				sInfo->data.client->localIp, sInfo->data.client->localPort,
				strerror(errno));
			exit(4);
		}

		hashDelete(clients, sInfo->sockfd);

		shutdown(sInfo->sockfd, SHUT_RDWR);
		close(sInfo->sockfd);

		free(sInfo->data.client->localIp);
		free(sInfo->data.client->remoteIp);
		free(sInfo->data.client);
		free(sInfo);
		return;
	}

	time(&sInfo->lastAction);

	if (r == 0) {
		if (debug) fprintf(stderr, "Client %s:%d->%s:%d closed connection\n",
			sInfo->data.client->remoteIp, sInfo->data.client->remotePort,
			sInfo->data.client->localIp, sInfo->data.client->localPort);

		if (epoll_ctl(ep, EPOLL_CTL_DEL, sInfo->sockfd, NULL) == -1) {
			fprintf(stderr, "Unable to remove client %s:%u->%s:%u from epoll: %s\n",
				sInfo->data.client->remoteIp, sInfo->data.client->remotePort,
				sInfo->data.client->localIp, sInfo->data.client->localPort,
				strerror(errno));
			exit(4);
		}

		hashDelete(clients, sInfo->sockfd);

		shutdown(sInfo->sockfd, SHUT_RDWR);
		close(sInfo->sockfd);

		free(sInfo->data.client->localIp);
		free(sInfo->data.client->remoteIp);
		free(sInfo->data.client);
		free(sInfo);
		return;
	}

	// send the same data back
	buffer[r] = 0;
	if (debug) fprintf(stderr, "Client %s:%d->%s:%d data: %s\n",
		sInfo->data.client->remoteIp, sInfo->data.client->remotePort,
		sInfo->data.client->localIp, sInfo->data.client->localPort,
		buffer);

	if (send(sInfo->sockfd, buffer, r, 0) == -1) {
		fprintf(stderr, "Unable to send data to client %s:%d->%s:%d: %s\n",
			sInfo->data.client->remoteIp, sInfo->data.client->remotePort,
			sInfo->data.client->localIp, sInfo->data.client->localPort,
			strerror(errno));
		exit(4);
	}
}
	
void closeAllClients(int ep) {
		unsigned int allCount;
		struct socketInfo **allInfos = (struct socketInfo **)hashGetAll(clients, &allCount);

		for (unsigned int i = 0; i < allCount; i++) {
			if (debug) fprintf(stderr, "Closing client %s:%d->%s:%d\n",
				allInfos[i]->data.client->remoteIp, allInfos[i]->data.client->remotePort,
				allInfos[i]->data.client->localIp, allInfos[i]->data.client->localPort);

			if (epoll_ctl(ep, EPOLL_CTL_DEL, allInfos[i]->sockfd, NULL) == -1) {
				fprintf(stderr, "Unable to remove client %s:%u->%s:%u from epoll: %s\n",
					allInfos[i]->data.client->remoteIp, allInfos[i]->data.client->remotePort,
					allInfos[i]->data.client->localIp, allInfos[i]->data.client->localPort,
					strerror(errno));
				exit(6);
			}

			hashDelete(clients, allInfos[i]->sockfd);
			shutdown(allInfos[i]->sockfd, SHUT_RDWR);
			close(allInfos[i]->sockfd);
		}
		free(allInfos);
}

void closeAllListeners(int ep) {
		unsigned int allCount;
		struct socketInfo **allInfos = (struct socketInfo **)hashGetAll(listeners, &allCount);

		for (unsigned int i = 0; i < allCount; i++) {
			if (debug) fprintf(stderr, "Closing listener %s:%d\n",
					allInfos[i]->data.listener->listenIp, allInfos[i]->data.listener->listenPort);

			if (epoll_ctl(ep, EPOLL_CTL_DEL, allInfos[i]->sockfd, NULL) == -1) {
				fprintf(stderr, "Unable to remove listners %s:%u from epoll: %s\n",
					allInfos[i]->data.listener->listenIp, allInfos[i]->data.listener->listenPort,
					strerror(errno));
				exit(7);
			}

			hashDelete(listeners, allInfos[i]->sockfd);
			close(allInfos[i]->sockfd);
		}
		free(allInfos);
}

void *worker(void *param) {
	int ep = *(int*)param;
	handleEpoll(ep);
	closeAllListeners(ep);
	closeAllClients(ep);
}

int main(int argc, char *argv[]) {

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
	
	if (getrlimit(RLIMIT_NOFILE, &limits) == -1) {
		fprintf(stderr, "Unable to get descriptors limit: %s\n", strerror(errno));
	}

	//
	listeners = hashInit(1000);
	unsigned int clientHashSize = 10000;
	int ep = epoll_create(100000);

	while (1) {
		static struct option long_options[] = {
			{"listen",     required_argument, 0,  'l' },
			{"hashsize",   required_argument, 0,  's' },
			{"debug",      no_argument,       0,  'd' },
			{"help",       no_argument,       0,  'h' },
		};

		int c = getopt_long(argc, argv, "l:s:dph", long_options, NULL);
		if (c == -1) break;
		
		switch (c) {
		case 'l':;
			// first split by colon
			char *colon = strchr(optarg, ':');
			if (colon == NULL) {
				fprintf(stderr, "Error: option --listen parameter must be in the format of \"ip:port\" or \"ip:port-port\"!\n");
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

			if (debug) fprintf(stderr, "Preparing listener on %s with listening ports %d-%d\n", lAddr, lPortStart, lPortEnd);
			registerEpollListeners(ep, lAddr, lPortStart, lPortEnd);
			break;

		case 's':
			clientHashSize = atoi(optarg);
			break;

		case 'd':
			debug = 1;
			break;

		case 'h':
			fprintf(stderr, "Usage: %s [-dh] --listen ip:port[-port]\n");
			fprintf(stderr, "       -h         ... show this help\n");
			fprintf(stderr, "       -d         ... enable debug outputs\n");
			fprintf(stderr, "       --listen   ... listen on IP address and port\n");
			fprintf(stderr, "                      optionally port range can be specified\n");
			fprintf(stderr, "                      this option can appear multiple times\n");
			fprintf(stderr, "       --hashsize ... size of primary hashing table for clients\n");
			return 0;
			break;
		}
	}

	if (hashGetActive(listeners) == 0) {
		fprintf(stderr, "No listeners specified.\nUse --listen parameter to specify at least one or -h for more information.\n");
		exit(1);
	}

	clients   = hashInit(clientHashSize);

	pthread_t workerThread;
	if (pthread_create(&workerThread, NULL, worker, &ep) == -1) {
		fprintf(stderr, "Unable to start worker thread: %s\n", strerror(errno));
		exit(5);
	}

	char last[1024];
	bzero(last, 1024);

	for (;;) {
		fprintf(stdout, "responder $ ");

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
			fprintf(stdout, "Connected clients        : %u\n", hashGetActive(clients));
			fprintf(stdout, "Active listeners         : %u\n", hashGetActive(listeners));
			fprintf(stdout, "Clients hash size        : %u\n", clientHashSize);
			fprintf(stdout, "Clients hash max depth   : %u\n", hashGetDepth(clients));
			fprintf(stdout, "Listeners hash max depth : %u\n", hashGetDepth(listeners));
			fprintf(stdout, "Max descriptors limit    : %u/%u\n", limits.rlim_cur, limits.rlim_max);
			if (debug) 
				fprintf(stdout, "Debug output             : yes\n");
			else
				fprintf(stdout, "Debug output             : no\n");

		} else if (strcmp(line, "list") == 0) {
			unsigned int allCount;
			struct socketInfo **allInfos = (struct socketInfo **)hashGetAll(clients, &allCount);
			time_t now;
			time(&now);

			fprintf(stdout, "Connected clients according to internal hash structure: %u\n", allCount);
			for (unsigned int i = 0; i < allCount; i++) {
				fprintf(stdout, "%s:%d->%s:%d : last action %u seconds ago\n",
					allInfos[i]->data.client->remoteIp, allInfos[i]->data.client->remotePort,
					allInfos[i]->data.client->localIp, allInfos[i]->data.client->localPort,
					now-allInfos[i]->lastAction);
			}
			free(allInfos);

		} else if (strcmp(line, "listeners") == 0) {
			unsigned int allCount;
			struct socketInfo **allInfos = (struct socketInfo **)hashGetAll(listeners, &allCount);

			fprintf(stdout, "Active listeners according to internal hash structure: %u\n", allCount);
			for (unsigned int i = 0; i < allCount; i++) {
				fprintf(stdout, "%s:%d : %u accepted connections\n",
					allInfos[i]->data.listener->listenIp, allInfos[i]->data.listener->listenPort,
					allInfos[i]->data.listener->acceptedConnections);
			}
			free(allInfos);

		} else if (strcmp(line, "debug") == 0) {
			if (debug == 0) {
				debug = 1;
				fprintf(stdout, "Debug output enabed\n");
			} else {
				debug = 0;
				fprintf(stdout, "Debug output disabled\n");
			}

		} else if (strcmp(line, "quit") == 0) {
			terminate = 1;
			pthread_join(workerThread, NULL);
			break;

		} else {
			fprintf(stdout, "Unknown command \"%s\", valid commands are:\n", line);
			fprintf(stdout, "- status          ... show number of currently connected clients\n");
			fprintf(stdout, "- list            ... list IP addresses and ports of all established sessions\n");
			fprintf(stdout, "- listeners       ... show information about active listeners\n");
			fprintf(stdout, "- debug           ... toggle the debug log\n");
			fprintf(stdout, "- quit            ... close all clients and exit\n");
			continue;
		}

		strcpy(last, line);
	}

	// cleanup memory
	hashDestroy(clients);
	hashDestroy(listeners);
	close(ep);
}
