#ifndef __SERVER_H__
#define __SERVER_H__

enum  socketType{LISTENER, CLIENT};

union socketData {
	struct listenerInfo  *listener;
	struct clientInfo    *client;
};

struct socketInfo {
	int              sockfd;
	enum socketType  type;
	time_t           lastAction;
	union socketData data;
};

struct clientInfo {
	char             *localIp;
	unsigned short   localPort;
	char             *remoteIp;
	unsigned short   remotePort;
};

struct listenerInfo {
	char             *listenIp;
	unsigned short   listenPort;
	unsigned int     acceptedConnections;
};

int createListener(unsigned short port, char *ip);
void registerEpollListeners(int ep, char *ip, unsigned short portStart, unsigned short portEnd);
void handleEpoll(int ep);
void handleNewClient(int ep, struct socketInfo *sInfo);
void handleDataFromClient(int ep, struct socketInfo *sInfo);

#endif
