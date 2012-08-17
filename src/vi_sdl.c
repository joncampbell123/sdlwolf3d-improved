#include "wl_def.h"

#include "SDL.h"

#define	JoyScaleMax		32768
#define	JoyScaleShift	8

typedef	struct {
	SDL_Joystick* joystick;
	
	word joyMinX;
	word joyMinY;
	word threshMinX;
	word threshMinY;
	word threshMaxX;
	word threshMaxY;
	word joyMaxX;
	word joyMaxY;
	word joyMultXL;
	word joyMultYL;
	word joyMultXH;
	word joyMultYH;
} JoystickDef;

static JoystickDef JoyDefs[MaxJoys];

static SDL_Surface *surface;
static unsigned int sdl_palettemode;

byte *gfxbuf = NULL;

extern void keyboard_handler(int code, int press);
extern boolean InternalKeyboard[NumCodes];

int main (int argc, char *argv[])
{
	return WolfMain(argc, argv);
}

void DisplayTextSplash(byte *text);

/*
==========================
=
= Quit
=
==========================
*/

void Quit(const char *error)
{
	memptr screen = NULL;

	if (!error || !*error) {
		CA_CacheGrChunk(ORDERSCREEN);
		screen = grsegs[ORDERSCREEN];
		WriteConfig();
	} else if (error) {
		CA_CacheGrChunk(ERRORSCREEN);
		screen = grsegs[ERRORSCREEN];
	}
	
	ShutdownId();
	
	if (screen) {
		//DisplayTextSplash(screen);
	}
	
	if (error && *error) {
		fprintf(stderr, "Quit: %s\n", error);
		exit(EXIT_FAILURE);
 	}
	exit(EXIT_SUCCESS);
}

void VL_WaitVBL(int vbls)
{
	unsigned long last = get_TimeCount() + vbls;
	while (last > get_TimeCount()) ;
}

void VW_UpdateScreen()
{
	SDL_Flip(surface);

	gfxbuf = surface->pixels;
}

/*
=======================
=
= VL_Startup
=
=======================
*/

void VL_Startup()
{
	const SDL_VideoInfo *vinfo;
	int flags;
	
	vwidth = 320;
	vheight = 200;
	
	if (MS_CheckParm("x2")) {
		vwidth *= 2;
		vheight *= 2;
	} else if (MS_CheckParm("x3")) {
		vwidth *= 3;
		vheight *= 3;
	}
	
	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE) < 0) {
		Quit("Couldn't init SDL");
	}

	vinfo = SDL_GetVideoInfo();
	sdl_palettemode = (vinfo->vfmt->BitsPerPixel == 8) ? (SDL_PHYSPAL|SDL_LOGPAL) : SDL_LOGPAL;
	
	flags = SDL_SWSURFACE|SDL_HWPALETTE|SDL_DOUBLEBUF;
	if (MS_CheckParm("fullscreen"))
		flags |= SDL_FULLSCREEN;
		
	surface = SDL_SetVideoMode(vwidth, vheight, 8, flags);
		
	if (surface == NULL) {
		SDL_Quit();
		Quit("(SDL) Couldn't set video mode");
	}
	gfxbuf = surface->pixels;
	
	if (surface->flags & SDL_FULLSCREEN)
		SDL_ShowCursor(0);
	
	SDL_WM_SetCaption(GAMENAME, GAMENAME);
	
	SDL_InitSubSystem(SDL_INIT_JOYSTICK);
}

/*
=======================
=
= VL_Shutdown
=
=======================
*/

void VL_Shutdown()
{
	SDL_Quit();
}

/* ======================================================================== */

/*
=================
=
= VL_SetPalette
=
=================
*/

void VL_SetPalette(const byte *palette)
{
	SDL_Color colors[256];
	int i;
	
	VL_WaitVBL(1);
	
	for (i = 0; i < 256; i++)
	{
		colors[i].r = ((int)palette[i*3+0] * 255) / 63;
		colors[i].g = ((int)palette[i*3+1] * 255) / 63;
		colors[i].b = ((int)palette[i*3+2] * 255) / 63;
	}
	SDL_SetPalette(surface, sdl_palettemode, colors, 0, 256);
}

