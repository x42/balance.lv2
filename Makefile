#!/usr/bin/make -f

# these can be overridden using make variables. e.g.
#   make CFLAGS=-O2
#   make install DESTDIR=$(CURDIR)/debian/balance_lv2 PREFIX=/usr
#
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
PREFIX ?= /usr/local
CFLAGS ?= $(OPTIMIZATIONS) -Wall
LIBDIR ?= lib

###############################################################################

LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2
LOADLIBES=-lm
LV2NAME=balance
LV2GUI=balanceUI
BUNDLE=balance.lv2

override CFLAGS+=-fPIC -std=c99
TX=textures/

IS_OSX=
UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  IS_OSX=yes
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
else
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic
  LIB_EXT=.so
endif

targets=$(LV2NAME)$(LIB_EXT)

# check for build-dependencies
ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
else
  override CFLAGS+=`pkg-config --cflags lv2`
endif

# optional UI
ifeq ($(IS_OSX), yes)
  FONTFILE?=/usr/X11/lib/X11/fonts/TTF/VeraBd.ttf
  FONTSIZE?=36
else
  FONTFILE?=/usr/share/fonts/truetype/freefont/FreeSansBold.ttf
  FONTSIZE?=40
endif


ifeq ($(shell test -f $(FONTFILE) || echo no ), no)
  $(warning "!!")
  $(warning "!! UI font can not be found on this system")
  $(warning "!! install fonts-freefont-ttf or set the FONTFILE variable to a ttf file")
  $(warning "!! LV2 GUI will not be built")
  $(warning "!!")
  FONT_FOUND=no
else
  FONT_FOUND=yes
endif

ifeq ($(IS_OSX), yes)
  HAVE_UI=$(shell pkg-config --exists ftgl && echo $(FONT_FOUND))
else
  HAVE_UI=$(shell pkg-config --exists glu ftgl && echo $(FONT_FOUND))
endif

LV2UIREQ=
# check for LV2 idle thread -- requires 'lv2', atleast_version='1.4.1
ifeq ($(shell pkg-config --atleast-version=1.4.2 lv2 || echo no), no)
  UICFLAGS+=-DOLD_SUIL
else
  LV2UIREQ=lv2:requiredFeature ui:idle;\\n\\tlv2:extensionData ui:idle;
endif

ifeq ($(HAVE_UI), yes)
  UIDEPS=pugl/pugl.h pugl/pugl_internal.h ui_model.h
  UIDEPS+=$(TX)dial.c $(TX)background.c
  UIDEPS+=$(TX)mm_lr.c $(TX)mm_rl.c $(TX)mm_ll.c $(TX)mm_rr.c $(TX)mm_mono.c
  UIDEPS+=$(TX)btn_inv.c $(TX)btn_link.c
  ifeq ($(IS_OSX), yes)
    UIDEPS+=pugl/pugl_osx.m
    UILIBS=pugl/pugl_osx.m -framework Cocoa -framework OpenGL
    UI_TYPE=CocoaUI
  else
    UIDEPS+=pugl/pugl_x11.c
    UICFLAGS+=`pkg-config --cflags glu`
    UILIBS=pugl/pugl_x11.c -lX11 `pkg-config --libs glu`
    UI_TYPE=X11UI
  endif
  override CFLAGS+=`pkg-config --cflags ftgl`
  UILIBS+=`pkg-config --libs ftgl`
  override CFLAGS+=-DFONTFILE=\"$(FONTFILE)\"
  override CFLAGS+=-DFONTSIZE=$(FONTSIZE)
  targets+=$(LV2GUI)$(LIB_EXT)
else
  $(warning "!!")
  $(warning "!! openGL/GLU is not available - GUI disabled")
  $(warning "!! install glu-dev, ftgl-dev and fonts-freefont-ttf to build LV2 GUI")
  $(warning "!!")
endif


# build target definitions
default: all

all: manifest.ttl $(LV2NAME).ttl $(targets)

manifest.ttl: manifest.ttl.in manifest.ui.ttl.in
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" \
	  manifest.ttl.in > manifest.ttl
ifeq ($(HAVE_UI), yes)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LV2GUI@/$(LV2GUI)/;s/@LIB_EXT@/$(LIB_EXT)/;s/@UI_TYPE@/$(UI_TYPE)/" manifest.ui.ttl.in >> manifest.ttl
endif

$(LV2NAME).ttl: $(LV2NAME).ttl.in $(LV2NAME).ui.ttl.in
	cat $(LV2NAME).ttl.in > $(LV2NAME).ttl
ifeq ($(HAVE_UI), yes)
	sed "s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/;" $(LV2NAME).ui.ttl.in >> $(LV2NAME).ttl
endif

$(LV2NAME)$(LIB_EXT): balance.c uris.h
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	  -o $(LV2NAME)$(LIB_EXT) balance.c \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)

$(LV2GUI)$(LIB_EXT): ui.c uris.h $(UIDEPS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UICFLAGS) \
		-o $(LV2GUI)$(LIB_EXT) ui.c \
		-shared $(LV2LDFLAGS) $(LDFLAGS) $(UILIBS)

# install/uninstall/clean target definitions

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(LV2NAME)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 manifest.ttl $(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)
ifeq ($(HAVE_UI), yes)
	install -m755 $(LV2GUI)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
endif

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GUI)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

clean:
	rm -f manifest.ttl balance.ttl $(LV2NAME)$(LIB_EXT) $(LV2GUI)$(LIB_EXT)

.PHONY: clean all install uninstall
