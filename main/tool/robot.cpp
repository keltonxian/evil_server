/**
 * TODO re-conn logic (we may kill the robot in the middle)
 * TODO use wait_cmd() and signal_cmd()
 * note: only process_robot() do the signal (centralized), no more signal
 *       in other process_xxx()
 *
 * robot.cpp 
 * - command-wait-feedback mechanism (no user input)
 * - init_robot(robot_t & robot, const char * ip, int port)
 * - send_command(robot_t & robot, const char * cmd)
 * - read_command(robot_t & robot, char * buffer)
 *   note: define MAX_BUFFER 10000  (@see evil.h)
 * - process_command(robot_t & robot, const char * cmd)
 *
 * actual logic:
 * MAX_WAIT default to 10 seconds : wait time before a robot do a quick game
 * 1. log [username] [pass]
 *    RET: log [eid] [st]
 *    note: st=5 means ok to do CMD:quick
 *          st=15 means it is still in game, need to finish the game first!
 * 2. every 3 seconds, do:
 *    robot.quick = 0
 *    CMD: @lquick
 *    RET:
 *    @lquick total=1 now=1419497988
 *    @lquick eid=1076 rating=1000.00000 start_time=1419497871
 *    ... (number of records = total)
 *    set robot.quick = total (from RET @lquick )
 * 3. if total = 0 or total > 2 then do nothing, goto 2
 * 4. Assume: there is only 1 user waiting 
 * 5. if now - user.start_time  < MAX_WAIT (10 second) then goto 2
 * 6. Assume: now we have only 1 user who has waited up to 10 second
 * 7. do a quick game, when this_robot end game, it dies
 * 8. fork a new_robot before start the quick game
 * 9. CMD: quick
 *    RET: quick 0   ( or -negative, error dies)
 * 10. Wait for RET:
 *    room [channel] [room_id] [st=15] [room_pwd] [guest_info_1] [guest_info_2]
 *    guest_info = [eid] [alias] [icon]
 *    e.g. room 0 1 15 _ 1352 守护者 8 1076 大法师皮蛋 5
 *    e.g
 * 11. CMD: ginfo
 *   RET: ...
 *   send the game info to lua init_game()
 * 12. random seed knows whether this is my turn or other turn
 * 13. if this is other turn, wait for "n", then it is my turn
 * 14. if this is my turn, we will go for ai logic 
 *     lua: ai_cmd_global()
 * 
 */


// C++ header 
#include <iostream>
#include <vector>
#include <string>

using namespace std;

// C header
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
#include <sys/time.h>

#include "fatal.h"
#include "lualib.h" // implicit: luaconf.h
#include "lauxlib.h"
}

// evil.h is C++ header
#include "evil.h"  // mainly use the #define constant

// supplement to ERROR_RETURN, this goto cleanup section, usually cleanup use
// a "ret" to carry the returning error code 
#define ERROR_NEG_CLEANUP(ret, ...)	do { ERROR_NEG_PRINT((ret), __VA_ARGS__); if ((ret) < 0) { goto cleanup; } } while(0);

#define ERROR_CLEANUP(ret, ...)	do { ERROR_PRINT((ret), __VA_ARGS__); if ((ret) != 0) { goto cleanup; } } while(0);

// DB_BUFFER_MAX is from evil.h
#define BUFFER_SIZE		(DB_BUFFER_MAX+1000)// peter: grep buffer_overflow

#define MAX_CMD_SIZE	2000

#define ST_NULL		0
#define ST_LOGIN	5
#define ST_ROOM		10
#define ST_GAME		15

#define MATCH_ST_NULL			0
#define MATCH_ST_PLAYER_DATA	1
#define MATCH_ST_APPLY			2
#define MATCH_ST_WAITING		3

// use int, because we may setup in main()
int ROBOT_SLEEP	= 10;	// TODO use a longer sleep(10?) for real use! (second)
int QUICK_WAIT = 10;	// seconds to let player wait before robot start quick
int TIMEOUT_FORCE_NEXT = 80;  // idle (seconds) before robot do force next
int ROBOT_CHECK_WAIT_TIME = 30;
int WAIT_GINFO_TIME = 1;

pthread_mutex_t mylua_mutex = PTHREAD_MUTEX_INITIALIZER;

// TODO move MAX_GUEST to evil.h! 
#define MAX_GUEST	10

// @see connect_t evil.h   
// robot_t is the client version of connect_t
typedef struct _robot_struct {
	int sock;	// init 0
	int st;		// init 0

//	char cmd_list[200][1000];
	vector<string> cmd_list;
	int cmd_start_pos;
	int cmd_end_pos;


	// for read_loop to communicate
//	int cmd;	// init 0, it is a toggle between read_loop and quick_loop
	int code;	// error code from the last process_xxx 
	char last_buffer[8000];

	char cache[BUFFER_SIZE + 1];
	int cache_len;

	// for @lquick
	int quick_total;  // init 0, it will init as -1 in quick_loop
	int quick_now;
	int quick_start_time;
	time_t last_force_time;

	pthread_t robot_thread;  // use for robot_quick_loop or other loop

	// TODO no more read_thread ?
	pthread_t read_thread;	// can be bzero
	// mutex and cond should be init in init_robot and clean in shutdown_robot
	pthread_mutex_t mutex;
	pthread_cond_t cond;	// conditional wait


	evil_user_t euser;

	// room related
	int guest[MAX_GUEST];  // guest[0] is the room master
	char alias[MAX_GUEST][EVIL_ALIAS_MAX+1]; // XXX need add/remove
	// icon is useless, skip


	// game related
	int seed;
	int side;	// my side
	char deck0[EVIL_CARD_MAX+1];
	char deck1[EVIL_CARD_MAX+1];
	// runtime game
	int current_side;  // = mylua_get_int(robot.lua, "g_current_side");
	int cmd_size;		// this is for waiting my command come back
	int winner ;

	// match related
	long match_id_list[20];		// all match_id not finished, sorted by status dec
	int match_count;
	int match_ready_pos;	// match_id_list pos with status ready
	int match_cur_pos;			// for check match_id in list is valid
	time_t match_wait_time;		// check match is finished
	int match_st;

	long match_id ; // for match_loop
	int max_player; 
	int current_player;


	// ver check later
	int logic_ver;
	int game_ver;
	int client_ver;

	lua_State * lua; // init as NULL, cleanup to free it

} robot_t;


//////////////////////////////////
///////// UTILITY START //////////
//////////////////////////////////

int is_trim(char ch)
{
	const char sep[] = {'\n', ' '};	// separator to be removed
	const int sep_size = sizeof(sep);

	for (int i=0; i<sep_size; i++) {
		if (ch == sep[i]) {
			return 1;
		}
	}
	return 0;
}

// trim '\n' and ' ' for the starting and ending of str
// may return char * >= str, because the trim may occur at the beginning
char * trim(char *str)
{
	char * ptr = str;
	char * endp;
	int len;
	if (ptr == NULL) {
		BUG_PRINT(-3, "trim:null");
		return ptr;
	}

	len = strlen(ptr);
	endp = ptr + len - 1;
	while (is_trim(*ptr)) {
		ptr++;
	}

	while (endp > ptr && is_trim(*endp)) {
		*endp = '\0';
		endp--;
	}

	return ptr;
}


// handy socket write function, with format like printf
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
	printf(">>>>>> socket: %d SEND(%3d): %s\n", sock, len, buffer);
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


