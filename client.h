#ifndef __CLIENT_H__
#define __CLIENT_H__

struct direction {
	char            *remoteIp;
	unsigned short  remotePort;
	unsigned int    maxConnections;
	struct intHash  *sessions;
	unsigned int    pingTime;
	unsigned int    pingMax;
};

struct connection {
	int               sockfd;
	struct direction  *direction;
	char              *localIp;
	unsigned short    localPort;
	char              *remoteIp;
	unsigned short    remotePort;
	time_t            connected;
	time_t            lastSent;
	time_t            lastReceived;
	unsigned int      pingRandom;
};

struct direction *directionInit(char *remoteIp, unsigned short remotePort, unsigned int maxConnections);
int createConnection(char *ip, unsigned short port);
void directionCheckup(int ep, struct direction *dir);
void closeConnection(int ep, struct connection *con);
void handleEpoll(int ep);


#endif