/*
=================
=
= VL_GetPalette
=
=================
*/

void VL_GetPalette(byte *palette)
{
	int i;
	for (i=0;i<256;i++)
	{
		palette[i*3+0] = surface->format->palette->colors[i].r >> 2;
		palette[i*3+1] = surface->format->palette->colors[i].g >> 2;
		palette[i*3+2] = surface->format->palette->colors[i].b >> 2;
	}
}

static int XKeysymToScancode(unsigned int keysym)
{
	switch (keysym) {
		case SDLK_KP_ENTER:
		case SDLK_RETURN:
			return sc_Enter;
		case SDLK_ESCAPE:
			return sc_Escape;
		case SDLK_SPACE:
			return sc_Space;
		case SDLK_BACKSPACE:
			return sc_BackSpace;
		case SDLK_TAB:
			return sc_Tab;
		case SDLK_LALT:
		case SDLK_RALT:
			return sc_Alt;
		case SDLK_LCTRL:
		case SDLK_RCTRL:
			return sc_Control;
		case SDLK_CAPSLOCK:
			return sc_CapsLock;
		case SDLK_LSHIFT:
			return sc_LShift;
		case SDLK_RSHIFT:
			return sc_RShift;
		
		case SDLK_UP:
		case SDLK_KP8:
			return sc_UpArrow;
		case SDLK_DOWN:
		case SDLK_KP2:
			return sc_DownArrow;
		case SDLK_LEFT:
		case SDLK_KP4:
			return sc_LeftArrow;
		case SDLK_RIGHT:
		case SDLK_KP6:
			return sc_RightArrow;
		case SDLK_HOME:
			return sc_Home;
		case SDLK_END:
			return sc_End;
		case SDLK_PAGEUP:
			return sc_PgUp;
		case SDLK_PAGEDOWN:
			return sc_PgDn;
			
		case SDLK_INSERT:
			return sc_Insert;
		case SDLK_DELETE:
			return sc_Delete;
		case SDLK_F1:
			return sc_F1;
		case SDLK_F2:
			return sc_F2;
		case SDLK_F3:
			return sc_F3;
		case SDLK_F4:
			return sc_F4;
		case SDLK_F5:
			return sc_F5;
		case SDLK_F6:
			return sc_F6;
		case SDLK_F7:
			return sc_F7;
		case SDLK_F8:
			return sc_F8;
		case SDLK_F9:
			return sc_F9;
		case SDLK_F10:
			return sc_F10;
		case SDLK_F11:
			return sc_F11;
		case SDLK_F12:
			return sc_F12;	
		
		case SDLK_1:
			return sc_1;
		case SDLK_2:
			return sc_2;
		case SDLK_3:
			return sc_3;
		case SDLK_4:
			return sc_4;
		case SDLK_5:
			return sc_5;
		case SDLK_6:
			return sc_6;
		case SDLK_7:
			return sc_7;
		case SDLK_8:
			return sc_8;
		case SDLK_9:
			return sc_9;
		case SDLK_0:
			return sc_0;
		
		case SDLK_a:
			return sc_A;
		case SDLK_b:
			return sc_B;
		case SDLK_c:
			return sc_C;
		case SDLK_d:
			return sc_D;
		case SDLK_e:
			return sc_E;
		case SDLK_f:
			return sc_F;
		case SDLK_g:
			return sc_G;
		case SDLK_h:
			return sc_H;
		case SDLK_i:
			return sc_I;
		case SDLK_j:
			return sc_J;
		case SDLK_k:
			return sc_K;
		case SDLK_l:
			return sc_L;
		case SDLK_m:
			return sc_M;
		case SDLK_n:
			return sc_N;
		case SDLK_o:
			return sc_O;
		case SDLK_p:
			return sc_P;
		case SDLK_q:
			return sc_Q;
		case SDLK_r:
			return sc_R;
		case SDLK_s:
			return sc_S;
		case SDLK_t:
			return sc_T;
		case SDLK_u:
			return sc_U;
		case SDLK_v:
			return sc_V;
		case SDLK_w:
			return sc_W;
		case SDLK_x:
			return sc_X;
		case SDLK_y:
			return sc_Y;
		case SDLK_z:
			return sc_Z;
		
		case SDLK_PAUSE:
			return 0xE1;
		default:
			return sc_None;
	}
}