// read until \n  can be more than 1 lines, caller should do separator
// @return number of bytes read,  -2 is buffer overflow
// @see getline() from Standard C Library, why re-invent the wheel?
// caller should do a trim() to remove \n
// this will null-terminate the returned buffer
int read_line(robot_t &robot, int fd, char *buffer, int max)
{
	char *ptr;
	char *end = buffer + max - 1;
	int ret = 0;
	int total = 0;

	// if cache is not empty, copy cache data up to \n
	// if there is an ending \n, return immediately, else continue read

//	printf("read_line: cache_len= %d ------\n", robot.cache_len);
	if (robot.cache_len > 0) {
		robot.cache[robot.cache_len] = '\0';

		while ((ptr=strchr(robot.cache, '\n')) != NULL) {
//			printf("read_line: DIRECT RET _before_ cache (%d)=[%s]\n", robot.cache_len, robot.cache);
			total = ptr - robot.cache;
			memcpy(buffer, robot.cache, total);
			// ptr+1 to skip the \n
			memmove(robot.cache, ptr+1, robot.cache_len - total - 1);
			robot.cache_len = robot.cache_len - total - 1;  // update the cache len

			buffer[total] = 0;  // null-term it
			robot.cache[robot.cache_len] = 0;

//			printf("read_line: DIRECT RET cached content  buffer(%d)=[%s]   _after_ cache(%d)=[%s]\n", total, buffer, robot.cache_len, robot.cache);
			if (total > 0) {
				return total; // early exit
			}
			// fall through if total = 0
		}

		// we do not find the \n, copy whole cache to buffer
		// and clear the cache
		memcpy(buffer, robot.cache, robot.cache_len);
		total = robot.cache_len;   // like already read something
		robot.cache_len = 0;  // clear the cache
		buffer[total] = 0; // null terminate it for printf
//		printf("read_line:move cache to buffer total=%d [%s] \n", total, buffer);
	}
	

	ptr = buffer + total;  // may have cached content

//	printf("before read: total= %d max= %d end-ptr=%d, thread_pid=(0x%lx)------\n"
//	, total, max, (int)(end-ptr), (unsigned long)pthread_self());
	while (ptr < end && total < max) {
		char *linefeed;
		ret = read( fd, ptr, max - total);
		if (ret == 0) {
			return total;
		}
		ERROR_NEG_RETURN(ret, "read_line:read");

		total += ret;
		buffer[total] = 0; // null terminate it
		// printf("read_line:_after_read ret=%d total=%d  ptr(%p)=[%s] buffer(%p)=[%s]\n", ret, total, ptr, ptr, buffer, buffer);
//		printf("read_line:strlen=%lu\n", strlen(buffer));

		// found \n,  return
		linefeed = strchr(ptr, '\n');
		if (linefeed != NULL) {
			// when the last char is \n: linefeed - ptr == ret
			// else:  (linefeed-ptr) < ret
			// from ptr to linefeed is the content needed (up to \n)
			// from (linefeed+1) to (ptr+ret) is the content store in cache
			robot.cache_len = (ptr+ret) - (linefeed+1) ;
			memcpy(robot.cache, linefeed+1, robot.cache_len);
			robot.cache[robot.cache_len] = 0;
//			printf("read_line: store to cache len=%d [%s]\n", robot.cache_len, robot.cache);

			total -= robot.cache_len;  // reduce the cached content
			buffer[total] = 0; // null terminate it
			return total;
		}
//		if (strchr(ptr, '\n') != NULL) {
//			return total;
//		}
		ptr += ret;
	}

	FATAL_EXIT(-2, "read_line:buffer_overflow %d", total);
	return -2; 
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


// init the socket and connect to ipaddress and port
int init_socket(const char * ipaddress, int port)
{
	int sock;
	int ret;
	struct sockaddr_in addr; // should use internet address
	addr = prepare_addr(ipaddress, port);
	if (addr.sin_addr.s_addr == 0xffffffff) {
		FATAL_EXIT(-1, "prepare_addr: %s", ipaddress);
	}

	sock = socket( addr.sin_family, SOCK_STREAM, 0);
	FATAL_NEG_EXIT(sock, "socket");
	printf("init_socket: sock=%d\n", sock);

	ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	FATAL_EXIT(ret, "init_socket:connect");
	return sock;
}


// kv = "total=1"
// key = "total"
// RET value = 1
// if key is not match, value will not be set
// note: can only handle int value, not double!
int parse_key_value(const char *kv, const char *key, int &value)
{
	int ret;
	char k[100] = "_null_key_";
	char ch = 'x';
	int v  = -999;

	ret = sscanf(kv, "%[^=]%c%d", k, &ch, &v);
	if (ret != 3) {
		// ERROR_PRINT(-5, "parse_key_value:sscanf %d  [%s]", ret, kv);
		return -5;
	}

	if (0 != strcasecmp(k, key)) {
		// ERROR_PRINT(-6, "parse_key_value:key_not_match %s!=%s", k, key);
		return -6;
	}

	// ok, key matched
	value = v;
	// printf("---- good: parse_key_value : %s = %d\n", key, value);
	return 0;
}

//////////////////////////////////
///////// UTILITY END   //////////
//////////////////////////////////



/////////////////////////////
///////// LUA START /////////
/////////////////////////////

#define MYLUA_CHECK_MAGIC(lua)	do { \
if (NULL!=(lua)) { ERROR_PRINT(1974-(int)lua_tointeger((lua), -1), "MYLUA_CHECK_MAGIC"); }\
} while (0);
#define MYLUA_PUSH_MAGIC(lua) do { lua_pushinteger((lua), 1974); } while (0);

int mylua_print_stack(lua_State *L) {
	pthread_mutex_lock(&mylua_mutex);

	int top = lua_gettop(L);
	int type;
	printf("=========================\n");
	INFO_PRINT(0, "mylua_print_stack:thread_id=0x%lx top=%d"
	, (unsigned long)pthread_self(), top);
	for (int idx = top; idx > 0; idx--) {
		type = lua_type(L, idx);
		printf("\t lua_type[%d] %s : %s \n", idx
		, lua_typename(L, type), lua_tostring(L, idx));
	}
	printf("=========================\n");
	
	pthread_mutex_unlock(&mylua_mutex);
	return 0;
}


// XXX beware, need to pop after use
const char* mylua_get_string(lua_State *L, const char * var) {
	if (L == NULL) {
		WARN_PRINT(-3, "mylua_get_string:L_nil");
		return NULL;
	}
	pthread_mutex_lock(&mylua_mutex);

	lua_getglobal(L, var); // read from global with name [var], value in stack
	const char*  sss;
	size_t len = 0;
	sss = lua_tolstring(L, -1, &len); // -1 is the first in stack
	printf("CLUA : (MUST POP) get_str [%s](%s)\n", sss, var);
	
	pthread_mutex_unlock(&mylua_mutex);
	return sss;
}

int mylua_set_string(lua_State *L, const char * var, const char * str) {
	if (L == NULL) {
		WARN_PRINT(-3, "mylua_set_string:L_nil");
		return -3;
	}
	pthread_mutex_lock(&mylua_mutex);

	lua_pushstring(L, str);
	lua_setglobal(L, var);

	pthread_mutex_unlock(&mylua_mutex);
//	printf("CLUA : set_str %s=[%s]\n", var, str);
	return 0;
}


int mylua_get_int(lua_State *L, const char * var) {
	if (L == NULL) {
		WARN_PRINT(-3, "mylua_get_int:L_nil");
		return -3;
	}
	pthread_mutex_lock(&mylua_mutex);

	int value;
	lua_getglobal(L, var); // read from global with name [var], value in stack
	value = lua_tonumber(L, -1); // -1 is the first in stack
	lua_pop(L, 1);

	pthread_mutex_unlock(&mylua_mutex);
//	printf("CLUA : get_int : %d(%s)\n", value, var);
	return value;
}

int mylua_set_int(lua_State *L, const char * var, int value) {
	if (L == NULL) {
		WARN_PRINT(-3, "mylua_set_int:L_nil");
		return -3;
	}
	pthread_mutex_lock(&mylua_mutex);

	lua_pushinteger(L, value);
	lua_setglobal(L, var);

	pthread_mutex_unlock(&mylua_mutex);
//	printf("CLUA : set_int : %s=%d\n", var, value);
	return 0;
}


// t=table, i=index, n=name
// return table[index].name  as string
// caller need to allocate enough space for str, e.g. char str[max]
// if return NULL, str is not used
const char* mylua_get_table_index_name(lua_State *L, char *str, int max
, const char *table, int index, const char *name) {
	if (L == NULL) {
		WARN_PRINT(-3, "mylua_get_table_index_name:L_nil");
		return NULL;
	}
	int ret;
	const char *ptr;
	char fmt[10];
	size_t size = 0;
	pthread_mutex_lock(&mylua_mutex);

	lua_getglobal(L, table); 
	ret = lua_type(L, -1);
	if (ret != LUA_TTABLE) {
		BUG_PRINT(-6, "getglobal: %s type=%d", table, ret);
		lua_pop(L, 1); // remove the get global
		str = NULL;
		goto cleanup;
	}

	lua_rawgeti(L, -1, index); // rusty sword test
	ret = lua_type(L, -1);
	if (ret != LUA_TTABLE) {
		// this can be warning later
		BUG_PRINT(-16, "rawgeti: %d type=%d", index, ret);
		lua_pop(L, 2);
		str = NULL;
		goto cleanup;
	}

	lua_getfield(L, -1, name);
	ret = lua_type(L, -1);
	if (ret != LUA_TSTRING) {
		BUG_PRINT(-26, "getfield: %s type=%d", name, ret);
		lua_pop(L, 3);
		str = NULL;
		goto cleanup;
	}
	ptr = lua_tolstring(L, -1, &size);
	if (ptr == NULL) {
		BUG_PRINT(-36, "tolstring_null: %s[%d].%s", table, index, name);
		lua_pop(L, 3);
		str = NULL;
		goto cleanup;
	}
	sprintf(fmt, "%%.%ds", max-1);
	sprintf(str, fmt, ptr);
	lua_pop(L, 3); // balance the lua stack

cleanup:
	pthread_mutex_unlock(&mylua_mutex);
	return str;
}

// TODO  get: table[index] return on str
const char* mylua_get_table_index(lua_State *L, char *str, int max
, const char *table, int index) {
	return NULL;
}


// handly lua new state
// remember to add $(FLAGS_LUA) in Makefile for Mac 
// -pagezero_size 10000 -image_base 100000000
lua_State * mylua_open(int lang)
{
	lua_State * lua;
	int ret;
	pthread_mutex_lock(&mylua_mutex);

	lua = luaL_newstate();	// global access
	luaL_openlibs(lua);  // peter: copy somewhere else
	pthread_mutex_unlock(&mylua_mutex);

	// this action will lock mylua_mutex
	// which should be unlocked before
	mylua_set_int(lua, "g_ui", 1);

	pthread_mutex_lock(&mylua_mutex);
	ret = luaL_dofile(lua, "res/lua/logic.lua");  // LANG was logic.lua
	FATAL_EXIT(ret, "mylua_open:dofile:logic.lua");
	if (lang == 1) {
		ret = luaL_dofile(lua, "res/lua/lang_zh.lua");  // LANG was logic.lua
	}
	MYLUA_PUSH_MAGIC(lua);

	pthread_mutex_unlock(&mylua_mutex);
	return lua;
}

