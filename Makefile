CC=cc
LD=-l pthread

all: responder

responder: server.c server.h hash.c hash.h
	${CC} ${LD} -o responder server.c hash.c
