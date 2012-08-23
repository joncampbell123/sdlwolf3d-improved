#include "wl_def.h"

#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* TODO: this code is seriously braindead.  Needs to be rewritten.
   Debug adlib sound issue with intel compiler
 */
                     
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

boolean AdLibPresent, SoundBlasterPresent;
	
SDMode SoundMode;
SMMode MusicMode;
SDSMode DigiMode;

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

static volatile boolean SD_Started = 0;
static volatile int audiofd = -1;

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

static volatile int NewAdlib;
static volatile int AdlibPlaying;

static pthread_t hSoundThread = -1;
static pthread_mutex_t hSoundThreadMutex = PTHREAD_MUTEX_INITIALIZER;

static int CurDigi;
static int CurAdlib;

static short int sndbuf[512];
static short int musbuf[256];

static void *SoundThread(void *data)
{
	int i;
	int MusicLength = 0;
	int MusicCount = 0;
	word *MusicData = NULL;
	word dat;
	
	AdLibSound *AdlibSnd;
	byte AdlibBlock = 0;
	byte *AdlibData = NULL;
	int AdlibLength = -1;
	Instrument *inst;

	OPLWrite(OPL, 0x01, 0x20); /* Set WSE=1 */
	OPLWrite(OPL, 0x08, 0x00); /* Set CSM=0 & SEL=0 */

/* Yeah, one day I'll rewrite this... */
	
	while (SD_Started) {
		pthread_mutex_lock(&hSoundThreadMutex);
		if (audiofd != -1) {
			if (NewAdlib != -1) {
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
				NewAdlib = -1;
			}
			if (NewMusic != -1) {
				NewMusic = -1;
				MusicLength = Music->length;
				MusicData = Music->values;
				MusicCount = 0;
			}
			for (i = 0; i < 4; i++) {
				if (sqActive) {
					while (MusicCount <= 0) {
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

				if (AdlibPlaying != -1) {
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

			SoundFxCh_OneBlock(&SoundFxCh,sndbuf,sizeof(sndbuf)/(sizeof(sndbuf[0])*2));
			if (multiple_fx) for (i=0;i < MAX_ALT_FX;i++) SoundFxCh_OneBlock(&SoundFxAlt[i],sndbuf,sizeof(sndbuf)/(sizeof(sndbuf[0])*2));
			pthread_mutex_unlock(&hSoundThreadMutex);
			write(audiofd, sndbuf, sizeof(sndbuf));
		}
		else {
			pthread_mutex_unlock(&hSoundThreadMutex);
		}
	}
	return NULL;
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
	audio_buf_info info;
	int want, set;
	
	if (SD_Started)
		return;

	memset(&SoundFxCh,0,sizeof(SoundFxCh));
	SoundFxCh.active = -1;

	memset(&SoundFxAlt,0,sizeof(SoundFxAlt));
	for (want=0;want < MAX_ALT_FX;want++) SoundFxAlt[want].active = -1;

	Blah();
	
	InitDigiMap();
	
	OPL = OPLCreate(OPL_TYPE_YM3812, 3579545, 44100);
	
	audiofd = open("/dev/dsp", O_WRONLY);
	if (audiofd == -1) {
		perror("open(\"/dev/dsp\")");
		SD_Shutdown();
		return;
	}

	set = (8 << 16) | 10;
	if (ioctl(audiofd, SNDCTL_DSP_SETFRAGMENT, &set) == -1) {
		perror("ioctl SNDCTL_DSP_SETFRAGMENT");
		SD_Shutdown();
		return;
	}

	want = set = AFMT_S16_LE;
	if (ioctl(audiofd, SNDCTL_DSP_SETFMT, &set) == -1) {
		perror("ioctl SNDCTL_DSP_SETFMT");
		SD_Shutdown();
		return;
	}
	if (want != set) {
		fprintf(stderr, "Format: Wanted %d, Got %d\n", want, set);
		SD_Shutdown();
		return;
	}

	want = set = 1;
	if (ioctl(audiofd, SNDCTL_DSP_STEREO, &set) == -1) {
		perror("ioctl SNDCTL_DSP_STEREO");
		SD_Shutdown();
		return;
	}
	if (want != set) {
		fprintf(stderr, "Stereo: Wanted %d, Got %d\n", want, set);
		SD_Shutdown();
		return;
	}

	want = set = 44100;
	if (ioctl(audiofd, SNDCTL_DSP_SPEED, &set) == -1) {
		perror("ioctl SNDCTL_DSP_SPEED");
		SD_Shutdown();
		return;
	}
	if (want != set) {
		fprintf(stderr, "Speed: Wanted %d, Got %d\n", want, set);
		SD_Shutdown();
		return;
	}

	if (ioctl(audiofd, SNDCTL_DSP_GETOSPACE, &info) == -1) {
		perror("ioctl SNDCTL_DSP_GETOSPACE");
		SD_Shutdown();
		return;
	}
	printf("Fragments: %d\n", info.fragments);
	printf("FragTotal: %d\n", info.fragstotal);
	printf("Frag Size: %d\n", info.fragsize);
	printf("Bytes    : %d\n", info.bytes);

	CurDigi = -1;
	CurAdlib = -1;
	NewAdlib = -1;
	NewMusic = -1;
	AdlibPlaying = -1;
	sqActive = false;

	SD_Started = true;

	if (pthread_create(&hSoundThread, NULL, SoundThread, NULL) != 0) {
		perror("pthread_create");
		SD_Shutdown();
		return;
	}
}

/* Jonathan C: The idea is the main loop (or event loop where it matters) will call this
 *             periodically and this function will take care of ensuring continuous playback
 *             across pages. I think it was unwise for the SDLWolf3d developer to rely on
 *             calling PM_GetPages() from the sound thread. */
void SD_Idle() {
	unsigned int i;

	pthread_mutex_lock(&hSoundThreadMutex);
	SoundFxCh_Idle(&SoundFxCh);
	if (multiple_fx) for (i=0;i < MAX_ALT_FX;i++) SoundFxCh_Idle(&SoundFxAlt[i]);
	pthread_mutex_unlock(&hSoundThreadMutex);
}

void SD_Shutdown()
{
	fprintf(stderr,"SD_Shutdown() SD_Started=%u\n",SD_Started);
	if (SD_Started) {
		fprintf(stderr,"SD_Shutdown() in progress\n");
		SD_MusicOff();
		SD_StopSound();
		SD_Started = false;
	}

	/* unlike the original codebase, wait for the thread to stop */
	if (hSoundThread != -1) {
		pthread_join(hSoundThread,NULL);
		hSoundThread = -1;
	}

	if (OPL != NULL) OPLDestroy(OPL);
	OPL = NULL;
	if (audiofd != -1) close(audiofd);
	audiofd = -1;
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

	pthread_mutex_lock(&hSoundThreadMutex);
	if (DigiMap[sound] != -1) {
		if (SoundFxCh.active < 0 || (!multiple_fx && s->priority >= SoundFxCh.priority)) {
			SoundFxCh_ReInit(&SoundFxCh);
			SoundFxCh_BeginStreamingPages(&SoundFxCh,sound);
			pthread_mutex_unlock(&hSoundThreadMutex);
			return true;
		}

		if (multiple_fx) {
			unsigned int i;

			for (i=0;i < MAX_ALT_FX;i++) {
				if (SoundFxAlt[i].active < 0) {
					SoundFxCh_ReInit(&SoundFxAlt[i]);
					SoundFxCh_BeginStreamingPages(&SoundFxAlt[i],sound);
					pthread_mutex_unlock(&hSoundThreadMutex);
					return true;
				}
			}

			for (i=0;i < MAX_ALT_FX;i++) {
				if (SoundFxAlt[i].active < 0 || s->priority >= SoundFxAlt[i].priority) {
					SoundFxCh_ReInit(&SoundFxAlt[i]);
					SoundFxCh_BeginStreamingPages(&SoundFxAlt[i],sound);
					pthread_mutex_unlock(&hSoundThreadMutex);
					return true;
				}
			}
		}

		pthread_mutex_unlock(&hSoundThreadMutex);
		return false;
	}
	
	if ((AdlibPlaying == -1) || (CurAdlib == -1) || 
	(s->priority >= ((SoundCommon *)audiosegs[STARTADLIBSOUNDS+CurAdlib])->priority) ) {
		CurAdlib = sound;
		NewAdlib = sound;
		pthread_mutex_unlock(&hSoundThreadMutex);
		return true;
	}
	pthread_mutex_unlock(&hSoundThreadMutex);
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
	pthread_mutex_lock(&hSoundThreadMutex);
	if (SoundFxCh.soundname != -1 && SoundFxCh.active >= 0) {
		pthread_mutex_unlock(&hSoundThreadMutex);
		return SoundFxCh.soundname;
	}

	if (multiple_fx) {
		unsigned int i;
		for (i=0;i < MAX_ALT_FX;i++) {
			if (SoundFxAlt[i].active >= 0 && SoundFxAlt[i].soundname != -1) {
				pthread_mutex_unlock(&hSoundThreadMutex);
				return SoundFxCh.soundname;
			}
		}
	}

	if (AdlibPlaying != -1) {
		pthread_mutex_unlock(&hSoundThreadMutex);
		return CurAdlib;
	}
	pthread_mutex_unlock(&hSoundThreadMutex);
	return 0;
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_StopSound() - if a sound is playing, stops it
//
//////////////////////////////////////////////////////////////////////// */
void SD_StopSound()
{
	pthread_mutex_lock(&hSoundThreadMutex);
	SoundFxCh.active = -1;
	pthread_mutex_unlock(&hSoundThreadMutex);
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

	pthread_mutex_lock(&hSoundThreadMutex);
	if (DigiMap[sound] != -1) {
		if (SoundFxCh.active < 0 || (!multiple_fx && s->priority >= SoundFxCh.priority)) {
			SoundFxCh_ReInit(&SoundFxCh);
			SoundFxCh_SetPosition(&SoundFxCh,gx,gy);
			SoundFxCh_BeginStreamingPages(&SoundFxCh,sound);
			pthread_mutex_unlock(&hSoundThreadMutex);
			return;
		}

		if (multiple_fx) {
			unsigned int i;

			for (i=0;i < MAX_ALT_FX;i++) {
				if (SoundFxAlt[i].active < 0) {
					SoundFxCh_ReInit(&SoundFxAlt[i]);
					SoundFxCh_SetPosition(&SoundFxAlt[i],gx,gy);
					SoundFxCh_BeginStreamingPages(&SoundFxAlt[i],sound);
					pthread_mutex_unlock(&hSoundThreadMutex);
					return;
				}
			}

			for (i=0;i < MAX_ALT_FX;i++) {
				if (SoundFxAlt[i].active < 0 || s->priority >= SoundFxAlt[i].priority) {
					SoundFxCh_ReInit(&SoundFxAlt[i]);
					SoundFxCh_SetPosition(&SoundFxAlt[i],gx,gy);
					SoundFxCh_BeginStreamingPages(&SoundFxAlt[i],sound);
					pthread_mutex_unlock(&hSoundThreadMutex);
					return;
				}
			}
		}

		pthread_mutex_unlock(&hSoundThreadMutex);
		return;
	}
	
	if ((AdlibPlaying == -1) || (CurAdlib == -1) || 
	(s->priority >= ((SoundCommon *)audiosegs[STARTADLIBSOUNDS+CurAdlib])->priority) ) {
		CurAdlib = sound;
		NewAdlib = sound;
		pthread_mutex_unlock(&hSoundThreadMutex);
		return;
	}
	pthread_mutex_unlock(&hSoundThreadMutex);
}

void UpdateSoundLoc(fixed x, fixed y, int angle)
{
	pthread_mutex_lock(&hSoundThreadMutex);
	if (SoundFxCh.active >= 0 && SoundFxCh.positional)
		SoundFxCh_UpdatePosition(&SoundFxCh);

	if (multiple_fx) {
		unsigned int i;
		for (i=0;i < MAX_ALT_FX;i++) {
			if (SoundFxAlt[i].active >= 0 && SoundFxAlt[i].positional)
				SoundFxCh_UpdatePosition(&SoundFxAlt[i]);
		}
	}

	pthread_mutex_unlock(&hSoundThreadMutex);
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
	pthread_mutex_lock(&hSoundThreadMutex);
	sqActive = true;
	pthread_mutex_unlock(&hSoundThreadMutex);
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_MusicOff() - turns off the sequencer and any playing notes
//
//////////////////////////////////////////////////////////////////////// */
void SD_MusicOff()
{
	pthread_mutex_lock(&hSoundThreadMutex);
	sqActive = false;
	pthread_mutex_unlock(&hSoundThreadMutex);
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

	pthread_mutex_lock(&hSoundThreadMutex);
	Music = (MusicGroup *)audiosegs[music];
	NewMusic = 1;
	pthread_mutex_unlock(&hSoundThreadMutex);
}

void SD_SetMultipleFxMode(unsigned char on) {
	pthread_mutex_lock(&hSoundThreadMutex);

	if (on != multiple_fx) {
		unsigned int i;

		memset(&SoundFxAlt,0,sizeof(SoundFxAlt));
		for (i=0;i < MAX_ALT_FX;i++) SoundFxAlt[i].active = -1;
		multiple_fx = on;
	}

	pthread_mutex_unlock(&hSoundThreadMutex);
}

void SD_SetDigiDevice(SDSMode mode)
{
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_SetSoundMode() - Sets which sound hardware to use for sound effects
//
//////////////////////////////////////////////////////////////////////// */
boolean SD_SetSoundMode(SDMode mode)
{
	return false;
}

/*/////////////////////////////////////////////////////////////////////////
//
//	SD_SetMusicMode() - sets the device to use for background music
//
//////////////////////////////////////////////////////////////////////// */
boolean SD_SetMusicMode(SMMode mode)
{
	return false;
}
