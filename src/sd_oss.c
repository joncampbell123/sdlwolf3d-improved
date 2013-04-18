#include "wl_def.h"

#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL_audio.h>

#include "fmopl.h"

#define PACKED __attribute__((packed))

/* this hacked version allows multiple simultaneous sound effects,
 * but makes it an option, so you can have the traditional one-sound-fx-at-a-time
 * mode you're probably used to */
#define MAX_ALT_FX		12

typedef	struct {
	longword length;
	word priority;
} PACKED SoundCommon;

typedef	struct {
	SoundCommon common;
	byte data[1];
} PACKED PCSound;

typedef	struct {
	byte mChar, cChar, mScale, cScale, mAttack, cAttack, mSus, cSus,
		mWave, cWave, nConn, voice, mode, unused[3];
} PACKED Instrument;

typedef	struct {
	SoundCommon common;
	Instrument inst;
	byte block, data[1];
} PACKED AdLibSound;

typedef	struct {
	word length, values[1];
} PACKED MusicGroup;
	
SDMode SoundMode;
SMMode MusicMode;
SDSMode DigiMode;

static SDL_AudioSpec sdl_audiofmt;

static volatile boolean sqActive;

static word *DigiList = NULL;

/* JMC: Options! */
/* --Option: Enable multiple simultaneous sound effects at once.
 *           If disabled, this code behaves like the original Wolfenstein 3D sound code
 *           and only permits one digital sound effect at once. */
unsigned char multiple_fx = 1;
/* --Option: Enable diminishing positional sound effects by distance.
 *           If disabled, only the 2D "panning" effects apply. */
unsigned char EnableVolumeByDistance = 1;
/* TODO: Option to apply DC restoration to the audio samples, default off except for the "Eat at Joes" build.
 *       The DC restoration would avoid clipping, popping, and saturation artifacts caused by the
 *       mediocre quality of the digitized samples */

static volatile boolean SD_Started = 0;

typedef struct SoundPlaybackState {
	byte*		data;		/* must remain valid until sound thread is finished! */
	unsigned int	PlayPos;	/* NTS: 16.16 fixed point! */
	unsigned int	PlayLen;	/* in samples */
	unsigned int	PlayRate;	/* NTS: 16.16 fixed point, (1 << 16) = 1.0 */
} SoundPlaybackState;

typedef struct SoundPlaybackChannel {
	SoundPlaybackState	s[CHUNKS_PER_CHANNEL];
	int			soundpage;
	word			digiindex;
	word			soundpage_remaining_samples;
	unsigned char		positional;
	fixed			posx,posy;
	soundnames		soundname;
	word			priority;
	signed char		active;	/* -1 inactive  0..3 sound chunks */
	unsigned char		L,R;		/* positional audio (0...15) */
} SoundPlaybackChannel;

/* this is called from main thread */
void SoundFxCh_Idle(SoundPlaybackChannel *c) {
	SoundPlaybackState *s;
	unsigned int i;

	if (c->active < 0)
		return;

	/* NTS: OneBlock() will set data == NULL when it's done with the block */
	i = c->active;
	s = &c->s[i];
	if (s->data != NULL) {
		if (++i >= CHUNKS_PER_CHANNEL) i = 0;
		s = &c->s[i];
	}

	/* find a chunk and fill it */
	if (s->data == NULL && c->soundpage >= 0) {
		if (c->soundpage_remaining_samples == 0) {
			/* the end */
			s->PlayRate = 0;
			c->soundpage = -1;
		}
		else {
			s->PlayLen = (c->soundpage_remaining_samples > 4096 ? 4096 : c->soundpage_remaining_samples);
			c->soundpage_remaining_samples -= s->PlayLen;
			s->data = PM_GetSoundPage(++c->soundpage);
		}
	}
}

void SoundFxCh_ReInit(SoundPlaybackChannel *c) {
	c->active = -1;
	c->priority = 0;
	c->soundpage = -1;
	c->positional = 0;
}