// usage:  lua = mylua_close(lua);
lua_State * mylua_close(lua_State * lua)
{
	if (lua == NULL) {
		WARN_PRINT(-3, "mylua_close:null_lua");
		return NULL;
	}
	mylua_print_stack(lua);

	pthread_mutex_lock(&mylua_mutex);

	MYLUA_CHECK_MAGIC(lua);
	lua_pop(lua, 1);

	// avoid lock mylua_mutex after locked
//	mylua_print_stack(lua);

	lua_close(lua);

	pthread_mutex_unlock(&mylua_mutex);
	return NULL;
}


static int mylua_traceback (lua_State *L) {
//	pthread_mutex_lock(&mylua_mutex);

	printf("-------- mylua_traceback:\n");
	if (!lua_isstring(L, 1)) { /* 'message' not a string? */
		printf("BUGBUG mylua_traceback stack 1 not a string\n");
		// return 1;  /* keep it intact */
		goto cleanup;
	}
	lua_getfield(L, LUA_GLOBALSINDEX, "debug");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		goto cleanup;
	}
	lua_getfield(L, -1, "traceback");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 2);
		goto cleanup;
	}
	lua_pushvalue(L, 1);  /* pass error message */
	lua_pushinteger(L, 2);  /* skip this function and traceback */
	lua_call(L, 2, 1);  /* call debug.traceback */

cleanup:
//	pthread_mutex_unlock(&mylua_mutex);
	return 1;
}

int mylua_trace_call(lua_State *L, int narg, int result)
{
	// this function is called by lua
	// should not be locked by mylua_mutex
//	pthread_mutex_lock(&mylua_mutex);

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

//	pthread_mutex_unlock(&mylua_mutex);
	return status;
}

// function logic_init(hero1, hero2, deck1, deck2, seed, start_side) -- {
// function logic_init_array(deck1_array, deck2_array, seed, start_side) -- {
// deck1, deck2 are 400 character array
int mylua_logic_init(lua_State *L, const char *deck1, const char *deck2
, int seed, int start_side)
{
	pthread_mutex_lock(&mylua_mutex);

	lua_getglobal(L, "logic_init_array");
	lua_pushstring(L, deck1);
	lua_pushstring(L, deck2);
	lua_pushinteger(L, seed);
	lua_pushinteger(L, start_side);
	int ret;
	ret = lua_pcall(L, 4, 1, 0);

	if (ret != 0) {
		lua_pop(L, 1);  // skip the handler
		ret = -8;
		ERROR_CLEANUP(-8, "mylua_logic_init:pcall %d", ret);
	}

	ret = lua_tointeger(L, -1);
	lua_pop(L, 1);

	DEBUG_PRINT(ret, "mylua_logic_init");
	MYLUA_CHECK_MAGIC(L);

cleanup:
	pthread_mutex_unlock(&mylua_mutex);

	return ret;
}

// cmd must have at least 100
int mylua_get_ai_cmd(lua_State *lua, char *cmd) {
	if (lua == NULL) {
		WARN_PRINT(-3, "mylua_get_ai_cmd:lua_nil");
		return -3;
	}
	pthread_mutex_lock(&mylua_mutex);

	int ret;
	lua_getglobal(lua, "ai_cmd_global");
	// ret = lua_pcall(g_lua, 0, 1, 0);  // 1=input param, 1=output return
	ret = mylua_trace_call(lua, 0, 1); // 1=input param, 1=output return
	if (ret != 0) {
		// XXX TODO send 'n' to skip this ai round !
		ERROR_PRINT(ret, "mylua_get_ai_cmd:error");
		MYLUA_CHECK_MAGIC(lua);
		strcpy(cmd, "n");  // use next for error
		// return -68;
		lua_pushstring(lua, "n");
		ret = -7;
		goto cleanup;
	}
	cmd[99] = '\0';
	// char out_cmd[101] = {'\0'};
	strncpy(cmd, lua_tostring(lua, -1), 99); cmd[99] = '\0';
	WARN_PRINT( strlen(cmd) > 90, "ai_cmd_global:cmd_too_long %zu", strlen(cmd));
	lua_pop(lua, 1); // pop the cmd returned 
	
	MYLUA_CHECK_MAGIC(lua);

	ret = 0;
cleanup:
	pthread_mutex_unlock(&mylua_mutex);
	return ret;
}

/**
 * @return <0 for error,  =0 not yet have winner, >0 means winner
 */
int mylua_play_cmd_global(lua_State *L, const char * buffer) 
{
	if (L == NULL) {
		WARN_PRINT(-3, "mylua_play_cmd_global:L_nil");
		return -1;
	}
	pthread_mutex_lock(&mylua_mutex);

	int ret;
	int winner;
	lua_getglobal(L, "play_cmd_global");	
	// printf("C: -- play_cmd_global : (%s)\n", buffer);
	lua_pushstring(L, buffer); 
	// (lua_state, num_param, num_return, ???)
	ret = lua_pcall(L, 1, 4, 0);  // balance AAA, expect: ret=0
	ERROR_NEG_CLEANUP(ret, "mylua_play_cmd_global_111");  // ret != 0, print error
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
	ERROR_NEG_CLEANUP(ret, "mylua_play_cmd_global_222");
	
cleanup:
	pthread_mutex_unlock(&mylua_mutex);
	if (ret < 0) {
		return ret;
	}
	return winner;
}


int mylua_print_index_both(lua_State *L) 
{
	pthread_mutex_lock(&mylua_mutex);

	int ret;
	lua_getglobal(L, "print_index_both");	
	lua_getglobal(L, "g_logic_table");
	// (lua_state, num_param, num_return, ???)
	ret = lua_pcall(L, 1, 0, 0);  // expect: ret=0
	// peter: do we need to remove 2 elements from stack ?
	ERROR_NEG_PRINT(ret, "mylua_print_index_both");

	pthread_mutex_unlock(&mylua_mutex);
	return ret;
}

