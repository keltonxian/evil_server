
#include <iostream>
#include <vector>
#include <string>
// #include <ostream>

using namespace std;

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
// socket related
#include <netdb.h> 	// hostname lookup @see prepare_addr
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

// lua specific
// #include "lua.h"  // optional
#include "lualib.h" // implicit: luaconf.h
#include "lauxlib.h"

// void luaL_openlibs(lua_State *);  // peter: copy somewhere else

// user defined header
#include "fatal.h"
}

#include "evil.h"

// hard-coded room_t for client, trim down version
#define MAX_GUEST	10
typedef struct {
	// note: state == 999 is a bug!
	int state;	// 0=free,  ST_ROOM=(wait for guest), ST_GAME=started game
	int next_free;	
	int next_used; // -1 initially
	int prev_used; // -1 init
	int channel; 	// @see init_room_list for next_free, next/prev_used, channel
	int rid;  // g_room_list[channel][rid] == rid @see init_room_list
	time_t create_time; // TODO room creation time
	time_t last_active[MAX_GUEST];  // LATER
	int guest[MAX_GUEST];  // guest[0] is the room master
	char guest_alias[MAX_GUEST][EVIL_ALIAS_MAX + 11];  // default to room master alias, can be anything
	int num_guest;  
	char title[EVIL_ALIAS_MAX + 11];  // default to room master alias, can be anything
	int seed;  // seed = 0 means not started, s
	vector<string> cmd_list;

	// re-conn will broadcast all play_list
} client_room_t;  // avoid confusion from server

//////// GAME GLOBAL START /////////
lua_State * g_lua = NULL;
int g_lang = 1;
int	g_my_side = 0;
int g_st = 0;
int g_cmd_size = 0;
evil_user_t g_euser;  // @see main() bzero
client_room_t g_room;
int g_pick_list[MAX_LOC];
int g_pick_sum = 0;


#define AUTO_NEXT	9
// auto 
int g_auto_winner = 0;
int g_auto_count = 0; // record num of game
int g_auto_stat[1000];   // record winner of N game

// these globals are optional, since lua VM already save them
int g_seed = 0;
char g_deck0[EVIL_CARD_MAX+1]; // +1 for null-term
char g_deck1[EVIL_CARD_MAX+1]; // +1 for null-term
char g_card[EVIL_CARD_MAX+1] = {0};  // +1 for null-term
char ipaddress[100];

//////// GAME GLOBAL END /////////



#define ST_NULL		0
#define ST_LOGIN	5
#define ST_ROOM		10
#define ST_GAME		15
#define ST_REPLAY	80	// only client use this
#define ST_AUTO		90	// only client use this

// peter: must be large enough to receive all server data in one batch
#define BUFFER_SIZE 	8000


///////////////////////////////
////////// LUA START //////////
///////////////////////////////
// note: mylua_xxx = my lua function (different from the lua library)

//	after open, do : MYLUA_PUSH_MAGIC(lua);  // push magic
#define MYLUA_CHECK_MAGIC(L)	do { \
if (NULL!=(L)) { ERROR_PRINT(1974-(int)lua_tointeger(L, -1), "MYLUA_CHECK_MAGIC"); }\
} while (0);
#define MYLUA_PUSH_MAGIC(L) do { lua_pushinteger((L), 1974); } while (0);


// XXX beware, need to pop after use
const char* mylua_get_string(lua_State *L, const char * var) {
	lua_getglobal(L, var); // read from global with name [var], value in stack
	const char*  sss;
	size_t len = 0;
	sss = lua_tolstring(L, -1, &len); // -1 is the first in stack
//	printf("CLUA : (MUSTPOP) get_str [%s](%s)\n", sss, var);
	return sss;
}


int mylua_set_string(lua_State *L, const char * var, const char * str) {
	lua_pushstring(L, str);
	lua_setglobal(L, var);
//	printf("CLUA : set_str %s=[%s]\n", var, str);
	return 0;
}


int mylua_get_int(lua_State *L, const char * var) {
	int value;
	lua_getglobal(L, var); // read from global with name [var], value in stack
	value = lua_tonumber(L, -1); // -1 is the first in stack
	lua_pop(L, 1);
//	printf("CLUA : get_int : %d(%s)\n", value, var);
	return value;
}


int mylua_set_int(lua_State *L, const char * var, int value) {
	lua_pushinteger(L, value);
	lua_setglobal(L, var);
//	printf("CLUA : set_int : %s=%d\n", var, value);
	return 0;
}


// t=table, i=index, n=name
// return table[index].name  as string
// caller need to allocate enough space for str, e.g. char str[max]
// if return NULL, str is not used
const char* mylua_get_table_index_name(lua_State *L, char *str, int max
, const char *table, int index, const char *name) {
	int ret;
	const char *ptr;
	char fmt[10];
	size_t size = 0;
	lua_getglobal(L, table); 
	ret = lua_type(L, -1);
	if (ret != LUA_TTABLE) {
		BUG_PRINT(-6, "getglobal: %s type=%d", table, ret);
		lua_pop(L, 1); // remove the get global
		return NULL; 
	}

	lua_rawgeti(L, -1, index); // rusty sword test
	ret = lua_type(L, -1);
	if (ret != LUA_TTABLE) {
		// this can be warning later
		BUG_PRINT(-16, "rawgeti: %d type=%d", index, ret);
		lua_pop(L, 2);
		return NULL;
	}

	lua_getfield(L, -1, name);
	ret = lua_type(L, -1);
	if (ret != LUA_TSTRING) {
		BUG_PRINT(-26, "getfield: %s type=%d", name, ret);
		lua_pop(L, 3);
		return NULL;
	}
	ptr = lua_tolstring(L, -1, &size);
	if (ptr == NULL) {
		BUG_PRINT(-36, "tolstring_null: %s[%d].%s", table, index, name);
		lua_pop(L, 3);
		return NULL;
	}
	sprintf(fmt, "%%.%ds", max-1);
	sprintf(str, fmt, ptr);
	lua_pop(L, 3); // balance the lua stack
	return str;
}


// TODO  get: table[index] return on str
const char* mylua_get_table_index(lua_State *L, char *str, int max
, const char *table, int index) {
	// not yet implement
	return NULL;
}


// handly lua new state
// use standard lua_close(L) to close it
lua_State * mylua_open(int lang)
{
	lua_State * lua;
	int ret;
	lua = luaL_newstate();	// global access
	luaL_openlibs(lua);  // peter: copy somewhere else
	mylua_set_int(lua, "g_ui", 1);
	ret = luaL_dofile(lua, "logic.lua");  // LANG was logic.lua
	FATAL_EXIT(ret, "mylua_open:dofile:logic.lua");
	if (lang == 1) {
		ret = luaL_dofile(lua, "lang_zh.lua");  // LANG was logic.lua
	}
	MYLUA_PUSH_MAGIC(lua);
//	lua_pushinteger(lua, 1974);  // push magic
	return lua;
}

///////// LUA END /////////


////////// LUA LOGIC START /////////

// obsolete: pls use mylua_print_index_both() 
int mylua_print_both_side(lua_State *L) 
{
	int ret;
	lua_getglobal(L, "print_both_side");	
	lua_getglobal(L, "g_logic_table");
	// (lua_state, num_param, num_return, ???)
	ret = lua_pcall(L, 1, 0, 0);  // expect: ret=0
	// peter: do we need to remove 2 elements from stack ?
	ERROR_NEG_PRINT(ret, "lua_print_both_side");
	return ret;
}

int mylua_print_index_both(lua_State *L) 
{
	int ret;
	lua_getglobal(L, "print_index_both");	
	lua_getglobal(L, "g_logic_table");
	// (lua_state, num_param, num_return, ???)
	ret = lua_pcall(L, 1, 0, 0);  // expect: ret=0
	// peter: do we need to remove 2 elements from stack ?
	ERROR_NEG_PRINT(ret, "mylua_print_index_both");
	return ret;
}

int mylua_print_status(lua_State *L) 
{
	int ret;
	lua_getglobal(L, "print_status");	
	lua_pushnumber(L, g_my_side);
	ret = lua_pcall(L, 1, 0, 0);  // expect: ret=0
	return ret;
/*
	int current_side;
	int current_phase;
	current_side = get_int(L, "g_current_side");
	current_phase = get_int(L, "g_phase");
	printf("STATUS:  g_current_side=%d  g_phase=%d  g_my_side(C)=%d\n"
	, current_side, current_phase, g_my_side);
	*/
}

static int mylua_traceback (lua_State *L) {
	printf("-------- mylua_traceback:\n");
	if (!lua_isstring(L, 1)) { /* 'message' not a string? */
		printf("BUGBUG mylua_traceback stack 1 not a string\n");
		return 1;  /* keep it intact */
	}
	lua_getfield(L, LUA_GLOBALSINDEX, "debug");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 1;
	}
	lua_getfield(L, -1, "traceback");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		return 1;
	}
	lua_pushvalue(L, 1);  /* pass error message */
	lua_pushinteger(L, 2);  /* skip this function and traceback */
	lua_call(L, 2, 1);  /* call debug.traceback */
	return 1;
}

int mylua_trace_call(lua_State *L, int narg, int result)
{
	int status;
	int base = lua_gettop(L) - narg - 1;
	lua_pushcfunction(L, mylua_traceback);
	lua_insert(L, base);
	// LUA_MULTRET : multiple result, lua let the function push N result
	// it will not enforce to push N result
	status = lua_pcall(L, narg, (result==0 ? 0 : LUA_MULTRET), base);
	lua_remove(L, base);  
//	if (status != 0) {	// force garbage collection in case of error
//		lua_gc(L, LUA_GCCOLLECT, 0);
//	}
	return status;
}


/**
 * @return <0 for error,  =0 not yet have winner, >0 means winner
 */
int mylua_play_cmd_global(lua_State *L, const char * buffer) 
{
	int ret;
	int winner;
	lua_getglobal(L, "play_cmd_global");	
	printf("C: -- play_cmd_global : (%s)\n", buffer);
	lua_pushstring(L, buffer); 
	// (lua_state, num_param, num_return, ???)
	ret = lua_pcall(L, 1, 4, 0);  // balance AAA, expect: ret=0
	ERROR_NEG_RETURN(ret, "lua_play_cmd_global_111");  // ret != 0, print error
	// peter: do we need to remove 2 elements from stack ?
//	ret = lua_tonumber(g_lua, -4);  // err return  :  tonumber may bug!


	// err is a string means there is an error
	if (lua_isstring(L, -1)) {
		ret = -1;
		// printf("err: [%s]\n", lua_tostring(L, -1)) ;
	} else {
		ret = 0; // normal
		// printf("err: is non-string\n");
	}
	winner = lua_tointeger(L, -4);  // get the winner

	lua_pop(L, 4 );  // balance AAA,   and 4 parameters
	// do we need to pop 4 times ?
	ERROR_NEG_RETURN(ret, "lua_play_cmd_global_222");
	return winner;
}

////////// LUA LOGIC END /////////


///////// UTILITY START /////////

static int trim(char * str, int max)
{
	if (max <= 0) {
		max = strlen(str);
	}

	int i;
	for (i=max-1; i>=0; i--) {
		if (str[i]=='\r' || str[i]=='\n' || str[i]==' ')  {
			str[i] = 0;  // null it
			max --;
		} else {
			return max;
		}
	}
	return max;
}

