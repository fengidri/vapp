SOURCES=main.c common.c
SOURCES+=shm.c fd_list.c stat.c vring.c
SOURCES+=server.c vhost_server.c
SOURCES+=client.c vhost_client.c
SOURCES+=packet.c process.c

HEADERS=common.h
HEADERS+=shm.h fd_list.h stat.h vring.h
HEADERS+=server.h vhost_server.h
HEADERS+=client.h vhost_client.h
HEADERS+=packet.h

BIN=vhost
CFLAGS += -Wall -Werror
CFLAGS += -ggdb3 -O2 -Wno-address-of-packed-member -Wno-strict-aliasing
LFLAGS = -lrt -l pthread

all: ${BIN}

${BIN}: ${SOURCES} ${HEADERS}
		${CC} ${CFLAGS} ${SOURCES} -o $@ ${LFLAGS}

clean:
		rm -rf ${BIN}

