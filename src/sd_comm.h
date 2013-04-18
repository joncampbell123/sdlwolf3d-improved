#ifndef	__SD_COMM_H__
#define	__SD_COMM_H__

#define CHUNKS_PER_CHANNEL	4

typedef	enum	{
					sdm_Off,
					sdm_AdLib
				}	SDMode;
typedef	enum	{
					smm_Off,
					smm_AdLib
				}	SMMode;
typedef	enum	{
					sds_Off,
					sds_SDL_Audio
				}	SDSMode;

extern	SDMode		SoundMode;
extern	SDSMode		DigiMode;
extern	SMMode		MusicMode;
extern  unsigned char multiple_fx;

extern void SD_Startup();
extern void SD_Shutdown();
extern void SD_Idle();

extern boolean SD_PlaySound(soundnames sound);
extern void SD_StopSound(),
				SD_WaitSoundDone(),
				SD_StartMusic(int music),
				SD_MusicOn(),
				SD_MusicOff();

extern void SD_SetSoundMode(SDMode mode), SD_SetMusicMode(SMMode mode);
		
extern word SD_SoundPlaying();

extern void SD_SetDigiDevice(SDSMode);

void PlaySoundLocGlobal(word s, intptr_t id, fixed gx, fixed gy);
void UpdateSoundLoc(fixed x, fixed y, int angle);
void SD_SetMultipleFxMode(unsigned char on);


extern int DigiMap[];
void InitDigiMap();

#endif