void INL_Update()
{
	SDL_Event event;
	boolean DebouncedKeyboard[NumCodes];
	
	memcpy(DebouncedKeyboard, InternalKeyboard, sizeof(DebouncedKeyboard));
	
	/* poll joysticks */
	SDL_JoystickUpdate();
		
	if (SDL_PollEvent(&event)) {
		do {
			switch(event.type) {
				case SDL_KEYDOWN:
					keyboard_handler(XKeysymToScancode(event.key.keysym.sym), 1);
					break;
				case SDL_KEYUP:
					keyboard_handler(XKeysymToScancode(event.key.keysym.sym), 0);
					break;
				case SDL_QUIT:
					/* TODO do something here */
					break;
				default:
					break;
			}
		} while (SDL_PollEvent(&event));
	}
	
	if (InternalKeyboard[sc_Alt] && 
	(!DebouncedKeyboard[sc_Return] && InternalKeyboard[sc_Return])) {
		SDL_GrabMode gm;
		
		SDL_WM_ToggleFullScreen(surface);
		
		gm = SDL_WM_GrabInput(SDL_GRAB_QUERY);
		if (gm == SDL_GRAB_OFF && !(surface->flags & SDL_FULLSCREEN))
			SDL_ShowCursor(1);
		else
			SDL_ShowCursor(0);
	}
	
	if (InternalKeyboard[sc_Control] &&
	(!DebouncedKeyboard[sc_G] && InternalKeyboard[sc_G])) {
		SDL_GrabMode gm;
		
		gm = SDL_WM_GrabInput(SDL_GRAB_QUERY);
		SDL_WM_GrabInput((gm == SDL_GRAB_ON) ? SDL_GRAB_OFF : SDL_GRAB_ON);
		
		gm = SDL_WM_GrabInput(SDL_GRAB_QUERY);
		if (gm == SDL_GRAB_OFF && !(surface->flags & SDL_FULLSCREEN))
			SDL_ShowCursor(1);
		else
			SDL_ShowCursor(0);
	}
	
	/* ctrl-z for iconify window? */
}

void IN_GetMouseDelta(int *dx, int *dy)
{
	int x, y;
	
	SDL_GetRelativeMouseState(&x, &y);
	
	if (dx)
		*dx = x;
	if (dy)
		*dy = y;
}