int mylua_print_status(lua_State *L, int my_side) 
{
	pthread_mutex_lock(&mylua_mutex);

	int ret;
	lua_getglobal(L, "print_status");	
	lua_pushnumber(L, my_side);
	ret = lua_pcall(L, 1, 0, 0);  // expect: ret=0

	pthread_mutex_unlock(&mylua_mutex);
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


/////////////////////////////
///////// LUA END ///////////
/////////////////////////////

// handy print function for debug
void print_euser(evil_user_t & euser)
{
	printf("user: eid=%d  username=%s  alias=%s  lv=%d\n"
	, euser.eid, euser.username, euser.alias, euser.lv);
}

void print_robot(robot_t &robot)
{
	printf("robot: st=%d  sock=%d  logic_ver=%d  lua=%s\n"
	, robot.st, robot.sock, robot.logic_ver
	, robot.lua==NULL ? "_null_" : "YES");
	print_euser(robot.euser);
}

void print_room(robot_t &robot)
{
	printf("room: st=%d   guest1(%d)=%s  guest2(%d)=%s\n"
	, robot.st, robot.guest[0], robot.alias[0], robot.guest[1], robot.alias[1]);
}

void print_game(robot_t &robot)
{
	printf("game: st=%d  seed=%d  cmd_size=%d  guest1(%d)=%s  guest2(%d)=%s\n"
	, robot.st, robot.seed, robot.cmd_size
	, robot.guest[0], robot.alias[0]
	, robot.guest[1], robot.alias[1]);

	printf("    deck0: %400s\n", robot.deck0);
	printf("    deck1: %400s\n", robot.deck1);

}

void print_runtime_game(robot_t &robot)
{
	mylua_print_index_both(robot.lua);
	mylua_print_status(robot.lua, robot.side);
}

#define WAIT_EQUAL	1
#define WAIT_NOT_EQUAL	2
#define WAIT_LESS	3
#define WAIT_MORE	4


// it may return ETIMEDOUT
int wait_cmd_list(robot_t &robot, int timeout)
{
	pthread_mutex_lock(&robot.mutex);
	int ret = 0;

	// no wait if cmd_list.size > 0
//	if (robot.cmd_end_pos > 0) {
//		goto cleanup;
//	}
	if (robot.cmd_list.size() > 0) {
		goto cleanup;
	}

	if (timeout == 0) {
		ret = pthread_cond_wait(&robot.cond, &robot.mutex);
	} else {
		struct timespec abstime;
		struct timeval now;
		gettimeofday(&now, NULL);
		abstime.tv_sec = now.tv_sec + timeout;
		abstime.tv_nsec = 0;
		ret = pthread_cond_timedwait(&robot.cond, &robot.mutex, &abstime);
	}

cleanup:
	pthread_mutex_unlock(&robot.mutex);
	return ret;
}

// return 0 or 1 : 1 means condition match
int check_condition(int &key, int &value, int wait_check)
{
	int flag = 1;  // 0 means no wait
	switch (wait_check)
	{
	case WAIT_EQUAL:
		flag = (key==value) ;
		break;
	case WAIT_NOT_EQUAL:
		flag = (key!=value);
		break;
	case WAIT_LESS:
		flag = (key < value);
		break;
	case WAIT_MORE:
		flag = (key > value);
		break;
	default:
		flag = 1;  // let it wait forever!
		ERROR_PRINT(-10, "check_condition:unknown_wait_check:%d", wait_check);
		break;
	}
	return flag;
}


int wait_condition(robot_t & robot, int &key, int &value, int wait_check
, int timeout, const char * tag)
{
	int ret = 0;

	// at most 10 times
	for (int i=0; i<10; i++) {
		if (0 == check_condition(key, value, wait_check)) {
			return 0;
		}
		ret = wait_cmd_list(robot, timeout);
		WARN_PRINT(ret<0, "wait_condition:timeout:%s", tag);
		if (ret < 0) {
			return ret;
		}
	}

	ret = -2;
	ERROR_PRINT(-2, "wait_condition:overflow:%s  last_buffer=%s", tag, robot.last_buffer);
	return -2;
}

int signal_cmd(robot_t & robot)
{
	pthread_mutex_lock(&robot.mutex);
//	robot.cmd ++;

	pthread_cond_signal(&robot.cond);

//	cleanup:
	pthread_mutex_unlock(&robot.mutex);
	return 0;
}

//////////////////////////////////
////////// COMMAND START /////////
//////////////////////////////////
// process server ret command 

// ginfo 1 0 25505 0 0000100000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000

// game [side] [cmd_size] [seed] [timeout] [deck_400_0] [deck_400_1]
// init local deck0, deck1 to lua
int process_ginfo(robot_t &robot, const char *cmd, const char * buffer)
{
	int ret;
	int n;
	int side = 0;
	int cmd_size = 0;
	char tmp[100];
	int seed = 777;
//	int start_side = 1;  // peter: is it useless?  seed odd/even = start_side?
	int timeout = 0; // count down
	char deck0[EVIL_CARD_MAX + 1];
	char deck1[EVIL_CARD_MAX + 1];
	const char * ptr;

	// safety
	bzero(deck0, EVIL_CARD_MAX+1);
	bzero(deck1, EVIL_CARD_MAX+1);

	ret = sscanf(buffer, "%99s %d %n", tmp, &side, &n);
	if (ret < 2) {
		ERROR_PRINT(-5, "process_ginfo:sscanf %d", ret);
		ret = -5;
		goto cleanup;
	}

	if (side < 0) {
		ret = side;
		ERROR_NEG_CLEANUP(ret, "process_ginfo:side");
	}

	ptr = buffer + n;
	ret = sscanf(ptr, "%d %d %d %s %s", &cmd_size, &seed
	, &timeout, deck0, deck1);

	if (ret != 5) {
		ERROR_PRINT(-15, "process_ginfo:sscanf222 %d", ret);
		ret = -15;
		goto cleanup;
	}

	// run_robot should init the lua, before calling ginfo
	// run_robot do the lua init
//	if (robot.lua == NULL) {
//		ret = -3;
//		ERROR_NEG_CLEANUP(ret, "process_ginfo:lua_null");
//	}

	robot.side = side;
	robot.seed = seed;
	robot.cmd_size = cmd_size;
	memcpy(robot.deck0, deck0, EVIL_CARD_MAX+1);
	memcpy(robot.deck1, deck1, EVIL_CARD_MAX+1);
	printf("--- process_ginfo set robot.side = %d\n", robot.side);

	// @see run_robot
//	robot.lua = mylua_open(0);	// seems multi-thread does not work ?

//	ret = mylua_logic_init(robot.lua, deck0, deck1, seed, start_side);
//	ERROR_NEG_CLEANUP(ret, "process_ginfo:mylua_logic_init");

//	pthread_mutex_lock(&robot.mutex);
	// init lua
	if (robot.lua != NULL) {
		ret = -7;
		ERROR_NEG_CLEANUP(ret, "process_ginfo:lua_not_null_before_mylua_open");
	}
	// core logic: init the lua

	robot.lua = mylua_open(1);

	ret = mylua_logic_init(robot.lua, robot.deck0, robot.deck1, robot.seed, 1);
	robot.current_side = mylua_get_int(robot.lua, "g_current_side");

	robot.winner = 0;
	INFO_PRINT(1, "socket %d robot_cur_side=%d side=%d"
	, robot.sock, robot.current_side, robot.side);

	ret = 0; // normal
	print_robot(robot);
	print_game(robot);
	INFO_PRINT(1, "socket %d robot_cur_side=%d side=%d"
	, robot.sock, robot.current_side, robot.side);

cleanup:

	robot.last_force_time = time(NULL);

	return ret;
}

int process_log(robot_t &robot, const char *cmd, const char * buffer)
{
	int ret;
	char tmp[100];
	int exp_next;	//skip this
	int exp_this;	//skip this

	evil_user_t &euser = robot.euser;

	// do not set robot.st , use signal_robot_value
	ret = sscanf(buffer, "%s %d %d %s %d %d %d %s %d %d %d %d %d %d %d"
	, tmp, &euser.eid, &robot.st, euser.alias
	, &euser.icon, &euser.gid, &euser.gpos, euser.gname
	, &euser.lv, &euser.exp, &exp_next, &exp_this, &robot.logic_ver
	, &robot.game_ver, &robot.client_ver);

	printf("process log: ret=%d\n", ret );
	print_euser(euser);

	robot.code = euser.eid;

	robot.match_st = MATCH_ST_NULL;
	robot.match_id = 0;

	return 0;
}



// 2 types of @lquick RET:
// @lquick total=1 now=1419672143
// @lquick eid=548  rating=1319.220000  start_time=1419672140
int process_lquick(robot_t &robot, const char *cmd, const char * buffer)
{
	int ret;
	char tmp[100]; // for command
	char str1[100] = ""; // total=1  or  eid=548
	char str2[100] = ""; // now=141... or rating=1319...
	char str3[100] = ""; // start_time=141... or empty
	// int total;
	ret = sscanf(buffer, "%s %s %s %s" , tmp, str1, str2, str3);

	// this is bad
	if (ret < 3) {
		ERROR_RETURN(-5, "process_lquick:sscanf %d [%s]", ret, buffer);
	}

	// order is important, must parse "now" before "total" because total will
	parse_key_value(str2, "now", robot.quick_now);

	parse_key_value(str1, "total", robot.quick_total);

	// when total==1 we need the second type of @lquick to get
	// start_time and do signal_robot

	ret = parse_key_value(str3, "start_time", robot.quick_start_time);
	if (ret >= 0 && robot.quick_total == 1) {
		//INFO_PRINT(robot.quick_total, "process_lquick:quick_total=%d  now=%d  start_time=%d  diff=%d"
		//, robot.quick_total, robot.quick_now, robot.quick_start_time
		//, (robot.quick_now - robot.quick_start_time));
	}

	return 0;
}


// quick 0 is normal, do not signal
// other ret is error, signal
int process_quick(robot_t &robot, const char *cmd, const char * buffer)
{
//	int ret;
	char tmp[100];
//	ret = sscanf(buffer, "%s %d" , tmp, &robot.code);
	sscanf(buffer, "%s %d" , tmp, &robot.code);
	return 0;
}


// room 0 1 15 _ 548 y 8 547 x 0
// room [channel] [room_id] [st] [password] [guest_info1] [guest_info2] ...
// guest_info = [eid] [alias] [icon]
int process_room(robot_t &robot, const char *cmd, const char * buffer)
{
	printf("----- process_room\n");
	int ret;
	//int code = -1; // code is also channel, useless
	int n;
	const char *ptr;
	int icon; // useless
	int room_id; // useless
	char tmp[100]; // useless
	char pwd[100]; // useless
	// room [channel] [room_id] [st] [password] [guest_info1] [guest_info2] ...
	ret = sscanf(buffer, "%s %d %d %d %s %n" 
	, tmp, &robot.code, &room_id, &robot.st, pwd, &n);

	if (robot.code < 0) {
		return 0;
	}

	// core logic: should be st=15
	if (robot.st != 15) {
		ERROR_PRINT(-6, "process_room:st!=15 %d", robot.st);
		robot.code = -9;
		return 0;
	}

	ptr = buffer + n;
	// only fill up 2 guests
	for (int i=0; i<2; i++) {
		ret = sscanf(ptr, "%d %s %d %n"
		, &robot.guest[i], robot.alias[i], &icon, &n);
		if (ret != 3) {
			ERROR_PRINT(-5, "process_room:guest[%d] ret=%d ptr=%s", i, ret, ptr);
			break;
		}
		ptr += n;
	}
	// for debug
	print_room(robot);
	robot.match_wait_time = time(NULL);

	return 0;
}

int process_mchange(robot_t &robot, const char *cmd, const char * buffer)
{
	// completely ignore!
	// no need to signal, but we need to make it normal
	robot.code = 0;
	return 0;
}

int process_wchat(robot_t &robot, const char *cmd, const char * buffer)
{
	printf("worldchat: %s\n", buffer);
	return 0;
}

int process_rchat(robot_t &robot, const char *cmd, const char * buffer)
{
	printf("roomchat: %s\n", buffer);
	return 0;
}

// RECV( 39); [win 2 1 3.000000 4800 480 11 7700 817 0]
// win [winner] [fold_flag] [rating] [gold] [exp] [lv] [next_exp] [exp] [card]
int process_win(robot_t &robot, const char *cmd, const char * buffer)
{
	printf("----- process_win\n");
	int ret;
	int gold, exp, lv, next_exp, card_id; // useless
	int fold_flag;
	char tmp[101];

	ret = sscanf(buffer, "%s %d %d %lf %d %d %d %d %d %d"
	, tmp, &robot.winner, &fold_flag, &robot.euser.rating, &gold, &exp, &lv, &next_exp
	, &exp, &card_id);
	
	if (ret != 10) {
		ERROR_PRINT(-5, "process_win:sscanf %d", ret);
		ret = -5;
		goto cleanup;
	}
	
	INFO_PRINT(1
	, "@@@@@@@@@@@@@@@@ process_win: winner=%d my_side=%d   %s(%d)_vs_%s(%d)\n"
	, robot.winner, robot.side, robot.alias[0], robot.guest[0]
	, robot.alias[1], robot.guest[1]);

cleanup:

	robot.lua = mylua_close(robot.lua);
	robot.code = 0; 
	robot.cmd_size = 0;
	robot.side = 0;
	robot.st = ST_LOGIN;
	
	return 0;
}

// cmd = "n", "t", "b",   buffer is the full command, include prefix number
// fold ?  f ?
int process_play(robot_t &robot, const char *cmd, const char * buffer)
{
	printf("----- process_play\n");
	int ret;
	int n;
	int code;
	const char *ptr;

	ret = sscanf(buffer, "%d %n", &code, &n);
	if (ret != 1) {
		ret = -1;
		goto cleanup;
	}
	ptr = buffer + n;

	INFO_PRINT(1, "---- process_play: cmd=[%s]  full_cmd=[%s]\n", cmd, ptr);
	
	// lua access must be lock
	ret = mylua_play_cmd_global(robot.lua, ptr);
	if (ret > 0) {
		robot.winner = ret;
	}
	robot.current_side = mylua_get_int(robot.lua, "g_current_side");
	INFO_PRINT(1, "process_player robot.size = %d robot.current_side=%d"
	, robot.side, mylua_get_int(robot.lua, "g_current_side"));

	ERROR_NEG_PRINT(ret, "process_play:mylua_play_cmd_global %s", ptr);

cleanup:
	robot.last_force_time = time(NULL);
	robot.code = 0;
	return 0;
}


/**
 * let the robot thread handle, just place robot.code 
 */
int process_gerr(robot_t &robot, const char *cmd, const char * buffer)
{
	char tmp[100];
	// no more checking
	sscanf(buffer, "%s %d", tmp, &robot.code);
	printf("process_gerr: %s\n", buffer);
	return 0;
}

int process_lmatch(robot_t &robot, const char *cmd, const char * buffer)
{
	char cmd_title[105];
	int num_match = 0;
	int ret;
	int n;
	const char* ptr;
	time_t match_id;
	int status;
	int max_player, cur_player;
	time_t start_time;
	time_t match_round_time;
	time_t tmp_time;
	int daily_round;
	int match_id_ready_list[10];
	int match_id_running_list[10];
	int match_ready_count, match_running_count;

	ret = sscanf(buffer, "%s %d %n", cmd_title, &num_match, &n);
	
	if (ret != 2) {
		robot.match_id = -5;
		ERROR_RETURN(-5, "process_lmatch:sscanf %d", ret);
	}

	if (num_match <= 0) {
		robot.match_id = -6;
		ERROR_RETURN(-6, "process_lmatch:num_match %d", num_match);
	}

	// scan only the first match (for testing is enough)
	// [match_id] [status] [title] [max_player] [current_player] [start_time] 
	// [MATCH_ROUND_TIME] [daily_info]
	// daily_info = max_daily_round t1 t2 ... tn
	match_ready_count = 0;
	match_running_count = 0;
	ptr = buffer + n;
	for (int i = 0; i < num_match; i++) {
		ret = sscanf(ptr, "%ld %d %s %d %d %ld %ld %d %n", &match_id, &status
		, cmd_title, &max_player, &cur_player, &start_time, &match_round_time
		, &daily_round, &n);
		if (ret != 8) {
			robot.match_id = -15;
			ERROR_RETURN(-15, "process_lmatch:match_info_sscanf %d", ret);
		}
		ptr = ptr + n;
		for (int r = 0; r < daily_round; r++) {
			ret = sscanf(ptr, "%ld %n", &tmp_time, &n);
			if (ret != 1) {
				ERROR_RETURN(-25, "process_lmatch:match_info_sscanf %d", ret);
			}
			ptr = ptr + n;
		}

		switch (status) {
			case 3:		// MATCH_STATUS_FINISH
			{
				if (match_id == robot.match_id) {
					// last match has finished, exit
					robot.match_st = MATCH_ST_NULL;
					ERROR_RETURN(-6, "process_lmatch:match_has_finished");
				}
				continue;
			}
			case 0:		// MATCH_STATUS_READY
			{
				match_id_ready_list[match_ready_count++] = match_id;
				break;
			}
			case 1:		// MATCH_STATUS_ROUND_START
			case 2:		// MATCH_STATUS_ROUND_END
			{
				match_id_running_list[match_running_count++] = match_id;
				break;
			}
			default:
			{
				ERROR_RETURN(-35, "process_lmatch:match_status_error: %d", status);
			}
		}
	}

	robot.match_count = 0;
	for (int i = 0; i < match_running_count; i++) {
		robot.match_id_list[robot.match_count++] = match_id_running_list[i];
	}
	robot.match_ready_pos = robot.match_count;
	for (int i = 0; i < match_ready_count; i++) {
		robot.match_id_list[robot.match_count++] = match_id_ready_list[i];
	}
	robot.match_cur_pos = 0;

//	robot.match_id = robot.match_id_list[0];
//	if (robot.match_id <= 0) {
//		ERROR_RETURN(-3, "process_lmatch:no_match_to_apply");
//	}
	if (robot.match_count <= 0) {
		robot.match_st = MATCH_ST_NULL;
		ERROR_RETURN(-3, "process_lmatch: no_match_to_apply");
	}

	robot.match_wait_time = time(NULL);
	robot.match_st = (robot.match_id > 0)
	? MATCH_ST_WAITING : MATCH_ST_PLAYER_DATA;

	return 0;
}


int process_match_apply(robot_t &robot, const char *cmd, const char * buffer)
{
	char tmp[100];
	int ret;
	long match_id = -95; // local

	ret = sscanf(buffer, "%s %ld %d %d", tmp, &match_id, &robot.max_player, &robot.current_player);

	// already apply case
	if (match_id == -6) {
		BUG_PRINT(-6, "process_match_apply:match_already_apply or match_started");
		robot.match_ready_pos++;

		if (robot.match_ready_pos >= robot.match_count) {
			robot.match_st = MATCH_ST_NULL;
		}
		return 0; // let it be OK
	}

	ERROR_RETURN(ret!=4, "process_match_apply:sscanf %d", ret);
	// TODO check match_id and robot.match_id

	robot.match_id = match_id;
	robot.match_st = MATCH_ST_WAITING;
	return 0;
}


int process_player_data(robot_t &robot, const char *cmd, const char *buffer)
{
	char tmp[105];
	int ret;
	long match_id = -95;
	int n;

	ret = sscanf(buffer, "%s %ld %n", tmp, &match_id, &n);
	if (ret != 2) {
		ERROR_RETURN(-5, "process_player_data:sscanf %d", ret);
	}

	if (match_id <= 0) {
		robot.match_cur_pos++;
		if (robot.match_cur_pos >= robot.match_count) {
			robot.match_st = (robot.match_ready_pos < robot.match_count)
			? MATCH_ST_APPLY : MATCH_ST_NULL;
		}
		WARN_PRINT(-3, "process_player_data:no_such_player_data");
		return 0;
	}

	robot.match_id = match_id;
	robot.match_st = MATCH_ST_WAITING;
	return 0;
}



typedef struct {
	const char * cmd;
	int (* fun) (robot_t &, const char *, const char *);
} command_t;

command_t command_list[] = {
	{ "log", process_log }
,	{ "@lquick", process_lquick }
,	{ "quick", process_quick }
,	{ "room", process_room }
,	{ "ginfo", process_ginfo}
,	{ "win", process_win}
,	{ "mchange", process_mchange}	// simply ignore
,	{ "wchat", process_wchat}	// simply ignore
,	{ "rchat", process_rchat}	// simply ignore
,	{ "gerr", process_gerr }
,	{ "lmatch", process_lmatch} 
,	{ "match_apply", process_match_apply} 
,	{ "player_data", process_player_data }
};

const int TOTAL_COMMAND = sizeof(command_list) / sizeof(command_t);


// process_command robot version
int process_robot(robot_t &robot, const char * buffer)
{
	char cmd[100+1] = "";
	int n;
	int num;
	int ret;
	const char *ptr;

	// this is for play command with a prefix number
	ptr = buffer;
	ret = sscanf(ptr, "%d %n", &num, &n);
	if (ret >= 1) {
		ptr = ptr + n;
		ret = sscanf(ptr, "%20s %n", cmd, &n);
		if (ret != 1) {
			ERROR_RETURN(-5, "process_robot:unknown_cmd %s", buffer);
		}
		// buffer = 1 n 1
		ret = process_play(robot, cmd, buffer); // cmd = "n", "t", "b"
		strcpy(robot.last_buffer, buffer);  // for debug

		INFO_PRINT(1, "process_play: cmd=%s", cmd);
		return ret;
	}

	// normal command
	ret = sscanf(buffer, "%100s %n", cmd, &n);
	ERROR_RETURN(ret != 1, "process_robot:sscanf %d", ret);

	for (int i=0; i<TOTAL_COMMAND; i++) {
		if (0 != strcasecmp(cmd, command_list[i].cmd)) {
			continue; // early continue
		}
		// note: we send buffer, not buffer+n
		ret = command_list[i].fun(robot, cmd, buffer);
		strcpy(robot.last_buffer, buffer);  // for debug
		return ret;
	}

	ERROR_PRINT(-1, "process_robot:unknown_command %s", cmd);
	return 0;
}

//////////////////////////////////
////////// COMMAND END ///////////
//////////////////////////////////

typedef void * (*thread_loop_t)(void*);

// ptr is the pointer to robot_t 
// note: it will send condition signal to run_robot thread (main thread)
//       via process_robot() -> cmd_xxx()
void * robot_net_loop(void *robot_ptr)
{
	char buffer[BUFFER_SIZE + 5];  // +5 for safety, +1 for \0
	FATAL_EXIT(robot_ptr==NULL, "robot_net_loop:null_robot_ptr");
	robot_t & robot = *(robot_t *)robot_ptr;
	int sock = robot.sock;
	int running = 1;
	int ret;
	char * ptr;	// for pointing buffer

	
	printf("robot_net_loop sock=%d\n", sock);
	while (0 != running) {
		buffer[0] = 0; // clean up buffer length

		ret = read_line(robot, sock, buffer, BUFFER_SIZE);
		FATAL_NEG_EXIT(BUFFER_SIZE-ret, "read_loop:read_overflow %d", ret);

		// this is EOF or error
		if (ret <= 0) {
			INFO_PRINT(ret, "read_loop:end ret=%d errno=%d", ret, errno);
			break;
		}

		// assume buffer is valid after read_line
		// TODO we may need to trim multi-linefeed
		buffer[ret] = 0;
		ptr = trim(buffer);
		ret = strlen(ptr);
		if (ret <= 0) {
			continue; // skip empty command
		}

		printf("<<<<<< socket %d RECV(%3d); [%s]\n", sock, ret, ptr);

//		ret = process_robot(robot, ptr);

		pthread_mutex_lock(&robot.mutex);
		// use cmd_list to save cmd, for logic thread get and run
	//	sprintf(robot.cmd_list[robot.cmd_end_pos], "%s", ptr);
		char cmd[1000];
		sprintf(cmd, "%s", ptr);
		robot.cmd_list.push_back(string(cmd));
		
//		robot.cmd_end_pos++;
//		if (robot.cmd_end_pos >= MAX_CMD_SIZE) {
//			ERROR_PRINT(-2, "robot_net_loop: cmd_size_out_bound!!!");
//		}

		if (robot.cache_len <= 0) {
			pthread_cond_signal(&robot.cond);
		}

		pthread_mutex_unlock(&robot.mutex);

	}

	INFO_PRINT(0, "robot_net_loop END\n");
	pthread_mutex_lock(&robot.mutex);
	close(robot.sock);
	robot.sock = 0;
	pthread_cond_signal(&robot.cond);
	pthread_mutex_unlock(&robot.mutex);

	return robot_ptr;
}


void sys_shutdown()
{
	printf("QUIT robot shutdown\n");
	fflush(stdout);
	// usleep(500000); // 0.5 sec
	exit(0);
}


void sig_handler(int sig) 
{
	pthread_t pid = pthread_self();
	printf("sig_handler: sig=%d thread_id=(0x%lx)\n", sig, (unsigned long)pid);
	if (sig==SIGPIPE) {
		sys_shutdown();
		return;
	}
	
	sys_shutdown();
}

int shutdown_robot(robot_t &robot)
{
	int ret;

	// TODO kill the read thread , detach is not tested, is it working?
	ret = pthread_cancel(robot.read_thread);
	ERROR_NEG_PRINT(ret, "shutdown_robot:pthread_cancel ret=%d", ret);

	ret = pthread_join(robot.read_thread, NULL);
	ERROR_NEG_PRINT(ret, "shutdown_robot:pthread_join ret=%d", ret);

	ret = pthread_mutex_destroy(&robot.mutex);
	ERROR_PRINT(ret, "shutdown_robot:mutex_destroy");
	
	ret = pthread_cond_destroy(&robot.cond);
	ERROR_PRINT(ret, "shutdown_robot:cond_destroy");

	// XXX not sure whether we should close before or after mutex destroy?
	// safe close
	if (robot.sock != 0) {
		close(robot.sock);
		robot.sock = 0;  // mark as disconnect
	}
	robot.lua = mylua_close(robot.lua);
	return 0;
}

#define SLEEPTEST()	do { } while(0);







int test0_null(int argc, char *argv[])
{
	printf("----- test0_null has nothing!\n");
	return 0;
}


// robot 1 1 
int test1_server(int argc, char * argv[])
{
	int ret;
	//int sock;
	int listen_fd;
	struct sockaddr_in address;

	if (argc <= 2) {
		printf("test1 need one more param: robot 1 1\n");
		return 0;
	}


	address = prepare_addr("127.0.0.1", 7710);
	listen_fd = socket(address.sin_family, SOCK_STREAM, 0);
	FATAL_NEG_EXIT(listen_fd, "socket()");

   int on = 1;
    // make it easy to re-use the same address (avoid stop/start bind issue)
    ret = setsockopt( listen_fd, SOL_SOCKET, SO_REUSEADDR,
        (const void *)&on, sizeof(on));

	FATAL_NEG_EXIT(ret, "setsockopt");

	ret = bind( listen_fd, (struct sockaddr*)&address, sizeof(address));
	FATAL_NEG_EXIT(ret, "socket bind IP");

	ret = listen( listen_fd, 1024);
	FATAL_NEG_EXIT(ret, "listen");

	//////////////////////// 
	// accept once and send something, sleep 5 seconds, send another
	// linefeed in the middle to test read_line
	////////////////////////

	struct sockaddr_in client_addr;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	int client_fd;

	printf("_before_ accept\n");
	client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
	FATAL_NEG_EXIT(client_fd, "accept");
	printf("_after_ accept\n");

	sock_write(client_fd, "\n\nABC");
	sleep(1);
	sock_write(client_fd, "111\n222");
	sleep(3);
	sock_write(client_fd, "333\n\n\n");

	sleep(1);
	sock_write(client_fd, "444\n");

	close(client_fd);
	close(listen_fd);

	return 0;
}


typedef int (*testcase_t) (int, char*[]); //  testcase_t;

testcase_t test_list[] = {
	test0_null
,	test1_server
};


int testcase(int num, int argc, char *argv[])
{
	int ret;
	int test_max = sizeof(test_list) / sizeof(test_list[0]);
	if (num < 0 || num >= test_max) {
		printf("ERROR invalid testcase %d\n", num);
		return -2;
	}

	printf("RUN test%d:\n", num);
	ret = test_list[num](argc, argv);
	printf("RET %d\n", ret);
	ERROR_NEG_PRINT(ret, "XXXXXXXXXX BUG ret %d\n", ret);

	return ret;
}






/**
 *
 * do login
 *
 */
int do_robot_login(robot_t &robot)
{
	int ret;
	const int sock = robot.sock;
	evil_user_t & euser = robot.euser;

	INFO_PRINT(0, " ---- before login: sock=%d user=%s pwd=%s\n", sock
	, euser.username, euser.password);

	ret = sock_write(sock, "log %s %s\n", euser.username, euser.password);
	ERROR_NEG_RETURN(ret, "sock_write:login_error");
	
	return 0;
}


/**
 *
 * list match data
 *
 */
int do_robot_lmatch(robot_t &robot)
{
	
	int ret;
	const int sock = robot.sock;

	WARN_PRINT(robot.match_id > 0, "do_robot_lmatch: robot.match_id %ld"
	, robot.match_id);

	ret = sock_write(sock, "lmatch\n");
	ERROR_NEG_RETURN(ret, "do_robot_lmatch:sock_write_error");

	return 0;
}


int do_robot_match_player_data(robot_t &robot)
{
	int ret;
	const int sock = robot.sock;
	int match_id;

	ERROR_RETURN(robot.match_id > 0, "do_robot_match_player_data: match_id %ld"
	, robot.match_id);
	ERROR_RETURN(robot.match_count <= 0, "do_robot_match_player_data: match_count %d"
	, robot.match_count);
	if (robot.match_cur_pos >= robot.match_count) {
		ERROR_RETURN(-3, "do_robot_match_player_data:match_cur_pos_out_bound %d/%d"
		, robot.match_cur_pos, robot.match_count);
	}

	match_id = robot.match_id_list[robot.match_cur_pos];
	ret = sock_write(sock, "player_data %d\n", match_id);
	return ret;
}


/**
 *
 * match_apply
 * it will be actived when match_id has got
 *
 */
int do_robot_match_apply(robot_t &robot)
{

	int ret;
	const int sock = robot.sock;

	ERROR_RETURN(robot.match_id > 0, "do_robot_match_apply: match_id %ld"
	, robot.match_id);

	if (robot.match_ready_pos >= robot.match_count) {
		ERROR_RETURN(-2, "do_robot_match_apply: apply_flag_out_bound %d/%d"
		, robot.match_ready_pos, robot.match_count);
	}
	ret = sock_write(sock, "match_apply %ld\n"
	, robot.match_id_list[robot.match_ready_pos]);
	ERROR_NEG_RETURN(ret, "do_robot_match_apply: sock_write_error");

	return 0;
}


/**
 *
 * request ginfo
 * if will be actived when game has started, but has not data in local
 *
 */
int do_robot_ginfo(robot_t &robot)
{

	int ret;
	const int sock = robot.sock;
	ret = sock_write(sock, "ginfo\n");
	ERROR_NEG_RETURN(ret, "db_robot_ginfo: sock_write_error");

	return 0;
}


/**
 *
 * do one play in game
 * 
 */
int do_robot_ai_game(robot_t &robot)
{

	int ret = 0;
	const int sock = robot.sock;
	char cmd[101];

	if (robot.winner != 0) {
		WARN_PRINT(1, "do_robot_ai_game: there has winner %d", robot.winner);
		return 0;
	}

	BUG_PRINT(robot.current_side == 0, "robot_cur_side=%d robot.size=%d"
	, robot.current_side, robot.side);

	if (robot.current_side == robot.side) {

		cmd[0] = 0;
		ret = mylua_get_ai_cmd(robot.lua, cmd);

		sock_write(sock, "%s\n", cmd);
	}

	return ret;
}

int do_robot_force_next(robot_t &robot)
{
	const int sock = robot.sock;
	time_t now_time;

	if (robot.winner != 0) {
		WARN_PRINT(1, "do_robot_force_next: there has winner %d", robot.winner);
		return 0;
	}

	now_time = time(NULL);
	// last_force_time in current_side = self_side
	if (robot.last_force_time != 0
	&& (now_time - robot.last_force_time < TIMEOUT_FORCE_NEXT)) {
		return 0;
	}

	BUG_PRINT(robot.current_side == 0, "do_robot_force_next: cur_side=%d side=%d"
	, robot.current_side, robot.side);

	if (robot.current_side != robot.side) {
		WARN_PRINT(1, "do_robot_force_next: send_force_next");
		sock_write(sock, "f\n");
	}

	return 0;

}

int do_robot_lquick(robot_t &robot)
{
	const int sock = robot.sock;
	robot.quick_total = -1;
	robot.quick_now = 0;
	robot.quick_start_time = 0;
	return sock_write(sock, "@lquick\n");
}

int do_robot_quick(robot_t &robot)
{
	const int sock = robot.sock;
	return sock_write(sock, "quick\n");
}


/**
 *
 * execute command from server
 * 
 */
int do_game_cmd_list(robot_t &robot)
{
	int ret = 0;

	pthread_mutex_lock(&robot.mutex);

	vector<string>::iterator iter;
	for (iter = robot.cmd_list.begin(); iter != robot.cmd_list.end(); iter++) {
		ret = process_robot(robot, (*iter).c_str());

		ERROR_CLEANUP(ret, "do_game_cmd_list: process_robot_fail");
	}
	robot.cmd_list.clear();

//	for (int i = robot.cmd_start_pos; i < robot.cmd_end_pos; i++) {
//		const char *cmd = robot.cmd_list[i];
//		// dispatch cmd and run
//		ret = process_robot(robot, cmd);
//		robot.cmd_start_pos++;
//
//		ERROR_CLEANUP(ret, "do_game_cmd_list: process_robot_fail");
//	}
//	robot.cmd_start_pos = 0;
//	robot.cmd_end_pos = 0;

cleanup:
	pthread_mutex_unlock(&robot.mutex);
	return ret;
}



int do_quick_event(robot_t &robot)
{
	int ret = 0;
	int zero = 0;

	switch (robot.st) {
		case ST_NULL:
		{
			ret = do_robot_login(robot);
			ERROR_RETURN(ret, "do_quick_event: login_error");
			break;
		}
		case ST_LOGIN:
		{
			if (robot.winner != 0) {
				robot.quick_total = -1;
				int winner = robot.winner;
				robot.winner = 0;
				ERROR_RETURN(1, "sock=%d name=[%s] winner=%d"
				, robot.sock, robot.euser.username, winner);
			}
			// list quick game
			if (robot.quick_total < 0) {
				ret = do_robot_lquick(robot);
				ERROR_NEG_RETURN(ret, "do_quick_event: lquick_error");
				break;
			}
			if ((robot.quick_total == 1)
			&& (robot.quick_start_time > 0)
			&& (robot.quick_now - robot.quick_start_time >= QUICK_WAIT)) {
				ret = do_robot_quick(robot);
				ERROR_NEG_RETURN(ret, "do_quick_event: quick_error");
				break;
			} else {
				robot.quick_total = -1;
			}

//			ret = wait_cmd_list(robot, ROBOT_SLEEP);
//			INFO_PRINT(ret, "do_quick_event: normal_wait_timeout");
//			return 0;
			break;
		}
		case ST_ROOM:
		{
			BUG_PRINT(1, "do_quick_event: robot_st = ST_ROOM");
			break;
		}
		case ST_GAME:
		{
			if (robot.side == zero) {
				// robot has enter game, but has not get data
				ret = do_robot_ginfo(robot);
				ERROR_RETURN(ret, "do_quick_event: ginfo_error");
				break;
			}
			// send ai_cmd to server
			ret = do_robot_ai_game(robot);
			ERROR_RETURN(ret, "do_quick_event: ai_game_error");
			ret = wait_cmd_list(robot, ROBOT_SLEEP);
			WARN_PRINT(ret, "do_quick_event: game_wait_timeout name=[%s]"
			, robot.euser.username);
			ret = do_robot_force_next(robot);
			break;
		}
		default:
		{
			ERROR_PRINT(1, "do_quick_event: ST_UNKOWN st=%d", robot.st);
			break;
		}
	}

	ret = wait_cmd_list(robot, ROBOT_SLEEP);
	INFO_PRINT(ret, "do_quick_event: wait_timeout total=%d st=%d name=[%s]"
	, robot.quick_total, robot.st, robot.euser.username);
	return 0;
}


//int quick_game(robot_t *robot_list, int count)
void * quick_game(void *robot_ptr)
{
	FATAL_EXIT(robot_ptr == NULL, "quick_game: robot_null");
	robot_t &robot = *(robot_t*) robot_ptr;
//	evil_user_t &euser = robot.euser;
	const int sock = robot.sock;
	int is_running;
	int ret;

	if (robot.sock == 0) {
		ERROR_CLEANUP(robot.sock!=0, "quick_game:robot.sock!=0 %d"
		, robot.sock);
	}

	is_running = 1;
	robot.quick_total = -1;
	robot.quick_now = 0;
	robot.quick_start_time = 0;
	while (is_running != 0) {
		// batch command from server
		ret = do_game_cmd_list(robot);
		ERROR_CLEANUP(ret, "quick_game_loop:do_game_cmd_list_error");

		// auto request action
		ret = do_quick_event(robot);
		ERROR_CLEANUP(ret, "quick_game_loop:do_quick_event");
	}

cleanup:
	sock_write(sock, "q\n");
	shutdown_robot(robot);
	return robot_ptr;
}


int quick_robot(robot_t &robot, const char *ip, const int port
, const char* username, const char* password) {
	int ret;

	BUG_PRINT(robot.sock!=0, "quick_robot:robot.sock!=0 %d", robot.sock);

	bzero(&robot, sizeof(robot));

	robot.sock = init_socket(ip, port);
	ret = robot.sock;
	ERROR_NEG_CLEANUP(ret, "quick_robot:init_socket");

	// last param is the pointer to robot_t, which is also the param
	// pass to robot_read_loop
	ret = pthread_create(&robot.read_thread, NULL, robot_net_loop, &robot);
	FATAL_EXIT(ret, "quick_robot:pthread_create:read_thread");

	ret = pthread_mutex_init(&robot.mutex, NULL);
	FATAL_EXIT(ret, "mutex_init");

	ret = pthread_cond_init(&robot.cond, NULL);
	FATAL_EXIT(ret, "cond_init");

	// before pass to thread, we need to setup username, password
	strcpy(robot.euser.username, username);
	strcpy(robot.euser.password, password);

	ret = pthread_create(&robot.robot_thread, NULL, quick_game, &robot);
	FATAL_EXIT(ret, "quick_robot:pthread_create:robot_loop");

	ERROR_NEG_CLEANUP(ret, "quick_robot:init_robot");
	return 0;
cleanup:
	// clean up at the end
	shutdown_robot(robot);
	return ret;
}


int quick_loop(robot_t *robot_list, const int num_robot, const char* ip, const int port) {
	int index = 0;

	while (1) {
		int free_robot = -1;
		int wait_count = 0;	// number of robot waiting
//		printf("----- robot list -----\n");
		for (int i=0; i<num_robot; i++) {
			robot_t & robot = robot_list[(i+index) % num_robot];
//			print_robot(robot);
			if (robot.sock > 0 && robot.st == 5) {
				wait_count ++;
			}
			if (robot.sock == 0) {
				free_robot = (i + index) % num_robot;
			}
		}

		if (wait_count <= 0 && free_robot >= 0) {
			char username[100];
			char password[100] = "2015";
			sprintf(username, "robot%d", free_robot+1001);
			// sprintf(password, "2015"); // this can be on the outter loop
			quick_robot(robot_list[free_robot], ip, port, username, password);
		}
		index = (index + 1) % num_robot;

		sleep(5);
	}


	return 0;
}






/**
 *
 * request action if condition is suit
 *
 * 1. get all match_id, which the match is not finished, status=ready will put tail
 * 2. loop all match_id get player_data
 *    if started match has robot data, set match_id to robot.match_id, goto step 4
 * 3. apply one match_id which the match status is ready
 *    - if apply success, set robot.match_id = match_id, goto next step
 *    - if apply fail, apply next match_id
 *    if all match_id is fail, goto step 1
 * 4. wait game start, and finish this match
 *
 */
int do_match_event(robot_t &robot)
{
	int ret = 0;
	int zero = 0;
	time_t now_time = time(NULL);

	switch (robot.st) {
		case ST_NULL:
		{
			ret = do_robot_login(robot);
			ERROR_RETURN(ret, "do_match_event: login_error");
			break;
		}
		case ST_LOGIN:
		{
			switch (robot.match_st) {
				case MATCH_ST_NULL:
				{
					// list all match_id which has not finished
					ret = do_robot_lmatch(robot);
					ERROR_RETURN(ret, "do_match_event: lmatch_error");
					break;
				}
				case MATCH_ST_PLAYER_DATA:
				{
					// get player_data from the match which has not finished
					ret = do_robot_match_player_data(robot);
					ERROR_RETURN(ret, "do_match_event: match_player_data_error");
					break;
				}
				case MATCH_ST_APPLY:
				{
					// only apply match which is ready
					ret = do_robot_match_apply(robot);
					ERROR_RETURN(ret, "do_match_event: match_apply_error");
					break;
				}
				case MATCH_ST_WAITING:
				default:
				{
					// assume: robot.match_id > 0
					// wait too long, send lmatch to get match status
					// if match has finished, reset match data
					if (now_time - robot.match_wait_time > ROBOT_CHECK_WAIT_TIME) {
						ret = do_robot_lmatch(robot);
						ERROR_RETURN(ret, "do_match_event: lmatch_error");
					}
					ret = wait_cmd_list(robot, ROBOT_SLEEP);
					// INFO_PRINT(ret, "do_match_event: normal_wait_timeout");
					return 0;
				}
			}
			break;
		}
		case ST_ROOM:
		{
			BUG_PRINT(1, "do_match_event: robot_st = ST_ROOM");
			break;
		}
		case ST_GAME:
		{
			if (robot.side == zero) {
				if (now_time - robot.match_wait_time < WAIT_GINFO_TIME) {
					sleep(WAIT_GINFO_TIME);
					return 0;
				}
				// robot has enter game, but has not get data
				ret = do_robot_ginfo(robot);
				ERROR_RETURN(ret, "do_match_event: ginfo_error");
				break;
			}
			// send ai_cmd to server
			ret = do_robot_ai_game(robot);
			ERROR_RETURN(ret, "do_match_event: ai_game_error");

			ret = wait_cmd_list(robot, ROBOT_SLEEP);
			WARN_PRINT(ret, "do_match_event: game_wait_cmd_timeout");

			ret = do_robot_force_next(robot);
			break;
		}
		default:
		{
			ERROR_PRINT(1, "do_match_event: ST_UNKOWN st=%d", robot.st);
			break;
		}
	}

	ret = wait_cmd_list(robot, ROBOT_SLEEP);
	ERROR_RETURN(ret, "do_match_event: wait_cmd_timeout");

	return 0;
}

/**
 *
 * match core logic
 *
 * for every robot, execute command from server and check and do request action
 *
 */
// int match_game(robot_t *robot)
void * match_game(void *robot_ptr)
{
	FATAL_EXIT(robot_ptr == NULL, "quick_game: robot_null");
	robot_t &robot = *(robot_t*) robot_ptr;
	const int sock = robot.sock;
	int ret;
	int is_running;
	is_running = 1;

	while (is_running != 0) {

		ERROR_CLEANUP(robot.sock==0, "match_game_loop:robot.sock!=0 %d"
		, robot.sock);

		// batch command from server
		ret = do_game_cmd_list(robot);
		ERROR_CLEANUP(ret, "match_game_loop:do_game_cmd_list_error");

		// auto request action
		ret = do_match_event(robot);
		ERROR_CLEANUP(ret, "match_game_loop:do_match_event");
	}

cleanup:
	sock_write(sock, "q\n");
	shutdown_robot(robot);
	return robot_ptr;
}




int match_loop(robot_t* robot_list, const int num_robot, const char* ip, const int port) {
	int ret;

	for (int i=0; i<num_robot; i++) {
		char username[100];
		char password[100] = "2015";
		sprintf(username, "robot%d", i+1);
		// sprintf(password, "2015"); // this can be on the outter loop
		robot_t & robot = robot_list[i];
		bzero(&robot, sizeof(robot_t));

		robot.sock = init_socket(ip, port);
		ERROR_NEG_RETURN(robot.sock, "init_robot:init_socket");

		// before pass to thread, we need to setup username, password
		strcpy(robot.euser.username, username);
		strcpy(robot.euser.password, password);

		ret = pthread_create(&robot.read_thread, NULL, robot_net_loop, &robot);
		FATAL_EXIT(ret, "main:pthread_create:read_thread");

		ret = pthread_mutex_init(&robot.mutex, NULL);
		FATAL_EXIT(ret, "main:mutex_init");

		ret = pthread_cond_init(&robot.cond, NULL);
		FATAL_EXIT(ret, "main:cond_init");

		ret = pthread_create(&robot.robot_thread, NULL, match_game, &robot);
		FATAL_EXIT(ret, "main:pthread_create:read_thread");
	}

//cleanup:
	// clean up at the end
	for (int i = 0; i < num_robot; i++) {
		robot_t & robot = robot_list[i];
		ret = pthread_join(robot.robot_thread, NULL);
//		shutdown_robot(robot);
	}

	return 0;
}


int null_loop(robot_t* robot_list, const int num_count, const char* ip, const int port) {
	ERROR_RETURN(-5, "null_loop: error_loop_style");
}


typedef int (*robot_loop_t)(robot_t*, const int, const char*, const int);

robot_loop_t robot_loop_list[] = {
	null_loop
,	quick_loop
,	match_loop
};

const int ROBOT_LOOP_LIST_SIZE = sizeof(robot_loop_list) / sizeof(robot_loop_t);

// typedef void* (loop_func_t)(void *);

// usage: 
// robot   (no param)  go to local server 127.0.0.1
// robot [num]  :  testcase num  (no test 0)
// robot [ip]   :  auto-detect
int main(int argc, char *argv[])
{
	int ret;
	 char ip[100] = "127.0.0.1";
	// char ip[100] = "192.168.1.33";
	const int port = 7710;  // port is hard coded
	int style = 1; // default
	const int num_robot = 100; 

	// argv[1] is style, default = 1
	// argv[2] is num_robot, default = 10
	// TODO [3] is IP
	if (argc > 1) {
		style = atoi(argv[1]);
//		FATAL_EXIT(style<=0, "style<=0 : %d", style);
		if (style < 0 || style >= ROBOT_LOOP_LIST_SIZE) {
			FATAL_EXIT(-5, "main_robot_loop:unknown_loop_style %d", style);
		}
	}
//	if (argc > 2) {
//		num_robot = atoi(argv[2]);
//		FATAL_EXIT(num_robot<=0, "num_robot<=0 : %d", num_robot);
//	}
	INFO_PRINT(1, "style %d, robot_count=%d", style, num_robot);

//	signal(SIGTERM, sig_handler);  // kill command
//	signal(SIGINT, sig_handler);  // ctl-C 
	signal(SIGCHLD, sig_handler);
	signal(SIGPIPE, sig_handler); // SIG_IGN  // broken pipe = network error
	signal(SIGHUP, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGUSR2, sig_handler);
	signal(SIGALRM, sig_handler);
	signal(SIGSEGV, sig_handler); // momory overflow

	// cycle:  robot1, robot2 ... robot10 


	// every 5 seconds, check:
	// wait_count = number of robot in login status (st==5) and sock > 0
	// if (wait_count >= 1) good, we do not need new robot
	// 
	// if (wait_count <= 0) : create a new robot
	// check if any robot with sock == 0, use main_robot to start it
	// if all sock > 0 : sorry it is full capacity!
	// stop thinking about killing a prolonged robot, useless, create more
	// robot can solve the short term problem (when we need a lot of robot
	// we already solve the PVP problem!)

	// single mode:  (
//	robot_t robot;
//	main_robot(robot, ip, port, "robot1", "2015");
//	sleep(999999);

	ret = pthread_mutex_init(&mylua_mutex, NULL);
	FATAL_EXIT(ret, "main:mylua_mutex_init");
	 
	robot_t robot_list[num_robot];
	bzero(robot_list, sizeof(robot_list)); // safety

	INFO_PRINT(1, "robot_list_size=%lu", sizeof(robot_list));


	robot_loop_t loop_func = robot_loop_list[style];
	ret = loop_func(robot_list, num_robot, ip, port);

	return 0;
}

