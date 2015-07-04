#!/usr/bin/make -f

# these can be overridden using make variables. e.g.
#   make CXXFLAGS=-O2
#   make install DESTDIR=$(CURDIR)/debian/balance_lv2 PREFIX=/usr
#
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
PREFIX ?= /usr/local
CXXFLAGS ?= $(OPTIMIZATIONS) -Wall
LIBDIR ?= lib

STRIP=strip
STRIPFLAGS=-s
UISTRIPFLAGS=-s

balance_VERSION?=$(shell git describe --tags HEAD 2>/dev/null | sed 's/-g.*$$//;s/^v//' || echo "LV2")
###############################################################################

LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2
LOADLIBES=-lm
LV2NAME=balance
LV2GUI=balanceUI
BUNDLE=balance.lv2
BUILDDIR=build/
targets=

override CXXFLAGS+=-fPIC
TX=textures/

IS_OSX=
PKG_GL_LIBS=
UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  IS_OSX=yes
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
  EXTENDED_RE=-E
  STRIPFLAGS=-u -r -arch all -s lv2syms
  UISTRIPFLAGS=-u -r -arch all -s lv2uisyms
  targets+=lv2syms lv2uisyms
else
  ifneq ($(XWIN),)
    IS_WIN=yes
    CXX=$(XWIN)-g++
    STRIP=$(XWIN)-strip
    LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed
    LIB_EXT=.dll
    override LDFLAGS += -static-libgcc -static-libstdc++
  else
    LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic
    LIB_EXT=.so
    PKG_GL_LIBS=glu gl
  endif
  EXTENDED_RE=-r
endif

targets+=$(BUILDDIR)$(LV2NAME)$(LIB_EXT)

###############################################################################
# extract versions
LV2VERSION=$(balance_VERSION)
include git2lv2.mk

# check for build-dependencies
ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
else
  override CXXFLAGS+=`pkg-config --cflags lv2`
endif

# optional UI
ifeq ($(IS_OSX), yes)
  FONTFILE?=/usr/X11/lib/X11/fonts/TTF/VeraBd.ttf
  FONTSIZE?=32
else
  FONTFILE?=/usr/share/fonts/truetype/freefont/FreeSansBold.ttf
  FONTSIZE?=36
endif


ifeq ($(FONTFILE),verabd.h)
  # TODO built-in font - see setBfree
  FONT_FOUND=yes
  override FONTSIZE=32
else
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
endif

HAVE_UI=$(shell pkg-config --exists $(PKG_GL_LIBS) ftgl && echo $(FONT_FOUND))

LV2UIREQ=
# check for LV2 idle thread -- requires 'lv2', atleast_version='1.4.6
ifeq ($(shell pkg-config --atleast-version=1.4.6 lv2 || echo no), no)
  UICFLAGS+=-DOLD_SUIL
else
	LV2UIREQ=lv2:requiredFeature ui:idleInterface;\\n\\tlv2:extensionData ui:idleInterface;
endif

