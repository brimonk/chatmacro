/*
 * Brian Chrzanowski
 * Tue Apr 28, 2020 21:52
 *
 * Chat Macro Program
 *
 * This is honestly just built to facilitate chat macros while playing video games, but it could
 * probably have other uses too.
 *
 * Currently, only Win32 is supported as a platform; however, it wouldn't be too much effort to
 * swap out the Win32 calls with equivalent, or close to, Xorg calls, to make it available on Linux.
 *
 * Virtual Keycodes:
 *   https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
 *
 * USAGE
 *   chatmacro.exe <macrofile>
 *
 *   Currently, these keys are hardcoded in main, with these functions:
 *     NUMPAD .    - quits program
 *     NUMPAD 0    - toggle hotkeys on / off (leaves running)
 *     NUMPAD 1    - swaps to the previous macro bank (-1)
 *     NUMPAD 2    - swaps to the next macro bank     (+1)
 *     NUMPAD 4    - moves to the previous macro      (-1)
 *     NUMPAD 5    - moves to the next macro          (+1)
 *     NUMPAD 8    - "types" the macro through the keyboard
 *
 *   Also, the macro file ("macros.txt") is also hardcoded. Some argument parsing or configuration
 *   would probably do this program well.
 *
 * ChatWheel NOTE
 *
 *   It's currently the case that we don't _really_ handle key releases particularly well. The main
 *   thing we do, is basically, bring up the window on hotkey press, then wait for the SDL event
 *   that says our "hotkey key" was released.
 *
 *   Also possible: run a shell command :)
 *
 *   It looks like, to create the overlay situation, just creating the window and destroying it
 *   every time might be the most consistent way to do this. Not quite sure.
 *
 * TODO
 * 1. Minimize to Tray (Not Console Application)
 * 2. Redirect stdout/stderr to a log file
 * 3. Overlay Window
 * 4. Shuffle Button
 * 5. Start Applications (Custom Run Dialog for Specially Hooked up Programs??)
 */

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#define COMMON_IMPLEMENTATION
#include "common.h"

// #define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MACRO_FILE      ("macros.txt")
#define WINDOW_TITLE    ("chatwheel")
#define HOTKEY_TOGGLE   (VK_NUMPAD0)
#define HOTKEY_QUIT     (VK_DECIMAL)
#define SDL_WINDOW_PARAMS (SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_BORDERLESS|SDL_WINDOW_MOUSE_CAPTURE)

struct state_t {
	// SDL Vals
	SDL_Window *gWindow;
	SDL_Renderer *gRenderer;

	s32 display_w;
	s32 display_h;

	s32 nonce;

	s32 curr; // @DEPRECATED current string
	s32 is_running;
	s32 is_active;

	// Basically, replicate the DotA2 functionality, and save the user's mouse position
	s32 mouse_bak_x;
	s32 mouse_bak_y;

	// regular mouse values
	s32 mouse_x;
	s32 mouse_y;

	// macro text
	char **lines;
	size_t lines_len, lines_cap;
};

// NOTE (brian): the first three parameters are passed directly to RegisterHotkey
struct hotkey_t {
	u32 vk;
	u32 modifiers;
	s32 (*func)(struct state_t *state);
};

struct hotkey_event_arg_t {
	struct state_t *state;
	struct hotkey_t *hotkeys;
	s32 hotkeys_len;
};

/* sys_lasterror : handles errors that aren't propogated through win32 errno */
static void sys_lasterror();

/* macros_read : parse macros from the input file to the state */
s32 macros_read(struct state_t *state, char *fname);

/* hotkey_fn_togglewheel : toggles the chat wheel & sets up SDL state for the wheel */
s32 hotkey_fn_togglewheel(struct state_t *state);
/* hotkey_fn_quit : toggles the availabliliy of the other hotkeys */
s32 hotkey_fn_quit(struct state_t *state);
/* hotkey_fn_macro : swaps between macros in the bank */
s32 hotkey_fn_macro(struct state_t *state);
/* hotkey_fn_say : says the selected macro */
s32 hotkey_fn_say(struct state_t *state);

