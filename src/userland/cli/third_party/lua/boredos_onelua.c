/*
** BoredOS single-file Lua 5.5.0 build
** Based on the official onelua.c, adapted for BoredOS freestanding environment.
*/

// BOREDOS_APP_DESC: Lua REPL and scripting runtime.
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "stdlib.h"
#include "string.h"
#include "syscall.h"
#include "math.h"
#include "boredos_stubs.h"

/* Pre-define l_signalT so lstate.h won't #include <signal.h> */
#define l_signalT   volatile int

#define LUA_CORE
#define LUA_LIB
#define MAKE_LUA
#undef LUA_USE_LINUX
#undef LUA_USE_POSIX
#undef LUA_USE_DLOPEN
#undef LUA_USE_WINDOWS
#ifndef LUA_USE_C89
#define LUA_USE_C89
#endif
#undef lua_getlocaledecpoint
#define lua_getlocaledecpoint() '.'
#undef lua_writestring
#undef lua_writeline
#undef lua_writestringerror
#define lua_writestring(s, l) sys_write(1, (s), (int)(l))
#define lua_writeline()       sys_write(1, "\n", 1)
static void _boredos_lua_writeerror(const char *fmt, ...) {
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    if (len > 0) sys_write(1, buf, len > 512 ? 512 : len);
}
#define lua_writestringerror(s, p) _boredos_lua_writeerror(s, p)
#undef lua_stdin_is_tty
#define lua_stdin_is_tty() 1
#undef LUA_USE_READLINE
#undef LUA_READLINELIB
#include "lprefix.h"
#include "luaconf.h"
#undef LUAI_FUNC
#undef LUAI_DDEC
#undef LUAI_DDEF
#define LUAI_FUNC   static
#define LUAI_DDEC(def)  
#define LUAI_DDEF   static
#include "lzio.c"
#include "lctype.c"
#include "lopcodes.c"
#include "lmem.c"
#include "lundump.c"
#include "ldump.c"
#include "lstate.c"
#include "lgc.c"
#include "llex.c"
#include "lcode.c"
#include "lparser.c"
#include "ldebug.c"
#include "lfunc.c"
#include "lobject.c"
#include "ltm.c"
#include "lstring.c"
#include "ltable.c"
#include "ldo.c"
#include "lvm.c"
#include "lapi.c"
#include "lauxlib.c"
#include "lbaselib.c"
#include "lcorolib.c"
#include "ldblib.c"
#include "lmathlib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lutf8lib.c"
#include "boredos_lua_io.c"
#include "boredos_lua_os.c"
#include "boredos_lua_loadlib.c"
#include "linit.c"
#include "boredos_lua_main.c"
