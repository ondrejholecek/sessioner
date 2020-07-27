CC=cc
LD=-l pthread -g

all: initiator responder

initiator: client.c client.h hash.c hash.h
	${CC} ${LD} -o initiator client.c hash.c

responder: server.c server.h hash.c hash.h
	${CC} ${LD} -o responder server.c hash.c