void SoundFxCh_BeginStreamingPages(SoundPlaybackChannel *c,word soundname) {
	unsigned int i;

	c->priority = ((SoundCommon *)audiosegs[STARTADLIBSOUNDS+soundname])->priority;
	c->soundname = soundname;
	c->digiindex = DigiMap[c->soundname];
	c->soundpage = DigiList[c->digiindex*2];
	c->soundpage_remaining_samples = DigiList[(c->digiindex*2)+1];
	for (i=0;i < CHUNKS_PER_CHANNEL;i++) {
		c->s[i].PlayRate = 10402; /* 7000 / 44100 * 65536 */
		c->s[i].data = NULL;
		c->s[i].PlayPos = 0;
		c->s[i].PlayLen = 0;
	}
	c->s[0].PlayLen = (c->soundpage_remaining_samples > 4096 ? 4096 : c->soundpage_remaining_samples);
	c->soundpage_remaining_samples -= c->s[0].PlayLen;
	c->s[0].data = PM_GetSoundPage(c->soundpage);
	c->L = c->R = 8;
	c->active = 0;
}

/* NTS: This is called from sound thread! */
void SoundFxCh_OneBlock(SoundPlaybackChannel *c,int16_t *blk,unsigned int cnt) {
	unsigned int excess = 0;

	if (c->active < 0)
		return;

	do {
		SoundPlaybackState *s = &c->s[c->active];

		if (s->data == NULL) {
			if (s->PlayRate == 0) {
				/* the section with PlayRate == 0 is the EOS */
				c->priority = 0;
				c->active = -1;
			}

			break;
		}

		if (cnt == 0) break;

		s->PlayPos += excess;
		excess = 0;

		while ((s->PlayPos>>16) < s->PlayLen) {
			int l = (int)blk[0],r = (int)blk[1];
			int ss = ((int)s->data[s->PlayPos>>16] - 0x80) << 8;

			l += (ss*(16-c->L))>>5;
			if (l > 32767) blk[0] = 32767;
			else if (l < -32768) blk[0] = -32768;
			else blk[0] = (int16_t)l;
			r += (ss*(16-c->R))>>5;
			if (r > 32767) blk[1] = 32767;
			else if (r < -32768) blk[1] = -32768;
			else blk[1] = (int16_t)r;

			blk += 2;
			s->PlayPos += s->PlayRate;
			if (--cnt == 0) break;
		}

		if ((s->PlayPos>>16) >= s->PlayLen) {
			excess += s->PlayPos - (s->PlayLen<<16UL);
			s->data = NULL;
			s->PlayLen = 0;
			s->PlayPos = 0;
			if (++c->active >= CHUNKS_PER_CHANNEL) c->active = 0;
		}
	} while (1);
}

static SoundPlaybackChannel SoundFxCh;
static SoundPlaybackChannel SoundFxAlt[MAX_ALT_FX];

static FM_OPL *OPL;

static MusicGroup *Music;
static volatile int NewMusic;
static int MusicLength = 0;
static int MusicCount = 0;
static word *MusicData = NULL;

static volatile int NewAdlib;
static volatile int AdlibPlaying;
static AdLibSound *AdlibSnd;
static byte AdlibBlock = 0;
static byte *AdlibData = NULL;
static int AdlibLength = -1;

static int CurDigi;
static int CurAdlib;

static short int sndbuf[512];
static short int musbuf[256];
static size_t sndbuf_out=0;
static unsigned char sndbuf_ready=0;