// domain: can be domain name or IP address
struct sockaddr_in prepare_addr(const char *hostname, int port)
{
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family    	= AF_INET;
	sin.sin_port      	= htons(port);
    int ip = inet_addr(hostname);
    if (ip==-1)
    {
        struct hostent *hent;
        hent = gethostbyname(hostname);
        if (hent==NULL) {
            // printf("ERROR prepare_addr: gethostbyname fail\n");
            ip = 0xffffffff; // 8 x f
        } else {
            ip = *(int *)(hent->h_addr_list[0]);
        }
        printf("DEBUG prepare_addr: resolve ip=%d.%d.%d.%d\n", ip & 0xff
               , (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff);
    }
	sin.sin_addr.s_addr = ip;

	// note: assume sockaddr, sockaddr_in same len
	return sin;
}


int sock_write(int sock, const char * fmt, ...) 
{
//	int n;
	char buffer[BUFFER_SIZE + 1]; 
    va_list argptr;
    va_start(argptr, fmt);
//    n = vsnprintf(buffer, BUFFER_SIZE, fmt, argptr);
    vsnprintf(buffer, BUFFER_SIZE, fmt, argptr);
    va_end(argptr);
//	buffer[n] = last;
//	buffer[n+1] = 0;
//	strcat(buffer, "\n");

	int offset = 0;
	int len = strlen(buffer);
	int ret = 0;
	int count = 0;
	printf("SEND(%3d): %s", len, buffer);
	while (offset < len) {
		ret = write(sock, buffer+offset, (len-offset));
		if (ret < 0)  {
			printf("BUG write ret %d\n", ret);
			return -5;
		}
		offset += ret;
	}
	
	if (count > 1) {
		printf("DEBUG sock_write split write into %d\n", count);
	}
	return len;
}
///////// UTILITY END /////////


/*  pcard
RECV(415): pcard 1 400 1 0000000100000000000002200321030000000000000000000000000000000000000000000000000000000000002202212021000000000000000000000000000000111110000000000000002002200000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
pcard 2
SEND(  8): pcard 2
RECV(415): pcard 0 400 1 1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
*/

// function logic_init(hero1, hero2, deck1, deck2, seed, start_side) -- {
// function logic_init_array(deck1_array, deck2_array, seed, start_side) -- {
// deck1, deck2 are 400 character array
int mylua_logic_init(lua_State *L, const char *deck1, const char *deck2
, int seed, int start_side)
{
	lua_getglobal(L, "logic_init_array");
	lua_pushstring(L, deck1);
	lua_pushstring(L, deck2);
	lua_pushinteger(L, seed);
	lua_pushinteger(L, start_side);
	int ret;
	ret = lua_pcall(L, 4, 1, 0);

	if (ret != 0) {
		lua_pop(L, 1);  // skip the handler
		ERROR_RETURN(-8, "mylua_logic_init:pcall %d", ret);
	}

	ret = lua_tointeger(L, -1);
	lua_pop(L, 1);

	DEBUG_PRINT(ret, "mylua_logic_init");

	return ret;
}

// peter: deck1, deck2 are unified form
// form1: deck400
// form2: [hero_id] [c1] [c2] [c3] ...
// no need to pass hero_id 
int lu_solo_plus_init(lua_State *L, const char *deck1, const char *deck2, int solo_type, int max_ally, int max_hp, int myhero_hp, int myhero_energy, const char *solo_type_list, int seed)
{
	lua_getglobal(L, "solo_init_array"); 
	lua_pushstring(L, deck1);
	lua_pushstring(L, deck2);
	lua_pushinteger(L, solo_type);
	lua_pushinteger(L, max_ally);
	lua_pushinteger(L, max_hp);
	lua_pushinteger(L, myhero_hp);
	lua_pushinteger(L, myhero_energy);
	lua_pushstring(L, solo_type_list);
	lua_pushinteger(L, seed);
	int ret;
	ret = lua_pcall(L, 9, 1, 0);
	DEBUG_PRINT(0, "lu_solo_plus_init:ret=%d", ret);

	if (ret != 0) {
		lua_pop(L, 1);  // skip the handler
		ERROR_PRINT(-8, "lu_solo_plus_init:pcall ret=%d  seed=%d", ret, seed);
		return -8;
	}

	ret = lua_tointeger(L, -1);
	lua_pop(L, 1);

	MYLUA_CHECK_MAGIC(g_lua);

	return ret;  // always zero now, TODO card_array_list may return err
}


void sys_shutdown()
{
	printf("QUIT sleep\n");
	fflush(stdout);
	// usleep(500000); // 0.5 sec
	exit(0);
}



// TODO 
// game [side] [cmd_size] [seed] [timeout] [deck_400_0] [deck_400_1]
// init local deck0, deck1 to lua
int game_start(int sock, const char *buffer) 
{
	int ret;
	int side = 0;
	int cmd_size = 0;
	char cmd[21];
	int seed = 777;
	int start_side = 1;
	int timeout = 0; // count down
	char deck0[EVIL_CARD_MAX + 1];
	char deck1[EVIL_CARD_MAX + 1];
	int n;
	// safety
	bzero(deck0, EVIL_CARD_MAX+1);
	bzero(deck1, EVIL_CARD_MAX+1);

	ret = sscanf(buffer, "%20s %d %n", cmd, &side, &n);
	if (ret < 2) {
		ERROR_RETURN(-1, "game_not_enough_input_1 %d", ret);
	}
	const char *ptr = buffer + n;
	if (side < 0) {
		ERROR_RETURN(side, "server err: %s", ptr);
	}

	ret = sscanf(ptr, "%d %d %d %s %s", &cmd_size, &seed
	, &timeout, deck0, deck1);

	printf("--- game_start: side=%d  cmd_size=%d  seed=%d  timeout=%d\n"
	, side, cmd_size, seed, timeout);
	printf("--- game_start: deck0=[%s]  deck1=[%s]\n", deck0, deck1);

	if (ret < 4) {
		ERROR_RETURN(-1, "game_not_enough_input_2 %d", ret);
	}
	if (NULL != g_lua) {
		ERROR_RETURN(-6, "g_lua_not_null");
	}

	g_my_side = side;	// global access
	// optional:
	g_seed = seed;
	g_cmd_size = cmd_size;
	memcpy(g_deck0, deck0, EVIL_CARD_MAX+1);
	memcpy(g_deck1, deck1, EVIL_CARD_MAX+1);

	// for debug:
//	printf("game seed [%d]\n", seed);
//	printf("game deck0 [%s]\n", deck0);
//	printf("game deck1 [%s]\n", deck1);

	g_lua = mylua_open(g_lang);

	ret = mylua_logic_init(g_lua, deck0, deck1, seed, start_side);
	ERROR_RETURN(ret, "game_start_mylua_logic_init");

	g_st = ST_GAME; // TODO hard code
	return 0 ;
}


// CMD: solo_plus [solo_id]
// RET: room ...
// CMD: ginfo
// RET: solo_plus [side] [cmd_size] [seed] [timeout] 
// [solo_type] [solo_max_ally] [solo_max_hp] [myhero_hp] [myhero_energy]
// [solo_type_list]
// [card_len1] [deck1] 
// [card_len2] [deck2]
int solo_plus_start(int sock, const char *buffer) 
{
	int ret;
	int side = 0;
	int cmd_size = 0;
	char cmd[21];
	char deck0[EVIL_CARD_MAX + 1];
	char deck1[EVIL_CARD_MAX + 1];
	int n;
	// safety
	bzero(deck0, EVIL_CARD_MAX+1);
	bzero(deck1, EVIL_CARD_MAX+1);

	long timeout = 0;
	int solo_type = 0;
	int solo_max_ally = 0;
	int solo_max_hp = 0;
	int solo_myhero_hp = 0;
	int solo_myhero_energy = 0;
	char solo_type_list[101];
	int card_len1 = 0;
	int card_len2 = 0;

	ret = sscanf(buffer, "%20s %d %d %d %ld %d %d %d %d %d %100s %d %n"
	, cmd
	, &side, &cmd_size, &g_seed, &timeout
	, &solo_type, &solo_max_ally, &solo_max_hp
	, &solo_myhero_hp, &solo_myhero_energy
	, solo_type_list
	, &card_len1, &n);

	if (side < 0) {
		ERROR_RETURN(-5, "solo_plus_start:error_return_side %d", side);
	}

	printf("ret=%d card_len1=%d\n", ret, card_len1);

	const char *ptr = buffer;
	char *ptr_deck = deck0;
	for (int i=0; i<card_len1; i++) {
		int cid = 0;
		ptr+=n;
		ret = sscanf(ptr, "%d %n", &cid, &n);
		if (ret != 1) {
			FATAL_EXIT(-5, "solo_plus_start:deck0_error");
		}
		ptr_deck+=sprintf(ptr_deck, "%d ", cid);	
	}
	printf("deck0=[%s]\n", deck0);

	// TODO handle deck[2] 400
	ptr+=n;
	ptr_deck = deck1;
	sscanf(ptr, "%d %n", &card_len2, &n);
	printf("card_len2=%d\n", card_len2);
	for (int i=0; i<card_len2; i++) {
		int cid = 0;
		ptr+=n;
		ret = sscanf(ptr, "%d %n", &cid, &n);
		if (ret != 1) {
			FATAL_EXIT(-5, "solo_plus_start:deck1_error");
		}
		ptr_deck+=sprintf(ptr_deck, "%d ", cid);	
	}
	printf("deck1=[%s]\n", deck1);

	

// 	, &proom->deck[0]
// 	, &card_len2, &proom->deck[1]);


	printf("--- solo_plus_start: side=%d  cmd_size=%d  g_seed=%d  timeout=%ld\n"
	, side, cmd_size, g_seed, timeout);
	printf("--- solo_plus_start: deck0=[%s]  deck1=[%s]\n", deck0, deck1);

	if (NULL != g_lua) {
		ERROR_RETURN(-6, "g_lua_not_null");
	}

	g_my_side = side;	// global access
	// optional:
	g_cmd_size = cmd_size;
	memcpy(g_deck0, deck0, EVIL_CARD_MAX+1);
	memcpy(g_deck1, deck1, EVIL_CARD_MAX+1);

	// for debug:
//	printf("game seed [%d]\n", seed);
//	printf("game deck0 [%s]\n", deck0);
//	printf("game deck1 [%s]\n", deck1);

	g_lua = mylua_open(g_lang);

	ret = lu_solo_plus_init(g_lua, deck0, deck1, solo_type
	, solo_max_ally, solo_max_hp, solo_myhero_hp, solo_myhero_energy, solo_type_list, g_seed);
	ERROR_RETURN(ret, "solo_plus_start_lua_solo_plus_init");

	g_st = ST_GAME; // TODO hard code
	return 0 ;
}

int game_win(int sock, const char *buffer) 
{
	int ret;
	int win = 0; // 0=invalid,  1 or 2 valid
	int fold_flag = 0;
	char cmd[21];

	DEBUG_PRINT(0, "CMD game_win : %s", buffer);

	ret = sscanf(buffer, "%20s %d %d", cmd, &win, &fold_flag);
	if (ret < 2) {
		ERROR_RETURN(-5, "game_win_input");
	}

	if (win != 1 && win != 2 && win != 9) {
		ERROR_RETURN(-15, "game_win_side %d", win);
	}
	
	if (fold_flag!=0) {
		int lose_side = 3-win;

		if (lose_side != 1 && lose_side != 2) {
			BUG_RETURN(-7, "win_lose_side %d", lose_side);
		}

		printf("FOLD: Side %d [%s] fold(surrender)\n", 
			lose_side, g_room.guest_alias[lose_side-1]);
	}

	// it is in game, so g_lua should not be null
	if (g_lua==NULL) {
		ERROR_RETURN(-7, "game_win_lua_null");
	}

	lua_close(g_lua);
	g_lua = NULL;  // must set to NULL
	DEBUG_PRINT(0, "GAME win %d   fold %d", win, fold_flag);
	// TODO room_clean 
	g_st = ST_LOGIN; // back to normal
	g_room.channel = 0;
	g_room.rid = 0;
	g_room.num_guest = 0;
	g_st = ST_LOGIN; // back to normal
	return 0;
}


int unknown_fun(int sock, const char *buffer) 
{
	printf("--- unknown_fun: do nothing \n");
	return 0;
}

int game_error(int sock, const char *buffer) 
{
	// print the error message from server
	ERROR_PRINT(-1, "%s", buffer);
	return 0;
}

int game_fold(int sock, const char *buffer) 
{
	// TODO close the lua, tell client someone is folded /surrender 
	int side = 0;
	int ret;
	char cmd[21];
	char errmsg[101];
	ret = sscanf(buffer, "%20s %d %80s", cmd, &side, errmsg);
	if (side == 0 || ret<1) {
		ERROR_RETURN(-5, "fold_input");
	}
	if (side < 1 || side > 2) {
		ERROR_RETURN(-6, "fold_error %d %s", side, errmsg);
	}
	DEBUG_PRINT(side, "fold_side");

	g_room.rid = 0; // core logic
	g_room.num_guest = 0; // this is the key
	if (g_lua != NULL) {
		lua_close(g_lua);
		g_lua = NULL;
	}
	g_st = ST_LOGIN;   // hard code, not from server ?

	return -10;
}

// read_loop  -> play_cmd, we need to ignore cmd 
// when g_my_side==g_current_side
// since the command is already executed
int play_cmd(int sock, const char *in_buffer) 
{
	int ret;
	int num;
	int n;
	const char *buffer;
	ret = sscanf(in_buffer, "%d %n", &num, &n);
	if (ret >= 1) {
		buffer = in_buffer + n;
	} else { 
		buffer = in_buffer;
	}
	DEBUG_PRINT(0, "play_cmd: %s", buffer);

	if (NULL==g_lua) {
		ERROR_RETURN(-3, "play_null_lua");
	}



	// client-side checking  
	// TODO after server-side implement checking no need for client-side check
	// if we are on my side, the command is already executed!
	// 'next' is always executed!
	if (g_my_side == mylua_get_int(g_lua, "g_current_side") 
	&& buffer!=strstr(buffer, "n") && g_cmd_size<=0) {
		// g_cmd_size > 0 means this command is from re-conn logic
		return 0;
	}

	// this is used by re-conn logic
	if (g_cmd_size > 0) {
		g_cmd_size --;
	}
	ret = mylua_play_cmd_global(g_lua, buffer);

	// feedback error case
	if (ret < 0 ) {
		ERROR_PRINT(ret, "play_cmd buffer=[%s]\n", buffer);
		return ret;
	}
	return ST_GAME;
}


int client_login(int sock, const char *buffer) 
{
	int ret;
	char cmd[21];
	char alias[EVIL_ALIAS_MAX];
	int eid = 0;

	ret = sscanf(buffer, "%20s %d %d %s", cmd, &eid, &g_st, alias);
	if (ret < 3) {
		ERROR_NEG_RETURN(-5, "login_input_error");
	}
	if (eid == 0) {
		ERROR_NEG_RETURN(-7, "login_eid_zero");
	}
	// normal eid must be > 0
	if (eid < 0) {
		ERROR_NEG_RETURN(eid, "login_error");
	}

	g_euser.eid = eid;
	strcpy(g_euser.alias, alias);  // strcpy for array copy
	DEBUG_PRINT(0, "euser.eid=%d   alias=[%s]  g_st=%d\n"
	, g_euser.eid, g_euser.alias, g_st);

	// g_st may be in ROOM or in GAME, in both case, we should get guest list
	if (g_st >= ST_ROOM) {
		sock_write(sock, "room\n"); // re-conn, get guest list
	}

// this will be triggered in room_join()
//	if (g_st >= ST_GAME) {
//		sock_write(sock, "ginfo\n"); // get the game status : re-conn
//	}
	return g_st; // ST_LOGIN;
}


// leave [eid] [st]
int room_leave(int sock, const char *buffer) 
{
	int ret;
	int eid;
	// int clean_flag;
	int st;
	int n;
	char cmd[21];
	ret = sscanf(buffer, "%20s %d %n", cmd, &eid, &n);
	if (ret < 2) {
		ERROR_RETURN(-5, "leave_input_error");
	}
	if (eid < 0) {  // channel is also the error code (<0)
		ERROR_RETURN(eid, "leave_error");
	}

	// clean_flag, status
	ret = sscanf(buffer+n, "%d", &st);
	if (ret < 1) {
		ERROR_RETURN(-15, "leave_input_error2");
	}

	// need to check whether i am the eid ? TODO  server send st

	// i am leaving, so g_st will be down grade to ST_LOGIN
	// TODO  server send st and client catch up
	if (g_euser.eid == eid)  {
		g_room.rid = 0; // core logic
		g_room.num_guest = 0; // this is the key
		if (g_lua != NULL) {
			lua_close(g_lua);
			g_lua = NULL;
		}
		g_st = st;  
		return 0;
	} 
	
	// other user leave
	
	// TODO room_remove_guest() as a function
	int index = -3;  // null case, eid not found
	for (int i=0; i<g_room.num_guest; i++) {
		if (eid == g_room.guest[i]) {
			index = i;
		}
	}
	ERROR_NEG_RETURN(index, "leave_eid_not_found eid=%d", eid);

	// shift guest to front, after the index
	for (int i=index;  i<(g_room.num_guest-1); i++) {
		g_room.guest[i] = g_room.guest[i+1];
	}
	g_room.num_guest--;

	// note: we do not need
	if (st != g_st) {
		BUG_RETURN(-27, "leave_st_not_match %d %d", st, g_st);
	}
	return 0;
}


// join / lguest may send "st"
// room [channel] [room] [st] [guest0_info] [guest1_info] ...
// guestX_info = [eid alias]
int cmd_room(int sock, const char *buffer) 
{
	int ret;
	char cmd[21];
	// char alias[EVIL_ALIAS_MAX + 1];
	// int eid = 0;
	int room; // room id
	int channel;
	// int join_eid;
	int n;
	int st;
	const char *ptr = NULL;

	// channel is overloaded with error code
	ret = sscanf(buffer, "%20s %d %n", cmd, &channel, &n);
	if (ret < 2) {
		ERROR_RETURN(-5, "room_input_error");
	}
	if (channel < 0) {  // channel is also the error code (<0)
		// TODO print remaining error message
		ERROR_RETURN(channel, "room_error %s", buffer+n);
	}
	if (n < 0) {
		ERROR_RETURN(-7, "room_sscanf_111");
	}

	// e.g. buffer="2 15 88 HELLO"  -> room=2, st=15, n=5(shift 5 chars)
	ptr = buffer + n;
	ret = sscanf(ptr, "%d %d %n", &room, &st, &n);
	if (ret < 2)  {
		ERROR_RETURN(-17, "room_sscanf_222");
	}
	ptr += n; 
	DEBUG_PRINT(0, "cmd_room channel=%d  room=%d  st=%d  g_st=%d"
	, channel, room, st, g_st);

	// XXX shall we check g_room data is refreshed?  
	// any issue if we updated g_room when the game is already started?
	g_room.channel = channel;
	g_room.rid = room;
	g_room.num_guest = 0;
	g_st = st;  // key logic

	for (int i=0; i<10; i++) {
		int guest_eid = 0;
		char guest_alias[EVIL_ALIAS_MAX+1];
		ret = sscanf(ptr, "%d %s %n", &guest_eid, guest_alias, &n);
		if (ret < 2) {
			break; // skip
		}
		ptr += n;
		g_room.num_guest ++;
		g_room.guest[i] = guest_eid;
		strcpy(g_room.guest_alias[i],  guest_alias);
	}
	DEBUG_PRINT(g_room.num_guest, "num_guest");


	// auto-start if i am master, and num_guest >= 2
	// XXX this will break the re-conn logic!
	/**
	if (st<ST_GAME && g_room.guest[0] == g_euser.eid 
	&& g_room.num_guest==2) {
		printf("TRIGGER auto-start game\n");
		sock_write(sock, "game\n");
	}
	**/
	
	//TODO  when game already start, new joiner send ginfo??
	// and re-conn need test!!
	// if we are in ST_GAME, but g_lua is null, the local game
	// context is not initialized, so we need game_start(), call ginfo
	if (st == ST_GAME && g_lua==NULL) {
		sock_write(sock, "ginfo\n");
	}

	return st; // state can be either ST_ROOM or ST_GAME
}

int sys_status(int sock, const char *buffer) 
{
	int ret;
	char cmd[21];
	// char alias[EVIL_ALIAS_MAX + 1];
	int eid = 0;
	int n;

	ret = sscanf(buffer, "%20s %d %n", cmd, &eid, &n);

	if (ret < 2) {
		ERROR_RETURN(-5, "status_input");
	}
	if (eid < 0) {
		WARN_PRINT(-6, "status logic error");
		return eid;
	}

	evil_user_t & user = g_euser;

	// 9 input 
	ret = sscanf(buffer+n, "%d %lf %d %d %d %d %d %d %d"
		, &(user.lv), &(user.rating), &(user.gold), &(user.crystal)
		, &(user.game_count), &(user.game_win), &(user.game_lose)
		, &(user.game_draw), &(user.game_run));

	if (ret < 9) {
		WARN_PRINT(-15, "status_not_enough %d", ret);
	}
	return 0;
}

int cmd_solo(int sock, const char *buffer) 
{
	// nothing to do
	printf("%s\n", buffer);
	return 0;
}

int cmd_lcard(int sock, const char *buffer)
{
	char cmd[100];
	int code = -75, n=0;
	int start = -1;
	sscanf(buffer, "%80s %d %d %n", cmd, &code, &start, &n);
	if (code < 0) {
		printf("ERROR %d %s", code, buffer+n);
		return code;
	}
//	printf("cmd_lcard: %s\n", buffer+n);
	sprintf(g_card, "%.400s", buffer+n);
	printf("OK saved to local g_card, use $card to read\n");
	// printf("g_card=[%s]\n", g_card);
	return 0;
}

int cmd_batch(int sock, const char *buffer)
{
	printf("RECV: batch: %s", buffer);
	return 0;
}

int cmd_pick(int sock, const char *buffer)
{
	int ret;
	char cmd[10];
	int code;
	int eid;
	int batch_type;
	int loc;
	int card_id;

	ret = sscanf(buffer, "%s %d %d %d %d %d"
	, cmd, &code, &eid, &batch_type
	, &loc, &card_id);
	if (ret < 6) {
		printf("cmd_pick:sscanf_fail %s\n", buffer);
		return 0;
	}
	printf("cmd_pick:%s %d %d %d %d %d\n", cmd, code, eid, batch_type
	, loc, card_id);

	g_pick_list[loc] = g_pick_list[loc] + 1;
	g_pick_sum++;

	printf("g_pick_sum=%d, loc:%d %d %d %d %d %d\n", g_pick_sum, g_pick_list[0]
	, g_pick_list[1]
	, g_pick_list[2]
	, g_pick_list[3]
	, g_pick_list[4]
	, g_pick_list[5]
	);


	return 0;
}



typedef struct {
	long gameid;
	int winner;
	int seed;
	int start_side;
	int ver;
	int eid1, eid2;
	int lv1, lv2;
	int icon1, icon2;
	char alias1[EVIL_ALIAS_MAX+1],  alias2[EVIL_ALIAS_MAX+1];
	char deck1[EVIL_CARD_MAX+1], deck2[EVIL_CARD_MAX+1];
	char cmd[EVIL_CMD_MAX+1];
	char *cmd_ptr; // pointer to cmd, init = cmd;
} replay_t;

replay_t g_replay;

int replay_start(replay_t &replay)
{
	int ret;
	g_my_side = 1; // fix is ok
	// optional:
	g_seed = replay.seed;
	g_cmd_size = 0; // this is actually not need, for re-conn
	memcpy(g_deck0, replay.deck1, EVIL_CARD_MAX+1);
	memcpy(g_deck1, replay.deck2, EVIL_CARD_MAX+1);

	g_lua = mylua_open(g_lang);

	ret = mylua_logic_init(g_lua, replay.deck1, replay.deck2, replay.seed
	, replay.start_side);
	ERROR_RETURN(ret, "reply_start:mylua_logic_init");

	replay.cmd_ptr = replay.cmd; // init the command ptr
	return 0;
}


// replay [status=0] [gameid] [winner] [seed] [start_side] [ver]
// [eid1] [eid2] [lv1] [lv2] [icon1] [icon2] [alias1] [alias2] 
// [deck1] [deck2]
// [cmd]  // ; separated
// 
int cmd_replay(int sock, const char *buffer)
{
	int ret;
	int status = -77;
	int n;
	char my_cmd[200];
	const char *ptr;
	replay_t &replay = g_replay;

	printf("--- cmd_replay\n");

	ret = sscanf(buffer, "%20s %d %n", my_cmd, &status, &n);

	ERROR_NEG_RETURN(ret, "replay_input");

	ptr = buffer;
	ptr += n;
	///                gameid       ver      lv1   icon1
	ret = sscanf(ptr, "%ld %d %d %d %d %d %d %d %d %d %d %s %s %s %s %n"
	, &replay.gameid, &replay.winner, &replay.seed, &replay.start_side
	, &replay.ver, &replay.eid1, &replay.eid2, &replay.lv1, &replay.lv2
	, &replay.icon1, &replay.icon2, replay.alias1, replay.alias2
	, replay.deck1, replay.deck2, &n);
	ERROR_RETURN(ret != 15, "replay_sscanf %d", ret);

	ptr += n;
	strcpy(replay.cmd, ptr);
	replay.cmd_ptr = replay.cmd;


	printf("deck1=%s\n-----\n", replay.deck1);
	printf("deck2=%s\n-----\n", replay.deck2);
	printf("cmd=%s\n-----\n", replay.cmd);

	printf("HELP replay:\nzq=quit_replay | zf [n]=forward n step | zn [n]=play up to [n] next(n) | zp=print card status\n");
	

	replay_start(replay);
	return ST_REPLAY;
}


int replay_print(int sock, const char *buffer)
{
	printf("please use p to print card status\n");
	return 0;
}

int replay_quit(int sock, const char *buffer)
{
	printf("replay_quit clean g_lua status\n");
	if (g_lua != NULL) {
		lua_close(g_lua);
		g_lua = NULL;
	}
	g_st = ST_LOGIN; // maybe not?
	return 0;
}


// CMD: zf [n]
// if n > 0 : it will execute n step
// e.g. cmd="s 1202;b1205;"   retrieve first_cmd="s 1202"
int replay_forward(int sock, const char *buffer)
{
	char cmd[100];
	char *ptr;
	int step = 0;
	int n = 0;
	int ret;
	int count;
	replay_t & replay = g_replay;


	count = 1; // default
	sscanf(buffer, "%s %d", cmd, &count);
	ERROR_NEG_RETURN(count, "replay_forward:negative count");
	printf("replay_forward : zf %d\n", count);
	// get command from the reply.cmd_ptr up to the separator ;
	// update replay.cmd_ptr

	for (int i=0; i<count; i++) {

		ptr = strstr(replay.cmd_ptr, ";");
		if (ptr == NULL) {
			ERROR_RETURN(-3, "replay_forward:no_more_cmd");
		}

		// v=step
		// 1 s 1206;2 n ;3 s 2204;4 n ;5 s 1202;
		//   ^cmd ^  ; is the separator
		sscanf(replay.cmd_ptr, "%d %[^;]%n", &step, cmd, &n);
		replay.cmd_ptr += n + 1;

		// print
		printf("forward step:%d [%s]\n", step, cmd);

		// ppp
		ret = mylua_play_cmd_global(g_lua, cmd);
		ERROR_NEG_RETURN(ret, "replay_forward:play_cmd:%s", cmd);

		sscanf(replay.cmd_ptr, "%d %[^;]%n", &step, cmd, &n);
		printf("(next) cmd = step:%d [%s]  (n=%d)\n", step, cmd, n);
		// forward [1 s 1206]
		// TODO replay.cmd_ptr = ptr + 1;

		if (ret > 0) {
			printf("replay_forward: winner=%d  at step:%d\n", ret, step);
			break;
		}
	}

	return 0;
}

int replay_next(int sock, const char *buffer)
{
	printf("replay_next NOT YET implement\n");
	// 
	return 0;
}

// not yet test, very bug
int auto_ai(int sock, const char *in_buffer) 
{
	int ret;

	printf("------ auto_ai\n");
	if (NULL == g_lua) {
		BUG_RETURN(-13, "auto_ai null_lua");
	}

	// MYLUA_CHECK_MAGIC(g_lua);

	int current_side = mylua_get_int(g_lua, "g_current_side");
	int my_side = 1; // playing with ai, my side always = 1
	printf("---- auto_ai : g_current_side = %d\n", current_side);

	if (current_side != my_side) {
		return -1;  // not my side
		// BUG_RETURN(-7, "auto_ai:invalid_current_side %d", current_side);
	}

	// ai_global  or using ai_cmd_globa ? 
	// note: get_ai_sac(), get_ai_play, then play_cmd
	//traceback

	lua_getglobal(g_lua, "ai_cmd_global");
	// ret = lua_pcall(g_lua, 0, 1, 0);  // 1=input param, 1=output return
	ret = mylua_trace_call(g_lua, 0, 1); // 1=input param, 1=output return
	if (ret != 0) {
		// XXX TODO send 'n' to skip this ai round !
		ERROR_PRINT(ret, "auto_ai_cmd_global:error_use_next");
		MYLUA_CHECK_MAGIC(g_lua);
		// return -68;
		lua_pushstring(g_lua, "n");
	}
	char cmd[100] = {'\0'};
	// char out_cmd[101] = {'\0'};
	strncpy(cmd, lua_tostring(g_lua, -1), 99); cmd[99] = '\0';
	WARN_PRINT( strlen(cmd) > 90, "ai_cmd_global:cmd_too_long %zu", strlen(cmd));
	lua_pop(g_lua, 1); // pop the cmd returned 
	
	MYLUA_CHECK_MAGIC(g_lua);


	int winner = 0;
	if (cmd[0] != 'n') {
		winner = mylua_play_cmd_global(g_lua, cmd);
		DEBUG_PRINT(0, "##### auto_ai : play_cmd winner = %d", winner);
	}
	sock_write(sock, "%s\n", cmd);
	printf("auto_ai after play local and send to server=[%s]\n", cmd);

	if (cmd[0] == 'n') {
		return AUTO_NEXT;
	}

	if (1) return winner;
	

	//// following code is for reference, remove later
	////////////////////////////////////

	// need to check lua
	// note: win(-4), g_current_side, g_phase, err(-1);
	// traceback
	lua_getglobal(g_lua, "play_cmd_global");
	lua_pushstring(g_lua, cmd);  // include "at" command

	ret = mylua_trace_call(g_lua, 1, 4); // 1=input param, 4=output return
	if (ret != 0) {
		ERROR_PRINT(ret, "ai_play_once:play_cmd_global");
		// TODO send 'n' to skip next ?  or simply let user fold?
		MYLUA_CHECK_MAGIC(g_lua);

		// retry with a simple next command for ai
		lua_getglobal(g_lua, "play_cmd_global");
		strcpy(cmd, "n");  // n to skip
		lua_pushstring(g_lua, cmd);  // n to skip
		ret = lua_pcall(g_lua, 1, 4, 0);  // 1=input param, 4=output return
		if (ret != 0) {
			ERROR_PRINT(ret, "ai_play_once:lua_error");
			MYLUA_CHECK_MAGIC(g_lua);
			return -68;
		}
		// implicit: ret = 0
	}
	
	if (lua_isstring(g_lua, -1))  { // err message
		ERROR_NEG_PRINT(-18, "ai_play_lua_error %s"
		, lua_tostring(g_lua, -1));
		lua_pop(g_lua, 4);  // pop 4 return value
		// no broadcast, early exit
		MYLUA_CHECK_MAGIC(g_lua);
		// we can indeed try 'n' later, beware infinite loop!
		return -18;
	}
	
	winner = lua_tointeger(g_lua, -4);
	lua_pop(g_lua, 4);  // pop 4 return value 

	MYLUA_CHECK_MAGIC(g_lua);
	if (ret < 0) {
		BUG_PRINT(ret, "ai_play_once broadcast error");
	}
	// net_write(conn, cmd, '\n');
	// DEBUG_PRINT(0, "AI PLAY CMD OK: [%s]  win=%d", cmd, winner);

	printf("---- auto_ai winner = %d\n", winner);

	return 0;
}


int auto_win(int sock, const char *buffer) 
{
	int ret;
	int win = 0; // 0=invalid,  1 or 2 valid
	int fold_flag = 0;
	char cmd[21];

	DEBUG_PRINT(0, "CMD auto_win : %s", buffer);

	ret = sscanf(buffer, "%20s %d %d", cmd, &win, &fold_flag);
	if (ret < 2) {
		ERROR_RETURN(-5, "auto_win_input");
	}

	if (win != 1 && win != 2 && win != 9) {
		ERROR_RETURN(-15, "auto_win_side %d", win);
	}
	
	if (fold_flag!=0) {
		int lose_side = 3-win;

		if (lose_side != 1 && lose_side != 2) {
			BUG_RETURN(-7, "win_lose_side %d", lose_side);
		}

		printf("FOLD: Side %d [%s] fold(surrender)\n", 
			lose_side, g_room.guest_alias[lose_side-1]);
	}

	// it is in game, so g_lua should not be null
	if (g_lua==NULL) {
		ERROR_RETURN(-7, "auto_win_lua_null");
	}

	lua_close(g_lua);
	g_lua = NULL;  // must set to NULL
	DEBUG_PRINT(0, "GAME win %d   fold %d", win, fold_flag);
	// TODO room_clean 
	g_st = ST_LOGIN; // back to normal
	g_room.channel = 0;
	g_room.rid = 0;
	g_room.num_guest = 0;
	g_st = ST_LOGIN; // back to normal
	return 0;
}


int loop_auto_ai(int sock, const char *buffer) 
{
	int ret;
	if (1!=mylua_get_int(g_lua, "g_current_side"))  {
		ERROR_RETURN(-6, "loop_auto_ai:invalid_g_current_side");
	}

	while ((ret = auto_ai(sock, buffer)) != AUTO_NEXT) {
		if (ret > 0) {
			g_auto_winner = ret;
			printf("#### loop_auto_ai winner = %d\n", g_auto_winner);
			// TODO record winner
			break;
		}
	}
	return 0;
}

int auto_play(int sock, const char *in_buffer) 
{
	int ret;
	int num;
	int n;
	const char *buffer;
	ret = sscanf(in_buffer, "%d %n", &num, &n);
	if (ret >= 1) {
		buffer = in_buffer + n;
	} else { 
		buffer = in_buffer;
	}
	DEBUG_PRINT(0, "auto_play_cmd: %s", buffer);

	if (NULL==g_lua) {
		ERROR_RETURN(-3, "play_null_lua");
	}



	// client-side checking  
	// TODO after server-side implement checking no need for client-side check
	// if we are on my side, the command is already executed!
	// 'next' is always executed!
	if (g_my_side == mylua_get_int(g_lua, "g_current_side") 
	&& buffer!=strstr(buffer, "n") && g_cmd_size<=0) {
		// g_cmd_size > 0 means this command is from re-conn logic
		return 0;
	}

	// this is used by re-conn logic
	if (g_cmd_size > 0) {
		g_cmd_size --;
	}
	ret = mylua_play_cmd_global(g_lua, buffer);
	// feedback error case
	if (ret < 0 ) {
		ERROR_PRINT(ret, "play_cmd buffer=[%s]\n", buffer);
		return ret;
	}

	// normal case ret = 0,  ret>0 means there is a winner
	if (ret > 0) {
		g_auto_winner = ret;
		printf("#### winner 111 = %d\n", g_auto_winner);
	}


	loop_auto_ai(sock, buffer);
	return ST_AUTO;
}


// zplay [cmd] 
// e.g. zplay s 1201
//      zplay n
// why "n" is not executed locally?  why other command execute locally?
// because we need to check valid on command, "n" is the only one we
// do not need to check.  when the command come from server, we will
// always skip, except the "n" command,  so "n" command will be execute
// to the local lua when come from network
int auto_zplay(int sock, const char *in_buffer) 
{
	int ret;
	int n;
	char cmd[21];
	const char * ptr;
	char str[100];

	ret = sscanf(in_buffer, "%20s %n", cmd, &n);

	ptr = in_buffer + n;

	strcpy(str, ptr);
	trim(str, 99);

	if (str[0] != 'n') {
		ret = mylua_play_cmd_global(g_lua, str);
		printf("--- auto_zplay lua_play_cmd ret=%d  [%s]\n", ret, str);
		if (ret < 0) {
			ERROR_RETURN(-6, "auto_zplay:lua_play_cmd %d", ret);
		}
	}
	printf("---- auto_zplay ptr=[%s]\n", str);

	sock_write(sock, "%s\n", str);

	printf("--- auto_zplay lua_play_cmd ret=%d\n", ret);
	return 0;
}


int auto_login(int sock, const char *buffer) 
{
	int ret;
	char cmd[21];
	char alias[EVIL_ALIAS_MAX];
	int eid = 0;

	ret = sscanf(buffer, "%20s %d %d %s", cmd, &eid, &g_st, alias);
	if (ret < 3) {
		ERROR_NEG_RETURN(-5, "auto_login_input_error");
	}
	if (eid == 0) {
		ERROR_NEG_RETURN(-7, "auto_login_eid_zero");
	}
	// normal eid must be > 0
	if (eid < 0) {
		ERROR_NEG_RETURN(eid, "auto_login_error");
	}

	g_euser.eid = eid;
	strcpy(g_euser.alias, alias);  // strcpy for array copy
	DEBUG_PRINT(0, "auto_login: euser.eid=%d   alias=[%s]  g_st=%d\n"
	, g_euser.eid, g_euser.alias, g_st);

	return ST_AUTO;
}

// join / lguest may send "st"
// room [channel] [room] [st] [guest0_info] [guest1_info] ...
// guestX_info = [eid alias]
int auto_room(int sock, const char *buffer) 
{
	int ret;
	char cmd[21];
	// char alias[EVIL_ALIAS_MAX + 1];
	// int eid = 0;
	int room; // room id
	int channel;
	// int join_eid;
	int n;
	int st;
	const char *ptr = NULL;

	// channel is overloaded with error code
	ret = sscanf(buffer, "%20s %d %n", cmd, &channel, &n);
	if (ret < 2) {
		ERROR_RETURN(-5, "auto_room_input_error");
	}
	if (channel < 0) {  // channel is also the error code (<0)
		// TODO print remaining error message
		ERROR_RETURN(channel, "auto_room_error %s", buffer+n);
	}
	if (n < 0) {
		ERROR_RETURN(-7, "auto_room_sscanf_111");
	}

	// e.g. buffer="2 15 88 HELLO"  -> room=2, st=15, n=5(shift 5 chars)
	ptr = buffer + n;
	ret = sscanf(ptr, "%d %d %n", &room, &st, &n);
	if (ret < 2)  {
		ERROR_RETURN(-17, "auto_room_sscanf_222");
	}
	ptr += n; 
	DEBUG_PRINT(0, "auto_room channel=%d  room=%d  st=%d  g_st=%d"
	, channel, room, st, g_st);

	// XXX shall we check g_room data is refreshed?  
	// any issue if we updated g_room when the game is already started?
	g_room.channel = channel;
	g_room.rid = room;
	g_room.num_guest = 0;

	for (int i=0; i<10; i++) {
		int guest_eid = 0;
		char guest_alias[EVIL_ALIAS_MAX+1];
		ret = sscanf(ptr, "%d %s %n", &guest_eid, guest_alias, &n);
		if (ret < 2) {
			break; // skip
		}
		ptr += n;
		g_room.num_guest ++;
		g_room.guest[i] = guest_eid;
		strcpy(g_room.guest_alias[i],  guest_alias);
	}
	DEBUG_PRINT(g_room.num_guest, "num_guest");
	
	if (st == ST_GAME && g_lua==NULL) {
		sock_write(sock, "ginfo\n");
	}

	return ST_AUTO; // state can be either ST_ROOM or ST_GAME
}

// @see game_start()
int auto_game(int sock, const char *buffer) 
{
	int ret;
	int side = 0;
	int cmd_size = 0;
	char cmd[21];
	int seed = 777;
	int start_side = 1;
	int timeout = 0; // count down
	char deck0[EVIL_CARD_MAX + 1];
	char deck1[EVIL_CARD_MAX + 1];
	int n;
	// safety
	bzero(deck0, EVIL_CARD_MAX+1);
	bzero(deck1, EVIL_CARD_MAX+1);

	ret = sscanf(buffer, "%20s %d %n", cmd, &side, &n);
	if (ret < 2) {
		ERROR_RETURN(-1, "auto_game_not_enough_input_1 %d", ret);
	}
	const char *ptr = buffer + n;
	if (side < 0) {
		ERROR_RETURN(side, "auto_game:server err: %s", ptr);
	}

	ret = sscanf(ptr, "%d %d %d %s %s", &cmd_size, &seed
	, &timeout, deck0, deck1);
	start_side = 2 - (seed % 2); // abs(random()) % 2 + 1;

	printf("--- auto_game: side=%d  cmd_size=%d  seed=%d  timeout=%d  start_side=%d\n"
	, side, cmd_size, seed, timeout, start_side);
	printf("--- auto_game: deck0=[%s]  deck1=[%s]\n", deck0, deck1);

	if (ret < 4) {
		ERROR_RETURN(-1, "game_not_enough_input_2 %d", ret);
	}
	if (NULL != g_lua) {
		ERROR_RETURN(-6, "g_lua_not_null");
	}

	g_my_side = side;	// global access
	// optional:
	g_seed = seed;
	g_cmd_size = cmd_size;
	memcpy(g_deck0, deck0, EVIL_CARD_MAX+1);
	memcpy(g_deck1, deck1, EVIL_CARD_MAX+1);

	// for debug:
//	printf("game seed [%d]\n", seed);
//	printf("game deck0 [%s]\n", deck0);
//	printf("game deck1 [%s]\n", deck1);

	g_lua = mylua_open(g_lang);  // global access

	ret = mylua_logic_init(g_lua, deck0, deck1, seed, start_side);
	ERROR_RETURN(ret, "game_start_mylua_logic_init");

	// check if the start_side is me, do loop auto ai
	if (1==mylua_get_int(g_lua, "g_current_side"))  {
		loop_auto_ai(sock, buffer) ;
	}
	return 0 ;
}

// CMD: auto [robot_id] [ai_id] [count]
// it will send: 
// log r[robot_id] [robot_id]
// for i=1 to count do:
//		solo [ai_id] 
//		while winner == 0 do
//         wait for server (if current_side == 1)
//         ai_play()
//      end
//      save winner to winner_list[i]
// end
// 
// @see nio.cpp: ai_play()
int cmd_auto(int sock, const char *buffer)
{
	int robot_id = 1;
	int ai = 8; // default zhanna (8) // 1=warrior, 5=elect_mage, 6=ice, 8=zhanna
	int count = 1;
	char cmd[100];
	char password[100] = "2015";

	sscanf(buffer, "%s %d %d %d", cmd, &robot_id, &ai, &count);

	printf("auto play: robot_id=%d  ai=%d  count=%d\n", robot_id, ai, count);

	// peter: change to robot[N] and password 2015 to test
	sock_write(sock, "log robot%d %s\n", robot_id, password);
	sock_write(sock, "fold\n");   // this is optional, may in room (game)
	sleep(1);

	// TODO remove hard code seed
	sock_write(sock, "solo %d 1001\n", ai); // hard code seed 1001
//	sock_write(sock, "ginfo\n");  

// ERROR -6 loop_auto_ai:invalid_g_current_side cli.cpp:1279: errno 0  2014-12-29 12:11

	return ST_AUTO;
}


int cmd_auto_quick(int sock, const char *buffer)
{
	int robot_id = 1;
//	int ai = 8; // default zhanna (8) // 1=warrior, 5=elect_mage, 6=ice, 8=zhanna
//	int count = 1; // LATER
	char cmd[100];

	sscanf(buffer, "%s %d", cmd, &robot_id);

	printf("auto quick: robot_id=%d\n", robot_id );

	sock_write(sock, "log r%d %d\n", robot_id, robot_id);
	sock_write(sock, "fold\n");   // this is optional, may in room (game)
	sleep(1);

	// TODO remove hard code seed
	sock_write(sock, "quick\n"); // hard code seed 1001
	// sock_write(sock, "ginfo\n");  

	return ST_AUTO;
}


// CMD: zareg 8
int cmd_auto_reg(int sock, const char *buffer)
{
	int total = 0;
	const int MAX_HERO = 4;
	int hero[4] = { 1, 5, 6, 8 };
	char cmd[100];
	sscanf(buffer, "%20s %d", cmd, &total);

	printf("auto_reg %d users\n", total);
	

	for (int i=0; i<total; i++) {
		sock_write(sock, "reg r%d %d\n", i+1, i+1);
		sock_write(sock, "job %d\n", hero[i % MAX_HERO]);
	}

	return 0;
}


// XXX obsolete: @see cmd_room
// read server room_create return, no send method
// just for change the g_level
int room_create(int sock, const char *buffer)
{

	int ret;
	char cmd[21];

	int n;
	int channel;
	int rid;
	int state;
	const char *ptr = NULL;

	ret = sscanf(buffer, "%21s %d %n", cmd, &channel, &n);
	if (ret < 2) {
		ERROR_RETURN(-5, "room_create_input_error");
	}

	if (channel < 0) {
		ERROR_RETURN(channel, "room_create_error");
	}
	printf("channel=%d	rest=[%s]\n", channel, buffer + n);

	if (n < 0) {
		ERROR_RETURN(-7, "room_create_sscanf_111");
	}

	ptr = buffer + n;
	ret = sscanf(ptr, "%d %d %n", &rid, &state, &n);
	if (ret < 2) {
		ERROR_RETURN(-17, "room_create_sscanf_222");
	}

	printf("room_create channel=%d, rid=%d, state=%d\n", channel, rid, state);

	g_st = state;


	return ST_ROOM;
}



int local_info(int sock, const char *buffer)
{
	printf("$info local_info\n"); 
	printf("LOCAL INFO: eid=%d  alias=%s  rating=%.2lf  st=%d\n"
		, g_euser.eid, g_euser.alias, g_euser.rating, g_st);
	printf("GAME  count=%d  win=%d  lose=%d\n"
		, g_euser.game_count, g_euser.game_win, g_euser.game_lose);

	if (g_room.rid > 0) {
		printf("ROOM  channel=%d  room_id=%d  num_guest=%d\n"
		, g_room.channel, g_room.rid, g_room.num_guest);
		for (int i=0; i<g_room.num_guest; i++) {
			printf("Guest[%d] : %d %s\n", i, g_room.guest[i]
			, g_room.guest_alias[i]);
		}
	}
	printf("--------\n");
	return 0;
}

int local_card(int sock, const char *buffer)
{
	int ret;
	int len = 0;
	int total;
	const int max = 100;
	char str[max];
	const char *ptr = NULL;
	const char *table;
	lua_State *lua_logic = NULL;

	printf("$card local_card\n");
	len = strlen(g_card);
	if (len != EVIL_CARD_MAX) {
		ERROR_RETURN(-2, "g_card_invalid_len %d", len);
	}


	lua_logic = mylua_open(g_lang);

/***  keep this for testing: temp.lua
	lua_getglobal(lua_logic, "g_card_list");
	ret = lua_istable(lua_logic, -1);
	printf("istable -1:%d  type=%d\n", ret, lua_type(lua_logic, -1));
	lua_rawgeti(lua_logic, -1, 22);
	ptr = lua_tolstring(lua_logic, -1, &size);
	printf("g_card_list[22] : str%zd = %s\n", size, ptr);
	lua_pop(lua_logic, 2);  // pop g_card_list and the string
***/



/****
	lua_getglobal(lua_logic, "g_card_list");
	lua_rawgeti(lua_logic, -1, 188); // rusty sword test
	ret = lua_type(lua_logic, -1);
	printf("after rawgeti type=%d\n", ret); // expect 5=table
	lua_getfield(lua_logic, -1, "name");
	ret = lua_type(lua_logic, -1);
	printf("after getfield type=%d\n", ret); // expect 4=str (3=num, 0=nil)
	ptr = lua_tolstring(lua_logic, -1, &size);
	printf("g_card_list[188].name (%zd) = %s\n", size, ptr);
*****/ 
	
	ret = lua_tonumber(lua_logic, -1); // -1 is the first in stack
	printf("magic 3333: %d\n", ret);



	total = 0;
	for (int i=0; i<len; i++) {
		int ch = g_card[i];
		if (ch < '0' || ch > '9') {
			BUG_PRINT(-27, "g_card_outbound index=%d  ch=%d", i, ch);
			continue;
		}
		if (ch == '0') continue; // skip empty card
		if (i+1 <= 20) {
			table = "hero_list";
		} else {
			table = "g_card_list";
		}
		ptr = mylua_get_table_index_name(lua_logic, str, max, table, i+1, "name");
		printf("[%d] x %c : %s\n", i+1, ch, (ptr!=NULL) ? ptr : "_null_");
		total += ch - '0';
	}
	printf("--------- total=%d\n", total);

	MYLUA_CHECK_MAGIC(lua_logic);

	// check magic before close!
	if (lua_logic != NULL) {
		lua_pop(lua_logic, 1);
		lua_close(lua_logic);
	}
	return 0;
}

int local_lang(int sock, const char *buffer)
{
	int value = -1;
	char cmd[10];
	printf("$lang : local_lang\n");
	sscanf(buffer, "%9s %d", cmd, &value); // cmd is useless here
	if (value < 0) {
		ERROR_RETURN(-5, "invalid_input: try $lang 1");
	}
	g_lang = value;
	printf("$lang : set g_lang=%d\n", g_lang);
	return 0;
}


int local_st(int sock, const char *buffer)
{
	int value = -1;
	char cmd[10];
	printf("$st : change the g_st (DANGER!)\n");
	sscanf(buffer, "%9s %d", cmd, &value); // cmd is useless here
	if (value < 0) {
		ERROR_RETURN(-5, "invalid_input: try $st 90");
	}
	g_st = value;
	printf("$st : set st=%d\n", g_st);
	return 0;
}

int cmd_lpiece(int sock, const char *buffer)
{
	int ret;
	char my_cmd[200];
	int eid;
	char card_buffer[2401];
	int piece[EVIL_CARD_MAX+1];
	int n;
	char *ptr;
	int cur_pos;
	const int row_count = 20;
	int cur_piece[EVIL_CARD_MAX+1][2];
	int cur_count;

	ret = sscanf(buffer, "%20s %d %s", my_cmd, &eid, card_buffer);
	ERROR_RETURN(ret != 3, "lpiece_sscanf %d", ret);

	cur_count = 0;
	ptr = card_buffer;
	for (int i = 0; i < EVIL_CARD_MAX; i++) {
		sscanf(ptr, "%02d%n", &(piece[i]), &n);
		ptr += n;
		if (piece[i] > 0) {
			cur_piece[cur_count][0] = i;
			cur_piece[cur_count][1] = piece[i];
			cur_count++;
		}
	}

	cur_pos = 0;
	int row = (cur_count / row_count) + ((cur_count % row_count > 0) ? 1 : 0);
	for (int i = 0; i < row; i++, cur_pos += row_count) {
		printf("card_id ");
		for (int i = 0; i < row_count; i++) {
			if (cur_pos + i >= cur_count) {
				break;
			}
			printf("%.02d ", cur_piece[cur_pos+i][0]);
		}
		printf("\n");
		printf("piece   ");
		for (int i = 0; i < row_count; i++) {
			if (cur_pos + i >= cur_count) {
				break;
			}
			printf("%.02d ", cur_piece[cur_pos+i][1]);
		}
		printf("\n");
	}

	return 0;
}

int cmd_ppiece(int sock, const char *buffer)
{
//,	{ ST_LOGIN, "ppiece", cmd_ppiece }
	return 0;
}

int cmd_mpiece(int sock, const char *buffer)
{
//,	{ ST_LOGIN, "mpiece", cmd_mpiece }
	
	return 0;
}

int cmd_cpiece(int sock, const char *buffer)
{
	int ret;
	char my_cmd[200];
	int n;
	int count;
	const char *ptr;
	int card_id;
	int piece_count;
	int merge_count;
	int gold;
	int crystal;

	ret = sscanf(buffer, "%20s %d %n", my_cmd, &count, &n);
	ERROR_RETURN(ret != 2, "cpiece_sscanf %d", ret);

	ptr = buffer + n;
	printf("piece_count[%d]\n", count);
	printf("\tcard_id | piece_count | merge_count | gold | crystal\n");
	printf("\t--------|-------------|-------------|------|--------\n");
	for (int i = 0; i < count; i++) {
		ret = sscanf(ptr, "%d %d %d %d %d %n", &card_id, &piece_count
		, &merge_count, &gold, &crystal, &n);
		ERROR_RETURN(ret != 5, "cpiece_sscanf_piece %d", ret);
		printf("\t%.7d | %.11d | %.11d | %.4d | %.7d\n", card_id
		, piece_count, merge_count, gold, crystal);
		ptr += n;
	}

	return 0;
}

int cmd_piece_chapter(int sock, const char *buffer)
{
//,	{ ST_LOGIN, "piece_chapter", cmd_piece_chapter }
	return 0;
}


typedef struct {
	int level;  // 0=nologin, 5=login, 10=in_room, 99=system
	const char * name;
	int (* fun)(int , const char *);
} command_t;


command_t command_list[] = {
	{ 0, "unknown", unknown_fun } // start game
,   { ST_NULL, "$info", local_info }
,   { ST_NULL, "$card", local_card }
,   { ST_NULL, "$lang", local_lang }
,   { ST_NULL, "$c", local_card }	// XXX for debug, shorter, delete later
,	{ ST_NULL, "$st", local_st } // force to change local st 
////// auto must be earlier than ST_GAME
,	{ ST_NULL, "zauto", cmd_auto }   // enter auto mode (bug!)
,	{ ST_NULL, "zquick", cmd_auto_quick }   // enter auto mode
,	{ ST_NULL, "zareg", cmd_auto_reg }   // auto reg N user r1, r2...rn
,	{ ST_AUTO, "game", auto_game } // start auto game
,	{ ST_AUTO, "ginfo", auto_game } // re-conn auto game
,	{ ST_AUTO, "room", auto_room } 
,	{ ST_AUTO, "log", auto_login } 
,	{ ST_AUTO, "zai", auto_ai } 
,	{ ST_AUTO, "zplay", auto_zplay } // local enter command
,	{ ST_AUTO, "win", auto_win }   // win, finished
,	{ ST_AUTO, "t", auto_play } 	// server return command
,	{ ST_AUTO, "b", auto_play }
,	{ ST_AUTO, "s", auto_play }
,	{ ST_AUTO, "n", auto_play } 
///////////////
,	{ ST_NULL, "log", client_login } // login
,	{ ST_LOGIN, "lguest", cmd_room } // lguest, join, room are the same
,	{ ST_LOGIN, "join", cmd_room }  // TODO remove lguest,join
,	{ ST_LOGIN, "room", cmd_room } 
// ,	{ ST_LOGIN, "room", room_create } // change g_st when create a room 
,	{ ST_LOGIN, "leave", room_leave }	// leave [eid] [st]
,	{ ST_LOGIN, "kick", room_leave }	// kick [eid] [st]
,	{ ST_LOGIN, "game", game_start } // start game
,	{ ST_LOGIN, "ginfo", game_start } // re-conn start game
,	{ ST_LOGIN, "solo_plus", solo_plus_start } // re-conn start game
,	{ ST_LOGIN, "sta", sys_status } // re-conn start game
,	{ ST_LOGIN, "solo", cmd_solo } // re-conn start game
, 	{ ST_LOGIN, "lcard", cmd_lcard } // load card to g_card
,	{ ST_LOGIN, "bat", cmd_batch }
,	{ ST_LOGIN, "pick", cmd_pick }
,	{ ST_LOGIN, "replay", cmd_replay }

,	{ ST_LOGIN, "lpiece", cmd_lpiece }
,	{ ST_LOGIN, "ppiece", cmd_ppiece }
,	{ ST_LOGIN, "mpiece", cmd_mpiece }
,	{ ST_LOGIN, "cpiece", cmd_cpiece }
,	{ ST_LOGIN, "piece_chapter", cmd_piece_chapter }


,	{ ST_GAME, "gerr", game_error } // surrender
,	{ ST_GAME, "fold", game_fold } // surrender
,	{ ST_GAME, "t", play_cmd }
,	{ ST_GAME, "b", play_cmd }
,	{ ST_GAME, "s", play_cmd }
,	{ ST_GAME, "n", play_cmd } 

,	{ ST_REPLAY, "zp", replay_print }   // using p may works too
,	{ ST_REPLAY, "zq", replay_quit }   // using p may works too
,	{ ST_REPLAY, "zf", replay_forward }   // using p may works too
,	{ ST_REPLAY, "zn", replay_next }   // using p may works too


,	{ ST_GAME, "win", game_win }   // win, finished
//,	{ 0, "q", 	sys_close }  // must be after quitall
};
const int TOTAL_COMMAND = sizeof(command_list) / sizeof(command_t);

int process_command(int sock, const char *buffer) 
{
	int n;
	int ret;
	int num;
	char cmd[21];
	const char *ptr ;
	// potential issue:  this will be same as [log n p]
	// login678901234567890n p
	// log n p -> cmd="log"  n=4
	// logn p -> cmd="logn" n=5
	ptr = buffer;
	ret = sscanf(ptr, "%d %n", &num, &n);
	if (ret >= 1) {
		ptr = ptr + n;
	}
	ret = sscanf(ptr, "%20s %n", cmd, &n);
	if (ret <= 0) {
		printf("unknown -11 ERROR unknown command\n");
		return 0;
	}
	ptr = ptr + n; // ptr is the rest of parameters after cmd

	for (int i=0; i<TOTAL_COMMAND; i++) {
		const char *name = command_list[i].name;
		if (cmd != strstr(cmd, name)) {
			continue; // early continue
		}

		if (g_st < command_list[i].level) {
			// WARN_PRINT(-3, "process [%s] require st>=%d  now %d"
			// , name, command_list[i].level, g_st);
			//  implement auto means we need to skip this
			continue;
		}
		// command_list[i].fun == NULL : do some error handling
		ret = command_list[i].fun(sock, buffer);  // was: ptr
		return ret;
	}

	// printf("unknown -1 %s", cmd);
	return -1;
}


// read until \n
// @return number of bytes read,  -2 is buffer overflow
int read_line(int fd, char *buffer, int max)
{
	char *ptr;
	char *end = buffer + max - 1;
	int ret = 0;
	int total = 0;
	ptr = buffer;

	while (ptr < end && total < max) {
		ret = read( fd, ptr, max - total);
		if (ret == 0) {
			return total;
		}
		ERROR_NEG_RETURN(ret, "read_line:read");
		// found \n,  return
		if (strchr(ptr, '\n') != NULL) {
			total += ret;
			return total;
		}
		ptr += ret;
		total += ret;
	}

	FATAL_EXIT(-2, "read_line:buffer_overflow %d", total);
	return -2; 
}



// *ptr is the sock
void * read_loop(void *ptr) {
	char buffer[BUFFER_SIZE+1];
	// char temp[BUFFER_SIZE+1];
	int sock;
	int ret;
	int running = 1;
	char * tok = NULL;
	const char * sep = "\n";
	sock = *(int *)ptr;

	printf("read_loop: sock=%d\n", sock);

	// TODO read until "\n"
	while (running) {
		buffer[0] = 0;

		// ret = read( sock, buffer, BUFFER_SIZE);
		ret = read_line(sock, buffer, BUFFER_SIZE);
		FATAL_NEG_EXIT(BUFFER_SIZE-ret, "read_overflow %d", ret);
		if (ret >= 0) buffer[ret] = 0;  // null-term, ret must be >= 0

		printf("RECV(%3d): %s", ret, buffer);
		if (ret <= 0) {
			DEBUG_PRINT(ret, "read_loop:read, errno = %d", errno);
			sys_shutdown();
			return NULL;
		}

		tok = strtok(buffer, sep);
		while (tok != NULL) {
			// printf("DEBUG cli_read_loop before g_st=%d\n", g_st);
			//printf("TOK: %s\n", tok);
			int new_level = process_command(sock, tok);
			if (new_level > 0) {
				g_st = new_level;  // useless now
				printf("--- set g_st = %d\n", g_st);
			}
			// after core logic, we get the next token (\n separator)
			tok = strtok(NULL, sep); // next while use this
			// printf("DEBUG cli_read_loop end g_st=%d\n", g_st);
		} // end while
	}
	return NULL;
}


// actually this is not a pthread callback, but we use the same function type
void * write_loop(void *ptr) {
	int sock = *(int *)ptr;
	int running = 1;
	int ret;
	char buffer[BUFFER_SIZE+1];

	while (running) {
		// keyboard input
		buffer[0] = 0;
		if (NULL==fgets(buffer, BUFFER_SIZE, stdin)) {
			running = 0;
			break;
		}
		if (0==buffer[0]) {
			continue; // or quit?
		}

		// printf("SEND(%3zu): %s", strlen(buffer), buffer);
		if (buffer==strstr(buffer, "q") && strlen(buffer) <= 2) {
			printf("write_loop:quit\n");
			running = 0;
			break;
		}


		// TODO : capture "sta" and show the status (rating, win/lose etc)
		// '%' is the prefix for local command
		if (buffer[0]=='$' || buffer[0]=='z') {
			printf("process local command: %s\n", buffer);
			int new_level = process_command(sock, buffer);
			if (new_level > 0) {
				g_st = new_level;  // useless now
				printf("--- set g_st = %d\n", g_st);
			}
			continue;
		}

		if (buffer==strstr(buffer, "i") && strlen(buffer) <= 2) {
			printf("write_loop:i info\n");
			printf("LOCAL INFO: eid=%d  alias=%s  rating=%.2lf  st=%d\n"
			, g_euser.eid, g_euser.alias, g_euser.rating, g_st);
			printf("GAME  count=%d  win=%d  lose=%d\n"
			, g_euser.game_count, g_euser.game_win, g_euser.game_lose);

			if (g_room.rid > 0) {
				printf("ROOM  channel=%d  room_id=%d  num_guest=%d\n"
				, g_room.channel, g_room.rid, g_room.num_guest);
				for (int i=0; i<g_room.num_guest; i++) {
					printf("Guest[%d] : %d %s\n", i, g_room.guest[i]
					, g_room.guest_alias[i]);
				}
			}
			printf("--------\n");

			continue;
		}

		// when we have a game (g_lua != NULL), command is specially handled
		if (g_lua != NULL) {
			MYLUA_CHECK_MAGIC(g_lua);  // must be with push magic

			// read-only access is ok, for my side or non-my-side
			if (buffer == strstr(buffer, "p")) {
				mylua_print_index_both(g_lua);
				mylua_print_status(g_lua);
				continue; // do not write to network
			}
	

			// client-side checking
			// if not my turn, skip all play command, except fold
			if (g_my_side != mylua_get_int(g_lua, "g_current_side")
			&& buffer != strstr(buffer, "fold") ) {
				printf("ERROR not my side, only p, fold work!\n");
				continue;
			}

			// TODO we should refactor: use test_cmd(cmd) validate cmd
			// next should not execute locally, 
			// buffer == strstr(buffer, "next")
			if (  	buffer == strstr(buffer, "t ") 
			|| 		buffer == strstr(buffer, "b ")
			|| 		buffer == strstr(buffer, "s ")
			) {
				ret = mylua_play_cmd_global(g_lua, buffer);
				if (ret < 0) {
					printf("ERROR invalid play cmd [%s]\n", buffer);
					continue;
				}
				// ret >= 0 :  fall through,  send to network
			}

		}

		// ret = write(sock, buffer, strlen(buffer));
		ret = sock_write(sock, "%s", buffer);
		if (ret <= 0) {
			printf("write_loop:write quit %d\n", ret);
			running = 0;
			break;
		}
	}

	return NULL;
}




static void sig_handler(int sig) {
	printf("sig_handler: sig=%d\n", sig);
//	|| sig==SIGINT) {
	if (sig==SIGPIPE) {
		sys_shutdown();
		return;
	}
}


// assume cli 0,  cli 1, cli 2 are in order
int init_command(int v, int sock, int argc, char *argv[])
{
	char * msg;
	switch (v) {
		case 0:  // default is no command
		// sock_write(sock, "log x x\nroom 1\ninfo\nsta\n");
		break;

		case 1:
		sock_write(sock, "log y y\n");
		sock_write(sock, "join 1 1\n");
		break;

		case 2:
		sock_write(sock, "log z z\n");
		sock_write(sock, "fold\nleave\nquick\n");
		break;

		case 3:
		sock_write(sock, "log www www\n");
		sock_write(sock, "sta\n");
		break;

		case 4:
		sock_write(sock, "log lll lll\n");
		sock_write(sock, "sta\n");
		break;

		case 5:
		sock_write(sock, "log masha 1\n");
		sock_write(sock, "course\ncourse\ncourse\ncourse\nsta\n");
		sleep(1);
		exit(0);
		break;
		
		case 6:
		sock_write(sock, "log masha2 1\n");
		sock_write(sock, "course\ncourse\ncourse\ncourse\nsta\n");
		sleep(1);
		exit(0);
		break;

		case 7: 
		sock_write(sock, "log z z\n");
		sock_write(sock, "info\n");
		break;

		case 8:
		sock_write(sock, "log www www\n");
		sock_write(sock, "room 1\n");
		break;

		case 9:
		sock_write(sock, "log x x\n");
		sock_write(sock, "solo 1 1234\n");
		break;

		case 12:
		sock_write(sock, "log x2 x2\n");
		sock_write(sock, "info\n");
		break;

		case 13:
		sock_write(sock, "log x3 x3\n");
		sock_write(sock, "info\n");
		break;

		case 14:
		sock_write(sock, "log x4 x4\n");
		sock_write(sock, "info\n");
		break;

		case 15:
		sock_write(sock, "log x5 x5\n");
		sock_write(sock, "info\n");
		break;

		case 16:
		sock_write(sock, "log x6 x6\n");
		sock_write(sock, "info\n");
		break;

		case 17:
		sock_write(sock, "log x7 x7\n");
		sock_write(sock, "info\n");
		break;

		case 18:
		sock_write(sock, "log x8 x8\n");
		sock_write(sock, "info\n");
		break;

		case 19:
		sock_write(sock, "log x x\n");
		sock_write(sock, "bat 0\n");
		break;

		case 20:
		sock_write(sock, "log y y\n");
		sock_write(sock, "bat 0\n");
		break;

		case 22:  // chinese reg: my <  db-init.sql
		sock_write(sock, "reg ccc ccc 马化腾\n");
		sock_write(sock, "sta\n");
		break;

		case 23:
		sock_write(sock, "log x x\n");
		sock_write(sock, "batch 0\n");
		for (int i=0; i < 50; i++) {
			sock_write(sock, "pick 0\n");
		}
		break;

		case 24:
		sock_write(sock, "log x x\n");
		sock_write(sock, "batch 1\n");
		for (int i=0; i < 50; i++) {
			sock_write(sock, "pick 1\n");
		}
		break;

		case 31:
		sock_write(sock, "log e1 e1\n");
		sock_write(sock, "sta\n");
		break;

		case 32:
		sock_write(sock, "log e2 e2\n");
		sock_write(sock, "sta\n");
		break;

		case 33: // @see nio.cpp : main 
		sock_write(sock, "reg aa aa\nreg bb bb\nreg cc cc\nsta\nreg dd dd\n"
		/*
			"reg ee ee\n" "reg ff ff\n" "reg gg gg\n" "reg hh hh\n" 
			"reg ii ii\n" "reg jj jj\n" "reg kk kk\n"
		*/
			);
		break;

		case 34: // @see nio.cpp : store_read_buffer
		sock_write(sock, "log x x\nsta\nsta\nsta\n"
		"sta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		"sta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		);
		sleep(1);
		sock_write(sock, "log y y\n");
		break;

		case 35: // @see nio.cpp : store_read_buffer
		sock_write(sock, "log x x\nsta\nsta\nlog z z\nsta\n");
		sleep(1);
		sock_write(sock, "info\ninfo\ninfo\n");
		sleep(1);
		sock_write(sock, "log y y\n");
		break;


		case 41: // log x x mission vs  (test 20)
		sock_write(sock, "log x x\nlmis\nquick\n");
		break;

		case 42: // log y y mission vs
		sock_write(sock, "log y y\nlmis\nquick\nfold\n");
		break;

		case 43: // win AI 98 one time   (mid=3 n1++)
		sock_write(sock, "log x x\nlmis\nsolo 98 5\nn\nn\n");
		break;

		// require:  547 mission 1 (mid=1) status=3 (got reward)
		case 44: // win AI 99 one time   (mid=2 n1++,  mid=3 n1++)
		sock_write(sock, "log x x\nlmis\nsolo 99 5\nn\nn\n");
		break;

		// test mid=6, mid=7
		case 45: // win AI 99 one time   (mid=2 n1++,  mid=3 n1++)
		sock_write(sock, "log x x\nlmis\nsolo 99 5\n");
		// TODO another guy view
		break;

		case 46:
		sock_write(sock, "log y y\nlmis\nroom 2 1\n");
		break;

		// test mid=6, mid=7
		case 47: // win AI 99 one time   (mid=2 n1++,  mid=3 n1++)
		sock_write(sock, "log x x\nlmis\nsolo 98 5\n");
		// TODO another guy view
		break;


		case 52:	// read buffer overflow : nio.cpp: -52
		sock_write(sock, "log x x\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		"sta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		"sta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		"sta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		);
		break;

		case 55:
		sock_write(sock, "log x x\nreplay 1387677967427992\n");
		break;

		case 56:  // test replay (s1)
		sock_write(sock, "log peter adgjmp\nreplay 141106232030001\n");
		break;


		case 57:
		sock_write(sock, "log x x\nwchat 一二三四五六七八九零一二三四五六七八九零一二三四五六七八九零一二三四五六七八九零一二三四五六七八九零一二三四五六七八九零一二三四五六七八九零东西南北中发白\n");
		sock_write(sock, "q\n");
		break;

		// send wchat by argv
		case 59:
		if (argc != 4) {
			WARN_PRINT(-5, "case58:input_error");
			sock_write(sock, "q\n");
			return 0;
		}
		msg = argv[3];
		if (strlen(msg) >= 200) {
			WARN_PRINT(-5, "case58:msg_too_long %lu", strlen(msg));
			sock_write(sock, "q\n");
			return 0;
		}
		/*
		char buffer[500];
		sprintf(buffer, "log x x\nwchat %s\n", msg);
		*/
		sock_write(sock, "log x x\nwchat %s\n", msg);
		sock_write(sock, "q\n");
		break;

		// send wchat by argv
		case 60:
		if (argc != 4) {
			WARN_PRINT(-5, "case58:input_error");
			sock_write(sock, "q\n");
			return 0;
		}
		msg = argv[3];
		if (strlen(msg) >= 200) {
			WARN_PRINT(-5, "case58:msg_too_long %lu", strlen(msg));
			sock_write(sock, "q\n");
			return 0;
		}
		/*
		char buffer[500];
		sprintf(buffer, "log 系统 860618\nwchat %s\n", msg);
		sock_write(sock, buffer);
		*/
		sock_write(sock, "log 系统 860618\nwchat %s\n", msg);
		sleep(1);
		sock_write(sock, "q\n");
		break;

		case 61:
		if (argc != 5) {
			WARN_PRINT(-3, "case61:input_error");
			sock_write(sock, "q\n");
			return 0;
		}
		msg = argv[4];
		if (strlen(msg) >= 200) {
			WARN_PRINT(-5, "case61:msg_too_long %lu", strlen(msg));
			sock_write(sock, "q\n");
			return 0;
		}
		/*
		char buffer[500];
		sprintf(buffer, "log 系统 860618\nwchat %s\n", msg);
		sock_write(sock, buffer);
		*/
		sock_write(sock, "@chat %s %s\n", argv[3], msg);
		sleep(1);
		sock_write(sock, "q\n");
		break;

		case 66: 
		sock_write(sock, "log x x\n");
		sock_write(sock, "chapter 1 1\n");
		break;

		case 100:
		// sock_write(sock, "dblog x x\n");
		sock_write(sock, "log x x\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		"sta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		"sta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		"sta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\nsta\n"
		);
		sock_write(sock, "q\n");
		break;

		case 88:
		sock_write(sock, "log 系统 860618\nwchat 加入玩家QQ群90196112，和群友们交流心得，约战比赛，迟些还可以参加丰厚的现金奖大赛!\n");
		sock_write(sock, "q\n");
		break;

		case 89:
		sock_write(sock, "log 系统 860618\nwchat 决战王者全员大赛开始了！参与可获5000金币，详情请进入Q群90196112咨询。\n");
		sleep(1);
		sock_write(sock, "q\n");
		break;

		case 99:
		DEBUG_PRINT(v, "init_command:do_nothing");
		break;

		default:
		WARN_PRINT(v, "init_command:v not handle");
		break;
			
	}
	return 0;
}


int test1() {
	// room_join(0, "join 2 3 18 x 19 y");
	// room_join(0, "join -6 logic errro");

	client_login(0, "log 18 x");
	printf("----------------------\n");
	client_login(0, "log -3 null_name");
	return 0;
}

int test2() {
	int ret;
	g_lua = mylua_open(g_lang);

	const char *deck1 = "0000000100000000000002200321030000000000000000000000000000000000000000000000000000000000002202212021000000000000000000000000000000111110000000000000002002200000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	const char *deck2 = "1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	int seed = 100;  // core logic!!!
	int start_side = 1;

	ret = mylua_logic_init(g_lua, deck1, deck2, seed, start_side);
	ERROR_RETURN(ret, "test_mylua_logic_init_error");

	g_my_side = 1;

	mylua_print_index_both(g_lua);
	mylua_print_status(g_lua);

	
	// TODO lua_close!
	return 0;
}


// testing core dump for x(warrior) vs ai_6
// lua : play_cmd_global
// http://comments.gmane.org/gmane.comp.lang.lua.luajit/3254
//
// gdb cli
// run
// --- crash
// bt
// frame X  (peter: X=0)
//
// p (char *)J->pt->chunkname.gcptr32 + 16
// p J->pt->firstline 
//
int test3()
{
	int ret;
	lua_State * lua = NULL;
	int current_side;
	
	lua = mylua_open(g_lang);

	mylua_set_int(lua, "g_ui", 1);
	ret = luaL_dofile(lua, "logic.lua");
	ERROR_PRINT(ret, "test3:logic.lua");

	int win = 0;
	char cmd[100];
	int seed = 4492;
	int start_side = 1;
	const char *deck1 = "1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	const char *deck2 = "0000010000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	ret = mylua_logic_init(lua, deck1, deck2, seed, start_side);

	
	for (int i=0; i<100; i++) {
		current_side = mylua_get_int(lua, "g_current_side");
		printf("------------- Turn: %d   current_side=%d\n", i, current_side);
		if (current_side == 2) {
			lua_getglobal(lua, "ai_cmd_global");
			ret = lua_pcall(lua, 0, 1, 0);  // 1=input param, 1=output return
			FATAL_EXIT(ret, "test3:lua_pcall %d", i);

			strcpy(cmd, lua_tostring(lua, -1));  // dangerous?
			lua_pop(lua, 1); // pop the cmd returned
			MYLUA_CHECK_MAGIC(lua);

			printf("ai will play: [%s]\n", cmd);

			// now, use the 'cmd'
			lua_getglobal(lua, "play_cmd_global");
			lua_pushstring(lua, cmd);  // include "at" command
			ret = lua_pcall(lua, 1, 4, 0); // 1=input_param, 4=output return
			if (ret != 0) {
				FATAL_EXIT(-7, "lua_pcall( play_cmd_global) ");
			}

			if (lua_isstring(lua, -1)) {
				ERROR_NEG_PRINT(-18, "ai_play_lua_error %s", lua_tostring(lua, -1));
				FATAL_EXIT(-17, "play_cmd_global error2");
			}

			win = lua_tointeger(lua, -4);
			printf("win = %d\n", win);
			lua_pop(lua, 4); // 4 is ok?
			MYLUA_CHECK_MAGIC(lua);
			if (win != 0) {
				break;
			}
			
		}  else {
			// human press 'n'
			strcpy(cmd, "n");
			// do 'n' next
			lua_getglobal(lua, "play_cmd_global");
			lua_pushstring(lua, cmd);  // include "at" command
			ret = lua_pcall(lua, 1, 4, 0);  // 1=input param, 4=output return
			FATAL_EXIT(ret, "normal_play_cmd_global");

			if (lua_isstring(lua, -1))  { // err message
				ERROR_NEG_PRINT(-1, "lua_error : current_side=%d  phase=%d"
					, mylua_get_int(lua, "g_current_side")
					, mylua_get_int(lua, "g_phase")  );

				FATAL_EXIT(-27, "normal_play_error");
			}
			win = lua_tointeger(lua, -4);
			printf("win = %d\n", win);
			lua_pop(lua, 4);
			MYLUA_CHECK_MAGIC(lua);
			if (win != 0) {
				break;
			}
		}
	}

	printf("-------------DONE good win = %d??\n", win);

	return -10;
}



int main(int argc, char *argv[])
{
	struct sockaddr_in addr; // should use internet address
	int sock;
	int ret;
	// char buffer[BUFFER_SIZE+1];
	// char username[10], password[10];
	pthread_t read_thread;

	printf("Usage: ./cli 99 [server_ip]    note: no [server_ip] means local\n");
	printf("t1=211.149.186.201   s1=121.42.15.78\n");

	if (argc >= 3) {
		strcpy(ipaddress, argv[2]);
	} else {
		strcpy(ipaddress, "127.0.0.1");
	}

//	test3();
//	return 0;

	// init g_euser
	bzero(&g_euser, sizeof(g_euser));
	bzero(&g_room, sizeof(g_room));   // TODO leave, need to bzero my room

	bzero(&g_pick_list, sizeof(g_pick_list));

	
	// signal(SIGTERM, handler);  // ctl-C
	// signal(SIGINT, sig_handler);  // ctl-C ?
	signal(SIGCHLD, sig_handler);
	signal(SIGPIPE, sig_handler); // SIG_IGN  // broken pipe
	signal(SIGHUP, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGUSR2, sig_handler);
	signal(SIGALRM, sig_handler);

	int v = 0;
	if (argc >= 2) {
		v = atoi(argv[1]);
	}

	addr = prepare_addr(ipaddress, 7710);
	if (addr.sin_addr.s_addr == 0xffffffff) {
		FATAL_EXIT(-1, "prepare_addr: %s", ipaddress);
	}

	sock = socket( addr.sin_family, SOCK_STREAM, 0);
	FATAL_NEG_EXIT(sock, "socket");
	printf("main: sock=%d  v=%d\n", sock, v);

	ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	FATAL_EXIT(ret, "connect");

	ret = pthread_create(&read_thread, NULL, read_loop, &sock);
	FATAL_EXIT(ret, "pthread_create");

	usleep(100000); // 0.1 sec

	init_command(v, sock, argc, argv);  // send init command to server

	write_loop(&sock);
	
	sys_shutdown();
	return 0;
}