byte IN_MouseButtons()
{
	Uint8 state;
	byte retr;
	
	state = SDL_GetMouseState(NULL, NULL);
	
	retr = 0;
	
	if (state & SDL_BUTTON(SDL_BUTTON_LEFT)) {
		retr |= 1;
	}
	
	if (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
		retr |= 2;
	}
	
	if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) {
		retr |= 4;
	}
	
	return retr;
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_StartJoy() - Detects & auto-configures the specified joystick
//					The auto-config assumes the joystick is centered
//
///////////////////////////////////////////////////////////////////////////
boolean INL_StartJoy(word joy)
{
	JoystickDef* def;
	
	def = &JoyDefs[joy];
	
	memset(def, 0, sizeof(*def));

	def->joystick = SDL_JoystickOpen(joy);
	
	return (def->joystick != NULL);
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_ShutJoy() - Cleans up the joystick stuff
//
///////////////////////////////////////////////////////////////////////////
void INL_ShutJoy(word joy)
{
	JoystickDef* def;
	
	def = &JoyDefs[joy];
	
	if (def->joystick != NULL) {
		SDL_JoystickClose(def->joystick);
	}
	
	memset(def, 0, sizeof(*def));
}

/*
===================
=
= IN_JoyButtons
=
===================
*/

byte IN_JoyButtons()
{
	return ((INL_GetJoyButtons(0) << 0) | (INL_GetJoyButtons(1) << 2));
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_GetJoyAbs() - Reads the absolute position of the specified joystick
//
///////////////////////////////////////////////////////////////////////////
void IN_GetJoyAbs(word joy,word *xp,word *yp)
{
	JoystickDef* def;
	
	def = &JoyDefs[joy];
	
	if (def->joystick != NULL) {
		*xp = ((int)SDL_JoystickGetAxis(def->joystick, 1) + 32768) / 16;
		*yp = ((int)SDL_JoystickGetAxis(def->joystick, 0) + 32768) / 16;
	} else {
		*xp = 0;
		*yp = 0;
	}
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_GetJoyDelta() - Returns the relative movement of the specified
//		joystick (from +/-127)
//
///////////////////////////////////////////////////////////////////////////
void INL_GetJoyDelta(word joy,int *dx,int *dy)
{
	word		x,y;
	JoystickDef	*def;

	IN_GetJoyAbs(joy,&x,&y);
	def = JoyDefs + joy;

	if (x < def->threshMinX)
	{
		if (x < def->joyMinX)
			x = def->joyMinX;

		x = -(x - def->threshMinX);
		x *= def->joyMultXL;
		x >>= JoyScaleShift;
		*dx = (x > 127)? -127 : -x;
	}
	else if (x > def->threshMaxX)
	{
		if (x > def->joyMaxX)
			x = def->joyMaxX;

		x = x - def->threshMaxX;
		x *= def->joyMultXH;
		x >>= JoyScaleShift;
		*dx = (x > 127)? 127 : x;
	}
	else
		*dx = 0;

	if (y < def->threshMinY)
	{
		if (y < def->joyMinY)
			y = def->joyMinY;

		y = -(y - def->threshMinY);
		y *= def->joyMultYL;
		y >>= JoyScaleShift;
		*dy = (y > 127)? -127 : -y;
	}
	else if (y > def->threshMaxY)
	{
		if (y > def->joyMaxY)
			y = def->joyMaxY;

		y = y - def->threshMaxY;
		y *= def->joyMultYH;
		y >>= JoyScaleShift;
		*dy = (y > 127)? 127 : y;
	}
	else
		*dy = 0;
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_GetJoyButtons() - Returns the button status of the specified
//		joystick
//
///////////////////////////////////////////////////////////////////////////
word INL_GetJoyButtons(word joy)
{
	JoystickDef* def;
	
	def = &JoyDefs[joy];
	
	if (def->joystick != NULL) {
		return (SDL_JoystickGetButton(def->joystick, 0) << 0)
			| (SDL_JoystickGetButton(def->joystick, 1) << 1);
	}
	
	return 0;
}

//
//	INL_SetJoyScale() - Sets up scaling values for the specified joystick
//
static void INL_SetJoyScale(word joy)
{
	JoystickDef	*def;

	def = &JoyDefs[joy];
	def->joyMultXL = JoyScaleMax / (def->threshMinX - def->joyMinX);
	def->joyMultXH = JoyScaleMax / (def->joyMaxX - def->threshMaxX);
	def->joyMultYL = JoyScaleMax / (def->threshMinY - def->joyMinY);
	def->joyMultYH = JoyScaleMax / (def->joyMaxY - def->threshMaxY);
}

///////////////////////////////////////////////////////////////////////////
//
//      IN_SetupJoy() - Sets up thresholding values and calls INL_SetJoyScale()
//              to set up scaling values
//
///////////////////////////////////////////////////////////////////////////
void IN_SetupJoy(word joy,word minx,word maxx,word miny,word maxy)
{
	word		d,r;
	JoystickDef	*def;

	def = &JoyDefs[joy];

	def->joyMinX = minx;
	def->joyMaxX = maxx;
	r = maxx - minx;
	d = r / 3;
	def->threshMinX = ((r / 2) - d) + minx;
	def->threshMaxX = ((r / 2) + d) + minx;

	def->joyMinY = miny;
	def->joyMaxY = maxy;
	r = maxy - miny;
	d = r / 3;
	def->threshMinY = ((r / 2) - d) + miny;
	def->threshMaxY = ((r / 2) + d) + miny;

	INL_SetJoyScale(joy);
}
