TARGET = vbus

OBJS= vbus.o ini.o

CC = gcc
INSTALL = /usr/bin/install

CFLAGS = -c -g
#LDFLAGS = `curl-config --static-libs`

LIBS =

prefix = $(DESTDIR)
bindir = $(prefix)/usr/bin/

$(TARGET) : $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LDFLAGS) $(LIBS)

.PHONY: install
install:
	$(INSTALL) $(TARGET) $(bindir)$(TARGET)

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)
