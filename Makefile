VERSION = 0.1

PREFIX = /usr
INCS = -I. -I/usr/include
LIBS = -L/usr/lib -lc

CC = cc
CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS += -std=c99 -pedantic -Wall ${INCS} ${CPPFLAGS}
LDFLAGS = -s ${LIBS}

SRC = jjd.c
OBJS = ${SRC:.c=.o}

all: jjd

%.o: %.c
	@echo CC $<
	$(CC) -c $(CFLAGS) $<

jjd: $(OBJS)
	@echo CC -o $@
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

clean:
	@echo cleaning
	rm -f *.o jjd

install: all
	@echo installing executable files to ${DESTDIR}${PREFIX}/bin
	@install -Dm 755 jjd ${DESTDIR}${PREFIX}/bin/jjd
	@install -Dm 755 jjc ${DESTDIR}${PREFIX}/bin/jjc
	@install -Dm 755 jjp ${DESTDIR}${PREFIX}/bin/jjp

uninstall:
	@echo removing executable files from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/jjd
	@rm -f ${DESTDIR}${PREFIX}/bin/jjc
	@rm -f ${DESTDIR}${PREFIX}/bin/jjp


.PHONY: all clean install uninstall
