PREFIX  ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin
UNITDIR ?= /etc/systemd/system

INSTALL ?= install
RM      ?= rm -f

PROG = mdnsd
OBJS = mdnsd.o

.PHONY: all clean install uninstall

all: $(PROG)
	case `uname -s` in \
	  FreeBSD) \
	    cat $(PROG).rc.in | sed 's:@SBINDIR@:$(SBINDIR):g' > $(PROG).rc;; \
	  Linux) \
	    cat $(PROG).service.in | sed 's:@SBINDIR@:$(SBINDIR):g' > $(PROG).service;; \
	esac

clean:
	$(RM) $(PROG).rc $(PROG).service $(PROG) $(OBJS)

install: all
	$(INSTALL) -m 755 $(PROG) $(SBINDIR)/
	case `uname -s` in \
	  FreeBSD) \
	    install -m 755 $(PROG).rc /usr/local/etc/rc.d/$(PROG);; \
	  Linux) \
	    install -m 644 $(PROG).service $(UNITDIR)/;; \
	esac

uninstall:
	$(RM) $(SBINDIR)/$(PROG)
	case `uname -s` in \
	  FreeBSD) \
	    $(RM) /usr/local/etc/rc.d/$(PROG);; \
	  Linux) \
	    $(RM) $(UNITDIR)/$(PROG).service;; \
	esac

$(PROG): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) -c $< $(CFLAGS)
