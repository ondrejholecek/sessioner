CC=cc
LD=-l pthread

all: sessioner_initiator sessioner_responder

sessioner_initiator: client.c client.h hash.c hash.h
	${CC} ${LD} -o sessioner_initiator client.c hash.c

sessioner_responder: server.c server.h hash.c hash.h
	${CC} ${LD} -o sessioner_responder server.c hash.c

clean:
	dh_clean
	rm -f sessioner_initiator sessioner_responder
	
package:
	dpkg-buildpackage --no-sign
	dh_clean