# check for lv2_atom_forge_object  new in 1.8.1 deprecates lv2_atom_forge_blank
ifeq ($(shell pkg-config --atleast-version=1.8.1 lv2 && echo yes), yes)
  override CXXFLAGS += -DHAVE_LV2_1_8
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
    UILIBS+=`pkg-config --variable=libdir ftgl`/libftgl.a `pkg-config --variable=libdir ftgl`/libfreetype.a -lm -mmacosx-version-min=10.5 
    UILIBS+=`pkg-config --libs zlib`
  else
    ifneq ($(XWIN),)
      UIDEPS+=pugl/pugl_win.cpp
      UICFLAGS+=-DPTW32_STATIC_LIB
      UILIBS=pugl/pugl_win.cpp
      UILIBS+=`pkg-config --variable=libdir ftgl`/libftgl.a `pkg-config --variable=libdir ftgl`/libfreetype.a
      UILIBS+=`pkg-config --libs zlib`
      UILIBS+=-lws2_32 -lwinmm -lopengl32 -lglu32 -lgdi32 -lcomdlg32 -lpthread
      UI_TYPE=WindowsUI
    else
      UIDEPS+=pugl/pugl_x11.c
      UICFLAGS+=`pkg-config --cflags glu gl`
      UILIBS=pugl/pugl_x11.c -lX11
      UI_TYPE=X11UI
      ifeq ($(STATICBUILD), yes)
        UILIBS+=`pkg-config --libs glu`
        UILIBS+=`pkg-config --variable=libdir ftgl`/libftgl.a `pkg-config --variable=libdir ftgl`/libfreetype.a
        UILIBS+=`pkg-config --libs zlib`
      else
        UILIBS+=`pkg-config --libs glu ftgl`
      endif
      UICFLAGS+=-DFONTFILE=\"$(FONTFILE)\"
    endif
  endif
  UILIBS+=`pkg-config --libs ftgl`
  UICFLAGS+=`pkg-config --cflags freetype2` `pkg-config --cflags ftgl` -DHAVE_FTGL -DUINQHACK=Blc
  UICFLAGS+=-DFONTSIZE=$(FONTSIZE)

  targets+=$(BUILDDIR)$(LV2GUI)$(LIB_EXT)
else
  $(warning "!!")
  $(warning "!! openGL/GLU is not available - GUI disabled")
  $(warning "!! install glu-dev, ftgl-dev and fonts-freefont-ttf to build LV2 GUI")
  $(warning "!!")
endif

# build target definitions
default: all

all: $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets)

lv2syms:
	echo "_lv2_descriptor" > lv2syms

lv2uisyms:
	echo "_lv2ui_descriptor" > lv2uisyms

$(BUILDDIR)manifest.ttl: manifest.ttl.in manifest.ui.ttl.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LIB_EXT@/$(LIB_EXT)/" \
	  manifest.ttl.in > $(BUILDDIR)manifest.ttl
ifeq ($(HAVE_UI), yes)
	sed "s/@LV2NAME@/$(LV2NAME)/;s/@LV2GUI@/$(LV2GUI)/;s/@LIB_EXT@/$(LIB_EXT)/;s/@UI_TYPE@/$(UI_TYPE)/" \
		manifest.ui.ttl.in >> $(BUILDDIR)manifest.ttl
endif

$(BUILDDIR)$(LV2NAME).ttl: $(LV2NAME).ttl.in $(LV2NAME).ui.ttl.in
	@mkdir -p $(BUILDDIR)
	sed "s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g" \
		$(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl
ifeq ($(HAVE_UI), yes)
	sed "s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/;" \
		$(LV2NAME).ui.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
endif

$(BUILDDIR)$(LV2NAME)$(LIB_EXT): balance.c uris.h
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) balance.c \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)
	$(STRIP) $(STRIPFLAGS) $(BUILDDIR)$(LV2NAME)$(LIB_EXT)

$(BUILDDIR)$(LV2GUI)$(LIB_EXT): ui.c uris.h $(UIDEPS) $(FONTFILE)
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(UICFLAGS) \
		-o $(BUILDDIR)$(LV2GUI)$(LIB_EXT) ui.c \
		-shared $(LV2LDFLAGS) $(LDFLAGS) $(UILIBS)
	$(STRIP) $(UISTRIPFLAGS) $(BUILDDIR)$(LV2GUI)$(LIB_EXT)

verabd.h: VeraBd.ttf
	xxd -i VeraBd.ttf > verabd.h

# install/uninstall/clean target definitions

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)
ifeq ($(HAVE_UI), yes)
	install -m755 $(BUILDDIR)$(LV2GUI)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
endif

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GUI)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)balance.ttl $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(BUILDDIR)$(LV2GUI)$(LIB_EXT) lv2syms lv2uisyms
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

.PHONY: clean all install uninstall