void sdl_audio_callback(void *unused,unsigned char *stream,int len) {
	int16_t *out;
	size_t i;
	
	out = (int16_t*)stream;
	len /= 2 * 2; /* 16-bit stereo */

	while (len > 0) {
		if (sndbuf_ready) {
			if (sndbuf_out < 512) {
				*out++ = sndbuf[sndbuf_out++];
				*out++ = sndbuf[sndbuf_out++];
				len--;
			}
			if (sndbuf_out >= 512) {
				sndbuf_out = 0;
				sndbuf_ready = false;
			}
		}
		else {
			if (NewAdlib != -1) {
				if (SoundMode == sdm_AdLib) {
					Instrument *inst;

					AdlibPlaying = NewAdlib;
					AdlibSnd = (AdLibSound *)audiosegs[STARTADLIBSOUNDS+AdlibPlaying];
					inst = (Instrument *)&AdlibSnd->inst;
#define alChar		0x20
#define alScale		0x40
#define alAttack	0x60
#define alSus		0x80
#define alFeedCon	0xC0
#define alWave		0xE0

					OPLWrite(OPL, 0 + alChar, 0);
					OPLWrite(OPL, 0 + alScale, 0);
					OPLWrite(OPL, 0 + alAttack, 0);
					OPLWrite(OPL, 0 + alSus, 0);
					OPLWrite(OPL, 0 + alWave, 0);
					OPLWrite(OPL, 3 + alChar, 0);
					OPLWrite(OPL, 3 + alScale, 0);
					OPLWrite(OPL, 3 + alAttack, 0);
					OPLWrite(OPL, 3 + alSus, 0);
					OPLWrite(OPL, 3 + alWave, 0);
					OPLWrite(OPL, 0xA0, 0);
					OPLWrite(OPL, 0xB0, 0);

					OPLWrite(OPL, 0 + alChar, inst->mChar);
					OPLWrite(OPL, 0 + alScale, inst->mScale);
					OPLWrite(OPL, 0 + alAttack, inst->mAttack);
					OPLWrite(OPL, 0 + alSus, inst->mSus);
					OPLWrite(OPL, 0 + alWave, inst->mWave);
					OPLWrite(OPL, 3 + alChar, inst->cChar);
					OPLWrite(OPL, 3 + alScale, inst->cScale);
					OPLWrite(OPL, 3 + alAttack, inst->cAttack);
					OPLWrite(OPL, 3 + alSus, inst->cSus);
					OPLWrite(OPL, 3 + alWave, inst->cWave);

					/*OPLWrite(OPL, alFeedCon, inst->nConn);*/
					OPLWrite(OPL, alFeedCon, 0);

					AdlibBlock = ((AdlibSnd->block & 7) << 2) | 0x20;
					AdlibData = (byte *)&AdlibSnd->data;
					AdlibLength = AdlibSnd->common.length*5 - 1;
					OPLWrite(OPL, 0xB0, AdlibBlock);
				}
				else {
					AdlibData = NULL;
					AdlibLength = 0;
					AdlibPlaying = -1;
					AdlibSnd = NULL;
				}

				NewAdlib = -1;
			}
			if (NewMusic != -1) {
				NewMusic = -1;
				if (MusicMode == smm_AdLib && Music != NULL) {
					MusicLength = Music->length;
					MusicData = Music->values;
					MusicCount = 0;
				}
				else {
					MusicLength = 0;
					MusicData = NULL;
					MusicCount = 0;
				}
			}

			if (MusicMode == smm_AdLib || SoundMode == sdm_AdLib) {
				for (i = 0; i < 4; i++) {
					if (sqActive && MusicLength >= 4) {
						while (MusicCount <= 0) {
							word dat;

							/* JMC HACK: Either this code is off by 16 bytes or Wolfenstein 3D music blocks
							 *           have 16 bytes of junk at the end. On most music, the end data will
							 *           cause this code to write 0x00 to register 0x01, forcing all voices
							 *           to the sine wave and making the Adlib sound effects sound wrong
							 *           right after the music loops */
							dat = *MusicData++;
							MusicCount = *MusicData++;
							MusicLength -= 4;
							if (dat == 0 || (dat&0xFF) == 0x01) {
								/* no writing to WSE and test register or to register zero! */
							}
							else {
								OPLWrite(OPL, dat & 0xFF, dat >> 8);
							}
						}
						if (MusicLength <= 0) {
							NewMusic = 1;
						}
						MusicCount--;
					}

					if (AdlibPlaying != -1 && AdlibData != NULL) {
						if (AdlibLength <= -1) {
							OPLWrite(OPL, 0xA0, 00);
							OPLWrite(OPL, 0xB0, AdlibBlock & (~0x20));
							AdlibPlaying = -1;
						} else if ((AdlibLength % 5) == 4) {
							/* JMC HACK: I dunno what the programmer was doing with AdlibBlock & ~2,
							 *           that would only cut the F-number range in half, right?
							 *
							 *           I noticed that *AdlibData actually tends to vary to and from
							 *           zero. When I added this code, the Adlib sound effects rendered
							 *           correctly instead of sounding incomplete. Perhaps the OPL2
							 *           chipset considers F-Number == 0 the same as clearing the KEY bit? */
							OPLWrite(OPL, 0xA0, *AdlibData);
							OPLWrite(OPL, 0xB0, AdlibBlock & ~(*AdlibData == 0 ? 0x20/*KEY*/ : 0x00));
							AdlibData++;
						}
						AdlibLength--;
					}

					YM3812UpdateOne(OPL, &musbuf[i*64], 64);
				}

				for (i = 0; i < (sizeof(sndbuf)/sizeof(sndbuf[0])); i += 2)
					sndbuf[i+0] = sndbuf[i+1] = musbuf[i>>1U];
			}
			else {
				memset(sndbuf,0,sizeof(sndbuf));
			}

			if (DigiMode == sds_SDL_Audio) {
				SoundFxCh_OneBlock(&SoundFxCh,sndbuf,sizeof(sndbuf)/(sizeof(sndbuf[0])*2));
				if (multiple_fx) for (i=0;i < MAX_ALT_FX;i++) SoundFxCh_OneBlock(&SoundFxAlt[i],sndbuf,sizeof(sndbuf)/(sizeof(sndbuf[0])*2));
			}

			sndbuf_ready = 1;
			sndbuf_out = 0;
		}
	}
}

