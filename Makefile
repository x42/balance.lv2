#!/usr/bin/make -f

# these can be overrideen using make variables. e.g.
#   make CFLAGS=-O2
#   make install DESTDIR=$(CURDIR)/debian/balance_lv2 PREFIX=/usr
#
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
PREFIX ?= /usr/local
CFLAGS ?= $(OPTIMIZATIONS) -Wall
LIBDIR ?= lib

###############################################################################
LIB_EXT=.so

LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2
LOADLIBES=-lm
LV2NAME=balance
BUNDLE=balance.lv2
CFLAGS+=-fPIC

# check for build-dependencies
ifeq ($(shell pkg-config --exists lv2 lv2core || echo no), no)
  $(error "LV2 SDK was not found")
else
  CFLAGS+=`pkg-config --cflags lv2 lv2core`
endif

# build target definitions
default: all

all: manifest.ttl balance.ttl $(LV2NAME)$(LIB_EXT)

manifest.ttl: manifest.ttl.in
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" \
	  manifest.ttl.in > manifest.ttl

$(LV2NAME)$(LIB_EXT): balance.c
	$(CC) $(CFLAGS) \
	  -o $(LV2NAME)$(LIB_EXT) balance.c \
	  $(LDFLAGS) $(LOADLIBES) -shared -Wl,-Bstatic -Wl,-Bdynamic

# install/uninstall/clean target definitions

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(LV2NAME)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 manifest.ttl $(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/*.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

clean:
	rm -f *.o manifest.ttl $(LV2NAME)$(LIB_EXT) $(LV2GUI)$(LIB_EXT)

.PHONY: clean all install uninstall
