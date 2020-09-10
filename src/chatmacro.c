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
 * NOTE
 *
 * TODO
 * 1. Minimize to Tray (Not Console Application)
 * 2. Redirect stdout/stderr to a log file
 * 3. Overlay Window
 * 4. Shuffle Button
 * 5. Start Applications (Custom Run Dialog for Specially Hooked up Programs??)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define COMMON_IMPLEMENTATION
#include "common.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MACRO_FILE ("macros.txt")

struct bank_t {
	char *name;
	char **lines;
	size_t lines_len, lines_cap;
	s32 curr;
};

struct state_t {
	struct bank_t *banks;
	size_t banks_len, banks_cap;
	s32 curr;
	s32 s_bank;
	s32 s_macro;
	s32 quit;
};

// NOTE (brian): the first three parameters are passed directly to RegisterHotkey
struct hotkey_t {
	u32 modifiers;
	u32 vk;
	s8 on_always;
	s8 on_now;
	s8 arg1;
	s8 arg2;
	s32 (*func)(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx);
};

/* sys_lasterror : handles errors that aren't propogated through win32 errno */
static void sys_lasterror();

/* macros_parse : parse macros from the input file to the state */
s32 macros_parse(struct state_t *state, char *fname);
/* state_dump : dumps the state of the 'state' object */
s32 state_dump(struct state_t *state);

/* hotkey_fn_toggle : toggles the availabliliy of the other hotkeys */
s32 hotkey_fn_toggle(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx);
/* hotkey_fn_quit : toggles the availabliliy of the other hotkeys */
s32 hotkey_fn_quit(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx);
/* hotkey_fn_swap : swaps between items */
s32 hotkey_fn_swap(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx);
/* hotkey_fn_macro : swaps between macros in the bank */
s32 hotkey_fn_macro(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx);
/* hotkey_fn_say : says the selected macro */
s32 hotkey_fn_say(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx);

/* mk_kbdinput : helper function to fill in an INPUT structure for a keyboard */
void mk_kbdinput(INPUT *input, s16 vk, s16 sk, s32 key_up);
/* sendkey_single : sends a single key */
s32 sendkey_single(s32 keycode);

int main(int argc, char **argv)
{
	struct state_t state;
	s32 i, rc;
	MSG msg;

	struct hotkey_t hotkeys[] = {
		  { 0x4000, VK_NUMPAD0, 1, 1,  0,  0, hotkey_fn_toggle }
		, { 0x4000, VK_DECIMAL, 1, 1,  0,  0, hotkey_fn_quit }
		, { 0x4000, VK_NUMPAD1, 0, 0, -1,  0, hotkey_fn_swap } // bank  -1
		, { 0x4000, VK_NUMPAD2, 0, 0,  1,  0, hotkey_fn_swap } // bank  +1
		, { 0x4000, VK_NUMPAD4, 0, 0,  0, -1, hotkey_fn_swap } // macro -1
		, { 0x4000, VK_NUMPAD5, 0, 0,  0,  1, hotkey_fn_swap } // macro +1
		, { 0x4000, VK_NUMPAD8, 0, 0,  0,  0, hotkey_fn_say } // prints the macro
	};

	memset(&msg, 0, sizeof msg);
	memset(&state, 0, sizeof state);

	rc = macros_parse(&state, MACRO_FILE);
	if (rc < 0) {
		ERR("Couldn't parse macro file!\n");
		exit(1);
	}

	// turn on all of the hotkeys that are "always on"
	for (i = 0; i < ARRSIZE(hotkeys); i++) {
		if (hotkeys[i].on_always) {
			rc = RegisterHotKey(NULL, i, hotkeys[i].modifiers, hotkeys[i].vk);
			if (!rc) {
				ERR("Couldn't Register Hotkey %d :(\n", i);
				exit(1);
			}
		}
	}

	while (!state.quit && GetMessage(&msg, NULL, 0, 0) != 0) {
		switch (msg.message) {
		case WM_HOTKEY:
			hotkeys[msg.wParam].func(&state, hotkeys, ARRSIZE(hotkeys), msg.wParam);
			break;
		}
	}

	// turn off all of the hotkeys
	for (i = 0; i < ARRSIZE(hotkeys); i++) {
		if (hotkeys[i].on_now) {
			rc = UnregisterHotKey(NULL, i);
			if (!rc) {
				ERR("Couldn't Unregister Hotkey %d :(\n", i);
				exit(1);
			}
		}
	}

	return 0;
}