static void Blah()
{
        memptr  list = NULL;
        word    *p, pg;
        int     i;

        MM_GetPtr(&list,PMPageSize);
        p = PM_GetPage(ChunksInFile - 1);
        memcpy((void *)list,(void *)p,PMPageSize);
        
        pg = PMSoundStart;
        for (i = 0;i < PMPageSize / (sizeof(word) * 2);i++,p += 2)
        {
                if (pg >= ChunksInFile - 1)
                        break;
                pg += (p[1] + (PMPageSize - 1)) / PMPageSize;
        }
        MM_GetPtr((memptr *)&DigiList, i * sizeof(word) * 2);
        memcpy((void *)DigiList, (void *)list, i * sizeof(word) * 2);
        MM_FreePtr(&list);        
}

void SD_Startup()
{
	size_t i;
	
	if (SD_Started)
		return;

	memset(&SoundFxCh,0,sizeof(SoundFxCh));
	SoundFxCh.active = -1;

	memset(&SoundFxAlt,0,sizeof(SoundFxAlt));
	for (i=0;i < MAX_ALT_FX;i++) SoundFxAlt[i].active = -1;

	Blah();
	
	InitDigiMap();
	
	OPL = OPLCreate(OPL_TYPE_YM3812, 3579545, 44100);

	OPLWrite(OPL, 0x01, 0x20); /* Set WSE=1 */
	OPLWrite(OPL, 0x08, 0x00); /* Set CSM=0 & SEL=0 */

	sdl_audiofmt.freq = 44100;
	sdl_audiofmt.format = AUDIO_S16;
	sdl_audiofmt.channels = 2;
	sdl_audiofmt.samples = 512;
	sdl_audiofmt.callback = sdl_audio_callback;
	if (SDL_OpenAudio(&sdl_audiofmt,NULL) < 0) {
		fprintf(stderr,"SDL unable to open audio\n");
		SD_Shutdown();
		return;
	}

	SDL_PauseAudio(0);

	CurDigi = -1;
	CurAdlib = -1;
	NewAdlib = -1;
	NewMusic = -1;
	AdlibPlaying = -1;
	sqActive = false;

	SD_Started = true;
}

/* Jonathan C: The idea is the main loop (or event loop where it matters) will call this
 *             periodically and this function will take care of ensuring continuous playback
 *             across pages. I think it was unwise for the SDLWolf3d developer to rely on
 *             calling PM_GetPages() from the sound thread. */
void SD_Idle() {
	unsigned int i;

	SoundFxCh_Idle(&SoundFxCh);
	if (multiple_fx) for (i=0;i < MAX_ALT_FX;i++) SoundFxCh_Idle(&SoundFxAlt[i]);
}

