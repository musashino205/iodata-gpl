CROSS   = mipsel-linux-gnu
CC	= $(CROSS)gcc
#CC      = $(CC)
#COPTS	= -O2 -g
CFLAGS	= $(COPTS) -I.. -I../../include -fPIC
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS	= -shared
LDADD   = -lutil
INSTALL	= install -o root
prefix  = /usr/local

PLUGINS = pptpd-logwtmp.so

# include dependencies if present
ifeq (.depend,$(wildcard .depend))
include .depend
endif

all:	$(PLUGINS)

%.so: %.c
	$(CC) -o $@ $(LDFLAGS) $(CFLAGS) $^ $(LDADD)

LIBDIR	?= $(DESTDIR)$(prefix)/lib/pptpd

install: $(PLUGINS)
	$(INSTALL) -d $(LIBDIR)
	$(INSTALL) $? $(LIBDIR)

uninstall:
	rm -f $(LIBDIR)$(PLUGINS)

clean:
	rm -f *.o *.so *.a

depend:
	$(CPP) -M $(CFLAGS) *.c >.depend