/* mk_kbdinput : helper function to fill in an INPUT structure for a keyboard */
void mk_kbdinput(INPUT *input, s16 vk, s16 sk, s32 key_up);
/* sendkey_single : sends a single key */
s32 sendkey_single(s32 keycode);

// Rendering Functions
/* Render : renders the transparent screen */
s32 Render(struct state_t *state);

// WIN32 Custom
/* Win32_CustomEventHandler : custom event handler to grab windows events in sdl */
void Win32_CustomEventHandler(void *user, void *hwnd, u32 message, u64 wparam, s64 lparam);
/* Win32_MakeWindowTransparent : makes the sdl window transparent */
int Win32_MakeWindowTransparent(SDL_Window *window, u8 r, u8 g, u8 b);
/* Win32_SetWindowPos : sets the window's z order */
int Win32_SetWindowPos(SDL_Window *window, s32 x, s32 y, s32 w, s32 h, s32 is_top);
/* Win32_GetWindowHWND : get the window handle */
HWND Win32_GetWindowHWND(SDL_Window *window);

int main(int argc, char **argv)
{
	SDL_DisplayMode display_mode;
	SDL_Event event;
	POINT mouse_point;
	struct state_t state;
	struct hotkey_event_arg_t hkarg;
	s32 i, rc;
	s16 ks;

	struct hotkey_t hotkeys[] = {
		  { HOTKEY_TOGGLE, 0x4000, hotkey_fn_togglewheel }
		, { HOTKEY_QUIT,   0x4000, hotkey_fn_quit }
	};

	memset(&state, 0, sizeof state);

	rc = macros_read(&state, MACRO_FILE);
	if (rc < 0) {
		ERR("Couldn't parse macro file!\n");
		exit(1);
	}

	// turn on all of the hotkeys that are "always on"
	for (i = 0; i < ARRSIZE(hotkeys); i++) {
		rc = RegisterHotKey(NULL, i, hotkeys[i].modifiers, hotkeys[i].vk);
		if (!rc) {
			ERR("Couldn't Register Hotkey %d :(\n", i);
			exit(1);
		}
	}

	SDL_Init(SDL_INIT_EVERYTHING);

	// hkarg setup (for handling WM_HOTKEY)
	hkarg.state = &state;
	hkarg.hotkeys = hotkeys;
	hkarg.hotkeys_len = ARRSIZE(hotkeys);

	SDL_SetWindowsMessageHook(Win32_CustomEventHandler, &hkarg);

	SDL_GetCurrentDisplayMode(0, &display_mode);
	state.display_w = display_mode.w;
	state.display_h = display_mode.h;

	SDL_CaptureMouse(SDL_TRUE);

	for (state.is_running = 1; state.is_running;) {

		// NOTE (brian) we don't really use SDL events at the moment. there's some nonsense about
		// how SDL doesn't report events unless you're in like, a visible region of the window, and
		// it's just crazy enough that it won't really work for this. But, it's here to capture the
		// SDL_QUIT event, just in case someone sends (the Win32 version of) a SIGINT

		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT: {
					state.is_running = 0;
					break;
				}
				default: {
					break;
				}
			}
		}

		// NOTE (brian) we need to take the vk code and stuff it into state, but I won't be ready
		// to do that until we have a nice-ish way to map between some strings, and these virtual
		// keycodes

		if (state.is_active) {
			rc = GetCursorPos(&mouse_point);
			if (rc) {
				state.mouse_x = mouse_point.x;
				state.mouse_y = mouse_point.y;
			}

			printf("M: (%04d,%04d)\n", state.mouse_x, state.mouse_y);

			ks = GetAsyncKeyState(HOTKEY_TOGGLE);
			if (state.is_active && ks == 0) {
				hotkey_fn_togglewheel(&state);
			}

			Render(&state);
		}

		state.nonce++;
		SDL_Delay(8);
	}

	if (state.gRenderer) {
		SDL_DestroyRenderer(state.gRenderer);
	}

	if (state.gWindow) {
		SDL_DestroyWindow(state.gWindow);
	}

	SDL_Quit();

	// turn off all of the hotkeys
	for (i = 0; i < ARRSIZE(hotkeys); i++) {
		rc = UnregisterHotKey(NULL, i);
		if (!rc) {
			ERR("Couldn't Unregister Hotkey %d :(\n", i);
			exit(1);
		}
	}

	return 0;
}

