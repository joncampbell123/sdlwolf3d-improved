CC = gcc

ifndef $(SUBDIR)
SUBDIR = wolf3d
WMODE = 0
endif

ifndef $(WMODE)
 ifeq ($(SUBDIR),wolf3d)
   WMODE = 0
 endif
 ifeq ($(SUBDIR),wolf3dr)
   WMODE = 1
 endif
 ifeq ($(SUBDIR),wolf3dr92)
   WMODE = 1
   MORE_CFLAGS += -DWOLF92
 endif
 ifeq ($(SUBDIR),speard)
   WMODE = 2
 endif
 ifeq ($(SUBDIR),spear)
   WMODE = 3
 endif
endif

MORE_CFLAGS += -DWMODE=$(WMODE)

# DEBUG
MORE_CFLAGS += -g3 -O0 -fno-omit-frame-pointer

CFLAGS = -g0 -Os -Wall -fomit-frame-pointer -pedantic $(MORE_CFLAGS)
OBJS = $(SUBDIR)/objs.o $(SUBDIR)/misc.o $(SUBDIR)/id_ca.o $(SUBDIR)/id_vh.o $(SUBDIR)/id_us.o \
	$(SUBDIR)/wl_act1.o $(SUBDIR)/wl_act2.o $(SUBDIR)/wl_act3.o $(SUBDIR)/wl_agent.o $(SUBDIR)/wl_game.o \
	$(SUBDIR)/wl_inter.o $(SUBDIR)/wl_menu.o $(SUBDIR)/wl_play.o $(SUBDIR)/wl_state.o $(SUBDIR)/wl_text.o $(SUBDIR)/wl_main.o \
	$(SUBDIR)/wl_debug.o $(SUBDIR)/vi_comm.o $(SUBDIR)/sd_comm.o
ROBJS = $(SUBDIR)/wl_draw.o
DOBJS = $(OBJS) $(ROBJS) $(SUBDIR)/vi_sdl.o

LDLIBS = -lm

# sound using OSS
OBJS += $(SUBDIR)/sd_oss.o $(SUBDIR)/fmopl.o

CFLAGS += $(shell sdl-config --cflags)

DLDLIBS = $(LDLIBS) $(shell sdl-config --libs)

all: $(SUBDIR) sdl$(SUBDIR)

$(SUBDIR):
	mkdir -p $(SUBDIR)

clean-datafiles:
	rm -fv *.wl1 *.wl6

# we need to unpack the Wolfenstein 3D shareware files
datafiles: audiohed.wl1 audiot.wl1 config.wl1 gamemaps.wl1 maphead.wl1 vgadict.wl1 vgagraph.wl1 vgahead.wl1 vswap.wl1

# we need to unpack the Wolfenstein 3D registered files
rdatafiles: audiohed.wl6 audiot.wl6 config.wl6 gamemaps.wl6 maphead.wl6 vgadict.wl6 vgagraph.wl6 vgahead.wl6 vswap.wl6

# Spear of Destiny registered
sdatafiles: audiohed.sod audiot.sod config.sod gamemaps.sod maphead.sod vgadict.sod vgagraph.sod vgahead.sod vswap.sod

# Spear of Destiny shareware
sddatafiles: audiohed.sdm audiot.sdm config.sdm gamemaps.sdm maphead.sdm vgadict.sdm vgagraph.sdm vgahead.sdm vswap.sdm

audiohed.sdm: ../datasd/audiohed.sdm.gz
	gunzip -c -d $^ >$@

audiot.sdm: ../datasd/audiot.sdm.gz
	gunzip -c -d $^ >$@

config.sdm: ../datasd/config.sdm.gz
	gunzip -c -d $^ >$@

gamemaps.sdm: ../datasd/gamemaps.sdm.gz
	gunzip -c -d $^ >$@

maphead.sdm: ../datasd/maphead.sdm.gz
	gunzip -c -d $^ >$@

vgadict.sdm: ../datasd/vgadict.sdm.gz
	gunzip -c -d $^ >$@

vgagraph.sdm: ../datasd/vgagraph.sdm.gz
	gunzip -c -d $^ >$@

vgahead.sdm: ../datasd/vgahead.sdm.gz
	gunzip -c -d $^ >$@

vswap.sdm: ../datasd/vswap.sdm.gz
	gunzip -c -d $^ >$@

audiohed.sod: ../datas/audiohed.sod.gz
	gunzip -c -d $^ >$@

audiot.sod: ../datas/audiot.sod.gz
	gunzip -c -d $^ >$@

config.sod: ../datas/config.sod.gz
	gunzip -c -d $^ >$@

gamemaps.sod: ../datas/gamemaps.sod.gz
	gunzip -c -d $^ >$@

maphead.sod: ../datas/maphead.sod.gz
	gunzip -c -d $^ >$@

vgadict.sod: ../datas/vgadict.sod.gz
	gunzip -c -d $^ >$@

vgagraph.sod: ../datas/vgagraph.sod.gz
	gunzip -c -d $^ >$@

vgahead.sod: ../datas/vgahead.sod.gz
	gunzip -c -d $^ >$@

vswap.sod: ../datas/vswap.sod.gz
	gunzip -c -d $^ >$@