/* hotkey_fn_toggle : toggles the availabliliy of the other hotkeys */
s32 hotkey_fn_toggle(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx)
{
	s32 i, rc;

	// NOTE (brian): toggle all of the hotkeys that aren't supposed to be kept on

	for (i = 0; i < len; i++) {
		if (i == idx)
			continue;

		if (hotkeys[i].on_always)
			continue;

		if (!hotkeys[i].on_now) {
			rc = RegisterHotKey(NULL, i, hotkeys[i].modifiers, hotkeys[i].vk);
		} else {
			rc = UnregisterHotKey(NULL, i);
		}

		hotkeys[i].on_now = !hotkeys[i].on_now;

		if (!rc) {
			sys_lasterror();
			ERR("Couldn't Toggle Hotkey %d :(\n", i);
		}
	}

	return 0;
}

/* hotkey_fn_quit : toggles the availabliliy of the other hotkeys */
s32 hotkey_fn_quit(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx)
{
	state->quit = 1;
	return 0;
}

/* hotkey_fn_swap : swaps between banks */
s32 hotkey_fn_swap(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx)
{
	s32 b, m;
	struct bank_t *lbank;

	b = hotkeys[idx].arg1;
	m = hotkeys[idx].arg2;

	// We attempt to apply both motions. Unless the values in the array are not quite what we
	// expect, we'll end up adding zero and whatnot.
	//
	// At the very least, this gives the "swapping" function a very constant time.

	// Handle the Bank Swap First
	state->curr += b;
	if (state->curr < 0) {
		state->curr = state->banks_len - 1;
	}
	if (state->banks_len <= state->curr) {
		state->curr = 0;
	}

	// Then the Macro Clamping
	lbank = state->banks + state->curr;
	lbank->curr += m;
	if (lbank->curr < 0) {
		lbank->curr = lbank->lines_len - 1;
	}
	if (lbank->lines_len <= lbank->curr) {
		lbank->curr = 0;
	}

	return 0;
}

/* hotkey_fn_say : says the selected macro */
s32 hotkey_fn_say(struct state_t *state, struct hotkey_t *hotkeys, s32 len, s32 idx)
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

	s = state->banks[state->curr].lines[state->banks[state->curr].curr];
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

/* macros_parse : parse macros from the input file to the state */
s32 macros_parse(struct state_t *state, char *fname)
{
	FILE *fp;
	struct bank_t *lbank;
	char *s;
	char buf[BUFLARGE];

	// NOTE (brian):
	//
	// The macro file format is as follows:
	//
	// Example:
	//
	// BankFoo
	//     You're trash.
	//     I'd say you were cancer, but cancer wins sometimes.
	//
	// BankBar
	//     glhf
	//     Good Luck Having Fun
	//
	// That gets parsed into two banks, with two macros a piece

	memset(state, 0, sizeof(*state));

	fp = fopen(fname, "r");
	if (!fp)
		return -1;

	while (buf == fgets(buf, sizeof buf, fp)) {
		s = rtrim(buf);

		switch (s[0]) {
			case '\0':
			case '#':
				continue;

			default: // new bank
				C_RESIZE(&state->banks);
				state->banks[state->banks_len].name = strdup(s);
				state->banks[state->banks_len].curr = 0;
				state->banks[state->banks_len].lines_len = 0;
				state->banks[state->banks_len].lines_cap = 0;
				state->banks_len++;
				state->curr = state->banks_len - 1; // use curr in the next case
				break;

			case '\t': // new macro in the bank
				s = ltrim(buf);
				C_RESIZE(&state->banks[state->curr].lines);
				lbank = state->banks + state->curr;
				lbank->lines[lbank->lines_len++] = strdup(s);
				break;
		}
	}

	// reset curr to the first bank, like we expect
	state->curr = 0;

	fclose(fp);

	return 0;
}

/* state_dump : dumps the state of the 'state' object */
s32 state_dump(struct state_t *state)
{
	s32 i, j;

	if (!state) {
		return -1;
	}

	printf("state.curr    : %d\n", state->curr);
	printf("state.s_bank  : %d\n", state->s_bank);
	printf("state.s_macro : %d\n", state->s_macro);
	printf("state.quit    : %d\n", state->quit);

	for (i = 0; i < state->banks_len; i++) {
		printf("%s\n", state->banks[i].name);
		for (j = 0; j < state->banks[i].lines_len; j++) {
			printf("\t%s\n", state->banks[i].lines[j]);
		}
	}

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