/* Render : renders the transparent screen */
s32 Render(struct state_t *state)
{
	SDL_Renderer *render;
	SDL_Rect r;

	assert(state);

	if (!state->gWindow && !state->gRenderer) {
		return -1;
	}

	render = state->gRenderer;

	SDL_SetRenderDrawColor(render, 255, 0, 255, 255);
	SDL_RenderClear(render);

	r.x = 0;
	r.y = 0;
	r.w = 64;
	r.h = 64;

	SDL_SetRenderDrawColor(render, 0, 255, 255, 255);
	SDL_RenderFillRect(render, &r);

	SDL_RenderPresent(render);

	return 0;
}

/* hotkey_fn_togglewheel : toggles the chat wheel & sets up SDL state for the wheel */
s32 hotkey_fn_togglewheel(struct state_t *state)
{
	SDL_Window *window;
	s32 w, h;

	// NOTE (brian) inside here, depending on state->is_active, we:
	//
	//   - show / hide the window
	//   - draw the wheel
	//   - save the mouse position, center the mouse, etc

	assert(state);

	window = state->gWindow;

	state->is_active = !state->is_active;
	state->nonce = 0;

	printf("active: %d\n", state->is_active);
	printf("win w: %d\n", state->display_w);
	printf("win h: %d\n", state->display_h);

	w = state->display_w;
	h = state->display_h;

	if (state->is_active) {
		state->gWindow   = SDL_CreateWindow(WINDOW_TITLE, 0, 0, w, h, SDL_WINDOW_PARAMS);
		state->gRenderer = SDL_CreateRenderer(state->gWindow, -1, SDL_RENDERER_ACCELERATED);

		Win32_MakeWindowTransparent(state->gWindow, 255, 0, 255);

		SDL_GetMouseState(&state->mouse_bak_x, &state->mouse_bak_y);
		SDL_WarpMouseInWindow(state->gWindow, state->display_w / 2, state->display_h / 2);

	} else {

		SDL_WarpMouseInWindow(state->gWindow, state->mouse_bak_x, state->mouse_bak_y);

		SDL_DestroyWindow(state->gWindow);
		SDL_DestroyRenderer(state->gRenderer);

		state->gWindow   = NULL;
		state->gRenderer = NULL;
	}

	return 0;
}

/* hotkey_fn_quit : toggles the availabliliy of the other hotkeys */
s32 hotkey_fn_quit(struct state_t *state)
{
	assert(state);
	state->is_running = 0;
	return 0;
}

/* hotkey_fn_say : says the selected macro */
s32 hotkey_fn_say(struct state_t *state)
{
	INPUT *inputs;
	size_t inputs_len, inputs_cap;
	char *s;
	s32 i, slen;
	u16 scan;
	s8 vk, sk;
	u32 rc;

	// NOTE (brian): We literally just send every possible keystroke into the
	// keyboard input queue. Because of the way the KEYBDINPUT function works
	// we need to
	//
	// https://docs.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-input
	// https://docs.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-keybdinput

	assert(state);
	assert(state->lines);

	s = state->lines[state->curr];
	slen = strlen(s);

	inputs = NULL;
	inputs_len = inputs_cap = 0;

#if 1
	sendkey_single('T');
	// sendkey_single('Y');
#else
	sendkey_single(VK_RETURN);
#endif

	// this 50 ms wait time lets chat boxes open and shit
	Sleep(50);

	// TODO (brian): configurable way to change what this key is
	C_RESIZE(&inputs);

	// We add an event for the KEYDOWN and KEYUP.
	for (i = 0; i < slen; i++) {
		// convert our ascii character into a virtual scancode
		// NOTE (brian): i'm not entirely certain that the scancode part of this
		// result is useful to us. There's some more research to be done there.
		// I suppose it's the same in the mk_kbdinput function.
		scan = VkKeyScanA(s[i]);
		vk = scan;
		sk = scan >> 8;

		// This conversion function is also somewhat in-flux.

		if (sk & 0x01) { // if shift _should_ be pushed
			C_RESIZE(&inputs);
			mk_kbdinput(inputs + inputs_len, VK_LSHIFT, 0, 0);
			inputs_len++;
		}

		// down
		C_RESIZE(&inputs);
		mk_kbdinput(inputs + inputs_len, vk, 0, 0);
		inputs_len++;

		// up
		C_RESIZE(&inputs);
		mk_kbdinput(inputs + inputs_len, vk, 0, 1);
		inputs_len++;

		if (sk & 0x01) { // if shift _should_ be pushed
			C_RESIZE(&inputs);
			mk_kbdinput(inputs + inputs_len, VK_LSHIFT, 0, 1);
			inputs_len++;
		}
	}

	// add in an "ENTER" push
	mk_kbdinput(inputs + inputs_len, VK_RETURN, sk, 0);
	inputs_len++;
	C_RESIZE(&inputs);

	rc = SendInput(inputs_len, inputs, sizeof(INPUT));
	if (rc != inputs_len) {
		ERR("Only put %d items on the keyboard queue\n", rc);
	}

	free(inputs);

	return 0;
}