audiohed.wl6: ../datar/audiohed.wl6.gz
	gunzip -c -d $^ >$@

audiot.wl6: ../datar/audiot.wl6.gz
	gunzip -c -d $^ >$@

config.wl6: ../datar/config.wl6.gz
	gunzip -c -d $^ >$@

gamemaps.wl6: ../datar/gamemaps.wl6.gz
	gunzip -c -d $^ >$@

maphead.wl6: ../datar/maphead.wl6.gz
	gunzip -c -d $^ >$@

vgadict.wl6: ../datar/vgadict.wl6.gz
	gunzip -c -d $^ >$@

vgagraph.wl6: ../datar/vgagraph.wl6.gz
	gunzip -c -d $^ >$@

vgahead.wl6: ../datar/vgahead.wl6.gz
	gunzip -c -d $^ >$@

vswap.wl6: ../datar/vswap.wl6.gz
	gunzip -c -d $^ >$@

audiohed.wl1: ../data/audiohed.wl1.gz
	gunzip -c -d $^ >$@

audiot.wl1: ../data/audiot.wl1.xz
	xz -c -d $^ >$@

config.wl1: ../data/config.wl1.gz
	gunzip -c -d $^ >$@

gamemaps.wl1: ../data/gamemaps.wl1.xz
	xz -c -d $^ >$@

maphead.wl1: ../data/maphead.wl1.gz
	gunzip -c -d $^ >$@

vgadict.wl1: ../data/vgadict.wl1.gz
	gunzip -c -d $^ >$@

vgagraph.wl1: ../data/vgagraph.wl1.xz
	xz -c -d $^ >$@

vgahead.wl1: ../data/vgahead.wl1
	cat $^ >$@

vswap.wl1: ../data/vswap.wl1.xz
	xz -c -d $^ >$@

$(SUBDIR)/objs.o: objs.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/misc.o: misc.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/id_ca.o: id_ca.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/id_vh.o: id_vh.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/id_us.o: id_us.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_act1.o: wl_act1.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_act2.o: wl_act2.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_act3.o: wl_act3.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_agent.o: wl_agent.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_game.o: wl_game.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_inter.o: wl_inter.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_menu.o: wl_menu.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_play.o: wl_play.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_state.o: wl_state.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_text.o: wl_text.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_main.o: wl_main.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_debug.o: wl_debug.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/vi_comm.o: vi_comm.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/sd_comm.o: sd_comm.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/wl_draw.o: wl_draw.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/vi_sdl.o: vi_sdl.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/sd_oss.o: sd_oss.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(SUBDIR)/fmopl.o: fmopl.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(DOBJS): version.h id_heads.h wl_def.h

sdl$(SUBDIR): $(DOBJS)
	$(CC) -o sdl$(SUBDIR) $(DOBJS) $(DLDLIBS)

clean:
	rm -rf sdlspear sdlspeard sdlwolf3d sdlwolf3dr sdlwolf3dr92 *.o *.il wolf3d wolf3dr wolf3dr92 spear speard

distclean: clean
	rm -rf *~ DEADJOE *.wl1 *.wl6 *.sod *.sdm

forced: distclean all
	rm -rf *.o

everything:
	make datafiles
	make rdatafiles
	make sdatafiles
	make sddatafiles
	make SUBDIR=wolf3d
	make SUBDIR=wolf3dr
	make SUBDIR=wolf3dr92
	make SUBDIR=spear
	make SUBDIR=speard

install: everything
	mkdir -p $(DESTDIR)/usr/share/sdlwolf3d/wolf3d/
	cp -v sdlwolf3d *.wl1 $(DESTDIR)/usr/share/sdlwolf3d/wolf3d/
	mkdir -p $(DESTDIR)/usr/share/sdlwolf3d/wolf3dr/
	cp -v sdlwolf3dr92 *.wl6 $(DESTDIR)/usr/share/sdlwolf3d/wolf3dr/
	mkdir -p $(DESTDIR)/usr/share/sdlwolf3d/spear/
	cp -v sdlspear *.sod $(DESTDIR)/usr/share/sdlwolf3d/spear/
	mkdir -p $(DESTDIR)/usr/share/sdlwolf3d/speard/
	cp -v sdlspeard *.sdm $(DESTDIR)/usr/share/sdlwolf3d/speard/
	mkdir -p $(DESTDIR)/usr/bin/
	cp -v script-sdlwolf3d.sh $(DESTDIR)/usr/bin/sdlwolf3d
	cp -v script-sdlwolf3dr92.sh $(DESTDIR)/usr/bin/sdlwolf3dr92
	cp -v script-sdlspear.sh $(DESTDIR)/usr/bin/sdlspear
	cp -v script-sdlspeard.sh $(DESTDIR)/usr/bin/sdlspeard
	mkdir -p $(DESTDIR)/usr/share/applications/
	cp -v sdlwolf3d.desktop $(DESTDIR)/usr/share/applications/
	cp -v sdlwolf3dr92.desktop $(DESTDIR)/usr/share/applications/
	cp -v sdlspear.desktop $(DESTDIR)/usr/share/applications/
	cp -v sdlspeard.desktop $(DESTDIR)/usr/share/applications/