void SD_Shutdown()
{
	if (SD_Started) {
		SD_MusicOff();
		SD_StopSound();
		SD_Started = false;
	}

	SDL_PauseAudio(1);
	SDL_CloseAudio();

	if (OPL != NULL) OPLDestroy(OPL);
	OPL = NULL;

	MM_FreePtr((memptr*)(&DigiList));
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_PlaySound() - plays the specified sound on the appropriate hardware
//
//////////////////////////////////////////////////////////////////////// */
boolean SD_PlaySound(soundnames sound)
{
	SoundCommon *s;
	
	s = (SoundCommon *)audiosegs[STARTADLIBSOUNDS + sound];

	SDL_LockAudio();
	if (DigiMap[sound] != -1 && DigiMode == sds_SDL_Audio) {
		if (SoundFxCh.active < 0 || (!multiple_fx && s->priority >= SoundFxCh.priority)) {
			SoundFxCh_ReInit(&SoundFxCh);
			SoundFxCh_BeginStreamingPages(&SoundFxCh,sound);
			SDL_UnlockAudio();
			return true;
		}

		if (multiple_fx) {
			unsigned int i;

			for (i=0;i < MAX_ALT_FX;i++) {
				if (SoundFxAlt[i].active < 0) {
					SoundFxCh_ReInit(&SoundFxAlt[i]);
					SoundFxCh_BeginStreamingPages(&SoundFxAlt[i],sound);
					SDL_UnlockAudio();
					return true;
				}
			}

			for (i=0;i < MAX_ALT_FX;i++) {
				if (SoundFxAlt[i].active < 0 || s->priority >= SoundFxAlt[i].priority) {
					SoundFxCh_ReInit(&SoundFxAlt[i]);
					SoundFxCh_BeginStreamingPages(&SoundFxAlt[i],sound);
					SDL_UnlockAudio();
					return true;
				}
			}
		}

		SDL_UnlockAudio();
		return false;
	}

	if ((AdlibPlaying == -1) || (CurAdlib == -1) || 
		(s->priority >= ((SoundCommon *)audiosegs[STARTADLIBSOUNDS+CurAdlib])->priority) ) {
		CurAdlib = sound;
		NewAdlib = sound;
		SDL_UnlockAudio();
		return true;
	}

	SDL_UnlockAudio();
	return false;
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_SoundPlaying() - returns the sound number that's playing, or 0 if
//		no sound is playing
//
//////////////////////////////////////////////////////////////////////// */
word SD_SoundPlaying()
{
	SDL_LockAudio();

	if (DigiMode == sds_SDL_Audio) {
		if (SoundFxCh.soundname != -1 && SoundFxCh.active >= 0) {
			SDL_UnlockAudio();
			return SoundFxCh.soundname;
		}

		if (multiple_fx) {
			unsigned int i;
			for (i=0;i < MAX_ALT_FX;i++) {
				if (SoundFxAlt[i].active >= 0 && SoundFxAlt[i].soundname != -1) {
					SDL_UnlockAudio();
					return SoundFxCh.soundname;
				}
			}
		}
	}

	if (DigiMode == sds_SDL_Audio || SoundMode == sdm_AdLib) {
		if (AdlibPlaying != -1) {
			SDL_UnlockAudio();
			return CurAdlib;
		}
	}

	SDL_UnlockAudio();
	return 0;
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_StopSound() - if a sound is playing, stops it
//
//////////////////////////////////////////////////////////////////////// */
void SD_StopSound()
{
	unsigned int i;

	SDL_LockAudio();
	SoundFxCh.active = -1;
	for (i=0;i < MAX_ALT_FX;i++) SoundFxAlt[i].active = -1;
	SDL_UnlockAudio();
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_WaitSoundDone() - waits until the current sound is done playing
//
//////////////////////////////////////////////////////////////////////// */
void SD_WaitSoundDone()
{
	while (SD_SoundPlaying()) SD_Idle();
}

/*
==========================
=
= SetSoundLoc - Given the location of an object (in terms of global
=	coordinates, held in globalsoundx and globalsoundy), munges the values
=	for an approximate distance from the left and right ear, and puts
=	those values into leftchannel and rightchannel.
=
= JAB
=
==========================
*/

#define ATABLEMAX 15
static const byte righttable[ATABLEMAX][ATABLEMAX * 2] = {
{ 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 6, 0, 0, 0, 0, 0, 1, 3, 5, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 6, 4, 0, 0, 0, 0, 0, 2, 4, 6, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 4, 1, 0, 0, 0, 1, 2, 4, 6, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 6, 5, 4, 2, 1, 0, 1, 2, 3, 5, 7, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 6, 5, 4, 3, 2, 2, 3, 3, 5, 6, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 6, 6, 5, 4, 4, 4, 4, 5, 6, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 6, 6, 5, 5, 5, 6, 6, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 6, 6, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8}
};
static const byte lefttable[ATABLEMAX][ATABLEMAX * 2] = {
{ 8, 8, 8, 8, 8, 8, 8, 8, 5, 3, 1, 0, 0, 0, 0, 0, 6, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 6, 4, 2, 0, 0, 0, 0, 0, 4, 6, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 6, 4, 2, 1, 0, 0, 0, 1, 4, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 7, 5, 3, 2, 1, 0, 1, 2, 4, 5, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 6, 5, 3, 3, 2, 2, 3, 4, 5, 6, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 6, 5, 4, 4, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 6, 6, 5, 5, 5, 6, 6, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 6, 6, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
{ 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8}
};

void SoundFxCh_SetPosition(SoundPlaybackChannel *c,fixed gx,fixed gy) {
	fixed xt, yt;
	int x, y;

	c->positional = 1;
	c->posx = gx;
	c->posy = gy;

/* translate point to view centered coordinates */
	gx -= viewx;
	gy -= viewy;

/*
// calculate newx
*/
	xt = FixedByFrac(gx,viewcos);
	yt = FixedByFrac(gy,viewsin);
	x = (xt - yt) >> TILESHIFT;

/*
// calculate newy
*/
	xt = FixedByFrac(gx,viewsin);
	yt = FixedByFrac(gy,viewcos);
	y = (yt + xt) >> TILESHIFT;

	if (y >= ATABLEMAX)
		y = ATABLEMAX - 1;
	else if (y <= -ATABLEMAX)
		y = -ATABLEMAX;
	if (x < 0)
		x = -x;
	if (x >= ATABLEMAX)
		x = ATABLEMAX - 1;

	c->L =  lefttable[x][y + ATABLEMAX];
	c->R = righttable[x][y + ATABLEMAX];

/* Jonathan C: Also scale down by distance! */
	if (EnableVolumeByDistance) {
		fixed md = (abs(gx) > abs(gy) ? abs(gx) : abs(gy));
		if (md < (2<<TILESHIFT)) md = 2<<TILESHIFT;
		md = (16<<TILESHIFT) / ((fixed)(sqrt((double)md / (2<<TILESHIFT)) * (1<<TILESHIFT))); /* <- FIXME: Use lookup table */
		c->L = 16 - (((16 - c->L) * md) / 16);
		c->R = 16 - (((16 - c->R) * md) / 16);
	}
}

void SoundFxCh_UpdatePosition(SoundPlaybackChannel *c) {
	SoundFxCh_SetPosition(c,c->posx,c->posy);
}

/*
==========================
=
= SetSoundLocGlobal - Sets up globalsoundx & globalsoundy and then calls
=	UpdateSoundLoc() to transform that into relative channel volumes. Those
=	values are then passed to the Sound Manager so that they'll be used for
=	the next sound played (if possible).
=
==========================
*/

void PlaySoundLocGlobal(word sound, intptr_t id, fixed gx, fixed gy)
{
	SoundCommon *s;
	
	s = (SoundCommon *)audiosegs[STARTADLIBSOUNDS + sound];

	SDL_LockAudio();
	if (DigiMap[sound] != -1 && DigiMode == sds_SDL_Audio) {
		if (SoundFxCh.active < 0 || (!multiple_fx && s->priority >= SoundFxCh.priority)) {
			SoundFxCh_ReInit(&SoundFxCh);
			SoundFxCh_SetPosition(&SoundFxCh,gx,gy);
			SoundFxCh_BeginStreamingPages(&SoundFxCh,sound);
			SDL_UnlockAudio();
			return;
		}

		if (multiple_fx) {
			unsigned int i;

			for (i=0;i < MAX_ALT_FX;i++) {
				if (SoundFxAlt[i].active < 0) {
					SoundFxCh_ReInit(&SoundFxAlt[i]);
					SoundFxCh_SetPosition(&SoundFxAlt[i],gx,gy);
					SoundFxCh_BeginStreamingPages(&SoundFxAlt[i],sound);
					SDL_UnlockAudio();
					return;
				}
			}

			for (i=0;i < MAX_ALT_FX;i++) {
				if (SoundFxAlt[i].active < 0 || s->priority >= SoundFxAlt[i].priority) {
					SoundFxCh_ReInit(&SoundFxAlt[i]);
					SoundFxCh_SetPosition(&SoundFxAlt[i],gx,gy);
					SoundFxCh_BeginStreamingPages(&SoundFxAlt[i],sound);
					SDL_UnlockAudio();
					return;
				}
			}
		}

		SDL_UnlockAudio();
		return;
	}

	if ((AdlibPlaying == -1) || (CurAdlib == -1) || 
			(s->priority >= ((SoundCommon *)audiosegs[STARTADLIBSOUNDS+CurAdlib])->priority) ) {
		CurAdlib = sound;
		NewAdlib = sound;
		SDL_UnlockAudio();
		return;
	}

	SDL_UnlockAudio();
}

void UpdateSoundLoc(fixed x, fixed y, int angle)
{
	SDL_LockAudio();
	if (SoundFxCh.active >= 0 && SoundFxCh.positional)
		SoundFxCh_UpdatePosition(&SoundFxCh);

	if (multiple_fx) {
		unsigned int i;
		for (i=0;i < MAX_ALT_FX;i++) {
			if (SoundFxAlt[i].active >= 0 && SoundFxAlt[i].positional)
				SoundFxCh_UpdatePosition(&SoundFxAlt[i]);
		}
	}

	SDL_UnlockAudio();
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_MusicOn() - turns on the sequencer
//
//////////////////////////////////////////////////////////////////////// */
void SD_MusicOn()
{
	/* NTS: This is only called from SD_StartMusic or the "music pause" function in wl_menu.c
	 *      which obviously means the iD developers meant it to start/stop but not restart
	 *      the song */
	SDL_LockAudio();
	sqActive = true;
	SDL_UnlockAudio();
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_MusicOff() - turns off the sequencer and any playing notes
//
//////////////////////////////////////////////////////////////////////// */
void SD_MusicOff()
{
	SDL_LockAudio();
	sqActive = false;
	SDL_UnlockAudio();
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_StartMusic() - starts playing the music pointed to
//
//////////////////////////////////////////////////////////////////////// */
void SD_StartMusic(int music)
{
	music += STARTMUSIC;

	CA_CacheAudioChunk(music);

	SD_MusicOff();
	SD_MusicOn();

	SDL_LockAudio();
	Music = (MusicGroup *)audiosegs[music];
	NewMusic = 1;
	SDL_UnlockAudio();
}

void SD_SetMultipleFxMode(unsigned char on) {
	SDL_LockAudio();

	if (on != multiple_fx) {
		SD_StopSound();
		multiple_fx = on;
	}

	SDL_UnlockAudio();
}

void SD_SetDigiDevice(SDSMode mode)
{
	if (DigiMode != mode) {
		SD_StopSound();
		DigiMode = mode;
	}
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_SetSoundMode() - Sets which sound hardware to use for sound effects
//
//////////////////////////////////////////////////////////////////////// */
void SD_SetSoundMode(SDMode mode)
{
	if (SoundMode != mode) {
		SDL_LockAudio();
		NewAdlib = -1;
		AdlibPlaying = -1;
		OPLWrite(OPL, 0xA0, 0x00);
		OPLWrite(OPL, 0xB0, 0x00);
		SDL_UnlockAudio();

		SoundMode = mode;
	}
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_SetMusicMode() - sets the device to use for background music
//
//////////////////////////////////////////////////////////////////////// */
void SD_SetMusicMode(SMMode mode)
{
	size_t i;

	if (MusicMode != mode) {
		SD_MusicOff();
		SD_MusicOn();

		SDL_LockAudio();
		NewMusic = 1;

		/* reset the OPL "chip" (except channel 0 which is SFX) */
		for (i=1;i <= 8;i++) {
			OPLWrite(OPL, 0xA0+i, 0x00);
			OPLWrite(OPL, 0xB0+i, 0x00);
		}

		SDL_UnlockAudio();

		MusicMode = mode;
	}
}