/* sendkey_single : sends a single key */
s32 sendkey_single(s32 keycode)
{
	INPUT input;
	mk_kbdinput(&input, keycode, 0, 0);
	SendInput(1, &input, sizeof(INPUT));
	return 0;
}

/* macros_read : parse macros from the input file to the state */
s32 macros_read(struct state_t *state, char *fname)
{
	FILE *fp;
	char *s;
	char buf[BUFLARGE];

	// NOTE (brian): read lines of text from the filename, and put them into the state structure

	assert(state);
	assert(fname);

	memset(state, 0, sizeof(*state));

	fp = fopen(fname, "r");
	if (!fp)
		return -1;

	while (buf == fgets(buf, sizeof buf, fp)) {
		s = rtrim(buf);
		C_RESIZE(&state->lines);
		state->lines[state->lines_len++] = strdup(s);
	}

	fclose(fp);

	return 0;
}

/* sys_lasterror : handles errors that aren't propogated through win32 errno */
static void sys_lasterror()
{
	DWORD error;
	char *errmsg;

	error = GetLastError();

	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&errmsg, 0, NULL);

	ERR("%s", errmsg);

	LocalFree(errmsg);
}

/* mk_kbdinput : helper function to fill in an INPUT structure for a keyboard */
void mk_kbdinput(INPUT *input, s16 vk, s16 sk, s32 key_up)
{
	if (!input)
		return;

	input->type = INPUT_KEYBOARD;
	input->ki.wVk = vk;
	input->ki.wScan = sk;
	input->ki.dwFlags = key_up ? KEYEVENTF_KEYUP : 0;
	input->ki.time = 0;
	input->ki.dwExtraInfo = 0;
}

/* Win32_CustomEventHandler : custom event handler to grab windows events in sdl */
void Win32_CustomEventHandler(void *user, void *hwnd, u32 message, u64 wparam, s64 lparam)
{
	struct hotkey_event_arg_t *hkarg;

	assert(user);
	hkarg = user;

	if (message == WM_HOTKEY && !hkarg->state->is_active) {
		hkarg->hotkeys[wparam].func(hkarg->state);
	}
}

/* Win32_MakeWindowTransparent : makes the sdl window transparent */
int Win32_MakeWindowTransparent(SDL_Window *window, u8 r, u8 g, u8 b)
{
	HWND hwnd;

	hwnd = Win32_GetWindowHWND(window);

	SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE)|WS_EX_LAYERED);

	return SetLayeredWindowAttributes(hwnd, RGB(r, g, b), 0, LWA_COLORKEY);
}

/* Win32_GetWindowHWND : get the window handle */
HWND Win32_GetWindowHWND(SDL_Window *window)
{
	SDL_SysWMinfo wminfo;

	SDL_VERSION(&wminfo.version);
	SDL_GetWindowWMInfo(window, &wminfo);

	return wminfo.info.win.window;
}

