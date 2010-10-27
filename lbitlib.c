/*
** $Id: lbitlib.c,v 1.8 2010/10/25 20:31:11 roberto Exp roberto $
** Standard library for bitwise operations
** See Copyright Notice in lua.h
*/

#define lbitlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


/* number of bits considered when shifting/rotating (must be a power of 2) */
#define NBITS	32


typedef LUA_INT32 b_int;
typedef unsigned LUA_INT32 b_uint;


#define getuintarg(L,arg)	luaL_checkunsigned(L,arg)


static b_uint andaux (lua_State *L) {
  int i, n = lua_gettop(L);
  b_uint r = ~(b_uint)0;
  for (i = 1; i <= n; i++)
    r &= getuintarg(L, i);
  return r;
}


static int b_and (lua_State *L) {
  b_uint r = andaux(L);
  lua_pushunsigned(L, r);
  return 1;
}


static int b_test (lua_State *L) {
  b_uint r = andaux(L);
  lua_pushboolean(L, r != 0);
  return 1;
}


static int b_or (lua_State *L) {
  int i, n = lua_gettop(L);
  b_uint r = 0;
  for (i = 1; i <= n; i++)
    r |= getuintarg(L, i);
  lua_pushunsigned(L, r);
  return 1;
}


static int b_xor (lua_State *L) {
  int i, n = lua_gettop(L);
  b_uint r = 0;
  for (i = 1; i <= n; i++)
    r ^= getuintarg(L, i);
  lua_pushunsigned(L, r);
  return 1;
}


static int b_not (lua_State *L) {
  b_uint r = ~getuintarg(L, 1);
  lua_pushunsigned(L, r);
  return 1;
}


static int b_shift (lua_State *L, b_uint r, int i) {
  if (i < 0) {  /* shift right? */
    i = -i;
    if (i >= NBITS) r = 0;
    else r >>= i;
  }
  else {  /* shift left */
    if (i >= NBITS) r = 0;
    else r <<= i;
  }
  lua_pushunsigned(L, r);
  return 1;
}


static int b_lshift (lua_State *L) {
  return b_shift(L, getuintarg(L, 1), luaL_checkint(L, 2));
}


static int b_rshift (lua_State *L) {
  return b_shift(L, getuintarg(L, 1), -luaL_checkint(L, 2));
}


static int b_arshift (lua_State *L) {
  b_uint r = getuintarg(L, 1);
  int i = luaL_checkint(L, 2);
  if (i < 0 || !(r & 0x80000000))
    return b_shift(L, r, -i);
  else {  /* arithmetic shift for 'negative' number */
    if (i >= NBITS) r = 0xffffffff;
    else
      r = (r >> i) | ~(~(b_uint)0 >> i);  /* add signal bit */
    lua_pushunsigned(L, r);
    return 1;
  }
}


static int b_rot (lua_State *L, int i) {
  b_uint r = getuintarg(L, 1);
  i &= (NBITS - 1);  /* i = i % NBITS */
  r = (r << i) | (r >> (NBITS - i));
  lua_pushunsigned(L, r);
  return 1;
}


static int b_rol (lua_State *L) {
  return b_rot(L, luaL_checkint(L, 2));
}


static int b_ror (lua_State *L) {
  return b_rot(L, -luaL_checkint(L, 2));
}


static const luaL_Reg bitlib[] = {
  {"AND", b_and},
  {"TEST", b_test},
  {"OR", b_or},
  {"XOR", b_xor},
  {"NOT", b_not},
  {"SHL", b_lshift},
  {"SAR", b_arshift},
  {"SHR", b_rshift},
  {"ROL", b_rol},
  {"ROR", b_ror},
  {NULL, NULL}
};



LUAMOD_API int luaopen_bit32 (lua_State *L) {
  luaL_newlib(L, bitlib);
  return 1;
}
