/**
 * nio.c
 * last update: 2013-12-26
 * start: 2013-08-28
 * by kelton
 *
 * require: 
 * fdwatch.c		: update limit from 1088 to what you want!
 * fdwatch.h
 * 
 * compile in Mac: @see Makefile
 * g++ -DHAVE_SYS_EVENT_H=1 -DHAVE_KQUEUE=1 -o nio nio.c fdwatch.c
 * with mysql: -I /usr/local/include/mysql libmysqlclient.a
 * with lua: -pagezero_size 10000 libluajit.a -ldl -lpthread  
 * (flags -pagezero_size in front, -lxxx must be the last)
 */

//////////  header_start ////////////
#if __APPLE__
// @see http://www.opensource.apple.com/source/mDNSResponder/mDNSResponder-258.18/mDNSPosix/PosixDaemon.c
#define daemon avoid_warning_thankyou
#endif

extern "C" {

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <wchar.h>
#include <locale.h>
#include <xlocale.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>  // for time_t
#include <sys/socket.h>	// for socket in general
#include <sys/un.h> // for unix domain socket
#include <sys/types.h>
// #include <sys/uio.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

// lua header  (implicit include lua.h  luaconf.h)
#include "lualib.h"
#include "lauxlib.h"

// custom header after standard header (system header)
// need fdwatch.c  (.o)
#include "base/fdwatch.h"

// lua related header @see clua.c
// #include "lua.h"
// #include "lauxlib.h"

// evil related
#include "fatal.h"

#ifdef HCARD
#include "sha1.h"
#include "base64.h"
#endif

#if __APPLE__
#undef daemon
extern int daemon(int, int);
#endif

}  // end extern "C" 

// C++ header
#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <deque>
#include <map>

using namespace std;

#include "mysql.h"  // for dbproxy, may remove later!
#include "evil.h"	// this one may include C++ features


//////////  header_end ////////////


//////////  DEFINE START /////////
#ifndef INT32_MAX
#define INT32_MAX		(2147483647)
#endif
#define UNIX_DOMAIN "/tmp/evil.sock" // TODO "/tmp/evil_sock%d"  %d=db_id
#define DBIO_UNIX_DOMAIN	"/tmp/evil_dbio.sock"
// @ref: DB_BUFFER_MAX  in dbio.cpp (which is a lot larger!)
// #define DB_BUFFER_SIZE	1024

#define SERVER_VER	"$Rev: 3630 $"

// bind 0.0.0.0 = *  which is all addresses in local machine
#define SERVER_IP	("0.0.0.0")
// #define SERVER_IP	("127.0.0.1")
// #define SERVER_IP	("192.168.1.20")
#define SERVER_PORT	(0x1E1E) 	// 7710
#define MAX_LEN_IP	(100)

// REMOVE 
#define BUFFER_SIZE		(DB_BUFFER_MAX+1000)// peter: grep buffer_overflow
#define SMALL_BUFFER_SIZE 	500
#define READ_BUFFER_SIZE 	2000		// at least 500
#define BUFFER_100	((EVIL_ALIAS_MAX + 10) * 10)
#define VISITOR_ALIAS 	"_visitor_"
#define ROOM_PASSWORD_MAX	10	// @see cmd_room()

// TODO move to constant
#define CREATE_GUILD_GOLD		100
#define CREATE_GUILD_CRYSTAL	0


////////// @see connect_t
#define STATE_FREE		0		// must be 0 (bzero the stub)
#define STATE_READING	1
#define STATE_SENDING	2
#define STATE_PAUSING	3
#define STATE_LINGERING	4
#define STATE_DBIO		10	// this is for dbio thread
#define STATE_LISTEN	20


//////// connect_t.websocket_flag
#define CONN_NONE   0
#define CONN_NORMAL 1
#define CONN_WEBSOCKET  2

// peter: consider -1=fail,  0=no_more, 1=OK,  ACCEPT_xxx
#define ACCEPT_FAIL		-1
// #define ACCEPT_OK		1
#define	ACCEPT_NO_MORE	-2	// would block


// level @see stub.level
#define ST_NULL		0
#define ST_LOGIN	5
#define ST_ROOM		10
#define ST_GAME		15
#define ST_ADMIN	0	// TODO use 99 for production!
#define ST_DB		0	// TODO like admin for testing now
#define LEVEL_CLOSE		88
#define LEVEL_QUITALL	999		// quit the whole server

#define WIN_GAME_NORMAL	0
#define WIN_GAME_FOLD	1

#define GAME_END_NORMAL			0
#define GAME_END_MATCH_CLOSE	1


#define FOLD_HERO_HP	10
#define TIME_FOLD_CHALLENGE		1200 // second
#define TIME_FOLD_QUICK			300 // second
#define MAX_FORCE_NEXT	3
#define OFFLINE_FOLD_TIME  120

/////////////////////////////////

#define BUY_TYPE_CARD		0
#define BUY_TYPE_PIECE		1
#define BUY_TYPE_GOLD		0
#define BUY_TYPE_CRYSTAL	1

// @see fdwatch()  milliseconds
#define WATCH_TIMEOUT	1000	// 1000=1secs,  suggest:1s to 3s

#define ERR_UNKNOWN		-1
#define ERR_OUTBOUND	-2
#define ERR_NULL		-3
#define ERR_LOOP		-4
#define ERR_IO			-5
#define ERR_LOGIC		-6
#define ERR_IMPOSSIBLE	-7
#define ERR_SUBFUN		-8
#define ERR_ORACLE		-8
#define ERR_DENY		-9
#define ERR_NOTYET		-10	// not yet implemented

// sub-error code : do not change!!!
#define	ERR_IO_DB		-55	// put it here for ref, use real number instead

long g_malloc_count = 0;  // obsolete, we don't use malloc anymore
long g_free_count = 0;
// XXX must be before malloc re-define, obsolete, we do not use malloc!
#define NEW(t,n) 	do {((t*) malloc( sizeof(t) * (n) )), g_malloc_count++; } while(0)
// #define malloc(ptr)		do {malloc((ptr));  g_malloc_count++; } while(0)
#define free(ptr)		do {free((ptr)); g_free_count++; } while(0)
#define NET_ERROR_RETURN(conn, ret, ...)  do { if (ret != 0) { net_write_space(conn, cmd); net_error(conn, ret, __VA_ARGS__); ERROR_RETURN(ret, __VA_ARGS__); } } while (0)
#define NET_NEG_ERROR_RETURN(conn, ret, ...)  do { if (ret < 0) { net_write_space(conn, cmd); net_error(conn, ret, __VA_ARGS__); ERROR_NEG_RETURN(ret, __VA_ARGS__); } } while (0)
#define NET_ERROR_PRINT(conn, ret, ...)  do { if (ret != 0) { net_write_space(conn, cmd); net_error(conn, ret, __VA_ARGS__); ERROR_PRINT(ret, __VA_ARGS__); } } while (0)




// max user must be larger than max connect, double is good
#define	MAX_USER	1000
#define MAX_CONNECT	1000	// XXX for debug use smaller connect
#define MAX_ROOM	100  // later: can be fixed, e.g. 100
#define MAX_CHANNEL	4	
#define MAX_GUEST	10
#define CHANNEL_DEFAULT	1	// default channel when no parameter
#define CHANNEL_SOLO	2	// default channel for solo game
#define CHANNEL_MATCH	3	// channel for match
// forbid the room create in channel quick
#define CHANNEL_QUICK	0	 // channel for quick match

#define LCHAN_ALL_CHANNEL	9
#define LROOM_ALL_CHANNEL	LCHAN_ALL_CHANNEL
#define LROOM_ALL 			0
#define LROOM_FREE			1
#define LROOM_PLAY			2


#define MAX_DB_THREAD	3	// XXX do not change > 1 : not yet ready

#define DB_TYPE_TEST	0
#define DB_TYPE_LOG		1
#define DB_TYPE_REG		2
#define DB_TYPE_LCARD	3
#define DB_TYPE_LDECK	4
#define DB_TYPE_SDECK	5
#define DB_TYPE_CCCARD	99

////////// DEFINE END /////////

typedef struct {
	// note: state == 999 is a bug!
	int state;	// 0=free,  LEVEL_ROOM=(wait for guest), ST_GAME=started game
	int next_free;	
	int next_used; // -1 initially
	int prev_used; // -1 init
	int channel; 	// @see init_room_list for next_free,next/prev_used,channel
	int rid;  // g_room_list[channel][rid] == rid @see init_room_list
	long gameid; // room creation time @see get_usec
	time_t create_time;
	time_t last_active[MAX_GUEST];  // LATER
	time_t start_time; // game start time, init in game_init()
	int game_type;
	double rating[2]; // hard coded, fill up when game start
	int guest[MAX_GUEST];  // guest[0] is the room master
	char alias[MAX_GUEST][EVIL_ALIAS_MAX+1]; // XXX need add/remove
	int icon[MAX_GUEST];
	int lv[MAX_GUEST];
	int num_guest;  
	// alias1 vs alias2
	char title[EVIL_ALIAS_MAX*2 + 11];  // default to room master alias, can be anything
	char password[ROOM_PASSWORD_MAX+1];   
	int seed;  // seed = 0 means not started (@see state)
	int start_side; // masha edit for start side

	long game_timeout; // force-next: now + g_design->constant.max_timeout

	time_t offline_time[2]; // force-fold
	int force_next[2]; // force next time

	int match_eid[2];	// for match_room, avoid player except target eid
	long match_id;

	// for tower game
	int tower_pos;
	int tower_hp;
	int tower_res;
	int tower_energy;

	// if deck[0][0] == '\0', deck[0] is empty, 
	// else deck[0][0] is either '0' or '1', deck is read
	// EVIL_CARD_MAX + 1 for reserve[0],  EVIL_CARD_MAX+2 for null-term '\0'
	char deck[EVIL_PLAYER_MAX][EVIL_CARD_MAX + 2]; 

#ifdef HCARD
	char world[850];
#endif

	// int solo_hero; // for solo_plus and auto_fight

	// param {
	int game_flag;
	// game_flag =  flag_shuffle_deck1 		* 1000 
	//				+ flag_shuffle_deck2 		* 100 
	//				+ flag_teach 				* 10 
	//				+ flag_solo_type			* 1

	char type_list[101]; // to control solo ai play hand card
	int ai_max_ally; // int solo_max_ally;
	int hp1; //  int solo_myhero_hp;
	int hp2; //  int solo_max_hp
	int energy1; // int solo_myhero_energy;
	int energy2;
	// }

	int solo_start_side;

	int chapter_id;
	int stage_id;
	int solo_id; // to get solo target

	int auto_battle; // now use for arena

	// TODO use EVIL_CMD_MAX
	vector<string> cmd_list;
	// re-conn will broadcast all cmd_list

	lua_State * lua;	  // usually L, =NULL when game not yet started
} room_t;


// mainly for guild chat
typedef struct {
	int gid;  // gid = 0 means no use, like state free
	int next_free; 
	int next_used;
	int prev_used;
	// content
	int num_member;  // bzero it!
	int member[MAX_GUILD_MEMBER];
	int glevel;
	int gold;
} guild_t;


typedef struct {
	int state;  // @see STATE_FREE
	int next_free;  // index to the next free slot (link-list)
	int conn_fd;  // shall we put this as the first variable in struct?
	int fd_errno; // use in fdwatch_check_fd()=0 for client connect, to disconnect the error fd
#ifdef HCARD
    int websocket_flag;  // zero
#endif
	struct sockaddr_in address;

	int read_offset;
	char read_buffer[READ_BUFFER_SIZE+1]; // smaller, assume cmd after db is short

	char buffer[BUFFER_SIZE+1];  // TODO this is too large!
	int offset;   // write offset
	int outlen;   // buffer[offset] to buffer[outlen-1] is the pending write
	// every ret = write(...),  offset+=ret (positive case)
	// when offset=outlen, all are written, set offset=outlen=0
	// to append new write:  buffer[outlen .. outlen+newlen] = new data
	// if outlen + newlen > BUFFER_SIZE then overflow!

	// stub state, 0=not_login,  5=login'ed,  10=room_created 15=game
	int st;

	// game specific: TODO move it to game context
	evil_user_t	euser;	// TODO *euser = NULL -> (g_user_list + N)
	room_t * room;  // null or pointer to g_room_list[x]

	// comment: variable guild has no used in anywhere
//	guild_t * guild ; // change during login or cguild, gapply, gquit
	int wchat_ts;  // init: wchat_ts = wchat_tail_ts()
	long db_flag;  // use time_t (which is long)

} connect_t;

// masha temp
// vector< pair<int, string> > g_db_request;
// int g_server_fd = -1; //make it global just for test

////////// HERO CONSTANT START ////////// 
#define STD_DECK_MAX	40

// TODO put all init standard deck in DB
// @see logic.lua : deck_boris
const int deck_boris[STD_DECK_MAX] = {   // for id = 1 or 2 
        // 5 id a row
    22, 22, 23, 23, 26, // dirk x 2, sandra x 2, puwen x 3
    26, 26, 27, 27, 28, // birgitte x 2, kurt x 1, 
    30, 30, 30, 61, 61, // blake x 3, valiant x 2, 
    63, 63, 64, 64, 65, // warbanner x 2, smashing x 2, enrage x 1, train x 1
    66, 67, 67, 69, 69, // train x 1, crippling x 2, shieldbash x 2, 
    70, 70, 131, 132, 133, // blood x 2, c_o_night, retreat, armor, 
    134, 135, 151, 151, 154, // special, campfire, rain x 2, extrasharp x 2
    154, 155, 155, 188, 188, // bazaar x 2, rustylongsword x 2
};


const int deck_nishaven[STD_DECK_MAX] = { // id = 5 or 6
         // 5 id a row
    22, 22, 23, 23, 26, // dirk x 2, sandra x 2, puwen x 3
    26, 26, 27, 27, 28, // birgitte x 2, kurt x 1, 
    30, 30, 30, 71, 71, // blake x 3, fireball(71) x 2
	72, 72, 73, 74, 74, // freeze(72)x2, poison(73), lightning(74)x2
	75, 76, 76, 79, 79, // flame(75), tome(76)x2, arcane(79)x2
    80, 80, 131, 132, 133, // clinging webs(80)x2, c_o_night, retreat, armor
    134, 135, 151, 151, 154, // special, campfire, rain(151)x2, extra(154)x2
    154, 155, 155, 172, 172, //bazaar(155)x2, dome of energy(172)x2
};

const int deck_zhanna[STD_DECK_MAX] = { // id = 8
         // 5 id a row
    22, 22, 23, 23, 26, // dirk x 2, sandra x 2, puwen x 3
    26, 26, 27, 27, 28, // birgitte x 2, kurt x 1, 
    30, 30, 30, 91, 91, // blake x 3, healingtouch(91) x 2
    92, 92, 94, 94, 95, // innerstr(92)x2, icestorm(94)x2, focus prayer x 2
    95, 96, 97, 97, 99, // resurrection(96), holyshield(97)x2, smite(99)x2
    99, 100, 131, 132, 133, // book of curses(100), c_o_night, retreat, armor
    134, 135, 151, 151, 154, // special, campfire, rain(151)x2, extra(154)x2
    154, 155, 155, 173, 173, //bazaar(155)x2, plate armor(173)x2
};

const int deck_teradun[STD_DECK_MAX] = {  // id = 11, shadow warrior
		// 5 id a row
	41, 41, 42, 42, 43, // deathbone x 2,  keldor x 2, gargoyle(43) x2
	43, 44, 44, 45, 47, // brutalis(44)x2, plasma(45), firesnake(47)x2
	47, 48, 48, 61, 61, // belladonna(48)x2, valiant def(61)x2
    63, 63, 64, 64, 65, // warbanner x 2, smashing x 2, enrage x 1, train x 1
    66, 67, 67, 69, 69, // train x 1, crippling x 2, shieldbash x 2, 
    70, 70, 142, 143, 143, // bloodfrenzy(70)x2, monsters(142), shadowspawn(143) x2
	144, 144, 151, 151, 154, // shriek(144)x2, rain(151)x2, extra(154)x2
    154, 155, 155, 188, 188, // bazaar x 2, rustylongsword x 2
};

const int deck_majiya[STD_DECK_MAX] = {  // id = 15
		// 5 id a row
	41, 41, 42, 42, 43, // deathbone x 2,  keldor x 2, gargoyle(43) x2
	43, 44, 44, 45, 47, // brutalis(44)x2, plasma(45), firesnake(47)x2
	47, 48, 48, 71, 71, // belladonna(48)x2, fireball(71)x2
	72, 72, 73, 74, 74, // freezing(72)x2, poison(73), lightning(74)
	75, 76, 76, 79, 79, // flames(75), tome(76)x2, arcane(79)x2
	80, 80, 142, 143, 143, // webs(80)x2, monsters(142), shadowspawn(143) x2
	144, 144, 151, 151, 154, // shriek(144)x2, rain(151)x2, extra(154)x2
	154, 155, 155, 172, 172, //bazaar(155)x2, dome(172)x2
};


// testing ahero : 19, 20
const int deck_ahero[STD_DECK_MAX] = {
	25, 25, 25, 25, 71	// 25=kristoffer(haste), 71=fireball
, 	71, 99, 154, 0, 0 		//	-- 99=smite,  154=extra sharp
, 	0, 0, 0, 0, 0 		//	
, 	0, 0, 0, 0, 0 		//
, 	0, 0, 0, 0, 0 		//
, 	0, 0, 0, 0, 0 		//
, 	0, 0, 0, 0, 0 		//
, 	0, 0, 0, 0, 0 		//
};



/*
// hero_valid[hero_id]==1 means this hero is valid
const int hero_valid[HERO_MAX+1] = { 0,	 // [0] is always invalid
	1,	1,	0, 	0,  1,  // 1=boris, 5=nishaven(electric mage)
	1,  0,  1,  0,  0, 	// 6=icemage, 8=zhanna(+3priest)
	1, 	0,  0,  0,  1, 	// 11=ter,  15=majiya(fire mage)  shadow
	0, 	0,  0,  1,  1,  // 19,20:  one-shot-kill hero for testing
};
*/


// NOTE: if hero_valid[hero_id]==1, then standard_deck[hero_id]!=NULL
// pls make sure this condition!!!!
// @see STD_DECK_MAX  the deck is zero-based array
const int *standard_deck[HERO_MAX+1] = { NULL,  // [0] is always invalid
	// 1=boris, 5=nishaven(electric mage)
	deck_boris,	deck_boris,	NULL, 	NULL,  deck_nishaven,   // 1-5
	// 6=icemage, 8=zhanna(+3priest)
	deck_nishaven,  NULL,  deck_zhanna,  NULL,  NULL, 	// 6-10
	deck_teradun, 	NULL,  NULL,  NULL,  deck_majiya, 			// 11-15
	NULL, 	NULL,  NULL,  deck_ahero,  deck_ahero, 			// 16-20
};

////////// HERO CONSTANT END //////////



////////// GLOBAL START //////////
int LOGIC_VER = 0;

// euser.eid -> connect_t.id
map<int, int> g_user_index;

// user eid -> room_t *
map<int, room_t* > g_user_room; 

int g_num_connect = 0;
int g_free_connect = 0;  // @see handle_newconnect
connect_t	g_connect_list[MAX_CONNECT];
// g_connect_list = pointer to first element = &(g_connect_list[0])

int g_free_room[MAX_CHANNEL]; 	// must bzero initially
int g_used_room[MAX_CHANNEL];  // last created room will be first
room_t	g_room_list[MAX_CHANNEL][MAX_ROOM + 1];  	// this is worst case?

// hash map
map<int, guild_t>	g_guild_map;

const char * g_channel_list[MAX_CHANNEL] = {
	"决斗之城" // quick
, 	"自定义对战" // free
, 	"战役" // solo
, 	"比赛频道" // match
};

match_t g_match_list[MAX_MATCH];
// match_player_t g_match_player_list[MAX_MATCH][MAX_MATCH_PLAYER * MAX_TEAM_ROUND];

design_t *g_design = NULL;
// locale_t g_locale;
/*
// db_design :
shop_t g_shop_list[EVIL_CARD_MAX+1]; //base 1(0 is useless)
std_deck_t g_std_deck_list[HERO_MAX+1]; //base 1(0 is useless)
ai_t g_ai_list[MAX_AI_EID+1]; //base 1(0 is useless)
pick_t g_pick_list[MAX_PICK]; 
card_t g_card_list[EVIL_CARD_MAX+1]; // base 1(0 is useless)
notice_t g_notice_list[MAX_NOTICE+1]; // base 1(0 is useless)
int g_notice_count;
vector<card_t> g_star_list[MAX_STAR];
vector<card_t> g_extract_list[MAX_STAR];
constant_t g_constant;
int g_max_level;
vector<int> g_exp_list;
int g_guild_max_level;
design_guild_t g_design_guild_list[100]; // XXX hard code max guild level = 100
design_merge_t g_design_merge_list[EVIL_CARD_MAX+1];  // base 1(0 is useless)
design_pay_t g_design_pay_list[MAX_PAY_NUM];  // base 1(0 is useless)
design_version_t g_design_version;
design_website_t g_design_website_list[MAX_WEBSITE_NUM]; //base 0, hard code max website = 50
*/

ladder_rating_t g_ladder_rating_list[MAX_LADDER+1];
ladder_level_t g_ladder_level_list[MAX_LADDER+1];
ladder_guild_t g_ladder_guild_list[MAX_LADDER+1];
ladder_collection_t g_ladder_collection_list[MAX_LADDER+1];
ladder_gold_t g_ladder_gold_list[MAX_LADDER+1];
ladder_chapter_t g_ladder_chapter_list[MAX_LADDER+1];

local_name_t g_name_xing;
local_name_t g_name_boy;
local_name_t g_name_girl;



// quick game related
typedef struct quick_struct {
	int eid;
	double rating;
	time_t start_time;
} quick_t;

// peter: we may consider vector?  was deque
#define QUICK_LIST vector<quick_t> 
QUICK_LIST g_quick_list;
// quick_t	g_quick_list[MAX_USER];  // or MAX_USER ?
// int g_quick_total = 0;

QUICK_LIST g_fight_gold_list;
QUICK_LIST g_fight_crystal_list;
QUICK_LIST g_fight_free_list;



#define KEEP_CHALLENGE_TIME		30
#define TYPE_CHALLENGE_SEND		0
#define TYPE_CHALLENGE_CANCEL	1
#define TYPE_CHALLENGE_ACCEPT	2
#define TYPE_CHALLENGE_REFUSE	3
typedef struct challenge_struct {
	int eid_challenger;
	int eid_receiver;
	char alias_challenger[EVIL_ALIAS_MAX + 5];
	char alias_receiver[EVIL_ALIAS_MAX + 5];
	time_t challenge_time;
} challenge_t;
#define CHALLENGE_LIST vector<challenge_t>
CHALLENGE_LIST g_challenge_list;


typedef struct ranking_pair_struct {
	int eid_challenger;
	int eid_receiver;
	time_t challenge_time;
	int status;
} ranking_pair_t;
#define RANKING_PAIR_LIST vector<ranking_pair_t>
RANKING_PAIR_LIST g_rankpair_list;


// dbproxy related (will be obsolete)
int g_main_fd[MAX_DB_THREAD];
pthread_t g_db_thread[MAX_DB_THREAD];

/// dbio 
// int g_dbio_fd[MAX_DB_THREAD];
// FILE * g_dbio_file[MAX_DB_THREAD];
pthread_t g_dbio_thread[MAX_DB_THREAD];
connect_t g_dbio_conn[MAX_DB_THREAD];
// TODO store all dbio related to dbio_init_t  (conn, pthread_t, file, fd)
dbio_init_t g_dbio_data[MAX_DB_THREAD];


////////// GLOBAL END /////////



////////// LUA START //////////
// lu_ = LUA UTILITY 

#define LU_CHECK_MAGIC(L)	do { \
if (NULL!=(L)) { ERROR_PRINT(1974-(int)lua_tointeger(L, -1), "lua_magic_error"); }\
} while (0);

// XXX use this with care! need to pop after use
const char* XXX_DO_NOT_USE_lu_get_string(lua_State *L, const char * var) {
	lua_getglobal(L, var); // read from global with name [var], value in stack
	const char*  sss;
	size_t len = 0;
	sss = lua_tolstring(L, -1, &len); // -1 is the first in stack
	// XXX sss may be remove if we pop it ?
	printf("CLUA : get_str [%s](%s)\n", sss, var);
	return sss;
}

int lu_set_string(lua_State *L, const char * var, const char * str) {
	lua_pushstring(L, str);
	lua_setglobal(L, var);
	// printf("CLUA : set_str %s=[%s]\n", var, str);
	return 0;
}

int lu_get_int(lua_State *L, const char * var) {
	int value;
	lua_getglobal(L, var); // read from global with name [var], value in stack
	value = lua_tonumber(L, -1); // -1 is the first in stack
	lua_pop(L, 1);
	// printf("CLUA : get_int : %d(%s)\n", value, var);
	return value;
}

int lu_set_int(lua_State *L, const char * var, int value) {
	lua_pushinteger(L, value);
	lua_setglobal(L, var);
	// printf("CLUA : set_int : %s=%d\n", var, value);
	return 0;
}

// t=table, i=index, n=name
// return table[index].name  as string
// caller need to allocate enough space for str, e.g. char str[max]
// if return NULL, str is not used
int lu_get_table_index_name(lua_State *L, char *str, int max
, const char *table, int index, const char *name) {
	int ret;
	const char *ptr;
	char fmt[10];
	size_t size = 0;
	lua_getglobal(L, table); 
	ret = lua_type(L, -1);
	if (ret != LUA_TTABLE) {
		// BUG_PRINT(-6, "getglobal: %s type=%d", table, ret);
		lua_pop(L, 1); // remove the get global
		return -6;
	}

	lua_rawgeti(L, -1, index); // rusty sword test
	ret = lua_type(L, -1);
	if (ret != LUA_TTABLE) {
		// this can be warning later
		// BUG_PRINT(-16, "rawgeti: %d type=%d", index, ret);
		lua_pop(L, 2);
		return -16;
	}

	lua_getfield(L, -1, name);
	ret = lua_type(L, -1);
	if (ret != LUA_TSTRING) {
		// BUG_PRINT(-26, "getfield: %s type=%d", name, ret);
		lua_pop(L, 3);
		return -26;
	}
	ptr = lua_tolstring(L, -1, &size);
	if (ptr == NULL) {
		//BUG_PRINT(-36, "tolstring_null: %s[%d].%s", table, index, name);
		lua_pop(L, 3);
		return -36;
	}
	sprintf(fmt, "%%.%ds", max-1);
	sprintf(str, fmt, ptr);
	lua_pop(L, 3); // balance the lua stack
	return 0;
}

int lu_get_table_index_int(lua_State *L, int *pint
, const char *table, int index, const char *name) {
	int ret;
	lua_getglobal(L, table); 
	ret = lua_type(L, -1);
	if (ret != LUA_TTABLE) {
		// BUG_PRINT(-6, "getglobal: %s type=%d", table, ret);
		lua_pop(L, 1); // remove the get global
		return -6;
	}

	lua_rawgeti(L, -1, index); // rusty sword test
	ret = lua_type(L, -1);
	if (ret != LUA_TTABLE) {
		// this can be warning later
		// BUG_PRINT(-16, "rawgeti: %d type=%d", index, ret);
		lua_pop(L, 2);
		return -16;
	}

	lua_getfield(L, -1, name);
	ret = lua_type(L, -1);
	if (ret != LUA_TNUMBER) {
		// BUG_PRINT(-26, "getfield: %s type=%d", name, ret);
		lua_pop(L, 3);
		return -26;
	}
	*pint = lua_tointeger(L, -1);
	lua_pop(L, 3); // balance the lua stack
	return 0;
}

// return the integer form for: table.name
int mylua_get_table_int(lua_State *L, const char *table, const char *name) {
    int ret;
    lua_getglobal(L, table);
    ret = lua_type(L, -1);
    if (ret != LUA_TTABLE) {
        BUG_PRINT(-6, "getglobal: %s type=%d", table, ret);
        lua_pop(L, 1); // remove the get global
        return -999998;  // negative enough for error reporting
    }

    lua_getfield(L, -1, name);
    ret = lua_type(L, -1);
    if (ret != LUA_TNUMBER) {
        BUG_PRINT(-26, "getfield: %s type=%d", name, ret);
        lua_pop(L, 2);
        return -999999;  // negative enough to avoid most common case
    }
    ret = lua_tointeger(L, -1);

    lua_pop(L, 2); // balance the lua stack
    return ret;
}

///// logic specific utility
int lu_print_index_both(lua_State *L) 
{
	int ret;
	lua_getglobal(L, "print_index_both");	
	lua_getglobal(L, "g_logic_table");
	// (lua_state, num_param, num_return, ???)
	ret = lua_pcall(L, 1, 0, 0);  // expect: ret=0
	// peter: do we need to remove 2 elements from stack ?
	if (0 != ret) {
		lua_pop(L, 1); // pop the error handler
		ERROR_PRINT(ret, "lu_print_index_both");
		return -1;
	}
	return 0;
}

int lu_print_status(lua_State *L) 
{
	int ret;
	lua_getglobal(L, "print_status");	
	lua_pushnumber(L, 0); // hard coded g_my_side=0, always=current
	ret = lua_pcall(L, 1, 0, 0);  // expect: ret=0
	if (0 != ret) {
		lua_pop(L, 1); // pop the error handler
		ERROR_PRINT(ret, "lu_print_status");
		return -1;
	}
	return 0;
/** keep this for future:
	int current_side;
	int current_phase;
	current_side = get_int(L, "g_current_side");
	current_phase = get_int(L, "g_phase");
	printf("STATUS:  g_current_side=%d  g_phase=%d  g_my_side(C)=%d\n"
	, current_side, current_phase, g_my_side);
	*/
}

// ref: lua.c  (lua 5.1.5)
static int lu_traceback (lua_State *L) {
	printf("-------- lu_traceback:\n");
	if (!lua_isstring(L, 1)) { /* 'message' not a string? */
		printf("BUGBUG lu_traceback stack 1 not a string\n");
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

int lu_trace_call(lua_State *L, int narg, int result)
{
	int status;
	int base = lua_gettop(L) - narg - 1;
	lua_pushcfunction(L, lu_traceback);
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




// function logic_init_array(deck1_array, deck2_array, seed) 
// deck1, deck2 are 400 character array
int lu_logic_init(lua_State *L, const char *deck1, const char *deck2, int seed, int start_side, int hp1, int hp2, int energy1, int energy2)
{
	lua_getglobal(L, "logic_init_array"); 
	lua_pushstring(L, deck1);
	lua_pushstring(L, deck2);
	lua_pushinteger(L, seed);
	lua_pushinteger(L, start_side);
	lua_pushinteger(L, hp1);
	lua_pushinteger(L, hp2);
	lua_pushinteger(L, energy1);
	lua_pushinteger(L, energy2);
	int ret;
	ret = lua_pcall(L, 8, 1, 0);

	if (ret != 0) {
		// printf("lu_logic_init ret=%d\n", ret);
		lua_pop(L, 1);  // skip the handler
		ERROR_PRINT(-8, "lu_logic_init:pcall ret=%d  seed=%d", ret, seed);
		ERROR_PRINT(-8, "lu_logic_init:pcall deck1=%s deck2=%s", deck1, deck2);
		return -8;
	}

	ret = lua_tointeger(L, -1);
	lua_pop(L, 1);

	return ret;  // always zero now, TODO card_array_list may return err
}

#ifdef HCARD

int lu_get_world(lua_State *L, char *str_world)
{
    int ret;
    lua_getglobal(L, "get_world");
    ret = lua_pcall(L, 0, 1, 0);

    if (ret != 0) {
        lua_pop(L, 1);  // skip the handler
        ERROR_PRINT(-8, "lu_get_world:pcall ret=%d", ret);
        return -8;
    }

    sprintf(str_world, "%.800s", lua_tostring(L, -1));
    DEBUG_PRINT(0, "lu_get_world:str_world=%s", str_world);

    lua_pop(L, 1);
    ret = 0;
    return ret;
}

#endif

// not finish
int lu_get_all_play(lua_State *L, char *play_list)
{
    int ret;
    lua_getglobal(L, "get_all_play");
    ret = lua_pcall(L, 0, 1, 0);

    if (ret != 0) {
        lua_pop(L, 1);  // skip the handler
        ERROR_PRINT(-8, "lu_get_all_play:pcall ret=%d", ret);
        return -8;
    }

    sprintf(play_list, "%.800s", lua_tostring(L, -1));
    DEBUG_PRINT(0, "lu_get_all_play:play_list=%s", play_list);

    lua_pop(L, 1);
    ret = 0;
    return ret;
}


// function gate_init_array(deck1_array, deck2_array, seed) 
// deck1 is 400 character array
// gate_list is card list
int lu_gate_init(lua_State *L, const char *deck1, const char *gate_list, int seed)
{
	INFO_PRINT(0, "lu_gate_init:start");
	lua_getglobal(L, "gate_init_array"); 
	// DEBUG_PRINT(0, "111lu_gate_init");
	lua_pushstring(L, deck1);
	lua_pushstring(L, gate_list);
	lua_pushinteger(L, seed);
	int ret;
	ret = lua_pcall(L, 3, 1, 0);
	// DEBUG_PRINT(0, "lu_gate_init:ret=%d", ret);

	if (ret != 0) {
		// printf("lu_logic_init ret=%d\n", ret);
		lua_pop(L, 1);  // skip the handler
		ERROR_PRINT(-8, "lu_gate_init:pcall ret=%d  seed=%d", ret, seed);
		ERROR_PRINT(-8, "lu_gate_init:pcall deck1=%s", deck1);
		return -8;
	}

	ret = lua_tointeger(L, -1);
	lua_pop(L, 1);
	LU_CHECK_MAGIC(L);

	INFO_PRINT(0, "lu_gate_init:end");
	return ret;  // always zero now, TODO card_array_list may return err
}


// peter: deck1, deck2 are unified form
// form1: deck400
// form2: [hero_id] [c1] [c2] [c3] ...
// no need to pass hero_id 
int lu_solo_plus_init(lua_State *L, const char *deck1, const char *deck2, int game_flag, int ai_max_ally, int max_hp, int myhero_hp, int myhero_energy, const char *type_list, int seed)
{
	lua_getglobal(L, "solo_init_array"); 
	lua_pushstring(L, deck1);
	lua_pushstring(L, deck2);
	lua_pushinteger(L, game_flag);
	lua_pushinteger(L, ai_max_ally);
	lua_pushinteger(L, max_hp);
	lua_pushinteger(L, myhero_hp);
	lua_pushinteger(L, myhero_energy);
	lua_pushstring(L, type_list);
	lua_pushinteger(L, seed);
	int ret;
	ret = lua_pcall(L, 9, 1, 0);
	// DEBUG_PRINT(0, "lu_solo_plus_init:ret=%d", ret);

	if (ret != 0) {
		lua_pop(L, 1);  // skip the handler
		ERROR_PRINT(-8, "lu_solo_plus_init:pcall ret=%d  seed=%d", ret, seed);
		return -8;
	}

	ret = lua_tointeger(L, -1);
	lua_pop(L, 1);

	LU_CHECK_MAGIC(L);

	return ret;  // always zero now, TODO card_array_list may return err
}

// useless now, remove later
/*
int lu_robot_game_init(lua_State *L, const char *deck1, const char *deck2_list, int seed)
{
	lua_getglobal(L, "robot_init_array"); 
	lua_pushstring(L, deck1);
	lua_pushstring(L, deck2_list);
	lua_pushinteger(L, seed);
	int ret;
	ret = lua_pcall(L, 3, 1, 0);
	DEBUG_PRINT(0, "lu_robot_game_init:ret=%d", ret);

	if (ret != 0) {
		lua_pop(L, 1);  // skip the handler
		ERROR_PRINT(-8, "lu_robot_game_init:pcall ret=%d  seed=%d", ret, seed);
		return -8;
	}

	ret = lua_tointeger(L, -1);
	lua_pop(L, 1);

	LU_CHECK_MAGIC(L);

	return ret;  // always zero now, TODO card_array_list may return err
}
*/

int lu_tower_init(lua_State *L, const char *deck1, const char *deck2, int seed, int start_side, int hp, int res, int energy)
{
	lua_getglobal(L, "tower_init_array"); 
	lua_pushstring(L, deck1);
	lua_pushstring(L, deck2);
	lua_pushinteger(L, seed);
	lua_pushinteger(L, start_side);
	lua_pushinteger(L, hp);
	lua_pushinteger(L, res);
	lua_pushinteger(L, energy);
	int ret;
	ret = lua_pcall(L, 7, 1, 0);
	DEBUG_PRINT(0, "lu_tower_init:ret=%d", ret);

	if (ret != 0) {
		// printf("lu_logic_init ret=%d\n", ret);
		lua_pop(L, 1);  // skip the handler
		ERROR_PRINT(-8, "lu_tower_init:pcall ret=%d  seed=%d", ret, seed);
		ERROR_PRINT(-8, "lu_tower_init:pcall deck1=%s", deck1);
		return -8;
	}

	ret = lua_tointeger(L, -1);
	lua_pop(L, 1);

	return ret;  // always zero now, TODO card_array_list may return err
}

/*
// old logic, remove later
int lu_get_chapter_target(lua_State *L, int *hero_hp, int *round, int *chapter_up_ally, int *chapter_up_support, int *chapter_up_ability, int *chapter_down_ally, int *chapter_down_support, int *chapter_down_ability, int *chapter_up_card_id1, int *chapter_up_card_count1, int *chapter_up_card_id2, int *chapter_up_card_count2, int *chapter_up_card_id3, int *chapter_up_card_count3, int *chapter_down_card_id1, int *chapter_down_card_count1, int *chapter_down_card_id2, int *chapter_down_card_count2, int *chapter_down_card_id3, int *chapter_down_card_count3)
{
	int ret;
	lua_getglobal(L, "get_chapter_target"); 
	ret = lua_pcall(L, 0, 20, 0);
	if (0 != ret) {
		lua_pop(L, 1); 
		ERROR_RETURN(-6, "lu_get_chapter_target_error");
	}

	*hero_hp = lua_tointeger(L, -20);
	*round = lua_tointeger(L, -19);
	*chapter_up_ally = lua_tointeger(L, -18);
	*chapter_up_support = lua_tointeger(L, -17);
	*chapter_up_ability = lua_tointeger(L, -16);
	*chapter_down_ally = lua_tointeger(L, -15);
	*chapter_down_support = lua_tointeger(L, -14);
	*chapter_down_ability = lua_tointeger(L, -13);
	*chapter_up_card_id1 = lua_tointeger(L, -12);
	*chapter_up_card_count1 = lua_tointeger(L, -11);
	*chapter_up_card_id2 = lua_tointeger(L, -10);
	*chapter_up_card_count2 = lua_tointeger(L, -9);
	*chapter_up_card_id3 = lua_tointeger(L, -8);
	*chapter_up_card_count3 = lua_tointeger(L, -7);
	*chapter_down_card_id1 = lua_tointeger(L, -6);
	*chapter_down_card_count1 = lua_tointeger(L, -5);
	*chapter_down_card_id2 = lua_tointeger(L, -4);
	*chapter_down_card_count2 = lua_tointeger(L, -3);
	*chapter_down_card_id3 = lua_tointeger(L, -2);
	*chapter_down_card_count3 = lua_tointeger(L, -1);
	lua_pop(L, 20);

	LU_CHECK_MAGIC(L);
	return 0;  
}
*/

/*
// old logic, remove later
int lu_set_chapter_target_card(lua_State *L, int up_id1, int up_id2, int up_id3, int down_id1, int down_id2, int down_id3)
{
	int ret;
	lua_getglobal(L, "set_chapter_target_card"); 
	lua_pushinteger(L, up_id1);
	lua_pushinteger(L, up_id2);
	lua_pushinteger(L, up_id3);
	lua_pushinteger(L, down_id1);
	lua_pushinteger(L, down_id2);
	lua_pushinteger(L, down_id3);
	ret = lua_pcall(L, 6, 0, 0);
	if (0 != ret) {
		ERROR_RETURN(-6, "set_chapter_target_card_error");
	}

	LU_CHECK_MAGIC(L);
	return 0;  
}
*/

int lu_get_solo_target(lua_State *L, int target, int p1, int p2)
{
	int ret;
	lua_getglobal(L, "get_solo_target"); 
	lua_pushinteger(L, target);
	lua_pushinteger(L, p1);
	lua_pushinteger(L, p1);
	ret = lua_pcall(L, 3, 1, 0);
	if (0 != ret) {
		lua_pop(L, 1); 
		ERROR_RETURN(-6, "lu_get_solo_target_error");
	}

	ret = lua_tointeger(L, -1);
	lua_pop(L, 1);

	LU_CHECK_MAGIC(L);
	return ret;  
}

int lu_get_hero_hp(lua_State *L, int side)
{
	int ret;
	int hero_hp;
	lua_getglobal(L, "get_hero_hp");
	lua_pushinteger(L, side); 
	ret = lua_pcall(L, 1, 1, 0); //1=input param, 1=output return
	if (0 != ret) {
		lua_pop(L, 1); 
		ERROR_RETURN(-6, "lu_get_hero_hp_error");
	}
	hero_hp = lua_tointeger(L, -1);
	lua_pop(L, 1);

	// DEBUG_PRINT(0, "lu_get_hero_hp:hero_hp=%d", hero_hp);
	LU_CHECK_MAGIC(L);
	return hero_hp;
}

int lu_get_hero_id(lua_State *L, int winner_side, int *win_hero_id, int *lose_hero_id)
{
	int ret;
	if (winner_side == 9) {
		winner_side = 1;
	}
	lua_getglobal(L, "get_hero_id");
	lua_pushinteger(L, winner_side); 
	ret = lua_pcall(L, 1, 2, 0); //1=input param, 1=output return
	if (0 != ret) {
		lua_pop(L, 2); 
		ERROR_RETURN(-6, "get_hero_id_error");
	}
	*win_hero_id = lua_tointeger(L, -2);
	*lose_hero_id = lua_tointeger(L, -1);
	lua_pop(L, 2);

	LU_CHECK_MAGIC(L);
	return 0;
}

///////// LUA END /////////



////////// FORWARD DECLARATION START /////////
void do_clean_disconnect( connect_t* conn );
void do_clean( connect_t* conn );
void do_disconnect( connect_t * conn );
int quick_check(int eid);  // this can be resolved
int ai_play(connect_t *conn);
int do_batch_command(connect_t *conn, char *buffer);

int dbin_win_param(connect_t *conn, room_t *proom, int winner, double rating
, int eid1, int eid2, int gold1, int crystal1, int gold2, int crystal2
, int exp1, int lv_offset1, int exp2, int lv_offset2
, int lv1, int lv2, int card_id1, int card_id2
, int icon1, int icon2, char *alias1, char *alias2
, int ai_times1, int ai_times2, int gold_times1, int gold_times2
, int crystal_times1, int crystal_times2
, long gameid, int seed, int start_side, int ver, int chapter_star
, const char *deck1, const char *deck2, const char *cmd_list);

int save_mission(connect_t *conn);
int dbin_save_batch(connect_t *conn, int eid, int ptype, int *card_list, int gold, int crystal);
int dbio_init(pthread_t *dbio_thread);
int dbin_write(connect_t *conn, const char *cmd, int dbtype
, const char *fmt, ...);
#ifdef HCARD
int websocket_getkey(char *key, const char * buf);
int websocket_computekey(char *key, const char * client_key);
#endif
////////// FORWARD DECLARATION END /////////



////////// UTILITY START //////////

/**
 * struct sockaddr -> char[] ip and int port
 * @param ip    output parameter for ip, caller do: char ip[16] or >16
 * @param paddr	input param  struct sockaddr *
 */
int address_readable(char *ip, struct sockaddr_in *paddr) {
	int ipint = paddr->sin_addr.s_addr;
	sprintf(ip, "%d.%d.%d.%d:%d", 
		ipint&0xff, (ipint>>8)&0xff, (ipint>>16)&0xff, (ipint>>24)&0xff
		, ntohs( paddr->sin_port )
		);
	return 0;
}

// split the string str into at most 9 token, trim all space tab \r \n
// return: number of valid token
// usage:
// char *token[9];   // 
// char str[100];   strcpy(str, "hello 222 33 44 55 66 77 88 99");
// int total;
// total = split9(token, str);
// if total < 0 BUG
// for (i=0; i<total; i++) { printf("token[%d]=[%s]\n", i, token[i]); }
int split9(char* tok[9], char *str)
{
	static const char sep[] = " \t\r\n";
	// char * ptr = NULL;
	int i;
	char *context;

	tok[0] = strtok_r(str, sep, &context);
	if (tok[0]==NULL) {
		return 0;
	}

	i = 1;
	for (i=1; i<9; i++) {
		tok[i]=strtok_r(NULL, sep, &context);
		if (tok[i]==NULL) {
			return i;
		}
	}
	return i;
}

int split29(char* tok[29], char *str)
{
	static const char sep[] = " \t\r\n";
	// char * ptr = NULL;
	int i;
	char *context;

	tok[0] = strtok_r(str, sep, &context);
	if (tok[0]==NULL) {
		return 0;
	}

	i = 1;
	for (i=1; i<29; i++) {
		tok[i]=strtok_r(NULL, sep, &context);
		if (tok[i]==NULL) {
			return i;
		}
	}
	return i;
}

//  we only need integer range! TODO rename strtoint_safe
// usage: 
// strtol_safe("123abc", -1) => 123
// strtol_safe("abc", -1) => -1
// strtok_safe("56.3", -1) => 56
int strtol_safe(const char *str, int default_value) {
	char *end;
	long value;
	value = strtol(str, &end, 10);
	if (end==str) {
		return default_value;
	}
	// partial conversion is allowed, where end > str
	return (int) value;
}

double str_double_safe(const char *str, double default_value)
{
	char *end;
	double value;
	value = strtod(str, &end);
	if (end==str) {
		return default_value;
	}
	return value;
}

// trim ending line feed / space : \r or \n or ' ' 
// return the new len of the string, trim is wasting cpu time, avoid
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

// was: struct sockaddr_in prepare_addr(const char *addr, int port)
struct sockaddr prepare_addr(const char *addr, int port)
{
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = inet_addr(addr);
  sin.sin_port = htons(port);
  // note: assume sockaddr, sockaddr_in same len
  return *((struct sockaddr *)&sin);  
}

/**
 * check_name stick to the following standard:
 * 1.  len <= max and len >= 1  (TODO min len >= 3)
 * 2.  first character must be >= 'A'
 * 3.  second the rest:   '0' - '9',  'A'-'Z', 'a'-'z'
 *     later: chinese OK
 *     except: '_', '-'
 * @return 0 for OK
 * @return <0 for invalid string
 */
int check_name(const char * str, int max)
{
	int len = strlen(str);
	if (len > max || len < 1) {
		return -2;  // too long
	}

	// temp not check!
	if (1) return 0;

	// first character must be A-Z  or a-z
	if ( ! iswalpha(str[0]) ) {
		// TODO for chinese: iswalpha
		return -6;
	}

	// only accept '_' as the only punctuation
	for (int i=1; i<len; i++) {
		// was isalnum
		if ( ! iswalnum( str[i] ) && str[i]!='_' && str[i]!='-') {
			return -16;
		}
	}
	
	return 0;
}

// array[x] -> array[x] + '0'
// array[0] is not used but must be there!
// e.g. size=400,  array[0] to array[400] totally 401 char
int num_to_digit(char *card_array, int size, int max_digit, int eid) 
{
	int ret = 0;
	for (int i=0; i<size; i++) {
		if (card_array[i] > max_digit) {
			ret = -2;
			ERROR_PRINT(-2, "num_to_digit up_bound array[%d]=%d  eid=%d"
			, i, card_array[i], eid);
			card_array[i] = max_digit;
		}
		if (card_array[i] < 0) {
			ret = -12;
			ERROR_PRINT(-12, "num_to_digit low_bound array[%d]=%d eid=%d"
			, i, card_array[i], eid);
			card_array[i] = 0;
		}
		card_array[i] += '0'; // make it string style number
	}
	return ret;
}

static long g_usec = 0;
long get_usec()
{
	// proom->gameid = get_usec();
	struct timeval tv;
	long usec;
	gettimeofday(&tv, NULL);
	usec = tv.tv_sec * 1000000 + tv.tv_usec;
	if (usec <= g_usec) {
		usec = g_usec + 1;
	}
	g_usec = usec;
	return usec;
}

time_t get_day_start(time_t rtime)
{
	struct tm timestruct;
//	time_t one_day_sec = 60 * 60 *24;
	localtime_r(&rtime, &timestruct);
	timestruct.tm_sec = 0;
	timestruct.tm_min = 0;
	timestruct.tm_hour = 0;
	return mktime(&timestruct); // get 00:00 time
}

static long g_gameid = 0;
long get_gameid()
{
	struct tm tm;
	long gameid;
	time_t now = time(NULL);
	bzero(&tm, sizeof(tm));
	localtime_r(&now, &tm);
	gameid = 0;

	gameid += (tm.tm_year % 100) * 10000000000000L;
	gameid += (tm.tm_mon + 1) * 	 100000000000L;
	gameid += tm.tm_mday * 			   1000000000L;
	gameid += tm.tm_hour *				 10000000L;
	gameid += tm.tm_min *				   100000L;
	gameid += tm.tm_sec * 					 1000L;
	
	if (gameid <= g_gameid) {
		gameid = g_gameid + 1;
	}
	g_gameid = gameid;
	return gameid;
}

// unsigned char num[2] -> int (16 bits)
// return as integer for safety, do not use short
static int char_to_short(unsigned char *num)
{
    return num[0] | (num[1] << 8);
}

// int (16-bits) -> unsigned char num[2]
static void short_to_char(unsigned char *num, int value)
{
    num[0] = value & 0xff;
    num[1] = (value >> 8) & 0xff;
}

// logic utility

int fit_hero(int hero_job, int card_job)
{
	if (card_job==0) {
		return 1; // neutral card
	}
	if ((hero_job & card_job) > 0) {
		return 1;
	}

	// implicit: hero_job & card_job == 0
	return 0;
}


// check the array of deck, which is 400 long, and have ascii '0' to '4'
// there is only 1 hero ([0] to [19] has only one '1')
// check each card in deck, num should not over the corresponding card
// @return 0 for good deck
int check_deck(char *deck, const char *card, int eid, int game_type = 0);
int check_deck(char *deck, const char *card, int eid, int game_type)
{
	int hero = -1;
	int hero_job;
	int total;

	// do not check len, because it may have trailing \r \n
	/**
	len = strlen(card);
	if (len != EVIL_CARD_MAX) {
		ERROR_RETURN(-2, "check_deck:len=%d eid=%d", len, eid);
	}
	**/

	total = 0;
	// check hero
	for (int i=0; i<HERO_MAX; i++) {
		if (deck[i] > '1') {
			ERROR_RETURN(-12, "check_deck:hero[%d]>1 %c eid=%d"
			, i+1, deck[i], eid);
		}
		if (deck[i] > '0') {
			if (hero >= 0) {
				ERROR_RETURN(-22, "check_deck:dup_hero %d %d eid=%d"
				, hero+1, i+1, eid);
			} else {
				hero = i+1;
			}
		}
		if (deck[i] > card[i]) {
			ERROR_RETURN(-62, "check_deck:hero_not_in_card %d eid=%d"
			, i+1, eid);
		}

	}

	if (hero < 1 || hero > HERO_MAX) {
		ERROR_RETURN(-6, "check_deck:no_hero");
	}

	hero_job = g_design->card_list[hero].job;
	// printf("check_deck: hero=%d   job=%d\n", hero, hero_job);
	total++;

	for (int i=HERO_MAX; i<EVIL_CARD_MAX; i++) {
		if (deck[i] < '0') {
			ERROR_RETURN(-32, "check_deck:num_deck underflow i=%d deck[i]=%c eid=%d", i+1, deck[i], eid);
		}
		if (deck[i] > ('0' + EVIL_NUM_DECK_MAX)) {
			ERROR_RETURN(-42, "check_deck:num_deck overflow i=%d deck[i]=%c eid=%d", i+1, deck[i], eid);
		}

		if (deck[i] > card[i]) {
			WARN_PRINT(-72, "check_deck:deck_count_over_card i=%d deck[i]=%c card[i]=%c eid=%d", i+1, deck[i], card[i], eid);
			deck[i] = card[i];
		}

		// more than 0 (>=1) and not fit
		if (deck[i]>'0' && 0==fit_hero(hero_job, g_design->card_list[i+1].job)) {
			ERROR_RETURN(-16, "check_deck:job_unfit eid=%d hero=%d deck=%d"
			, eid, hero,i+1);
		}

		total += deck[i] - '0';
	}

	DEBUG_PRINT(0, "check_deck:total=%d", total);

	if (total > 100) {
		WARN_PRINT(E_RETURN_CHECK_DECK_OVER_MAX, "check_deck:total %d", total);
	}


	if (total < EVIL_DECK_CARD_MIN) {
		ERROR_RETURN(E_RETURN_CHECK_DECK_LESS_MIN
		, "check_deck:total %d < %d", total, EVIL_DECK_CARD_MIN);
	}

	if (game_type == GAME_QUICK) {
		if (total < QUICK_DECK_CARD_MIN) {
			ERROR_RETURN(E_RETURN_CHECK_DECK_LESS_QUICK_MIN
			, "check_deck:total %d < %d", total, QUICK_DECK_CARD_MIN);
		}
	}

	return 0;
}


// get hero id from deck (assume 400 char)
int get_hero(char *deck)
{
	if (deck == NULL) {
		ERROR_RETURN(-3, "get_hero:null_deck");
	}
	for (int i=0; i<HERO_MAX; i++) {
		if (deck[i] > '0') {
			return i+1;
		}
	}
	ERROR_RETURN(-6, "get_hero:no_hero");
}

// get collection from deck (assume 400 char)
int get_collection(char *deck)
{
	int total = 0;
	if (deck == NULL) {
		ERROR_RETURN(-3, "get_collection:null_deck");
	}
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		if (deck[i] > '0') {
			total ++;
		}
	}
	return total;
}

int __get_recommand_card_percent(const char *card_list, const char *recommand_slot)
{
	int total_slot_count = 0;
	int total_card_count = 0;

	int slot_count = 0;
	int card_count = 0;

	for (int i = 0; i < EVIL_CARD_MAX; i++) {
		slot_count = recommand_slot[i] - '0';
		card_count = card_list[i] - '0';
		total_slot_count += slot_count;
		if (slot_count == 0) {
			continue;	
		}

		card_count = (card_count > slot_count ? slot_count : card_count);
		total_card_count += card_count;
	}
	if (total_slot_count == 0) {
		total_slot_count = 1;
	}
	return min((int)(total_card_count * 100 / total_slot_count), 100);
}

// card_id is base 1
// euser.card and euser.deck is base 0
int add_card(evil_user_t &euser, int card_id, int count)
{
	char * card = euser.card;
	char ch;

	// DEBUG only:
	// printf("---- memory add_card: eid=%d  card_id=%d  count=%d\n", euser.eid, card_id, count);

	// normal
	if (card_id <= 0) {
		return 0;
	}

	int index = card_id - 1;  // resolve base 1 to base 0

	ch = card[index];
	if (ch >= '9') {
		return -2; // overflow, but no warning
	}
	if (ch < '0') {
		BUG_PRINT(-7, "add_card:invalid_ch card_id=%d eid=%d", card_id, euser.eid);
		return -7; // overflow, but no warning
	}
	ch += count;
	if (ch >= '9') {
		ch = '9';
	}
	card[index] = ch;

	return 0;
}


// input: 22 23 32 
// output: 3
// maximum 100,  if we encounter non-digit, stop
int count_card(const char *deck)
{
	int count = 0;
	int n;
	const char *ptr;
	int dummy = 0;
	int ret;

	ptr = deck;
	for (int i=0; i<=100; i++) {
		ret = sscanf(ptr, "%d %n", &dummy, &n);
		// printf("count_card: dummy=%d  ret=%d\n", dummy, ret);
		if (ret <= 0) {
			break;
		}
		count++;
		ptr += n;
	}
	return count;
}

design_chapter_stage_t * get_chapter_stage(int chapter_id, int stage_id)
{
	
	// check chapter_id, stage_id
	design_chapter_stage_t * stage = &g_design->design_chapter_list[chapter_id].stage_list[stage_id];
	if (stage == NULL) {
		BUG_PRINT(-3, "get_chapter_stage:null_stage %d %d", chapter_id, stage_id);
	}

	if (stage->stage_id != stage_id) {
		BUG_PRINT(-6, "get_chapter_stage:stage_id_mismatch %d %d", chapter_id, stage_id);
		return NULL;
	}

	return stage;
}


////////// UTILITY END //////////


////////// NET UTILITY START //////////

// logic specific
connect_t* get_conn(int cid)
{
	if (cid < 0 || cid>=MAX_CONNECT) {
		if (cid == -3) {
			WARN_PRINT(-3, "get_conn:conn_null_cid= %d", cid);
		} else { 
			ERROR_NEG_PRINT(-2, "get_conn:id_outband %d", cid);
		}
		return NULL;
	}

	return g_connect_list + cid;
}

// this is useless, because conn->id exists
// do not delete, for reference
// fast
int get_conn_id(connect_t * conn) 
{
	if (NULL == conn) {
		BUG_RETURN(-3, "get_conn_id:null_conn");
		// WARN_PRINT(-3, "get_conn_id:null_conn");
		return -3;
	}
	if (conn < g_connect_list) {
		BUG_RETURN(-2, "get_conn_id:conn_too_small");
		return -2;
	}
	return conn - g_connect_list;
}

#ifdef HCARD
int websocket_header(char *out_buffer, const char *in_buffer, int len)
{
    if (len < 126) {
        out_buffer[0] = 0x82;
        out_buffer[1] = len;
        out_buffer[2] = '\0';
        printf("header : len=%d\n", len);
        return 2;
    }
    out_buffer[0] = 0x82;
    out_buffer[1] = 126;
    out_buffer[2] = (len >> 8) & 0xFF;  // this will be zero
    out_buffer[3] = len & 0xFF;
    out_buffer[4] = '\0';
    printf("header : len=%d (%X)   len_high=%02X  len_low=%02X\n",  len, len,
        (int)(unsigned char) out_buffer[2], (int)(unsigned char) out_buffer[3]);
    return 4;
}

void print_hex(const char *title, const char *array, int len)
{
    printf("%s(%d):", title, len);
    for (int i=0; i<len; i++) {
        printf("  %02X", (int) (unsigned char) array[i]);
    }
    printf("\n");
}
#endif

#ifdef HCARD
// NOTE: for HCARD server, NOT for nio
int net_write(connect_t* conn, const char * str, const char last)
{
    if (conn == NULL) {
        ERROR_RETURN(-3, "net_write:conn_null");
    }

    if (conn->state == STATE_FREE) {
        ERROR_RETURN(-13, "net_write:conn_state_free conn_id=%d", get_conn_id(conn));
    }
    int len;
    len = strlen(str); // , BUFFER_SIZE-2);  // leave 2 space, for \n \0

    char header[10];
    int header_len = 0;
    header[0] = '\0';
    if (conn->websocket_flag == CONN_WEBSOCKET) {
        header_len = websocket_header(header, str, len+1);
    }
    // +4 for header, +5 for header + last
    if (conn->outlen + len + 5 >= BUFFER_SIZE) {
        // TODO if conn->outlen > 3/4 BUFFER_SIZE) not accept new command!
        BUG_PRINT( BUFFER_SIZE - conn->outlen - len
        , "net_write:buffer_overflow outlen %d len %d", conn->outlen, len);
        // now we write nothing, all or nothing strategy
        do_clean_disconnect(conn);  // this is rather fatal!
        return -2;
    }
    // implicit:  enough space in buffer

    // strncpy(s2, s1, n)
    // man strncpy: If s2 is less than n characters long, 
    // the remainder of s1 is filled with `\0' characters.  
    // Otherwise, s1 is not terminated.
    len += header_len;
    // since header may have zero, so we need memcpy, not strcpy
    memcpy(conn->buffer + conn->outlen, header, header_len);

    // printf("__before__ outlen=%d\n", conn->outlen);
    // print_hex("header", conn->buffer + conn->outlen, header_len);
    // data str should not have zero inside content
    strcpy(conn->buffer + conn->outlen + header_len, str);
    // print_hex("content", conn->buffer + conn->outlen + header_len, strlen(str));
    // strcat(conn->buffer + conn->outlen, str);
    conn->buffer[ conn->outlen + len ] = last;  // either \n or ' ' (space)
    conn->buffer[ conn->outlen + len + 1 ] = 0;
    // print_hex("full", conn->buffer + conn->outlen, len+1);
    conn->outlen += len + 1;    // +1 for \n   ( \0 is not counted)
    // printf("___after__ outlen=%d   len=%d  header_len=%d\n", conn->outlen, len, header_len);



    //  peter: @see thttpd.c:1712
    conn->state = STATE_SENDING;
    fdwatch_del_fd( conn->conn_fd );
    fdwatch_add_fd( conn->conn_fd, conn, FDW_WRITE );  // only write 
    // peter: do not use READ + WRITE, not work!
    return len;
}

#else

// it does not actually write, only write to buffer and 
// set the fdwatch // FDW_WRITE 
// fdwatch_check_fd(conn->conn_fd)
// - BUG ERROR nio.c:2759: ret 7 errno 35 fdwatch_check_fd
int net_write(connect_t* conn, const char * str, const char last)
{
	if (conn == NULL) {
		ERROR_RETURN(-3, "net_write:conn_null");
	}
		
	if (conn->state == STATE_FREE) {
		ERROR_RETURN(-13, "net_write:conn_state_free conn_id=%d", get_conn_id(conn));
	}
	int len;
	len = strlen(str); // , BUFFER_SIZE-2);  // leave 2 space, for \n \0
	if (conn->outlen + len >= BUFFER_SIZE) {
		// TODO if conn->outlen > 3/4 BUFFER_SIZE) not accept new command!
		BUG_PRINT( BUFFER_SIZE - conn->outlen - len
		, "net_write:buffer_overflow outlen %d len %d", conn->outlen, len);
		// now we write nothing, all or nothing strategy
		do_clean_disconnect(conn);  // this is rather fatal!
		return -2;
	}
	// implicit:  enough space in buffer

	// strncpy(s2, s1, n)
	// man strncpy:	If s2 is less than n characters long, 
	// the remainder of s1 is filled with `\0' characters.  
	// Otherwise, s1 is not terminated.
	strcpy(conn->buffer + conn->outlen, str);
	conn->buffer[ conn->outlen + len ] = last;  // either \n or ' ' (space)
	conn->buffer[ conn->outlen + len + 1 ] = 0;
	// printf("safe_write %d [%s]\n", len + 1, (conn->buffer+conn->outlen));
	conn->outlen += len + 1;    // +1 for \n   ( \0 is not counted)

	//  peter: @see thttpd.c:1712
	conn->state = STATE_SENDING;
	fdwatch_del_fd( conn->conn_fd );
	fdwatch_add_fd( conn->conn_fd, conn, FDW_WRITE );  // only write 
	// peter: do not use READ + WRITE, not work!

	// check so_error here
	/*
	int error;
	socklen_t errlen=sizeof(error);
	getsockopt(conn->conn_fd,SOL_SOCKET,SO_ERROR,&error,&errlen);
	if (error != 0) {
		WARN_PRINT(-1, "net_write:error=%d, msg=%s",error,strerror(error));
	} else {
		INFO_PRINT(0, "net_write:error=%d, msg=%s",error,strerror(error));
	}
	*/

	return len;
}
#endif

int net_writeln(connect_t* conn, const char * fmt, ...)
{
	char buffer[BUFFER_SIZE + 1]; 
    va_list argptr;
    va_start(argptr, fmt);
    vsnprintf(buffer, BUFFER_SIZE, fmt, argptr);
    va_end(argptr);
	return net_write(conn, buffer, '\n');
}

int net_write_space(connect_t* conn, const char * fmt, ...)
{
	char buffer[BUFFER_SIZE + 1]; 
    va_list argptr;
    va_start(argptr, fmt);
    vsnprintf(buffer, BUFFER_SIZE, fmt, argptr);
    va_end(argptr);
	return net_write(conn, buffer, ' ');
}


// max 490 character
// errcode must be < 0
// TODO make this a macro, for ERROR_PRINT to get the line number
int net_error(connect_t * conn, int errcode, const char *fmt, ...)
{
	if (errcode == 0) return 0;  // no error print for 0
	char buffer[500 + 1]; 
	char *ptr;
	ptr = buffer + sprintf(buffer, "%d ",  errcode);
    va_list argptr;
    va_start(argptr, fmt);
    vsnprintf(ptr, 490, fmt, argptr);
    va_end(argptr);
	return net_write(conn, buffer, '\n');
}

// this is for lua_pcall error
inline int lua_error_print(lua_State *lua, connect_t * conn, int ret, const char *str)
{
	if (ret == 0) {
		return 0;
	}

	const char *err = lua_tostring(lua, -1);

	if (err == NULL) {
		BUG_PRINT(-3, "lua_error_print:null_err");
		lua_pop(lua, 1);
		return 0;
	}

	net_writeln(conn, "gerr -68 %s ret=%d err=%s", str, ret, err);
	ERROR_PRINT(-68, "lua_pcall %s ret=%d err=%s", str, ret, err);
	lua_pop(lua, 1);
	return 0;
}


// init the g_connect_list[], g_free_connect
int init_conn()
{
	// g_connect_list==&g_connect_list[0]
	// g_connect_list + 3 == &g_connect_list[3]
	// g_connect_list = NEW( connect_t, g_max_connect);  
	bzero(g_connect_list, MAX_CONNECT * sizeof(connect_t));
	for (int i=0; i<MAX_CONNECT; i++) {
		// order is important
		// same: memset(  &g_connect_list[i], ...)
		//g_connect_list[i].id = i; // assert: g_connect_list[i].id == i
		g_connect_list[i].state = STATE_FREE;
		g_connect_list[i].next_free = i + 1;
	}
	g_connect_list[MAX_CONNECT - 1].next_free = -1;  // end of link list
	g_free_connect = 0; // first free slot for stub
	return 0;
}

connect_t * new_conn(int fd, struct sockaddr_in *address)
{
	connect_t * conn;
	conn = get_conn(g_free_connect);
	if (conn == NULL) {
		BUG_PRINT(-7, "new_conn:null g_free_connect %d", g_free_connect);
		return NULL;
	}
	if (conn->state!=STATE_FREE) {
		BUG_PRINT(-17, "new_conn messed up not STATE_FREE %d state=%d"
		, g_free_connect, conn->state);
		// peter: temp fix, search a free conn from all 
		for (int i=0; i<MAX_CONNECT; i++) {
			conn = g_connect_list + i;
			if (conn->state == STATE_FREE) {
				g_free_connect = i;  // reset g_free_connect
				break; 
			}
		}
		// double check if it is still not free, return null
		if (conn->state != STATE_FREE) {
			return NULL;
		}
	}

	conn->conn_fd = fd;
	conn->address = *address;
	conn->st = 0;
	conn->state = STATE_READING;
	conn->db_flag = 0;
	conn->buffer[0] = '\0'; conn->outlen = 0; conn->offset = 0;
	conn->read_buffer[0] = '\0'; conn->read_offset = 0;
	g_free_connect = conn->next_free;
	conn->next_free = -1;
	++g_num_connect;

	conn->wchat_ts = wchat_tail_ts();  // GCHAT
	return conn;
}


int free_conn(connect_t *conn)
{
//	printf("----- free_conn: cid=%d\n", get_conn_id(conn));
	bzero(conn, sizeof(connect_t)); // order is important
	//conn->id = conn - g_connect_list; // safe!
	conn->state = STATE_FREE; // actually not necessary, STATE_FREE=0
	conn->next_free = g_free_connect;  // order: after bzero
	// conn - g_connect_list = index , where g_connect_list[index] == conn
	g_free_connect = conn - g_connect_list;  // pointer arithmetics
	--g_num_connect;
	return 0;
}

// fast
int get_eid(connect_t * conn) 
{
	// TODO check null
	if (conn == NULL) {
		return -3;
	}
	assert(conn != NULL);
	return conn->euser.eid;
}


// eid -> connect_t *
// slow, hash access
connect_t *	get_conn_by_eid( int eid )  
{
	int id;
	// TODO check eid < MAX_AI_EID
	id = g_user_index[eid];   // if not found, it will return 0
	if (id < 0 || id >= MAX_CONNECT) {
		BUG_PRINT(-2, "get_conn_by_eid:id_outbound");
		return NULL;
	}

	connect_t * conn;
	conn = get_conn(id);
	if (conn->state == STATE_FREE) {
		return NULL;
	}

	// connect_t[0] may be re-used and the eid will not match
	if (eid != get_eid(conn)) {
		// id == 0 is special, initially g_user_index[*] = 0
		if (id != 0) {
			WARN_PRINT(-1, "get_conn_by_eid:eid_mismatch %d!=%d cid=%d"
				, eid, get_eid(conn) , id);
			g_user_index[eid] = 0;
		}
		return NULL;
	}
	// note:   conn will be bzero and re-used, bzero euser.eid == 0
	// do not use eid=0 in database!
	return conn;
}

int check_vip(connect_t *conn) 
{
	if (conn == NULL) {
		return 0;
	}

	if (conn->euser.eid <= 0) {
		return 0;
	}
	if (conn->euser.monthly_end_date < time(NULL)) {
		return 0;
	}

	return 1;
}

int chat_format(char *out_buffer, const char * cmd, int eid, const char *alias, int vip_flag, int show_type, const char *msg)
{
	int len;
	len = sprintf(out_buffer, "%s %d %s %d %d %s", cmd, eid, alias, vip_flag, show_type, msg);
	return len;
}

// broadcast the str to all guests in room, except one eid
// @return count the number of guests broadcasted
int broadcast_room(room_t * proom, int except_eid, const char *str)
{
	int count = 0;
	for (int i=0; i<proom->num_guest; i++) {
		int guest_eid = proom->guest[i];
		// peter: add <= 20 for ai 
		if (guest_eid == except_eid || guest_eid <= MAX_AI_EID) {
			continue;  // skip AI eid 
		}
		connect_t*  guest_conn = get_conn_by_eid(guest_eid);
		if (guest_conn == NULL) {
			continue; // skip offline 
		}
		net_write(guest_conn, str, '\n');
		count ++;
	}
	return count;
}

// broadcast the str to non-playing guests(guest[2], guest[3]..) in room 
// @return count the number of guests broadcasted
int broadcast_room_guest(room_t * proom, int except_eid, const char *str)
{
	int count = 0;
	for (int i=2; i<proom->num_guest; i++) {
		int guest_eid = proom->guest[i];
		// peter: add <= 20 for ai 
		if (guest_eid == except_eid || guest_eid <= MAX_AI_EID) {
			continue;  // skip AI eid 
		}
		connect_t*  guest_conn = get_conn_by_eid(guest_eid);
		if (guest_conn == NULL) {
			continue; // skip offline 
		}
		net_write(guest_conn, str, '\n');
		count ++;
	}
	return count;
}

match_t & get_match(long match_id)
{
	static match_t empty_match = {0};
	if (match_id <= 0) {
		BUG_PRINT(-2, "get_match:id_outbound %ld", match_id);
		return empty_match;
	}
	for (int i = 0; i < MAX_MATCH; i++) {
		match_t &match = g_match_list[i];
		if (match.match_id == match_id) {
			return match;
		}
	
	}
	
	return empty_match;
}

////////// NET UTILITY END //////////


//////////// DB DESIGN START [ ///////////
const card_t * get_card_by_id(int cid)
{
	if (cid <= 0 || cid > EVIL_CARD_MAX) {
		ERROR_PRINT(-5, "get_card_by_id:invalid_cid %d", cid);
		return NULL;
	}
	
	const card_t *pcard = &g_design->card_list[cid];

	if (pcard->id != cid) {
		BUG_PRINT(-7, "get_card_by_id:id_mismatch %d %d", pcard->id, cid);
		return NULL;
	}

	// DEBUG_PRINT(0, "get_card_by_id:%d %d %d %d %s", pcard->id, pcard->star, pcard->job, pcard->cost, pcard->name);

	return pcard;
}

int load_design_shop_card(shop_t *shop_list) {

	int ret;

	///////// LOAD SHOP ////////
	// char shop_list[10010];
	// bzero(shop_list, 10010);
	// bzero outside
//	bzero(shop_list, sizeof(shop_list));//g_shop_list[*].card_id=0 
	ret = db_design_load_shop(shop_list);
	BUG_RETURN(ret, "db_design_load_shop");
	// FATAL_EXIT(ret, "db_design_load_shop");


//	shop_t ccc = g_shop_list[23];
//	DEBUG_PRINT(0, "load_shop_card:test:ccc.card_id=%d, gb=%d, gs=%d, cb=%d,cs=%d"
//		, ccc.card_id, ccc.card_buy_gold, ccc.card_sell_gold, ccc.card_buy_crystal, ccc.card_sell_crystal);

	return ret;
}

int load_design_std_deck(std_deck_t *std_deck_list) 
{
	int ret;
	// bzero(g_std_deck_list, sizeof(std_deck_list));
	ret = db_design_load_std_deck(std_deck_list);
	BUG_RETURN(ret, "db_design_load_std_deck");

	// TODO check the g_ai_list

	// simple check now, at least we have warrior
	WARN_PRINT(std_deck_list[1].id != 1
	, "load_std_deck:warrior1_not_found %d"
	, std_deck_list[1].id);

	char *deck;
	deck = std_deck_list[1].deck;
	WARN_PRINT(EVIL_CARD_MAX != strlen(deck), "load_std_deck:warrior1_len %zd"
	, strlen(deck));

	// DEBUG PRINT //
	/*
	std_deck_t std_deck;
	for (int i=0; i<HERO_MAX+1; i++) {
		std_deck = g_std_deck_list[i];
		if (std_deck.id == 0) continue;
		DEBUG_PRINT(0, "std_deck, id=%d, deck=%s"
		, std_deck.id, std_deck.deck);
	}
	*/


//	DEBUG_PRINT(0, "load_ai:OK %d", ret);
	return ret;
}

int load_design_ai(ai_t *ai_list) 
{
	int ret;
	// bzero(g_ai_list, sizeof(ai_list));
	ret = db_design_load_ai(ai_list);
	BUG_RETURN(ret, "db_design_ai");

	// TODO check the g_ai_list

	// simple check now, at least we have warrior
	WARN_PRINT(ai_list[1].id != 1, "load_ai:warrior1_not_found %d"
	, ai_list[1].id);

	char *deck;
	deck = ai_list[1].deck;
	WARN_PRINT(EVIL_CARD_MAX != strlen(deck), "load_ai:warrior1_len %zd"
	, strlen(deck));


//	DEBUG_PRINT(0, "load_ai:OK %d", ret);
	return ret;
}

int load_design_pick(pick_t *pick_list)
{
	int ret;
	// bzero(g_pick_list, sizeof(g_pick_list));
	ret = db_design_load_pick(pick_list);
	BUG_RETURN(ret, "db_design_pick");

	// zero sum check
	for (int p=0; p<MAX_PICK; p++) {
		pick_t *pick = pick_list + p;
		for (int loc=0; loc<MAX_LOC; loc++) {
			int sum = 0 ;
			for (int star=0; star<MAX_STAR; star++) {
				sum += pick->batch_rate[loc][star];
			}
			if (sum <= 0) {
				BUG_PRINT(-6, "load_pick:sum_zero p=%d loc=%d", p, loc);
				ret = -6;
			}
		}
	}
	if (ret < 0) {
		return ret;
	}

	// header
	/***
	printf("ptype\tloc\trate\tstar1\tstar2\tstar3\tstar4\tstar5\n");
	printf("---------------------------------\n");
	for (int p=0; p<MAX_PICK; p++) {
		pick_t *pick;
		pick = g_pick_list + p;
		for (int loc=0; loc<MAX_LOC; loc++) {
			printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n"
				, p, loc, pick->pick_rate[loc]
				, pick->batch_rate[loc][0]
				, pick->batch_rate[loc][1]
				, pick->batch_rate[loc][2]
				, pick->batch_rate[loc][3]
				, pick->batch_rate[loc][4]
			);
		}
	} // end for pick
	**/
	return ret;
}

// peter: XXX vector<card_t> [] is consider as dangerous param passing
int load_design_card(card_t *card_list
, star_t * star_list
, star_t * extract_list
, int *hero_hp_list) {
// , vector<card_t> star_list[MAX_STAR]
// , vector<card_t> extract_list[MAX_STAR]) 
	int ret;
	int value;
	int total_star, total_valid, total_extract;
	char *name;
	char job_name[100];
	card_t *card;

	lua_State * lua;
	lua = luaL_newstate();
	assert(lua != NULL);
	luaL_openlibs(lua);
	lu_set_int(lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(lua, "res/lua/logic.lua");
	FATAL_EXIT(ret, "load_card:dofile logic.lua");
	ret = luaL_dofile(lua, "res/lua/lang_zh.lua"); // for name!
	FATAL_EXIT(ret, "load_card:dofile lang_zh.lua");

	lua_pushinteger(lua, 1974);

	total_valid = 0;
	// bzero(g_card_list, sizeof(g_card_list)); // zero everything 
	for (int i=1; i<=EVIL_CARD_MAX; i++) {
		const char *table = (i>20) ? "g_card_list" : "hero_list";
		card = card_list + i;
		bzero(card, sizeof(card_t));
		name = card->name;
		ret = lu_get_table_index_name(lua, name, 99, table, i, "name");
		if (ret < 0) {
			card->id = 0;
			continue;
		}
		job_name[0] = 0;  // hard code to append job_name after name
		ret = lu_get_table_index_name(lua, job_name, 99, table, i, "job_name");
		strcat(name, job_name);

		total_valid ++;
		card->id = i;
		value = 99; ret = lu_get_table_index_int(lua, &value, table, i, "cost");
		ERROR_NEG_PRINT(ret, "load_design_card:cost_error i=%d", i);
		card->cost = value;
		// TODO add job_str to name

		/*
		card->star = card->cost; // (i % 5) + 1;
		if (card->star < 1) card->star = 1;
		if (card->star > MAX_STAR) card->star = MAX_STAR;
		*/
		value = 5; ret = lu_get_table_index_int(lua, &value, table, i, "star");
		ERROR_NEG_PRINT(ret, "load_design_card:cost_error i=%d", i);
		card->star = value;

		value = 0; ret = lu_get_table_index_int(lua, &value, table, i, "job");
		ERROR_NEG_PRINT(ret, "load_design_card:job_error i=%d", i);
		card->job = value;
		
		ret = lu_get_table_index_int(lua, &value, table, i, "id");
		if (ret < 0) {
			BUG_PRINT(ret, "load_design_card:get_int %d", i);
		}
		if (value != card->id) {  // or check against i
			BUG_PRINT(ret, "load_design_card:id_mismatch %d %d", value, card->id);
		}
	}

	LU_CHECK_MAGIC(lua);


	// debug check
	/***
	printf("id\tstar\tjob\tname\n");
	printf("----------------------\n");
	for (int i=1; i<=EVIL_CARD_MAX; i++) {
		card = g_card_list + i;
		if (card->id == 0) { continue; }
		printf("%d\t%d\t%d\t%s\n", card->id, card->star, card->job, card->name);
	}
	printf("----------------------\n");
	***/

	// bzero outside, assert star_one.size = 0;
	for (int i=0; i<=EVIL_CARD_MAX; i++) {
		card = card_list + i;
		if (card->id==0) { continue; }
		if (card->star < 1 || card->star>MAX_STAR)  {
			BUG_PRINT(i, "load_design_card:star_outbound %d", card->star);
			continue; // is it fatal?  error_count++ ?
		}
		star_t & star_one = star_list[card->star-1];
//		star_list[card->star-1].push_back(*card);
		star_one.card_list[star_one.size] = *card;
		star_one.size ++ ;
	}

	total_star = 0;
	for (int star=0; star<MAX_STAR; star++) {
		total_star += star_list[star].size;
	}
	//

	// fill up g_pick_list
//	for (int i=0; i<MAX_STAR; i++) {
//		extract_list[i].clear();
//	}

	// uninclude hero card
	for (int i=HERO_MAX+1; i<=EVIL_CARD_MAX; i++) {
		card = card_list + i;
		if (card->id==0) { continue; }
		if (card->star < 1 || card->star>MAX_STAR)  {
			BUG_PRINT(i, "load_design_card:star_outbound %d", card->star);
			continue; // is it fatal?  error_count++ ?
		}
//		extract_list[card->star-1].push_back(*card);
		star_t & star_one = extract_list[card->star-1];
		star_one.card_list[star_one.size] = *card;
		star_one.size++;
	}

	total_extract = 0;
	for (int star=0; star<MAX_STAR; star++) {
		total_extract += extract_list[star].size;
	}
	//

	// check
	/***
	printf("-------- g_star_list -------\n");
	for (int star=0; star<MAX_STAR; star++) {
		printf("<<<<< star%d\n", star+1);
		for (int i=0; i<(int)g_star_list[star].size(); i++) {
			card_t & ccc = g_star_list[star][i];
			printf("%d\t%d\t%s\n", ccc.id, ccc.star, ccc.name);
		}
		printf(">>>>> end\n");
	}
	printf("-------- g_extract_list -------\n");
	for (int star=0; star<MAX_STAR; star++) {
		printf("<<<<< star%d\n", star+1);
		for (int i=0; i<(int)g_extract_list[star].size(); i++) {
			card_t & ccc = g_extract_list[star][i];
			printf("%d\t%d\t%s\n", ccc.id, ccc.star, ccc.name);
		}
		printf(">>>>> end\n");
	}
	***/
	
	DEBUG_PRINT(0, "load_design_card:total valid=%d, star=%d, extract=%d"
	, total_valid, total_star, total_extract);

	if (total_valid != total_star) {
		WARN_PRINT(-6, "load_design_card: total_valid %d != total_star %d"
		, total_valid, total_star);
	} else {
		// INFO_PRINT(0, "GOOD:card_list: %d total_valid = match total_star", total_valid);
	}


	// TODO read hp in lua
	hero_hp_list[1] = 30;
	hero_hp_list[2] = 30;
	hero_hp_list[3] = 28;
	hero_hp_list[4] = 28;
	hero_hp_list[5] = 26;
	hero_hp_list[6] = 26;
	hero_hp_list[7] = 26;
	hero_hp_list[8] = 26;
	hero_hp_list[9] = 27;
	hero_hp_list[10] = 27;
	hero_hp_list[11] = 30;
	hero_hp_list[12] = 30;
	hero_hp_list[13] = 28;
	hero_hp_list[14] = 28;
	hero_hp_list[15] = 26;
	hero_hp_list[16] = 26;

	

	LU_CHECK_MAGIC(lua);

	if (lua != NULL) {
		lua_pop(lua, 1);
		lua_close(lua);
	}

	return 0;  // ret is used in the middle
}

int load_design_constant(constant_t * constant) {

	int ret;

	ret = db_design_load_constant(constant);
	BUG_RETURN(ret, "db_design_constant");
	// FATAL_EXIT(ret, "db_design_load_constant");

//	DEBUG_PRINT(0, "load_design_constant:win_quick_gold=%d, win_solo_gold=%d, batch_refresh_gold=%d, pick_gold=%d, batch_refresh_crystal=%d, pick_crystal=%d, max_timeout=%d, win_quick_exp=%d, win_solo_exp=%d, lose_quick_exp=%d, lose_solo_exp=%d, draw_quick_exp=%d, draw_solo_exp=%d, guild_bonus_rate=%lf, create_guild_gold=%d, create_guild_crystal=%d"
//	, g_constant.win_quick_gold, g_constant.win_solo_gold
//	, g_constant.batch_refresh_gold, g_constant.pick_gold
//	, g_constant.batch_refresh_crystal, g_constant.pick_crystal
//	, g_constant.max_timeout, g_constant.win_quick_exp
//	, g_constant.win_solo_exp, g_constant.lose_quick_exp
//	, g_constant.lose_solo_exp
//	, g_constant.draw_quick_exp, g_constant.draw_solo_exp
//	, g_constant.guild_bonus_rate
//	, g_constant.create_guild_gold, g_constant.create_guild_crystal
//	);

	return ret;
}

int load_design_exp(int * exp_list, int &max_level) {
	int ret;
//	int exp;
//	char exp_str[1000];
//	bzero(exp_str, 1000);
//	char *ptr;
//	int n;

	ret = db_design_load_exp(exp_list);
	BUG_NEG_RETURN(ret, "db_design_exp");

	if (ret > MAX_LEVEL) {
		BUG_RETURN(-2, "db_design_exp: max_level_out_bound %d %d", ret, MAX_LEVEL);
	}
	max_level = ret;
//	ret = db_design_load_exp(exp_str);
//	BUG_RETURN(ret, "db_design_exp");
//	// FATAL_EXIT(ret, "db_design_load_exp");
//
//	ptr = exp_str;
//	ret = sscanf(ptr, "%d %n", &max_level, &n);
//	if (ret != 1) {
//		BUG_RETURN(-7, "db_design_load_exp:sscanf");
//	}
//
//	if (max_level > MAX_LEVEL) {
//		BUG_RETURN(-6, "db_design_load_exp:max_level > MAX_LEVEL(%d > %d)"
//		, max_level, MAX_LEVEL);
//	}
//
////	exp_vector.clear();
//
//	// start from 1
////	exp_vector.push_back(0);
//	exp_list[0] = 0;	// exp_list base 1
//	for(int i=1; i<=max_level; i++) {
//		ptr += n;
//		ret = sscanf(ptr, "%d %n", &exp, &n);
//		if (ret != 1) {
//			BUG_RETURN(-6, "db_design_load_exp:null_exp");
//		}
////		exp_vector.push_back(exp);
//		exp_list[i] = exp;
//	}

	return 0;
}


int load_design_notice(notice_t *notice_list, int &notice_count) {
	int ret;

	// bzero(notice_list, sizeof(notice_list));

	ret = db_design_load_notice(notice_list);
	BUG_RETURN(ret, "db_design_load_notice");
	// XXX no fatal, too danger
	// FATAL_EXIT(ret, "db_design_load_notice");

	notice_count = 0;
	for (int i=1;i<= MAX_NOTICE;i++) {
		if (notice_list[i].title[0] != '\0') {
			notice_count++;
		}
	}

	printf("notice_count=%d\n", notice_count);

	ret = 0;

	return ret;
}

int load_design_guild(design_guild_t * design_guild_list, int &guild_max_level) {
	int ret;

	guild_max_level = 0;
	// bzero(g_design_guild_list, sizeof(g_design_guild_list));
	ret = db_design_load_guild(design_guild_list);
	// FATAL_EXIT(ret, "db_design_load_guild_consume");
	BUG_RETURN(ret, "db_design_load_guild");

	for (int i=0; i<100;i++) {
		if (design_guild_list[i].lv > guild_max_level) {
			guild_max_level = design_guild_list[i].lv;
		}
	}

	/*
	// DEBUG PRINT
	design_guild_t *pguild;
	printf("load_design_guild start, g_guild_max_level=%d\n"
	, g_guild_max_level);
	for (int i=0; i<100; i++) {
		pguild = &g_design_guild_list[i];
		if (pguild->lv == 0) continue;
		printf("lv=%d, consume_gold=%d, levelup_gold=%d, member_max=%d\n"
		, pguild->lv, pguild->consume_gold, pguild->levelup_gold
		, pguild->member_max);
	}
	*/

	return ret;
}

int load_design_merge(design_merge_t * design_merge_list) {
	int ret;

	// bzero(g_design_merge_list, sizeof(g_design_merge_list));
	ret = db_design_load_merge(design_merge_list);
	BUG_RETURN(ret, "db_design_merge");
	// FATAL_EXIT(ret, "db_design_load_merge_consume");

	return ret;
}

int load_design_pay(design_pay_t * design_pay_list) {
	int ret;

	// bzero(g_design_pay_list, sizeof(g_design_pay_list));
	ret = db_design_load_pay(design_pay_list);
	BUG_RETURN(ret, "db_design_load_pay");
	// FATAL_EXIT(ret, "db_design_load_pay_consume");

	return ret;
}

int load_design_monthly(int * pay_monthly_list, int &max_count) {

	int ret;

	ret = db_design_load_monthly(pay_monthly_list, max_count);
	BUG_RETURN(ret, "db_design_load_pay_monthly");

	return 0;
}

int load_design_version(design_version_t * design_version) {
	int ret;

	// bzero(&g_design_version, sizeof(design_version_t));
	ret = db_design_load_version(design_version);
	BUG_RETURN(ret, "db_design_load_version");
	// FATAL_EXIT(ret, "db_design_load_version_consume");

	return ret;
}

int load_design_website(design_website_t * design_website_list) {
	int ret;

	// bzero(g_design_website_list, sizeof(g_design_website_list));
	ret = db_design_load_website(design_website_list);
	BUG_RETURN(ret, "db_design_load_website");
	// FATAL_EXIT(ret, "db_design_load_pay_consume");

	return ret;
}

int load_design_mission(design_mission_t *mission_list) {
	int ret;

//	mission_list.clear();
	ret = db_design_load_mission(mission_list);
	BUG_RETURN(ret, "db_design_load_mission");

	// check: 
	// 1. pre mission exists
	for (int i = 1; i < MAX_MISSION; i++) {
		design_mission_t & mis = mission_list[i];
		if (mis.mid == 0) {
			continue;
		}

		if (mis.pre == 0) {
			continue;
		}

		// below mission should has pre_mission
		design_mission_t &pmis = mission_list[mis.pre];
		if (pmis.mid == 0) {
			BUG_RETURN(-65, "db_design_load_mission:pre_mission pre=%d mid=%d", mis.pre, mis.mid);
		}

	}

	/*
	for (D_MISSION_MAP::iterator it=mission_list.begin()
	; it!=mission_list.end(); it++) {

		design_mission_t & mis = (*it).second;
		// print_design_mission(mis);

		if (mis.pre == 0) {
			continue;
		}
		// if point to end, means cannot find the mis.pre
		if (mission_list.find(mis.pre) == mission_list.end()) {
			BUG_RETURN(-65, "db_design_load_mission:pre_mission pre=%d mid=%d", mis.pre, mis.mid);
		}


	}
	*/

	// for debug
	// printf("--- after load_design_mission:\n");
	// print_design_mission_list(mission_list);

	return 0;
}

int load_design_slot(design_slot_t * slot_list) {
	int ret;

	// bzero outside
//	bzero(slot_list, sizeof(slot_list));
	ret = db_design_load_slot(slot_list);
	BUG_RETURN(ret, "db_design_load_slot");

	return ret;
}

int load_design_rank(design_rank_reward_t * reward_list, int &max_level, design_rank_time_t * time_list, int &max_time) {
	int ret;

	max_level = 0;
	ret = db_design_load_rank_reward(reward_list);
	BUG_RETURN(ret, "db_design_load_rank_reward");

	for (int i=0; i<MAX_RANK_REWARD; i++) {
		if (reward_list[i].id > 0) {
			max_level++;
		}
	}
	DEBUG_PRINT(0, "load_design_rank_reward:max_level=%d", max_level);

	max_time = 0;
	ret = db_design_load_rank_time(time_list);
	BUG_RETURN(ret, "db_design_load_rank_time");

	for (int i=0; i<MAX_RANK_TIME; i++) {
		if (time_list[i].id > 0) {
			max_time++;
		}
	}
	// DEBUG_PRINT(0, "load_design_rank_time:max_level=%d", max_time);

	return ret;
}

int load_db_match(match_t &match) {
	int ret;

	ret = db_design_load_match(&match);
	if (ret != 0) {
		BUG_RETURN(ret, "load_db_match");
		bzero(&match, sizeof(match));
	}
	if (match.match_id == 0) {
		DEBUG_PRINT(0, "load_db_match:no_running_match");
		return 0;
	}
	ret = db_design_load_match_player(&match);
	if (ret != 0) {
		BUG_RETURN(ret, "load_db_match_player %ld", match.match_id);
		bzero(&match, sizeof(match));
	}

	// fix running match status, round problem
	// note: if load match in db, that match status may == round_start, load_db_match will change status to round_end, but this round is not over, in round start function should not update match.round, just start the rest of game in this round
	// if status == round start, there will have 2 situation
	// 1.round not end but all round game is over, round_start can do next round game, match.round++
	// 2.round not end and not all round game is over, round_start should start the rest of game in this round, do not update match.round
	if (match.status == MATCH_STATUS_ROUND_START) {
		match.status = MATCH_STATUS_ROUND_END;
		match.round -= 1; // XXX let match.round go back to last round end
	}

	return ret;
}

int load_design_fight_schedule(design_fight_schedule_t *fight_schedule_list, int &max_fight_schedule)
{
	int ret;
	ret = db_design_load_fight_schedule(fight_schedule_list, max_fight_schedule);
	BUG_RETURN(ret, "db_design_fight_schedule");
	
	WARN_PRINT(max_fight_schedule <= 0, "load_design_fight_schedule:list_len %d"
	, max_fight_schedule);

	return ret;
}

int load_design_lottery(design_lottery_t * lottery) {
	int ret;

	ret = db_design_load_lottery(lottery);
	BUG_RETURN(ret, "db_design_load_lottery");

	return ret;
}

int check_gate_info(int size, char * gate_info) {

	int ret;
	int n;
	int r;
	int c;
	char *ptr;
	ptr = gate_info;

	n = 0;
	for (int i=0; i<size; i++) {
		ptr += n;
		ret = sscanf(ptr, "%d %d %n", &r, &c, &n);
		if (ret != 2) {
			ERROR_RETURN(-6, "check_gate_info:gate_info_sscanf_bug %d %d %s", ret, size, gate_info);
		}

		if (r <= 0 || r >= 100) {
			ERROR_RETURN(-2, "check_gate_info:round_out_bound %d %d %s", r, size, gate_info);
		}

		if (get_card_by_id(c) == NULL) {
			ERROR_RETURN(-3, "check_gate_info:card_null %d %d %s", c, size, gate_info);
		}
	}

	return 0;

}

int load_design_gate(int * size, design_gate_t * gate_list) {
	int ret;
	int num;

	ret = db_design_load_gate(gate_list);
	BUG_RETURN(ret, "db_design_load_gate");

	num = 0;
	for (int i=1; i<MAX_GATE_LIST; i++) {
		design_gate_t & gate = gate_list[i];
		if (gate.gate_info[0] != '\0') {
			num++;
		}

		ret = check_gate_info(gate.size, gate.gate_info);
		if (ret != 0) {
			BUG_RETURN(-6, "load_design_gate:gate_info_bug %d", i);
		}
	}
	*size = num;
	DEBUG_PRINT(0, "load_design_gate:size=%d", *size);

	return ret;
}

int load_design_gate_msg(design_gate_msg_t * msg_list) {
	int ret;

	ret = db_design_load_gate_msg(msg_list);
	BUG_RETURN(ret, "db_design_load_gate_msg");
	return ret;
}


int load_design_tower(design_tower_reward_t * reward_list, int &max_level) {
	int ret;

	max_level = 0;
	ret = db_design_load_tower_reward(reward_list);
	BUG_RETURN(ret, "db_design_load_tower_reward");

	for (int i=0; i<MAX_RANK_REWARD; i++) {
		if (reward_list[i].id > 0) {
			max_level++;
		}
	}
	DEBUG_PRINT(0, "load_design_tower_reward:max_level=%d", max_level);

	return ret;
}

int load_design_piece_shop(design_piece_shop_t * shop_list, int *max_weight, design_pshop_show_map_t *show_map, int *max_show, char *piece_list) 
{
	int ret;

	ret = db_design_load_piece_shop(shop_list, max_weight);
	BUG_RETURN(ret, "db_design_load_piece_shop");

	*max_show = 0;
	memset(piece_list, '0', EVIL_CARD_MAX);
	piece_list[EVIL_CARD_MAX] = '\0';
	for (int i = 0; i < MAX_PIECE_SHOP_LIST; i++)
	{
		design_piece_shop_t &shop = shop_list[i];
		if (shop.id == 0)			{	continue;	}
		piece_list[shop.card_id-1] = '1';

		if (shop.hard_show == 0)	{	continue;	}
		if (shop.hard_show < 0 || shop.hard_show >= MAX_PIECE_SHOP_HARD_SHOW)
		{
			BUG_RETURN(-shop.id, "db_design_load_piece_shop:hard_show");
		}
		design_pshop_show_map_t &show = show_map[shop.hard_show];
		if (show.count >= MAX_PIECE_SHOP_SLOT) {
			BUG_RETURN(-shop.id, "db_design_load_piece_shop:count_out_bound");
		}
		show.pid_list[show.count++] = shop.id;
		if (*max_show < shop.hard_show)
		{
			*max_show = shop.hard_show;
		}
	}
	for (int i = 1; i <= *max_show; i++)
	{
		if (show_map[i].count != MAX_PIECE_SHOP_SLOT) {
			BUG_RETURN(-i, "db_design_load_piece_shop:count_not_enough");
		}
	}
	DEBUG_PRINT(0, "load_design_piece_shop:max_show=%d", *max_show);

	return ret;
}

int load_design_solo(solo_t * solo_list) {
	int ret;
	int size;

	ret = db_design_load_solo(solo_list);
	BUG_RETURN(ret, "db_design_load_solo_list");

	size = 0;
	for (int i=1; i<=MAX_AI_EID; i++) {
		solo_t * solo = solo_list + i;
		if (solo->id == 0) {
			continue;
		}
		// DEBUG_PRINT(0, "load_design_solo:id=%d hero=%d alias=%s game_flag=%d ai_max_ally=%d deck=[%s] type_list=[%s]", solo->id, solo->hero_id, solo->alias, solo->game_flag, solo->ai_max_ally, solo->deck, solo->type_list);
		size++;
	}
	DEBUG_PRINT(0, "load_design_solo:size=%d", size);

	return ret;
}

int load_design_robot(int *robot_size, design_robot_t * robot_list, int *fakeman_size, design_robot_t * fakeman_list) 
{
	int ret;

	ret = db_design_load_robot(robot_list, fakeman_list);
	BUG_RETURN(ret, "db_design_load_robot_list");

	*robot_size = 0;
	for (int i=1; i<=MAX_AI_EID; i++) {
		design_robot_t * robot = robot_list + i;
		if (robot->id == 0) {
			continue;
		}
		// DEBUG_PRINT(0, "load_design_robot:id=%d rtype=%d icon=%d lv=%d rating=%lf hero=%d alias=%s deck=[%s]", robot->id, robot->rtype, robot->icon, robot->lv, robot->rating, robot->hero_id, robot->alias, robot->deck);
		(*robot_size)++;
	}
	DEBUG_PRINT(0, "load_design_robot:robot_size=%d", *robot_size);

	*fakeman_size = 0;
	for (int i=1; i<=MAX_AI_EID; i++) {
		design_robot_t * fakeman = fakeman_list + i;
		if (fakeman->id == 0) {
			continue;
		}
		// DEBUG_PRINT(0, "load_design_robot:id=%d rtype=%d icon=%d lv=%d rating=%lf hero=%d alias=%s deck=[%s]", fakeman->id, fakeman->rtype, fakeman->icon, fakeman->lv, fakeman->rating, fakeman->hero_id, fakeman->alias, fakeman->deck);
		(*fakeman_size)++;
	}
	DEBUG_PRINT(0, "load_design_robot:fakeman_size=%d", *fakeman_size);

	if (*robot_size <= 0 || *fakeman_size <= 0) {
		BUG_RETURN(-5, "load_design_robot:size_zero robot_size=%d fakeman_size=%d", *robot_size, *fakeman_size);
	}

	return ret;
}

// usage: 
// solo_t * solo = get_design_solo(solo_id);
// return NULL if not found or solo->id mismatch
solo_t * get_design_solo(int id)
{
	if (id < 0 || id > MAX_AI_EID) {
		return NULL;
	}

	solo_t * solo = g_design->design_solo_list + id;
	if (solo->id == 0) {
		return NULL;
	}

	if (solo->id != id) {
		BUG_PRINT(-7, "get_design_solo:solo_id_mismatch %d %d", solo->id, id);
		return NULL;
	}

	return solo;
}

int load_design_chapter(int *chapter_size, design_chapter_t * chapter_list, solo_t *solo_list)
{
	int ret;

	ret = db_design_load_chapter(chapter_list);
	BUG_RETURN(ret, "db_design_load_chapter_list");

	*chapter_size = 0;
	design_chapter_t * chapter;
	design_chapter_stage_t * stage;
	solo_t * solo;
	int solo_id;
	for (int i=0; i<MAX_CHAPTER; i++) {
		chapter = chapter_list + i;
		if (chapter->chapter_id == 0) {continue;};
		(*chapter_size)++;
		if (chapter->chapter_id != *chapter_size) {
			BUG_RETURN(ret, "db_design_load_chapter_list:chapter_miss %d", chapter->chapter_id);
		}

		for (int j=1; j<=chapter->stage_size; j++) {
			stage = chapter->stage_list + j;
			for (int k=0; k<stage->solo_size; k++) {
				solo_id = stage->solo_list[k];

				if (solo_id < 0 || solo_id > MAX_AI_EID) {
					ret = - (chapter->chapter_id * 100 + stage->stage_id);
					BUG_RETURN(ret
					, "db_design_load_chapter_list:solo_id_range %d "
					"chapter %d-%d", solo_id
					, chapter->chapter_id, stage->stage_id);
				}
				solo = solo_list + solo_id;
				if (solo->id == 0) {
					ret = - (chapter->chapter_id * 100 + stage->stage_id);
					BUG_RETURN(ret
					, "db_design_load_chapter_list:solo_id_invalid %d "
					"chapter %d-%d", solo_id
					, chapter->chapter_id, stage->stage_id);
				}
//				// order is important, must read design_solo first
//				solo = get_design_solo(solo_id);
//				if (solo == NULL) {
//					// e.g. 1-4 become -104
//					ret = - (chapter->chapter_id * 100 + stage->stage_id);
//					BUG_RETURN(ret, "db_design_load_chapter_list:solo_id_invalid %d chapter %d-%d", solo_id, chapter->chapter_id, stage->stage_id);
////					// BUG_RETURN(ret "db_design_load_chapter_list:solo_id_invalid stage %d-%d solo_id %d", chapter->chapter_id, stage->stage_id, solo_id);
//				}
//
			}
		}
	}

	DEBUG_PRINT(0, "load_design_chapter:chapter_size=%d", *chapter_size);

	return ret;
}

int load_design_hero(design_hero_t * hero_list)
{
	int ret;

	ret = db_design_load_hero(hero_list);
	BUG_RETURN(ret, "db_design_load_hero");

	ret = 0;

	return ret;
}

int load_design_hero_mission(design_mission_hero_t * hero_list)
{
	int ret;

	ret = db_design_load_hero_mission(hero_list);
	BUG_RETURN(ret, "db_design_load_hero_mission");

	ret = 0;

	return ret;
}

int load_design_card_chapter(design_card_chapter_t * card_chapter_list)
{
	int ret;

	ret = db_design_load_card_chapter(card_chapter_list);
	BUG_RETURN(ret, "db_design_load_card_chapter");

	ret = 0;

	return ret;
}

// order is important: must be after design_card_chapter is loaded
int calc_design_card_chapter(design_card_chapter_t * card_chapter_list
, int chapter_size, design_chapter_t * chapter_list)
{
	int chapter_id;
	int stage_id;
	int card_id;
	const char *name;

	for (int i=0; i<chapter_size; i++) {
		design_chapter_t * chapter = chapter_list + i;
		chapter_id = chapter->chapter_id;
		for (int j=0; j<chapter->stage_size; j++) {
			design_chapter_stage_t *stage = chapter->stage_list + j;
			stage_id = stage->stage_id;
			name = stage->name;

			// loop all the reward within a stage
			// when reward->reward==3 it is piece, also need to check weight
			for (int k=0; k<MAX_CHAPTER_REWARD; k++) {
				design_chapter_reward_t * reward = stage->reward_list + k;
				if (reward->reward == CHAPTER_REWARD_PIECE 
				&& reward->weight_end > reward->weight_start) {
					card_id = reward->count;
					if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
						BUG_PRINT(-2, "calc_design_card_chapter:card_id_out_bound %d (%d-%d)", card_id, chapter_id, stage_id);
						// TODO return error code
						continue;
					}
					design_card_chapter_t *card_chapter = card_chapter_list + card_id;

					card_chapter->card_id = card_id;
					int index = card_chapter->count;
					// client may have limit 3
					if (index >= MAX_CARD_CHAPTER) {
						// no error, no warning, this is not be an error
						continue;
					}

					design_piece_chapter_t *piece = card_chapter->chapter_list + index;
					// NOTE: no space should be in name
					sprintf(piece->name, "%.29s", name);
					piece->chapter_id = chapter_id;
					piece->stage_id = stage_id;

					// increase the card_chapter count
					card_chapter->count++; // must do 
					
				}
			}
		}
	}
	
	
	return 0;
}

int load_design_hero_slot(design_hero_slot_t * hero_slot_list)
{
	int ret;

	ret = db_design_load_hero_slot(hero_slot_list);
	BUG_RETURN(ret, "db_design_load_hero_slot");

	ret = 0;

	return ret;
}

int load_design_achievement(design_achi_t * achi_list)
{
	int ret;

	ret = db_design_load_achievement(achi_list);
	BUG_RETURN(ret, "db_design_load_achievement");

	ret = 0;

	return ret;
}

int load_design_daily_login(design_daily_login_t * daily_login_list)
{
	int ret;

	ret = db_design_load_daily_login(daily_login_list);
	BUG_RETURN(ret, "db_design_load_daily_login");

	ret = 0;

	return ret;
}

int load_design_arena_reward(design_arena_reward_t * reward_list, int &reward_count) {
	int ret;

	ret = db_design_load_arena_reward(reward_list, reward_count);
	BUG_RETURN(ret, "db_design_load_arena_reward");
	ret = 0;
	return ret;
}

int load_design_quick_reward(design_quick_reward_list_t * reward_list) {
	int ret;

	ret = db_design_load_quick_reward(reward_list);
	BUG_RETURN(ret, "db_design_load_quick_reward");
	ret = 0;
	return ret;
}

int load_design_chapter_dialog(design_chapter_dialog_t * dialog_list) {
	int ret;

	ret = db_design_load_chapter_dialog(dialog_list);
	BUG_RETURN(ret, "db_design_load_chapter_dialog");
	ret = 0;
	return ret;
}

#define ERROR_GOTO(ret, str)   do { ERROR_PRINT((ret), (str)); if (ret != 0) { emsg = (str); goto cleanup;} } while (0)

// do FATAL_EXIT in load_design(), donnot in load_design_xxxx()
// flag == 1; use fatal_exit
// flag == 0; not use fatal_exit
int load_design(int flag, design_t * design, connect_t * conn) {
	int ret;
	const char * emsg = "OK";
	int match_flag = 0;
	ret = db_design_init();
	ERROR_GOTO(ret, "db_design_init");


	// load shop card
	bzero(design->shop_list, sizeof(design->shop_list));
	ret = load_design_shop_card(design->shop_list);
	ERROR_GOTO(ret, "load_shop_card_error");

	bzero(design->std_deck_list, sizeof(design->std_deck_list));
	ret = load_design_std_deck(design->std_deck_list);
	ERROR_GOTO(ret, "load_std_deck_error");

	bzero(design->ai_list, sizeof(design->ai_list));
	ret = load_design_ai(design->ai_list);
	ERROR_GOTO(ret, "load_ai_error");

	bzero(design->pick_list, sizeof(design->pick_list));
	ret = load_design_pick(design->pick_list);
	ERROR_GOTO(ret, "load_pick_error");

	bzero(design->card_list, sizeof(design->card_list));
	bzero(design->star_list, sizeof(design->star_list));
	bzero(design->extract_list, sizeof(design->extract_list));
	bzero(design->hero_hp_list, sizeof(design->hero_hp_list));
	// TODO check design->star_list
	ret = load_design_card(design->card_list, design->star_list
	, design->extract_list, design->hero_hp_list);
	ERROR_GOTO(ret, "load_card_error");

	bzero(&(design->constant), sizeof(design->constant));
	ret = load_design_constant(&(design->constant));
	ERROR_GOTO(ret, "load_constant_error");

//	design->exp_list.clear();
	bzero(design->exp_list, sizeof(design->exp_list));
	ret = load_design_exp(design->exp_list, design->max_level);
	ERROR_GOTO(ret, "load_exp_error");

	bzero(design->notice_list, sizeof(design->notice_list));
	ret = load_design_notice(design->notice_list, design->notice_count);
	ERROR_GOTO(ret, "load_notice_error");

	bzero(design->guild_list, sizeof(design->guild_list));
	ret = load_design_guild(design->guild_list, design->guild_max_level);
	ERROR_GOTO(ret, "load_guild_error");

	bzero(design->merge_list, sizeof(design->merge_list));
	ret = load_design_merge(design->merge_list);
	ERROR_GOTO(ret, "load_merge_error");

	bzero(design->pay_list, sizeof(design->pay_list));
	ret = load_design_pay(design->pay_list);
	ERROR_GOTO(ret, "load_pay_error");

	bzero(design->pay_monthly_list, sizeof(design->pay_monthly_list));
	design->max_pay_monthly_list = 0;
	ret = load_design_monthly(design->pay_monthly_list
	, design->max_pay_monthly_list);
	ERROR_GOTO(ret, "load_pay_monthly_list_error");

	bzero(&(design->version), sizeof(design->version));
	ret = load_design_version(&(design->version));
	ERROR_GOTO(ret, "load_version");

	bzero(&(design->website_list), sizeof(design->website_list));
	ret = load_design_website(design->website_list);
	ERROR_GOTO(ret, "load_website");

	bzero(&(design->mission_list), sizeof(design->mission_list));
	ret = load_design_mission(design->mission_list);  
	ERROR_GOTO(ret, "load_mission");

	bzero(&(design->slot_list), sizeof(design->slot_list));
	ret = load_design_slot(design->slot_list);
	ERROR_GOTO(ret, "load_slot");

	bzero(&(design->lottery_info), sizeof(design->lottery_info));
	ret = load_design_lottery(&design->lottery_info);
	ERROR_GOTO(ret, "load_lottery");

	bzero(&(design->fight_schedule_list), sizeof(design->fight_schedule_list));
	design->max_fight_schedule = 0;
	ret = load_design_fight_schedule(design->fight_schedule_list
	, design->max_fight_schedule);

	bzero(&(design->design_gate_list), sizeof(design->design_gate_list));
	ret = load_design_gate(&design->design_gate_size, design->design_gate_list);
	ERROR_GOTO(ret, "load_gate");

	bzero(&(design->design_gate_msg_list), sizeof(design->design_gate_msg_list));
	ret = load_design_gate_msg(design->design_gate_msg_list);
	ERROR_GOTO(ret, "load_gate_msg");

	bzero(&(design->rank_reward_list), sizeof(design->rank_reward_list));
	bzero(&(design->rank_time_list), sizeof(design->rank_time_list));
	ret = load_design_rank(design->rank_reward_list, design->max_rank_reward_level, design->rank_time_list, design->max_rank_time);
	ERROR_GOTO(ret, "load_rank_reward");

	bzero(design->tower_reward_list, sizeof(design->tower_reward_list));
	ret = load_design_tower(design->tower_reward_list, design->max_tower_reward_level);
	ERROR_GOTO(ret, "load_tower_reward");

	bzero(design->piece_shop_list, sizeof(design->piece_shop_list));
	bzero(design->pshop_show_map, sizeof(design->pshop_show_map));
	bzero(design->pshop_piece_list, sizeof(design->pshop_piece_list));
	ret = load_design_piece_shop(design->piece_shop_list, &design->max_piece_shop_weight, design->pshop_show_map, &design->max_pshop_hard_show, design->pshop_piece_list);
	ERROR_GOTO(ret, "load_piece_shop");

	bzero(design->design_solo_list, sizeof(design->design_solo_list));
	ret = load_design_solo(design->design_solo_list);
	ERROR_GOTO(ret, "load_solo");

	bzero(design->design_robot_list, sizeof(design->design_robot_list));
	bzero(design->design_fakeman_list, sizeof(design->design_fakeman_list));
	ret = load_design_robot(&design->design_robot_size, design->design_robot_list, &design->design_fakeman_size, design->design_fakeman_list);
	ERROR_GOTO(ret, "load_robot");

	bzero(design->design_chapter_list, sizeof(design->design_chapter_list));
	ret = load_design_chapter(&design->design_chapter_size, design->design_chapter_list, design->design_solo_list);
	ERROR_GOTO(ret, "load_chapter");

	bzero(design->mission_hero_list, sizeof(design->mission_hero_list));
	ret = load_design_hero_mission(design->mission_hero_list);
	ERROR_GOTO(ret, "load_hero_mission");

	bzero(design->hero_list, sizeof(design->hero_list));
	ret = load_design_hero(design->hero_list);
	ERROR_GOTO(ret, "load_design_hero");

	// peter: we can extract data from design->design_chapter_list
//	bzero(design->card_chapter_list, sizeof(design->card_chapter_list));
//	ret = load_design_card_chapter(design->card_chapter_list);
//	ERROR_GOTO(ret, "load_design_card_chapter");
	bzero(design->card_chapter_list, sizeof(design->card_chapter_list));
	ret = calc_design_card_chapter(design->card_chapter_list, design->design_chapter_size, design->design_chapter_list);
	ERROR_GOTO(ret, "calc_design_card_chapter");

	bzero(design->hero_slot_list, sizeof(design->hero_slot_list));
	ret = load_design_hero_slot(design->hero_slot_list);
	ERROR_GOTO(ret, "load_design_hero_slot");

	bzero(design->achi_list, sizeof(design->achi_list));
	ret = load_design_achievement(design->achi_list);
	ERROR_GOTO(ret, "load_design_achievement");

	bzero(design->daily_login_list, sizeof(design->daily_login_list));
	ret = load_design_daily_login(design->daily_login_list);
	ERROR_GOTO(ret, "load_design_daily_login");

	bzero(design->arena_reward_list, sizeof(design->arena_reward_list));
	ret = load_design_arena_reward(design->arena_reward_list
	, design->max_arena_reward_level);
	ERROR_GOTO(ret, "load_arena_reward");

	bzero(design->quick_reward_list, sizeof(design->quick_reward_list));
	ret = load_design_quick_reward(design->quick_reward_list);
	ERROR_GOTO(ret, "load_quick_reward");

	bzero(design->chapter_dialog_list, sizeof(design->chapter_dialog_list));
	ret = load_design_chapter_dialog(design->chapter_dialog_list);
	ERROR_GOTO(ret, "load_chapter_dialog");

	// load db match
	ret = 0;
	for (int i=0; i<MAX_MATCH; i++) {
		match_t &match = g_match_list[i];
		if (match.match_id != 0) {
			match_flag = 1;
			WARN_PRINT(-6, "load_design:already_has_match_in_nio %ld"
			, match.match_id);
			break;
		}
	}
	// only load one match now
	if (match_flag == 0) {
		ret = load_db_match(g_match_list[0]);
		ERROR_GOTO(ret, "load_match");
	}

cleanup:
	db_design_clean();
	if (flag) {
		FATAL_EXIT(ret, "load_design()");
	}
	if (ret != 0 && conn != NULL) {
		net_writeln(conn, "ERROR %s", emsg);
	}
	return ret;
}

// TODO check & reference is OK?
design_mission_t design_mission_by_mid(design_t * design, int mid)
{
	return design->mission_list[mid]; // key to value

/***
	for (size_t i=0; i<design->mission_list.size(); i++) {
		design_mission_t & dmis = design->mission_list[i];
		if (dmis.mid == mid) {
			return dmis;
		}
	}

	design_mission_t dmis; // this is local, do not return reference
	bzero(&dmis, sizeof(dmis));
	dmis.mid = 0;
	return dmis;
***/
}

//////////// DB DESIGN END ] ///////////

//////////// LOAD LOCAL NAME START [ ///////////
int load_local_name(local_name_t &names, const char * file_name)
{
    char fmt[10];
	sprintf(fmt, "%%%ds", NAME_SIZE);
	int num;
	FILE *pfile;

	// name_xing
	pfile = fopen(file_name, "r");
	if (pfile == NULL) {
		BUG_RETURN(-5, "load_local_name:pfile_null");
	}

	num = 0;
	char (&name_list)[MAX_LOCAL_NAME][NAME_SIZE] = names.name_list;
	while (!feof(pfile)) {

		fscanf(pfile, fmt, name_list[num]);

		if (strlen(name_list[num]) == 0) {
			continue;
		}

		if ((name_list[num][strlen(name_list[num])-1]) == '\n'
		&& strlen(name_list[num]) == 1) {
			continue;
		}

		// DEBUG_PRINT(0, "load_local_name:name_list[%d]=%s [%lu]", num, name_list[num], strlen(name_list[num]));

		if (num + 1 >= MAX_LOCAL_NAME) {
			break;
		}

		num ++;
	}
	fclose(pfile);

	names.count = num;
	DEBUG_PRINT(0, "load_local_name:count=%d", names.count);
	if (names.count <= 0 || names.count >= MAX_LOCAL_NAME) {
		BUG_RETURN(-2, "load_local_name:count_out_bound %d", names.count);
	}

	return 0;
}
//////////// LOAD LOCAL NAME END ] ///////////

///////// GUILD START [ /////////


int init_guild()
{
	g_guild_map.clear();
	return 0;
}

// use reference
guild_t & get_guild_by_gid(int gid)
{
	guild_t & guild = g_guild_map[gid];
	if (guild.gid == 0) {
		guild_t new_guild;
		bzero(&new_guild, sizeof(guild_t));
		new_guild.gid = gid;
		g_guild_map[gid] = new_guild;
	}
	return g_guild_map[gid];
}

int guild_add_member(guild_t & guild, int eid)
{
	if (guild.num_member >= MAX_GUILD_MEMBER) {
		ERROR_RETURN(-2, "guild_add_member:num_member_over_flow %d"
		, guild.num_member);
	}
	for (int i=0; i<guild.num_member; i++) {
		if (guild.member[i] == eid)  {
			ERROR_RETURN(-22, "guild_add_member:dup_eid %d", eid);
		}
	}

	guild.member[guild.num_member] = eid;
	guild.num_member++;
	return 0;
}


// global access : beware, guild may be invalid after this call!!
// assume caller use global g_guild_map to get the guild ref
int guild_del_member(guild_t & guild, int eid)
{
	for (int i=0; i<guild.num_member; i++) {
		if (guild.member[i] == eid)  {
			for (int j=i; j<guild.num_member-1; j++) {
				guild.member[j] = guild.member[j+1];
			}
			guild.num_member = guild.num_member - 1;
			// TODO remove guild from g_guild_map
			if (guild.num_member==0) {
				int gid = guild.gid;
				// printf("----- erase guild.gid=%d  guild=%p\n", gid, &guild);
				g_guild_map.erase(gid); // dangerous!
			}
			return guild.num_member;
		}
	}

	ERROR_RETURN(-3, "guild_del_member:eid_not_found %d", eid);
	return -3;
}


// all logic should call gid_add/del_eid
int gid_add_eid(int gid, int eid)
{
	if (gid <= 0) {
		return 0;
	}
	guild_t & guild = get_guild_by_gid(gid);
	return guild_add_member(guild, eid);
}

int gid_del_eid(int gid, int eid)
{
	if (gid <= 0) {
		return 0;
	}
	guild_t & guild = get_guild_by_gid(gid);
	return guild_del_member(guild, eid);
}


int guild_clear(int gid)
{
	guild_t & guild = get_guild_by_gid(gid);
	connect_t *member_conn;

	// XXX apply cannot get this msg
	for (int i=0; i<guild.num_member; i++) {
		int eid = guild.member[i];
		if (eid > 0) {
			member_conn = get_conn_by_eid(eid);	
			if (member_conn != NULL) {
				DEBUG_PRINT(0, "member_conn eid=%d alias=%s"
				, member_conn->euser.eid, member_conn->euser.alias);
				member_conn->euser.gid = 0;
				member_conn->euser.gpos = 0;
				strcpy(member_conn->euser.gname, "_no_guild");
				net_writeln(member_conn, "dguild %d", gid);
			}
		}
	}

	if (guild.gid == gid) {
		guild.gid = 0;
		guild.num_member = 0;
	}
	// global access
	g_guild_map.erase(gid); // dangerous!
	return 0;
}


// for debug
int print_guild_member(guild_t & guild)
{
	printf("guild [gid=%d] total=%d : ", guild.gid, guild.num_member);
	for (int i=0; i<guild.num_member; i++) {
		printf("  %d", guild.member[i]);
	}
	printf("\n");
	return 0;
}

int guild_clean(connect_t * conn) 
{
	int eid = conn->euser.eid;
	if (eid <= 0) {
		// WARN_PRINT(-3, "guild_clean:eid_negative %d", eid);
		return 0;
	}
	int gid = conn->euser.gid;
	int gpos = conn->euser.gpos;
	if (gpos > 0 && gpos != GUILD_POS_APPLY) {
		return gid_del_eid(gid, eid);
	} else {
		return 0;
	}
}

int broadcast_guild(guild_t &guild, int except_eid, const char *str)
{
	int count = 0;
	for (int i=0; i<guild.num_member; i++) {
		int eid = guild.member[i];
		// peter: add <= 20 for ai 
		if (eid == except_eid) {
			continue;  // skip except_eid
		}
		connect_t*  guest_conn = get_conn_by_eid(eid);
		if (guest_conn == NULL) {
			continue; // skip offline 
		}
		net_write(guest_conn, str, '\n');
		count ++;
	}
	return count;
}

// CMD: gchat [msg]
// RET: gchat -2
// BCT: gchat [eid] [alias] [msg]
int guild_chat(connect_t *conn, const char *cmd, const char * buffer) 
{
	int gid;
	int eid;
	int gpos;

	gid = conn->euser.gid;
	eid = conn->euser.eid;
	gpos = conn->euser.gpos;
	
	if (gid <= 0 || gpos == GUILD_POS_APPLY) {
		NET_ERROR_RETURN(conn, -6, E_GUILD_CHAT_NOT_IN_GUILD);
	}

	guild_t & guild = get_guild_by_gid(gid);

	if (guild.gid != gid) {
		// NET_ERROR_RETURN(conn, -7, "gchat:guild_gid_mismatch %d %d"
		// , guild.gid, gid);
		NET_ERROR_RETURN(conn, -7, "%s %d %d", E_GUILD_CHAT_NOT_IN_GUILD
		, guild.gid, gid);
	}

	int len;
	len = strlen(buffer); // strnlen(buffer, 100); // mac 10.6 not work
	// bounded
	if (len > 100) { 
		// NET_ERROR_RETURN(conn, -2, "msg_too_long");
		NET_ERROR_RETURN(conn, -2, "%s", E_GUILD_CHAT_MSG_TOO_LONG);
	}
	char str[100+EVIL_ALIAS_MAX+15]; // for gchat [eid] [alias] [msg]
	const char * alias = conn->euser.alias;
	if (strlen(alias)==0) {
		alias = VISITOR_ALIAS;
	}
	
	// peter: remove len parameter
	// len = sprintf(str, "%s %d %s %s", cmd, eid, alias, buffer);
	int vip_flag = check_vip(conn);
	len = chat_format(str, cmd, eid, alias, vip_flag, 0, buffer);
	// send to myself
	// net_writeln(conn, str);
	broadcast_guild(guild, 0, str);
	return 0;  // do not return count!!! it is level
}

///////// GUILD END ] /////////

///////// ROOM MALLOC START /////////
int init_room_list() 
{
	for (int c=0; c<MAX_CHANNEL; c++) {
		g_free_room[c] = 1;  // free_room start from 1
		g_used_room[c] = -1; // no room is used

		// room[] is using base 1 (skip room 1)
		for (int i=0; i<=MAX_ROOM; i++) {
			bzero(g_room_list[c]+i, sizeof(room_t));
			g_room_list[c][i].rid = i;
			g_room_list[c][i].next_free = i + 1; // note: last room is different
			g_room_list[c][i].next_used = -1;
			g_room_list[c][i].prev_used = -1;
			g_room_list[c][i].channel = c;
		}
		g_room_list[c][0].state = 999;  // special handling avoid using room[0]
		g_room_list[c][MAX_ROOM].next_free = -1; // last room next_free=-1
	}
	return 0;
}

// new_room() = new alloc room
// usage:
// room_t * proom = new_room(channel);
// if (NULL == proom ) {  error handling early exit }
// -- use proom, 
// e.g. proom->num_guest=2;  proom->guest[0]=myid; proom->guest[1]=your_id;
//      conn->proom = proom;  // save in connect_t
room_t * new_room(int channel)
{
	int index;
	if (channel < 0 || channel >= MAX_CHANNEL) {
		ERROR_PRINT(-2, "new_room:channel_overflow %d", channel);
		return NULL;
	}
	index = g_free_room[channel];

	// room index is base 1 to MAX_ROOM
	if (index < 1 || index > MAX_ROOM) {
		// this is unlikely to happen
		WARN_PRINT(-22, "new_room:channel_full %d", channel);
		return NULL;
	}

	room_t *proom = &(g_room_list[channel][index]);
	if (proom->state != 0) {
		// this is usually BUG
		BUG_PRINT(-7, "new_room:room_state %d", proom->state);
		return NULL;
	}

	if (proom->prev_used != -1 || proom->next_used != -1) {
		BUG_PRINT(-7, "new_room:prev_next_used %d %d"
			, proom->prev_used, proom->next_used);
		return NULL;
	}

	// handle free list
	g_free_room[channel] = proom->next_free;

	// handle use list
	if (g_used_room[channel] >= 0) {
		// g_used_room become the second room in used_list
		g_room_list[channel][g_used_room[channel]].prev_used = index;
	}
	proom->next_used = g_used_room[channel];
	g_used_room[channel] = index;


	// initialize the room (optional)
	proom->state = ST_ROOM; // either 0, ST_ROOM, ST_GAME
	proom->num_guest = 0; 
	proom->gameid = get_gameid(); //get_usec();
	proom->title[0] = 'x';  // for debug
	proom->title[1] = '\0';

	proom->password[0] = '\0';

	proom->create_time = time(NULL);

	// avoid type_list empty in save/load replay
	sprintf(proom->type_list, "0");

	// for debug
	// INFO_PRINT(0, "new_room(%d,%d)", channel, proom->rid);

	return proom;
}

int do_room_add_eid(room_t *proom, int eid, const char * name);

room_t * create_match_room(match_t & match, int eid1, int eid2, const char * alias1, const char * alias2)
{
	int ret;
	room_t *proom = NULL;
	proom = new_room(CHANNEL_MATCH);
	if (proom==NULL) {
		ERROR_PRINT(-7, "create_match_room:bug");
		return NULL;
	}

	proom->match_id = match.match_id;
	proom->match_eid[0] = eid1;
	proom->match_eid[1] = eid2;
	proom->game_type = GAME_MATCH;

	// add player to match_room, guset[0] must be non-ai player 
	if (eid1 <= MAX_AI_EID) {
		ret = do_room_add_eid(proom, eid2, alias2);
		if (ret < 0) {
			ERROR_PRINT(-6, "create_match_room:eid2 %d", eid2);
			return NULL;
			// XXX delete room?
		}

		ret = do_room_add_eid(proom, eid1, alias1);
		if (ret < 0) {
			ERROR_PRINT(-16, "create_match_room:eid1 %d", eid1);
			return NULL;
			// XXX delete room?
		}
	} else {
		ret = do_room_add_eid(proom, eid1, alias1);
		if (ret < 0) {
			ERROR_PRINT(-26, "create_match_room:eid1 %d", eid1);
			return NULL;
			// XXX delete room?
		}

		ret = do_room_add_eid(proom, eid2, alias2);
		if (ret < 0) {
			ERROR_PRINT(-36, "create_match_room:eid2 %d", eid2);
			return NULL;
			// XXX delete room?
		}
	}

	// start game
	sprintf(proom->title, "%s~VS~%s", alias1, alias2);

	return proom;
}

room_t * get_room_by_rid(int channel, int rid)
{
	if (rid < 1 || rid > MAX_ROOM) { // room id is base 1
		return NULL;
	}
	return g_room_list[channel] + rid;
}


int free_room(int channel, room_t * proom) 
{
	if (proom == NULL) {
		ERROR_NEG_RETURN(-3, "free_room:proom_null");
		return -3;
	}

	if (channel < 0 || channel >= MAX_CHANNEL) {
		ERROR_NEG_RETURN(-2, "free_room:channel_overflow %d", channel);
		return -2;
	}
	if (proom->state==0 || proom->state==999) {
		ERROR_NEG_RETURN(-7, "free_room:non_free_state %d\n", proom->state);
		return -7;
	}

	if (NULL != proom->lua) {
		LU_CHECK_MAGIC(proom->lua);
		// TODO we may need to save the result to database
		lua_pop(proom->lua, 1);  // pop the magic cookie
		lua_close(proom->lua);  // clean up lua VM
		proom->lua = NULL;  
	}

	// clean up user:
	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i]; 
		g_user_room[eid] = NULL;
		connect_t * guest_conn = get_conn_by_eid(eid);
		// st and room
		if (guest_conn != NULL) {
			guest_conn->room = NULL;
			if (guest_conn->st >= ST_ROOM) {
				guest_conn->st = ST_LOGIN;
			}
		}
	}


	int index;
	index = proom - g_room_list[channel];  // this is rid
	//DEBUG_PRINT(index, "free_room:index  channel=%d  state=%d"
	// , proom->channel, proom->state);
	if (index < 0 || index > MAX_ROOM) {
		ERROR_NEG_RETURN(-22, "free_room:index_outbound %d\n", index);
		return -22;
	}

	// init g_room_list and g_free_room
	int saved_next_free = g_free_room[channel];
	// proom->next_free = g_free_room[channel]; // core logic see below
	g_free_room[channel] = index;

	// g_used_room ->   last_used_room.next_used -> last2_used_room
	if (g_used_room[channel] == index) {
		g_used_room[channel] = proom->next_used;
	}
	if (proom->prev_used >= 0) {
		g_room_list[channel][proom->prev_used].next_used = proom->next_used;
	}
	if (proom->next_used >= 0) {
		g_room_list[channel][proom->next_used].prev_used = proom->prev_used;
	}

	// after bzero, we need to init proom->rid, channel, prev_used
	// , next_used, next_free (from above)
	proom->cmd_list.clear();  // bzero with a vector, may hv problem!
	bzero(proom, sizeof(room_t));  // clean up proom
	proom->state = 0;
	proom->password[0] = '\0';
	proom->offline_time[0] = 0;
	proom->offline_time[1] = 0;
	proom->rid = index; // 
	proom->channel = channel; 
	proom->prev_used = -1;
	proom->next_used = -1;
	proom->num_guest = 0;
	proom->next_free = saved_next_free; // core logic!
	return 0;
}


int print_room_list(int channel) 
{
	int index;

	printf("g_free_room[%d]=%d: ", channel, g_free_room[channel]);
	index = g_free_room[channel];
	while (index >= 1 && index <= MAX_ROOM ) {
		room_t * proom = g_room_list[channel] + index;
		printf("  %d(next_free=%d, s%d)", index
		, proom->next_free, proom->state);
		index = proom->next_free;
	}
	printf("\n");

	printf("g_used_room[%d]=%d : ", channel, g_used_room[channel]);
	index = g_used_room[channel];
	while (index >= 1 && index <= MAX_ROOM ) {
		room_t * proom = g_room_list[channel] + index;
		printf("  %d(prev=%d,next=%d, s%d)", index
		, proom->prev_used, proom->next_used, proom->state);
		index = proom->next_used;
	}
	printf("\n");

	return 0;
}

///////// ROOM MALLOC END /////////



////////// ROOM START /////////

// return a string represent the guest list
// @param buffer   output param
// @param proom    input room_t
int str_room_info(char *buffer, room_t * proom) 
{
	char * ptr;
	int n;
	ptr = buffer;
	const char *pwd;
	if ( proom->password[0]=='\0' ) {
		pwd = "_";
	} else {
		pwd = proom->password;
	}
	n = sprintf(ptr, "%d %d %d %s %d", proom->channel, proom->rid, proom->state, pwd, proom->num_guest);
	ptr += n;  // assume n >= 0

	for( int i = 0; i < proom->num_guest; i++){
		int eid = proom->guest[i];
		connect_t * guest_conn;
		// g_user_index[eid] -> tid
		guest_conn = get_conn_by_eid(eid);
		
		if ( NULL == guest_conn && eid > MAX_AI_EID){
			// net_write_space(conn, "%d _offline_", eid);
			// TODO use [alias]_off_ as offline alias (need search euser)
			// n = sprintf(ptr, " %d %s_off_", eid, proom->alias[i]);
			n = sprintf(ptr, " %d %s_off_ %d", eid, proom->alias[i]
			, proom->icon[i]);
			ptr += n;  // assume n >= 0
			continue;
		}

		const char * alias = proom->alias[i];
		if ( strlen(alias) == 0) {
			BUG_PRINT(-7, "empty_alias %d", eid);
			alias = VISITOR_ALIAS; // "_alias_";
		}

		// net_write_space(conn, "%d %s", eid, alias);
		// n = sprintf(ptr, " %d %s", eid, alias);
		n = sprintf(ptr, " %d %s %d", eid, alias, proom->icon[i]);
		ptr += n;  // assume n >= 0

	}// end for num_guest

	ptr += sprintf(ptr, " %d", proom->game_type);

	if (proom->game_type == GAME_CHAPTER) {
		solo_t * solo = get_design_solo(proom->guest[1]);
		ptr += sprintf(ptr, " %d", MAX_SOLO_TARGET);
		solo_target_t *target = NULL;
		for (int i=0; i<MAX_SOLO_TARGET; i++) {
			target = solo->target_list + i;
			ptr += sprintf(ptr, " %d %d %d", target->target, target->p1, target->p2);
		}
	}


	// n = sprintf(ptr, " %d %s", eid, alias);
	// ERROR_NEG_PRINT(n, "str_guest_list444");
	// ptr += n;
	// no need to add enter!
	// net_write(conn, "", '\n');
	return ptr - buffer;   // how many bytes written
}

// room [channel]  - create room in [channel]
// if channel is not specified, it is default to 1
// RET:  room [channel] [roomid]
// TODO forbid create room in channel quick
// 
int room_create(connect_t* conn, const char *cmd, const char * buffer)
{
	int ret;
	int channel = CHANNEL_DEFAULT; // TODO use conn->euser.channel 
	// avoid memory leak
	if (conn->room != NULL) {
		net_writeln(conn, "%s -6 ERROR already_has_room", cmd);
		return -6; // logic error, already has room
	}
	channel = strtol_safe(buffer, channel);
	// TODO if channel == CHANNEL_QUICK : error
	// channel id is base 0,  room id is base 1
	if (channel < 0 || channel >= MAX_CHANNEL) {
		NET_ERROR_RETURN(conn, -2, "room_create:channel outbound %d", channel);
	}

	int eid = conn->euser.eid; 
	// add to g_user_room, for re-conn
	if ( g_user_room[eid] != NULL) {
		NET_ERROR_RETURN(conn, -17, "room_create:g_user_room[%d] not 0", eid);
	}

	// the eid is inside quick list
	if (quick_check(eid) != 0) {
		NET_ERROR_RETURN(conn, -16, "in_quick_list");
	}

	room_t * proom = NULL;
	// @see room_clean
	proom = new_room(channel);
	// NEW(room_t, 1);  // @see room_clean for free()
	if (proom==NULL) {
		ret = -7;
		NET_ERROR_RETURN(conn, ret, "room_create:memory");
		// usually memory error is impossible
	}
	// implicit: proom != NULL
	proom->gameid = get_gameid();
	proom->guest[0] = eid;  // euser.eid
	proom->num_guest = 1;
	proom->state = ST_ROOM;
	strcpy(proom->title, conn->euser.alias); // room title = master alias
	strcat(proom->title, "_room");  // @see room_t  title EVIL_ALIAS_MAX + 10
	conn->room = proom;

	// re-conn logic
	g_user_room[eid] = proom;
	
	char str[50];
	sprintf(str, "%d %d %d channel room state", channel, proom->rid
	, proom->state);
	net_write_space(conn, cmd);
	net_writeln(conn, str);

	INFO_PRINT(0, "room_title=%s(%d)", proom->title, proom->guest[0] );
	return ST_ROOM;
}  // end: room_create 


int do_room_del(room_t *proom, connect_t *conn, int eid)
{
	assert(proom != NULL);
	// int eid = conn->euser.eid; // no check on null?
	int index = -1;
	for (int i=0; i<proom->num_guest; i++) {
		if (proom->guest[i] == eid)  {
			index = i;  // index is where myeid exists
			break;
		}
	}

	if (-1 == index) {
		return -3; // not found
	}

	// core logic to shift the rest of guest to the front
	for (int i=index; i<(proom->num_guest-1); i++) {
		proom->guest[i] = proom->guest[i+1];		// shift eid
		strcpy(proom->alias[i], proom->alias[i+1]);  // shift alias
		proom->icon[i] = proom->icon[i+1];
		proom->lv[i] = proom->lv[i+1];
	}
	proom->num_guest --;
	proom->guest[ proom->num_guest ] = 0; // zero it
	
	if (proom->num_guest == 1) { // resume title
		sprintf(proom->title, "%s_room", proom->alias[0]);  
	}


	// other clean up, g_user_room, conn
	// re-conn logic
	g_user_room.erase(eid);
	
	// in case of offline, (kick), conn is null, normal
	if (NULL != conn) {
		conn->st = ST_LOGIN;
		conn->room = NULL;
	}
	return 0;
}

int do_room_add_eid(room_t *proom, int eid, const char * name)
{
	assert(NULL != proom); // caller must ensure non-null

	if ( proom->num_guest >= MAX_GUEST ) {
		return -2;
	}

	if (eid <= 0) {
		return -16;
	}
	// TODO euser add to proom
	// re-conn logic
	proom->guest[ proom->num_guest ] = eid;  // euser.eid
	const char * alias = name;
	if (strlen(alias)==0) {
		alias = VISITOR_ALIAS;
	}
	strcpy(proom->alias[ proom->num_guest ], alias);
	// TODO
	proom->icon[ proom->num_guest] = 0;
	proom->lv[ proom->num_guest] = 1;
	// printf("do_room_add:icon=%d lv=%d\n"
	// , proom->icon[proom->num_guest]
	// , proom->lv[proom->num_guest]);
	if (proom->num_guest == 0) {
		sprintf(proom->title, "%s_room", proom->alias[0]);
	}
	if (proom->num_guest == 1) {
		sprintf(proom->title, "%s_v_%s"
		, proom->alias[0], proom->alias[1]);
	}
	proom->num_guest ++;

	// fill up "conn" 
	connect_t * conn;
	conn = get_conn_by_eid(eid);
	if (conn != NULL) {
		conn->room = proom;
		conn->st = proom->state;
	}
	// re-conn logic
	if (eid > MAX_AI_EID) {
		g_user_room[eid] = proom;
	}
	return 0;
}

int do_room_add(room_t *proom, connect_t * conn)
{
	assert(NULL != proom); // caller must ensure non-null

	if ( proom->num_guest >= MAX_GUEST ) {
		return -2;
	}
	if (conn->room != NULL) {
		return -6;
	}

	int eid = conn->euser.eid;
	if (eid <= 0) {
		return -16;
	}
	// TODO euser add to proom
	// re-conn logic
	proom->guest[ proom->num_guest ] = eid;  // euser.eid
	const char * alias = conn->euser.alias;
	if (strlen(alias)==0) {
		alias = VISITOR_ALIAS;
	}
	strcpy(proom->alias[ proom->num_guest ], alias);
	proom->icon[ proom->num_guest] = conn->euser.icon;
	proom->lv[ proom->num_guest] = conn->euser.lv;
	// printf("do_room_add:icon=%d lv=%d\n"
	// , proom->icon[proom->num_guest]
	// , proom->lv[proom->num_guest]);
	if (proom->num_guest == 0) {
		sprintf(proom->title, "%s_room", proom->alias[0]);
	}
	if (proom->num_guest == 1) {
		sprintf(proom->title, "%s_v_%s"
		, proom->alias[0], proom->alias[1]);
	}
	proom->num_guest ++;

	// fill up "conn" 
	conn->room = proom;
	conn->st = proom->state;
	// re-conn logic
	g_user_room[eid] = proom;
	return 0;
}



// first conn is one who broadcast, should be the game master
// @see room_game()
//  CMD: game
//  RET: game [side] [cmd_size] [seed] [timeout] [game_type]
//			  [hp1] [hp2] [energy1] [energy2]
//  		  [card_len1] [deck1] [card_len2] [deck2]
//  if card_len == 0, means deck is deck400
//  if card_len > 0, means deck is list deck [1 22 23 35 ...]
int game_info_broadcast(connect_t *conn, room_t *proom, int except_eid)
{
	DEBUG_PRINT(0, "game_info_broadcast");
	char deckbuff[1000]; // 400 + 400 + something

	// game [side] [cmd_size] [seed] [deck_400_0] [deck_400_1]

	if (proom->deck[0] == '\0' || proom->deck[1] == '\0') {
		BUG_PRINT(-37, "game_info_braodcast:null_deck");
		return -37;
	}

	// we remove "game [side], and start with cmd_size = 0 for game start
	// TODO force-next: replace proom->start_side to proom->game_timeout
	long timeout = proom->game_timeout - time(NULL);
	if (timeout < 0) {
		timeout = 0;
	}

	int card_len1 = 0;
	int card_len2 = 0;
	if (strlen(proom->deck[0]) != EVIL_CARD_MAX) {
		card_len1 = count_card(proom->deck[0]);
	}
	if (strlen(proom->deck[1]) != EVIL_CARD_MAX) {
		card_len2 = count_card(proom->deck[1]);
	}

	sprintf(deckbuff, "%d %d %ld %d %d %d %d %d %d %d %s %d %s"
	, 0, proom->seed, timeout, proom->game_type, proom->auto_battle
	, proom->hp1, proom->hp2, proom->energy1, proom->energy2
	, card_len1, proom->deck[0], card_len2, proom->deck[1]);


	// 1. send to room master
	if (conn != NULL) {
		net_write(conn, "game 1", ' ');
		net_write(conn, deckbuff, '\n'); // faster than net_writeln
	}

	// 2. send to guest[1]  (player eid2)
	connect_t * oppo_conn = get_conn_by_eid( proom->guest[1] ) ;
	if (oppo_conn != NULL) {
		oppo_conn->st = ST_GAME;
		net_write(oppo_conn, "game 2", ' ');
		net_write(oppo_conn, deckbuff, '\n'); // faster than net_writeln
	}

	for (int i=2; i<proom->num_guest; i++) {
		int guest_eid = proom->guest[i];
		// peter: add <= 20 for ai 
		if (guest_eid == except_eid || guest_eid <= MAX_AI_EID) {
			continue;  // skip AI eid 
		}
		connect_t*  guest_conn = get_conn_by_eid(guest_eid);
		if (guest_conn == NULL) {
			WARN_PRINT(-1, "guest(eid=%d) is offline", guest_eid);
			continue; // skip offline 
		}
		if (guest_conn->room != proom) {
			BUG_PRINT(-7, "guest_room_mismatch");
			continue;  // this is buggy
		}
		net_write(guest_conn, "game 99", ' ');
		net_write(guest_conn, deckbuff, '\n'); // faster than net_writeln
		guest_conn->st = ST_GAME;
	}
	return 0;
}

// broadcast room information, using lguest protocol, 
// consider: rename lguest -> room, and use croom as room_create() 
int room_info_broadcast(room_t *proom, int except_eid)
{
	assert(proom != NULL);  // TODO if check
	assert(proom->num_guest >= 2);
	assert(proom->state > 0);  // important, caller should check

	char line[SMALL_BUFFER_SIZE + 1];
	char *ptr;
	int n = sprintf(line, "room "); 
	ptr = line + n;
	str_room_info(ptr, proom);

	int ret = broadcast_room(proom, except_eid, line);
	if (ret < 0) {
		BUG_PRINT(-7, "room_info_broadcast ret=%d num_guest=%d  eid1=%d  eid2=%d  except=%d", ret, proom->num_guest, proom->guest[0], proom->guest[1], except_eid);
	}
	return 0;
}

// peter: consider a design: 
// pwd is always an non-empty parameter, "_" means no password
// parameters become: [pwd] [channel] [rid]
// where no param means list my room info
// 1 param is invalid
// 2 params (pwd channel) means create a room in a channel
//   note: if pwd=="_" then it is no password (client avoid _ input)
// 3 params : join a room (channel, rid) with password, if pwd != "_"
// 
// new room_cmd()  replace room_create(), room_join()
// room [channel] [rid]
// 1 list guest of my room (if has_room)
// 2 if only "channel" param is supplied, means create a room in "channel"
// 3 both [channel] and [rid] are there, means join that room
// 4 [channel] [rid] [pwd]:
// if rid = 0, means create a room in [channel] with password [pwd]
// if rid > 0 && rid < MAX_ROOM, means join a room with password [pwd]
int cmd_room(connect_t* conn, const char *cmd, const char * buffer)
{
	int rid = 0;
	int ret;
	int channel = CHANNEL_DEFAULT;
	char line[SMALL_BUFFER_SIZE];
	char pwd[ROOM_PASSWORD_MAX+2] = {'\0'};  // init as \0, %11s need 12 
	room_t *proom;
	// %11s => ROOM_PASSWORD_MAX+1
	ret = sscanf(buffer, "%d %d %11s", &channel, &rid, pwd);

	// case 1:  (-1 means EOF, which is valid when buffer[0]=0)
	// no param : list my room
	if (ret <= 0) {
		if (NULL == conn->room) {
			// peter: we can send null room to client but not log err in server
			NET_ERROR_RETURN(conn, -3, "%s", E_CMD_ROOM_NULL_ROOM);
			return -3;
		}

		// TODO add room lock in list
		str_room_info(line, conn->room);
		net_write_space(conn, cmd);
		net_write(conn, line, '\n');
		return ret;  // usually memory error is impossible
	}

	// implicit: ret==1   peter: enable watcher in quick channel
	if (channel < 0 || channel >= MAX_CHANNEL) {
		NET_ERROR_RETURN(conn, -2, "room_channel_outbound %d", channel);
	}


	// 2 only [channel] param is supplied, means create a room in channel
	if (ret == 1) {
		if (NULL != conn->room) {
			NET_ERROR_RETURN(conn, -6, "%s", E_CMD_ROOM_HAS_ROOM);
		}

		if (channel == CHANNEL_MATCH)
		{
			NET_ERROR_RETURN(conn, -46
			, "cmd_room:can_not_create_match_room");
		}

		proom = new_room(channel);
		if (proom==NULL) {
			NET_ERROR_RETURN(conn, -7, "new_room");
		}

		ret = do_room_add(proom, conn);
		if (ret < 0) {
			NET_ERROR_RETURN(conn, -17, "new_room_add_2 %d", ret);
		}
		str_room_info(line, proom);
		net_write_space(conn, cmd);
		net_write(conn, line, '\n');
		// INFO_PRINT(0, "room_title=%s(%d)", proom->title, proom->guest[0] );
		return 0;  // usually memory error is impossible
	}


	// ret == 2
	if (ret == 2) {
		// room id is base 1
		if (rid < 1 || rid > MAX_ROOM) {
			NET_ERROR_RETURN(conn, -12, "%s %d", E_CMD_ROOM_NO_SUCH_ROOM, rid);
		}

		// 3 both [channel] and [rid] ready, means join that room
		if (NULL != conn->room) {
			NET_ERROR_RETURN(conn, -16, "%s %d", E_CMD_ROOM_HAS_ROOM, rid);
		}

		proom = get_room_by_rid(channel, rid);
		if (NULL == proom) {
			// this is actually impossible
			NET_ERROR_RETURN(conn, -3, "%s", E_CMD_ROOM_NO_SUCH_ROOM);
		}

		if (0 == proom->state) {
			NET_ERROR_RETURN(conn, -13, "%s", E_CMD_ROOM_NO_SUCH_ROOM);
		}

		// only match has started, guest could enter the room
		if (proom->channel == CHANNEL_MATCH && proom->lua == NULL) {
			NET_ERROR_RETURN(conn, -46
			, "cmd_room:match_game_not_started");
		}

		if (proom->num_guest >= MAX_GUEST) {
			// NET_ERROR_RETURN(conn, -22, "room_max_guest");
			NET_ERROR_RETURN(conn, -22, "%s", E_CMD_ROOM_MAX_GUEST);
		}

		if (proom->password[0] != '\0') {
			// NET_ERROR_RETURN(conn, -26, "room_has_password");
			NET_ERROR_RETURN(conn, -26, "%s", E_CMD_ROOM_HAS_PASSWORD);
		}


		ret = do_room_add(proom, conn); // will do: guest[x]=eid, g_user_room
		if (ret < 0) {
			NET_ERROR_RETURN(conn, -36, "room_add %d", ret);
		}

		// finally return the room info string
		str_room_info(line, conn->room);
		net_write_space(conn, cmd);
		net_write(conn, line, '\n');
		
		room_info_broadcast(proom, conn->euser.eid);
		return 0;
	}

	// [channel] [rid] [pwd]
	if (ret == 3) {
		if (NULL != conn->room) {
			// NET_ERROR_RETURN(conn, -6, "room_has_room");
			NET_ERROR_RETURN(conn, -6, "%s", E_CMD_ROOM_HAS_ROOM);
		}

		// room id is base 1, rid == 0 means creat a room
		if (rid < 0 || rid > MAX_ROOM) {
			// NET_ERROR_RETURN(conn, -12, "room_id_outbound %d", rid);
			NET_ERROR_RETURN(conn, -12, "%s %d", E_CMD_ROOM_NO_SUCH_ROOM, rid);
		}

		if (pwd[0] == '\0' || pwd[0] == '_') {
			// NET_ERROR_RETURN(conn, -13, "room_pwd_error");
			NET_ERROR_RETURN(conn, -13, "%s", E_CMD_ROOM_PASSWORD_ERROR);
		}

		if (strlen(pwd) > 10) {
			// NET_ERROR_RETURN(conn, -22, "room_password_outbound %s", pwd);
			NET_ERROR_RETURN(conn, -22, "%s %s", E_CMD_ROOM_PASSWORD_OUT_BOUND, pwd);
		}


		// create a room with password
		if (rid == 0) {
			if (channel == CHANNEL_MATCH)
			{
				NET_ERROR_RETURN(conn, -46
				, "cmd_room:can_not_create_match_room");
			}

			proom = new_room(channel);
			if (proom==NULL) {
				NET_ERROR_RETURN(conn, -7, "new_room");
			}
			// TODO set in new_room
			sprintf(proom->password, "%.10s", pwd);
			// printf("room_password = %s\n", proom->password);
			ret = do_room_add(proom, conn);
			if (ret < 0) {
				NET_ERROR_RETURN(conn, -17, "new_room_add_2 %d", ret);
			}
			str_room_info(line, proom);
			net_write_space(conn, cmd);
			net_write(conn, line, '\n');
			INFO_PRINT(0, "room_title=%s(%d) pass=%s", proom->title, proom->guest[0], proom->password );

			return 0;
		}


		// join a room with password
		proom = get_room_by_rid(channel, rid);
		if (NULL == proom) {
			// this is actually impossible
			// NET_ERROR_RETURN(conn, -3, "room_null");
			NET_ERROR_RETURN(conn, -3, "%s", E_CMD_ROOM_NO_SUCH_ROOM);
		}

		if (0 == proom->state) {
			NET_ERROR_RETURN(conn, -13, "room_join_state_0");
		}

		// only match has started, guest can enter the room
		if (proom->channel == CHANNEL_MATCH && proom->lua == NULL) {
			NET_ERROR_RETURN(conn, -46, "cmd_room:match_game_not_started");
		}

		if (proom->num_guest >= MAX_GUEST) {
			// NET_ERROR_RETURN(conn, -22, "room_max_guest");
			NET_ERROR_RETURN(conn, -22, "%s", E_CMD_ROOM_MAX_GUEST);
		}


		if (proom->password[0] == '\0') {
			// NET_ERROR_RETURN(conn, -23, "room_no_password");
			NET_ERROR_RETURN(conn, -23, "%s", E_CMD_ROOM_PASSWORD_ERROR);
		}

		if (strlen(proom->password) != strlen(pwd)) {
			// NET_ERROR_RETURN(conn, -5, "room_password_mismatch");
			NET_ERROR_RETURN(conn, -5, "%s", E_CMD_ROOM_PASSWORD_INCORRECT);
		}

		for (int i=0;i<(int)strlen(proom->password);i++) {
			if (proom->password[i] != pwd[i]) {
				// NET_ERROR_RETURN(conn, -15, "room_password_mismatch");
				NET_ERROR_RETURN(conn, -15, "%s", E_CMD_ROOM_PASSWORD_INCORRECT);
			}
		}

		ret = do_room_add(proom, conn); // will do: guest[x]=eid, g_user_room
		if (ret < 0) {
			NET_ERROR_RETURN(conn, -36, "room_add %d", ret);
		}

		// finally return the room info string
		str_room_info(line, conn->room);
		net_write_space(conn, cmd);
		net_write(conn, line, '\n');
		
		room_info_broadcast(proom, conn->euser.eid);


		return 0;
	}

	return 0;
}



// in : join [channel] [room_id]  
// out : join [channel] [room_id]  [guest.id guest.alias] [guest.id guest.alias]..
// @see guest_list  same output protocol
int room_join(connect_t* conn, const char *cmd, const char * buffer)
{
	if (conn==NULL || conn->state==STATE_FREE) {  // this is impossible...
		ERROR_PRINT(-7, "join:conn_null_or_free");
		return -7;
	}
	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "join:has_room");
	}
	int rid = -1; // room id
	int channel = 0; // TODO from euser.channel
	if (2 != sscanf(buffer, "%d %d", &channel, &rid)) {
		NET_ERROR_RETURN(conn, -5, "join:invalid_input");
	}
	// add to g_user_room, for re-conn
	int eid = conn->euser.eid;		
	if ( g_user_room[eid] != NULL) {
		// this is rather BUG
		NET_ERROR_RETURN(conn, -17, "join:g_user_room_not_0 %d", eid);
	}

	room_t * proom = get_room_by_rid(channel, rid);
	if (proom == NULL) {
		NET_ERROR_RETURN(conn, -2, "join:get_room_by_id(%d,%d)"
			, channel, rid);
	}

	// this is normal, because get_room_by_rid() does not check validity of room
	if (proom->state == 0) {
		// room is free'ed
		NET_ERROR_RETURN(conn, -13, "join:invalid_room %d %d state %d"
		, channel, rid, proom->state);
	}

	// TODO connect_t.euser = &(g_euser_list[x])
	// check master exists?
	// ret = g_user_index[eid];  // ret == 0 may represent error
		

	if (proom->num_guest >= MAX_GUEST) {
		NET_ERROR_RETURN(conn, -22, "join:outbound_max_guest");
	}
	proom->guest[proom->num_guest] = get_eid(conn); // user eid
	proom->num_guest++;
	conn->room = proom;  // my conn->room share the pointer with master

	// re-conn logic
	g_user_room[eid] = proom;

	
	char line[SMALL_BUFFER_SIZE+1];
	str_room_info(line, proom);
	net_write_space(conn, cmd);
	net_write(conn, line, '\n');

	// broadcast to all guests
	for (int i=0; i<proom->num_guest; i++) {
		if (proom->guest[i]==eid) continue; // skip me
		int id = g_user_index[proom->guest[i]];
		connect_t * guest_conn = get_conn(id);
		if (guest_conn==NULL) {
			ERROR_PRINT(-3, "join_guest_conn_null");
			continue;
		}
		net_write_space(guest_conn, cmd);
		net_write(guest_conn, line, '\n');
	}
	// net_writeln(conn, "0 OK");
	// TODO broadcast to all guest (except me, my eid)
	// leave also need to broadcast

	// do not do auto-start now
	return proom->state; // ST_ROOM or ST_GAME;
}

// return number of guests, except except_eid
int room_num_guest(room_t *proom, int except_eid)
{
	int count = 0;
	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i];
		if (eid == except_eid) { 
			continue;
		}
		count ++;
	}
	return count;
}

// return number of online user in room, caller can supply except
// eid for the case when the eid is going to leave room in the future
int room_active_user(room_t *proom, int except_eid)
{
	assert(proom!=NULL); // OK to use assert, or use if (NULL) return 0
	int active_user = 0;
	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i];
		if (eid == except_eid) { continue; } // skip except
		connect_t * conn;
		conn = get_conn_by_eid(eid);
		if (conn == NULL) { continue; }

		// ok, eid match
		if (conn->room != proom) {
			BUG_PRINT(-7, "active_user_conn_room_not_match");
			continue;
		}
		active_user ++;
	}
	return active_user;
}

int force_room_clean(room_t * proom)
{
	int ret; 
	if (proom == NULL) {
		ERROR_RETURN(-3, "force_room_clean:proom_null");
	}
		
	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i];
		g_user_room.erase(eid); // re-conn logic, must before null check
		connect_t * guest_conn = get_conn_by_eid(eid);
		if (guest_conn==NULL) {
			// this may due to offline or ai
			continue;
		}
		guest_conn->st = ST_LOGIN;
		guest_conn->room = NULL;
	}
	ret = free_room(proom->channel, proom);
	ERROR_PRINT(ret, "force_room_clean:free_room_error");

	return ret;
}

int room_clean_by_eid(room_t * proom, int myeid)
{
	int ret;
	if (proom==NULL) {
		// printf("room_clean: no room to clean\n");
		return -3;
	}

	int active_user;
	// get online user count in room
	active_user = room_active_user(proom, myeid);
	// active_user = room_num_guest(proom, myeid);

	if (active_user > 0) {
		return -2; // too many online user in room
	}
	// donot clean room with a game
	if (proom->state == ST_GAME) {
		return -6;
	}

	// DEBUG_PRINT(active_user, "active_user");
	// real clean when active_user = 0
	// re-conn logic, must before free_room()
	for (int i = 0; i < proom->num_guest; i++) {
		g_user_room.erase(proom->guest[i]);  // eid is the key
	}

	DEBUG_PRINT(0, "room_clean(%d, %d)", proom->channel, proom->rid);
	// clean up lua, if it is non-null 
	// (non-start game will have lua=null)
	ret = free_room(proom->channel, proom);  // this will clean lua
	ERROR_NEG_PRINT(ret, "room_clean:free_room");

	return 0; // ok, 0 means the room is cleaned
}

int room_clean(connect_t* conn)
{
	int ret;
	if (conn->room==NULL) {
		// printf("room_clean: no room to clean\n");
		return -3;
	}
	room_t* proom = conn->room;

	// logic:  check how many active user in proom, if active users == 0
	// (not include myconn->euser), then we destroy the room, else keep the room

	int myeid = conn->euser.eid;

	int active_user;
	// get online user count in room
	active_user = room_active_user(proom, myeid);
	// active_user = room_num_guest(proom, myeid);

	if (active_user > 0) {
		return -2; // too many online user in room
	}
	// donot clean room with a game
	if (proom->state == ST_GAME) {
		return -6;
	}

	// DEBUG_PRINT(active_user, "active_user");
	// real clean when active_user = 0
	// re-conn logic, must before free_room()
	for (int i = 0; i < proom->num_guest; i++) {
		g_user_room.erase(proom->guest[i]);  // eid is the key
	}

	DEBUG_PRINT(0, "room_clean(%d, %d)", proom->channel, proom->rid);
	// clean up lua, if it is non-null 
	// (non-start game will have lua=null)
	ret = free_room(proom->channel, proom);  // this will clean lua
	ERROR_NEG_PRINT(ret, "room_clean:free_room");

	conn->room = NULL;
	conn->st = ST_LOGIN;

	return 0; // ok, 0 means the room is cleaned
}

// IN : leave
// OUT: leave [eid] [status]
// side effect: broadcast to all guests in the room
// when the game is NOT started, we can use this
int room_leave(connect_t* conn, const char *cmd, const char * buffer)
{
	int ret;
	room_t * proom = conn->room;
	if (NULL == proom) {
		// NET_ERROR_RETURN(conn, -3, "leave_null_room");
		NET_ERROR_RETURN(conn, -3, "%s", E_ROOM_LEAVE_NULL_ROOM);
	}

	int eid = conn->euser.eid; // TODO : get_eid(conn)


	// TODO not allow guest[0] and guest[1] leave the game 
	// after it is "started"
	if (proom->state == ST_GAME 
	&& (proom->guest[0]==eid || proom->guest[1]==eid)) {
		// NET_ERROR_RETURN(conn, -6, "leave_player_in_game");
		NET_ERROR_RETURN(conn, -6, "%s", E_ROOM_LEAVE_IN_GAME);
	}

	// ok, we send to me:
	net_writeln(conn, "%s %d %d", cmd, eid, ST_LOGIN); // set leaver state

	int clean_flag = room_clean( conn );  // it may clean the room when i am only one
	// core logic:   find the index where guest[index]==my_eid
	// shift the guest[i+1] to guest[i]   
	// where i >= index and  i<=num_guest-1

	if (clean_flag != 0) {
		// broadcast leave [my_eid] to all guests, only if room is there
		char str[30];
		sprintf(str, "leave %d %d", eid, proom->state);
		ret = broadcast_room(proom, eid, str);  
		// DEBUG_PRINT(ret, "leave_broadcast");

		ret = do_room_del(proom, conn, eid);
		if (ret < 0) {
			NET_ERROR_RETURN(conn, -7, "leave_room_del %d", ret);
		}
	}
	// broadcast may do nothing, we still call it!
	// if (clean_flag==1) we can skip broadcast
	return 0;  
}


// kick [eid] [st]
// similar to leave, kick  with eid as the first parameter
int room_kick(connect_t* conn, const char *cmd, const char * buffer)
{
	int ret;
	room_t * proom = conn->room;
	if (proom == NULL) {
		NET_ERROR_RETURN(conn, -3, "kick_null_room");
	}

	// TODO only master can do kick
	int my_eid = get_eid(conn);
	if (proom->guest[0]!=my_eid) {
		// NET_ERROR_RETURN(conn, -6, "kick_only_master_can_kick");
		NET_ERROR_RETURN(conn, -6, "%s", E_ROOM_KICK_ONLY_MASTER_CAN_KICK);
	}
	int eid = atoi(buffer);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -5, "kick_invalid_eid %d", eid);
	}
	// not allow guest[0] and guest[1] leave the started game
	if (proom->state == ST_GAME && eid==proom->guest[1]) {
		// NET_ERROR_RETURN(conn, -26, "kick_player_in_game");
		NET_ERROR_RETURN(conn, -26, "%s", E_ROOM_KICK_PLAYER_CANNOT_KICK);
	}

	// order is important, before clean
	// this is for me, 'kick ' already sent, allow kick myself
	int st = (eid==my_eid) ? ST_LOGIN : proom->state;
	net_write_space(conn, cmd);
	net_writeln(conn, "%d %d", eid, st);

	int clean_flag = 1;
	// core logic: move guest[i+1] to guest[i], for all i >= index
	connect_t *guest_conn = get_conn_by_eid(eid);
	if (guest_conn != NULL) { 
		clean_flag = room_clean(guest_conn);
		// back to login
		if (eid != my_eid) {
			net_writeln(guest_conn, "kick %d %d", eid, ST_LOGIN);
		}
	}

	// DEBUG_PRINT(clean_flag, "kick_clean_flag");
	if (0 != clean_flag) {
		ret = do_room_del(proom, guest_conn, eid);

		// 0 is included because we cannot kick master (0 is master)
		if (ret < 0) {
			// NET_ERROR_RETURN(conn, -16, "kick_eid_not_found %d %d", eid, ret);
			NET_ERROR_RETURN(conn, -16, "%s %d %d", E_ROOM_KICK_NO_SUCH_GUY, eid, ret);
		}

		// order is important, must before remove guest[] array
		char str[20];
		sprintf(str, "kick %d %d", eid, proom->state);
		broadcast_room(proom, my_eid, str);  // except me
		// note: kick target is removed before broadcast is called
	}
	return 0;
}

// lchan
// lchan [channel.id channel.title] [channel.id channel.title] ...
// e.g : 0 Newbie 1 Highhand  
int channel_list(connect_t* conn, const char *cmd, const char * buffer)
{
	net_write_space(conn, cmd);
	net_write_space(conn, "%d %s", LCHAN_ALL_CHANNEL, "所有频道");
	for ( int i = 0; i < MAX_CHANNEL; i++)
	{
		net_write_space(conn, "%d %s", i, g_channel_list[i]);
	}
	net_write(conn, "", '\n');  // no need to use vsprintf

	return 0;
}


// input : lguest 
// NOTE list the guest of my room
// output : lguest [channel.id] [room.id] [state] [guest.id guest.alias]  [guest.id guest.alias] ...
int guest_list(connect_t* conn, const char *cmd, const char *buffer)
{
	int channel, rid;
	int ret;
	room_t * proom = NULL;
	ret = sscanf(buffer, "%d %d", &channel, &rid);
	if (2 == ret) {
		DEBUG_PRINT(0, "get_room by channel=%d  rid=%d\n", channel, rid);
		proom = get_room_by_rid(channel, rid);
		if (NULL == proom) {
			NET_ERROR_RETURN(conn, -5, "guest_list:null_room:channel %d rid %d"
			, channel, rid);
		}
	} else {
		proom = conn->room;
	}

	if (NULL == proom) {
		NET_ERROR_RETURN(conn, -6, "guest_list:null_room");
	}

	char line[SMALL_BUFFER_SIZE + 1];
	str_room_info(line, proom);
	net_write_space(conn, cmd);
	net_write(conn, line, '\n');

	return 0;
}

int send_chan_room(connect_t *conn, int channel, int room_type, int max_size)
{
	int count = 0;
	int index;
	int pwd_flag = 0;
	index = g_used_room[channel];
	while (index >= 1 && index <= MAX_ROOM) {
		room_t * proom = g_room_list[channel] + index;
		// pair of room_id and room title(assume no space!)
		// TODO need to check title without space!!
		
		if (proom->password[0] == '\0') {
			pwd_flag = 0;
		} else {
			pwd_flag = 1;
		}

		if (room_type == LROOM_ALL
		|| (proom->lua == NULL && room_type == LROOM_FREE)
		|| (proom->lua != NULL && room_type == LROOM_PLAY)) {
			net_write_space(conn, "%d %d %d %d %s", channel, proom->rid,  proom->num_guest
			, pwd_flag, proom->title);
		}

		count++;
		if (max_size > 0 && count >= max_size) {
			break;
		}

		// this is after logic
		index = g_room_list[channel][index].next_used;
	}
	return 0;
}

// lroom [channel] [room_type]  - list the room in channel
// if [channel] is empty or non-number,  it is default to 1(CHANNEL_DEFAULT)
// output:
// lroom [channel]  [room.id num_guest room.title]  [room.id num_guest room.title] ...
// note: client using atoi(room.id) may return 0 which can be either
// valid room_id or invalid string to integer
// room_type == 0: all room
// room_type == 1: not playing room
// room_type == 2: playing room
int room_list(connect_t* conn, const char *cmd, const char * buffer)
{
	// channel hard code = 0
	int ret;
	int channel;
	int room_type;
	// channel = strtol_safe(buffer, CHANNEL_DEFAULT); // default is 0
	// TODO last_index = atoi( second arg in buffer)
	
	ret = sscanf(buffer, "%d %d", &channel, &room_type);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -3, "lroom:input_error");
	}

	if ((channel < 0 || channel >= MAX_CHANNEL) && channel != LROOM_ALL_CHANNEL) {
		net_writeln(conn, "-2 channel outbound=%d", channel);
		ERROR_NEG_PRINT(-2, "room_list:channel=%d", channel);
		return -2;
	}

	if (room_type != LROOM_ALL && room_type != LROOM_FREE && room_type != LROOM_PLAY) {
		NET_ERROR_RETURN(conn, -13, "lroom:error_room_type");
	}

	// @see print_room_list()

	net_write_space(conn, "%s %d", cmd, channel);  // return the channel first

	/*
	int index;
	int pwd_flag;
	pwd_flag = 0;
	// TODO #define : for_each_room_list(channel, proom)  {  proom...}
	index = g_used_room[channel];
	while (index >= 1 && index <= MAX_ROOM) {
		room_t * proom = g_room_list[channel] + index;
		// pair of room_id and room title(assume no space!)
		// TODO need to check title without space!!
		
		if (proom->password[0] == '\0') {
			pwd_flag = 0;
		} else {
			pwd_flag = 1;
		}

		if (room_type == LROOM_ALL
		|| (proom->lua == NULL && room_type == LROOM_FREE)
		|| (proom->lua != NULL && room_type == LROOM_PLAY)) {
			net_write_space(conn, "%d %d %d %s", proom->rid,  proom->num_guest
			, pwd_flag, proom->title);
		}

		// this is after logic
		index = g_room_list[channel][index].next_used;
	}
	*/
	if (channel != LROOM_ALL_CHANNEL) {
		send_chan_room(conn, channel, room_type, 0);
	} else {
		for (int i=0; i<MAX_CHANNEL; i++) {
			send_chan_room(conn, i, room_type, 50);
		}
	}
	net_write(conn, "", '\n');  // no need to use vsprintf
	return 0;
}


// CMD: rchat [msg]
// RET: rchat -2
// RET: rchat -6 not_in_room
// BCT: rchat [eid] [alias] [msg]
// note: unlike wchat, rchat will not feedback [rchat 0], but client is good
// to implement the same mechanism as wchat
//
// rchat sent to all guests in the room, can be used when
// waiting for game start in room, or after the game is started (playing)
// msg is a string, not more than 100 character
int room_chat(connect_t *conn, const char *cmd, const char * buffer) 
{
	room_t* proom = conn->room;
	if (proom == NULL) {
		// NET_ERROR_RETURN(conn, -3, "rchat:null_room");
		NET_ERROR_RETURN(conn, -3, "%s", E_ROOM_CHAT_NULL_ROOM);
	}
	// ST_ROOM is a must
	if (conn->st < ST_ROOM) {
		NET_ERROR_RETURN(conn, -6, "%s", E_ROOM_CHAT_NULL_ROOM);
	}

	int len;
	len = strlen(buffer); // strnlen(buffer, 100); // mac 10.6 not work
	// bounded
	if (len > 100) { 
		// NET_ERROR_RETURN(conn, -2, "msg_too_long");
		NET_ERROR_RETURN(conn, -2, "%s", E_ROOM_CHAT_MSG_TOO_LONG);
	}
	int eid = get_eid(conn);
	char str[100+EVIL_ALIAS_MAX+15]; // for rchat [eid] [alias] [msg]
	const char * alias = conn->euser.alias;
	if (strlen(alias)==0) {
		alias = VISITOR_ALIAS;
	}
	// peter: remove len parameter
	// sprintf(str, "%s %d %s %s", cmd, eid, alias, buffer);
	int vip_flag = check_vip(conn);
	len = chat_format(str, cmd, eid, alias, vip_flag, 0, buffer);
	net_writeln(conn, str);
	broadcast_room(proom, eid, str);
	return 0;  // do not return count!!! it is level
}

// global chat:  all users will receive this
// CMD: wchat [msg] 
// RET: wchat 0			- success
// RET: wchat -2 		- msg too long @see wchat_queue.cpp GCHAT_MSG_MAX
// RET: wchat -3 		- msg = null
// BCT: wchat [eid] [alias] [msg]   -- broadcast message
// - a FIFO queue (g_chat_queue), new msg append to the tail
//   each msg should have a timestamp tag attached, which +1 for every msg
// - system max queue size is limited, e.g. 2000 chat msg, each msg is 
//   100 bytes, if the queue is full, it will be truncated by half
//   and the older messages are removed
// - a pseudo-thread : run every time fdwatch() timeout, or every N ms
//   when the server is very busy (fdwatch TIMEOUT is very small)
//   for each online users (if > 3000, we can separate into 2 phases)
//      if the write_queue is empty, flag it as FDW_WRITE
//      the users write_queue, flag it has something to write
//      conn->wchat_ts = 1202     (timestamp)
// - do_send()   if len == 0 :  send one msg in g_chat_queue[conn->wchat_ts]
//   if success(?)  conn->wchat_ts ++ 
//   note: if wchat_ts > last_ts in queue, no need to send, then
//   conn->state = STATE_READING!
// 
int cmd_wchat(connect_t *conn, const char *cmd, const char *buffer)
{
	int eid = get_eid(conn);
	int ret;
	int len;
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	char msg[BUFFER_SIZE];
	const char *alias = conn->euser.alias;
	if (strlen(alias) == 0) {
		alias = VISITOR_ALIAS;  // "_visitor_";
	}

	// TODO check if there is double \n
	// len = sprintf(msg, "%s %d %s %s", cmd, eid, alias, buffer);
	int vip_flag = check_vip(conn);
	len = chat_format(msg, cmd, eid, alias, vip_flag, 0, buffer);
	trim(msg, len);
	// printf("\n--- world chat: [%s]\ncc\ndd\nlast2=%d last=%d  ll=%d\n", msg
	// , msg[strlen(msg)-2], msg[strlen(msg)-1], msg[strlen(msg)]);
	//net_writeln(conn, "%d %s %s", eid, conn->euser.alias, buffer);
	ret = wchat_add(msg); // TODO use this

	if (ret == -2) {
		// NET_ERROR_RETURN(conn, ret, "message_too_long");
		NET_ERROR_RETURN(conn, ret, "%s", E_CMD_WCHAT_MSG_TOO_LONG);
	}
	net_writeln(conn, "%s %d", cmd, ret);
	return ret;
}

int sys_wchat(int show_type, const char * fmt, ...)
{

	int ret;
	char msg[BUFFER_SIZE + 1];
	char *ptr = msg;
	ptr += sprintf(ptr, SYS_WCHAT, show_type);

	va_list argptr;
	va_start(argptr, fmt);
	vsnprintf(ptr, BUFFER_SIZE, fmt, argptr);
	va_end(argptr);

	// note: '系统' eid = 1501 in s1
	// len = sprintf(msg, "wchat 1501 系统 %s", buffer);
	trim(msg, strlen(msg));
	DEBUG_PRINT(0, "sys_wchat:msg=%s", msg);

	ret = wchat_add(msg); // TODO use this

	if (ret == -2) {
		ERROR_RETURN(ret, "sys_wchat:wchat_add_error");
	}
	return ret;
}

int cmd_notice(connect_t *conn, const char *cmd, const char *buffer)
{

	int ret;
	int type;

	ret = sscanf(buffer, "%d", &type);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input");
	}

	if (type < 0 || type > g_design->notice_count) {
		NET_ERROR_RETURN(conn, -15, "invalid_input");
	}

	if (type == 0) {
		if (g_design->notice_count == 0) {
			net_writeln(conn, "%s %d %d"
			, cmd, type, g_design->notice_count);
			return 0;
		}
		char out_buffer[MAX_TITLE_SIZE * MAX_NOTICE + 10];
		char * ptr = out_buffer;
		out_buffer[0] = '\0';
		for (int i=1;i<=g_design->notice_count;i++) {
			ptr += sprintf(ptr, "%s ", g_design->notice_list[i].title);
		}
		net_writeln(conn, "%s %d %d %s"
		, cmd, type, g_design->notice_count, out_buffer);
		return 0;
	}

	
	notice_t *pnotice = &g_design->notice_list[type];
	if (pnotice->note[0] == '\0') {
		WARN_PRINT(-6, "cmd_notice:null_note %d", type);
		return -6;
	}
		
	net_writeln(conn, "%s %d %s", cmd, type, pnotice->note);
	return 0;
}

// CMD:lai
// RET:lai total [ai_info1] [ai_info2]...
// ai_info = eid, alias, icon, rating, exp, gold, present 
int cmd_list_ai(connect_t *conn, const char *cmd, const char *buffer)
{
	int count;
	int lv;
	char out_buffer[BUFFER_SIZE];
	bzero(out_buffer, BUFFER_SIZE);
	char *ptr;
	ai_t *pai;

	int eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	lv = conn->euser.lv;

	ptr = out_buffer;
	count = 0;
	// eid start from 1
	for (int i=MAX_AI_EID; i>=1; i--) { 
		if (g_design->ai_list[i].id != 0 && g_design->ai_list[i].lv <= lv) {
			count++;
			pai = &g_design->ai_list[i];
			// ai_info = eid, alias, icon, rating
			// 			, exp, gold, pid, lv
			ptr += sprintf(ptr, "%d %s %d %lf %d %d %d %d "
			, pai->id, pai->alias, pai->icon, pai->rating
			, pai->win_exp, pai->win_gold
			, pai->pid, pai->lv
			);
		}
	}

	net_writeln(conn, "%s %d %s", cmd, count, out_buffer);

	return 0;
}

// CMD: lconstant
// RET: lconstant win_quick_gold=10 win_solo_gold=5 ...
int cmd_list_constant(connect_t *conn, const char *cmd, const char *buffer)
{
	net_writeln(conn, "%s batch_refresh_gold=%d pick_gold=%d batch_refresh_crystal=%d pick_crystal=%d max_timeout=%d guild_bonus_rate=%lf create_guild_gold=%d create_guild_crystal=%d exchange_crystal_gold=%d", cmd
	,	g_design->constant.batch_refresh_gold 
	,	g_design->constant.pick_gold 		
	,	g_design->constant.batch_refresh_crystal 	
	,	g_design->constant.pick_crystal 		
	,	g_design->constant.max_timeout 	
	,	g_design->constant.guild_bonus_rate		
	,	g_design->constant.create_guild_gold	
	,	g_design->constant.create_guild_crystal
	,	g_design->constant.exchange_crystal_gold
	);
	return 0;
}


// CMD:ljob
// RET:ljob total hero_id1, hero_id2...
int cmd_list_job(connect_t *conn, const char *cmd, const char *buffer)

{
	int count;
	char out_buffer[BUFFER_SIZE];
	bzero(out_buffer, BUFFER_SIZE);
	char *ptr;

	int eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ptr = out_buffer;
	count = 0;
	std_deck_t std_deck;
	for (int i=0; i<HERO_MAX+1; i++) {
		std_deck = g_design->std_deck_list[i];
		if (std_deck.id == 0) continue;
		count++;
		ptr += sprintf(ptr, "%d ", std_deck.id);
	}

	net_writeln(conn, "%s %d %s", cmd, count, out_buffer);

	return 0;
}


// CMD: fchat eid msg
// RET: fchat eid alias msg
// ERR: fchat -9 not_login
// ERR: fchat -19 recipient_not_login
int friend_chat(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int len;
	int s_eid;
	int r_eid;
	int n;
	char msg[BUFFER_SIZE];
	const char *ptr;

	s_eid = get_eid(conn);
	if (s_eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ptr = buffer;
	ret = sscanf(buffer, "%d %n", &r_eid, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input");
	}

	if (r_eid <= MAX_AI_EID || r_eid == s_eid) {
		NET_ERROR_RETURN(conn, -15, "invalid_eid");
	}

	ptr += n;

	connect_t *r_conn = get_conn_by_eid(r_eid);
	if (NULL == r_conn) {
		// NET_ERROR_RETURN(conn, -19, "recipient_not_login");
		NET_ERROR_RETURN(conn, -19, "%s", E_FRIEND_CHAT_FRIEND_NOT_LOGIN);
	}

	const char *alias = conn->euser.alias;
	if (strlen(alias) == 0) {
		alias = VISITOR_ALIAS;  // "_visitor_";
	}

	// len = sprintf(msg, "%s %d %s %s", cmd, s_eid, alias, ptr);
	int vip_flag = check_vip(conn);
	len = chat_format(msg, cmd, s_eid, alias, vip_flag, 0, ptr);
	trim(msg, len);

	net_writeln(conn, "%s", msg);
	net_writeln(r_conn, "%s", msg);
	return ret;
}

// CMD: lpay channel
// RET: lpay channel count pay_info
// pay_info = [pay_code] [pay_price] [money_type] [money] [description]
int pay_list(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int channel;
	int count = 0;
	char msg[BUFFER_SIZE];
	bzero(msg, BUFFER_SIZE);
	char *ptr;

	ret = sscanf(buffer, "%d", &channel);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "pay_list:invalid_input %d", ret);
	}

	if (channel <= 0 || channel >= MAX_PAY_NUM) {
		NET_ERROR_RETURN(conn, -15, "pay_list:invalid_channel %d", channel);
	}

	design_pay_t * ppay;
	ptr = msg;
	for (int i=0; i<MAX_PAY_NUM; i++) {
		ppay = g_design->pay_list + i;
		if (ppay->channel == channel) {
			count++;
			ptr += sprintf(ptr, "%d %d %d %d %s %s ", ppay->pay_code
			, ppay->price, ppay->money_type, ppay->money
			, ppay->title, ppay->description);
		}
	}
	
	net_writeln(conn, "%s %d %d %s", cmd, channel, count, msg);

	return ret;
}

// assume: lmis is already called (either in login / register)
// CMD: mis
// RET: mis [n] [mission_data1] [mission_data2] ... 
// mission_data = [mid] [daily] [mtype] [p1] [p2] [p3] [n1] [n2] [n3] [status] ["mtext"]
// mtext not ready yet
int cmd_mission(connect_t *conn, const char *cmd, const char *buffer)
{
	int mid;
	int msize;
	design_mission_t dmis;

	if (conn->euser.eid <= 0) {
		NET_ERROR_RETURN(conn, -8, "not_login");
	}


	net_write_space(conn, "%s", cmd);

	mission_t * mlist= conn->euser.mission_list;
	msize = mlist_size(mlist);
	net_write_space(conn, "%d", msize); // counter
	for (int i = 0; i<MAX_MISSION; i++) {
		mission_t & mission = mlist[i];
		if (mission.mid == 0) {
			continue;
		}
		mid = mission.mid;
		dmis = design_mission_by_mid(g_design, mid);
		if (dmis.mid == 0) {
			BUG_PRINT(-7, "mission mid_not_found %d", mid);
		}
		// mid use mlist[i].mid, do not use dmis.mid
		// if dmis not found, mtype=0
		// TODO : dmis.p2, dmis.p3 are duplicate, need to update client
		net_write_space(conn, "%d %d %d %d %d %d %d %d %d %d |"
		, mid, dmis.daily, dmis.mtype, dmis.p1, dmis.p2, dmis.p3
		, mission.n1
		, dmis.p2 //  mission-fix : 
		, dmis.p3 // mission-fix  : useless now, was n2, n3
		, mission.status
		);
		// TODO dmis.mtext
	}

	// write a line feed
	net_writeln(conn, ""); // TODO remove this
	

	return 0;
}


// in: mlist, page_size
// out: plist, ret=plist.size  (plist is allocated by caller)
// note: mlist.size = MAX_MISSION
// 1. ok_list = list of mission in mlist where mlist[i].status = OK
//    (ORDER BY mid DESC)
// 2. ready_list = sub-list of mlist where status = READY
// 3. full_list = ok_list + ready_list (ok first, then ready)
// 4. skip start_index in full_list, and start to get page_size of full_list
//    full_list[ start_index .. start_index + page_size] 
//    sometimes start_index + page_size >= MAX_MISSION, break!!
// 5. sub-list of full_list above -> plist, total usually = page_size,
//    but it will be smaller if 4. break
// note:  ok_list, ready_list, full_list size = MAX_MISSION
int get_mission_page(mission_t ** plist, mission_t *mlist, 
const int page_size, const int start_index, int mlist_type, int chapter_id)
{
	mission_t *ok_list[MAX_MISSION];
	mission_t *ready_list[MAX_MISSION];
	mission_t *finish_list[MAX_MISSION];
	mission_t *full_list[MAX_MISSION];
	int ok_size = 0;		
	int ready_size = 0;	
	int finish_size = 0;	
	int full_size = 0;
	int plist_size = 0;

	if (start_index < 0) {
		ERROR_PRINT(-5, "get_mission_page:invalid_start_index:%d"
		, start_index);
		return 0;
	}
	if (page_size < 0) {
		ERROR_PRINT(-15, "get_mission_page:invalid_page_size:%d"
		, page_size);
		return 0;
	}
	for (int i = MAX_MISSION - 1; i >= 0; i--) {
		int mid = mlist[i].mid;
		design_mission_t &dmis = g_design->mission_list[mid];
		if ((mlist_type == MLIST_TYPE_CHAPTER
			&& ((dmis.p2 != chapter_id)
				|| (dmis.mtype != MISSION_CHAPTER_STAGE
				&& dmis.mtype != MISSION_CHAPTER)))
		|| (mlist_type == MLIST_TYPE_WITHOUT_CHAPTER
			&& (dmis.mtype == MISSION_CHAPTER_STAGE
				|| dmis.mtype == MISSION_CHAPTER)))
		{
			continue;
		}
		// 1. ok_list
		if (mlist[i].status == MISSION_STATUS_OK) {
			ok_list[ok_size] = mlist + i;
			ok_size++;
			continue; // early continue
		}

		// 2. ready_list
		if (mlist[i].status == MISSION_STATUS_READY) {
			ready_list[ready_size] = mlist + i;
			ready_size++;
			continue; // early continue
		}

		// 3. finish_list
		if (mlist[i].status == MISSION_STATUS_FINISH && mlist_type == MLIST_TYPE_CHAPTER) {
			finish_list[finish_size] = mlist + i;
			finish_size++;
			continue; // early continue
		}
	}

	// 4. full_list
	full_size = ok_size + ready_size + finish_size;
	for (int i = 0; i < ok_size; i++) {
		full_list[i] = ok_list[i];
	}
	for (int i = 0; i < ready_size; i++) {
		full_list[i + ok_size] = ready_list[i];
	}
	for (int i = 0; i < finish_size; i++) {
		full_list[i + ok_size+ready_size] = finish_list[i];
	}
	// assert(full_size <= MAX_MISSION)

	// 4. skip start_index in full_list, and start to get page_size 
	//    of full_list
	//    full_list[ start_index .. start_index + page_size] 
	//    sometimes start_index + page_size >= MAX_MISSION, break!!
	plist_size = 0;
	for (int i = start_index; i < full_size && i < start_index + page_size
	; i++,plist_size++)
	{
		plist[plist_size] = full_list[i];
	}
	
	return plist_size;
}


// @see mission-design.txt
// CMD: mlist [mlist_type] [chapter_id] [start_id=0] [page_size]
// RET: mlist [mlist_type] [chapter_id] [start_id] [total] [mission1] [mission2] ... [mission_total]
// [mission_n] = [mid] [status] [mtype] [n1] [p1] [p2] [p3] 
//               [exp] [gold] [crystal] [power]
//               [card_count] [card_list]
//               [piece_count] [piece_list]
//				 [daily] [reset_time] [mtext]
// mlist_type: all(0),chapter(1),all_without_chapter(2)
// peter: updated mission_n format (status is after mid)
// reset_time is in form of hh:ii  (hour:minute)
int cmd_mlist(connect_t *conn, const char *cmd, const char *buffer)
{
	int start_index = 0;
	int page_size = 10; // default : 10 mission per page
	int mlist_type = 0;
	int chapter_id = 0;
	char *tmp_ptr;
	char tmp_buffer[1000];

	int eid = conn->euser.eid;
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -8, "not_login");
	}

	// default start from first record
	start_index = 0;
	sscanf(buffer, "%d %d %d %d", &mlist_type, &chapter_id
	, &start_index, &page_size); // no error check
	if (mlist_type != MLIST_TYPE_ALL && mlist_type != MLIST_TYPE_CHAPTER
	&& mlist_type != MLIST_TYPE_WITHOUT_CHAPTER)
	{
		NET_ERROR_RETURN(conn, -15, "is_chapter_invalid[%d] eid=%d"
		, mlist_type, eid);
	}
	if (chapter_id < 0 || chapter_id > MAX_CHAPTER)
	{
		NET_ERROR_RETURN(conn, -25, "chapter_id_invalid[%d] eid=%d"
		, chapter_id, eid);
	}
	if (start_index < 0) { // later: || start_id > MAX_MISSION 
		NET_ERROR_RETURN(conn, -5, "start_id < 0  eid=%d", eid);
	}
	if (page_size <= 0) { // page_size should never be zero
		NET_ERROR_RETURN(conn, -10, "page_size < 0  eid=%d", eid);
	}

	// plist is dynamic alloc in stack
	mission_t * plist[page_size];
	int plist_size = 0;

	mission_t * mlist = conn->euser.mission_list;
	// msize = mlist_size(mlist); // useful ?  usually there are missions
	plist_size = get_mission_page(plist, mlist, page_size, start_index
	, mlist_type, chapter_id);
	// assert: plist_size <= page_size

	// RET: mlist [start_id] [total] [mission1] [mission2] ... 
	// [mission_n] = [mid] [status] [mtype] [n1] [p1] [p2] [p3] 
	//				 [daily] [reset_time] [mtext]
	net_write_space(conn, "%s %d %d %d %d", cmd, mlist_type, chapter_id
	, start_index, plist_size);

	for (int i=0; i<plist_size; i++) {
		mission_t & mission = *plist[i]; 
		// shall we use get_design_mission() ?
		design_mission_t & dmis = get_design_mission(g_design->mission_list, mission.mid);
		if ((mlist_type == MLIST_TYPE_CHAPTER
			&& ((dmis.p2 != chapter_id)
				|| (dmis.mtype != MISSION_CHAPTER_STAGE
				&& dmis.mtype != MISSION_CHAPTER)))
		|| (mlist_type == MLIST_TYPE_WITHOUT_CHAPTER
			&& (dmis.mtype == MISSION_CHAPTER_STAGE
				|| dmis.mtype == MISSION_CHAPTER)))
		{
			continue;
		}

		tmp_ptr = tmp_buffer;
		tmp_ptr += sprintf(tmp_ptr, "%d", dmis.card_count);
		for (int cdx = 0; cdx < dmis.card_count; cdx++) {
			tmp_ptr += sprintf(tmp_ptr, " %d", dmis.reward_card[cdx]);
		}
		tmp_ptr += sprintf(tmp_ptr, " %d", dmis.piece_count);
		for (int pdx = 0; pdx < dmis.piece_count; pdx++) {
			tmp_ptr += sprintf(tmp_ptr, " %d %d", dmis.reward_piece[pdx][0]
			, dmis.reward_piece[pdx][1]);
		}
		// peter: leave more space between parts
		// 4 parts:
		// [mid] [status]
		// [mtype] [n1] [p1] [p2] [p3]
		// [exp] [gold] [crystal] [power]
		// [card_count] [card_list]
		// [piece_count] [piece_list]
		// [daily] [reset_time] [mtext]
		net_write_space(conn, " %d %d %d %d %d %d %d %d %d %d %d %s %d %s %s"
		, mission.mid, mission.status, dmis.mtype
		, mission.n1, dmis.p1, dmis.p2, dmis.p3
		, dmis.reward_exp
		, dmis.reward_gold, dmis.reward_crystal
		, dmis.reward_power, tmp_buffer
		, dmis.daily, dmis.reset_time, dmis.mtext);
	}

	net_writeln(conn, "%s", "");
	return 0;

/***
	// first, count how many record should be return (total)
	// use skip_count as temp counter to skip 0 to start_id - 1 valid record

	// early exit for empty map, since for(it=mlist.begin()) cannot handle 
	if (msize <= 0) {
		net_writeln(conn, "%s %d %d", cmd, start_id, 0);
		return 0;
	}

//	int skip_count;
//	int total ;
//	int fin_total, unfin_total;
	total = 0;
	fin_total = 0;
	unfin_total = 0;
	skip_count = start_id;  // keep start_id unchange
	// TODO reverse order
	for (int i=MAX_MISSION-1; i>=1; i--) {
		mission_t & mission = mlist[i];
		if (mission.mid == 0) {
			continue;
		}
		// only print ready and ok
		if (mission.status != MISSION_STATUS_READY 
		&& mission.status != MISSION_STATUS_OK) {
			continue;
		}

//		// here: the mission record is valid
//		if (skip_count > 0) {
//			skip_count --;
//			continue; // skip
//		}
//		
//		print_mission(mission);

		// check mission is valid, refer to design_mission
		design_mission_t & dmis = g_design->mission_list[mission.mid];
		if (dmis.mid == 0) {
			BUG_PRINT(-17, "dmis.mid=0  mid=%d  eid=%d", mission.mid, eid);
			// ??? shall we store in showlist ?
		} 

		if (mission.status == MISSION_STATUS_OK) {
			fin_showlist[fin_total] = mlist + i;
			fin_total++;
		} else {
			unfin_showlist[unfin_total] = mlist + i;
			unfin_total++;
		}
		if (fin_total + unfin_total >= MAX_MISSION_LIST) {
			break;
		}
//		// core logic : save the pointer to show list
//		showlist[total] = mlist + i;  // save the pointer
//		total ++;
//		if (total >= MAX_MISSION_LIST) {
//			break;  // over page limit
		// }
	}

	for (int i = 0; i < fin_total; i++) {
		showlist[i] = fin_showlist[i];
	}
	for (int i = 0; i < unfin_total; i++) {
		showlist[fin_total + i] = unfin_showlist[i];
	}
	total = fin_total + unfin_total;


	// printf("----- mlist start_id %d  total %d\n", start_id, total);

	// write the header first
	net_write_space(conn, "%s %d %d", cmd, start_id, total);

	skip_count = start_id;
	for (int i=skip_count; i<total; i++) {
		mission_t & mission = *(showlist[i]);
		design_mission_t & dmis = g_design->mission_list[mission.mid];
		// peter: leave more space between parts
		// 4 parts:
		// [mid] [status]
		// [mtype] [n1] [p1] [p2] [p3]
		// [card] [exp] [gold] [crystal]
		// [daily] [reset_time] [mtext]
		net_write_space(conn, " %d %d   %d %d %d %d %d   %d %d %d %d   %d %s %s"
		, mission.mid, mission.status, dmis.mtype
		, mission.n1, dmis.p1, dmis.p2, dmis.p3
		, dmis.reward_card, dmis.reward_exp
		, dmis.reward_gold, dmis.reward_crystal
		, dmis.daily, dmis.reset_time, dmis.mtext);
	}

	net_writeln(conn, "%s", "");
	return 0;
	**/
}

// RET: gate [side=1] [cmd_size=0] [seed] [deck1] [gate_count] [r1] [c1] [r2] [c2] ... [rn] [cn]
int gate_info(connect_t * conn, const char *cmd, const char * buffer)
{

	int ret;
	char out_buffer[BUFFER_SIZE];
	room_t * proom = conn->room;
	char *ptr;
	// int len;
	ptr = out_buffer;

	int eid = get_eid(conn);
	int side = 99; // default watcher 
	if (eid==proom->guest[0]) { side = 1; }

	int cmd_size = (int)proom->cmd_list.size();
	// DEBUG_PRINT(0, "gate_info:cmd_size=%d", cmd_size);
	// start_side,  cmd_size, seed, deck[0], gate_size, gate_info1, gate_info2 ... gate_infon
	// gate_info = [round] [card_id]
	int size = g_design->design_gate_list[proom->guest[1]].size;
	ptr += sprintf(ptr, "gate %d %d %d %d %.400s %d %s", proom->guest[1], side, cmd_size, proom->seed, proom->deck[0], size, proom->deck[1]);

	/*
	len = strlen(proom->deck[1]);
	DEBUG_PRINT(0, "gate_info:len=%d", len);

	ptr += sprintf(ptr, "%d ", len/2);
	for (int i=0; i<len; i++) {
		ptr += sprintf(ptr, "%d ", proom->deck[1][i]);
	}
	*/
	ret = net_write(conn, out_buffer, '\n'); // faster than net_writeln

	for (int i=0; i<cmd_size; i++) {
		// max cmd_size = "999 b 1201 2101 2301 2302 2303"
		int cmd_len = 0;
		cmd_len = strlen(proom->cmd_list[i].c_str());
		// 25 means "gerr -82 cmd_overflow"
		if (conn->outlen + cmd_len + 25 >= BUFFER_SIZE) {
			net_writeln(conn, "gerr -82 cmd_overflow");
			ERROR_RETURN(-82, "ginfo_buffer_overflow outlen %d cmd %d"
			, conn->outlen, i);
		}
		ret = net_write(conn, proom->cmd_list[i].c_str(), '\n');
		if (ret < 0) {
			BUG_PRINT(-82, "ginfo_buffer2 ret %d outlen %d cmd %d"
			, ret, conn->outlen, i);
		}
	}

	return 0;
}

	


// old RET: solo_plus [side] [cmd_size] [seed] [timeout] [deck1] [solo_hero] [game_flag] [solo_max_ally] [type_list] [c1] [c2] ... [cn]
//
// RET: solo_plus [side] [cmd_size] [seed] [timeout] 
// [game_flag] [solo_max_ally] [hp2] [hp1] [energy1]
// [type_list]
// [card_len1] [deck1] 
// [card_len2] [deck2]
// if card_len == 0, means deck is deck400
// if card_len > 0, means deck is list deck [1 22 23 35 ...]
int solo_plus_info(connect_t * conn, const char *cmd, const char * buffer)
{
	int ret;
	char out_buffer[BUFFER_SIZE];
	room_t * proom = conn->room;
	char *ptr;
	// int len;
	ptr = out_buffer;

	int eid = get_eid(conn);
	int side = 99; // default watcher 
	if (eid==proom->guest[0]) { side = 1; }

	long timeout = proom->game_timeout - time(NULL);
	if (timeout < 0) {
		timeout = 0;
	}
	int cmd_size = (int)proom->cmd_list.size();
	DEBUG_PRINT(0, "solo_plus_info:cmd_size=%d", cmd_size);

	int card_len1 = 0;
	int card_len2 = 0;
	if (strlen(proom->deck[0]) != EVIL_CARD_MAX) {
		card_len1 = count_card(proom->deck[0]);
	}
	if (strlen(proom->deck[1]) != EVIL_CARD_MAX) {
		card_len2 = count_card(proom->deck[1]);
	}

	// RET: solo_plus [side] [cmd_size] [seed] [timeout] 
	// [game_flag] [solo_max_ally] [hp2] [myhero_hp] [myhero_energy]
	// [type_list]
	// [card_len1] [deck1] 
	// [card_len2] [deck2]
	ptr += sprintf(ptr, "solo_plus %d %d %d %ld %d %d %d %d %d %.100s %d %s %d %s"
	, side, cmd_size, proom->seed, timeout
	, proom->game_flag, proom->ai_max_ally, proom->hp2
	, proom->hp1, proom->energy1
	, proom->type_list
	, card_len1, proom->deck[0]
	, card_len2, proom->deck[1]);

	ret = net_write(conn, out_buffer, '\n'); // faster than net_writeln

	for (int i=0; i<cmd_size; i++) {
		// max cmd_size = "999 b 1201 2101 2301 2302 2303"
		int cmd_len = 0;
		cmd_len = strlen(proom->cmd_list[i].c_str());
		// 25 means "gerr -82 cmd_overflow"
		if (conn->outlen + cmd_len + 25 >= BUFFER_SIZE) {
			net_writeln(conn, "gerr -82 cmd_overflow");
			ERROR_RETURN(-82, "solo_plus_info:buffer_overflow outlen %d cmd %d"
			, conn->outlen, i);
		}
		ret = net_write(conn, proom->cmd_list[i].c_str(), '\n');
		if (ret < 0) {
			BUG_PRINT(-82, "solo_plus_info:buffer2 ret %d outlen %d cmd %d"
			, ret, conn->outlen, i);
		}
	}

	if (proom->num_guest >= 2 && proom->guest[1]<=MAX_AI_EID) {
		return ai_play(conn);
	}

	return 0;
}

// useless now, remove later
/*
// RET: robot_game [side] [cmd_size] [seed] [timeout] [deck1(400)] [deck2(list)]
int robot_game_info(connect_t * conn, const char *cmd, const char * buffer)
{

	int ret;
	char out_buffer[BUFFER_SIZE];
	room_t * proom = conn->room;
	char *ptr;
	// int len;
	ptr = out_buffer;

	int eid = get_eid(conn);
	int side = 99; // default watcher 
	if (eid==proom->guest[0]) { side = 1; }

	long timeout = proom->game_timeout - time(NULL);
	if (timeout < 0) {
		timeout = 0;
	}
	int cmd_size = (int)proom->cmd_list.size();
	DEBUG_PRINT(0, "robot_game_info:cmd_size=%d", cmd_size);
	// RET: robot_game [side] [cmd_size] [seed] [timeout] [deck1(400)] [deck2(list)]
	ptr += sprintf(ptr, "robot_game %d %d %d %ld %d %.400s %s"
	, side, cmd_size, proom->seed
	, timeout, proom->game_type, proom->deck[0], proom->deck[1]);

	ret = net_write(conn, out_buffer, '\n'); // faster than net_writeln

	for (int i=0; i<cmd_size; i++) {
		// max cmd_size = "999 b 1201 2101 2301 2302 2303"
		int cmd_len = 0;
		cmd_len = strlen(proom->cmd_list[i].c_str());
		// 25 means "gerr -82 cmd_overflow"
		if (conn->outlen + cmd_len + 25 >= BUFFER_SIZE) {
			net_writeln(conn, "gerr -82 cmd_overflow");
			ERROR_RETURN(-82, "solo_plus_info:buffer_overflow outlen %d cmd %d"
			, conn->outlen, i);
		}
		ret = net_write(conn, proom->cmd_list[i].c_str(), '\n');
		if (ret < 0) {
			BUG_PRINT(-82, "solo_plus_info:buffer2 ret %d outlen %d cmd %d"
			, ret, conn->outlen, i);
		}
	}

	if (proom->num_guest >= 2 && proom->guest[1]<=MAX_AI_EID) {
		return ai_play(conn);
	}

	return 0;
}
*/

int tower_info(connect_t * conn, const char *cmd, const char * buffer)
{
	int ret;
//	char out_buffer[BUFFER_SIZE];
	room_t * proom = conn->room;
//	char *ptr;
	// int len;
//	ptr = out_buffer;

	int eid = get_eid(conn);
	int side = 99; // default watcher 
	if (eid==proom->guest[0]) { side = 1; }
	int cmd_size = (int)proom->cmd_list.size();

	// peter: make large buffer inside block
	{   // TODO merge code with game_info_broadcast  (one format = one code)
		char deckbuff[1000]; // 400 + 400 + something
		// TODO force-next replace proom->start_side to proom->game_timeout
		// ginfo [side] [cmd_size] [seed] [timeout] [deck_400_0] [deck_400_1]
		long timeout = proom->game_timeout - time(NULL);
		if (timeout < 0) {
			timeout = 0;
		}
		sprintf(deckbuff, "tower %d %d %d %ld %s %s %d %d %d %d", side, cmd_size
		, proom->seed, timeout, proom->deck[0], proom->deck[1]
		, proom->tower_pos, proom->tower_hp, proom->tower_res, proom->tower_energy);
		ret = net_write(conn, deckbuff, '\n'); // faster than net_writeln
	}
	if (ret < 0) {
		NET_ERROR_RETURN(conn, -82, "tower_info:buffer_overflow %d", conn->outlen);
	}

	for (int i=0; i<cmd_size; i++) {
		// max cmd_size = "999 b 1201 2101 2301 2302 2303"
		int cmd_len = 0;
		cmd_len = strlen(proom->cmd_list[i].c_str());
		// 25 means "gerr -82 cmd_overflow"
		if (conn->outlen + cmd_len + 25 >= BUFFER_SIZE) {
			net_writeln(conn, "gerr -82 cmd_overflow");
			ERROR_RETURN(-82, "tower_info:buffer_overflow outlen %d cmd %d"
			, conn->outlen, i);
		}
		ret = net_write(conn, proom->cmd_list[i].c_str(), '\n');
		if (ret < 0) {
			BUG_PRINT(-82, "tower_info:buffer2 ret %d outlen %d cmd %d"
			, ret, conn->outlen, i);
		}
	}
	// net_write(conn, "end", '\n');  // end of cmd_list

	if (proom->num_guest >= 2 && proom->guest[1]<=MAX_AI_EID) {
		return ai_play(conn);
	}

	return 0;
}

/**
 * normal game info
 * CMD: ginfo
 * RET: ginfo [side] [cmd_size] [seed] [timeout] [game_type]
 *			  [hp1] [hp2] [energy1] [energy2]
 * 			  [card_len1] [deck1] [card_len2] [deck2]
 * if card_len == 0, means deck is deck400
 * if card_len > 0, means deck is list deck [1 22 23 35 ...]
 * 
 * re-conn: lguest -> game_info
 */
int game_info(connect_t * conn, const char *cmd, const char * buffer)
{
	int ret;
	room_t * proom = conn->room;
	if (NULL == proom) {
		NET_ERROR_RETURN(conn, -3, "ginfo_null_room");
	}

	if (ST_GAME != proom->state) {
		NET_ERROR_RETURN(conn, -6, "ginfo_not_game");
	}

	if (proom->game_type == GAME_GATE) {
		ret =  gate_info(conn, cmd, buffer);
		return ret;
	}

	if (proom->game_type == GAME_SOLO_PLUS
	|| proom->game_type == GAME_CHAPTER
	) {
		ret =  solo_plus_info(conn, cmd, buffer);
		return ret;
	}


	if (proom->game_type == GAME_TOWER) {
		ret =  tower_info(conn, cmd, buffer);
		return ret;
	}

	if (proom->num_guest < 2) {
		// this is impossible, because game is already started!
		NET_ERROR_RETURN(conn, -7, "ginfo_less_num_guest %d"
		, proom->num_guest);
	}

	// ginfo N, where N>=0,  is the size of cmd_list (at command)
	int eid = get_eid(conn);
	int side = 99; // default watcher 
	if (eid==proom->guest[0]) { side = 1; }
	if (eid==proom->guest[1]) { side = 2; }
	int cmd_size = (int)proom->cmd_list.size();

	
	// peter: make large buffer inside block
	{   // TODO merge code with game_info_broadcast  (one format = one code)
		char deckbuff[1000]; // 400 + 400 + something
		// TODO force-next replace proom->start_side to proom->game_timeout
		// ginfo [side] [cmd_size] [seed] [timeout] [deck_400_0] [deck_400_1]
		long timeout = proom->game_timeout - time(NULL);
		if (timeout < 0) {
			timeout = 0;
		}

		int card_len1 = 0;
		int card_len2 = 0;
		if (strlen(proom->deck[0]) != EVIL_CARD_MAX) {
			card_len1 = count_card(proom->deck[0]);
		}
		if (strlen(proom->deck[1]) != EVIL_CARD_MAX) {
			card_len2 = count_card(proom->deck[1]);
		}
#ifdef HCARD
		// for H5 game!
        sprintf(deckbuff, "%s %d %d %d %ld %d %s"
        , cmd, side, cmd_size
        , proom->seed, timeout, proom->start_side, proom->world);
#else
		// for normal game, not H5!
		sprintf(deckbuff, "%s %d %d %d %ld %d %d %d %d %d %d %d %s %d %s"
		, cmd, side, cmd_size, proom->seed, timeout, proom->game_type
		, proom->auto_battle
		, proom->hp1, proom->hp2, proom->energy1, proom->energy2
		, card_len1, proom->deck[0], card_len2, proom->deck[1]);
#endif
		ret = net_write(conn, deckbuff, '\n'); // faster than net_writeln
	}
	if (ret < 0) {
		NET_ERROR_RETURN(conn, -82, "ginfo_buffer_overflow %d", conn->outlen);
	}

	for (int i=0; i<cmd_size; i++) {
		// max cmd_size = "999 b 1201 2101 2301 2302 2303"
		int cmd_len = 0;
		cmd_len = strlen(proom->cmd_list[i].c_str());
		// 25 means "gerr -82 cmd_overflow"
		if (conn->outlen + cmd_len + 25 >= BUFFER_SIZE) {
			net_writeln(conn, "gerr -82 cmd_overflow");
			ERROR_RETURN(-82, "ginfo_buffer_overflow outlen %d cmd %d"
			, conn->outlen, i);
		}
		ret = net_write(conn, proom->cmd_list[i].c_str(), '\n');
		if (ret < 0) {
			BUG_PRINT(-82, "ginfo_buffer2 ret %d outlen %d cmd %d"
			, ret, conn->outlen, i);
		}
	}
	// net_write(conn, "end", '\n');  // end of cmd_list

	if (proom->num_guest >= 2 && proom->guest[1]<=MAX_AI_EID) {
		return ai_play(conn);
	}

	return 0;
}


int get_hero_info(evil_hero_t * hero, int eid, char *deck)
{
	int ret = 0;
	hero->hp = 0;
	hero->energy = 99;

	if (deck == NULL) {
		ret = -3;
		BUG_PRINT(ret, "room_set_hero_info:deck_null");
		return ret;
	}

	if (eid <= MAX_AI_EID) {
		return 0;
	}


	connect_t * conn = get_conn_by_eid(eid);
	if (conn == NULL) {
		return 0;
	}

	if (strlen(deck) == EVIL_CARD_MAX) {
		// deck is deck400
		hero->hero_id = get_hero(deck);
	} else {
		// deck is card list
		sscanf(deck, "%d", &hero->hero_id);
	}

	if (hero->hero_id <= 0 || hero->hero_id > HERO_MAX) {
		ret = -5;
		BUG_PRINT(ret, "get_hero_info:hero_id %d", hero->hero_id);
		return ret;
	}

	// get my hero hp and energy
	evil_hero_data_t &hero_data = conn->euser.hero_data_list[hero->hero_id];

	// use player's hero
	hero->hp = hero_data.hero.hp;
	hero->energy = hero_data.hero.energy;

	if (hero_data.hero.hero_id == 0) {
		// player has no hero in design.design_solo.hero_id
		WARN_PRINT(-7, "get_hero_info:hero_data_error hero_id %d", hero->hero_id);
		design_hero_t &dhero = g_design->hero_list[hero->hero_id];
		if (dhero.hero_id == 0) {
			BUG_PRINT(-17, "get_hero_info:no_such_design_hero hero_id %d"
			, hero->hero_id);
		}
		hero->hp = dhero.hp;
		hero->energy = dhero.energy;
	}
	return 0;
};

int room_set_hero_info(room_t *proom, solo_t *solo = NULL, design_robot_t *robot = NULL);
int room_set_hero_info(room_t *proom, solo_t *solo, design_robot_t *robot)
{
	evil_hero_t hero_list[2];
	if (proom->hp1 == 0 && proom->energy1 == 0) {
		get_hero_info(&hero_list[0], proom->guest[0], proom->deck[0]);
		if (solo != NULL && solo->hp1 != 0) {
			// if solo set myhero_hp, use that
			hero_list[0].hp = solo->hp1;
		}
		proom->hp1 = hero_list[0].hp;
		proom->energy1 = hero_list[0].energy;

	}

	if (proom->hp2 == 0 && proom->energy2 == 0) {
		get_hero_info(&hero_list[1], proom->guest[1], proom->deck[1]);
		if (proom->guest[1] <= MAX_AI_EID) {
			if (solo != NULL) {
				hero_list[1].hp = solo->hp2;
			}
			if (robot != NULL) {
				hero_list[1].hp = robot->hp;
				hero_list[1].energy = robot->energy;
			}
		}
		proom->hp2 = hero_list[1].hp;
		proom->energy2 = hero_list[1].energy;
	}

	INFO_PRINT(0, "room_set_hero_info:hp1=%d hp2=%d energy1=%d energy2=%d", proom->hp1, proom->hp2, proom->energy1, proom->energy2);
	return 0;
}

/**
 * given: 
 * proom->num_guest >= 2
 * proom->guest[0], [1] contains valid eid which is online
 * (online: 
 * 
 * fill up:
 * proom->seed
 * proom->deck[0], [1]
 * 
 */
int game_init(room_t *proom, int seed, int first_player)
{
	int ret;
	if (proom->deck[0][0] == 0) {
		int eid0 = proom->guest[0];
		// this warning is important, after we implement out_game()
		ERROR_RETURN(-7, "game_init_load_deck_0 eid=%d", eid0);
	}
	// DEBUG_PRINT(0, "game_init_deck0 = [%s]", proom->deck[0]);

	if (proom->deck[1][0] == 0) {
		int eid1 = proom->guest[1];
		ERROR_RETURN(-17, "game_init_load_deck_1 eid=%d", eid1);
	}
	// DEBUG_PRINT(0, "game_init_deck1 = [%s]", proom->deck[1]);

	if (proom->lua != NULL) {
		BUG_PRINT(-33, "game_init_lua_null");
		return -33;
	}
	// assert(proom->lua == NULL); // @see room_clean 

	// TODO init: gameid, state (now in out_game)
	// reset both hero hp and energy in room
	// room_set_hero_info(proom, NULL);
	INFO_PRINT(0, "game_init:hero1 hp[%d] energy[%d], hero2 hp[%d] energy[%d]"
	, proom->hp1, proom->energy1, proom->hp2, proom->energy2);

	// init start_side

	// create lua VM
	proom->lua = luaL_newstate();
	assert(proom->lua != NULL);
	luaL_openlibs(proom->lua);
	lua_pushnumber(proom->lua, 1974);  // magic number @see room_clean

	lu_set_int(proom->lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(proom->lua, "res/lua/logic.lua");

	BUG_PRINT(proom->gameid==0, "game_init:gameid=0");

	// proom->seed = (seed > 0) ? seed : 1 + (proom->gameid % 100000); 
	proom->seed = (seed > 0) ? seed : (abs(random()) % 100000);

	// reset seed for start side in match game
	// 1. first_player is 0, seed % 2 should be 1
	// 2. first_plyaer is 1, seed % 2 should be 0
	if (proom->game_type == GAME_MATCH) {
		if ((first_player == 0 && proom->seed % 2 == 0)
		|| (first_player == 1 && proom->seed % 2 == 1)) {
			proom->seed += 1;
		}
	}
	// peter: fixed start_side = odd(1)/even(2) number of seed
	proom->start_side = 2 - (proom->seed % 2); // abs(random()) % 2 + 1;
	INFO_PRINT(0, "game_init:proom->start_side=%d seed=%d", proom->start_side, proom->seed);

	ret = lu_logic_init(proom->lua, proom->deck[0], proom->deck[1]
	, proom->seed, proom->start_side, proom->hp1, proom->hp2, proom->energy1, proom->energy2);
	// TODO we need to clean up proom->lua
	if (ret < 0) {
		lua_close(proom->lua);
		proom->lua = NULL;
		proom->seed = 0; // resume to zero
		ERROR_RETURN(-66, "game_lu_logic_init %d", ret);
	}

#ifdef HCARD
    ret = lu_get_world(proom->lua, proom->world);
    if (ret < 0) {
        lua_close(proom->lua);
        proom->lua = NULL;
        proom->seed = 0; // resume to zero
        ERROR_RETURN(-76, "game_init:get_world %d", ret);
    }
#endif

	proom->start_time = time(NULL);

	proom->game_timeout = time(NULL) + g_design->constant.max_timeout; // force-next

	return 0;
}

int gate_init(room_t *proom, int seed, int first_player)
{
	int ret;
	if (proom->deck[0][0] == 0) {
		int eid0 = proom->guest[0];
		// this warning is important, after we implement out_game()
		ERROR_RETURN(-7, "game_init_load_deck_0 eid=%d", eid0);
	}
	// DEBUG_PRINT(0, "game_init_deck0 = [%s]", proom->deck[0]);


	if (proom->lua != NULL) {
		BUG_PRINT(-33, "game_init_lua_null");
		return -33;
	}
	// assert(proom->lua == NULL); // @see room_clean 

	// TODO init: gameid, state (now in out_game)

	// init start_side

	// create lua VM
	proom->lua = luaL_newstate();
	assert(proom->lua != NULL);
	luaL_openlibs(proom->lua);
	lua_pushnumber(proom->lua, 1974);  // magic number @see room_clean

	lu_set_int(proom->lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(proom->lua, "res/lua/logic.lua");

	BUG_PRINT(proom->gameid==0, "gate_init:gameid=0");

	// proom->seed = (seed > 0) ? seed : 1 + (proom->gameid % 100000); 
	proom->seed = (seed > 0) ? seed : (abs(random()) % 100000);

	// peter: fixed start_side = odd(1)/even(2) number of seed
	proom->start_side = 2 - (proom->seed % 2); // abs(random()) % 2 + 1;
	INFO_PRINT(0, "gate_init:proom->start_side=%d seed=%d", proom->start_side, proom->seed);
	ret = lu_gate_init(proom->lua, proom->deck[0], proom->deck[1]
	, proom->seed);
	// TODO we need to clean up proom->lua
	if (ret < 0) {
		lua_close(proom->lua);
		proom->lua = NULL;
		proom->seed = 0; // resume to zero
		ERROR_RETURN(-66, "game_lu_logic_init %d", ret);
	}

	proom->start_time = time(NULL);

	proom->game_timeout = time(NULL) + g_design->constant.max_timeout; // force-next

	return 0;
}

int solo_plus_init(room_t *proom, int seed)
{
	int ret;

	if (proom->deck[0][0] == 0) {
		int eid0 = proom->guest[0];
		// this warning is important, after we implement out_game()
		ERROR_RETURN(-7, "solo_plus_init:load_deck_0 eid=%d", eid0);
	}

	if (proom->lua != NULL) {
		BUG_PRINT(-33, "solo_plus_init:lua_null");
		return -33;
	}
	// assert(proom->lua == NULL); // @see room_clean 

	// create lua VM
	proom->lua = luaL_newstate();
	assert(proom->lua != NULL);
	luaL_openlibs(proom->lua);
	lua_pushnumber(proom->lua, 1974);  // magic number @see room_clean

	lu_set_int(proom->lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(proom->lua, "res/lua/logic.lua");

	BUG_PRINT(proom->gameid==0, "solo_plus_init:gameid=0");

	proom->seed = (seed > 0) ? seed : (abs(random()) % 100000);
	if (((proom->seed % 2 == 0) && (proom->solo_start_side == 1))
	|| ((proom->seed % 2 == 1) && (proom->solo_start_side == 2))) {
		proom->seed++;
	}
//	if ((proom->seed + proom->solo_start_side) % 2 == 1) {
//		proom->seed++;
//	}

	proom->start_side = 2 - (proom->seed % 2); // abs(random()) % 2 + 1;
	INFO_PRINT(0, "solo_plus_init:proom->start_side=%d seed=%d", proom->start_side, proom->seed);


	ret = lu_solo_plus_init(proom->lua, proom->deck[0], proom->deck[1], proom->game_flag
	, proom->ai_max_ally, proom->hp2, proom->hp1, proom->energy1, proom->type_list, proom->seed);
	// TODO we need to clean up proom->lua
	if (ret < 0) {
		lua_close(proom->lua);
		proom->lua = NULL;
		proom->seed = 0; // resume to zero
		ERROR_RETURN(-66, "solo_plus_init:lu_logic_init %d", ret);
	}

	proom->start_time = time(NULL);

	proom->game_timeout = time(NULL) + g_design->constant.max_timeout; // force-next
	
	return 0;
}

// useless, remove later
/*
int robot_game_init(room_t *proom, int seed)
{
	int ret;
	if (proom->deck[0][0] == 0) {
		int eid0 = proom->guest[0];
		// this warning is important, after we implement out_game()
		ERROR_RETURN(-7, "robot_game_init:load_deck_0 eid=%d", eid0);
	}

	if (proom->lua != NULL) {
		BUG_PRINT(-33, "robot_game_init:lua_null");
		return -33;
	}
	// assert(proom->lua == NULL); // @see room_clean 

	// create lua VM
	proom->lua = luaL_newstate();
	assert(proom->lua != NULL);
	luaL_openlibs(proom->lua);
	lua_pushnumber(proom->lua, 1974);  // magic number @see room_clean

	lu_set_int(proom->lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(proom->lua, "res/lua/logic.lua");

	BUG_PRINT(proom->gameid==0, "robot_game_init:gameid=0");

	proom->seed = (seed > 0) ? seed : (abs(random()) % 100000);

	proom->start_side = 2 - (proom->seed % 2); // abs(random()) % 2 + 1;
	INFO_PRINT(0, "robot_game_init:proom->start_side=%d seed=%d", proom->start_side, proom->seed);
	ret = lu_robot_game_init(proom->lua, proom->deck[0], proom->deck[1], proom->seed);
	// TODO we need to clean up proom->lua
	if (ret < 0) {
		lua_close(proom->lua);
		proom->lua = NULL;
		proom->seed = 0; // resume to zero
		ERROR_RETURN(-66, "robot_game_init:lu_logic_init %d", ret);
	}

	proom->start_time = time(NULL);

	proom->game_timeout = time(NULL) + g_design->constant.max_timeout; // force-next

	return 0;
}
*/

int tower_init(room_t *proom, int seed, int hp, int res, int energy)
{
	int ret;
	if (proom->deck[0][0] == 0) {
		int eid0 = proom->guest[0];
		// this warning is important, after we implement out_game()
		ERROR_RETURN(-7, "tower_init:load_deck_0 eid=%d", eid0);
	}
	// DEBUG_PRINT(0, "game_init_deck0 = [%s]", proom->deck[0]);


	if (proom->lua != NULL) {
		BUG_PRINT(-33, "tower_init:lua_null");
		return -33;
	}
	// assert(proom->lua == NULL); // @see room_clean 

	// TODO init: gameid, state (now in out_game)

	// init start_side

	// create lua VM
	proom->lua = luaL_newstate();
	assert(proom->lua != NULL);
	luaL_openlibs(proom->lua);
	lua_pushnumber(proom->lua, 1974);  // magic number @see room_clean

	lu_set_int(proom->lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(proom->lua, "res/lua/logic.lua");

	BUG_PRINT(proom->gameid==0, "tower_init:gameid=0");

	// proom->seed = (seed > 0) ? seed : 1 + (proom->gameid % 100000); 
	proom->seed = (seed > 0) ? seed : (abs(random()) % 100000);

	// peter: fixed start_side = odd(1)/even(2) number of seed
	proom->start_side = 2 - (proom->seed % 2); // abs(random()) % 2 + 1;
	INFO_PRINT(0, "tower_init:proom->start_side=%d seed=%d", proom->start_side, proom->seed);
	ret = lu_tower_init(proom->lua, proom->deck[0], proom->deck[1]
	, proom->seed, proom->start_side, hp, res, energy);
	// TODO we need to clean up proom->lua
	if (ret < 0) {
		lua_close(proom->lua);
		proom->lua = NULL;
		proom->seed = 0; // resume to zero
		ERROR_RETURN(-66, "tower_init:lu_logic_init %d", ret);
	}

	proom->start_time = time(NULL);

	proom->game_timeout = time(NULL) + g_design->constant.max_timeout; // force-next

	return 0;
}

/**
 * input:
 * proom != NULL (assumed ok)
 * proom->seed >= 0
 * proom->deck[0], [1] ready
 * proom->num_guest >= 2
 * proom->guest[0], [1] ready, valid eid, better online (can be offline?)
 * 
 * output:
 * net broadcast following to guest[*] eid (get_conn_by_eid)
 * "game [side=1|2|99] [cmd_size=0] [seed] [deck_400_0] [deck_400_1]"
 * side=1 is for guest[0],   side=2 for guest[1], side=99 for watcher
 * 
 * TODO it is used by quick_room() only, can be extended to use by
 * other function, e.g. room_game(), but we need to 
 * 
 * obsolete by room_info_broadcast() pls keept this, do not delete!
 */
int game_broadcast(room_t *proom)
{
	assert(proom != NULL);  // TODO if check
	assert(proom->num_guest >= 2);
	assert(proom->state == ST_GAME);  // this is ???

	char deckbuff[1000]; // 400 + 400 + something
	// later: do a random  (I am master)
	// game 1 0 seed [deck_400_0] [deck_400_1]
	int cmd_size = (int)proom->cmd_list.size();
	assert(cmd_size == 0);  // TODO remove
	sprintf(deckbuff, "%d %d %d %s %s", cmd_size, proom->seed
	, proom->start_side, proom->deck[0], proom->deck[1]);

	int offline_count = 0;
	for (int i=0; i<proom->num_guest; i++) {
		int side = i + 1;
		if (side > 2) { side = 99; } // watcher
		connect_t *conn = get_conn_by_eid( proom->guest[i]);
		if (NULL == conn) {
			DEBUG_PRINT(proom->guest[i], "game_broadcast_offline_eid");
			offline_count++;
		}
		net_write_space(conn, "game %d", side);
		net_write(conn, deckbuff, '\n');
		conn->st = ST_GAME; // core logic 
	}

	DEBUG_PRINT(offline_count, "game_broadcast_offline_count");

	// shall we send cmd_list ?
	// no, we have assert cmd_size == 0 already!

	return 0;
}


// XXX useless now
// "game"  require:
// 1. conn->room != NULL
// 2. conn->euser.eid == conn->room->guest[0]   (I am master)
// 3. conn->room->num_guest >= 2  (master + 1 guest at least)
// @return:  game [side] [seed] [deck_400_0] [deck_400_1]
// usually master.side = 1 and guest.side = 2
// also broadcast to rest of guest for a "read-only" game
// start 99 means you are read-only guest
/*
int room_game(connect_t *conn, const char *cmd, const char * buffer) 
{
	DEBUG_PRINT(0, "room_game");
	int ret;
	if (conn->room == NULL) {
		// NET_ERROR_RETURN(conn, -3, "game_no_room");
		NET_ERROR_RETURN(conn, -3, "%s", E_ROOM_GAME_NULL_ROOM);
	}
	// implicit:  conn->room is non-null
	room_t * proom = conn->room;

	int eid = get_eid(conn);
	if (eid != proom->guest[0]) {
		// NET_ERROR_RETURN(conn, -6, "game_only_master_can_start");
		NET_ERROR_RETURN(conn, -6, "%s", E_ROOM_GAME_ONLY_MASTER_START);
	}
	if (proom->num_guest < 2) {
		// NET_ERROR_RETURN(conn, -2, "game_less_guest");
		NET_ERROR_RETURN(conn, -2, "%s", E_ROOM_GAME_LESS_PLAYER);
	}

	// check opponent guest[1] is online
	connect_t *oppo_conn = get_conn_by_eid(proom->guest[1]);
	if (oppo_conn == NULL) {
		// NET_ERROR_RETURN(conn, -22, "game_opponent_offline");
		NET_ERROR_RETURN(conn, -22, "%s", E_ROOM_GAME_OPPO_OFFLINE);
	}

	if (proom->state==ST_GAME) {
		// TODO this is ginfo ?
		// NET_ERROR_RETURN(conn, -16, "game_already_started");
		NET_ERROR_RETURN(conn, -16, "%s", E_ROOM_GAME_ALREADY_START);
	}


	// fill up rating  @see win_game for usage
	proom->rating[0] = conn->euser.rating;
	proom->rating[1] = oppo_conn->euser.rating;

	ret = game_init(proom, 0, 0);
	if (ret < 0) {
		// NET_ERROR_RETURN(conn, ret, "game_init_error");
		NET_ERROR_RETURN(conn, ret, "%s", E_ROOM_GAME_INIT_ERROR);
	}


	// conn->room->guest[0] : side = 1  // this conn 
	// conn->room->guest[1] : side = 2

	char deckbuff[1000]; // 400 + 400 + something
	// later: do a random  (I am master)
	// game 1 0 seed [deck_400_0] [deck_400_1]
	sprintf(deckbuff, "%d %d %d %s %s", 0, proom->seed
	, proom->start_side, proom->deck[0], proom->deck[1]);
	net_write(conn, "game 1", ' '); 
	net_write(conn, deckbuff, '\n'); // faster than net_writeln
	conn->st = ST_GAME;

	// guest[1] is the opponent, and the rest of guests are watchers
	connect_t* guest_conn = get_conn_by_eid( proom->guest[1]);
	// get_conn( conn->room->guest[1]);
	guest_conn->st = ST_GAME;  // let the guest[1] play card
	net_write(guest_conn, "game 2", ' ');
	net_write(guest_conn, deckbuff, '\n'); // faster than net_writeln
	proom->state = ST_GAME;   // @see sys_login for re-conn

	// watcher broadcast
	int i;
	for (i=2; i<conn->room->num_guest; i++) {
		guest_conn = get_conn_by_eid( proom->guest[i] );
		if (guest_conn == NULL) {
			// some guest may temporary disconnect, skip it
			WARN_PRINT(-1, "guest(eid=%d) is disconnect", proom->guest[i]);
			continue;
		}
		if (guest_conn->room != proom) {
			BUG_PRINT(-7, "guest_room_mismatch");
		}
		// get_conn(conn->room->guest[i]);
		net_write(guest_conn, "game 99", ' ');
		net_write(guest_conn, deckbuff, '\n'); // faster than net_writeln

		// peter: need to upgrade the watcher to ST_GAME
		// such that re-conn logic is working!
		guest_conn->st = ST_GAME;
	}

	return ST_GAME;
}
*/

int game_reconn(connect_t *conn, const char *cmd, const char * buffer) 
{
	DEBUG_PRINT(0, "game_reconn");
	int ret;
	int index;
	int total;

	if (conn->room == NULL) {
		NET_ERROR_RETURN(conn, -3, "game_no_room");
	}
	// implicit:  conn->room is non-null
	room_t * proom = conn->room;
	int cmd_size = (int)proom->cmd_list.size();
	total = 0;

	ret = sscanf(buffer, "%d", &index);
	printf("game_reconn:index=%d  (cmd_size=%d)\n", index, cmd_size);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "game_reconn:invalid_input");
		// TODO : send ginfo, refresh game
	}

	if (index < 0 || index > cmd_size) {
		// client index out bound, server should send ginfo to client to refresh game
		ret = game_info(conn, "ginfo", "");
		if (ret < 0) {
			ERROR_RETURN(-8, "game_reconn:send_ginfo_error");
		}
		return ret;
	}


	if (index == cmd_size) {
		net_writeln(conn, "%s %d", cmd, total);
		return ST_GAME;
	}

	for (int i=index; i<(int)proom->cmd_list.size(); i++) {
		net_write(conn, proom->cmd_list[i].c_str(), '\n');
	}

	return ST_GAME;
}




#define RATING_LEVEL	3

// immediately match
const double rating_diff[RATING_LEVEL] = {
	100
,	300
,	500  // actually this is useless
};

// waiting second 
const int rating_time[RATING_LEVEL] = {
	0
,	0
,	0
};


int rating_diff_time(double diff)
{
	diff = fabs(diff);  // make sure it is +ve
	for (int i=0; i<RATING_LEVEL; i++) {
		if (diff <= rating_diff[i]) {
			return rating_time[i];
		}
	}
	// last rating_time is always the max
	return rating_time[RATING_LEVEL-1];
}


int quick_del(int eid)
{
	int flag = -1;
	if (eid <= 0) {
		// WARN_PRINT(-3, "quick_del:eid_negative %d", eid);
		return flag;
	}
	QUICK_LIST::iterator it;
	// note: this for 
	it=g_quick_list.begin(); 
	while ( it<g_quick_list.end() ) {
		quick_t &qq = *it;
		if (qq.eid == eid) {
			it = g_quick_list.erase(it);
			flag ++;  // it may become 0, 1, 2...
			continue;
		}
		it++;  // must have!!
	}
	if (flag > 0) {
		// this is buggy!  flag should be either 0 or -1
		BUG_PRINT(flag, "quick_del_count eid=%d", eid);
	}
	return flag;
}

int fight_del(QUICK_LIST *list, int eid)
{
	int flag = -1;
	if (eid <= 0) {
		// WARN_PRINT(-3, "fight_del:eid_negative %d", eid);
		return flag;
	}
	QUICK_LIST::iterator it;
	// note: this for 
	it=list->begin(); 
	while ( it<list->end() ) {
		quick_t &qq = *it;
		if (qq.eid == eid) {
			it = list->erase(it);
			flag ++;  // it may become 0, 1, 2...
			continue;
		}
		it++;  // must have!!
	}
	if (flag > 0) {
		// this is buggy!  flag should be either 0 or -1
		BUG_PRINT(flag, "fight_del_count eid=%d", eid);
	}
	return flag;
}


int quick_check(int eid)
{
	QUICK_LIST::iterator it;
	
	for (it=g_quick_list.begin(); it<g_quick_list.end(); it++) {
		quick_t &qq = *it;
		if (qq.eid == eid) {
			return 1;
		}
	}
	return 0;
}

// how to avoid duplicate eid add ?
int quick_add(evil_user_t &euser, time_t now)
{
	if (quick_check(euser.eid)) {
		return -2;
	}
	quick_t quick;
	quick.eid = euser.eid;
	quick.rating = euser.rating;
	quick.start_time = now;
	g_quick_list.push_back(quick);
	return 0;
}


int get_random_fakeman();
/**
 * fill up pair_list with match up eid1, eid2,
 * return number of pair matched
 */
 // TODO put this into ttt.cpp for in-depth testing
 // quick_match( vector ... *pair_list, time_t now)
int quick_match(vector< pair<int, int> > &pair_list, time_t now) 
{
	// TODO this should be a separate function
	QUICK_LIST::iterator it1, it2;
	// TODO change to use rbegin(), rend(), delete is from the back
	for (it1=g_quick_list.begin(); it1 < g_quick_list.end(); it1++) {
		quick_t &q1 = *it1;
		if (q1.eid==0) { continue; }  // ??? is it ok?
		int wait1 = now - q1.start_time;
		if (wait1 < 0) {  // original assert wait1 >= 0
			ERROR_PRINT(-7, "quick_match_wait1_negative");
			continue; 
		}  
		if ( g_design->constant.quick_robot_flag && wait1 >= QUICK_AUTO_ROBOT_TIME) {
			// quick with a robot
			int ai_eid = get_random_fakeman();
			pair_list.push_back( make_pair(q1.eid, ai_eid) );
			q1.eid = 0;
			continue;	
		}
		for (it2=it1+1; it2 < g_quick_list.end(); it2++) {
			quick_t &q2 = *it2;
			if (q1.eid==0) { break; } // this break to outter loop
			if (q2.eid==0) { continue; }
			if (q1.eid == q2.eid) { // this is funny
				q1.eid = 0;  q2.eid = 0; // erase them
				BUG_PRINT(-27, "quick_match_same_eid %d", q1.eid);
				continue;
			}
			// check q1, q2 already in room
			// TODO
			int wait2 = now - q2.start_time;
			if (wait2 < 0) {  // original assert
				ERROR_PRINT(-17, "quick_match_wait2_negative");
				continue; 
			}
			int dt = rating_diff_time(q2.rating - q1.rating);

			// de morgan theorem
			// was: if (wait1 >= dt || wait2 >= dt) do positive
			if ( wait1<dt && wait2<dt) {
				continue;  // rating difference too large, wait more
			}

			// ok, we find a match: q1 and q2 , put them in pair_list
			pair_list.push_back( make_pair(q1.eid, q2.eid) );
			// DEBUG_PRINT(0, "pair matched: (%d %d)", q1.eid, q2.eid);
			// set zero to replace erase
			q1.eid = 0;
			q2.eid = 0;
			break;  // break out of inner for loop, to outter loop
		}

	}

	// erase those zero eid : note do not put it1++ inside for(...)
	for (it1=g_quick_list.begin(); it1 < g_quick_list.end(); ) {
		quick_t &qq = *it1;  // ref
		if (qq.eid == 0) {
			it1 = g_quick_list.erase(it1);
		} else {
			it1++;
		}
	}

	return pair_list.size();
}


/**
 * for each pair(eid1,eid2) in pair_list
 *      create a room with num_guest=2 and start game
 *		also broadcast the game info to all guests
 * end
 */
int quick_room(vector< pair<int,int> > &pair_list)
{
	int ret;
	int err_count = 0;
	
	// TODO use iterator may be faster?
	for (unsigned int i=0; i<pair_list.size(); i++) {
		// TODO create a room, start game!
		// @see room_create
		// @see room_game
		int eid1, eid2;
		connect_t *conn1, *conn2;
		design_robot_t *robot = NULL;
		eid1 = pair_list[i].first;
		eid2 = pair_list[i].second;
		if (eid2 > MAX_AI_EID) {
			conn1 = get_conn_by_eid(eid1);
			conn2 = get_conn_by_eid(eid2);
			// XXX TODO let the other component back to queue 
			if (NULL==conn1 || NULL==conn2) { // means it is offline
				const char *errstr = "quick -26 opponent_offline";
				if (conn1!=NULL) { net_writeln(conn1, errstr); }
				if (conn2!=NULL) { net_writeln(conn2, errstr); }
				continue; // skip this pair
			}
			if (conn1->room != NULL || conn2->room != NULL) {
				const char *errstr = "quick -36 already_in_room";
				if (conn1!=NULL) { net_writeln(conn1, errstr); }
				if (conn2!=NULL) { net_writeln(conn2, errstr); }
				continue; // skip this pair
			}
		} else {
			// eid2 is robot
			conn1 = get_conn_by_eid(eid1);
			if (NULL==conn1) {
				continue; // skip this pair
			}
			if (conn1->room != NULL) {
				const char *errstr = "quick -36 already_in_room";
				net_writeln(conn1, errstr);
				continue; // skip this pair
			}

			if (eid2 > g_design->design_fakeman_size || eid2 <= 0) {
				BUG_PRINT(-6, "quick_room:fakeman_id_out_bound %d", eid2);
				continue; //skip this pair
			}
			robot = g_design->design_fakeman_list+eid2;
			if (robot == NULL) {
				BUG_PRINT(-3, "quick_room:robot_null %d", eid2);
				continue; //skip this pair
			}
		}

		room_t *proom;
		proom = new_room(CHANNEL_QUICK);
		proom->gameid = get_gameid();
		proom->state = ST_GAME; // make it a game!
		proom->game_type = GAME_QUICK;

		proom->num_guest = 2;
		proom->guest[0] = eid1; // pair_list[i].first;
		proom->guest[1] = eid2; // pair_list[i].second;
		proom->rating[0] = conn1->euser.rating;
		strcpy(proom->alias[0], conn1->euser.alias);
		proom->icon[0] = conn1->euser.icon;
		proom->lv[0] = conn1->euser.lv;
		sprintf(proom->deck[0], "%.400s", conn1->euser.deck);

		if (eid2 > MAX_AI_EID) {
			proom->rating[1] = conn2->euser.rating;
			strcpy(proom->alias[1], conn2->euser.alias);
			proom->icon[1] = conn2->euser.icon;
			proom->lv[1] = conn2->euser.lv;
			sprintf(proom->deck[1], "%.400s", conn2->euser.deck);
			sprintf(proom->title, "%s~VS~%s", conn1->euser.alias, conn2->euser.alias);
		} else {
			proom->rating[1] = robot->rating; // hard coded for solo
			strcpy(proom->alias[1], robot->alias);
			proom->icon[1] = robot->icon; 
			proom->lv[1] = robot->lv; 
			sprintf(proom->deck[1], "%.400s", robot->deck);
			sprintf(proom->title, "%s~VS~%s", proom->alias[0], proom->alias[1]);
		}

		room_set_hero_info(proom, NULL, robot);
		ret = game_init(proom, 0, 0); // init proom->deck
		if (ret < 0) {
			const char *errstr = "quick -18 subfun_err %d";
			// TODO how to 
			if (conn1!=NULL) { net_writeln(conn1, errstr, ret); }
			if (conn2!=NULL) { net_writeln(conn2, errstr, ret); }
			ret = free_room(proom->channel, proom);  // order is important
			err_count--;
			continue;
		}
		// note: order is important, it must be after game_init()
		conn1->room = proom;
		conn1->st = proom->state;
		g_user_room[eid1] = proom;

		if (eid2 > MAX_AI_EID) {
			conn2->room = proom;
			conn2->st = proom->state;
			g_user_room[eid2] = proom;
		}

		room_info_broadcast(proom, 0); // 0 means all
		INFO_PRINT(0, "room_title=%s (%d vs %d)", proom->title, proom->guest[0], proom->guest[1]);
	}
	
	return err_count; // normally it is 0
}

int broadcast_quick()
{
	int ret;
	char msg[BUFFER_SIZE + 1];
	sprintf(msg, "quickmsg %s", QUICK_BROADCAST_MSG);
	ret = wchat_add(msg); 

	if (ret != 0) {
		ERROR_RETURN(ret, "broadcast_quick:wchat_add_error");
	}
	return ret;
}

int auto_quick_match(time_t now)
{
	static time_t last_quick_match = 0;
	// note: must be a bit smaller than WATCH_TIMEOUT 
	if (now - last_quick_match <= 5) {
		return 0;
	}
	// every 5 secs
	// DEBUG_PRINT(0, "auto_quick_match %ld", now);
	vector< pair<int,int> > pair_list;
	last_quick_match = now;
	quick_match( pair_list, now);  
	quick_room( pair_list );

	// every 30s broadcast quick message when have player in g_quick_list
	static time_t last_quick_broadcast = 0;
	if (now - last_quick_broadcast <= 30 || g_quick_list.size() == 0) {
		return 0;
	}
	last_quick_broadcast = now;
	broadcast_quick();

	return 0;
}

// create a room and start a game with robot
// XXX note: DO NOT USE conn, conn may==NULL
int out_fight_robot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int len;
	int game_type;
	int eid1;
	int eid2;
	char deck[EVIL_CARD_MAX+1];
	room_t *proom;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		ERROR_RETURN(-6, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %d %400s", &game_type, &eid1, &eid2, deck);
	if (ret != 4) {
		ERROR_RETURN(-55, "out_fight_robot:sscanf %d", ret);
	}

	connect_t * player_conn = get_conn_by_eid(eid1);
	if (player_conn == NULL) {
		return 0;
	}

	evil_user_t * puser = &player_conn->euser;
	if (puser->eid <= 0) {
		return 0;
	}

	if (player_conn->room != NULL) {
		return 0;
	}

	len = sprintf(puser->deck, "%.400s", deck);
	if (len != 400) {
		BUG_PRINT(-7, "out_fight_robot:deck_len=%d", len);
	}

	design_robot_t *robot;
	if (game_type == GAME_SOLO_FREE || game_type == GAME_SOLO_GOLD) {
		if (eid2 > g_design->design_robot_size || eid2 <= 0) {
			BUG_PRINT(-6, "out_fight_robot:robot_id_out_bound %d", eid2);
			return 0;
		}
		robot = g_design->design_robot_list+eid2;
	} else {
		if (eid2 > g_design->design_fakeman_size || eid2 <= 0) {
			BUG_PRINT(-6, "out_fight_robot:fakeman_id_out_bound %d", eid2);
			return 0;
		}
		robot = g_design->design_fakeman_list+eid2;
	}

	// now we get a player
	if (robot == NULL) {
		BUG_PRINT(-5, "out_fight_robot:ai_null %d", eid2);
		return 0;
	}

	DEBUG_PRINT(0, "out_fight_robot:alias=[%s] deck[%.400s] eid2=%d", robot->alias, robot->deck, eid2);

	int robot_hero = 0;
	sscanf(robot->deck, "%d", &robot_hero);

	// create fight robot room
	proom = new_room(CHANNEL_QUICK);
	proom->num_guest = 2;
	proom->guest[0] = eid1;
	proom->guest[1] = eid2;  // hard coded TODO check evil_user,deck eid=1
	proom->rating[0] = puser->rating;
	proom->rating[1] = robot->rating; // hard coded for solo
	strcpy(proom->alias[0], puser->alias);
	strcpy(proom->alias[1], robot->alias);
	proom->icon[0] = puser->icon;
	proom->icon[1] = robot->icon; 
	proom->lv[0] = puser->lv;
	proom->lv[1] = robot->lv; 
	proom->game_type = game_type;
	sprintf(proom->title, "%s~VS~%s", proom->alias[0], proom->alias[1]);
	proom->seed = 0; 
	// proom->solo_hero = robot_hero;

	// WARN_PRINT(-1, "NOT warning: dbin_solo room title: %s", proom->title);
	player_conn->room = proom;  // for out_game() 
	g_user_room[eid1] = proom; // re-conn logic
	// eid2 is always AI, so we don't need to assign to g_user_room[eid]

	// @see out_game()
	proom->gameid = get_gameid();
	proom->state = ST_GAME;
	sprintf(proom->deck[0], "%.400s", deck);
	sprintf(proom->deck[1], "%.400s", robot->deck);

	room_set_hero_info(proom, NULL);
	ret = game_init(proom, proom->seed, 0);
	if (ret < 0) {
		NET_ERROR_PRINT(player_conn, -66, "out_fight_robot:game_init %d", ret);
		ret = -66;  // order is important, need to retain 'ret'
		goto error_game;
	}
	player_conn->st = ST_GAME; // set me as GAME state
	room_info_broadcast(proom, 0); // 0 means all
	return 0;

error_game:
	if (proom != NULL) {
		free_room(proom->channel, proom);  // order is important
	}
	return ret; // not yet
}


int get_random_robot()
{
	int r = abs(random()) % g_design->design_robot_size + 1;
	INFO_PRINT(0, "get_random_robot:r=%d", r);
	design_robot_t *robot = g_design->design_robot_list + r;
	if (robot->id <= 0) {
		BUG_PRINT(-6, "get_random_robot:robot_null %d", r);
		return 1;
	}
	return r;
}

int get_random_fakeman()
{
	int r = abs(random()) % g_design->design_fakeman_size + 1;
	INFO_PRINT(0, "get_random_fakeman:r=%d", r);
	design_robot_t *fakeman = g_design->design_fakeman_list + r;
	if (fakeman->id <= 0) {
		BUG_PRINT(-6, "get_random_fakeman:fakeman_null %d", r);
		return 1;
	}
	return r;
}

// new auto fight with robot logic
int auto_fight_robot(QUICK_LIST *fight_list, int game_type)
{
	time_t now = time(NULL);
	QUICK_LIST::iterator it;
	for (it=fight_list->begin(); it<fight_list->end(); it++) {
		quick_t &q = *it;
		if (q.eid==0) {continue;}
		if (now - q.start_time < FIGHT_AUTO_ROBOT_TIME) {continue;}
		DEBUG_PRINT(0, "auto_fight_ai:q.eid=%d", q.eid);
		// get a robot 
		int ai_eid = get_random_fakeman();
		// core logic
		dbin_write(NULL, "fight_robot", DB_FIGHT_ROBOT, "%d %d %d", game_type, q.eid, ai_eid);
		q.eid = 0;
	}

	// erase those zero eid : note do not put it1++ inside for(...)
	for (it=fight_list->begin(); it<fight_list->end(); ) {
		quick_t &q = *it; 
		if (q.eid == 0) {
			it = fight_list->erase(it);
		} else {
			it++;
		}
	}

	return 0;
}

int auto_fight(time_t now)
{
	static time_t last_quick_match = 0;
	// note: must be a bit smaller than WATCH_TIMEOUT 
	if (now - last_quick_match <= 1) {
		return 0;
	}

	auto_fight_robot(&g_fight_crystal_list, GAME_VS_CRYSTAL);
	auto_fight_robot(&g_fight_gold_list, GAME_VS_GOLD);
	auto_fight_robot(&g_fight_free_list, GAME_VS_FREE);

	return 0;
}


/**
 * quick
 * no parameter, require login, no room
 * TODO quick 9 = leave the quick_list queue, become LOGIN status
 * note: before or after auto_quick_match() is important!!!
 */
int quick_game(connect_t* conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "quick_not_login");
	}

	ret = check_deck(conn->euser.deck, conn->euser.card, conn->euser.eid, GAME_QUICK);
	if (ret < 0 ) {
		if (ret == E_RETURN_CHECK_DECK_OVER_MAX) {
			NET_ERROR_RETURN(conn, E_RETURN_CHECK_DECK_OVER_MAX, "%s", E_CHECK_DECK_OVER_MAX);
		}
		if (ret == E_RETURN_CHECK_DECK_LESS_MIN) {
			NET_ERROR_RETURN(conn, E_RETURN_CHECK_DECK_LESS_MIN, "%s", E_CHECK_DECK_LESS_MIN);
		}
		if (ret == E_RETURN_CHECK_DECK_LESS_QUICK_MIN) {
			NET_ERROR_RETURN(conn, E_RETURN_CHECK_DECK_LESS_QUICK_MIN, "%s", E_CHECK_DECK_LESS_QUICK_MIN);
		}
		// this is rather buggy
		NET_ERROR_RETURN(conn, -55, "out_quick:check_deck=%d", ret);
	}

	// usually 9 : means leave the quick list
	if (atoi(buffer) > 0)  {
		ret = quick_del(eid);
		if (ret != 0) {
			NET_ERROR_RETURN(conn, -96, "quick_del %d eid=%d", ret, eid);
		}
		// implicit else : return "quick 9", 9 means run away
		net_write(conn, "9", '\n');
		return 0;
	}

	room_t * proom = conn->room;
	if (NULL != proom) {
		NET_ERROR_RETURN(conn, -16, "quick_has_room");
	}

	time_t now = time(NULL);  // assume logic run very fast!

	ret = quick_add(conn->euser, now);
	WARN_PRINT(ret, "quick_add_duplicate OK");


	// normal case, give a zero
	// TODO add max_wait_time
	const time_t MAX_WAIT_TIME = 300;
	net_writeln(conn, "%s 0 %ld", cmd, MAX_WAIT_TIME);  // finish the quick command it


	// TODO quick_del when offline
	vector< pair<int, int> > pair_list;
	quick_match(pair_list, now);
	if (pair_list.size() > 0) {
		quick_room(pair_list);
		// TODO put it into room, game
	}


	return 0;
}




// 1 means it is in room, either guest[0] or guest[1]
// 0 menas it has no room, or conn is a watcher
// TODO : check g_current_side
int check_play(connect_t* conn, const char* cmd, const char* buffer) {
	if (conn->room == NULL) {
		net_writeln(conn, "gerr -3 play_no_room");
		ERROR_RETURN(-3, "play_no_room");
	}

	// implicit:  conn->room is non-null
	room_t * proom = conn->room;

	if (proom->num_guest < 2) {
		net_writeln(conn, "gerr -2 play_less_guest");
		ERROR_RETURN(-2, "play_less_guest");
	}

	int eid = get_eid(conn);

	// conn->id != conn->room->id && conn->id != conn->room->guest[1]) 
	if (eid != proom->guest[0] && eid != proom->guest[1]) {
		net_writeln(conn, "gerr -6 game_non_player");
		ERROR_RETURN(-6, "play_non_player");
	}
	// TODO check whether it is started ? level
	// impossible, since process_command() already checked
	if (conn->st != ST_GAME) {
		net_writeln(conn, "gerr -7 game_not_start %d", conn->st);
		ERROR_RETURN(-7, "play_not_start");
	}

	if (NULL == proom->lua) {
		net_writeln(conn, "gerr -13 play_lua_null"); 
		ERROR_RETURN(-13, "play_lua_null");
	}

	// core logic: for checking my_side = g_current_side
#ifdef HCARD
    int current_side = mylua_get_table_int(proom->lua, "g_logic_table", "current_side");
#else
	int current_side = lu_get_int(proom->lua, "g_current_side");
#endif
	int my_side;  // base on current connect_t

	// assume eid must be either guest[0] or guest[1]  (checked above)
	my_side = (eid == proom->guest[0]) ? 1 : 2;
	// same as below: (remove later)
//	if (eid == proom->guest[0]) 
//		my_side = 1;
//	else
//		my_side = 2;

	// f only valid when not my turn
	if (strcmp(cmd, "f") == 0){
		if (my_side == current_side) {
			net_writeln(conn, "gerr -16 f_play_is_my_turn"); 
			ERROR_RETURN(-16, "f_play_is_turn");
		}
		int remain_time = proom->game_timeout - time(NULL);
		// int remain_time = 0; //proom->game_timeout - time(NULL);
		if (remain_time > 0) {
			net_writeln(conn, "gerr -26 f_timeout_not_reach %d", remain_time); 
			ERROR_RETURN(-26, "f_timeout_not_reach");
		}
	}

	// not my side, cannot play card
	if (strcmp(cmd, "f") != 0 && my_side != current_side) {
		net_writeln(conn, "gerr -96 play_not_my_turn"); 
		ERROR_RETURN(-96, "play_not_my_turn eid=%d  cmd=%s", eid, buffer);

	}

	// ok, we can play the card
	return 0;
}


// win = {1 | 2}
// kfactor hard code 32 first
// return rating diff (+ for winner / - for loser)
double elo_rating(double rating1, double rating2, int win, double kfactor)
{
	double q1;
	double q2;

	// printf("ELO: rating1,2 = %lf  %lf\n", rating1, rating2);

    q1 = pow(10.0, rating1 / 400.0); // 10 ^ (...)
    q2 = pow(10.0, rating2 / 400.0);

    double base = q1 + q2;
// how small is small but not too small ?  try 6-8 zero digits
#define TOO_SMALL   (0.000000001f)
#define NOT_TOO_SMALL   (0.0000001f); 
	// avoid division by zero
    if (base <=TOO_SMALL && base>=-TOO_SMALL) {
        ERROR_PRINT(-7, "elo_div_zero");
        base = NOT_TOO_SMALL;
    }
    double e1 = q1 / base;
    double e2 = 1.0 - e1; // q2 / base;

	// note: rating1,2 are input/output parameters (reference)
	double diff;
    if (win == 1) {  // assert win == 1 or 2 ?
		diff = kfactor * e2; // (1.0 - e1);
//        rating1 = rating1 + kfactor * (1.0 - e1);
//        rating2 = rating2 + kfactor * (0.0 - e2);
    } else {
		diff = kfactor * e1; // (1.0 - e2);
//        rating1 = rating1 + kfactor * (0.0 - e1);
//        rating2 = rating2 + kfactor * (1.0 - e2);
	}
	// printf("ELO: diff = %lf\n", diff);
	return diff;
}

// return next_exp
int add_exp(connect_t *conn, int exp, int *lv_offset, int *this_exp) {
	
	int exp_next;
	evil_user_t *puser;
	*lv_offset = 0;
	*this_exp = 0;
	// int target_lv;
	int now_exp = 0;
	int now_lv = 0;
//	vector<int> &exp_list = g_design->exp_list;
	int (& exp_list)[MAX_LEVEL+1] = g_design->exp_list;

	if (conn == NULL) {
		ERROR_RETURN(-9, "add_exp:conn_null");
	}

	puser = &conn->euser;
	if (puser->eid <= 0) {
		ERROR_RETURN(-19, "add_exp:not_login");
	}

	now_exp = puser->exp;
	now_lv = puser->lv;

	now_exp += exp;
	// design: exp_list[3] = 200, exp_list[4] = 300, max_level = 4
	// 1.
	// puser->exp = 199; exp = 5; now_exp = 204; now_lv = 2;
	// after for loop, now_exp = 199 + 5 - 200 = 4, now_lv = 3
	// 2.
	// puser->exp = 199; exp = 305; now_exp = 504; now_lv = 2;
	// after for loop, now_exp = 199 + 305 - 200 - 300 = 4, now_lv = 4
	// 
	for (int lv = now_lv+1; lv <= g_design->max_level; lv++) {
		if (now_exp < exp_list[lv]) {
			break;
		}
		now_exp -= exp_list[lv];
		now_lv = lv;
	}

	if (now_lv >= g_design->max_level) {
		exp_next = exp_list[g_design->max_level];
		// avoid exp over max_level.exp
		if (now_exp > exp_next) {
			now_exp = exp_next;
		} 
	} else {
		exp_next = exp_list[now_lv+1];
	}


	*this_exp = 0;
	*lv_offset = now_lv - puser->lv;

	// set euser lv, exp
	puser->lv = now_lv;
	puser->exp = now_exp;

	/*
	// if lv > max_level, do not add exp any more
	// TODO now exp++ when lv > max_level
	if (puser->lv >= g_design->max_level) {
		exp_next = g_design->exp_list[g_design->max_level];
		*this_exp = g_design->exp_list[g_design->max_level];
		DEBUG_PRINT(0, "add_exp:level_max");
		return exp_next;
	} 

	target_lv = puser->lv;
	for (int i=puser->lv + 1; i<=g_design->max_level; i++) {
		exp_next = g_design->exp_list[i];
		if (puser->exp >= exp_next) {
			target_lv = i;
			// DEBUG_PRINT(0, "add_exp:level_up %d", puser->lv);
		} else {
			// DEBUG_PRINT(0, "add_exp:no_level_up");
			break;
		}
	}
	*lv_offset = target_lv - puser->lv;
	puser->lv = target_lv;

	if (puser->lv >= g_design->max_level) {
		*this_exp = g_design->exp_list[g_design->max_level];
	} else {
		*this_exp = g_design->exp_list[puser->lv];
	}
	*/

	return exp_next;
}

// player:win [side] [fold_flag] [rating] [gold] 
// 				[exp_offset] [lv] [next_exp] [player.exp] [card_id]
//				[crystal]
//				[guest_pos] [game_type]
// if game_type == GAME_QUICK
//				[MAX_QUICK_REWARD] [reward_info1] [reward_info2] ...
//				[reward_pos]
// reward_info = [reward] [count]
// if game_type == GAME_CHAPTER
//				[chapter_id] [stage_id]
//				[MAX_CHAPTER_TARGET] [target_info1] [target_info2] ...
//				[MAX_CHAPTER_REWARD] [reward_info1] [reward_info2] ...
//				[reward_pos]
// target_info = [target] [p1] [p2] [value] [done]
// reward_info = [reward] [count]
int send_win_str(char *str, room_t *proom, connect_t *pconn, int eid, int winner, int fold_flag, double rating, int gold, int crystal, int exp, int next_exp, int this_exp, int card_id, int chapter_reward_pos, chapter_result_t *chapter_result, int quick_reward_pos)
{
	evil_user_t *puser;
	char *ptr;

	int lv = 0;
	int now_exp = 0;
	int guest_pos = 9;

	if (pconn!=NULL) {
		puser = &pconn->euser;	
		if (puser->eid > 0) {
			lv = puser->lv;
			now_exp = puser->exp;

			for (int i=0; i<MAX_GUEST; i++) {
				if (puser->eid == proom->guest[i]) {
					guest_pos = i;
					break;
				}
			}
		}
	}
	
	ptr = str;
	ptr += sprintf(ptr, "win %d %d %lf %d %d %d %d %d %d %d %d %d"
	, winner, fold_flag, rating, gold
	, exp, lv, next_exp, now_exp, card_id, crystal
	, guest_pos, proom->game_type
	);

	if (proom->game_type == GAME_QUICK) {
		int pos = 0;
		if ((proom->guest[0] == eid && winner == 1) 
		|| (proom->guest[1] == eid && winner == 2)) {
			pos = 0;	
		} else {
			pos = 1;
		}

		ptr += sprintf(ptr, " %d", MAX_QUICK_REWARD);
		design_quick_reward_list_t *reward_list = g_design->quick_reward_list + pos;
		for (int i=0; i<MAX_QUICK_REWARD; i++) {
			design_quick_reward_t *preward = reward_list->reward_array + i;
			ptr += sprintf(ptr, " %d %d", preward->type, preward->count);
		}

		ptr += sprintf(ptr, " %d", quick_reward_pos);
	}

	if (proom->game_type == GAME_CHAPTER) {

		ptr += sprintf(ptr, " %d %d", proom->chapter_id, proom->stage_id);

		ptr += sprintf(ptr, " %d", MAX_SOLO_TARGET);
		// list chapter_result
		for (int i=0; i<MAX_SOLO_TARGET; i++) {
			ptr += sprintf(ptr, " %d %d %d %d %d"
			, chapter_result->target_list[i].target, chapter_result->target_list[i].p1
			, chapter_result->target_list[i].p2, chapter_result->value_list[i]
			, chapter_result->done_list[i]);
		}
		
		// list chapter_reward
		design_chapter_stage_t * stage = get_chapter_stage(proom->chapter_id, proom->stage_id);
		if (stage == NULL) {
			BUG_PRINT(-3, "send_win:stage_null %d %d", proom->chapter_id, proom->stage_id);
			// TODO handle null stage
			ptr += sprintf(ptr, " %d", 0);
			return -3;
		}

		ptr += sprintf(ptr, " %d", MAX_CHAPTER_REWARD);
		for (int i=0; i<MAX_CHAPTER_REWARD; i++) {
			design_chapter_reward_t *preward = stage->reward_list + i;
			ptr += sprintf(ptr, " %d %d", preward->reward, preward->count);
		}

		ptr += sprintf(ptr, " %d", chapter_reward_pos);
	}

	return 0;
}

int send_win(room_t *proom, connect_t *pconn, int eid, int winner, int fold_flag, double rating, int gold, int crystal, int exp, int next_exp, int this_exp, int card_id, int chapter_reward_pos, chapter_result_t *chapter_result, int quick_reward_pos)
{
	char str[500]; // may be larger
	bzero(str, sizeof(str));

	send_win_str(str, proom, pconn, eid, winner, fold_flag, rating // rate_offset1
	, gold, crystal, exp, next_exp, this_exp, card_id
	, chapter_reward_pos, chapter_result, quick_reward_pos);
	net_write(pconn, str, '\n');

	return 0;

	/*
	// if player disconnect, before he get the send_win info, ignore
	if (pconn==NULL) {
		return -3; // this is possible
	}
	puser = &pconn->euser;	
	if (puser->eid <= 0) {
		//
		net_writeln(pconn, "-7 not_login %d", puser->eid);
	}

	int guest_pos = -1;
	for (int i=0; i<MAX_GUEST; i++) {
		if (puser->eid == proom->guest[i]) {
			guest_pos = i;
			break;
		}
	}
	if (guest_pos < 0) {
		BUG_PRINT(-7, "send_win:guest_pos_negative %d", puser->eid);
	}
	
	ptr = str;
	ptr += sprintf(ptr, "win %d %d %lf %d %d %d %d %d %d %d %d %d"
	, winner, fold_flag, rating, gold
	, exp, puser->lv, next_exp, puser->exp, card_id, crystal
	, guest_pos, proom->game_type
	);

	if (proom->game_type == GAME_CHAPTER) {

		ptr += sprintf(ptr, " %d %d", proom->chapter_id, proom->stage_id);

		ptr += sprintf(ptr, " %d", MAX_CHAPTER_TARGET);
		// list chapter_result
		for (int i=0; i<MAX_CHAPTER_TARGET; i++) {
			ptr += sprintf(ptr, " %d %d %d %d %d"
			, chapter_result->target_list[i].target, chapter_result->target_list[i].p1
			, chapter_result->target_list[i].p2, chapter_result->value_list[i]
			, chapter_result->done_list[i]);
		}
		
		// list chapter_reward
		design_chapter_stage_t * stage = get_chapter_stage(proom->chapter_id, proom->stage_id);
		if (stage == NULL) {
			BUG_PRINT(-3, "send_win:stage_null %d %d", proom->chapter_id, proom->stage_id);
			// TODO handle null stage
			ptr += sprintf(ptr, " %d", 0);
			net_write(pconn, str, '\n');
			return -3;
		}

		ptr += sprintf(ptr, " %d", MAX_CHAPTER_REWARD);
		for (int i=0; i<MAX_CHAPTER_REWARD; i++) {
			design_chapter_reward_t *preward = stage->reward_list + i;
			ptr += sprintf(ptr, " %d %d", preward->reward, preward->count);
		}

		ptr += sprintf(ptr, " %d", chapter_reward_pos);
	}


	net_write(pconn, str, '\n');
	*/
}

// count game income
// XXX NOT WORK
int new_win_game_income(room_t *proom, int winner, int fold_flag, int hero_hp
, game_income_t *income1, game_income_t *income2
)
{
	if (proom == NULL) {
		return -3;
	}
	
	if (income1 == NULL or income2 == NULL) {
		return -13;
	}
	
	game_income_t *win_income;	
	game_income_t *lose_income;	

	if (winner == 1 || winner == 9) {
		win_income = income1;
		lose_income = income2;
	}
	if (winner == 2) {
		win_income = income2;
		lose_income = income1;
	}

	bzero(win_income, sizeof(game_income_t));
	bzero(lose_income, sizeof(game_income_t));

	int game_type = proom->game_type;
	// DEBUG_PRINT(0, "win_game_income:game_type=%d", game_type);

	win_income->cost_gold			= -g_design->constant.cost_gold[game_type];
	lose_income->cost_gold			= -g_design->constant.cost_gold[game_type];
	win_income->cost_crystal		= -g_design->constant.cost_crystal[game_type];
	lose_income->cost_crystal		= -g_design->constant.cost_crystal[game_type];

	// draw
	if (winner == 9) {
		win_income->gold= g_design->constant.draw_gold[game_type];
		lose_income->gold= g_design->constant.draw_gold[game_type];

		win_income->exp= g_design->constant.draw_exp[game_type];
		lose_income->exp= g_design->constant.draw_exp[game_type];

		win_income->crystal= g_design->constant.draw_crystal[game_type];
		lose_income->crystal= g_design->constant.draw_crystal[game_type];

		if (proom->game_type == GAME_VS_GOLD) {
			win_income->gold_times= -1;
			lose_income->gold_times= -1;
		}
		if (proom->game_type == GAME_VS_CRYSTAL) {
			win_income->crystal_times= -1;
			lose_income->crystal_times= -1;
		}
		if (proom->game_type == GAME_SOLO_GOLD) {
			lose_income->ai_times= -1;
		}
		return 0;
	}

	// winner = 1 or 2
	int rating = elo_rating(win_income->rating, lose_income->rating, 1, 32.0); // last=kfactor

	if ((fold_flag != WIN_GAME_NORMAL) && (hero_hp > FIGHT_FOLD_HERO_HP))
	{
		win_income->crystal			= g_design->constant.draw_crystal[game_type];
		lose_income->crystal		= g_design->constant.fold_crystal[game_type];
		win_income->gold			= g_design->constant.draw_gold[game_type];
		lose_income->gold			= g_design->constant.fold_gold[game_type];
		// loser force exit, will cost all fight_gold_times
		// winner will not cost times
	} else {
		win_income->crystal			= g_design->constant.win_crystal[game_type];
		lose_income->crystal		= g_design->constant.lose_crystal[game_type];
		win_income->gold			= g_design->constant.win_gold[game_type];
		lose_income->gold			= g_design->constant.lose_gold[game_type];
	}

	if (game_type != GAME_SOLO_GOLD && game_type != GAME_SOLO_FREE)
	{
		win_income->rate_offset		= rating;
		lose_income->rate_offset	= -rating;
	}

	if (game_type == GAME_GATE) {
		if (winner != 1) {
			return 0;
		}
		// DEBUG_PRINT(0, "win_game_income:gate_id %d", proom->guest[1]);
		if (proom->guest[1] <= 0 
		|| proom->guest[1] > g_design->design_gate_size) {
			BUG_PRINT(-7, "win_game_income:gate_id_error %d", proom->guest[1]);
			return 0;
		}
		design_gate_t &gate = g_design->design_gate_list[proom->guest[1]];
		win_income->gold = gate.gold;
		win_income->exp = gate.exp;
		return 0;
	}

	if (game_type == GAME_CHAPTER) {
		if (winner != 1) {
			return 0;
		}
		design_chapter_stage_t * stage = get_chapter_stage(proom->chapter_id, proom->stage_id);
		if (stage == NULL) {
			BUG_PRINT(-3, "win_game_income:stage_null %d %d", proom->chapter_id, proom->stage_id);
			return -3;
		}
		win_income->exp = stage->exp;

		// TODO get chapter reward here
		return 0;
	}

	// update ai_times, gold_times, crystal_times
	switch (game_type) {
		case GAME_SOLO_GOLD:
		{
			// solo_gold will cost one challenge times
			income1->ai_times	= -1;
			break;
		}
		case GAME_VS_GOLD:
		{
			if ((fold_flag != WIN_GAME_NORMAL) && (hero_hp > FIGHT_FOLD_HERO_HP))
			{
				// loser force exit, will cost all fight_gold_times
				// winner will not cost times
				win_income->gold_times		= 0;
				lose_income->gold_times		= -2;
			} else {
				win_income->gold_times		= -1;
				lose_income->gold_times		= -1;
			}
			break;
		}
		case GAME_VS_CRYSTAL:
		{
			if ((fold_flag != WIN_GAME_NORMAL) && (hero_hp > FIGHT_FOLD_HERO_HP))
			{
				// loser force exit, will cost all fight_gold_times
				// winner will not cost times
				win_income->crystal_times	= 0;
				lose_income->crystal_times	= -2;
			} else {
				win_income->crystal_times	= -1;
				lose_income->crystal_times	= -1;
			}
			break;
		}
	}
//		DEBUG_PRINT(0, "win_game_income:max income gold_win=%d exp_win=%d gold_lose=%d exp_lose=%d", gold_win, exp_win, gold_lose, exp_lose);

	return 0;
}

// count game income
int win_game_income(room_t *proom, int winner, int fold_flag, int hero_hp
, int eid_win, double rating_win, int &gold_win, int &crystal_win
, int &exp_win, double &rate_offset_win, int &card_win
, int &ai_times_win, int &gold_times_win, int &crystal_times_win
, int &cost_gold_win, int &cost_crystal_win
, int eid_lose, double rating_lose, int &gold_lose, int &crystal_lose
, int &exp_lose, double &rate_offset_lose, int &card_lose
, int &ai_times_lose, int &gold_times_lose, int &crystal_times_lose
, int &cost_gold_lose, int &cost_crystal_lose
)
{
	int game_type = proom->game_type;
	// DEBUG_PRINT(0, "win_game_income:game_type=%d", game_type);
	// zero all income
	ai_times_win		= 0;
	gold_times_win		= 0;
	crystal_times_win	= 0;

	ai_times_lose		= 0;
	gold_times_lose		= 0;
	crystal_times_lose	= 0;

	cost_gold_win		= 0;
	cost_gold_lose		= 0;
	cost_crystal_win	= 0;
	cost_crystal_lose	= 0;

	gold_win = 0;
	crystal_win = 0;
	exp_win = 0;
	rate_offset_win	= 0;
	card_win = 0;

	gold_lose = 0;
	crystal_lose = 0;
	exp_lose = 0;
	rate_offset_lose= 0;
	card_lose = 0;
	//

	if (game_type == GAME_GATE) {
		// DEBUG_PRINT(0, "win_game_income:gate_id %d", proom->guest[1]);
		if (proom->guest[1] <= 0 
		|| proom->guest[1] > g_design->design_gate_size) {
			BUG_PRINT(-7, "win_game_income:gate_id_error %d", proom->guest[1]);
			return 0;
		}
		design_gate_t &gate = g_design->design_gate_list[proom->guest[1]];
		DEBUG_PRINT(0, "win_game_income:gold=%d crystal=%d exp=%d", gate.gold, gate.crystal, gate.exp);
		if (winner == 1) {
			gold_win = gate.gold;
			exp_win = gate.exp;
		}
		return 0;
	}

	if (game_type == GAME_CHAPTER) {
		if (winner == 1) {
			design_chapter_stage_t * stage = &g_design->design_chapter_list[proom->chapter_id].stage_list[proom->stage_id];
			exp_win = stage->exp;
		}
		return 0;
	}

//	time_t now = time(NULL);
	// draw
	if (winner == 9) {
		gold_win			= g_design->constant.draw_gold[game_type];
		exp_win				= g_design->constant.draw_exp[game_type];
		gold_lose			= g_design->constant.draw_gold[game_type];
		exp_lose			= g_design->constant.draw_exp[game_type];
		crystal_win			= g_design->constant.draw_crystal[game_type];
		crystal_lose		= g_design->constant.draw_crystal[game_type];
		cost_gold_win		= -g_design->constant.cost_gold[game_type];
		cost_gold_lose		= -g_design->constant.cost_gold[game_type];
		cost_crystal_win	= -g_design->constant.cost_crystal[game_type];
		cost_crystal_lose	= -g_design->constant.cost_crystal[game_type];

		if (proom->game_type == GAME_VS_GOLD) {
			gold_times_win		= -1;
			gold_times_lose		= -1;
		}

		if (proom->game_type == GAME_VS_CRYSTAL) {
			crystal_times_win	= -1;
			crystal_times_lose	= -1;
		}

		if (proom->game_type == GAME_SOLO_GOLD) {
			ai_times_lose	= -1;
		}
		if (proom->guest[1] <= MAX_AI_EID) {
			gold_lose			= 0;
			exp_lose			= 0;
			crystal_lose		= 0;
			rate_offset_lose	= 0;
			card_lose			= 0;
			gold_times_lose		= 0;
			crystal_times_lose	= 0;
		}

		return 0;
	}

	// winner = 1 or 2
	int rating = elo_rating(rating_win, rating_lose, 1, 32.0); // last=kfactor

	if ((game_type == GAME_SOLO)
	|| (game_type == GAME_CHALLENGE)
	|| (game_type == GAME_ROOM)
	|| (game_type == GAME_RANK)
	|| (game_type == GAME_MATCH)) { 
		return 0;
	}

	cost_crystal_win	= -g_design->constant.cost_crystal[game_type];
	cost_crystal_lose	= -g_design->constant.cost_crystal[game_type];
	cost_gold_win		= -g_design->constant.cost_gold[game_type];
	cost_gold_lose		= -g_design->constant.cost_gold[game_type];
	if ((fold_flag != WIN_GAME_NORMAL) && (hero_hp > FIGHT_FOLD_HERO_HP))
	{
		crystal_win		= g_design->constant.draw_crystal[game_type];
		crystal_lose	= g_design->constant.fold_crystal[game_type];
		gold_win		= g_design->constant.draw_gold[game_type];
		gold_lose		= g_design->constant.fold_gold[game_type];
		exp_win			= g_design->constant.draw_exp[game_type];
		exp_lose		= g_design->constant.fold_exp[game_type];
		// loser force exit, will cost all fight_gold_times
		// winner will not cost times
	} else {
		crystal_win		= g_design->constant.win_crystal[game_type];
		crystal_lose	= g_design->constant.lose_crystal[game_type];
		gold_win		= g_design->constant.win_gold[game_type];
		gold_lose		= g_design->constant.lose_gold[game_type];
		exp_win			= g_design->constant.win_exp[game_type];
		exp_lose		= g_design->constant.lose_exp[game_type];
	}

	if (game_type != GAME_SOLO_GOLD && game_type != GAME_SOLO_FREE)
	{
		rate_offset_win		= rating;
		rate_offset_lose	= -rating;
	}

	switch (game_type) {
		case GAME_SOLO_GOLD:
		{
			// solo_gold will cost one challenge times
			if (winner == 1) {
				// player win
				ai_times_win	= -1;
			} else {
				// robot win
				ai_times_lose	= -1;
			}
			break;
		}
		case GAME_VS_GOLD:
		{
			if ((fold_flag != WIN_GAME_NORMAL) && (hero_hp > FIGHT_FOLD_HERO_HP))
			{
				// loser force exit, will cost all fight_gold_times
				// winner will not cost times
				gold_times_win		= 0;
				gold_times_lose		= -2;
			} else {
				gold_times_win		= -1;
				gold_times_lose		= -1;
			}
			break;
		}
		case GAME_VS_CRYSTAL:
		{
			if ((fold_flag != WIN_GAME_NORMAL) && (hero_hp > FIGHT_FOLD_HERO_HP))
			{
				// loser force exit, will cost all fight_gold_times
				// winner will not cost times
				crystal_times_win		= 0;
				crystal_times_lose		= -2;
			} else {
				crystal_times_win		= -1;
				crystal_times_lose		= -1;
			}
			break;
		}
	}

	if (eid_win <= MAX_AI_EID) {
		gold_win			= 0;
		exp_win				= 0;
		crystal_win			= 0;
		rate_offset_win		= 0;
		card_win			= 0;
		gold_times_win		= 0;
		crystal_times_win	= 0;
	}

//		DEBUG_PRINT(0, "win_game_income:max income gold_win=%d exp_win=%d gold_lose=%d exp_lose=%d", gold_win, exp_win, gold_lose, exp_lose);

	return 0;
}


// only winner call this
// @return change (0 no change) >0 some changes
int win_mission_update(connect_t *conn, room_t *proom, int oppo_eid, int fold_flag, const char * deck, int hero_id, int guild_lv)
{
	if (conn == NULL) {
		return 0; // no need to report error (TODO offline update)
	}

	int change = 0;
	evil_user_t &euser = conn->euser;  // no need null check
	mission_t * mlist = euser.mission_list; // beware pointer and bzero
	design_mission_t *dlist = g_design->mission_list; // global
	/*

	evil_user_t &euser = conn->euser;  // no need null check
	design_mission_t *dlist = g_design->mission_list; // global
	mission_t * mlist = euser.mission_list; // beware pointer and bzero
	char *deck;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}

	if (euser.eid == proom->guest[0]) {
		deck = proom->deck[0];
	}
	if (euser.eid == proom->guest[1]) {
		deck = proom->deck[1];
	}

	int hero_id = get_hero(deck); // ???
	if (hero_id < 0) {
		BUG_PRINT(-7, "win_mission_update:hero_id");
		hero_id = 0;  // fail safe use 0
	}

	if (euser.eid == 0) {
		BUG_PRINT(-17, "win_mission_update:eid=0");
		return 0;
	}
	*/


	// 1. check AI
	// 2. check game type (proom->game_type)
	//    quick(vs), room(vs), challenge(vs + challenge)

	if (proom->game_type == GAME_GATE) {
		change |= mission_update(mlist, dlist, MISSION_GATE
			, 1, oppo_eid, hero_id, guild_lv);
	}

	// AI first
	if (oppo_eid > 0 && oppo_eid <= MAX_AI_EID) {
		change |= mission_update(mlist, dlist, MISSION_AI
		, 1, oppo_eid, hero_id, guild_lv);

		return change;
	}
	
	// here: the game is always QUICK, ROOM or CHALLENGE
	// VS : only quick game
	if (proom->game_type == GAME_QUICK) {
		change |= mission_update(mlist, dlist, MISSION_VS
			, 1, 0, hero_id, guild_lv);
		if (!fold_flag) {
			change |= mission_update(mlist, dlist, MISSION_QUICK_WIN
				, 1, 0, hero_id, guild_lv);
		}
	}

	if (proom->game_type == GAME_CHALLENGE) {
		int mtype = 0;

		if (proom->guest[0] == euser.eid) {
			mtype = MISSION_CHALLENGE;
		} else {
			mtype = MISSION_BEI_CHALLENGE;
		}
		change |= mission_update(mlist, dlist, mtype
			, 1, 0, hero_id, guild_lv);
	}
	
	return change;
}

int game_mission_update(connect_t *conn, room_t *proom, int oppo_eid, int fold_flag, int is_winner)
{
	if (conn == NULL) {
		return 0; // no need to report error (TODO offline update)
	}

	evil_user_t &euser = conn->euser;  // no need null check
	mission_t * mlist = euser.mission_list; // beware pointer and bzero
	design_mission_t *dlist = g_design->mission_list; // global
	char *deck;
	int change = 0;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}

	if (euser.eid == proom->guest[0]) {
		deck = proom->deck[0];
	}
	if (euser.eid == proom->guest[1]) {
		deck = proom->deck[1];
	}

	int hero_id = get_hero(deck); // ???
	if (hero_id < 0) {
		BUG_PRINT(-7, "win_mission_update:hero_id");
		hero_id = 0;  // fail safe use 0
	}

	if (euser.eid == 0) {
		BUG_PRINT(-17, "win_mission_update:eid=0");
		return 0;
	}

	if (is_winner) {
		change |= win_mission_update(conn, proom, oppo_eid, fold_flag, deck, hero_id, guild_lv);
	}

	if (proom->game_type == GAME_QUICK && !fold_flag) {
		change |= mission_update(mlist, dlist, MISSION_QUICK, 1, 0, hero_id, guild_lv);
	}

	return change;
}


// @return change (0 no change) >0 some changes
int fight_mission_update(connect_t *conn, room_t *proom, int winner, int fold_flag, int fold_hero_hp)
{
	if (conn == NULL) {
		return 0; // no need to report error (TODO offline update)
	}

	evil_user_t &euser = conn->euser;  // no need null check
	design_mission_t *dlist = g_design->mission_list; // global
	mission_t * mlist = euser.mission_list; // beware pointer and bzero
	int change = 0;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}

	if (euser.eid == 0) {
		BUG_PRINT(-7, "fight_mission_update:eid=0");
		return 0;
	}
	
	if (fold_flag != WIN_GAME_NORMAL && fold_hero_hp >= 5) {
		if ((euser.eid == proom->guest[0] && winner == 2)
		|| (euser.eid == proom->guest[1] && winner == 1)) {
			return 0;
		}
	}

	switch (proom->game_type)
	{
		case GAME_SOLO_GOLD:
		case GAME_SOLO_FREE:
		{
			change |= mission_update(mlist, dlist, MISSION_FIGHT_AI
			, 1, 0, 0, guild_lv);
			break;
		}
		case GAME_VS_GOLD:
		case GAME_VS_CRYSTAL:
		case GAME_VS_FREE:
		{
			change |= mission_update(mlist, dlist, MISSION_FIGHT_VS
			, 1, 0, 0, guild_lv);
			break;
		}
		default:
		{
			return 0;
		}
	}
	change |= mission_update(mlist, dlist, MISSION_FIGHT
	, 1, 0, 0, guild_lv);
	
	return change;
}

int rank_game_times_mission_update(connect_t* conn, room_t *proom)
{
	int change = 0;
	if (conn == NULL) {
		return 0;
	}

	evil_user_t &euser = conn->euser;
	if (euser.eid <= MAX_AI_EID) {
		return 0;
	}

	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}

	design_mission_t *dlist = g_design->mission_list; // global
	mission_t * mlist = euser.mission_list; // beware pointer and bzero
	change |= mission_update(mlist, dlist, MISSION_RANK_GAME_TIMES
	, 1, 0, 0, guild_lv);
	
	return change;
}

// levelup
// @return change (0 no change) > 0 some changes
// usage:
// change |= levelup_mission_update(...)
int levelup_mission_update(connect_t *conn, int lv_offset)
{
	if (conn == NULL) {
		return 0;
	}
	if (lv_offset <= 0) { // offset should be >= 0
		return 0;
	}
	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	change |= mission_update(mlist, dlist, MISSION_LEVEL, euser.lv, 0, 0, guild_lv);
	return change;
}

// guest user view a game until finish
// may update 2 times
int view_mission_update(connect_t *conn, room_t *proom)
{
	if (conn == NULL) {
		return 0;
	}
	if (proom == NULL) {
		BUG_PRINT(-3, "view_mission_update:null_room");
		return 0;
	}
	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	int hero1 = get_hero(proom->deck[0]);
	int hero2 = get_hero(proom->deck[1]);
	// DEBUG_PRINT(0, "view_mission_update:hero1=%d hero2=%d", hero1, hero2);

	change |= mission_update(mlist, dlist, MISSION_VIEW, 1, hero1, hero2, guild_lv);
	// change |= mission_update(mlist, dlist, MISSION_VIEW, 1, hero2, 0);

	return change;
}

int replay_mission_update(connect_t *conn, int hero1, int hero2)
{
	if (conn == NULL) {
		return 0;
	}
	if (hero1 <= 0 || hero2 <= 0) { // offset should be >= 0
		BUG_PRINT(-6, "replay_mission_update:hero_id %d %d"
		, hero1, hero2);
		return 0;
	}
	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	change |= mission_update(mlist, dlist, MISSION_REPLAY, 1, hero1, hero2, guild_lv);
	return change;
}

// need test
// channel: 1==world, 2==room, 3==guild, 
int chat_mission_update(connect_t *conn, int channel)
{
	if (conn == NULL) {
		return 0;
	}
	if (channel < 1 || channel > 3) {
		BUG_PRINT(-6, "chat_mission_update:invalid_channel %d", channel);
		return 0;
	}
	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	change |= mission_update(mlist, dlist, MISSION_CHAT, 1, channel, 0, guild_lv);
	return change;
}

// need test
int friend_mission_update(connect_t *conn)
{
	if (conn == NULL) {
		return 0;
	}

	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	change |= mission_update(mlist, dlist, MISSION_FRIEND, 1, 0, 0, guild_lv);
	return change;
}

// need test
int shop_mission_update(connect_t *conn, int count, int card_id)
{
	if (conn == NULL) {
		return 0;
	}
	if (count <= 0) {
		return 0;
	}
	if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
		return 0;
	}

	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	change |= mission_update(mlist, dlist, MISSION_SHOP, count, card_id, 0, guild_lv);
	return change;
}

// need test
int card_mission_update(connect_t *conn, int count, int card_id)
{
	if (conn == NULL) {
		return 0;
	}
	if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
		return 0;
	}

	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	// DEBUG_PRINT(0, "card_mission_update:count=%d card_id=%d", count, card_id);
	change |= mission_update(mlist, dlist, MISSION_CARD, count, card_id, 0, guild_lv);
	return change;
}

// mission-fix : use simplified mission_t
int deck_mission_update(connect_t *conn)
{
	if (conn == NULL) {
		return 0;
	}

	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;

	design_mission_t *dlist = g_design->mission_list; // global

	for (int i=1;i<=MAX_MISSION;i++) {
		mission_t * mis = mlist + i;
		if (mis->status != MISSION_STATUS_READY) {
			continue;
		}
		design_mission_t &dmis = get_design_mission(dlist, mis->mid);
		if (dmis.mtype != MISSION_DECK) {
			continue;
		}
		// now: assume mtype is MISSION_DECK and mis.status = READY
		int card_id = dmis.p2 - 1; // base 0
		if (card_id < 0 || card_id >= EVIL_CARD_MAX) {
			BUG_PRINT(-7, "deck_mission_update:card_id=%d", card_id);
			continue;
		}
		int count = euser.deck[card_id] - '0';
		if (count <= 0 && count != mis->n1) {
			continue;
		}
		// DEBUG_PRINT(0, "out_load_deck:mid=%d mtype=%d n1=%d n2=%d count=%d", mis->mid, mis->mtype, mis->n1, mis->n2, count);
		change |= mission_update(mlist, dlist, MISSION_DECK
		, count, dmis.p2, 0, guild_lv);
	}
	// DEBUG_PRINT(0, "deck_mission_update:change=%d", change);
	return change;
}

// need test
int collection_mission_update(connect_t *conn, int total)
{
	if (conn == NULL) {
		return 0;
	}
	if (total <= 0 || total > EVIL_CARD_MAX) {
		return 0;
	}

	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	change |= mission_update(mlist, dlist, MISSION_COLLECTION, total, 0, 0, guild_lv);
	return change;
}


int guild_mission_update(connect_t *conn)
{
	if (conn == NULL) {
		return 0;
	}
	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	// p1 = 1
	change |= mission_update(mlist, dlist, MISSION_GUILD, 1, 0, 0, guild_lv);
	return change;
}

// add friend
// @return change (0 no change) > 0 some changes
int friend_mission_update(connect_t *conn, int num)
{
	if (conn == NULL) {
		return 0;
	}
	if (num <= 0) { 
		return 0;
	}
	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	change |= mission_update(mlist, dlist, MISSION_FRIEND, num, 0, 0, guild_lv);
	return change;
}

int monthly_mission_update(connect_t *conn)
{
	if (conn == NULL) {
		return 0;
	}

	int change = 0;
	evil_user_t & euser = conn->euser;
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	mission_t * mlist = euser.mission_list;
	design_mission_t *dlist = g_design->mission_list; // global

	// DEBUG_PRINT(0, "card_mission_update:count=%d card_id=%d", count, card_id);
	change |= mission_update(mlist, dlist, MISSION_MONTHLY, 1, 0, 0, guild_lv);
	return change;
}

// @return change (0 no change) >0 some changes
int chapter_stage_mission_update(connect_t *conn, int chapter_id, int stage_id, char cstar)
{
	if (conn == NULL) {
		return 0; // no need to report error (TODO offline update)
	}

	evil_user_t &euser = conn->euser;  // no need null check
	design_mission_t *dlist = g_design->mission_list; // global
	mission_t * mlist = euser.mission_list; // beware pointer and bzero
	int change = 0;
	int guild_lv = 0;
	int star = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}

	if (euser.eid == 0) {
		BUG_PRINT(-7, "chapter_stage_mission_update:eid=0");
		return 0;
	}

	if (cstar != CHAPTER_DATA_START && cstar != CHAPTER_DATA_LOCK) {
		star = cstar - CHAPTER_DATA_STAR_0;
	}
	
	if (star >= 0) {
		change |= mission_update(mlist, dlist, MISSION_CHAPTER_STAGE
		, star, chapter_id, stage_id, guild_lv);
	}
	return change;
}

// @return change (0 no change) >0 some changes
int chapter_mission_update(connect_t *conn, int chapter_id, int stage_size, const char *chapter_data)
{
	if (conn == NULL) {
		return 0; // no need to report error (TODO offline update)
	}

	evil_user_t &euser = conn->euser;  // no need null check
	design_mission_t *dlist = g_design->mission_list; // global
	mission_t * mlist = euser.mission_list; // beware pointer and bzero
	int change = 0;
	int guild_lv = 0;
	int stage_count[4];
	bzero(stage_count, sizeof(stage_count));
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}

	if (euser.eid == 0) {
		BUG_PRINT(-7, "chapter_mission_update:eid=0");
		return 0;
	}
	
	for (int i = 0; i < stage_size; i++)
	{
		char cstar = chapter_data[i];
		if (cstar == CHAPTER_DATA_START || cstar == CHAPTER_DATA_LOCK) {
			continue;
		}
		int star = cstar - CHAPTER_DATA_STAR_0;
		if (star >= 0) {	stage_count[0]++;	}
		if (star >= 1) {	stage_count[1]++;	}
		if (star >= 2) {	stage_count[2]++;	}
		if (star >= 3) {	stage_count[3]++;	}
	}

	for (int i = 0; i <= 3; i++) {
		change |= mission_update(mlist, dlist, MISSION_CHAPTER
		, stage_count[i], chapter_id, i, guild_lv);
	}
	
	return change;
}


int chapter_mission_update(connect_t *conn) {
	int change = 0;
	if (conn == NULL) {
		return 0; // no need to report error (TODO offline update)
	}
	if (conn->euser.flag_load_chapter == 0) {
		return 0;
	}

	for (int i = 0; i < g_design->design_chapter_size; i++) {
		design_chapter_t * dchapter = &g_design->design_chapter_list[i];
		if (dchapter->chapter_id == 0) {
			continue;
		}
		evil_chapter_data_t * chapter = &conn->euser.chapter_list[i];
		for (int j=1; j<=dchapter->stage_size; j++) {
			design_chapter_stage_t * stage = dchapter->stage_list + j;
			if (stage->stage_id <= 0) {continue;};
			change |= chapter_stage_mission_update(conn, i
			, j, chapter->data[j-1]);
		}
		change |= chapter_mission_update(conn, i, dchapter->stage_size
		, chapter->data);
	}
	return change;
}

int hero_hp_mission_update(connect_t *conn) {
	int change = 0;
	if (conn == NULL) {
		return 0;
	}
	evil_user_t &euser = conn->euser;  // no need null check
	design_mission_t *dlist = g_design->mission_list; // global
	mission_t * mlist = euser.mission_list; // beware pointer and bzero
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	
	for (int i=1; i<=HERO_MAX; i++) {
		evil_hero_t &hero = conn->euser.hero_data_list[i].hero;
		if (hero.hp == 0) { continue; }
		change |= mission_update(mlist, dlist, MISSION_HERO_HP
		, hero.hp, hero.hero_id, 0, guild_lv);
	}

	return change;
}
// do mission_refresh here and save_mission to database
// also send net_write() mchange
// do not do mission_refresh in other code, except login
// usage:
//
// win_game() : 
// change |= win_mission_update(...)
// change |= levelup_mission_update(...)
// change_mission(conn, change);
// 
// + exp / lv up (reward)
// change |= levelup_mission_update(...)
// change_mission(conn, change);
// 
// note: *_mission_update() should never return < 0, this will
// add up positive number to become 0, if there is an error inside
// *_mission_update() : use BUG_PRINT or ERROR_PRINT or WARN_PRINT
int change_mission(connect_t *conn, int change)
{
	// if change == 0 return
	// net_write(conn, "mchange %d", change);
	if (conn == NULL) {
		return 0;
	}
	/*
	if (change <= 0) {
		return 0;
	}
	*/

	evil_user_t &euser = conn->euser;  // no need null check
	int guild_lv = 0;
	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	design_mission_t *dlist = g_design->mission_list; // global
	mission_t * mlist = euser.mission_list; // beware pointer and bzero

	// accurate logic:  (change & (MISSION_UP_NEW))
	// TODO euser.card is loaded in login (reg)
	change |= mission_refresh(mlist, dlist, euser.lv, guild_lv, euser.card);
	
	// save_mission
	if (change > 0) {
		int ret = save_mission(conn);  // offline do dbio 
		BUG_PRINT(ret, "change_mission:save_mission");
	}

	// mask = 3  : client only need OK and NEW
	// peter: let the UP_NUM send to client
	int notify_change = change & (MISSION_UP_OK | MISSION_UP_NEW);
	if ((change & MISSION_UP_NUM)>0) {
		notify_change |= MISSION_UP_NEW ;
	}
	if (notify_change > 0) {
		net_writeln(conn, "mchange %d", notify_change);
	}

	return change; // caller may not need this
}

int save_match_result(match_t &match, match_player_t &player1, match_player_t &player2) 
{

	int ret;
	int count = 0;
	char db_buffer[400];
	char * ptr;
	ptr = db_buffer;

	// 1.get player data in this round
	ptr += sprintf(ptr, "%d %d %d %d %d %d %d %d %d %d %s"
	, player1.eid, 0, player1.round, player1.team_id
	, player1.win, player1.lose, player1.draw, player1.tid
	, player1.point, player1.icon, player1.alias);
	count += 1;
	ptr += sprintf(ptr, " %d %d %d %d %d %d %d %d %d %d %s"
	, player2.eid, 0, player2.round, player2.team_id
	, player2.win, player2.lose, player2.draw, player2.tid
	, player2.point, player2.icon, player2.alias);
	count += 1;
	

	//INFO_PRINT(0, "---- get_match_resut: match_id=%ld status=%d round=%d", match->match_id, match->status, match->round);
	// 2.get player data in last round
	match_player_t &player1_new = get_player_last_round(match, player1.eid);
	if (player1_new.eid == 0 && player1_new.team_id == 0) {
		ERROR_RETURN(-6, "save_match_result:player1_null");
	}

	/*
	printf("======= save_match_result =====\n");
	print_player(player1);
	print_player(player1_new);
	*/
	if (player1.round != player1_new.round) {
		// get fake_eid
		int win_flag = player1.point >= 5 ? 2 : 1; 
		int fake_eid = (player1.round <= MAX_TEAM_ROUND)
		? -(win_flag * 100 + player1.tid * 10 + player1.round) : -1;
		// printf("---- fake_eid: %d\n", fake_eid);
		ptr += sprintf(ptr, " %d %d %d %d %d %d %d %d %d %d %s"
		, player1_new.eid, fake_eid, player1_new.round
		, player1_new.team_id, player1_new.win
		, player1_new.lose, player1_new.draw
		, player1_new.tid, player1_new.point
		, player1_new.icon, player1_new.alias);

		count += 1;
	}

	match_player_t &player2_new = get_player_last_round(match, player2.eid);
	if (player2_new.eid == 0 && player2_new.team_id == 0) {
		ERROR_RETURN(-16, "save_match_result:player2_null");
	}
	if (player2.round != player2_new.round) {
		// get fake_eid
		int win_flag = player2.point >= 5 ? 2 : 1; 
		int fake_eid = (player2.round <= MAX_TEAM_ROUND)
		? -(win_flag * 100 + player2.tid * 10 + player2.round) : -1;
		ptr += sprintf(ptr, " %d %d %d %d %d %d %d %d %d %d %s"
		, player2_new.eid, fake_eid, player2_new.round
		, player2_new.team_id, player2_new.win
		, player2_new.lose, player2_new.draw
		, player2_new.tid, player2_new.point
		, player2_new.icon, player2_new.alias);

		count += 1;
	}

	// DEBUG_PRINT(0, "save_match_result:buffer=%s", db_buffer);

	ret = dbin_write(NULL, "update_player", DB_UPDATE_MATCH_PLAYER
	, "%ld %d %s", match.match_id, count, db_buffer);

	return ret;
}

int old_get_chapter_star(room_t *proom, int winner, chapter_result_t &chapter_result);
int get_chapter_star(room_t *proom, int winner, chapter_result_t &chapter_result);

int change_hero_mission(connect_t *conn, room_t *proom, int winner, int win_hero_id, int lose_hero_id, int fold_flag, int fold_hero_hp)
{
	int eid;
	int count;
	int mid_list[MAX_HERO_MISSION+1];
	char buffer[BUFFER_SIZE+1];
	char *ptr;
	evil_hero_data_t *hero_data;
	evil_hero_mission_t *mission;
	design_mission_hero_t *dhero;
	design_hero_mission_t *dmis;
	chapter_result_t chapter_result;
	bzero(mid_list, sizeof(mid_list));
	bzero(buffer, sizeof(buffer));

	if (conn == NULL) {
		return 0;
	}
	evil_user_t &euser = conn->euser;
	eid = euser.eid;
	if (eid <= MAX_AI_EID) {
		return 0;
	}

	if ((eid == proom->guest[0] && winner != 1)
	|| (eid == proom->guest[1] && winner != 2))
	{
		// lose game
		return 0;
	}

	count = 0;
	hero_data = euser.hero_data_list + win_hero_id;
	if (hero_data->hero.hero_id == 0) {
		ERROR_RETURN(-6, "change_hero_mission:no_such_hero %d %d"
		, eid, win_hero_id);
	}
	dhero = g_design->mission_hero_list + win_hero_id;
	if (dhero->hero_id == 0) {
		BUG_RETURN(-7, "change_hero_mission:no_such_design_hero %d %d"
		, eid, win_hero_id);
	}
//	WARN_PRINT(1, "proom game_type[%d] chapter[%d]", proom->game_type
//	, proom->chapter_id);
	for (int i = 1; i <= MAX_HERO_MISSION; i++) {
		mission = hero_data->mission_list + i;
		dmis = dhero->mission_list + i;
//		DEBUG_PRINT(1, "mission_info[%d] %d / %d / %d dmis %d / %d / %d / %d"
//		, i, mission->mission_id, mission->status, mission->n1
//		, dmis->mtype, dmis->p1, dmis->p2, dmis->p3);
		if (mission->mission_id == 0) {
			continue;
		}
		if (mission->status >= MISSION_STATUS_OK) {
			continue;
		}
		switch (dmis->mtype) {
			case HERO_MISSION_TYPE_CHAPTER:
			{
				if (proom->game_type != GAME_CHAPTER) {
					break;
				}
				int m_chapter	= dmis->p1 / 100;
				int m_stage		= dmis->p1 % 100;
				if (proom->chapter_id != m_chapter
				|| proom->stage_id != m_stage) {
					break;
				}
				if (dmis->p3 != 0) {
					bzero(&chapter_result, sizeof(chapter_result));
					int star = get_chapter_star(proom, winner, chapter_result);
					if (star != dmis->p3) {
						break;
					}
				}
				
				mission->n1++;
				if (mission->n1 >= dmis->p2) {
					mission->n1 = dmis->p2;
					mission->status = MISSION_STATUS_OK;
				}
//				DEBUG_PRINT(1, "mid_list[%d] = %d", count, mission->mission_id);
				mid_list[count++] = mission->mission_id;
				break;
			}
			default:
			{
				// TODO:
				break;
			}
		}
		
	}

	ptr = buffer;
	for (int i = 0; i < count; i++) {
		int mid = mid_list[i];
		mission = hero_data->mission_list + mid;
		ptr += sprintf(ptr, " %d %d %d", mission->mission_id
		, mission->status, mission->n1);
	}
	dbin_write(NULL, "up_hero_mlist", DB_UPDATE_HERO_MISSION
	, "%d %d %d%s", eid, win_hero_id, count, buffer);

	return 0;
}

int refresh_chapter_data(connect_t *conn, int chapter_id, int stage_id, int star);
// update chapter stage star
int update_chapter_data(room_t *proom, int winner, chapter_result_t &chapter_result)
{

	int star = 0;
	// int next_stage_size;

	if (proom->game_type != GAME_CHAPTER) {
		return 0;
	}

	if (winner != 1) {
		return 0;
	}

	connect_t * conn = get_conn_by_eid(proom->guest[0]);
	if (conn == NULL) {
		// offline, no reward
		return 0;
	}

	// chapter_result_t chapter_result;
	bzero(&chapter_result, sizeof(chapter_result_t));
	star = get_chapter_star(proom, winner, chapter_result);

	/*
	next_stage_size = 0;
	if (proom->chapter_id < g_design->design_chapter_size) {
		next_stage_size = g_design->design_chapter_list[proom->chapter_id+1].stage_size;
	}
	dbin_write(conn, "chapter_update_data", DB_CHAPTER_UPDATE_DATA, "%d %d %d %d %d %d", proom->guest[0], proom->chapter_id, proom->stage_id, star, g_design->design_chapter_list[proom->chapter_id].stage_size, next_stage_size);

	*/

	refresh_chapter_data(conn, proom->chapter_id, proom->stage_id, star);
	return star;
}

int get_chapter_reward(design_chapter_stage_t * stage, int *reward, int *count)
{
	int r;
	if (stage == NULL) {
		return -3;
	}
	r = abs(random()) % stage->reward_weight_max + 1;
	for (int i=0; i<MAX_CHAPTER_REWARD; i++) {
		design_chapter_reward_t *preward = stage->reward_list + i;
		if (preward->weight_start <= r && r <= preward->weight_end) {
			*reward = preward->reward;	
			*count = preward->count;	
			return i;
		}
	}
	return -13;
}


int update_chapter_reward(room_t *proom, int winner, int *reward_pos)
{
	// int ret;

	int reward = 0;
	int count = 0;

	if (proom->game_type != GAME_CHAPTER) {
		return 0;
	}

	if (winner != 1) {
		return 0;
	}

	connect_t * conn = get_conn_by_eid(proom->guest[0]);
	if (conn == NULL) {
		// offline, no reward
		// TODO save db
		return 0;
	}

	int eid = get_eid(conn);
	int ext1 = 0; // for reward_exp lv
	int ext2 = 0; // for reward_exp exp

	const char * cmd = "chapter_reward";

	if (eid <= 0)  {
		ERROR_RETURN(-9, "update_chapter_reward:not_login");
	}


	evil_user_t *puser = &conn->euser;

	design_chapter_stage_t * stage = &g_design->design_chapter_list[proom->chapter_id].stage_list[proom->stage_id];

	*reward_pos = get_chapter_reward(stage, &reward, &count);
	if (*reward_pos < 0) {
		// BUG_PRINT(-6, "dbin_chapter_reward:get_chapter_reward_bug %d", stage->stage_id);
		NET_ERROR_RETURN(conn, -6, "update_chapter_reward:reward_pos_error %d", stage->stage_id);
	}

	if (reward <= 0) {
		// TODO handle reward negative
		// BUG_PRINT(-16, "dbin_chapter_reward:reward_bug %d", stage->stage_id);
		NET_ERROR_RETURN(conn, -16, "update_chapter_reward:reward_error %d", stage->stage_id);
	}

	DEBUG_PRINT(0, "update_chapter_reward:reward=%d count=%d", reward, count);

	if (reward == CHAPTER_REWARD_EXP) {
		int lv_offset = 0; // should be zero at this point
		int exp_offset = 0;
		DEBUG_PRINT(0, "update_chapter_reward:before lv=%d exp=%d", puser->lv, puser->exp);
		add_exp(conn, count, &lv_offset, &exp_offset);
		DEBUG_PRINT(0, "update_chapter_reward:after lv=%d exp=%d", puser->lv, puser->exp);
		ext1 = puser->lv;
		ext2 = puser->exp;
	}

	dbin_write(conn, "chapter_reward", DB_CHAPTER_REWARD, "%d %d %d %d %d %d %d", eid, proom->chapter_id, proom->stage_id, reward, count, ext1, ext2);
	return 0;
}


int get_quick_reward(int *quick_reward_gold, int *quick_reward_crystal, int pos)
{

	design_quick_reward_list_t * reward_list = &g_design->quick_reward_list[pos];

	int r;
	r = abs(random()) % reward_list->max_weight + 1;
	for (int i=0; i<MAX_QUICK_REWARD; i++) {
		design_quick_reward_t *preward = reward_list->reward_array + i;
		if (preward->weight_start <= r && r <= preward->weight_end) {
			if (preward->type == QUICK_REWARD_TYPE_GOLD) {
				*quick_reward_gold = preward->count;
				*quick_reward_crystal = 0;
			}
			if (preward->type == QUICK_REWARD_TYPE_CRYSTAL) {
				*quick_reward_crystal = preward->count;
				*quick_reward_gold = 0;
			}
			return i;
		}
	}

	return -13;
}

int update_quick_reward(room_t *proom, int winner, int fold_flag
, int *quick_reward_pos1, int *quick_reward_pos2
, int *quick_reward_gold1, int *quick_reward_gold2
, int *quick_reward_crystal1, int *quick_reward_crystal2)
{
	int reward_list_pos;
	if (proom->game_type != GAME_QUICK) {
		return 0;
	}

	if (winner != 1 && winner != 2) {
		return 0;
	}

	if (fold_flag) {
		return 0;
	}

	reward_list_pos = (winner == 1) ? 0 : 1;
	*quick_reward_pos1 = get_quick_reward(quick_reward_gold1, quick_reward_crystal1, reward_list_pos);

	reward_list_pos = (winner == 1) ? 1 : 0;
	*quick_reward_pos2 = get_quick_reward(quick_reward_gold2, quick_reward_crystal2, reward_list_pos);

	DEBUG_PRINT(0, "update_quick_reward:pos1=%d gold1=%d crystal1=%d", *quick_reward_pos1, *quick_reward_gold1, *quick_reward_crystal1);
	DEBUG_PRINT(0, "update_quick_reward:pos2=%d gold2=%d crystal2=%d", *quick_reward_pos2, *quick_reward_gold2, *quick_reward_crystal2);

	return 0;
}


// peter: TODO refactor 
// change |= winner_mission_update(conn, eid_oppo);
// change |= level_mission_update(conn, lv);
// ...
// if (change > 0) net_writeln(conn, "mchange %d", change);
int update_game_finish_mission(room_t *proom, int eid1, int eid2, int winner, int lv_offset1, int lv_offset2, int fold_flag, int fold_hero_hp, int win_hero_id, int lose_hero_id)
{
	int change;
	connect_t *player_conn;

	// do eid1 first
	change = 0;
	player_conn = get_conn_by_eid(eid1);
	change |= game_mission_update(player_conn, proom, eid2, fold_flag, (winner==1));
	/*
	if (winner == 1) {
		change |= win_mission_update(player_conn, proom, eid2, fold_flag);
	}
	*/

	change |= levelup_mission_update(player_conn, lv_offset1);
	change |= fight_mission_update(player_conn, proom, winner, fold_flag, fold_hero_hp);
	change |= rank_game_times_mission_update(player_conn, proom);

	change |= chapter_mission_update(player_conn);
	change_mission(player_conn, change); // refresh, mchange

	change_hero_mission(player_conn, proom, winner, win_hero_id, lose_hero_id
	, fold_flag, fold_hero_hp);

	// do eid2 
	change = 0;
	player_conn = get_conn_by_eid(eid2);
	change |= game_mission_update(player_conn, proom, eid1, fold_flag, (winner==2));
	/*
	if (winner == 2 && eid2 > 0) {
		change |= win_mission_update(player_conn, proom, eid1, fold_flag);
	}
	*/
	change |= levelup_mission_update(player_conn, lv_offset2);
	change |= fight_mission_update(player_conn, proom, winner, fold_flag, fold_hero_hp);
	change |= rank_game_times_mission_update(player_conn, proom);

	change_mission(player_conn, change); // refresh, mchange

	change_hero_mission(player_conn, proom, winner, win_hero_id, lose_hero_id
	, fold_flag, fold_hero_hp);


	// do other guest (viewer)
	for (int i=2; i<proom->num_guest; i++) {
		int guest_eid = proom->guest[i];
		if (guest_eid <= MAX_AI_EID) {
			continue;  
		}
		connect_t*  guest_conn = get_conn_by_eid(guest_eid);
		change = 0;
		change |= view_mission_update(guest_conn, proom);
		change_mission(guest_conn, change);
		// DEBUG_PRINT(guest_eid, "view_mission_update:change=%d", change);
	}

	return 0;
}

int update_ranking_game_data(room_t *proom, connect_t *conn, int eid1, int eid2, int winner)
{
	int ret;
	
	if (proom->game_type != GAME_RANK) {
		return 0;
	}
	ret = dbin_write(conn, "win", DB_CHANGE_RANKING_DATA
	, IN_CHANGE_RANKING_DATA_PRINT, eid1
	, eid2, winner);
	if (ret <= 0) {
		ERROR_PRINT(ret, "update_ranking_game_data:db_ranking_error");
	}

	return 0;
}

int update_arena_data(room_t *proom, connect_t *conn, int eid1, int eid2, int winner)
{
	if (proom->game_type != GAME_ARENA) {
		return 0;
	}

	if (winner != 1) {
		return 0;
	}

	dbin_write(conn, "win", DB_EXCHANGE_ARENA_RANK
	, "%d %d", eid1, eid2);
	return 0;
}

int update_match_game_data(room_t *proom, int eid1, int eid2, int winner)
{
	int ret;
//	long match_id;

	if (proom->channel != CHANNEL_MATCH 
	|| proom->game_type != GAME_MATCH) {
		return 0;
	}
	match_t & match = get_match(proom->match_id);
	if (match.match_id == 0) {
		return 0;
	}
	match_player_t &player1 = get_player_last_round(match, eid1);
	match_player_t &player2 = get_player_last_round(match, eid2);
	ret = match_result(match, eid1, eid2, winner, proom->start_side);
	// 1.get this round 2 player
	// 2.get_player_last_round(), may same as 
	if (ret != 0) {
		ERROR_PRINT(ret, "win_game:match_result_fail");
	}
//	match_id = match.match_id;
	// write match result into db
	ret = save_match_result(match, player1, player2);
	if (ret <= 0) {
		ERROR_PRINT(ret, "win_game:save_match_result_fail");
	}

	return 0;
}

int create_match_game(connect_t *conn, long match_id, int eid1, int eid2, char *alias1, char *alias2)
{
//	int ret;
	room_t * proom;
	match_t & match = get_match(match_id);
	if (match.match_id == 0) {
		return 0;
	}
	match_player_t &player1 = get_player_last_round(match, eid1);
	match_player_t player2 = get_player_last_round(match, eid2);
	if (player1.eid == 0 || player2.eid == 0) {
		ERROR_RETURN(-17, "win_game:eid_error %d %d"
		, player1.eid, player2.eid);
	}
	if (player1.round != player2.round
	|| player1.round != match.round
	|| player2.round != match.round
	|| player1.point >= 5 || player2.point >= 5) {
		// this round is over
		// DEBUG_PRINT(0, "win_game:round %d %d tid %d %d point %d %d", player1.round, player2.round, player1.tid, player2.tid, player1.point, player2.point);
		return ST_LOGIN;
	}
	if (match.round <= MAX_TEAM_ROUND 
	&& player1.tid != player2.tid) {
		return ST_LOGIN;
	}
	if (match.round > MAX_TEAM_ROUND) { 
		int max_tid = max(player1.tid, player2.tid);
		int min_tid = min(player1.tid, player2.tid);
		if (max_tid - min_tid != 1 || min_tid % 2 != 0) {
			return ST_LOGIN;
		}

	}
	DEBUG_PRINT(0, "win_game:%ld %d %d", match_id, player1.eid
	, player2.eid);

	proom = create_match_room(match, eid1, eid2, alias1, alias2);
	if (proom == NULL) {
		ERROR_RETURN(-66, "win_game:create_match_room_fail");
	}

	/*
	ret = do_room_add_eid(proom, eid1, alias1);
	if (ret < 0) {
		ERROR_RETURN(-17
		, "new_room_add_2 %d", ret);
	}

	ret = do_room_add_eid(proom, eid2, alias2);
	if (ret < 0) {
		ERROR_RETURN(-27
		, "new_room_add_2 %d", ret);
	}

	// start game
	sprintf(proom->title, "%s~VS~%s"
	, alias1, alias2);
	*/

	// order is important, room creater eid must before room joiner eid
//	ret =
	dbin_write(conn, "game", DB_GAME, IN_GAME_PRINT
	, proom->guest[0], proom->guest[1]);

	return ST_ROOM;
}


/**
 * this is called when a game is finished, either someone
 * win or draw game
 * TODO max round is 200 (?)
 * TODO add crystal to protocol
 * player:win [side] [fold_flag] [rating] [gold] [exp] [lv] [next_exp] [crystal]
 * guest:win [side] [fold_flag] [0.0f] [0] [0] [0] [0] [0]
 * side=1 or 2
 * fold_flag=0 or 1    (0=normal finish,  1=surrender)
 * game_end = 0 or 1 (0=normal end gamae, 1=match close)
 */
int win_game(connect_t *conn, room_t * proom, int winner, int fold_flag
, int game_end)
{
//	int game_type;
	double rating = 0.0L;
	// int gold[2], crystal[2]    gold[0]==player1 gold
	int gold1 = 0, gold2 = 0, crystal1 = 0, crystal2 = 0;
	int exp1 = 0, exp2 = 0;
	int lv_offset1 = 0, lv_offset2 = 0;  // this is lv offset
	int lv1 = 0, lv2 = 0;  
	int eid1, eid2;
	double rate_offset1 = 0, rate_offset2=0; 
	int icon1 = 0;
	int icon2 = 0;
	char alias1[EVIL_ALIAS_MAX+1];
	char alias2[EVIL_ALIAS_MAX+1];
	int ver, seed, start_side;
	long gameid;
	const char *deck1, *deck2;
	char cmd_list[EVIL_CMD_MAX+20]; // more then one cmd
	double rating1, rating2; // temp math variable
	int card_id1 = 0;
	int card_id2 = 0;
	int fold_hero_hp = 0; 
	int ai_times1 = 0, ai_times2 = 0;
	int gold_times1 = 0, gold_times2 = 0;
	int crystal_times1 = 0, crystal_times2 = 0;
	int cost_gold1 = 0, cost_gold2 = 0;
	int cost_crystal1 = 0, cost_crystal2 = 0;
	int win_hero_id = 0, lose_hero_id = 0;
	const char *cmd = "win";

	int ret;
	int len;
	if (winner != 1 && winner != 2 && winner != 9) { // winner = 9 is draw
		// TODO close lua, but do not handle db_win()
		if (conn != NULL) {
			NET_ERROR_RETURN(conn, -7, "win_is_not_1_2_9 %d", winner);
		}
	}

//	game_type = proom->game_type;
	eid1 = proom->guest[0];
	eid2 = proom->guest[1];
	rating1 = proom->rating[0];
	rating2 = proom->rating[1];
	deck1 = proom->deck[0];
	deck2 = proom->deck[1];
	gameid = proom->gameid;
	seed = proom->seed;
	start_side = proom->start_side;
	ver = lu_get_int(proom->lua, "LOGIC_VERSION");
	alias1[0] = '\0';
	alias2[0] = '\0';

	// 1.handle replay
	cmd_list[0] = 0; len = 0;
	int cmd_len = 0;
	for (size_t i=0; i<proom->cmd_list.size(); i++) {
		// max command size (30)
		cmd_len = strlen(proom->cmd_list[i].c_str());
		if (len + cmd_len + 3 >= EVIL_CMD_MAX) {
			BUG_PRINT(-2, "win_game:save_replay_overflow %d %d", len, cmd_len);
			// TODO logic limit max round
			ver = 0; // client check ver==0, no replay
			break;
		}
		len += sprintf(cmd_list + len, "%.30s;", proom->cmd_list[i].c_str());
	}

	fold_hero_hp = lu_get_hero_hp(proom->lua, 3-winner);
	lu_get_hero_id(proom->lua, winner, &win_hero_id, &lose_hero_id);

	/*
	game_income_t income1;
	game_income_t income2;
	bzero(&income1, sizeof(income1));
	bzero(&income2, sizeof(income2));

	new_win_game_income(proom, winner, fold_flag, fold_hero_hp
	, &income1, &income2);
	*/


	// 2.handle income
	if (winner == 1 || winner == 9) {
		ret = win_game_income(proom, winner, fold_flag, fold_hero_hp
		, eid1, rating1, gold1, crystal1, exp1, rate_offset1, card_id1
		, ai_times1, gold_times1, crystal_times1, cost_gold1, cost_crystal1
		, eid2, rating2, gold2, crystal2, exp2, rate_offset2, card_id2
		, ai_times2, gold_times2, crystal_times2, cost_gold2, cost_crystal2);
	}
	if (winner == 2) {
		ret = win_game_income(proom, winner, fold_flag, fold_hero_hp
		, eid2, rating2, gold2, crystal2, exp2, rate_offset2, card_id2
		, ai_times2, gold_times2, crystal_times2, cost_gold2, cost_crystal2
		, eid1, rating1, gold1, crystal1, exp1, rate_offset1, card_id1
		, ai_times1, gold_times1, crystal_times1, cost_gold1, cost_crystal1);
	}

	rating = fabs(rate_offset1); // rating == +rating_offset
	// DEBUG_PRINT(0, "win_game:eid1=%d gold1=%d exp1=%d rate_offset1=%f card_id1=%d crystal1=%d", eid1, gold1, exp1, rate_offset1, card_id1, crystal1);
	// DEBUG_PRINT(0, "win_game:eid2=%d gold2=%d exp2=%d rate_offset2=%f card_id2=%d crystal2=%d", eid2, gold2, exp2, rate_offset2, card_id2, crystal2);
	// printf("--- win_game: eid1=%d eid=2=%d rating=%lf\n", eid1, eid2, rating);


	// 3 update chapter result and reward
	chapter_result_t chapter_result;
	bzero(&chapter_result, sizeof(chapter_result));
	int chapter_star = update_chapter_data(proom, winner, chapter_result);
	int chapter_reward_pos = -1;
	update_chapter_reward(proom, winner, &chapter_reward_pos);

	int quick_reward_pos1 = -1;
	int quick_reward_pos2 = -1;
	int quick_reward_gold1 = 0;
	int quick_reward_gold2 = 0;
	int quick_reward_crystal1 = 0;
	int quick_reward_crystal2 = 0;
	update_quick_reward(proom, winner, fold_flag
	, &quick_reward_pos1, &quick_reward_pos2
	, &quick_reward_gold1, &quick_reward_gold2
	, &quick_reward_crystal1, &quick_reward_crystal2);

	// 4.send win message to room guest
	// peter: use integer based rating offset:
	double rate_int1 = floor(rating1+rate_offset1) - floor(rating1);
	double rate_int2 = floor(rating2+rate_offset2) - floor(rating2);
	int exp_next;
	int this_exp = 0;

	connect_t *player_conn;
	evil_user_t *player;
	player_conn = get_conn_by_eid(eid1);
	if (player_conn != NULL) {
		player = &player_conn->euser;
		int exp_old = player->exp;
		exp_next = add_exp(player_conn, exp1, &lv_offset1, &this_exp);	
		if (exp_next > 0) {
			send_win(proom, player_conn, eid1, winner, fold_flag, rate_int1 // rate_offset1
			, gold1, crystal1, exp1, exp_next, this_exp, card_id1
			, chapter_reward_pos, &chapter_result, quick_reward_pos1);
		}
		// DEBUG_PRINT(0, "win_game:eid1=%d exp1=%d lv_offset1=%d exp_new=%d exp_old=%d diff=%d", eid1, exp1, lv_offset1, player->exp, exp_old, player->exp-exp_old);
		exp1 = player->exp - exp_old;
	}

	player_conn = get_conn_by_eid(eid2);
	if (player_conn != NULL) {
		player = &player_conn->euser;
		int exp_old = player->exp;
		exp_next = add_exp(player_conn, exp2, &lv_offset2, &this_exp);	
		if (exp_next > 0) {
			send_win(proom, player_conn, eid2, winner, fold_flag, rate_int2 // rate_offset2
			, gold2, crystal2, exp2, exp_next, this_exp, card_id2
			, chapter_reward_pos, &chapter_result, quick_reward_pos2);
		}
		// DEBUG_PRINT(0, "win_game:eid2=%d exp2=%d lv_offset2=%d exp_new=%d exp_old=%d diff=%d", eid2, exp2, lv_offset2, player->exp, exp_old, player->exp-exp_old);
		exp2 = player->exp - exp_old;
	}

	// broadcast to other guest
	char str[500];
	send_win_str(str, proom, NULL, 0, winner, fold_flag
	, 0.0f, 0, 0, 0, 0, 0, 0, chapter_reward_pos, &chapter_result, -1);
	broadcast_room_guest(proom, 0, str);

	// 5.save into database
	icon1 = proom->icon[0];
	icon2 = proom->icon[1];
	lv1 = proom->lv[0];
	lv2 = proom->lv[1];
	sprintf(alias1, "%s", proom->alias[0]);
	sprintf(alias2, "%s", proom->alias[1]);
	// printf("win_game:alias1=%s lv1=%d icon1=%d\n", alias1, lv1, icon1);
	// printf("win_game:alias2=%s lv2=%d icon2=%d\n", alias2, lv2, icon2);

	if (alias1[0] == '\0') {
		sprintf(alias1, "_no_alias");
	}
	if (alias2[0] == '\0') {
		sprintf(alias2, "_no_alias");
	}

	// DEBUG_PRINT(0, "win_game:rating=%lf", rating);
	// need to reset user.lv, user.exp, user.gold in evil_status
	gold1 += cost_gold1 + quick_reward_gold1;
	gold2 += cost_gold2 + quick_reward_gold2;
	crystal1 += cost_crystal1 + quick_reward_crystal1;
	crystal2 += cost_crystal2 + quick_reward_crystal2;
	ret = dbin_win_param(conn, proom, winner, rating 
	, eid1, eid2, gold1, crystal1
	, gold2, crystal2
	, exp1, lv_offset1, exp2, lv_offset2
	, lv1, lv2, card_id1, card_id2
	, icon1, icon2, alias1, alias2
	, ai_times1, ai_times2, gold_times1, gold_times2
	, crystal_times1, crystal_times2
	, gameid, seed, start_side, ver, chapter_star, deck1, deck2, cmd_list);	// replay


	// 6.update player,guest mission
	// peter: TODO refactor 
	// change |= winner_mission_update(conn, eid_oppo);
	// change |= level_mission_update(conn, lv);
	// ...
	// if (change > 0) net_writeln(conn, "mchange %d", change);
	update_game_finish_mission(proom, eid1, eid2, winner
	, lv_offset1, lv_offset2, fold_flag, fold_hero_hp, win_hero_id, lose_hero_id);

	
	long match_id = proom->match_id;
	// update match result
	update_match_game_data(proom, eid1, eid2, winner);

	// this room is for ranking, then change the ranking data
	update_ranking_game_data(proom, conn, eid1, eid2, winner);

	update_arena_data(proom, conn, eid1, eid2, winner);


	// clean up the room, must be after all logic
	// peter: actually we have done this
	// TODO make it a function!   finish game etc
	// clean up the room, everyone is back to ST_LOGIN
	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i];
		g_user_room.erase(eid); // re-conn logic, must before null check

		connect_t * guest_conn = get_conn_by_eid(eid);
		if (guest_conn==NULL) {
			// this may due to offline
			// DEBUG_PRINT(eid, "win_eid_offline:erase(eid) OK");
			continue;
		}
		guest_conn->st = ST_LOGIN;
		// @see room_leave() logic
		guest_conn->room = NULL;
	}

	// proom->num_guest = 0; // not necessary, free_room do this
	ret = free_room(proom->channel, proom);
	ERROR_PRINT(ret, "win_free_room");


	// if match_id != 0, means it's match game or match end game
	// so if it's not over at this round, should start next match game
	// new round game should create room after free_room
	// DEBUG_PRINT(0, "win_game:match_id=%ld", match_id);
	if (match_id != 0 && game_end == GAME_END_NORMAL) {
		ret = create_match_game(conn, match_id, eid1, eid2, alias1, alias2);
	}

	return ST_LOGIN; 
}

// normal fold 
// require st==ST_GAME
int game_fold(connect_t *conn, const char *cmd, const char * buffer) 
{
	if (conn->st<ST_GAME) {
		NET_ERROR_RETURN(conn, -9, "fold_st %d", conn->st);	
	}

	room_t *proom = conn->room;
	if (NULL == proom) {
		NET_ERROR_RETURN(conn, -3, "fold_null_room");
	}

	if (NULL == proom->lua) {
		NET_ERROR_RETURN(conn, -13, "fold_null_lua");
	}

	int ret;
	int eid = get_eid(conn);
	int side = 0;
	int win = 0;
	if (eid == proom->guest[0]) { side = 1; }
	if (eid == proom->guest[1]) { side = 2; }

	if (0 == side) {
		NET_ERROR_RETURN(conn, -19, "fold_non_player %d", eid);
	}


	win = 3 - side; // opponent win
	//DEBUG_PRINT(win, "fold______win side=%d  guest0=%d  guest1=%d eid=%d"
	// , side, proom->guest[0], proom->guest[1], eid);


	char str[21];
	int cmd_size = (int) (proom->cmd_list.size() + 1);
	sprintf(str, "%d fold %d", cmd_size, side);
	proom->cmd_list.push_back(str);


	// NOTE: no need, because win_game() call free_room() call lua_close
	// clean: proom->lua
	// lua_close(proom->lua);
	// proom->lua = NULL;

	ret = win_game(conn, proom, win, WIN_GAME_FOLD, GAME_END_NORMAL);  // this will broadcast
	if (ret < 0)  {
		NET_ERROR_RETURN(conn, -6, "fold_win_game %d", ret);
	}
	
	return 0;
}

// offline timeout fold
// fold_side = 1 or 2  (loser side)
int force_fold(room_t *proom, int fold_side)
{
	int win = 0;
	connect_t *conn;

	if (1 != fold_side && 2 != fold_side) {
		ERROR_RETURN(-6, "force_fold_non_player %d", fold_side);
	}
	win = 3 - fold_side;

	char str[100];
	int cmd_size = (int) (proom->cmd_list.size() + 1);
	sprintf(str, "%d fold %d", cmd_size, fold_side);
	proom->cmd_list.push_back(str);

	conn = get_conn_by_eid(proom->guest[0]);
	if (conn == NULL) {
		conn = get_conn_by_eid(proom->guest[1]);
	}

	// both conn may null, win_game() should check null
	int ret = win_game(conn, proom, win, WIN_GAME_FOLD, GAME_END_NORMAL);  // this will broadcast
	if (ret < 0)  {
		ERROR_RETURN(-16, "force_fold_win_game %d eid1=%d  eid2=%d", ret
		, proom->guest[0], proom->guest[1]);
	}

	return 0;
}


// check: one guest inside the room
// @param offline_time is the input/output param
// auto_fold -> auto_fold_room -> auto_fold_guest
int auto_fold_guest(time_t now, int eid, time_t & offline_time)
{
	connect_t *conn;
	// TODO move offline_fold_time to design.constant
	// const int OFFLINE_FOLD_TIME = 15; //240;

	if (eid <= MAX_AI_EID) {
		return 0; // early exit
	}
	conn = get_conn_by_eid(eid);  // global access

	// user offline, initialize the offline time if not init
	// user still online, reset offline_time =  ???
	if (conn != NULL) {
		offline_time = 0;
		return 0;
	}

	if (offline_time <= 0) {
		offline_time = now;
		// return 0;  // can be omit, why?  when OFFLINE_FOLD_TIME=0
	}

	if (now - offline_time >= OFFLINE_FOLD_TIME) {
		// do force fold logic
		// force_fold(proom, 1);
		return 1; // means caller need to force fold 
	}

	return 0;
}


int auto_fold_room(time_t now, room_t *proom)
{
	int eid;
	int ret;
	// INFO_PRINT(0, "auto_fold_room:start");

	eid = proom->guest[0];
	ret = auto_fold_guest(now, eid, proom->offline_time[0]);
	if (ret == 1) {
		ret = force_fold(proom, 1);  // guest[0] fold 1 side
		ERROR_PRINT(ret, "auto_fold:force_fold err eid=%d", eid);
		return ret;  // force_fold may hv error, still early exit
	}

	eid = proom->guest[1];
	ret = auto_fold_guest(now, eid, proom->offline_time[1]);
	if (ret == 1) {
		ret = force_fold(proom, 2);  // guest[1] fold 0 side
		ERROR_PRINT(ret, "auto_fold:force_fold err eid=%d", eid);
		return ret;
	}

	return 0;
}

// 
// check every 30sec 
// - check each proom (loop channel, loop room inside channel)
// - @each room:
//   if (offline_time[0] <= 0)  offline_time[0] = now  // init
int auto_fold(time_t now) 
{
	// logic to make sure this logic interval >= N second
	static time_t last_force_fold = 0;
	if (now - last_force_fold < 30) {
		return 0;
	}
	last_force_fold = now;
	// DEBUG_PRINT(0, "auto_fold %ld", now);

	for (int c=0; c<MAX_CHANNEL; c++) {
		for (int r=1; r<=MAX_ROOM; r++) {
			room_t *proom = &(g_room_list[c][r]);
			if (proom==NULL) { continue; }
			if (proom->num_guest < 2) {continue; }
			if (proom->state!=ST_GAME) {continue; }

			auto_fold_room(now, proom);
		}
	}

	return 0;
}

int match_init(match_t *match);
int match_round(match_t * match);

int auto_match_round(time_t now) 
{
	// logic to make sure this logic interval >= N second
	static time_t last_force_round = 0;

	struct tm timestruct;
	time_t today;
	int ret;

	if (now - last_force_round < MATCH_CHECK_DURATION) {
		return 0;
	}
	last_force_round = now;
	// DEBUG_PRINT(0, "auto_fold %ld", now);

	localtime_r(&now, &timestruct);
	timestruct.tm_sec = 0;
	timestruct.tm_min = 0;
	timestruct.tm_hour = 0;
	today = mktime(&timestruct);

	for (int i = 0; i < MAX_MATCH; i++) {
		match_t & match = g_match_list[i];
		if (match.match_id == 0) {
			continue;
		}
		if (match.status == MATCH_STATUS_FINISHED 
		|| match.status == MATCH_STATUS_DELETE) {
			continue;
		}

		// XXX do match init here?
		// this is one time job
		if (match.status == MATCH_STATUS_READY) {
			if (now < match.start_time) {
				continue;
			}
			ret = match_init(&match);
			if (ret != 0) {
				BUG_RETURN(-6, "auto_match_round:match_init_fail %d %ld"
				, ret, match.match_id); 
			}
			continue;
		}

		for (int t = 0; t < MAX_DAILY_ROUND; t++) {
			time_t tt = match.round_time_list[t];
			if (tt == 0) {
				continue;
			}
			// do round start
			if ((now >= today + tt)
			&& (now < today + tt + MATCH_CHECK_DURATION)) {
				if (match.status != MATCH_STATUS_ROUND_END) {
					BUG_RETURN(-6, "auto_match_round:status_mismatch %d"
					, match.status); 
				}
				ret = match_round(&match);
				if (ret != 0) {
					BUG_PRINT(-16, "auto_match_round:round_start_error");
				}

				match_info(match, match.player_list);
				match_eli_info(match, match.e_player_list);
			}

			// do round end
			if ((now >= today + tt + MATCH_ROUND_TIME) 
			&& (now < today + tt + MATCH_ROUND_TIME + MATCH_CHECK_DURATION)) {
				if (match.status != MATCH_STATUS_ROUND_START) {
					// may caused by load a round start match in db
					WARN_PRINT(-26, "auto_match_round:status_mismatch %d %d"
					, match.status, MATCH_STATUS_ROUND_START); 
					return -26;
				}
				ret = match_round(&match);
				if (ret != 0) {
					BUG_PRINT(-36, "auto_match_round:round_end_error");
				}

				match_info(match, match.player_list);
				match_eli_info(match, match.e_player_list);
			}
		}


	}

	return 0;
}




// 1. if user is a guest, not player in room, leave that room
// 2. if user is a player in room, force fold game
int force_leave_room(int eid) {
	int ret;
	room_t * proom;
	connect_t * conn;

	if (eid <= MAX_AI_EID) {
		return 0;
	}

	proom = g_user_room[eid];
	if (NULL == proom) {
		return 0;
	}

	if (proom->state==ST_NULL) {
		BUG_PRINT(-7, "force_leave_room:room_state_0");
		return -7;
	} 

	conn = get_conn_by_eid(eid);

	// 1. game not start, leave this room
	// 2. user just a guest, watch game, so leave this room
	if ((proom->state == ST_ROOM) || (proom->state == ST_GAME && proom->guest[0] != eid && proom->guest[1] != eid)) {

		int clean_flag = room_clean_by_eid(proom, eid);  

		if (conn != NULL) {
			net_writeln(conn, "leave %d %d", eid, ST_LOGIN); 
		}

		if (clean_flag != 0) {
			// broadcast leave [my_eid] to all guests, only if room is there
			char str[30];
			sprintf(str, "leave %d %d", eid, proom->state);
			ret = broadcast_room(proom, eid, str);  

			ret = do_room_del(proom, conn, eid);
			if (ret < 0) {
				BUG_RETURN(-17, "leave_room_del %d", ret);
			}
		}
		
		return 0;
	}

	// user is a player, use force fold
	if (proom->state != ST_GAME) {
		return 0;
	}

	int fold_side = 0;
	fold_side =  (proom->guest[0] == eid) ? 1 : fold_side;
	fold_side =  (proom->guest[1] == eid) ? 2 : fold_side;
	if (fold_side == 0) {
		BUG_RETURN(-7, "force_leave_room:player_side_bug");
	}

	ret = force_fold(proom, fold_side);
	ERROR_PRINT(ret, "force_leave_room:force_fold err eid=%d", eid);

	return ret;

}

// end a match game 
int force_end_match_game(room_t *proom)
{
	if (proom == NULL) {
		BUG_RETURN(-6, "force_leave_room:room_null");
	}
	if (proom->state != ST_GAME) {
		BUG_RETURN(-7, "force_leave_room:room_state_error %d"
		, proom->state);
	} 

	int win = 9;
	connect_t *conn;

	/*
	char str[100];
	int cmd_size = (int) (proom->cmd_list.size() + 1);
	sprintf(str, "%d fold %d", cmd_size, fold_side);
	proom->cmd_list.push_back(str);
	*/

	conn = get_conn_by_eid(proom->guest[0]);
	if (conn == NULL) {
		conn = get_conn_by_eid(proom->guest[1]);
	}

	// both conn may null, win_game() should check null
	int ret = win_game(conn, proom, win, WIN_GAME_NORMAL
	, GAME_END_MATCH_CLOSE);  // game end by match round end
	if (ret < 0)  {
		ERROR_RETURN(-16, "force_fold_win_game %d eid1=%d  eid2=%d", ret
		, proom->guest[0], proom->guest[1]);
	}

	return 0;
}

// end 2 player round 
int force_end_match_round(match_t &match, match_player_t &player1, match_player_t &player2)
{
	int ret;

	// 1. check 2 player in the same room
	// 2. draw game
	// 3. check point get winner
	// 4. if point same, get player1 win
	// 5. update fake match_result()

	room_t * proom = NULL;
	room_t * proom1 = NULL;
	room_t * proom2 = NULL;
	proom1 = g_user_room[player1.eid];
	if (NULL == proom1 && player1.eid > MAX_AI_EID) {
		BUG_RETURN(-6, "force_end_match_round:player_has_no_room %d"
		, player1.eid);
	}

	proom2 = g_user_room[player2.eid];
	if (NULL == proom2 && player2.eid > MAX_AI_EID) {
		BUG_RETURN(-16, "force_end_match_round:player_has_no_room %d"
		, player2.eid);
	}

	if (proom1 != proom2 && player1.eid > MAX_AI_EID
	&& player2.eid > MAX_AI_EID) {
		BUG_RETURN(-26, "force_end_match_round:not_in_same_room %d %d"
		, player1.eid, player2.eid);
	}

	proom = (proom1 != NULL) ? proom1 : proom2;
	ret = force_end_match_game(proom);
	if (ret != 0) {
		BUG_RETURN(-36, "force_end_match_round:force_end_match_game_fail  %d %d"
		, player1.eid, player2.eid);
	}

	if (player1.point >= 5 || player2.point >= 5) {
		return 0;
	}

	// 1. player1.point >= player2.point, player1 win
	// 2. player1.point < player2.point, player2 win
	match_player_t * win_player;
	match_player_t * lose_player;
	win_player = (player1.point >= player2.point) ? &player1 : &player2;
	lose_player = (player1.point >= player2.point) ? &player2 : &player1;

	// do fake match result
	ret = match_result(match
	, win_player->eid, lose_player->eid, 1, 1);
	if (ret != 0) {
		ERROR_RETURN(ret, "force_end_match_round:match_result_fail");
	}
	if (player1.point >= 5 || player2.point >= 5) {
		// write match result into db
		ret = save_match_result(match, player1, player2);
		if (ret <= 0) {
			ERROR_RETURN(ret, "force_end_match_round:save_match_result_fail");
		}
		return 0;
	}

	ret = match_result(match
	, win_player->eid, lose_player->eid, 1, 1);
	if (ret != 0) {
		ERROR_RETURN(ret, "force_end_match_round:match_result_fail");
	}
	if (player1.point >= 5 || player2.point >= 5) {
		// write match result into db
		ret = save_match_result(match, player1, player2);
		if (ret <= 0) {
			ERROR_RETURN(ret, "force_end_match_round:save_match_result_fail");
		}
		return 0;
	}

	// one player.point should >= 5 now, no way to get here
	BUG_RETURN(-77, "admin_eli_round_end:fake_match_result_bug %d %d", player1.eid, player2.eid);

}

#ifdef HCARD
// for H5 game server
int ai_play_once_old(connect_t *conn)
{
    room_t *proom = conn->room;
    int ret;
    if (NULL == proom) {
        BUG_RETURN(-3, "ai_play_proom_null eid=%d", conn->euser.eid);
    }

    int eid = proom->guest[0];
    int ai_eid = proom->guest[1]; // hard code
    if (ai_eid > MAX_AI_EID) {
        BUG_RETURN(-6, "ai_play_invalid_ai_eid %d", ai_eid);
    }
    if (eid != conn->euser.eid) { // conn->euser is a watcher
        // watcher does not trigger this
        return -9;
    }

    if (NULL == proom->lua) {
        BUG_RETURN(-13, "ai_play_lua_null eid %d ai_eid %d", eid, ai_eid);
    }

    LU_CHECK_MAGIC(proom->lua);

    // int current_side = lu_get_int(proom->lua, "g_current_side");
    int current_side = mylua_get_table_int(proom->lua, "g_logic_table", "current_side");
    int my_side = (ai_eid == proom->guest[0]) ? 1 : 2; // we may hard code 2
    // assert(my_side==2);
    if (my_side != 2) {
        BUG_PRINT(-7, "ai_play_once:my_side_not_2 %d", my_side);
        return -7;
    }
    if (current_side != my_side) {
        // DEBUG_PRINT(9, "ai_play_not_my_side normal");
        return -9;
    }

    // ai_global  or using ai_cmd_globa ? 
    // note: get_ai_sac(), get_ai_play, then play_cmd
    //traceback

    lua_getglobal(proom->lua, "ai_cmd_global");
    // ret = lua_pcall(proom->lua, 0, 1, 0);  // 1=input param, 1=output return
    ret = lu_trace_call(proom->lua, 0, 1); // 1=input param, 1=output return
    if (ret != 0) {
        // XXX TODO send 'n' to skip this ai round !
        ERROR_PRINT(ret, "ai_cmd_global:error_use_next");
        lua_error_print(proom->lua, conn, ret, "ai_play_once:ai_cmd_global");
        LU_CHECK_MAGIC(proom->lua);
        // return -68;
        lua_pushstring(proom->lua, "n");
    }
    char cmd[100] = {'\0'};
    // char out_cmd[101] = {'\0'};
    char out_eff[500] = {'\0'};
    strncpy(cmd, lua_tostring(proom->lua, -1), 99); cmd[99] = '\0';
    WARN_PRINT( strlen(cmd) > 90, "ai_cmd_global:cmd_too_long %zu", strlen(cmd));
    lua_pop(proom->lua, 1); // pop the cmd returned 

    LU_CHECK_MAGIC(proom->lua);

    // need to check lua
    // note: win(-4), g_current_side, g_phase, err(-1);
    // traceback
    lua_getglobal(proom->lua, "play_cmd_global");
    lua_pushstring(proom->lua, cmd);  // include "at" command
    // ret = lua_pcall(proom->lua, 1, 4, base);  // 1=input param, 4=output return
    ret = lu_trace_call(proom->lua, 1, 5); // 1=input param, 5=output return
    if (ret != 0) {
        ERROR_PRINT(ret, "ai_play_once:play_cmd_global");
        lua_error_print(proom->lua, conn, ret, "ai_play_once:lua_error");
        // TODO send 'n' to skip next ?  or simply let user fold?
        LU_CHECK_MAGIC(proom->lua);

        // retry with a simple next command for ai
        lua_getglobal(proom->lua, "play_cmd_global");
        strcpy(cmd, "n");  // n to skip
        lua_pushstring(proom->lua, cmd);  // n to skip
        ret = lua_pcall(proom->lua, 1, 5, 0);  // 1=input param, 5=output return
        if (ret != 0) {
            lua_error_print(proom->lua, conn, ret, "ai_play_once:lua_error");
            LU_CHECK_MAGIC(proom->lua);
            return -68;
        }
        // implicit: ret = 0
    }

    if (lua_isstring(proom->lua, -1))  { // err message
        net_writeln(conn, "gerr -18 ai_play_lua_error %s"
        , lua_tostring(proom->lua, -1));
        ERROR_NEG_PRINT(-18, "ai_play_lua_error %s"
        , lua_tostring(proom->lua, -1));
        lua_pop(proom->lua, 5);  // pop 4 return value
        // no broadcast, early exit
        LU_CHECK_MAGIC(proom->lua);
        // we can indeed try 'n' later, beware infinite loop!
        return -18;
    }

    int winner = lua_tointeger(proom->lua, -5);
    const char *eff_list = lua_tostring(proom->lua, -2);
    int cmd_size = (int)(proom->cmd_list.size() + 1);
    // sprintf(out_cmd, "%d %s", cmd_size, cmd);
    DEBUG_PRINT(0, "ai_play_once:winner=%d eff_list=%s", winner, eff_list);
    sprintf(out_eff, "%d %s", cmd_size, eff_list);
    // add the command to the cmd_list
    proom->cmd_list.push_back(out_eff);
    lua_pop(proom->lua, 5);  // pop 4 return value 

    LU_CHECK_MAGIC(proom->lua);
    ret = broadcast_room(proom, 0, out_eff);
    if (ret < 0) {
        BUG_PRINT(ret, "ai_play_once broadcast error");
    }
    // net_write(conn, cmd, '\n');
    // DEBUG_PRINT(0, "AI PLAY CMD OK: [%s]  win=%d", cmd, winner);

    return winner;
}

#else

// 9 means not my side
// 0 : means play command ok, 
// 1,2 : means play command ok and someone one win (win=1 or 2)
// other negative are error
int ai_play_once_old(connect_t *conn)
{
	room_t *proom = conn->room;
	int ret;
	if (NULL == proom) {
		BUG_RETURN(-3, "ai_play_proom_null eid=%d", conn->euser.eid);
	}

	int eid = proom->guest[0];
	int ai_eid = proom->guest[1]; // hard code
	if (ai_eid > MAX_AI_EID) {
		BUG_RETURN(-6, "ai_play_invalid_ai_eid %d", ai_eid);
	}
	if (eid != conn->euser.eid) { // conn->euser is a watcher
		// watcher does not trigger this
		return -9;
	}

	if (NULL == proom->lua) {
		BUG_RETURN(-13, "ai_play_lua_null eid %d ai_eid %d", eid, ai_eid);
	}

	LU_CHECK_MAGIC(proom->lua);

	int current_side = lu_get_int(proom->lua, "g_current_side");
	int my_side = (ai_eid == proom->guest[0]) ? 1 : 2; // we may hard code 2
	// assert(my_side==2);
	if (my_side != 2) {
		BUG_PRINT(-7, "ai_play_once:my_side_not_2 %d", my_side);
		return -7;
	}
	if (current_side != my_side) {
		// DEBUG_PRINT(9, "ai_play_not_my_side normal");
		return -9;
	}

	// ai_global  or using ai_cmd_globa ? 
	// note: get_ai_sac(), get_ai_play, then play_cmd
	//traceback

	lua_getglobal(proom->lua, "ai_cmd_global");
	// ret = lua_pcall(proom->lua, 0, 1, 0);  // 1=input param, 1=output return
	ret = lu_trace_call(proom->lua, 0, 1); // 1=input param, 1=output return
	if (ret != 0) {
		// XXX TODO send 'n' to skip this ai round !
		ERROR_PRINT(ret, "ai_cmd_global:error_use_next");
		lua_error_print(proom->lua, conn, ret, "ai_play_once:ai_cmd_global");
		LU_CHECK_MAGIC(proom->lua);
		// return -68;
		lua_pushstring(proom->lua, "n");
	}
	char cmd[100] = {'\0'};
	char out_cmd[101] = {'\0'};
	strncpy(cmd, lua_tostring(proom->lua, -1), 99); cmd[99] = '\0';
	WARN_PRINT( strlen(cmd) > 90, "ai_cmd_global:cmd_too_long %zu", strlen(cmd));
	lua_pop(proom->lua, 1); // pop the cmd returned 
	
	LU_CHECK_MAGIC(proom->lua);

	// need to check lua
	// note: win(-4), g_current_side, g_phase, err(-1);
	// traceback
	lua_getglobal(proom->lua, "play_cmd_global");
	lua_pushstring(proom->lua, cmd);  // include "at" command
	// ret = lua_pcall(proom->lua, 1, 4, base);  // 1=input param, 4=output return
	ret = lu_trace_call(proom->lua, 1, 4); // 1=input param, 4=output return
	if (ret != 0) {
		ERROR_PRINT(ret, "ai_play_once:play_cmd_global");
		lua_error_print(proom->lua, conn, ret, "ai_play_once:lua_error");
		// TODO send 'n' to skip next ?  or simply let user fold?
		LU_CHECK_MAGIC(proom->lua);

		// retry with a simple next command for ai
		lua_getglobal(proom->lua, "play_cmd_global");
		strcpy(cmd, "n");  // n to skip
		lua_pushstring(proom->lua, cmd);  // n to skip
		ret = lua_pcall(proom->lua, 1, 4, 0);  // 1=input param, 4=output return
		if (ret != 0) {
			lua_error_print(proom->lua, conn, ret, "ai_play_once:lua_error");
			LU_CHECK_MAGIC(proom->lua);
			return -68;
		}
		// implicit: ret = 0
	}
	
	if (lua_isstring(proom->lua, -1))  { // err message
		net_writeln(conn, "gerr -18 ai_play_lua_error %s"
		, lua_tostring(proom->lua, -1));
		ERROR_NEG_PRINT(-18, "ai_play_lua_error %s"
		, lua_tostring(proom->lua, -1));
		lua_pop(proom->lua, 4);  // pop 4 return value
		// no broadcast, early exit
		LU_CHECK_MAGIC(proom->lua);
		// we can indeed try 'n' later, beware infinite loop!
		return -18;
	}
	
	int	winner = lua_tointeger(proom->lua, -4);
	int cmd_size = (int)(proom->cmd_list.size() + 1);
	sprintf(out_cmd, "%d %s", cmd_size, cmd);
	// add the command to the cmd_list
	proom->cmd_list.push_back(out_cmd);
	lua_pop(proom->lua, 4);  // pop 4 return value 

	LU_CHECK_MAGIC(proom->lua);
	ret = broadcast_room(proom, 0, out_cmd);
	if (ret < 0) {
		BUG_PRINT(ret, "ai_play_once broadcast error");
	}
	// net_write(conn, cmd, '\n');
	// DEBUG_PRINT(0, "AI PLAY CMD OK: [%s]  win=%d", cmd, winner);

	return winner;
}
#endif

int lu_get_ai_cmd(lua_State *L, char * cmd, int size)
{
	int ret;
	if (NULL == L) {
		BUG_RETURN(-3, "lu_get_ai_cmd:lua_null");
	}

	lua_getglobal(L, "ai_cmd_global");
	ret = lu_trace_call(L, 0, 1); // 1=input param, 1=output return
	if (ret != 0) {
		// send 'n' to skip this ai round !
		ERROR_PRINT(ret, "lu_get_ai_cmd:error_use_next");
		lua_error_print(L, NULL, ret, "lu_get_ai_cmd:ai_cmd_global");
		LU_CHECK_MAGIC(L);
		lua_pushstring(L, "n");
	}
	strncpy(cmd, lua_tostring(L, -1), size); 
	cmd[size] = '\0';
	WARN_PRINT( strlen(cmd) > 90, "lu_get_ai_cmd:cmd_too_long %zu", strlen(cmd));
	lua_pop(L, 1);
	
	LU_CHECK_MAGIC(L);
	return 0;
}

int lu_play_ai_cmd(lua_State *L, char * cmd, int *winner)
{
	int ret;
	if (NULL == L) {
		BUG_RETURN(-3, "lu_play_ai_cmd:lua_null");
	}
	// note: win(-4), g_current_side, g_phase, err(-1);
	lua_getglobal(L, "play_cmd_global");
	lua_pushstring(L, cmd);  // include "at" command
	ret = lu_trace_call(L, 1, 4); // 1=input param, 4=output return
	if (ret != 0) {
		ERROR_PRINT(ret, "lu_play_ai_cmd:play_cmd_global");
		lua_error_print(L, NULL, ret, "lu_play_ai_cmd:lua_error");
		LU_CHECK_MAGIC(L);

		// retry with a simple next command for ai
		lua_getglobal(L, "play_cmd_global");
		strcpy(cmd, "n");  // n to skip
		lua_pushstring(L, cmd);  // n to skip
		ret = lua_pcall(L, 1, 4, 0);  // 1=input param, 4=output return
		if (ret != 0) {
			lua_error_print(L, NULL, ret, "lu_play_ai_cmd:lua_error");
			LU_CHECK_MAGIC(L);
			BUG_RETURN(-8, "lu_play_ai_cmd:lua_bug");
		}
	}
	
	if (lua_isstring(L, -1))  { // err message
		ERROR_NEG_PRINT(-18, "ai_play_all:lua_error %s", lua_tostring(L, -1));
		lua_pop(L, 4);  // pop 4 return value
		// no broadcast, early exit
		LU_CHECK_MAGIC(L);
		// we can indeed try 'n' later, beware infinite loop!
		ERROR_RETURN(-18, "lu_play_ai_cmd:lua_play_error");
	}
	
	*winner = lua_tointeger(L, -4);
	lua_pop(L, 4);  // pop 4 return value 

	LU_CHECK_MAGIC(L);
	return 0;
}

int lu_get_round(lua_State *L)
{
	if (NULL == L) {
		BUG_RETURN(-3, "lu_play_ai_cmd:lua_null");
	}

	int round = lu_get_int(L, "g_round");
	return round;
}

// 9 means not my side
// 0 : means play command ok, 
// 1,2 : means play command ok and someone one win (win=1 or 2)
// other negative are error
int ai_play_once(connect_t *conn, int flag_check_side)
{
	room_t *proom = conn->room;
	int ret;
	if (NULL == proom) {
		BUG_RETURN(-3, "ai_play_once:proom_null eid=%d", conn->euser.eid);
	}

	int eid = proom->guest[0];
	int ai_eid = proom->guest[1]; // hard code
	if (ai_eid > MAX_AI_EID) {
		BUG_RETURN(-6, "ai_play_once:invalid_ai_eid %d", ai_eid);
	}
	if (eid != conn->euser.eid) { // conn->euser is a watcher
		// watcher does not trigger this
		return -9;
	}

	if (NULL == proom->lua) {
		BUG_RETURN(-13, "ai_play_once:lua_null eid %d ai_eid %d", eid, ai_eid);
	}

	LU_CHECK_MAGIC(proom->lua);

	if (flag_check_side) {
		int current_side = lu_get_int(proom->lua, "g_current_side");
		int my_side = (ai_eid == proom->guest[0]) ? 1 : 2; // we may hard code 2
		if (my_side != 2) {
			BUG_PRINT(-7, "ai_play_once:my_side_not_2 %d", my_side);
			return -7;
		}
		if (current_side != my_side) {
			return -9;
		}
	}

	char cmd[100] = {'\0'};
	char out_cmd[101] = {'\0'};

	ret = lu_get_ai_cmd(proom->lua, cmd, 99);
	if (ret != 0) {
		BUG_RETURN(-6, "ai_play_once:get_ai_cmd");
	}

	int winner = 0;
	ret = lu_play_ai_cmd(proom->lua, cmd, &winner);
	if (ret != 0) {
		BUG_RETURN(-16, "ai_play_once:play_ai_cmd");
	}

	int cmd_size = (int)(proom->cmd_list.size() + 1);
	sprintf(out_cmd, "%d %s", cmd_size, cmd);
	// add the command to the cmd_list
	proom->cmd_list.push_back(out_cmd);

	ret = broadcast_room(proom, 0, out_cmd);
	if (ret < 0) {
		BUG_PRINT(ret, "ai_play_once:broadcast error");
	}
	DEBUG_PRINT(0, "AI PLAY CMD OK: [%s]  win=%d", cmd, winner);

	return winner;
}

int ai_play(connect_t *conn) 
{
	int ret;
	room_t *proom;
	proom = conn->room;

	if (NULL == proom) {
		ERROR_NEG_RETURN(-3, "ai_play_room_null");
	}

	// normal ai
	if (!proom->auto_battle) {
		int counter = 0;
		while ( (ret=ai_play_once(conn, 1))==0 ) {  
			counter++;  
			if (counter > 50) { // assume not too many move!
				BUG_PRINT(-27, "ai_play:loop_overflow %d", counter);
				break;
			}
		} // nothing in loop
	} else {
		// arena auto play
		int counter = 0;
		while ( (ret=ai_play_once(conn, 0))==0 ) {  
			counter++;  
			if (counter%20 == 0) { 
				// every 20 moves, check g_round
				int round = lu_get_round(proom->lua);
				if (round < 0) {
					BUG_RETURN(-27, "ai_play:round_error %d", round);
				}
				if (round > 100) {
					WARN_PRINT(-66, "ai_play:round_overflow %d", round);
					ret = 2; // ai win
					char str[21];
					int cmd_size = (int) (proom->cmd_list.size() + 1);
					sprintf(str, "%d fold %d", cmd_size, 2);
					proom->cmd_list.push_back(str);
					break;
				}
			}
		} 
	}

	// ERROR_NEG_RETURN(ret, "ai_play_once:error");
	// ai may draw game
	if (ret==1 || ret==2 || ret == 9) {
		return win_game(conn, proom, ret, WIN_GAME_NORMAL, GAME_END_NORMAL); 
	}
	return 0;

}


// force_next[0] = guest[0] being forced next time
// when i press 'n', i will reset force next of my side
// ------
// return 0 for ok,  return 1=force next 3 times, need to fold
// force_next_check("n", fn, fn_oppo) -> fn = 0,   return 0
// force_next_check("f", fn, fn_oppo) -> fn_oppo++, 
//     if fn_oppo >= 3 return 1;  (else return 0)
int force_next_check(const char *cmd, int &force_next, int &force_next_oppo)
{

	if (strcmp(cmd, "n")==0) {
		force_next = 0;
		return 0;  // early exit
	}

	if (strcmp(cmd, "f") == 0) {
		force_next_oppo ++;
		if (force_next_oppo >= MAX_FORCE_NEXT) {
			return 1;
		}
	}

	// reset of the case
	return 0;
}

int get_play_list(connect_t *conn, const char *cmd, const char * buf)
{
    int ret;
    room_t *proom = conn->room;
    if (proom == NULL) {
		NET_ERROR_RETURN(conn, -3, "get_play_list:room_null");
	}
    int eid = get_eid(conn);
    if (eid != proom->guest[0] && eid != proom->guest[1]) {
		NET_ERROR_RETURN(conn, -6, "get_play_list:not_player");
	}
	char out_buffer[2000];
    ret = check_play(conn, cmd, out_buffer);
    if (ret < 0) {
        return ret;  // early exit, error message already sent in check_play()
    }
	if (proom->lua == NULL) {
		NET_ERROR_RETURN(conn, -16, "get_play_list:lua_null");
	}

	ret = lu_get_all_play(proom->lua, out_buffer);
	net_writeln(conn, "%s %s", cmd, out_buffer);
	return 0;
}

#ifdef HCARD
// hcard game play_cmd
// @see logic.lua : play_cmd_global()
int play_cmd(connect_t *conn, const char *cmd, const char * buf)
{
    int ret;
    int win = 0;
    char buffer[100];
    char out_buffer[500]; // save eff_list
    sprintf(buffer, "%s %.80s", cmd, buf); // assume length of buf < 80
    trim(buffer, 99); // peter: remove \n
    ret = check_play(conn, cmd, buffer);
    if (ret < 0) {
        return ret;  // early exit, error message already sent in check_play()
    }
    room_t *proom = conn->room;
    int eid = get_eid(conn);
    // clean player force_next


    // force next logic @see force_next_check
    if (eid == proom->guest[0]) {
        // eid1 playing round
        ret = force_next_check(cmd, proom->force_next[0]
            , proom->force_next[1]);
        if (ret > 0) {  // > 0 means reach the max fold count
            ret = force_fold(proom, 2);  // side 1 round, fold 2
            return ret; // early exit
        }
    } else {
        // eid2 playing round
        ret = force_next_check(cmd, proom->force_next[1]
            , proom->force_next[0]);
        if (ret > 0) {
            ret = force_fold(proom, 1);  // side 2 round, fold 1
            return ret; // early exit
        }
    }



    // force-next "f" become "n"
    if (strcmp(cmd, "f") == 0) {
        cmd = "n";
        sprintf(buffer, "%s", cmd); // assume length of buf < 80
    }
    // DEBUG_PRINT(0, "play_cmd:force_next= %d %d", proom->force_next[0]
    // , proom->force_next[1]);

    // make sure stack top/bottom is the magic number 1974
    ret = lua_tonumber(proom->lua, -1);

    // need to check lua
    lua_getglobal(proom->lua, "play_cmd_global");
    lua_pushstring(proom->lua, buffer);  // include "at" command
    ret = lua_pcall(proom->lua, 1, 5, 0);  // 1=input param, 5=output return
    if (ret != 0) {
        lua_error_print(proom->lua, conn, ret, "play_cmd:play_cmd_global");
        LU_CHECK_MAGIC(proom->lua);
        return -68;
    }
    if (lua_isstring(proom->lua, -1))  { // err message
        ret = -1;
        int current_side = mylua_get_table_int(proom->lua, "g_logic_table", "current_side");
        int phase = mylua_get_table_int(proom->lua, "g_logic_table", "phase");
        net_writeln(conn, "gerr -1 lua_error %s", lua_tostring(proom->lua, -1));
        ERROR_NEG_PRINT(-1, "lua_error %s gameid=%ld  eid1=%d eid2=%d cmd=%s", lua_tostring(proom->lua, -1), proom->gameid, proom->guest[0], proom->guest[1], buffer);
        ERROR_NEG_PRINT(-1, "lua_error : current_side=%d  phase=%d"
        , current_side
        , phase  );

        lua_pop(proom->lua, 5);  // pop 5 return value
        LU_CHECK_MAGIC(proom->lua);
        // no broadcast, early exit
        return -1;
    } else {
        win = lua_tointeger(proom->lua, -5);
        const char *eff_list = lua_tostring(proom->lua, -2);
        DEBUG_PRINT(0, "play_cmd:OK [%s]  win=%d eff_list=%s", buffer, win, eff_list);
        ret = 0;

        int cmd_size = (int)(proom->cmd_list.size() + 1);
        // old logic, save cmd
        // sprintf(out_buffer, "%d %s", cmd_size, buffer);
        // new logic, save eff
        sprintf(out_buffer, "%d %s", cmd_size, eff_list);
        // printf("play_cmd:out_buffer=%s \n", out_buffer);
        // add the command to the cmd_list
        proom->cmd_list.push_back(out_buffer);
    }
    lua_pop(proom->lua, 5);  // pop 5 return value

    LU_CHECK_MAGIC(proom->lua);

    // TODO force-next, give a timeout after n 
    if (strcmp(cmd, "n") == 0) {
        // sprintf(out_buffer, "%zu n %d", proom->cmd_list.size(), g_design->constant.max_timeout + 1);
        proom->game_timeout = time(NULL) + g_design->constant.max_timeout;
    }


    // (proom, except, buffer)
    ret = broadcast_room(proom, 0, out_buffer); // except=0 means sent to all

    // early exit if no winner
    if (win <= 0) {
        if (proom->num_guest >= 2 && proom->guest[1]<=MAX_AI_EID) {
            DEBUG_PRINT(0, "play_cmd:ai_play");
            return ai_play(conn);
        }
        return ret;
    }

    return win_game(conn, proom, win, WIN_GAME_NORMAL, GAME_END_NORMAL);
}
#else

// normal game pla_cmd, not hcard
// @see logic.lua : play_cmd_global()
int play_cmd(connect_t *conn, const char *cmd, const char * buf) 
{
	int ret;
	int win = 0;
	char buffer[100];
	// char out_buffer[101];
	char out_buffer[101];
	sprintf(buffer, "%s %.80s", cmd, buf); // assume length of buf < 80
	trim(buffer, 99); // peter: remove \n
	ret = check_play(conn, cmd, buffer);
	if (ret < 0) {
		return ret;  // early exit, error message already sent in check_play()
	}
	room_t *proom = conn->room;
	int eid = get_eid(conn);
	// clean player force_next


	// force next logic @see force_next_check
	if (eid == proom->guest[0]) {
		// eid1 playing round
		ret = force_next_check(cmd, proom->force_next[0]
			, proom->force_next[1]);
		if (ret > 0) {  // > 0 means reach the max fold count
			ret = force_fold(proom, 2);  // side 1 round, fold 2
			return ret; // early exit
		}
	} else {
		// eid2 playing round
		ret = force_next_check(cmd, proom->force_next[1]
			, proom->force_next[0]);
		if (ret > 0) {
			ret = force_fold(proom, 1);  // side 2 round, fold 1
			return ret; // early exit
		}
	}



	// force-next "f" become "n"
	if (strcmp(cmd, "f") == 0) {
		cmd = "n";
		sprintf(buffer, "%s", cmd); // assume length of buf < 80
	}
	// DEBUG_PRINT(0, "play_cmd:force_next= %d %d", proom->force_next[0]
	// , proom->force_next[1]);

	// make sure stack top/bottom is the magic number 1974
	ret = lua_tonumber(proom->lua, -1);

	// need to check lua
	lua_getglobal(proom->lua, "play_cmd_global");
	lua_pushstring(proom->lua, buffer);  // include "at" command
	ret = lua_pcall(proom->lua, 1, 4, 0);  // 1=input param, 4=output return
	if (ret != 0) {
		lua_error_print(proom->lua, conn, ret, "play_cmd:play_cmd_global");
		LU_CHECK_MAGIC(proom->lua);
		return -68;
	}
	if (lua_isstring(proom->lua, -1))  { // err message
		ret = -1;
		net_writeln(conn, "gerr -1 lua_error %s", lua_tostring(proom->lua, -1));
		ERROR_NEG_PRINT(-1, "lua_error %s gameid=%ld  eid1=%d eid2=%d cmd=%s", lua_tostring(proom->lua, -1), proom->gameid, proom->guest[0], proom->guest[1], buffer);
		ERROR_NEG_PRINT(-1, "lua_error : current_side=%d  phase=%d"
		, lu_get_int(proom->lua, "g_current_side")
		, lu_get_int(proom->lua, "g_phase")  );

		lua_pop(proom->lua, 4);  // pop 4 return value
		LU_CHECK_MAGIC(proom->lua);
		// no broadcast, early exit
		return -1;
	} else {
		win = lua_tointeger(proom->lua, -4);
		// DEBUG_PRINT(0, "PLAY CMD OK [%s]  win=%d", buffer, win);
		ret = 0;

		int cmd_size = (int)(proom->cmd_list.size() + 1);
		sprintf(out_buffer, "%d %s", cmd_size, buffer);
		// printf("play_cmd:out_buffer=%s \n", out_buffer);
		// add the command to the cmd_list
		proom->cmd_list.push_back(out_buffer);
	}
	lua_pop(proom->lua, 4);  // pop 4 return value

	LU_CHECK_MAGIC(proom->lua);

	// TODO force-next, give a timeout after n 
	if (strcmp(cmd, "n") == 0) {
		sprintf(out_buffer, "%zu n %d", proom->cmd_list.size(), g_design->constant.max_timeout + 1);
		proom->game_timeout = time(NULL) + g_design->constant.max_timeout;
	}
	

	// (proom, except, buffer)
	ret = broadcast_room(proom, 0, out_buffer); // except=0 means sent to all

	// early exit if no winner
	if (win <= 0) {
		if (proom->num_guest >= 2 && proom->guest[1]<=MAX_AI_EID) {
			// DEBUG_PRINT(0, "play_cmd:ai_play");
			return ai_play(conn);
		}
		return ret;
	}

	return win_game(conn, proom, win, WIN_GAME_NORMAL, GAME_END_NORMAL);
}

#endif

////////// ROOM END //////////



////////// GAME START //////////
// game related logic,  load card, load deck etc.
// utility:
// card_list_array() : list to array,   card_array_list() : array to list

// input: card_list, card_total
// output:  array, caller must alloc memory, e.g.: char array[EVIL_CARD_MAX+1]
int card_list_array(char *array, const int *card_list, int card_total) {
	if (card_total < 0) {
		ERROR_NEG_PRINT(card_total, "card_list_array -ve card_total");
		return ERR_OUTBOUND;
	}

	bzero(array, EVIL_CARD_MAX);  // zero-base
	for (int i=0; i<card_total; i++) {
		int id = card_list[i];
		if (id <= 0) {
			continue;
		}
		if (id > 400) {  // hard code, now support 400 cards only
			BUG_PRINT(-22, "card_list_array id overflow %d", id);
			continue;
		}
		char *ptr = array + id - 1;  // array[0] means card1
		if (*ptr >= 9) {
			ERROR_NEG_RETURN(-12, "card_list_array array[%d]>=%d",
				id, *ptr);
			return -12; // safety only, shall never run
		}
		(*ptr) ++;  
	}
	
	return 0;
}


// card list to digit_400 form
int card_list_digit(char *array, const int *card_list, int card_total) {
	if (card_total < 0) {
		ERROR_NEG_PRINT(card_total, "card_list_array -ve card_total");
		return ERR_OUTBOUND;
	}

	for (int i=0; i<EVIL_CARD_MAX; i++) {
		array[i] = '0';
	}
	array[EVIL_CARD_MAX] = '\0'; // null-term it

	for (int i=0; i<card_total; i++) {
		int id = card_list[i];
		if (id <= 0) {
			continue;
		}
		if (id > 400) {  // hard code, now support 400 cards only
			BUG_PRINT(-22, "card_list_array id overflow %d", id);
			continue;
		}
		char *ptr = array + id - 1;  // array[0] means card1
		if (*ptr >= '9') {
			ERROR_NEG_RETURN(-12, "card_list_array array[%d]>=%d",
				id, *ptr);
			return -12; // safety only, shall never run
		}
		(*ptr) ++;  
	}
	return 0;
}

// input: char *array,   card_total is the size alloc to card_list[]
// output: card_list (size bound by card_total)
// note:  if the card_list overflow, it return -12 and 
// a partially filled card_list[] is there.
// recommend usage:
// char array[EVIL_CARD_MAX+1]; // beware out of bound
// // here fill up array or load from database etc.
// int total = count_card_array(array);
// if total <= 0  error handling, early exit
// int *card_list;
// card_list = NEW(int, total);
// ret = card_array_list(card_list, total, array); // array is the input
// if (ret < 0) error handling, early exit
// total = ret;
// // use card_list
// free(card_list);
// 
int card_array_list(int *card_list, int card_total, char *array)
{
	if (card_total <= 0) {
		ERROR_NEG_RETURN(-2, "card_array_list card_total %d", card_total);
		return -2; // safety never run!!!
	}
	// skip array[0]
	int index = 0;  // always: index < card_total, BOUND: index=card_total-1
	for (int i=1; i<=EVIL_CARD_MAX; i++) {
		int count = array[i];
		for (int c=1; c<=count; c++) {
			if (index >= card_total) {
				ERROR_NEG_RETURN(-12, "card_array_list overflow card_total %d  index %d", card_total, index);
				return -12; // safety never run
			}
			card_list[index] = i;
			index ++;
		}
	}

	// do not split line for error print
	ERROR_PRINT(card_total-index, "card_array_list not match %d %d", card_total, index);
	return index;
}


int count_card_array(char *array, int max_digit)
{
	int count = 0;
	// no need to start from 0
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		// assert array[i] >= 0
		// TODO check 0 to 9
		if (array[i] < 0) {
			ERROR_PRINT(-2, "count_card_array negative");
			continue;
		}
		if (array[i] > max_digit) {
			ERROR_PRINT(-12, "count_card_array too large %d > %d"
			, array[i], max_digit);
			continue;
		}
		count += array[i];
	}
	return count;
}


/**
 * @see db_conn.c : case 11
 * 
 * @return 0 for normal
 * @return -2 for str contains less than total cards
 * @return -12 for any card out of range (1 to EVIL_CARD_MAX)
 * @return -22 too many cards?
 */
int parse_card_list(int *card_list, int total, const char *str)
{
	assert(card_list != NULL);
	int ret;
	int n;
	int id;
	const char *ptr;

	ptr = str;
	for (int i=0; i<total; i++) {
		ret = sscanf(ptr, "%d %n", &id, &n);
		if (ret != 1) {
			ERROR_PRINT(ret, "parse_card_list less total=%d i=%d str=[%s]", 
				total, i, str);
			return -2;
		}
		if (id < 1 || id > EVIL_CARD_MAX) {
			ERROR_PRINT(ret, "parse_card_list out bound id=%d", id);
			return -12;
		}
		card_list[i] = id;
		ptr += n;
	}
	ret = sscanf(ptr, "%d", &id);
	if (ret >= 1) {
		ERROR_PRINT(ret, "parse_card_list more total=%d str=[%s]", total, str);
		return -22;
	}

	return 0;
}


////////// GAME END //////////


////////// ARENA START //////////

// CMD: @arena
// RET: @arena [row_num]
int admin_init_arena(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;

	ret = dbin_write(conn, cmd, DB_INIT_ARENA, "");
	return ret;
}

////////// ARENA START //////////

////////// RANKING START //////////

// CMD: @ranking
// RET: @ranking [row_num]
int admin_init_ranking(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;

	ret = dbin_write(conn, cmd, DB_INIT_RANKING, "");
	return ret;
}

// CMD: @reset_ranktime
int admin_reset_ranking_time(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;

	ret = dbin_write(conn, cmd, DB_RESET_RANKING_TIME, "");
	return ret;
}

// CMD: rlist
// RET: rlist [row_num] [info1] [info2] ...
// info = eid, rank, icon, alias
int ranking_list(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	if (eid <= 0) {
		net_writeln(conn, "%s -9 not_login", cmd);
		return 0;
	}

	ret = dbin_write(conn, cmd, DB_RANKING_LIST
	, IN_RANKING_LIST_PRINT, eid);
	return ret;
}

// CMD: rtarlist
// RET: rtarlist [rank] [row_num] [info1] [info2] ...
// info = eid, rank, icon, alias
int ranking_targets(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;

	int eid = get_eid(conn);
	if (eid <= 0) {
		net_writeln(conn, "%s -9 not_login", cmd);
		return 0;
	}

	ret = dbin_write(conn, cmd, DB_RANKING_TARGETS
	, IN_RANKING_TARGETS_PRINT, eid);
	return ret;
}


int __check_in_rankpair_list(int eid, bool be_challenged)
{
	RANKING_PAIR_LIST::iterator it;
	for (it = g_rankpair_list.begin(); it < g_rankpair_list.end(); it++) {
		ranking_pair_t &rt = *it;
		if (rt.status != RANKING_CHALLENGE_STATUS_WAITING) {
			continue;
		}
		if (rt.eid_challenger == eid) { // eid is challenger
			return rt.eid_receiver;
		}
		if (be_challenged && (rt.eid_receiver == eid)) { // eid is being challenged
			return rt.eid_challenger;
		}
	}
	return -1;
}

ranking_pair_t* __get_rankpair_in_list(int eid, bool be_challenged)
{
	RANKING_PAIR_LIST::iterator it;
	for (it = g_rankpair_list.begin(); it < g_rankpair_list.end(); it++) {
		ranking_pair_t &rt = *it;
		if (rt.eid_challenger == eid) { // eid is challenger
			return &rt;
		}
		if (be_challenged && (rt.eid_receiver == eid)) { // eid is being challenged
			return &rt;
		}
	}
	return NULL;
}

int __add_to_rankpair_list(int eid, int target_eid)
{
	ranking_pair_t rank_pair;
	rank_pair.eid_challenger	= eid;
	rank_pair.eid_receiver		= target_eid;
	rank_pair.challenge_time	= time(NULL);
	rank_pair.status			= RANKING_CHALLENGE_STATUS_WAITING;
	g_rankpair_list.push_back(rank_pair);
	return 0;
}

int __remove_from_rankpair_list(int eid, bool be_challenged)
{
	int ret = -1;
	RANKING_PAIR_LIST::iterator it;
	for (it = g_rankpair_list.begin(); it < g_rankpair_list.end(); it++) {
		ranking_pair_t &rt = *it;
		if (rt.eid_challenger == eid) { // eid is challenger
			g_rankpair_list.erase(it);
			ret = 0;
			break;
		}
		if (be_challenged && (rt.eid_receiver == eid)) { // eid is being challenged
			g_rankpair_list.erase(it);
			ret = 0;
			break;
		}
	}
	return ret;
}

// ret = 1: player is playing ranking game or ai is playing ranking game
int __is_in_ranking_game(int eid)
{
	room_t *proom;
	proom = g_user_room[eid];

	// player in ranking game
	if (proom != NULL && proom->game_type == GAME_RANK) {
		return 1;
	}

	// player' ai in ranking game
	proom = g_user_room[-eid];
	if (proom != NULL && proom->game_type == GAME_RANK) {
		return 1;
	}

	return 0;
}

int __is_in_rankpair_list(int eid)
{
	RANKING_PAIR_LIST::iterator it;
	for (it = g_rankpair_list.begin(); it < g_rankpair_list.end(); it++) {
		ranking_pair_t &rt = *it;
		if (rt.status != RANKING_CHALLENGE_STATUS_WAITING) {
			continue;
		}
		if (rt.eid_challenger == eid || rt.eid_receiver == eid) {
			return 1;
		}
	}
	return 0;
}

int __check_rank_game_valid(int eid, bool be_challenged = true)
{
	room_t *proom, *ai_proom;
	int oppo_eid;

	ERROR_NEG_RETURN(eid, "__check_rank_game_valid:eid_invalid %d", eid);
	proom = g_user_room[eid];
	ai_proom = g_user_room[-eid];

	// eid has a room played by AI
	if (be_challenged && (NULL != ai_proom)
	&& ai_proom->game_type == GAME_RANK) {
		return -6;
	}

	// eid has a room played by self
	if (proom != NULL && proom->game_type == GAME_RANK) {
		return -16;
	}

	if (proom != NULL) {
		return ST_ROOM;
	}

	oppo_eid = __check_in_rankpair_list(eid, be_challenged);
	if (oppo_eid > 0) {
		return -26;
	}

	connect_t *conn;
	conn = get_conn_by_eid(eid);
	if (NULL == conn) {
		return ST_NULL;
	}

	return ST_LOGIN;
}

// CMD: rgame [target_eid] [target_rank]
// RET: room... 
int dbin_ranking_game(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int target_eid;
	int target_rank;
	int is_in_ranking_game;
	int is_in_rankpair_list;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_ranking_gmae:not_login");
	}

	ret = sscanf(buffer, "%d %d", &target_eid, &target_rank);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "dbin_ranking_game:invalid_input %d", ret);
	}

	if (target_eid <= 0 || target_rank <= 0) {
		NET_ERROR_RETURN(conn, -15
		, "dbin_ranking_game:invalid_input target_eid[%d] target_rank[%d]"
		, target_eid, target_rank);
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "dbin_ranking_game:alrealy_in_room");
	}

	// is_in_ranking_game = __is_in_ranking_game(eid);
	is_in_rankpair_list = __is_in_rankpair_list(eid);
	if (is_in_rankpair_list) {
		NET_ERROR_RETURN(conn, -16, "out_ranking_game:already_in_rankpair_list");
	}

	is_in_ranking_game = __is_in_ranking_game(target_eid);
	is_in_rankpair_list = __is_in_rankpair_list(target_eid);
	if (is_in_ranking_game || is_in_rankpair_list) {
		NET_ERROR_RETURN(conn, -16, "dbin_ranking_game:target_in_rank_game %d", target_eid);
	}

	/*
	// olg logic
	// TODO if player self is being challenged
	// should the player could challenge other player???
	ret = __check_rank_game_valid(eid, false);
	if (ret != ST_LOGIN) {
		NET_ERROR_RETURN(conn, -17, "dbin_ranking_game:eid_invalid %d", eid);
	}

	ret = __check_rank_game_valid(target_eid);
//	DEBUG_PRINT(1, "ranking_game: eid[%d] target_eid[%d] target_rank[%d] ret[%d]"
//	, eid, target_eid, target_rank, ret);
	if (ret < 0) {
		NET_ERROR_RETURN(conn, -6, "dbin_ranking_game:target_in_rank_game %d"
		, target_eid);
	}
	*/

	ret = dbin_write(conn, cmd, DB_CHECK_RANKING_TARGET
	, IN_CHECK_RANKING_TARGET_PRINT, eid
	, target_eid, target_rank);
	return ret;
}


// CMD: rresp [eid] [resp]	-- this cmd sent by player be challenged
// RET: if accept challenge (resp = 1)
// 		room ...	-- normally start a battle
// 	 else
// 	 	rresp 0 (resp = 0)
// 	 @Notice it will leave 10 second to accept to accept challenge, if overtime, challenger will auto enter a battle with ai, player be challenged need not send this cmd
int dbin_resp_ranking_game(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int challenger_eid;
	int resp;
	ranking_pair_t* rankpair;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_ranking_gmae:not_login");
	}

	ret = sscanf(buffer, "%d %d", &challenger_eid, &resp);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "dbin_resp_ranking_game:invalid_input %d", ret);
	}

	if (resp != RANKING_CHALLENGE_STATUS_REFUSE
	&& resp != RANKING_CHALLENGE_STATUS_ACCEPT) {
		NET_ERROR_RETURN(conn, -15, "dbin_resp_ranking_game:invalid_response %d", resp);
	}

	if (challenger_eid <= 0) {
		NET_ERROR_RETURN(conn, -25, "dbin_resp_ranking_game:invalid_eid %d", challenger_eid);
	}

	rankpair = __get_rankpair_in_list(eid, true);
	if (rankpair == NULL) {
		NET_ERROR_RETURN(conn, -6, "dbin_resp_ranking_game:rankpair_null %d", eid);
	}

	if (rankpair->eid_challenger != challenger_eid) {
		NET_ERROR_RETURN(conn, -16, "dbin_resp_ranking_game:challenger_eid_mismatch %d %d", rankpair->eid_challenger, challenger_eid);
	}

	if (rankpair->eid_receiver != eid) {
		NET_ERROR_RETURN(conn, -26, "dbin_resp_ranking_game:receiver_eid_mismatch %d %d", rankpair->eid_receiver, eid);
	}

	if (rankpair->status != RANKING_CHALLENGE_STATUS_WAITING) {
		NET_ERROR_RETURN(conn, -36, "dbin_resp_ranking_game:challenge_request_expired");
	}


	rankpair->status = resp;
	/*
	if (rankpair->challenge_time == -1) {
		NET_ERROR_RETURN(conn, -36, "dbin_resp_ranking_game:challenge_request_expired");
	}
	*/

	ret = dbin_write(conn, cmd, DB_RANKING_CHALLENGE
	, IN_RANKING_CHALLENGE_PRINT, eid, challenger_eid, resp);

	return 0;
}

// CMD: rcancel [target_eid]	-- this cmd sent by the challenger
// RET: rcancel 0
int cancel_ranking_game(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int target_eid;
	ranking_pair_t *rankpair;
	connect_t *conn2;

	ret = sscanf(buffer, "%d", &target_eid);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "cancel_ranking_game:invalid_input %d", ret);
	}
	if (target_eid <= 0) {
		NET_ERROR_RETURN(conn, -15
		, "cancel_ranking_game:invalid_input target_eid[%d]"
		, target_eid);
	}

	eid = get_eid(conn);
	if (eid <= 0) {
		net_writeln(conn, "%s -9 not_login", cmd);
		return 0;
	}

	rankpair = __get_rankpair_in_list(eid, false);
	if (rankpair == NULL)
	{
		NET_ERROR_RETURN(conn, -6
		, "cancel_ranking_game:not_in_challenge_list eid[%d]"
		, eid);
	}
	if (rankpair->challenge_time == -1) {
		NET_ERROR_RETURN(conn, -16, "cancel_ranking_game:entering_ranking_game");
	}
	if (rankpair->status != RANKING_CHALLENGE_STATUS_WAITING)
	{
		NET_ERROR_RETURN(conn, -26
		, "cancel_ranking_game:challenge_has_started eid[%d]"
		, eid);
	}

	WARN_PRINT(target_eid != rankpair->eid_receiver
	, "cancel_ranking_game:target_eid_not_match target_eid[%d]!=oppo_eid[%d]"
	, target_eid, rankpair->eid_receiver);

	ret = __remove_from_rankpair_list(eid, false);

	ret = net_writeln(conn, "%s 0", cmd);

	conn2 = get_conn_by_eid(target_eid);
	if (conn2 == NULL) {
		WARN_PRINT(-36, "cancel_ranking_game:target_eid_off_line");
		return 0;
	}
	net_writeln(conn2, "%s %d", cmd, eid);

	return ret;
}

// CMD: ranklog
// RET: ranklog [row_num] [info1] [info2] ...
// info = [eid1] [eid2] [rank1] [rank2] [icon1] [icon2] [alias1] [alias2] [success] [time]
int get_ranking_history(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		net_writeln(conn, "%s -9 not_login", cmd);
		return 0;
	}
	ret = dbin_write(conn, cmd, DB_GET_RANKING_HISTORY, IN_RANKING_HISTORY_PRINT, eid);
	return ret;
}

int	auto_ranking_challenge_resp(time_t now)
{
	static time_t last_ranking_resp = 0;
	// note: must be a bit smaller than WATCH_TIMEOUT 
	if (now - last_ranking_resp <= 2) {
		return 0;
	}
	// checking every 2 secs
	last_ranking_resp = now;

	RANKING_PAIR_LIST::iterator it;
	for (it = g_rankpair_list.begin(); it < g_rankpair_list.end(); it++) {
		ranking_pair_t &rt = *it;
		if (rt.status != RANKING_CHALLENGE_STATUS_WAITING) {
			continue;
		}
		/*
		if (rt.challenge_time == -1) {
			continue;
		}
		*/
		if (now - rt.challenge_time < RANKING_CHALLENGE_TIMEOUT) {
			continue;
		}

		// rt.challenge_time = -1;
		rt.status = RANKING_CHALLENGE_STATUS_REFUSE;
		dbin_write(NULL, "rresp", DB_RANKING_CHALLENGE
		, IN_RANKING_CHALLENGE_PRINT, rt.eid_receiver, rt.eid_challenger, 0);
	}
	
	return 0;
}

////////// RANKING END //////////



////////// GIFT START //////////





// CMD: gift [key_code]
// RET: gift [gold] [crystal] [card_count] [card_id] [card_id] ...
int exchange_gift(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	char key_code[35];

	eid = get_eid(conn);
	if (eid <= 0) {
		net_writeln(conn, "%s -9 not_login", cmd);
		return 0;
	}
	ret = sscanf(buffer, "%30s", key_code);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "exchange_gift:invalid_input %d", ret);
	}

	ret = dbin_write(conn, cmd, DB_EXCHANGE_GIFT, IN_EXCHANGE_GIFT_PRINT
	, eid, key_code);
	return ret;
}



////////// GIFT END //////////








////////// FIGHT START //////////

int __check_in_fight_schedule(int game_type, time_t cur_time)
{
	time_t rtime = cur_time - get_day_start(cur_time);
	design_fight_schedule_t *schedule;
	for (int i = 0; i < g_design->max_fight_schedule; i++)
	{
		schedule = g_design->fight_schedule_list + i;
		if (schedule->game_type != game_type)
		{
			continue;
		}

		if ((schedule->open_time == schedule->close_time)
		|| (schedule->open_time < schedule->close_time
		&& rtime >= schedule->open_time && rtime < schedule->close_time)
		|| (schedule->open_time > schedule->close_time
		&& (rtime >= schedule->open_time || rtime < schedule->close_time)))
		{
			return FIGHT_STATUS_OPEN;
		}
	}

	return FIGHT_STATUS_CLOSE;
}

design_fight_schedule_t* __get_fight_schedule(int game_type, time_t cur_time)
{
	time_t rtime = cur_time - get_day_start(cur_time);
	design_fight_schedule_t *schedule;
	design_fight_schedule_t *tschedule = NULL;
	for (int i = 0; i < g_design->max_fight_schedule; i++)
	{
		schedule = g_design->fight_schedule_list + i;
		if (schedule->game_type != game_type)
		{
			continue;
		}
		if ((tschedule == NULL)
		|| (schedule->open_time == schedule->close_time)) {
			tschedule = schedule;
			continue;
		}

		if ((rtime >= schedule->open_time) && (rtime < schedule->close_time))
		{
			tschedule = schedule;
			break;
		}
	}

	return tschedule;
	
}

// * CMD: fdata
// * RET: fdata [fight_ai_time] [fight_ai_win_gold] [fight_ai_cost_gold] [fight_gold_time] [fight_win_gold] [fight_cost_gold] [fight_crystal_time] [fight_win_crystal] [fight_cost_crystal] [fight_vs_free_win_gold] [fight_vs_free_lose_gold] [fight_ai_free_win_gold] [fight_ai_free_lose_gold] [fight_ai_status] [fight_gold_status] [fight_crystal_status] [fight_ai_free_status] [fight_free_status] [fight_gold_start_time] [fight_gold_end_time] [fight_crystal_start_time] [fight_crystal_end_time]
// fight_xxx_status means whether that kind of fight is open
int get_fight_data(connect_t *conn, const char *cmd, const char *buffer)
{
//	int ret;
	int eid;
	time_t now, gmt_time, local_diff;
	int fight_ai_status;
	int fight_gold_status, fight_crystal_status;
	int fight_ai_free_status, fight_free_status;
	design_fight_schedule_t *fight_gold_schedule = NULL;
	design_fight_schedule_t *fight_crystal_schedule = NULL;


	eid = get_eid(conn);
	if (eid <= 0) {
		net_writeln(conn, "%s -9 not_login", cmd);
		return 0;
	}

	now = time(NULL);
	gmt_time = mktime(gmtime(&now));
	local_diff = now - gmt_time;
	fight_ai_status			= __check_in_fight_schedule(GAME_SOLO_GOLD, now);
	fight_gold_status		= __check_in_fight_schedule(GAME_VS_GOLD, now);
	fight_crystal_status	= __check_in_fight_schedule(GAME_VS_CRYSTAL, now);
	fight_ai_free_status	= __check_in_fight_schedule(GAME_SOLO_FREE, now);
	fight_free_status		= __check_in_fight_schedule(GAME_VS_FREE, now);
	fight_gold_schedule		= __get_fight_schedule(GAME_VS_GOLD, now);
	fight_crystal_schedule	= __get_fight_schedule(GAME_VS_CRYSTAL, now);
	if (fight_gold_schedule == NULL || fight_crystal_schedule == NULL)
	{
		net_writeln(conn, "%s -7 schdule_not_set", cmd);
		return -7;
	}

	evil_user_t &euser = conn->euser;

//	ret =
	net_writeln(conn
	, "%s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, cmd, euser.fight_ai_time, g_design->constant.win_gold[GAME_SOLO_GOLD]
	, g_design->constant.cost_gold[GAME_SOLO_GOLD]
	, euser.fight_gold_time, g_design->constant.win_gold[GAME_VS_GOLD]
	, g_design->constant.cost_gold[GAME_VS_GOLD]
	, euser.fight_crystal_time, g_design->constant.win_crystal[GAME_VS_CRYSTAL]
	, g_design->constant.cost_crystal[GAME_VS_CRYSTAL]
	, g_design->constant.win_gold[GAME_VS_FREE]
	, g_design->constant.lose_gold[GAME_VS_FREE]
	, g_design->constant.win_gold[GAME_SOLO_FREE]
	, g_design->constant.lose_gold[GAME_SOLO_FREE]
	, fight_ai_status, fight_gold_status, fight_crystal_status
	, fight_ai_free_status, fight_free_status
	, fight_gold_schedule->open_time - local_diff
	, fight_gold_schedule->close_time - local_diff
	, fight_crystal_schedule->open_time - local_diff
	, fight_crystal_schedule->close_time - local_diff
	);

	return 0;
}

// * CMD: @reset_fighttimes
// * RET: @reset_fighttimes 0
int reset_fight_times(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;

	ret = dbin_write(conn, cmd, DB_RESET_FIGHT_TIMES, "");
	return ret;
}


int get_random_ai()
{
	int ai_id_list[MAX_AI_EID + 1];
	int id_len = 0;
	for (int i = 1; i <= MAX_AI_EID; i++)
	{
		if (g_design->ai_list[i].id == i) {
			ai_id_list[id_len++] = i;
		}
	}

	return ai_id_list[random() % id_len];
}


int user_signals_check(evil_user_t &user, int pos)
{
	if (pos < 0 || pos >= EVIL_SIGNAL_MAX)
	{
		// TODO add ERROR_PRINT : user.eid, pos
		return -5;
	}
//	printf("user_signals[%d]=[%c] %d\n", pos, user.signals[pos]
//	, user.signals[pos] != '\0' && user.signals[pos] != '0');
	if (user.signals[pos] != '\0' && user.signals[pos] != '0')
	{
		return 1;
	}
	return 0;
}

int fight_list_check(QUICK_LIST *fight_list, int eid)
{
	QUICK_LIST::iterator it;
	
	for (it=fight_list->begin(); it<fight_list->end(); it++) {
		quick_t &qq = *it;
		if (qq.eid == eid) {
			return 1;
		}
	}
	return 0;
}

int fight_vs_add(evil_user_t &euser, QUICK_LIST *fight_list, time_t now)
{
	if (fight_list_check(fight_list, euser.eid)) {
		return -2;
	}
	quick_t quick;
	quick.eid = euser.eid;
	quick.rating = euser.rating;
	quick.start_time = now;
	fight_list->push_back(quick);
	return 0;
}

int fight_list_match(vector< pair<int, int> > &pair_list, QUICK_LIST *fight_list, time_t now) 
{
	QUICK_LIST::iterator it1, it2;
	// TODO change to use rbegin(), rend(), delete is from the back
	for (it1=fight_list->begin(); it1 < fight_list->end(); it1++) {
		quick_t &q1 = *it1;
//		printf("fight_list:eid[%d] rating[%lf] start_time[%ld]\n"
//		, q1.eid, q1.rating, q1.start_time);
		if (q1.eid==0) { continue; }  // ??? is it ok?
		int wait1 = now - q1.start_time;
		if (wait1 < 0) {  // original assert wait1 >= 0
			ERROR_PRINT(-7, "quick_match_wait1_negative");
			continue; 
		}
		for (it2=it1+1; it2 < fight_list->end(); it2++) {
			quick_t &q2 = *it2;
			printf("\tfight_list:eid[%d] rating[%lf] start_time[%ld]\n"
			, q2.eid, q2.rating, q2.start_time);
			if (q1.eid==0) { break; } // this break to outter loop
			if (q2.eid==0) { continue; }
			if (q1.eid == q2.eid) { // this is funny
				q1.eid = 0;  q2.eid = 0; // erase them
				BUG_PRINT(-27, "quick_match_same_eid %d", q1.eid);
				continue;
			}
			// check q1, q2 already in room
			// TODO
			int wait2 = now - q2.start_time;
			if (wait2 < 0) {  // original assert
				ERROR_PRINT(-17, "quick_match_wait2_negative");
				continue; 
			}
			int dt = rating_diff_time(q2.rating - q1.rating);

			// de morgan theorem
			// was: if (wait1 >= dt || wait2 >= dt) do positive
			if ( wait1<dt && wait2<dt) {
				continue;  // rating difference too large, wait more
			}

			// ok, we find a match: q1 and q2 , put them in pair_list
			pair_list.push_back( make_pair(q1.eid, q2.eid) );
			// DEBUG_PRINT(0, "pair matched: (%d %d)", q1.eid, q2.eid);
			// set zero to replace erase
			q1.eid = 0;
			q2.eid = 0;
			break;  // break out of inner for loop, to outter loop
		}

	}

	// erase those zero eid : note do not put it1++ inside for(...)
	for (it1=fight_list->begin(); it1 < fight_list->end(); ) {
		quick_t &qq = *it1;  // ref
		if (qq.eid == 0) {
			it1 = fight_list->erase(it1);
		} else {
			it1++;
		}
	}

	return pair_list.size();
}

int fight_room(QUICK_LIST *fight_list, vector< pair<int,int> > &pair_list, int game_type, time_t now)
{
	int ret;
	int err_count = 0;
	
	// TODO use iterator may be faster?
	for (unsigned int i=0; i<pair_list.size(); i++) {
		// TODO create a room, start game!
		// @see room_create
		// @see room_game
		int eid1, eid2;
		connect_t *conn1, *conn2;
		eid1 = pair_list[i].first;
		eid2 = pair_list[i].second;
		conn1 = get_conn_by_eid(eid1);
		conn2 = get_conn_by_eid(eid2);

//		printf("fight_room: eid1[%d] eid2[%d]\n", eid1, eid2);

		// let the other component back to queue 
		if (NULL==conn1 || NULL==conn2) { // means it is offline
//			const char *errstr = "fight -26 opponent_off_line";
			if (conn1!=NULL) { 
				ret = fight_vs_add(conn1->euser, fight_list, now);
			}
			if (conn2!=NULL) {
				ret = fight_vs_add(conn2->euser, fight_list, now);
			}
			continue; // skip this pair
		}
		if (conn1->room != NULL || conn2->room != NULL) {
//			const char *errstr = "fight -36 already_in_room";
			if (conn1!=NULL) {
				ret = fight_vs_add(conn1->euser, fight_list, now);
			}
			if (conn2!=NULL) {
				ret = fight_vs_add(conn2->euser, fight_list, now);
			}
			continue; // skip this pair
		}

		room_t *proom;
		proom = new_room(CHANNEL_QUICK);
		proom->game_type = game_type;
		proom->gameid = get_gameid();
		proom->state = ST_GAME; // make it a game!
		proom->num_guest = 2;
		proom->guest[0] = eid1; // pair_list[i].first;
		proom->guest[1] = eid2; // pair_list[i].second;
		proom->rating[0] = conn1->euser.rating;
		proom->rating[1] = conn2->euser.rating;
		strcpy(proom->alias[0], conn1->euser.alias);
		strcpy(proom->alias[1], conn2->euser.alias);
		proom->icon[0] = conn1->euser.icon;
		proom->icon[1] = conn2->euser.icon;
		proom->lv[0] = conn1->euser.lv;
		proom->lv[1] = conn2->euser.lv;
		// copy the deck to proom deck
		sprintf(proom->deck[0], "%.400s", conn1->euser.deck);
		sprintf(proom->deck[1], "%.400s", conn2->euser.deck);
		sprintf(proom->title, "%s~VS~%s", conn1->euser.alias, conn2->euser.alias);
		room_set_hero_info(proom, NULL);
		ret = game_init(proom, 0, 0); // init proom->deck
		if (ret < 0) {
			const char *errstr = "fight -18 subfun_err %d";
			// TODO how to 
			if (conn1!=NULL) { net_writeln(conn1, errstr, ret); }
			if (conn2!=NULL) { net_writeln(conn2, errstr, ret); }
			ret = free_room(proom->channel, proom);  // order is important
			err_count--;
			continue;
		}
		// note: order is important, it must be after game_init()
		conn1->room = proom;
		conn2->room = proom;
		conn1->st = proom->state;
		conn2->st = proom->state;
		// re-conn logic
		g_user_room[eid1] = proom;
		g_user_room[eid2] = proom;
		room_info_broadcast(proom, 0); // 0 means all
		INFO_PRINT(0, "room_title=%s (%d vs %d)", proom->title, proom->guest[0], proom->guest[1]);
		// game_broadcast(proom);
	}
	
	return err_count; // normally it is 0
}

int fight_with_player(connect_t* conn, const char *cmd, int eid, int game_type)
{
	int ret;

	room_t * proom = conn->room;
	if (NULL != proom) {
		NET_ERROR_RETURN(conn, -16, "quick_has_room");
	}

	QUICK_LIST *fight_list;
	time_t now = time(NULL);  // assume logic run very fast!

//	printf("fight_with_player: game_type[%d]\n", game_type);
	switch (game_type)
	{
		case GAME_VS_GOLD:
		{
			if (conn->euser.fight_gold_time <= 0)
			{
				NET_ERROR_RETURN(conn, -26, "fight_with_player:times_0");
			}
			fight_list = &g_fight_gold_list;
			break;
		}
		case GAME_VS_CRYSTAL:
		{
			if (conn->euser.fight_crystal_time <= 0)
			{
				NET_ERROR_RETURN(conn, -36, "fight_with_player:times_0");
			}
			fight_list = &g_fight_crystal_list;
			break;
		}
		case GAME_VS_FREE:
		{
			fight_list = &g_fight_free_list;
			break;
		}
		default:
		{
			NET_ERROR_RETURN(conn, -7, "fight_with_player:game_type_error %d"
			, game_type);
		}
	}

	ret = fight_vs_add(conn->euser, fight_list, now);
	WARN_PRINT(ret, "fight_add_duplicate OK");

	// normal case, give a zero
	net_writeln(conn, "%s 0", cmd);  // finish the fight command it

	// TODO quick_del when offline
	vector< pair<int, int> > pair_list;
	fight_list_match(pair_list, fight_list, now);
	if (pair_list.size() > 0) {
		fight_room(fight_list, pair_list, game_type, now);
		// TODO put it into room, game
	}


	return 0;
}


// *
// * CMD: fight [fight_type]
// * RET: if fight with ai[game_type = GAME_SOLO_GOLD/GAME_SOLO_FREE]
// * 			room ...
// *	  else fight with player[game_type = GAME_VS_GOLD/GAME_VS_CRYSTAL/GAME_VS_FREE]
// *			fight 0
// *		if match other players, will also get resp
// * 			room ...
// *
int dbin_fight(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;
	int eid;
	int game_type;

	eid = get_eid(conn);
	if (eid <= 0) {
		net_writeln(conn, "%s -9 not_login", cmd);
		return 0;
	}
	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "already_has_room");
	}

	if (fight_list_check(&g_fight_gold_list, eid)
	|| fight_list_check(&g_fight_crystal_list, eid)
	|| fight_list_check(&g_fight_free_list, eid))
	{
		NET_ERROR_RETURN(conn, -26, "fight_with_type:already_in_fight");
	}

	ret = sscanf(buffer, "%d", &game_type);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "in_fight:invalid_input %d", ret);
	}

	// GAME_SOLO_GOLD	8
	// GAME_VS_GOLD		9
	// GAME_VS_CRYSTAL	10
	// GAME_SOLO_FREE	11
	// GAME_VS_FREE		12
	if (game_type < GAME_SOLO_GOLD || game_type > GAME_VS_FREE)
	{
		NET_ERROR_RETURN(conn, -15, "in_fight:game_type_out_bound %d"
		, game_type);
	}

	if (__check_in_fight_schedule(game_type, time(NULL)) == FIGHT_STATUS_CLOSE)
	{
		NET_ERROR_RETURN(conn, -16, "in_fight:not_in_schedule_time");
	}

	// fight with robot, goto fight_robot logic
	if (game_type == GAME_SOLO_GOLD) {
		int eid2;
		if (conn->euser.fight_ai_time <= 0)
		{
			NET_ERROR_RETURN(conn, -16, "dbin_fight:ai_times_0");
		}

		int gold = g_design->constant.cost_gold[GAME_SOLO_GOLD];
		if (conn->euser.gold < gold)
		{
			NET_ERROR_RETURN(conn, -16, "dbin_fight:money_not_enough");
		}

		if (user_signals_check(conn->euser, SIGNAL_FIGHT_AI))
		{
			eid2 = get_random_robot();
		} else {
			eid2 = FIGHT_SIGNAL_AI_EID;
		}
		ret = dbin_write(conn, "fight_robot", DB_FIGHT_ROBOT, "%d %d %d", game_type, eid, eid2);
		return ret;
	}

	if (game_type == GAME_SOLO_FREE) {
		int eid2;
		if (user_signals_check(conn->euser, SIGNAL_FIGHT_AI_FREE))
		{
			eid2 = get_random_robot();
		} else {
			eid2 = FIGHT_SIGNAL_AI_EID;
		}
		ret = dbin_write(conn, "fight_robot", DB_FIGHT_ROBOT, "%d %d %d", game_type, eid, eid2);
		return ret;
	}

	// here, fight with player
	if (game_type == GAME_VS_GOLD) {
		int gold = g_design->constant.cost_gold[GAME_VS_GOLD];
		if (conn->euser.gold < gold)
		{
			NET_ERROR_RETURN(conn, -36, "dbin_fight:vs_gold_not_enough");
		}
	}

	if (game_type == GAME_VS_CRYSTAL) {
		int crystal = g_design->constant.cost_crystal[GAME_VS_CRYSTAL];
		if (conn->euser.crystal < crystal)
		{
			NET_ERROR_RETURN(conn, -46, "dbin_fight:vs_crystal_not_enough");
		}
	}

	ret = dbin_write(conn, cmd, DB_FIGHT, IN_FIGHT_LOAD_DECK_PRINT
	, eid, game_type);

	return ret;
}

int fight_list_remove(QUICK_LIST *fight_list, int eid)
{
	QUICK_LIST::iterator it1;
	for (it1=fight_list->begin(); it1 < fight_list->end(); it1++) {
		quick_t &qq = *it1;  // ref
		if (qq.eid == eid) {
			fight_list->erase(it1);
			return 1;
		}
	}
	return 0;
}

// * CMD: fcancel
// * RET: fcancel 0
int fight_cancel(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;
	int eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		ret = net_writeln(conn, "%s -9 not_login", cmd);
		return ret;
	}

	if (fight_list_remove(&g_fight_gold_list, eid)
	|| fight_list_remove(&g_fight_crystal_list, eid)
	|| fight_list_remove(&g_fight_free_list, eid))
	{
		ret = net_writeln(conn, "%s 0", cmd);
	} else {
		NET_ERROR_RETURN(conn, -6, "fight_cancel:not_in_fight_list");
	}
	
	return ret;
}

////////// FIGHT END //////////



////////// HERO MISSION START //////////

// * CMD: lhero
// * RET: lhero [count] [hero_id]...[hero_id] [ncount] [hero_id]...[hero_id]
int get_hero_data_list(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;
	int eid;
	int count;
	int ncount;
	char hero_buffer[BUFFER_SIZE + 1];
	char *ptr;
	evil_hero_data_t *hero_data;
	design_hero_t *dhero;
	int hero_id_list[HERO_MAX+1];
	int nhero_id_list[HERO_MAX+1];

	eid = get_eid(conn);
	if (eid <= 0) {
		ret = net_writeln(conn, "%s -9 not_login", cmd);
		return ret;
	}

	count = 0;
	bzero(hero_id_list, sizeof(hero_id_list));
	for (int i = 1; i <= HERO_MAX; i++) {
		hero_data = conn->euser.hero_data_list + i;
		if (hero_data->hero.hero_id == 0) {
			continue;
		}
		hero_id_list[hero_data->hero.hero_id] = hero_data->hero.hero_id;
		count++;
	}

	ncount = 0;
	bzero(nhero_id_list, sizeof(nhero_id_list));
	for (int i = 1; i <= HERO_MAX; i++) {
		dhero = g_design->hero_list + i;
		if (dhero->hero_id == 0) {
			continue;
		}
		// find hero_id in design table but not in user table
		if (hero_id_list[dhero->hero_id] != 0) {
			continue;
		}
		nhero_id_list[dhero->hero_id] = dhero->hero_id;
		ncount++;
	}

	bzero(hero_buffer, sizeof(hero_buffer));
	ptr = hero_buffer;
	ptr += sprintf(ptr, "%d", count);
	for (int i = 1; i <= HERO_MAX; i++) {
		if (hero_id_list[i] == 0) {
			continue;
		}
		ptr += sprintf(ptr, " %d", hero_id_list[i]);
	}

	ptr += sprintf(ptr, " %d", ncount);
	for (int i = 1; i <= HERO_MAX; i++) {
		if (nhero_id_list[i] == 0) {
			continue;
		}
		ptr += sprintf(ptr, " %d", nhero_id_list[i]);
	}

	ret = net_writeln(conn, "%s %s", cmd, hero_buffer);
	return ret;

//	count = 0;
//	ptr = hero_buffer;
//	bzero(hero_buffer, sizeof(hero_buffer));
//	bzero(mid_list, sizeof(mid_list));
//	for (int i = 1; i <= HERO_MAX; i++) {
//		hero_data = conn->euser.hero_data_list + i;
//		if (hero_data->hero.hero_id == 0) {
//			continue;
//		}
//		ptr += sprintf(ptr, " %d %d %d", hero_data->hero.hero_id
//		, hero_data->hero.hp, hero_data->hero.energy);
//		count++;
//	}
//	ret = net_writeln(conn, "%s %d%s", cmd, count, hero_buffer);


}


design_hero_mission_t& __get_design_hero_mission(design_mission_hero_t *mission_hero_list, int hero_id, int mission_id)
{
	static design_hero_mission_t empty_mis = {0, 0, 0, 0, 0, 0, 0, 0, "_"};
	design_mission_hero_t *hero;
	if (hero_id <= 0 || hero_id > HERO_MAX)
	{
		BUG_PRINT(-7, "__get_design_hero_mission:hero_id_out_bound %d", hero_id);
		return empty_mis;
	}
	hero = mission_hero_list + hero_id;
	if (mission_id <= 0 || mission_id > MAX_HERO_MISSION)
	{
		BUG_PRINT(-17, "__get_design_hero_mission:mission_id_out_bound %d"
		, mission_id);
		return empty_mis;
	}
	return hero->mission_list[mission_id];
}

// * CMD: hero_mlist [hero_id]
// * RET: hero_mlist [hero_id] [hero_hp] [hero_energy] [count] [mission_info]
// * mission_info: [mission_id] [mission_status] [mtype] [mission_n1] [p1] [p2] [p3] [reward_type] [reward_count] [msg]
int get_hero_mission_list(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;
	int eid;
	int hero_id;
	int count;
	char *ptr;
	char tmp_buffer[BUFFER_SIZE + 1];
	evil_hero_data_t *hero_data;
	evil_hero_mission_t *mis;

	eid = get_eid(conn);
	if (eid <= 0) {
		ret = net_writeln(conn, "%s -9 not_login", cmd);
		return ret;
	}

	ret = sscanf(buffer, "%d", &hero_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "get_hero_mission_list:invalid_input %d", ret);
	}

	hero_data = conn->euser.hero_data_list + hero_id;
	if (hero_data->hero.hero_id == 0) {
		NET_ERROR_RETURN(conn, -5, "get_hero_mission_list:do_not_have_hero %d %d"
		, eid, hero_id);
	}

	count = 0;
	ptr = tmp_buffer;
	bzero(tmp_buffer, sizeof(tmp_buffer));
	// 1. finish mission can be submited
	for (int i = 1; i <= MAX_HERO_MISSION; i++)
	{
		mis = hero_data->mission_list + i;
		if (mis->mission_id == 0) {
			continue;
		}
		if (mis->status != MISSION_STATUS_OK) {
			continue;
		}
		design_hero_mission_t &dmis
		= __get_design_hero_mission(g_design->mission_hero_list
		, hero_id, mis->mission_id);
//		DEBUG_PRINT(1,
//		"hero_mission_data: dmid[%d] mis_id[%d] %d %d %d %d %d %d %d %s"
//		, dmis.mission_id, mis->mission_id
//		, dmis.mtype, mis->n1, dmis.p1, dmis.p2, dmis.p3
//		, dmis.reward_type, dmis.reward_count, dmis.msg);
		if (dmis.mission_id == 0) {
			continue;
		}
		// * mission_info: [mission_id] [mission_status] [mtype]
		// * [mission_n1] [p1] [p2] [p3] [reward_type] [reward_count] [msg]
		ptr += sprintf(ptr, " %d %d %d %d %d %d %d %d %d %s"
		, mis->mission_id, mis->status
		, dmis.mtype, mis->n1, dmis.p1, dmis.p2, dmis.p3
		, dmis.reward_type, dmis.reward_count, dmis.msg);
		count++;
	}
	// 2. mission not finished
	for (int i = 1; i <= MAX_HERO_MISSION; i++)
	{
		mis = hero_data->mission_list + i;
		if (mis->mission_id == 0) {
			continue;
		}
		if (mis->status >= MISSION_STATUS_OK) {
			continue;
		}
		design_hero_mission_t &dmis
		= __get_design_hero_mission(g_design->mission_hero_list
		, hero_id, mis->mission_id);
		if (dmis.mission_id == 0) {
			continue;
		}
		// * mission_info: [mission_id] [mission_status] [mtype]
		// * [mission_n1] [p1] [p2] [p3] [reward_type] [reward_count] [msg]
		ptr += sprintf(ptr, " %d %d %d %d %d %d %d %d %d %s"
		, mis->mission_id, mis->status
		, dmis.mtype, mis->n1, dmis.p1, dmis.p2, dmis.p3
		, dmis.reward_type, dmis.reward_count, dmis.msg);
		count++;
	}
	// 3. mission has been submited
	for (int i = 1; i <= MAX_HERO_MISSION; i++)
	{
		mis = hero_data->mission_list + i;
		if (mis->mission_id == 0) {
			continue;
		}
		if (mis->status != MISSION_STATUS_FINISH) {
			continue;
		}
		design_hero_mission_t &dmis
		= __get_design_hero_mission(g_design->mission_hero_list
		, hero_id, mis->mission_id);
		if (dmis.mission_id == 0) {
			continue;
		}
		// * mission_info: [mission_id] [mission_status] [mtype]
		// * [mission_n1] [p1] [p2] [p3] [reward_type] [reward_count] [msg]
		ptr += sprintf(ptr, " %d %d %d %d %d %d %d %d %d %s"
		, mis->mission_id, mis->status
		, dmis.mtype, mis->n1, dmis.p1, dmis.p2, dmis.p3
		, dmis.reward_type, dmis.reward_count, dmis.msg);
		count++;
	}

	ret = net_writeln(conn, "%s %d %d %d %d%s", cmd
	, hero_id, hero_data->hero.hp, hero_data->hero.energy, count, tmp_buffer);
	return ret;
}

// * CMD: shero_mis [hero_id] [mission_id]
// * RET: shero_mis [hero_id] [mission_id] [hp] [energy]
int submit_hero_mission(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;
	int eid;
	int hero_id;
	int mission_id;

	eid = get_eid(conn);
	if (eid <= 0) {
		ret = net_writeln(conn, "%s -9 not_login", cmd);
		return ret;
	}

	ret = sscanf(buffer, "%d %d", &hero_id, &mission_id);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "submit_hero_mission:invalid_input %d", ret);
	}
	if (hero_id <= 0 || hero_id > HERO_MAX)
	{
		NET_ERROR_RETURN(conn, -15, "submit_hero_mission:hero_id_out_bound %d %d"
		, eid, hero_id);
	}
	if (mission_id <= 0 || mission_id > MAX_HERO_MISSION)
	{
		NET_ERROR_RETURN(conn, -25
		, "submit_hero_mission:mission_id_out_bound %d %d %d"
		, eid, hero_id, mission_id);
	}

	evil_hero_data_t &hero_data = conn->euser.hero_data_list[hero_id];
	if (hero_data.hero.hero_id == 0) {
		NET_ERROR_RETURN(conn, -6
		, "submit_hero_mission:do_not_have_hero %d %d %d"
		, eid, hero_id, mission_id);
	}
	evil_hero_mission_t &mission =  hero_data.mission_list[mission_id];
	if (mission.mission_id == 0) {
		NET_ERROR_RETURN(conn, -16
		, "submit_hero_mission:mission_not_exists %d %d %d"
		, eid, hero_id, mission_id);
	}
	if (mission.status == MISSION_STATUS_FINISH) {
		NET_ERROR_RETURN(conn, -26
		, "submit_hero_mission:mission_has_been_submited %d %d %d %d"
		, eid, hero_id, mission_id, mission.status);
	}

	design_hero_mission_t &mis = __get_design_hero_mission(g_design->mission_hero_list, hero_id, mission_id);
	if (mis.mission_id == 0 || mis.mission_id != mission_id)
	{
		NET_ERROR_RETURN(conn, -7
		, "submit_hero_mission:not_such_mission %d %d %d"
		, eid, hero_id, mission_id);
	}
	switch (mis.mtype) {
		case HERO_MISSION_TYPE_CHAPTER:
		{
			if (mission.status != MISSION_STATUS_OK && mission.n1 < mis.p2) {
				NET_ERROR_RETURN(conn, -36
				, "submit_hero_mission:mission_not_finished %d %d %d %d"
				, eid, hero_id, mission_id, mission.status);
			}
			break;
		}
		default:
		{
			BUG_RETURN(-17, "submit_hero_mission:unkown_mission_mtype %d"
			, mis.mtype);
			break;
		}
	}

//	char hero_buffer[BUFFER_SIZE+1];
//	bzero(hero_buffer, sizeof(hero_buffer));
//	hero_ptr = hero_buffer;
//	for (int i = 0; i <HERO_MAX; i++) {
//		design_hero_t *hero = g_design->hero_list + i + 1;
//		hero_ptr += sprintf(hero_ptr, " %d %d", hero->hp, hero->energy);
//	}

	ret = dbin_write(conn, cmd, DB_SUBMIT_HERO_MISSION, "%d %d %d %d %d"
	, eid, hero_id, mission_id, mis.reward_type, mis.reward_count);
	return ret;
}


////////// HERO MISSION END //////////


////////// DAILY LOGIN START //////////


design_daily_reward_t &__get_design_daily_reward(int day, int reward_times)
{
	static design_daily_reward_t empty_reward = {0, 0, 0, 0, {}, 0, {}};
	bzero(&empty_reward, sizeof(empty_reward));
	if (day <= 0) {
		ERROR_PRINT(-3, "get_design_daily_reward:day_out_bound %d", day);
		return empty_reward;
	}
	if (reward_times < 0) {
		ERROR_PRINT(-13, "get_design_daily_reward:reward_times_out_bound %d"
		, reward_times);
		return empty_reward;
	}

	design_daily_login_t &daily_login = g_design->daily_login_list[day];
	if (reward_times > 0 && daily_login.daily_reward[2].log_time > 0) {
		return daily_login.daily_reward[2];
	}
	return daily_login.daily_reward[1];
}

int __get_daily_login_output_data(char *tmp_buffer, evil_daily_login_t &daily_login, int has_get_reward)
{
	char *out_ptr;
	out_ptr = tmp_buffer;
	out_ptr += sprintf(out_ptr, "%d %d", daily_login.log_day
	, has_get_reward);
	// daily_login.log_day means continus get reward days
	for (int i = 1; i <= MAX_DAILY_LOGIN; i++) {
		// get every day could get reward
		// if that day has get reward, reward_list should show pre reward data.
		int reward_times = daily_login.reward_day[i];
		if (i <= daily_login.log_day) {
			reward_times --;
		}

		design_daily_reward_t &reward = __get_design_daily_reward(i
		, reward_times);
		if (reward.log_time <= 0) {
			ERROR_PRINT(-7, "out_get_daily_login:daily_reward_null %d %d"
			, i, daily_login.reward_day[i]);
		}
		out_ptr += sprintf(out_ptr, " %d %d %d %d", i, reward.gold
		, reward.crystal, reward.card_count);
		for (int cdx = 0; cdx < reward.card_count; cdx++) {
			out_ptr += sprintf(out_ptr, " %d", reward.cards[cdx]);
		}
		out_ptr += sprintf(out_ptr, " %d", reward.piece_count);
		for (int pdx = 0; pdx < reward.piece_count; pdx++) {
			out_ptr += sprintf(out_ptr, " %d %d", reward.pieces[pdx][0]
			, reward.pieces[pdx][1]);
		}
	}
	return 0;
}

// CMD: daily_log
// RET: daily_log [continus_log_day] [has_get_reward] [day1_reward_info] ... [day7_reward_info]
// day(x)_reward_info: [day] [gold] [crystal] [card_list] [piece_list]
// card_list: [card_count] [card_id1] [card_id2] ...
// piece_list: [list_count] [piece_info1] [piece_info2] ...
// piece_info: [piece_id] [piece_count]
int get_daily_login(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;
	int eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		ret = net_writeln(conn, "%s -9 not_login", cmd);
		return ret;
	}

	ret = dbin_write(conn, cmd, DB_GET_DAILY_LOGIN, "%d", eid);
	return ret;
}

// CMD: daily_reward [day]
// RET: daily_reward [day] [gold] [crystal] [card_list] [piece_list]
// card_list: [card_count] [card_id1] [card_id2] ...
// piece_list: [list_count] [piece_info1] [piece_info2] ...
// piece_info: [piece_id] [piece_count]
int get_daily_reward(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int log_day;
	int limit_day;
	char *tmp_ptr;
	char tmp_buffer[1000];

	eid = get_eid(conn);
	if (eid <= 0) {
		ret = net_writeln(conn, "%s -9 not_login", cmd);
		return ret;
	}

	ret = sscanf(buffer, "%d", &log_day);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "get_daily_reward:invalid_input %d", ret);
	}

	evil_daily_login_t &daily_login = conn->euser.daily_login;
	if (daily_login.load_from_db == 0) {
		NET_ERROR_RETURN(conn, -7, "get_daily_reward:load_from_db");
	}
	DEBUG_PRINT(1, "get_daily_reward: %d %d", log_day
	, daily_login.log_day);
	if (log_day <= 0) {
		NET_ERROR_RETURN(conn, -15, "get_daily_reward:log_day_error %d/%d"
		, log_day, daily_login.log_day);
	}

	limit_day = (log_day > MAX_DAILY_LOGIN) ? MAX_DAILY_LOGIN : log_day;
	design_daily_reward_t &reward = __get_design_daily_reward(limit_day
	, daily_login.reward_day[limit_day]);
	if (reward.log_time <= 0) {
		BUG_PRINT(-7, "design_daily_reward_data_empty log_day[%d]", log_day);
		NET_ERROR_RETURN(conn, -15, "get_daily_reward:log_day_error %d/%d"
		, log_day, daily_login.log_day);
	}
	tmp_ptr = tmp_buffer;
	tmp_ptr += sprintf(tmp_ptr, "%d", reward.card_count);
	for (int i = 0; i < reward.card_count; i++) {
		tmp_ptr += sprintf(tmp_ptr, " %d", reward.cards[i]);
	}
	tmp_ptr += sprintf(tmp_ptr, " %d", reward.piece_count);
	for (int i = 0; i < reward.piece_count; i++) {
		tmp_ptr += sprintf(tmp_ptr, " %d %d", reward.pieces[i][0]
		, reward.pieces[i][1]);
	}

	dbin_write(conn, cmd, DB_GET_DAILY_REWARD, "%d %d %d %d %s", eid
	, log_day, reward.gold, reward.crystal, tmp_buffer);
	return 0;
}


////////// DAILY LOGIN END //////////

////////// ARENA START //////////

// CMD: arenatop
// RET: arenatop [size] [arena_info1] [arena_info2] ...
// arena_info = [rank] [eid] [icon] [lv] [win_rate(double)] [alias]
int dbin_arena_top_list(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;

	int eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_arena_top_list:not_login");
	}

	ret = dbin_write(conn, cmd, DB_ARENA_TOP_LIST, "%d", eid);
	return ret;
}

// CMD: arenatarget
// RET: arenatarget [my_rank] [reward_gold] [reward_crystal] 
//					[buy_times_count] [buy_times_crystal]
//					[has_reward] [reward_time_offset] [arena_times] 
//					[size] [arena_info1] [arena_info2] ...
// arena_info = [rank] [eid] [icon] [lv] [win_rate(double)] [alias]
int dbin_arena_target(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;

	int eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_arena_target:not_login");
	}

	ret = dbin_write(conn, cmd, DB_ARENA_TARGET, "%d", eid);
	return ret;
}

// CMD: arenatimes 
// RET: arenatimes [times] [gold] [crystal]
int dbin_arena_times(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;
	int eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_arena_times:not_login");
	}

	if (conn->euser.crystal < ARENA_TIMES_BUY_CRYSTAL) {
		NET_ERROR_RETURN(conn, -6, "dbin_arena_times:money_not_enough");
	}

	dbin_write(conn, "arenatimes", DB_UPDATE_ARENA_TIMES, "%d %d %d %d", eid, ARENA_TIMES_BUY_COUNT, 0, -ARENA_TIMES_BUY_CRYSTAL);
	return ret;
}

// CMD: arenagame [eid_target]
// RET: room ...
int dbin_arena_game(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;

	int eid_target;
	int eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_arena_game:not_login");
	}

	ret = sscanf(buffer, "%d", &eid_target);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_arena_game:invalid_input %d", ret);
	}

	ret = dbin_write(conn, cmd, DB_ARENA_GAME, "%d %d", eid, eid_target);
	return ret;
}

// CMD: arenareward
// RET: arenareward [rank] [gold] [crystal]
int dbin_get_arena_reward(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;
	char tmp_buffer[BUFFER_SIZE+1];
	char *tmp_ptr;

	int eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_get_arena_reward:not_login");
	}

	tmp_ptr = tmp_buffer;
	tmp_ptr += sprintf(tmp_ptr, "%d %d", eid, g_design->max_arena_reward_level);
	for (int i = 1; i <= g_design->max_arena_reward_level; i++)
	{
		design_arena_reward_t &reward = g_design->arena_reward_list[i];
		tmp_ptr += sprintf(tmp_ptr, " %d %d %d %d", reward.start
		, reward.end, reward.gold, reward.crystal);
	}

	ret = dbin_write(conn, cmd, DB_GET_ARENA_REWARD, "%s", tmp_buffer);
	return ret;
}

// CMD: @reset_arenatimes
int admin_reset_arena_times(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;

	ret = dbin_write(conn, cmd, DB_RESET_ARENA_TIMES, "");
	return ret;
}

////////// ARENA END //////////

// CMD: moneyexchange [crystal]
// RET: moneyexchange [gold_offset] [crystal_offset]
int dbin_money_exchange(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = 0;

	int crystal;
	int eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_money_exchange:not_login");
	}

	ret = sscanf(buffer, "%d", &crystal);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_money_exchange:invalid_input %d", ret);
	}

	if (crystal <= 0 || crystal > 100000) {
		NET_ERROR_RETURN(conn, -5, "dbin_money_exchange:invalid_input %d", ret);
	}

	if (conn->euser.crystal < crystal) {
		NET_ERROR_RETURN(conn, -6, "dbin_arena_times:crystal_not_enough");
	}
	
	int gold = crystal * g_design->constant.exchange_crystal_gold;

	if (gold + conn->euser.gold > INT32_MAX) {
		NET_ERROR_RETURN(conn, -16, "dbin_arena_times:gold_out_bound");
	}

	ret = dbin_write(conn, cmd, DB_MONEY_EXCHANGE, "%d %d %d", eid, gold, -crystal);
	return ret;
}


////////// SYSTEM COMMAND START //////////

int test0(int argc, char *argv[]) {
	int channel = 0;
	if (argc > 1) {
		channel = atoi(argv[1]);
	}
	printf("--- test0 print_room_list(%d)\n", channel);

	print_room_list(channel);

	return 0;
}

// test1 list conn with username IP:port
int test1(int argc, char *argv[]) {
	connect_t * conn;
	char ip[50];
	printf("--- test1 list conn\n");

	for (int i=0; i<MAX_CONNECT; i++) {
		conn = get_conn(i);
		if (conn==NULL || conn->state==STATE_FREE) continue;
		address_readable(ip, &conn->address);
		printf("cid=%d ip=%s username=%s alias=%s\n", i, ip, conn->euser.username
		, conn->euser.alias);
	}

	return 0;
}

// test2:free_room
// test 2 [room_id] [channel_id]
int test2(int argc, char *argv[]) 
{
	int channel = 0;
	int index = 0;
	int ret;
	room_t * proom;
	if (argc > 1) { index = atoi(argv[1]); }
	if (argc > 2) { channel = atoi(argv[2]); }
	proom = &(g_room_list[channel][index]);
	printf("test2:free_room  index=%d  channel=%d\n", index, channel);
	printf("proom = %p\n", proom);

	ret = free_room(proom->channel, proom);
	printf("RET %d\n", ret);

	return 0;
}

// room_leave:print room num_guest, guest[] array
int test3(int argc, char *argv[]) 
{
	int rid = 0;
	int channel = 0; // hard code
	if (argc >= 2) {
		rid = atoi(argv[1]);
	}
	room_t * proom = g_room_list[channel] + rid;
	printf("test3 rid=%d   num_guest=%d:\n", rid, proom->num_guest);
	assert(proom->rid == rid);

	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i];
		connect_t * conn = get_conn_by_eid(eid);
		evil_user_t euser;  // NOTE: this is not a pointer
		bzero(&euser, sizeof(evil_user_t));
		if (conn==NULL) {
			strcpy(euser.username, "_null_conn_");
			euser.eid = eid;
		} else {
			euser = conn->euser; // memory copy, slow
		}
		printf("guest[%d]  eid=%d   username=%s  alias=%s\n", i
		, eid, euser.username, euser.alias);
	} 
	return 0;
}

// print the create_time of the room 
int test4(int argc, char *argv[]) 
{
	int channel = 0;
	int rid = 0;
	if (argc > 1) { channel = atoi(argv[1]); }
	if (argc > 2) { rid = atoi(argv[2]); }

	printf("test4 [channel=%d] [room_id=%d]\n", channel, rid);

	// room_t * proom = g_room_list[channel] + rid;
	// char buff[100];
	// ctime_r(&(proom->create_time), buff);
	// printf("create_time = [%s]\n", buff);

	return 0;
}

// print the g_user_room (eid -> room_t *)
int test5(int argc, char *argv[]) 
{
	
	cout << "g_user_room : " << endl;
	map<int, room_t*>::iterator it = g_user_room.begin();
	while (it!=g_user_room.end()) {
		cout << it->first << " => " << it->second << endl;
		it++;
	}
	return 0;
}

// print the room play list
// usage: test 6 0 1      (0=channel, 1=roomid)
int test6(int argc, char *argv[])
{
	int channel = 0;
	int room = 1;
	if (argc > 1) channel = atoi(argv[1]);
	if (argc > 2) room = atoi(argv[2]);
	if (channel < 0 || channel >= MAX_CHANNEL) channel = 0;
	if (room < 1 || room > MAX_ROOM) room = 1; // base 1
	
	printf("room[%d][%d].cmd_list :\n", channel, room);

	vector<string> & plist = g_room_list[channel][room].cmd_list;
	vector<string>::iterator it = plist.begin();
	while (it!=plist.end()) {
		cout << *it++ << endl;  // do not add count
	}

	return 0;
}

// print the status in LUA
// usage:  test 7 0 1      (0=channel, 1=roomid)
int test7(int argc, char *argv[])
{
	int channel = 0;
	int room = 1;
	if (argc > 1) channel = atoi(argv[1]);
	if (argc > 2) room = atoi(argv[2]);
	if (channel < 0 || channel >= MAX_CHANNEL) channel = 0;
	if (room < 1 || room > MAX_ROOM) room = 1; // base 1
	
	room_t * proom = g_room_list[channel] + room;
	printf("room[%d][%d].lua==NULL ? %d\n", channel, room
	, proom->lua==NULL);

	if (NULL==proom->lua) {
		return -3;
	}

	lu_print_index_both(proom->lua);
	lu_print_status(proom->lua);

	return 0;
}

// test: quick_add eid, now
// test 8 [eid] [rating] [now]
/** sample test case 
eid/rating/name: 10/1010/peter, 11/1011/masha  12/1312/kelton, 13/1413/win
note on rating diff:  (kelton-peter)=302(60sec),  (win-kelton)=101(30sec)
and:   win-masha=402(60sec)    masha-peter=1(0secs)
test 8 10 1010 0
test 9 0
output:  no match
test 8 12 1312 1
test 9 1
output:  no match
test 8 11 1011 10
test 9 10
output: match (10,11)
test 8 13 1413 35

test 8 10 1010 0
test 8 12 1312 1
test 8 11 1011 10
test 8 13 1413 35
test 9 35

// match up 10, 11
test 8 10 1010 0
test 8 11 1011 10
test 8 12 1312 15
test 8 13 1413 20
test 9 35

test 8 10 1010 0
test 8 11 1511 10
test 8 12 1612 15
test 8 13 1013 20
test 9 20  (only 10,13)
or 
test 9 40  (all)


match: (11,12)   (13, 14)   (15,16)
test 8 11 1011 10
test 8 12 1012 10
test 8 13 1313 20
test 8 14 1414 20
test 8 15 1815 30
test 8 16 1816 30

test 9 50 		(all)
test 9 30 		(11,12)  (15,16)


testcase for different now will trigger different match up pairs
--------------
test 8 11 1011 10
test 8 12 1822 10
test 8 13 1711 20
test 8 14 1422 21
test 8 15 1312 30
test 8 16 1016 30

test 9 30 	(11,16)
test 9 50 	(11,16)  (12,13)  
test 9 60   (11,16)  (12,13)  (14,15)
test 9 90 	(11,12)  (13,14)  (15,16)

 */
int test8(int argc, char *argv[])
{
	int eid = 10;
	double rating = 1000;
	time_t now = 0;
	if (argc < 4) {
		ERROR_RETURN(-5, "need param: test 8 [eid] [rating] [now]");
	}
	eid = strtol_safe(argv[1], -1);
	rating = strtol_safe(argv[2], -1);
	now = strtol_safe(argv[3], -1);
	if (eid < 0 || rating < 0 || now < 0) {
		ERROR_RETURN(-15, "invalid input: test 8 [eid] [rating] [now]");
	}

	evil_user_t euser;
	euser.eid = eid;
	euser.rating = rating;
	int ret = quick_add(euser, now);
	ERROR_NEG_RETURN(ret, "test8_quick_add");
	return 0;
}


// test 9 [now]
// now can be any integer >= 0
// test: quick_match
int test9(int argc, char *argv[])
{
	time_t now = 0;
	if (argc > 1) { 
		now = strtol_safe(argv[1], now);
	}
	DEBUG_PRINT((int)now, "test9:now");
	
	vector< pair<int,int> > pair_list;
	int ret = quick_match(pair_list, now);
	printf("test9:ret=%d\n", ret);
	if (pair_list.size() == 0) {
		printf(" no match!!\n");
		return 0;
	}
	printf("Total match %zu :", pair_list.size());
	for (unsigned int i=0; i<pair_list.size(); i++) {
		printf("  (%d,%d)", pair_list[i].first, pair_list[i].second);
	}
	printf("\n");
	return 0;
}


// test:  list the g_quick_list
int test10(int argc, char *argv[])
{
	QUICK_LIST::iterator it;
	int count = 0;
	for (it=g_quick_list.begin(); it != g_quick_list.end(); it++) {
		quick_t &q = *it;
		cout << count++ << " : eid=" << q.eid << "  rating=" << q.rating
		<< " start_time=" << q.start_time << endl;
	}
	return 0;
}

int test11(int argc, char *argv[])
{
	// double rating1 = 1613;
	// double rating2 = 1609;
	double rating1 = 4000;
	double rating2 = 1000;
	double kfactor = 32;
	double diff = 0.0;

	diff = elo_rating(rating1, rating2, 1, kfactor);
	printf("win=1 : elo diff = %lf\n", diff);
	diff = elo_rating(rating1, rating2, 2, kfactor);
	printf("win=2 : elo diff = %lf\n", diff);
	return 0;
}


int test12(int argc, char *argv[])
{
	int gid = 550;
	guild_t & guild = get_guild_by_gid(gid);
	printf("guild.gid = %d\n", gid);
	printf("guild_address = %p\n", &guild);

	guild_add_member(guild, 547);
	print_guild_member(guild);

	guild_del_member(guild, 547);

	printf("after del : guild.gid %d\n", guild.gid);

	guild_t & guildx = get_guild_by_gid(580);

	printf("after del2222 : guild.gid %d\n", guild.gid);
	guild_t & guild2 = get_guild_by_gid(gid);

	printf("after del3333 : guildx.gid %d\n", guildx.gid);
	printf("--- guild2=%p\n", &guild2);

	return 0;
}


// print g_guild_map
int test13_list_guild_map(int argc, char *argv[])
{
	int total = 0;
	map<int, guild_t>::iterator it = g_guild_map.begin();

	printf("test13: list all guilds in memory\n");
	while (it != g_guild_map.end()) {
		print_guild_member(it->second);
		it++;
		total++;
	}

	printf("test13: total guild in memory: %d\n", total);
	
	return 0;
}

int test14(int argc, char *argv[])
{

	int ret;
	const char *card="0000100000000000000002200321030000000000111100000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	const char *ptr;
	char count[2];
	int hero;
	int total;
	
	lua_State * lua;
	lua = luaL_newstate();
	if (lua == NULL) {
		BUG_PRINT(-7, "test14:lua_null");
	}
	luaL_openlibs(lua);
	lu_set_int(lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(lua, "res/lua/logic.lua");
	lua_pushinteger(lua, 1974);

	total = 0;
	hero = 0;
	ptr = card;
	for (int i=1;i<=EVIL_CARD_MAX;i++) {
		sscanf(ptr, "%1s", count);

		if (i <= HERO_MAX && atoi(count) > 0 && atoi(count) <= 9) {
			if (hero != 0) {
				ERROR_PRINT(-9, "test14:already_has_hero hero=%d card=%d"
				, hero, i);
				break;
			}
			if (atoi(count) > 1) {
				ERROR_PRINT(-9, "test14:two_hero hero=%d count=%d"
				, hero, atoi(count));
				break;
			}
			hero = i;
			total++;
			DEBUG_PRINT(0, "test14:count=%s, i=%d", count, i);
		}

		if (i > HERO_MAX && atoi(count) > 0 && atoi(count) <= 9) {
			lua_getglobal(lua, "fit_hero_id");
			lua_pushinteger(lua, hero); 
			lua_pushinteger(lua, i); 
			ret = lua_pcall(lua, 2, 1, 0);  // 2=input param, 1=output return
			if (0 != ret) {
				lua_pop(lua, 1); 
				ERROR_PRINT(ret, "lu_print_index_both");
				return -1;
			}
			ret = lua_toboolean(lua, -1);
			if (ret == 0) {
				WARN_PRINT(-9, "test14:fit_hero_false hero=%d card=%d ret=%d"
				, hero, i, ret);
			}
			lua_pop(lua, 1);
			total++;
		}

		ptr += 1;
	}

	ret = lua_tointeger(lua, -1);
	LU_CHECK_MAGIC(lua);
	lua_pop(lua, 1);
	lua_close(lua);  // clean up lua VM
	lua = NULL;  

	DEBUG_PRINT(0, "test14:hero=%d total=%d", hero, total);
	
	return 0;
}

int test15(int argc, char *argv[])
{

	char password_a[10];
	char password_b[10];

	sprintf(password_a, "qwerty");
	sprintf(password_b, "rwerty");

	DEBUG_PRINT(0, "strlen %d %d", (int)strlen(password_a), (int)strlen(password_b));

	for (int i=0;i<(int)strlen(password_a);i++){
		if (password_a[i] == password_b[i]) {
			DEBUG_PRINT(0, "word_same %d", password_a[i]);
		} else {
			DEBUG_PRINT(0, "word_mismatch %d %d", password_a[i], password_b[i]);
		}
	}
		
	return 0;
}

int test16(int argc, char *argv[])
{
	char pwd[] = "_password";

	printf("test16:pwd[0] = %d\n", pwd[0]);

	printf("test16: _ = %d\n", '_');

	return 0;
}


int test17_check_deck(int argc, char *argv[])
{
	int ret;

	char deck[] = "0000000000000001000000000000000000000000023020200002103002000000000000400404420000000000000000000000000000000000000000000000000000000000000000020000020000003000000000000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	// hero=16 shadow mage (add 92 inner strength (priest card)
	char deck2[] = "0000000000000001000000000000000000000000023020200002103002000000000000400404420000000000000100000000000000000000000000000000000000000000000000020000020000003000000000000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	// 136 good descendent,  155 bazaar(neutral ok)
	char deck3[] = "0000000000000001000000000000000000000000023020200002103002000000000000400404420000000000000000000000000000000000000000000000000000000001000000020000020000203000000000000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";


	ret = check_deck(deck, deck, 888);
	printf("check_deck ret %d\n", ret);

	ret = check_deck(deck2, deck2, 777);
	printf("check_deck bad job ret %d\n", ret);

	ret = check_deck(deck3, deck3, 666);
	printf("check_deck bad camp ret %d\n", ret);

	return 0;
}

int test18_get_gameid(int argc, char *argv[])
{
	long gameid = get_gameid();
	DEBUG_PRINT(0, "test18:game_id = %ld", gameid);

	return 0;
}

int test19_print_challenge(int argc, char *argv[])
{
	int count = 0;
	CHALLENGE_LIST::iterator it;
	for (it=g_challenge_list.begin(); it < g_challenge_list.end(); it++) {
		challenge_t &ct = *it;
		DEBUG_PRINT(0, "print_challenge:eid1=%d eid2=%d alias1=%s alias2=%s time=%ld"
		, ct.eid_challenger, ct.eid_receiver
		, ct.alias_challenger, ct.alias_receiver
		, ct.challenge_time);

		count ++;
	}
	DEBUG_PRINT(0, "print_challenge:count = %d", count);
	return 0;
}


// create 2 design_mission VS 1 time and 3 times
int test20_mission_vs(int argc, char *argv[])
{
	printf("---- test mission_vs\n");
	design_mission_t *dlist = g_design->mission_list;

	bzero(dlist, MAX_LEVEL * sizeof(design_mission_t));

	// mid; pre; lv; hero; daily; mtype; p1; p2; p3; reward_card; reward_exp; reward_gold; reward_crystal; char reset_time[10]; // 00:00 char mtext[302]; 
	{
		// mid,pre,lv,hero,daily,guild_lv      mtype,p1,p2,p3,
		// reward card,exp,gold,crystal     reset_time, mtext
		design_mission_t dmis = { 
			4, 0, 0,0,0, 0,	 3, 1, 0, 0
			, 1, 2, 3,	0, 1,{25},0,{}
			, "00:00", "Win_VS_1_time,get_card_25,exp_1.."
		};
		dlist[dmis.mid] = dmis;
	}
	{
		design_mission_t dmis = { 
			5, 0, 0,0,0, 0,	 3, 3, 0, 0
			, 0, 10, 0, 0,  0,{},0,{}
			, "00:00", "Win_VS_3_time,gold_10"
		};
		dlist[dmis.mid] = dmis;
	}

	printf("TODO: log x x;  lmis; quick\n");
	printf("TODO: log y y;  lmis; quick;  fold\n");
	return 0;
}

int test21_force_next(int argc, char *argv[])
{
	const char * cmd;
	int force[2];
	int ret;
	printf("---- test force_next\n");

	force[0] = 0;
	force[1] = 0;
	
	// 111 go first
	
	cmd = "f";  // in 111 round, 000 do a force
	ret = force_next_check(cmd, force[0], force[1]);
	printf("000 cmd=%s  force[0]=%d  force[1]=%d  ret = %d\n"
		, cmd, force[0], force[1], ret);
	// 000 round

	cmd = "f";  // in 000 round, 111 do a force
	ret = force_next_check(cmd, force[1], force[0]);
	printf("111 cmd=%s  force[0]=%d  force[1]=%d  ret = %d\n"
		, cmd, force[0], force[1], ret);

	// 111 round

	cmd = "b 2201";
	ret = force_next_check(cmd, force[1], force[0]);
	printf("111 cmd=%s  force[0]=%d  force[1]=%d  ret = %d\n"
		, cmd, force[0], force[1], ret);

	cmd = "n";  // 111 do:  clear force[1] = 0
	ret = force_next_check(cmd, force[1], force[0]);
	printf("111 cmd=%s  force[0]=%d  force[1]=%d  ret = %d\n"
		, cmd, force[0], force[1], ret);  // exp: force[0]=0
	
	// 000 round

	cmd = "f";  // 111 do : force[0] ++
	ret = force_next_check(cmd, force[1], force[0]);
	printf("111 cmd=%s  force[0]=%d  force[1]=%d  ret = %d\n"
		, cmd, force[0], force[1], ret);  // exp: force[1]=2
	
	// 1111 round

	cmd = "s 2201";
	ret = force_next_check(cmd, force[1], force[0]);
	printf("111 cmd=%s  force[0]=%d  force[1]=%d  ret = %d\n"
		, cmd, force[0], force[1], ret);  // exp: force[1]=2

	cmd = "n";  // 111 do n (clear my force[1] = 0)
	ret = force_next_check(cmd, force[1], force[0]);
	printf("111 cmd=%s  force[0]=%d  force[1]=%d  ret = %d\n"
		, cmd, force[0], force[1], ret);  // exp: force[1]=2

	// 0000 round

	cmd = "f";  // 111 do f,  force[0] ++
	ret = force_next_check(cmd, force[1], force[0]);
	printf("111 cmd=%s  force[0]=%d  force[1]=%d  ret = %d\n"
		, cmd, force[0], force[1], ret);  // exp: force[1]=2


	return 0;
}

int test22_bug_newconn(int argc, char *argv[])
{

	int index = 0;
	if (argc > 1) {
		index = strtol_safe(argv[1], 1);
	}
	connect_t *conn;
	conn = get_conn(index);
	printf("test22:index = %d state=%d\n", index, conn->state);
	conn->state = STATE_READING;
	return 0;
}

// int add_exp(connect_t *conn, int exp, int *lv_offset, int *this_exp);
int test23_add_exp(int argc, char *argv[])
{
	int lv_offset = 0;
	int this_exp = 0;
	int exp_offset = 0;
	int exp_next = 0;
	connect_t conn;	
	bzero(&conn, sizeof(conn));
	evil_user_t & euser = conn.euser;
	euser.eid = 547;

	conn.euser.lv = 2;
	conn.euser.exp = 5;
	exp_offset = 599;
	exp_next = add_exp(&conn, exp_offset, &lv_offset, &this_exp);
	DEBUG_PRINT(0, "test23:case1 euser.lv=%d exp=%d exp_next=%d this_exp=%d lv_offset=%d", euser.lv, euser.exp, exp_next, this_exp, lv_offset);


	conn.euser.lv = 30;
	conn.euser.exp = 2700;
	exp_offset = 300;
	exp_next = add_exp(&conn, exp_offset, &lv_offset, &this_exp);
	DEBUG_PRINT(0, "test23:case2 euser.lv=%d exp=%d exp_next=%d this_exp=%d lv_offset=%d", euser.lv, euser.exp, exp_next, this_exp, lv_offset);

	conn.euser.lv = 29;
	conn.euser.exp = 2500;
	exp_offset = 3150;
	exp_next = add_exp(&conn, exp_offset, &lv_offset, &this_exp);
	DEBUG_PRINT(0, "test23:case3 euser.lv=%d exp=%d exp_next=%d this_exp=%d lv_offset=%d", euser.lv, euser.exp, exp_next, this_exp, lv_offset);


	conn.euser.lv = 1;
	conn.euser.exp = 0;
	exp_offset = 50;
	exp_next = add_exp(&conn, exp_offset, &lv_offset, &this_exp);
	DEBUG_PRINT(0, "test23:case4 euser.lv=%d exp=%d exp_next=%d this_exp=%d lv_offset=%d", euser.lv, euser.exp, exp_next, this_exp, lv_offset);

	conn.euser.lv = 21;
	conn.euser.exp = 1999;
	exp_offset = 10;
	exp_next = add_exp(&conn, exp_offset, &lv_offset, &this_exp);
	DEBUG_PRINT(0, "test23:case5 euser.lv=%d exp=%d exp_next=%d this_exp=%d lv_offset=%d", euser.lv, euser.exp, exp_next, this_exp, lv_offset);
	
	return 0;
}

// 1. fill up mlist,  mid=i+1, status=NULL
// 2. select 5 mission (6, 8, 9, 10, 15) = status = OK
// 3. select 8 mission (25, 26, 27, ... 33) status = READY
// 4. page_size = 10 : get_mission_page(...., page_size, start_index=0)
//    expect: 15, 10, 9, 8, 6,    33, 32, 31, 30, 29
// 5. page_size = 3, start_index=0:  15, 10, 9
// 6. page_size = 3, start_index=4:  6, 33, 32
// 7. page_size = 10, start_index=13:   empty
// 8. page_size = 10, start_index=12:   25

int test24_get_mission_page(int argc, char *argv[])
{
	int page_size;
	int start_index;
	int plist_size = 0;
	int mlist_type = 0;
	int chapter_id = 0;
	mission_t mlist[MAX_MISSION];
	mission_t *plist[MAX_MISSION];


	bzero(mlist, sizeof(mlist)); // only stack declaraction works, ptr fail

	for (int i=0; i<MAX_MISSION; i++) {
		mlist[i].mid = i;  // base 0
	}

// 2. select 5 mission (6, 8, 9, 10, 15) = status = OK
// 3. select 8 mission (25, 26, ... 31, 32) status = READY
	
	mlist[6].status = MISSION_STATUS_OK;
	mlist[8].status = MISSION_STATUS_OK;
	mlist[9].status = MISSION_STATUS_OK;
	mlist[10].status = MISSION_STATUS_OK;
	mlist[15].status = MISSION_STATUS_OK;

	// 25 .. 32 : ready
	for (int i=25; i<=32; i++) {
		mlist[i].status = MISSION_STATUS_READY;
	}


	page_size = 10;		start_index = 0;	
	plist_size = get_mission_page(plist, mlist, page_size, start_index
	, mlist_type, chapter_id);
	ERROR_NEG_RETURN(plist_size, "test_get_mission_page:case1");
	printf("----- case1: plist_size = %d\n", plist_size);
	for (int i=0; i<plist_size; i++) {
		mission_t * pmis = plist[i];
		printf("  i=%d  mid=%d  status=%d\n", i, pmis->mid, pmis->status);
	}

	page_size = 3;		start_index = 0;	
	plist_size = get_mission_page(plist, mlist, page_size, start_index
	, mlist_type, chapter_id);
	ERROR_NEG_RETURN(plist_size, "test_get_mission_page:case2");
	printf("----- case2: plist_size = %d\n", plist_size);
	for (int i=0; i<plist_size; i++) {
		mission_t * pmis = plist[i];
		printf("  i=%d  mid=%d  status=%d\n", i, pmis->mid, pmis->status);
	}

	page_size = 3;		start_index = 4;	
	plist_size = get_mission_page(plist, mlist, page_size, start_index
	, mlist_type, chapter_id);
	ERROR_NEG_RETURN(plist_size, "test_get_mission_page:case_%d_%d", page_size, start_index);
	printf("----- case2: plist_size = %d\n", plist_size);
	for (int i=0; i<plist_size; i++) {
		mission_t * pmis = plist[i];
		printf("  i=%d  mid=%d  status=%d\n", i, pmis->mid, pmis->status);
	}

	// 7. page_size = 10, start_index=13:   empty
	page_size = 10;		start_index = 13;	
	plist_size = get_mission_page(plist, mlist, page_size, start_index
	, mlist_type, chapter_id);
	ERROR_NEG_RETURN(plist_size, "test_get_mission_page:case_%d_%d", page_size, start_index);
	printf("----- plist_size = %d  (page_size=%d, start_index=%d)\n"
	, plist_size, page_size, start_index);
	for (int i=0; i<plist_size; i++) {
		mission_t * pmis = plist[i];
		printf("  i=%d  mid=%d  status=%d\n", i, pmis->mid, pmis->status);
	}

	// 8. page_size = 10, start_index=12:   25
	page_size = 10;		start_index = 12;	
	plist_size = get_mission_page(plist, mlist, page_size, start_index
	, mlist_type, chapter_id);
	ERROR_NEG_RETURN(plist_size, "test_get_mission_page:case_%d_%d", page_size, start_index);
	printf("----- plist_size = %d  (page_size=%d, start_index=%d)\n"
	, plist_size, page_size, start_index);
	for (int i=0; i<plist_size; i++) {
		mission_t * pmis = plist[i];
		printf("  i=%d  mid=%d  status=%d\n", i, pmis->mid, pmis->status);
	}

	// 9. negative page_size and start_index
	page_size = 50;		start_index = -10;	
	plist_size = get_mission_page(plist, mlist, page_size, start_index
	, mlist_type, chapter_id);
	ERROR_NEG_RETURN(plist_size, "test_get_mission_page:case_%d_%d", page_size, start_index);
	printf("----- plist_size = %d  (page_size=%d, start_index=%d)\n"
	, plist_size, page_size, start_index);
	for (int i=0; i<plist_size; i++) {
		mission_t * pmis = plist[i];
		printf("  i=%d  mid=%d  status=%d\n", i, pmis->mid, pmis->status);
	}

	// 10. very large page_size
	page_size = MAX_MISSION + MAX_MISSION; 	start_index = 0;
	plist_size = get_mission_page(plist, mlist, page_size, start_index
	, mlist_type, chapter_id);
	ERROR_NEG_RETURN(plist_size, "test_get_mission_page:case_%d_%d", page_size, start_index);
	printf("----- plist_size = %d  (page_size=%d, start_index=%d)\n"
	, plist_size, page_size, start_index);
	for (int i=0; i<plist_size; i++) {
		mission_t * pmis = plist[i];
		printf("  i=%d  mid=%d  status=%d\n", i, pmis->mid, pmis->status);
	}
	return 0;
}
	


int test25_design_card(int argc, char *argv[]) {
	
	printf("----- test design_card info -----\n");
	for (int i = 0; i < MAX_STAR; i++) {
		star_t & star_one = g_design->star_list[i];
		printf("==== star size = %d\n", star_one.size);
		for (int j = 0; j < star_one.size; j++) {
			card_t & card = star_one.card_list[j];
			printf(" star info id=%d star=%d cost=%d job=%d name=%s\n", card.id, card.star, card.cost, card.job, card.name); 
		}
	}
	
	printf("\n\n -- extract list info below -- \n\n");

	for (int i = 0; i < MAX_STAR; i++) {
		star_t & star_one = g_design->extract_list[i];
		printf("==== star size = %d\n", star_one.size);
		for (int j = 0; j < star_one.size; j++) {
			card_t & card = star_one.card_list[j];
			printf(" star info id=%d star=%d cost=%d job=%d name=%s\n", card.id, card.star, card.cost, card.job, card.name); 
		}
	}
	printf("----- end test design_card info -----\n");
	
	return 0;
}



int test26_exp_list(int argc, char *argv[]) {
	
	printf("---- start test exp_list ----\n");

	for (int i = 0; i <= g_design->max_level; i++) {
		printf(" exp info level=%d exp=%d\n", i, g_design->exp_list[i]);
	}
	printf("\n max level = %d \n", g_design->max_level);

	printf("---- end test exp_list ----\n");
	
	return 0;
}

int test27_force_leave_room(int argc, char *argv[]) {

	int ret;

	ret = force_leave_room(547);
	DEBUG_PRINT(0, "test27:ret=%d", ret);
	
	return 0;
}

int get_lottery_card(int team, int money_type);
int test28_lottery(int argc, char *argv[]) {

	int ret;
	int card_id;

	int c1 = 0;
	int c2 = 0;
	int c3 = 0;

	for (int i=0; i<1; i++) {
		card_id = get_lottery_card(1, 1);
		if (card_id >= 20 && card_id <30) {
			c1++;
		} else if (card_id >= 30 && card_id <40) {
			c2++;
		} else if (card_id >= 40 && card_id <50) {
			c3++;
		}
	}

	DEBUG_PRINT(0, "test28:c1=%d c2=%d c3=%d", c1, c2, c3);

	const char * ttt = "000286";
	sscanf(ttt, "%d", &ret);

	printf("ret=%d\n", ret);

	ret = 0;
	
	return 0;
}
	
int test29_add_exp(int argc, char *argv[]) {
	connect_t *conn = get_conn_by_eid(547);
	if (conn == NULL) {return 0;}

	DEBUG_PRINT(0, "test28:before lv=%d exp=%d", conn->euser.lv, conn->euser.exp);
	int count = 1100;
	int lv_offset = 0; // should be zero at this point
	int exp_offset = 0;
	add_exp(conn, count, &lv_offset, &exp_offset);
	DEBUG_PRINT(0, "test28:after lv=%d exp=%d", conn->euser.lv, conn->euser.exp);
	return 0;
}


int test30_count_card(int argc, char *argv[]) {
	char deck[400];
	int count;

	strcpy(deck, "22 23 25");
	count = count_card(deck);
	printf("count=%d  deck=[%s]\n", count, deck);
	ERROR_RETURN(count!=3, "test30_count_card:case1 %d != 3", count);

	strcpy(deck, "");
	count = count_card(deck);
	printf("count=%d  deck=[%s]\n", count, deck);
	ERROR_RETURN(count!=0, "test30_count_card:case2 %d != 0", count);

	strcpy(deck, "abc ded ggg");
	count = count_card(deck);
	printf("count=%d  deck=[%s]\n", count, deck);
	ERROR_RETURN(count!=0, "test30_count_card:case3 %d != 0", count);

	strcpy(deck, "22 25 abc 66");
	count = count_card(deck);
	printf("count=%d  deck=[%s]\n", count, deck);
	ERROR_RETURN(count!=2, "test30_count_card:case4 %d != 0", count);

	return 0;
}

int test31_card_percent(int argc, char *argv[]) {
	int ret;
	const char *card_list = "1000110100000000000054301424043501311512000000000000000000000022102002222424042200000000002002212021000000000000000000000000000000020200000000000000000002400000020000000002200000000000000200000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
	const char *slot_list = "1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
	ret = __get_recommand_card_percent(card_list, slot_list);
	DEBUG_PRINT(0, "test31:recommand_card_percent_ret[%d]", ret);
	ret = __get_recommand_card_percent(slot_list, slot_list);
	DEBUG_PRINT(0, "test31:recommand_card_percent_ret[%d]", ret);

	return 0;
}

int __random_piece_shop(int *piece_list, int hard_show, int slot_count)
{
	int count = 0;
	if (hard_show > 0 && hard_show <= g_design->max_pshop_hard_show)
	{
		design_pshop_show_map_t &show_map = g_design->pshop_show_map[hard_show];

		memcpy(piece_list, show_map.pid_list, sizeof(show_map.pid_list));
		count = show_map.count;

		if (count > 0 && count != slot_count) {
			BUG_RETURN(-7, "__random_piece_shop:count_not_enough %d", count);
		}

		return count;
	}

	int max_weight = g_design->max_piece_shop_weight;
	design_piece_shop_t *shop_ptr_list[MAX_PIECE_SHOP_LIST];
	for (int i = 0; i < MAX_PIECE_SHOP_LIST; i++)
	{
		shop_ptr_list[i] = &g_design->piece_shop_list[i];
	}


	int cur_weight = 0;
	for (int i = 0; i < slot_count; i++)
	{
		int w = (int)(random() % max_weight) + 1;
		// INFO_PRINT(1, "w = %d max_weight = %d", w, max_weight);
		cur_weight = 0;
		for (int j = 0; j < MAX_PIECE_SHOP_LIST; j++)
		{
			if (shop_ptr_list[j] == NULL)
			{
				continue;
			}
			cur_weight += shop_ptr_list[j]->weight;
			if (w <= cur_weight)
			{
				max_weight -= shop_ptr_list[j]->weight;
				piece_list[count++] = shop_ptr_list[j]->id;
				shop_ptr_list[j] = NULL;
				break;
			}
		}
		if (count != i+1) // means can not get any shop_item
		{
			BUG_RETURN(-15, "__random_piece_shop:count_not_enough");
			break;
		}
	}

	if (count != slot_count) {
		BUG_RETURN(-25, "__random_piece_shop:count_not_enough %d", count);
	}

	return count;
}

int test32_piece_shop(int argc, char *argv[]) {
	int ret;
	int slot_count;
	int piece_list[MAX_PIECE_SHOP_SLOT];
	int pid_list[MAX_PIECE_SHOP_LIST];
	bzero(piece_list, sizeof(piece_list));

	int hard_show = 1;
	int total_outcome = 10000;
	if (argc > 1) {
		total_outcome = strtol_safe(argv[1], total_outcome);
	}
	if (argc > 2) {
		hard_show = strtol_safe(argv[2], hard_show);
	}

	slot_count = MAX_PIECE_SHOP_SLOT;
	ret = __random_piece_shop(piece_list, hard_show, slot_count);
	ERROR_NEG_RETURN(ret, "test32_piece_shop:random_piece_shop");
	
	printf("piece_list[%d] count = %d\t", hard_show, ret);
	for (int i = 0; i < slot_count; i++)
	{
		printf(" [%d]", piece_list[i]);
	}
	printf("\n");

	if (1) return 0;

	bzero(piece_list, sizeof(piece_list));
	hard_show = 0;
	ret = __random_piece_shop(piece_list, hard_show, slot_count);
	ERROR_NEG_RETURN(ret, "test32_piece_shop:random_piece_shop");
	
	DEBUG_PRINT(1, "piece_list count = %d", ret);
	for (int i = 0; i < slot_count; i++)
	{
		DEBUG_PRINT(1, "piece_list[%d] = %d", i, piece_list[i]);
	}

	bzero(pid_list, sizeof(pid_list));
	for (int i=0; i<total_outcome; i++) {
		bzero(piece_list, sizeof(piece_list));
		hard_show = 0; slot_count = 1;
		ret = __random_piece_shop(piece_list, hard_show, slot_count);
		ERROR_NEG_RETURN(ret, "test32_piece_shop:random_piece_shop");
		// printf("piece_list count = %d \t", ret);
		int pid = piece_list[slot_count-1];
		pid_list[pid] ++;
	}

	printf("pid_list: total_weight=%d\n", g_design->max_piece_shop_weight);
	for (int i = 0; i < MAX_PIECE_SHOP_LIST; i++)
	{
		design_piece_shop_t & item = g_design->piece_shop_list[i];
		if (item.id == 0) {continue;}
		double rate = (double) item.weight / (double) g_design->max_piece_shop_weight;
		double outcome = (double)pid_list[i] / (double)total_outcome;
		printf("%d\t%d\t%d\t%lf\t%lf\t%s\n"
		, i, pid_list[i], item.weight, rate, outcome
		, fabs(rate - outcome) < 0.01 ? "OK" : "XXX"
		);
	}
	printf("pid\tcount\tweight\trate\toutcome\n");
	printf("total_outcome = %d\n", total_outcome);


	return 0;
}

int test33_quick_reward(int argc, char *argv[]) {
	
	int size = 10000;
	if (argc > 1) {
		size = strtol_safe(argv[1], size);
	}
	int pos_list[4];
	bzero(pos_list, sizeof(pos_list));
	int pos = 0;
	int win_flag = 0;
	int gold;
	int crystal;
	int sum = 0;
	for (int i=0; i<size; i++) {
		pos = get_quick_reward(&gold, &crystal, win_flag);
		if (pos < 0 || pos >=MAX_QUICK_REWARD) {
			ERROR_RETURN(-6, "test33_pos_error %d", pos);
		}
		pos_list[pos]++;
	}

	for (int i=0; i<MAX_QUICK_REWARD; i++) {
		printf("pos=%d times=%d percent=%lf\n", i, pos_list[i], ((double)pos_list[i])/size);
		sum += pos_list[i];
	}
	printf("sum=%d\n", sum);

	return 0;
}

typedef int (*testcase_t) (int, char *[]); // function pointer

testcase_t test_list[] = {
	test0
,	test1
, 	test2
,	test3
,	test4
, 	test5
,	test6	// print cmd_list  (room.cmd_list)
,	test7  	// print_index_both()  (room.lua)
,	test8	// quick_add
,	test9	// quick_match
,	test10	// quick_list
,	test11	// elo rating test elo_rating()
,	test12	// test guild
,	test13_list_guild_map
,	test14
,	test15
,	test16
,	test17_check_deck
,	test18_get_gameid
,	test19_print_challenge
,	test20_mission_vs
,	test21_force_next
,	test22_bug_newconn
,	test23_add_exp
,   test24_get_mission_page
,	test25_design_card
,	test26_exp_list
,	test27_force_leave_room
,	test28_lottery
,	test29_add_exp
,	test30_count_card
,	test31_card_percent
,	test32_piece_shop
,	test33_quick_reward
};

const int TOTAL_TEST = sizeof(test_list) / sizeof(testcase_t);

int admin_login(connect_t* conn, const char *cmd, const char *buffer) {
	if (strstr(buffer, "New2xin") == buffer) {
		net_writeln(conn, "%s 0 OK", cmd);
		return ST_ADMIN;
	}

	net_writeln(conn, "%s -6 password_error", cmd);
	return 0;
}

int admin_test(connect_t* conn, const char *cmd, const char *buffer) {
	int ret; 
	int tc;   // testcase number
	char arg[BUFFER_SIZE+1];
	char *token[10]; // max 9, one more for safety
	int argc;
	tc = atoi(buffer); // maybe 0
	if (tc < 0 || tc >= TOTAL_TEST) {
		net_writeln(conn, "test -2 tc overflow tc=%d  total=%d", tc, TOTAL_TEST);
		return -2;
	}

	strcpy(arg, buffer);
	argc = split9(token, arg);

	ret = test_list[tc](argc, token);
	net_writeln(conn, "test %d tc=%d", ret, tc);
	
	return ret;
}

// this function is for channel register/login user
// CMD: @login [username] [password]
int admin_check_login(connect_t* conn, const char *cmd, const char *buffer) {
	int ret;
	char username[EVIL_USERNAME_MAX+10];
	char password[EVIL_USERNAME_MAX+10];
	int channel;

	ret = sscanf(buffer, "%s %s %d", username, password
	, &channel);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -5, "input_invalid");
	}
	if (strlen(username) <= 0 || strlen(password) <= 0) {
		NET_ERROR_RETURN(conn, -15, "input_invalid");
	}

	ret = dbin_write(conn, cmd, DB_CHECK_LOGIN, IN_CHECK_LOGIN_PRINT
	, username, password, channel);
	
	return ret;
}



int admin_flush(connect_t* conn, const char *cmd, const char *buffer) {
	int ret;
	ret = fflush(stdout);
	net_writeln(conn, "%s %d", cmd, ret);
	return 0;
}

int str_quick_list(int *total, char *buffer, QUICK_LIST *list)
{
	char * ptr = buffer;
	*total = 0;
	QUICK_LIST::iterator it;
	it=list->begin(); 
	for (it=list->begin(); it<list->end(); it++) {
		quick_t &qq = *it;
		if (qq.eid == 0) {
			continue;
		}
		buffer += sprintf(buffer, "%d ", qq.eid);
		(*total)++;
		// printf("str_quick_list:%ld\n", buffer-ptr);
		if (buffer - ptr > 990) {
			return -2;
		}
	}

	return 0;
}

// print quick list
int admin_list_quick(connect_t* conn, const char *cmd, const char *buffer) 
{
	
	int ret;
	int total = 0;
	char out_buffer[1000];	

	total = 0;
	out_buffer[0] = '\0';
	ret = str_quick_list(&total, out_buffer, &g_fight_gold_list);
	net_writeln(conn, "%s %d g_fight_gold_list:total=%d\n%s", cmd, ret, total, out_buffer);

	total = 0;
	out_buffer[0] = '\0';
	ret = str_quick_list(&total, out_buffer, &g_fight_crystal_list);
	net_writeln(conn, "%s %d g_fight_crystal_list:total=%d\n%s", cmd, ret, total, out_buffer);

	total = 0;
	out_buffer[0] = '\0';
	ret = str_quick_list(&total, out_buffer, &g_fight_free_list);
	net_writeln(conn, "%s %d g_fight_free_list:total=%d\n%s", cmd, ret, total, out_buffer);

	return 0;
}

// 
// online [username] [alias]
// partial match on 
// if username=="_" means all username, need partial match on alias
int admin_online(connect_t* conn, const char *cmd, const char *buffer) {

	INFO_PRINT(0, "admin_online:start");
	// int ret;
	int total = 0;
	/*
	char username[EVIL_USERNAME_MAX+1];
	char alias[EVIL_ALIAS_MAX+1];
	// char ip[100];
	username[0] = 0;
	alias[0] = 0;
	sscanf(buffer, "%s %s", username, alias);
	if (0==strcmp(username, "_")) {
		username[0] = 0;
	}
	*/

	total = 0;
	for (int i=0; i<MAX_CONNECT; i++) {
		connect_t * guest_conn = get_conn(i);
		if (guest_conn==NULL || guest_conn->state==STATE_FREE) {
			continue; // offline or no login
		}
		if (guest_conn->euser.eid > 0) {
			total ++;
		}
	}

	net_writeln(conn, "%s %d", cmd, total);

	/*
	net_writeln(conn, "Pattern match: username=[%s] alias=[%s]", username, alias);

	total = 0;
	for (int i=0; i<MAX_CONNECT; i++) {
		connect_t * guest_conn = get_conn(i);
		if (guest_conn==NULL || guest_conn->state==STATE_FREE) {
			continue; // offline or no login
		}

		
		address_readable(ip, &(guest_conn->address)); // ip len 16+8
		if (guest_conn->euser.eid==0) {
			net_writeln(conn, "%s cid=%d %s %s not_login"
			, cmd, i, ip, (guest_conn->db_flag>0) ? "DB" : "");
			continue;
		}

		if (NULL==strstr(guest_conn->euser.username, username)) {
			continue;
		}
		if (NULL==strstr(guest_conn->euser.alias, alias)) {
			continue;
		}
		// only count normal online user
		total ++;

		room_t *proom = guest_conn->room;
		char str[200];
		char *ptr = str;
		ptr += sprintf(ptr, "cid=%d %s %s eid=%d %s %s st%d", i
		, ip, (guest_conn->db_flag>0) ? "DB" : "", guest_conn->euser.eid
		, guest_conn->euser.username, guest_conn->euser.alias, guest_conn->st);
		if (NULL == proom) {
			ptr += sprintf(ptr, " no_room");
		} else {
			ptr += sprintf(ptr, " room(%d,%d)", proom->channel, proom->rid);
			ptr += sprintf(ptr, " gameid %ld seed %d lua %s",
				proom->gameid, proom->seed, (proom->lua==NULL) ? "NULL" : "Yes");
		}

		net_writeln(conn, "%s %s", cmd, str);
	}
	*/

	net_writeln(conn, "total user=%d  g_num_connect=%d  g_free_connect=%d"
	, total, g_num_connect, g_free_connect);
	return 0;
}

int admin_kill(connect_t *conn, const char *cmd, const char *buffer) {
	int ret;
	int eid;
	ret = sscanf(buffer, "%d", &eid);
	if (ret <= 0) {
		NET_ERROR_RETURN(conn, -5, "invalid_eid");
	}
	connect_t * guest_conn = get_conn_by_eid(eid);
	if (guest_conn==NULL || guest_conn->state==STATE_FREE) {
		NET_ERROR_RETURN(conn, -6, "eid_offline");
	}

	if (guest_conn->euser.eid != eid) {
		NET_ERROR_RETURN(conn, -7, "euser_eid_not_match");
	}

	// order is important if kill my eid
	net_writeln(conn, "%s 0 killed_eid %d", cmd, eid);
	do_clean_disconnect(guest_conn);
	return 0;
}


// pgame [channel] [rid] = print game of a specific room in (channel, rid)
int admin_pgame(connect_t *conn, const char *cmd, const char *buffer) {
	char arg[BUFFER_SIZE+1];
	char *argv[10]; // max 9, one more for safety
	int argc;
	strcpy(arg, buffer);
	argc = split9(argv, arg);
	int channel = 0;
	int room = 1;
	if (argc < 1) {
		NET_ERROR_RETURN(conn, -5, "[eid] or pgame [channel] [rid]");
	}
	if (argc == 2) {
		channel = atoi(argv[0]);
		room = atoi(argv[1]);
	} else if (argc == 1) {
		int eid = atoi(argv[0]);
		connect_t * guest_conn;
		guest_conn = get_conn_by_eid(eid);
		if (guest_conn == NULL) {
			NET_ERROR_RETURN(conn, -3, "offline_eid");
		}
		if (guest_conn->room==NULL) {
			NET_ERROR_RETURN(conn, -13, "null_room");
		}
		channel = guest_conn->room->channel;
		room = guest_conn->room->rid;
	}
	if (channel < 0 || channel >= MAX_CHANNEL) channel = 0;
	if (room < 1 || room > MAX_ROOM) room = 1; // base 1

	
	room_t * proom = g_room_list[channel] + room;
	net_writeln(conn, "%s room[%d][%d].lua=%s", cmd, channel, room
	, (proom->lua==NULL) ? "null" : "yes");

	if (NULL==proom->lua) {
		return -3;
	}

	lu_print_index_both(proom->lua);
	lu_print_status(proom->lua);

	fflush(stdout); // ignore ret
	
	return 0;
}

/*
	match begin:
	admin_add_match()
	match_apply()
	admin_match_init()
	admin_round_start() : update match.round
	admin_round_end() : end round, round = match.round
	admin_round_start()
	admin_round_end()
	...
*/

// @match max_player start_time t1 t2 t3 t4 title
// e.g.: @match_add 32 20141222 2000 2200 0 0 match1
// @match_add 4 20141222 2000 2200 0 0 match1
/*

// if MATCH_CHECK_DURATION = 10, MATCH_ROUND_TIME = 100
// -85: round time too close
@match_add 4 20141222 2000 2200 2201 0 match2
// -95: round over one day
@match_add 4 20141222 2000 2200 2359 0 match2
// ok:
@match_add 4 20141222 2000 2200 2358 0 match2

*/
int admin_add_match(connect_t *conn, const char *cmd, const char *buffer) {
	int ret;
	int n;
	int max_player;
	int start_date_str;
	int round_time[MAX_DAILY_ROUND];
	char title[100];
	const char *ptr;
	ptr = buffer;

	ret = sscanf(ptr, "%d %d %n"
	, &max_player, &start_date_str, &n);

	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "admin_add_match:invalid_input");
	}

	int flag = 0;
	for (int i=2; i<=10; i++) {
		if (max_player == pow(2, i)) {
			flag = 1;
			break;
		}
	}

	if (flag == 0) {
		NET_ERROR_RETURN(conn, -15, "admin_add_match:max_player_error %d"
		, max_player);
	}

	// get a free match
	match_t *match = NULL;
	for (int i = 0; i < MAX_MATCH; i++) {
		if (g_match_list[i].match_id == 0) {
			match = g_match_list + i;
			break;
		}
	}
	if (match == NULL) {
		NET_ERROR_RETURN(conn, -2, "admin_add_match:match_list_full");
	}
	//

	// get start time
	int year = start_date_str / 10000;
	int month = (start_date_str % 10000) / 100;
	int day = start_date_str % 100;
	if (year <= 0 || month <= 0 || month > 12
	|| day <= 0 || day > 31) {
		NET_ERROR_RETURN(conn, -25, "admin_add_match:invalid_start_date: %d"
		, start_date_str);
	}

	time_t now = time(NULL);
	struct tm timestruct;
	localtime_r(&now, &timestruct);
	timestruct.tm_year = year - 1900;
	timestruct.tm_mon = month - 1;
	timestruct.tm_mday = day;
	timestruct.tm_hour = 0;
	timestruct.tm_min = 0;
	timestruct.tm_sec = 0;
	time_t start_date = mktime(&timestruct);
	DEBUG_PRINT(0, "admin_add_match:start_date: %ld %s\n", start_date, asctime(&timestruct));
	if (start_date < now) {
		NET_ERROR_RETURN(conn, -12, "admin_add_match:start_date %ld %ld", start_date, now);
	}
	//
	
	// get round time
	for (int i=0; i<MAX_DAILY_ROUND; i++) {
		ptr += n;
		ret = sscanf(ptr, "%d %n", &round_time[i], &n);
		if (ret != 1) {
			NET_ERROR_RETURN(conn, -35, "admin_add_match:invalid_input");
		}
		if (round_time[i] < 0) {
			NET_ERROR_RETURN(conn, -45, "admin_add_match:invalid_round_time %d", round_time[i]);
		}
		if (i == 0 && round_time[i] == 0) {
			NET_ERROR_RETURN(conn, -55
			, "admin_add_match:invalid_round_time %d", round_time[i]);
		}
		if (i > 0 && round_time[i] != 0
		&& round_time[i] <= round_time[i-1]) {
			NET_ERROR_RETURN(conn, -65
			, "admin_add_match:invalid_round_time %d", round_time[i]);
		}
	}

	time_t round_time_list[MAX_DAILY_ROUND];
	for (int i = 0; i < MAX_DAILY_ROUND; i++) {
		int hour = round_time[i] / 100;
		int minute = round_time[i] % 100;
		if (hour < 0 || hour >= 24 || minute < 0 || minute >= 60) {
			NET_ERROR_RETURN(conn, -75
			, "admin_add_match:match_round_time_error %ld"
			, round_time_list[i]);
		}

		round_time_list[i] = (hour * 60 * 60) + minute * 60;

		if (i >= 1 && round_time_list[i] != 0 
		&& round_time_list[i-1] + MATCH_ROUND_TIME + 2 * MATCH_CHECK_DURATION >= round_time_list[i]) {
			NET_ERROR_RETURN(conn, -85
			, "admin_add_match:round_time_too_close %ld %ld %d"
			, round_time_list[i-1], round_time_list[i], MATCH_ROUND_TIME);
		}

		if (round_time_list[i] + MATCH_ROUND_TIME + MATCH_CHECK_DURATION >= 60 * 60 * 24) {
			NET_ERROR_RETURN(conn, -95
			, "admin_add_match:round_over_one_day %ld %d"
			, round_time_list[i], MATCH_ROUND_TIME);
		}

	}
	//

	ptr += n;
	ret = sscanf(ptr, "%100s", title);

	ret = match_create(*match, max_player, start_date, round_time_list, title);
	DEBUG_PRINT(0, "admin_add_match: match_id=%ld, title=%s max_player=%d max_team=%d start_date=%ld round_time_list=%ld %ld %ld %ld status=%d\n"
	, match->match_id, match->title, match->max_player
	, match->max_team, match->start_time, match->round_time_list[0]
	, match->round_time_list[1], match->round_time_list[2]
	, match->round_time_list[3], match->status);

	ret = dbin_write(conn, cmd, DB_ADD_MATCH, IN_ADD_MATCH_PRINT
	, match->match_id, match->title, match->max_player, match->start_time
	, match->round_time_list[0], match->round_time_list[1]
	, match->round_time_list[2], match->round_time_list[3], 0);


	net_writeln(conn, "%s %ld ok", cmd, match->match_id);

	return 0;
}

int admin_delete_match(connect_t *conn, const char *cmd, const char *buffer) 
{
	int ret;
	long match_id;

	ret = sscanf(buffer, "%ld", &match_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "admin_match_init:input_error");
	}

	if (match_id <= 0) {
		NET_ERROR_RETURN(conn, -15, "admin_match_init:invalid_match_id %ld", match_id);
	}

	match_t &match = get_match(match_id);
	if (match.match_id == 0) {
		NET_ERROR_RETURN(conn, -3, "admin_match_init:match_null %ld"
		, match_id);
	}

	if (match.status == MATCH_STATUS_DELETE) {
		NET_ERROR_RETURN(conn, -6, "admin_match_init:match_round_already_delete %d", match.status);
	}

	if (match.status == MATCH_STATUS_ROUND_START) {
		NET_ERROR_RETURN(conn, -16, "admin_match_init:match_round_already_start %d", match.status);
	}

	ret = dbin_write(conn, "@match_update", DB_UPDATE_MATCH
	, IN_UPDATE_MATCH_PRINT, match_id, match.round, MATCH_STATUS_DELETE);

	// XXX update match.status in db
	bzero(&match, sizeof(match_t));
		
	net_writeln(conn, "%s %ld ok", cmd, match_id);
	return 0;
}

int admin_match_init(connect_t *conn, const char *cmd, const char *buffer) 
{
	INFO_PRINT(0, ">>>>>>>>>>>>>>>>>admin_match_init:start=%ld", time(NULL));
	int ret;
	long match_id;

	ret = sscanf(buffer, "%ld", &match_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "admin_match_init:input_error");
	}

	if (match_id <= 0) {
		NET_ERROR_RETURN(conn, -15, "admin_match_init:invalid_match_id %ld", match_id);
	}

	match_t &match = get_match(match_id);
	if (match.match_id == 0) {
		NET_ERROR_RETURN(conn, -3, "admin_match_init:match_null %ld"
		, match_id);
	}
	if (match.status != MATCH_STATUS_READY) {
		NET_ERROR_RETURN(conn, -6, "admin_match_init:match_not_ready %d"
		, match.status);
	}

	ret = match_init(&match);
	if (ret != 0) {
		NET_ERROR_RETURN(conn, -16, "admin_match_init:init_fail %d"
		, ret);
	}


	/*

	int num = 0; // match apply player num
	for(int j=0; j < match.max_player; j++) {
		match_player_t &player = match.player_list[j];
		if (player.eid == 0) {
			continue;
		}
		num ++;
	}
	DEBUG_PRINT(0, "admin_match_init:match.max_player=%d apply_player=%d"
	, match.max_player, num);
	
	// check if ai are enough
	int count = match.max_player - num; // need ai count
	int ai_count = 0;
	for (int i=MAX_AI_EID-1; i>=1; i--) {
		if (g_design->ai_list[i].id != 0) {
			ai_count ++;
		}
	}
	if (ai_count < count) {
		NET_ERROR_RETURN(conn, -2, "admin_match_init:ai_not_enough %d %d"
		, ai_count, count);
	}

	ai_count = 0;
	char ai_buffer[3000];
	char * ai_ptr;
	ai_ptr = ai_buffer;
	if (count > 0) {
		for (int i=MAX_AI_EID-1; i>=1; i--) { 
			if (g_design->ai_list[i].id == 0) {
				continue;
			}

			if (count == 0) {
				break;
			}

			ai_t &pai = g_design->ai_list[i];
			ret = match_apply(match, pai.id, pai.alias);
			if (ret != 0) {
				WARN_PRINT(-16, "admin_match_init:ai_apply_error %d"
				, pai.id);
				continue;
			}
			count--;

			ai_ptr += sprintf(ai_ptr, " %d %s", pai.id, pai.alias);
			ai_count++;
			// every 50 ai do 1 time db_write
			if (ai_count == 50) {
				ret = dbin_write(conn, "match_apply_ai", DB_MATCH_APPLY_AI
				, IN_ADD_MATCH_AI_PRINT
				, match_id, ai_count, ai_buffer);
				ai_count = 0;
				ai_ptr = ai_buffer;
				if (count == 0) {
					break;
				}
				continue;
			}
			// last ai do 1 time db_write
			if (count == 0) {
				ret = dbin_write(conn, "match_apply_ai", DB_MATCH_APPLY_AI
				, IN_ADD_MATCH_AI_PRINT
				, match_id, ai_count, ai_buffer);
				break;
			}

		}
	}


	ret = match_team_init(match);
	if (ret != 0) {
		NET_ERROR_RETURN(conn, -26, "admin_match_init:init_error %ld"
		, match.match_id);
	}

	print_all_record(match, match.player_list);
	match_info(match, match.player_list);

	// db_buffer="eid1 eid2 eid3 ... eidn"
	char db_buffer[6000];
	char * ptr;
	ptr = db_buffer;
	for (int i = 0; i<match.max_player; i++) {
		ptr += sprintf(ptr, " %d", match.player_list[i].eid);
	}

	ret = dbin_write(conn, cmd, DB_MATCH_TEAM_INIT
	, "%ld %d %s"
	, match_id, match.max_player, db_buffer);

	*/
	net_writeln(conn, "%s %ld ok", cmd, match.match_id);
	return ret;
}


// before do this function, every player must be in same round
// 1. get match
// 2. loop get 2 player in this round
// 3. if two players are ai, only call match.match_result() max(player.eid) win 2 game
// 4. if player is watching a game, leave that room
// 5 .if player is running a game, force fold that game
// 6. put these 2 players into a match game room and start game
int admin_team_round_start(match_t * match)
{
	int ret;

	if (match == NULL) {
		ERROR_RETURN(-2, "admin_team_round_start:match_null");
	}
	// init in match.match_init()
	if (match->status != MATCH_STATUS_ROUND_END) {
		ERROR_RETURN(-6, "admin_team_round_start:match_not_end");
	}

	// check all player in same round, and get now round
	// note: if load match in db, that match status may == round_start, load_db_match will change status to round_end, but this round is not over, in round start function should not update match.round, just start the rest of game in this round
	// now match.round is base on player.round
	DEBUG_PRINT(0, "----------- admin_team_round_start:round=%d", match->round);
	/*
	int round = 0; // base 0
	for (int i = MAX_TEAM_ROUND-1; i >= 0; i--) {
		int count_neg = 0;
		int count_zero = 0;
		for ( int j = 0; j < match->max_player; j++) {
			match_player_t &player = match->player_list[i*match->max_player + j];
			if (player.eid < 0) {
				count_neg++;
			}
			if (player.eid == 0) {
				count_zero++;
			}
		}
		// round not ready
		if (count_neg + count_zero == match->max_player) {
			continue;
		}

		// some player ready, some player not ready, may caused by load match in db
		if (count_neg != 0) {
			WARN_PRINT(-16, "admin_team_round_start:match_round_not_ok %ld %d", match->match_id, i);
			continue;
		}

		// this round is now round
		round = i;
		break;
	}
	match->round = round;
	*/

	int round = -1;
	int not_ready_round = 0;
	for (int i = (match->max_player * MAX_TEAM_ROUND) - 1; i >= 0; i--) {
		match_player_t &player = match->player_list[i];
		if (player.eid < 0) {
			if (player.round < round && round > 0) {
				BUG_RETURN(-7, "admin_team_round_start:player_list_buy %ld", match->match_id);
			}
			not_ready_round = player.round;
			round = -1;
			continue;
		}
		if (player.round == not_ready_round) {
			continue;
		}
		if (player.round > round) {
			round = player.round;
		}
	}
	if (round <= 0) {
		BUG_RETURN(-17, "admin_team_round_start:no_round_ready %ld", match->match_id);
	}

	// match.round is define by player round
	match->round = round;
	match->status = MATCH_STATUS_ROUND_START;
	DEBUG_PRINT(0, "-------------- admin_team_round_start:new_round = %d", match->round);


	// loop player
	for (int i = 0; i < match->max_team; i++) {
		for (int j = 0; j < MAX_TEAM_PLAYER; j++ ) {
			match_player_t &player1 = match->player_list[(match->round-1) 
			* match->max_player + i * MAX_TEAM_PLAYER + j];
			if (player1.eid == 0) {
				continue;
			}
			// get oppo player
			for (int k = j+1; k < MAX_TEAM_PLAYER; k++ ) {
				match_player_t &player2 = match->player_list[(match->round-1) * match->max_player + i * MAX_TEAM_PLAYER + k];
				if (player1.round != player2.round 
				|| player1.team_id != player2.team_id) {
					ERROR_RETURN(-7, "admin_team_round_start:player_round_mismatch");
				}
				if (player2.eid == 0) {
					continue;
				}
				if (player1.tid != player2.tid) {
					continue;
				}

				if (player1.point >= 5 || player2.point >= 5) {
					continue;
				}
				DEBUG_PRINT(0, "admin_team_round_start:game_two_player %d %d", player1.eid, player2.eid);

				// if player1, player2 both are ai, no need to create match room, just do match_result to make them have fake match result
				if (player1.eid <= MAX_AI_EID 
				&& player2.eid <= MAX_AI_EID) {
					int max_eid = max(player1.eid, player2.eid);
					int min_eid = min(player1.eid, player2.eid);
					ret = match_result(*match, max_eid
					, min_eid, 1, 1);
					if (ret != 0) {
						ERROR_PRINT(ret, "admin_team_round_start:ai1_match_result_fail");
					}

					ret = match_result(*match, max_eid
					, min_eid, 1, 2);
					if (ret != 0) {
						ERROR_PRINT(ret, "admin_team_round_start:ai2_match_result_fail");
					}

					// write match result into db
					ret = save_match_result(*match, player1, player2);
					if (ret <= 0) {
						ERROR_PRINT(ret, "admin_team_round_start:save_match_result_fail");
					}

					continue;
				}

				// now we get 2 player
				// 1. leave room, if game is on, fold that game
				force_leave_room(player1.eid);
				force_leave_room(player2.eid);

				// 2. create a match room, join these 2 player
				room_t *proom = create_match_room(*match, player1.eid
				, player2.eid, player1.alias, player2.alias);	
				if (proom == NULL) {
					ERROR_RETURN(-66, "admin_team_round_start:create_match_room_fail");
				}

				// 3. start game
				// order is important, room creater eid must before room joiner eid
				ret = dbin_write(NULL, "game", DB_GAME, IN_GAME_PRINT
				, proom->guest[0], proom->guest[1]);
			}
		}
	}

	ret = dbin_write(NULL, "@match_update"
	, DB_UPDATE_MATCH, IN_UPDATE_MATCH_PRINT
	, match->match_id, match->round, match->status);

	return 0;
}

int admin_eli_round_start(match_t * match)
{
	int ret;

	if (match == NULL) {
		ERROR_RETURN(-2, "admin_eli_round_start:match_null");
	}
	// init in match.match_init()
	if (match->status != MATCH_STATUS_ROUND_END) {
		ERROR_RETURN(-6, "admin_eli_round_start:match_not_end");
	}

	DEBUG_PRINT(0, "admin_eli_round_start:round = %d", match->round);

	// check player in same round, and get now round
	int round = -1;
	int not_ready_round = 0;
	for (int i = match->max_player-1; i >= 0; i--) {
		match_player_t &player = match->e_player_list[i];
		if (player.eid <= 0) {
			if (player.round < round && round > 0) {
				BUG_RETURN(-7, "admin_eli_round_start:player_list_buy %ld", match->match_id);
			}
			not_ready_round = player.round;
			round = -1;
			continue;
		}
		if (player.round == not_ready_round) {
			continue;
		}
		if (player.round > round) {
			round = player.round;
		}
	}
	if (round <= 0) {
		BUG_RETURN(-17, "admin_eli_round_start:no_round_ready %ld", match->match_id);
	}

	// player.round base 1, match.round base 0
	match->round = round;
	match->status = MATCH_STATUS_ROUND_START;
	DEBUG_PRINT(0, "admin_eli_round_start:new_round = %d", match->round);


	// loop player
	for (int i = 0; i < match->max_player; i++) {
		match_player_t &player1 = match->e_player_list[i];
		if (player1.round != match->round) {
			continue;
		}
		if (player1.eid <= 0) {
			BUG_PRINT(-3, "admin_eli_round_start:player_eid_error %d %d"
			, player1.eid, i);
			continue;
		}
		// get oppo player
		for (int j = i + 1; j < match->max_player; j++) {
			match_player_t &player2 = match->e_player_list[j];
			if (player2.round != player1.round) {
				continue;
			}
			if (player2.eid <= 0) {
				BUG_PRINT(-13, "admin_eli_round_start:player_eid_error %d %d"
				, player2.eid, j);
				continue;
			}

			int max_tid = max(player1.tid, player2.tid);
			int min_tid = min(player1.tid, player2.tid);
			if (max_tid - min_tid != 1 || min_tid % 2 != 0) {
				continue;
			}

			if (player1.point >= 5 || player2.point >= 5) {
				continue;
			}

			DEBUG_PRINT(0, "admin_eli_round_start:game_two_player %d %d %d %d"
			, player1.eid, player2.eid, player1.point, player2.point);

			// if player1, player2 both are ai, no need to create match room, just do match_result to make them have fake match result
			if (player1.eid <= MAX_AI_EID 
			&& player2.eid <= MAX_AI_EID) {
				int max_eid = max(player1.eid, player2.eid);
				int min_eid = min(player1.eid, player2.eid);
				ret = match_result(*match, max_eid
				, min_eid, 1, 1);
				if (ret != 0) {
					ERROR_PRINT(ret, "admin_eli_round_start:ai1_match_result_fail");
				}

				ret = match_result(*match, max_eid
				, min_eid, 1, 2);
				if (ret != 0) {
					ERROR_PRINT(ret, "admin_eli_round_start:ai2_match_result_fail");
				}

				// write match result into db
				ret = save_match_result(*match, player1, player2);
				if (ret <= 0) {
					ERROR_PRINT(ret, "admin_eli_round_start:save_match_result_fail");
				}

				continue;
			}

			// now we get 2 player
			// 1. leave room, if game is on, fold that game
			force_leave_room(player1.eid);
			force_leave_room(player2.eid);

			// 2. create a match room, join these 2 player
			room_t *proom = create_match_room(*match, player1.eid
			, player2.eid, player1.alias, player2.alias);	
			if (proom == NULL) {
				ERROR_RETURN(-66, "admin_eli_round_start:create_match_room_fail");
			}

			// 3. start game
			// order is important, room creater eid must before room joiner eid
			ret = dbin_write(NULL, "game", DB_GAME, IN_GAME_PRINT
			, proom->guest[0], proom->guest[1]);
		}
	}

	ret = dbin_write(NULL, "@match_update"
	, DB_UPDATE_MATCH, IN_UPDATE_MATCH_PRINT
	, match->match_id, match->round, match->status);

	DEBUG_PRINT(0, "admin_eli_round_start:now_round = %d", match->round);

	return 0;
}

// loop get 2 player in match.round, check:
// 0. if a player point >= 5, continue
// 1. if player in game, draw this game, and stop win_game start a new game
// 2. which player get more point in this round, win
// 3. if point are the same, max(eid) win
int admin_team_round_end(match_t * match)
{
	int ret;

	if (match == NULL) {
		ERROR_RETURN(-2, "admin_team_round_end:match_null");
	}
	if (match->status != MATCH_STATUS_ROUND_START) {
		ERROR_RETURN(-6, "admin_team_round_end:match_not_start");
	}

	// loop player
	for (int i = 0; i < match->max_team; i++) {
		for (int j = 0; j < MAX_TEAM_PLAYER; j++ ) {
			match_player_t &player1 = match->player_list[(match->round-1) 
			* match->max_player + i * MAX_TEAM_PLAYER + j];
			if (player1.eid == 0) {
				continue;
			}
			for (int k = j+1; k < MAX_TEAM_PLAYER; k++ ) {
				match_player_t &player2 = match->player_list[(match->round-1) * match->max_player + i * MAX_TEAM_PLAYER + k];
				if (player1.round != player2.round 
				|| player1.team_id != player2.team_id) {
					ERROR_RETURN(-7, "admin_team_round_end:player_round_mismatch");
				}
				if (player2.eid == 0) {
					continue;
				}
				if (player1.tid != player2.tid) {
					continue;
				}

				if (player1.point >= 5 || player2.point >= 5) {
					continue;
				}

				// 2 player ai round should finish in round_start, below is bug
				if (player1.eid <= MAX_AI_EID && player2.eid <= MAX_AI_EID) {
					BUG_PRINT(-16, "admin_team_round_end:ai_round_not_end %d %d", player1.eid, player2.eid);
					continue;
				}

				DEBUG_PRINT(0, "admin_team_round_end:two_player %d(%d) %d(%d)"
				, player1.eid, player1.point, player2.eid, player2.point);

				ret = force_end_match_round(*match, player1, player2);
				if (ret != 0) {
					BUG_PRINT(-26, "admin_team_round_end:force_end_match_round_fail %d %d", player1.eid, player2.eid);
					continue;
				}

			}
		}
	}

	// for admin_round_start
	match->status = MATCH_STATUS_ROUND_END;

	ret = dbin_write(NULL, "@match_update"
	, DB_UPDATE_MATCH, IN_UPDATE_MATCH_PRINT
	, match->match_id, match->round, match->status);


	return 0;
}

int admin_eli_round_end(match_t * match)
{
	int ret;

	if (match == NULL) {
		ERROR_RETURN(-2, "admin_eli_round_end:match_null");
	}
	if (match->status != MATCH_STATUS_ROUND_START) {
		ERROR_RETURN(-6, "admin_eli_round_end:match_not_start");
	}

	DEBUG_PRINT(0, "admin_eli_round_end:round=%d", match->round);

	// loop player
	for (int i = 0; i < match->max_player; i++) {
		match_player_t &player1 = match->e_player_list[i];
		if (player1.round != match->round) {
			continue;
		}
		if (player1.eid <= 0) {
			BUG_PRINT(-3, "admin_eli_round_end:player_eid_error %d %d"
			, player1.eid, i);
			continue;
		}
		for (int j = i + 1; j < match->max_player; j++) {
			match_player_t &player2 = match->e_player_list[j];
			if (player2.round != player1.round) {
				continue;
			}
			if (player2.eid <= 0) {
				BUG_PRINT(-13, "admin_eli_round_end:player_eid_error %d %d"
				, player2.eid, j);
				continue;
			}

			int max_tid = max(player1.tid, player2.tid);
			int min_tid = min(player1.tid, player2.tid);
			if (max_tid - min_tid != 1 || min_tid % 2 != 0) {
				continue;
			}

			if (player1.point >= 5 || player2.point >= 5) {
				continue;
			}

			// 2 player ai round should finish in round_start, below is bug
			if (player1.eid <= MAX_AI_EID && player2.eid <= MAX_AI_EID) {
				BUG_PRINT(-16, "admin_eli_round_end:ai_round_not_end %d %d", player1.eid, player2.eid);
				continue;
			}

			DEBUG_PRINT(0, "admin_eli_round_end:two_player %d(%d) %d(%d)"
			, player1.eid, player1.point, player2.eid, player2.point);

			ret = force_end_match_round(*match, player1, player2);
			if (ret != 0) {
				BUG_PRINT(-26, "admin_eli_round_end:force_end_match_round_fail %d %d", player1.eid, player2.eid);
				continue;
			}

		}
	}

	// for admin_round_start
	match->status = MATCH_STATUS_ROUND_END;

	// match over
	for (int i = match->max_player; i >= 0; i--) {
		match_player_t &player = match->e_player_list[i];
		if (player.tid == 1) {
			if (player.eid > 0) {
				match->status = MATCH_STATUS_FINISHED;
			}
			break;
		}
	}

	ret = dbin_write(NULL, "@match_update"
	, DB_UPDATE_MATCH, IN_UPDATE_MATCH_PRINT
	, match->match_id, match->round, match->status);


	return 0;
}

int match_init(match_t * match) 
{
	int ret;

	if (match->status != MATCH_STATUS_READY) {
		ERROR_RETURN(-6, "admin_match_init:match_not_ready %d"
		, match->status);
	}

	int num = 0; // match apply player num
	for(int j=0; j < match->max_player; j++) {
		match_player_t &player = match->player_list[j];
		if (player.eid == 0) {
			continue;
		}
		num ++;
	}
	DEBUG_PRINT(0, "admin_match_init:match->max_player=%d apply_player=%d"
	, match->max_player, num);
	
	// check if ai are enough
	int count = match->max_player - num; // need ai count
	int ai_count = 0;
	for (int i=MAX_AI_EID-1; i>=1; i--) {
		if (g_design->ai_list[i].id != 0) {
			ai_count ++;
		}
	}
	if (ai_count < count) {
		ERROR_RETURN(-2, "admin_match_init:ai_not_enough %d %d"
		, ai_count, count);
	}

	ai_count = 0;
	char ai_buffer[3000];
	char * ai_ptr;
	ai_ptr = ai_buffer;
	if (count > 0) {
		for (int i=MAX_AI_EID-1; i>=1; i--) { 
			if (g_design->ai_list[i].id == 0) {
				continue;
			}

			if (count == 0) {
				break;
			}

			ai_t &pai = g_design->ai_list[i];
			ret = match_apply(*match, pai.id, pai.icon, pai.alias);
			if (ret != 0) {
				WARN_PRINT(-16, "admin_match_init:ai_apply_error %d"
				, pai.id);
				continue;
			}
			count--;

			ai_ptr += sprintf(ai_ptr, " %d %d %s", pai.id, pai.icon, pai.alias);
			ai_count++;
			// every 50 ai do 1 time db_write
			if (ai_count == 50) {
				ret = dbin_write(NULL, "match_apply_ai", DB_MATCH_APPLY_AI
				, IN_ADD_MATCH_AI_PRINT
				, match->match_id, ai_count, ai_buffer);
				ai_count = 0;
				ai_ptr = ai_buffer;
				if (count == 0) {
					break;
				}
				continue;
			}
			// last ai do 1 time db_write
			if (count == 0) {
				ret = dbin_write(NULL, "match_apply_ai", DB_MATCH_APPLY_AI
				, IN_ADD_MATCH_AI_PRINT
				, match->match_id, ai_count, ai_buffer);
				break;
			}

		}
	}


	ret = match_team_init(*match);
	if (ret != 0) {
		ERROR_RETURN(-26, "admin_match_init:init_error %ld"
		, match->match_id);
	}

	print_all_record(*match, match->player_list);
	match_info(*match, match->player_list);

	// db_buffer="eid1 eid2 eid3 ... eidn"
	char db_buffer[6000];
	char * ptr;
	ptr = db_buffer;
	for (int i = 0; i<match->max_player; i++) {
		ptr += sprintf(ptr, " %d", match->player_list[i].eid);
	}

	ret = dbin_write(NULL, "@match_init", DB_MATCH_TEAM_INIT
	, "%ld %d %s"
	, match->match_id, match->max_player, db_buffer);

	return 0;
}

// this function is for auto_match_round() and admin_match_round()
int match_round(match_t * match) 
{

	int ret;
	if (match == NULL) {
		ERROR_RETURN(-2, "match_round:match_null");
	}

	if (match->status == MATCH_STATUS_READY
	|| match->status == MATCH_STATUS_FINISHED) {
		ERROR_RETURN(-6, "match_round:match_status_error %d"
		, match->status);
	}

	if (match->status == MATCH_STATUS_ROUND_START) {
		if (match->round <= MAX_TEAM_ROUND) {
			ret = admin_team_round_end(match);
			ERROR_RETURN(ret, "match_round:round_end_fail");
		}

		// after end last team game round, should init eli game player
		if (match->round == MAX_TEAM_ROUND) {
			ret = match_eli_init(*match);
			ERROR_RETURN(ret, "match_round:round_eli_init_fail");

			
			char db_buffer[6000];
			char * ptr;
			ptr = db_buffer;
			for (int i = 0; i<match->max_player/2; i++) {
				ptr += sprintf(ptr, " %d", match->e_player_list[i].eid);
			}

			ret = dbin_write(NULL, "@match_eli_init", DB_MATCH_ELI_INIT
			, "%ld %d %s"
			, match->match_id, match->max_player/2, db_buffer);

		}

		if (match->round > MAX_TEAM_ROUND) {
			ret = admin_eli_round_end(match);
			ERROR_RETURN(ret, "match_round:round_end_fail");
		}

		return 0;
	}

	if (match->status == MATCH_STATUS_ROUND_END) {

		if (match->round < MAX_TEAM_ROUND) {
			ret = admin_team_round_start(match);
			ERROR_RETURN(ret, "match_round:round_start_fail");
			return 0;
		}
		if (match->round >= MAX_TEAM_ROUND) {
			ret = admin_eli_round_start(match);
			ERROR_RETURN(ret, "match_round:round_start_fail");
			return 0;
		}
	}

	ret = 0;
	return ret;
}


/**
 * match round start/end
 * CMD: @round [match_id]
 */
int admin_match_round(connect_t *conn, const char *cmd, const char *buffer) {

	int ret;
	long match_id;

	ret = sscanf(buffer, "%ld", &match_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "admin_match_round:input_error");
	}

	if (match_id <= 0) {
		NET_ERROR_RETURN(conn, -15, "admin_match_round:invalid_match_id %ld", match_id);
	}

	match_t &match = get_match(match_id);
	if (match.match_id == 0) {
		NET_ERROR_RETURN(conn, -3, "admin_match_round:match_null %ld"
		, match_id);
	}

	ret = match_round(&match);
	if (ret != 0) {
		NET_ERROR_RETURN(conn, -6, "admin_match_round:match_round_fail %ld %d %d"
		, match.match_id, match.status, match.round);
	}
	net_writeln(conn, "%s 0 ok", cmd);

	match_info(match, match.player_list);
	match_eli_info(match, match.e_player_list);

	return ret;
}


// CMD: @lquick
// list quick_list
int admin_lquick(connect_t *conn, const char *cmd, const char *buffer) {
	net_writeln(conn, "%s total=%d now=%ld", cmd, g_quick_list.size()
	, time(NULL));
	
	QUICK_LIST::iterator it;
	int count = 0;
	for (it=g_quick_list.begin(); it != g_quick_list.end(); it++) {
		quick_t &q = *it;
		net_writeln(conn, "%s eid=%d  rating=%lf  start_time=%ld", cmd, q.eid
		, q.rating, q.start_time);

		cout << count++ << " : eid=" << q.eid << "  rating=" << q.rating
		<< " start_time=" << q.start_time << endl;
	}
	return 0;
}


// DANGEROUS: this will cause fatal error (SIGBUS or SIGSEGV)
int admin_fatal(connect_t *conn, const char *cmd, const char *buffer) {
	int *ptr = NULL;

	net_writeln(conn, "fatal 0"); // usually cannot see this

	ptr[3] = 10; // die here!
	ptr+=3;
	printf("%s", (char*)ptr);
	return 0;
}

// @ckroom
int admin_check_room(connect_t *conn, const char *cmd, const char *buffer) {
	int num_free ;
	room_t * proom;
	connect_t *ccc; 

	for (int c=0; c<MAX_CHANNEL; c++)  {
		num_free = 0;
		for (int i=1; i<=MAX_ROOM; i++) {
			proom = &(g_room_list[c][i]);
			if (0 == proom->state) {
				num_free ++;
			}
		}
		net_writeln(conn, "%s channel[%d] : free/total = %d / %d", cmd
			, c, num_free, MAX_ROOM);
	}
	
	for (int i=0; i<MAX_CONNECT; i++) {
		ccc = g_connect_list + i;
		if (ccc->room==NULL) { continue; } // skip null room
		proom = ccc->room;
		if (0 == proom->state) {
			BUG_PRINT(-7, "conn[%d] room.state=0", i);
			net_writeln(conn, "%s BUGBUG conn[%d] room.state=0", cmd, i);
		}
	}
	return 0;
}


// @ckdb
int admin_check_db(connect_t *conn, const char *cmd, const char *buffer) {
	int free_buff = 0;
	char *ptr;
	for (int i=0; i<DB_TRANS_MAX; i++) {
		ptr = g_dbio_data[0].db_buffer[i];
		if (ptr[0] == 0) {
			free_buff ++;
		}
	}

	net_writeln(conn, "%s free/total=%d/%d", cmd, free_buff, DB_TRANS_MAX);
	return 0;
}

// @ckconn
int admin_check_conn(connect_t *conn, const char *cmd, const char *buffer) {
	int free_buff = 0;
	connect_t * tmp;
	for (int i=0; i<MAX_CONNECT; i++) {
		tmp = g_connect_list + i;
		if (tmp->state == STATE_FREE) {
			free_buff++;
		}
	}

	int free_count = 0;
	int bug_count = 0;
	int index = 0;
	index = g_free_connect;
	while ( index != -1) {
		if (g_connect_list[index].state != STATE_FREE) {
			bug_count ++;
		}
		index = g_connect_list[index].next_free;
		free_count++;
	}

	net_writeln(conn, "%s free1/free2/bug/total=%d/%d/%d/%d"
	, cmd, free_buff, free_count, bug_count, MAX_CONNECT);
	return 0;
}

// @kconn
int admin_kill_conn(connect_t *conn, const char *cmd, const char *buffer) {
	int ret;
	int cid;
	connect_t *guest_conn;
	ret = sscanf(buffer, "%d", &cid);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "kill_conn:invalid_input");
	}
	if (cid < 0 || cid >= MAX_CONNECT) {
		NET_ERROR_RETURN(conn, -5, "kill_conn:invalid_cid");
	}
	guest_conn = get_conn(cid);
	do_disconnect(guest_conn);
	net_writeln(conn, "kill_conn ok");
	return 0;
}

// @scard eid card_array
int admin_save_card(connect_t *conn, const char *cmd, const char *buffer) {
//	int fd = g_dbio_fd[0];   // hard code, TODO round robin
//	int cid = get_conn_id(conn);
	int eid;
	int ret;
//	int size, len;
	char array[EVIL_CARD_MAX + 5];
//	char in_buffer[DB_BUFFER_MAX];


	net_writeln(conn, "NOTE: pls delete the evil_card record first");
	ret = sscanf(buffer, "%d %400s", &eid, array);
	if (ret < 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input [eid] [array_400]");
	}

	if (eid == 0) {
		eid = get_eid(conn);
		if (eid <= 0) {
			NET_ERROR_RETURN(conn, -9, "not_login");
		}
	}


	ret = dbin_write(conn, cmd, DB_SAVE_CARD, IN_SAVE_CARD_PRINT, eid, array);
	return ret;

/***
	len = sprintf(in_buffer, "%d %d ", cid, DB_SAVE_CARD);
	len += sprintf(in_buffer+len, IN_SAVE_CARD_PRINT, eid, array);
	conn->db_flag = time(NULL);
	size = write(fd, in_buffer, len);
	FATAL_EXIT(size-len, "admin_save_card:write %d / %d %s", size, len, in_buffer);
	return 0;
****/
}

int admin_add_robot(connect_t *conn, const char *cmd, const char *buffer) 
{
	int ret;
	int icon;
	int hp;
	int energy;
	char username[EVIL_USERNAME_MAX+1];
	char password[EVIL_PASSWORD_MAX+1];
	char alias[EVIL_ALIAS_MAX+1];
	char deck[EVIL_CARD_MAX+1];

	ret = sscanf(buffer, "%s %s %s %d %d %d %400s"
	, username, password, alias, &icon, &hp, &energy, deck);
	if (ret < 7) {
		NET_ERROR_RETURN(conn, -5, "admin_add_robot:invalid_input %s %d", username, ret);
	}

	if (strlen(deck) != EVIL_CARD_MAX) {
		NET_ERROR_RETURN(conn, -15, "admin_add_robot:invalid_deck %lu", strlen(deck));
	}

	// DEBUG_PRINT(0, "admin_add_robot:username=[%s] password=[%s] alias=[%s] icon=%d hp=%d energy=%d deck=[%s]", username, password, alias, icon, hp, energy, deck);

	ret = dbin_write(conn, cmd, DB_ADMIN_ADD_ROBOT, "%s", buffer);

	return 0;
}

int admin_dbrestart(connect_t *conn, const char *cmd, const char *buffer) {
	int ret;
	net_writeln(conn, "%s stopping_dbio_threads", cmd);
	for (int i=0; i<MAX_DB_THREAD; i++) {
		void *ptr;
		close(g_dbio_data[i].fd);
		g_dbio_data[i].fd = -1; // make sure it is invalid
//		close(g_dbio_fd[i]);
//		g_dbio_fd[i] = -1;  // make sure it is invalid
		pthread_join(g_dbio_thread[i], &ptr);
	}
	net_writeln(conn, "%s starting_dbio_threads", cmd);
	ret = dbio_init(g_dbio_thread);   // TODO use g_dbio_data
	net_writeln(conn, "%s dbio_init ret=%d", cmd, ret);
	return 0;
}

// reload db_design
int admin_reload(connect_t *conn, const char *cmd, const char *buffer) {
	int ret;

	// not use fatal_exit   note: sizeof(design_t) is around 1M
	design_t * pdesign = (design_t*)malloc(sizeof(design_t));
	ret = load_design(0, pdesign, conn);	// 0=non-fatal exit
	if (ret == 0) {
		if (g_design != NULL) {
			free(g_design);
		}
		g_design = pdesign;
	} else {
		// error case: free the temp pdesign
		free(pdesign); 
	}

	net_writeln(conn, "%s %d", cmd, ret);
	return ret;
}

// CMD: @dbstress [max]
// stress test for dbio  (max can be 10 or 100)
int admin_dbstress(connect_t *conn, const char *cmd, const char *buffer) {
	int max;
	int ret;
	int eid = get_eid(conn);
	char deck[EVIL_CARD_MAX + 5];

	if (eid <= 0) {
		net_writeln(conn, "%s -9 not_login", cmd);
		return 0;
	}
	max = atoi(buffer);
	net_writeln(conn, "%s max=%d need login", cmd, max);

	sprintf(deck, "%.400s", "1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");

	// conn->db_flag = time(NULL);
	int bug_count=0;
	int slot = 1;
	for (int i=0; i<max; i++) {
		ret = dbin_write(conn, cmd, DB_SAVE_DECK, IN_SAVE_DECK_PRINT, eid, slot, deck);
		if (ret < 0) {
			bug_count ++;
			ERROR_NEG_PRINT(ret, "dbstress:dbin_write");
		}
	}
	net_writeln(conn, "%s max=%d bug_count=%d", cmd, max, bug_count);
	
	return 0;
}

// @lconstant  display the constant value in server memory
int admin_lconstant(connect_t *conn, const char *cmd, const char *buffer) {
	char tmp[1000];
	sprintf(tmp, "%s batch_refresh_gold=%d, pick_gold=%d, batch_refresh_crystal=%d, pick_crystal=%d, max_timeout=%d, guild_bonus_rate=%lf, create_guild_gold=%d, create_guild_crystal=%d"
	, cmd
	, g_design->constant.batch_refresh_gold, g_design->constant.pick_gold
	, g_design->constant.batch_refresh_crystal, g_design->constant.pick_crystal
	, g_design->constant.max_timeout
	, g_design->constant.guild_bonus_rate
	, g_design->constant.create_guild_gold
	, g_design->constant.create_guild_crystal
	);
	net_write(conn, tmp, '\n');
	return 0;
}


int __check_in_monthly_list(int pay_code)
{
	for (int i = 0; i < g_design->max_pay_monthly_list; i++)
	{
		if (pay_code == g_design->pay_monthly_list[i])
		{
			return 1;
		}
	}
	return 0;
}

// @pay pay_id, eid, game_money_type, game_money channel price pay_code
// TODO need to check from ip
int admin_pay(connect_t *conn, const char *cmd, const char *buffer) {
	
	long order_no;
	int eid;
	int pay_code;
	int game_money_type;
	int game_money;
	int channel; // channel: 1==alipay, 2==ninebill @see PayConfig.java
	int price;	// in fen,  1/100 yuan
	int ret;
	char ip[20];
	int monthly_flag;
	time_t user_end_date = 0;
	connect_t * guest_conn = NULL; // must hv
	char tmp_buffer[100];
	char *tmp_ptr;
	bzero(tmp_buffer, sizeof(tmp_buffer));

		

	ret = sscanf(buffer, "%ld %d %d %d %d %d %d", &order_no, &eid
		, &game_money_type, &game_money, &channel, &price, &pay_code);

	// we get the guest_conn to print error msg for the player
	if (ret >= 2) {
		guest_conn = get_conn_by_eid(eid);
	}

	if (ret != 7) {
		// if ret >= 2 : we get the eid, send the @pay -5 ...
		if (guest_conn != NULL) {
			NET_ERROR_PRINT(guest_conn, -5, 
			"admin_pay:invalid_input %d", ret);
		}
		NET_ERROR_RETURN(conn, -5, "admin_pay:invalid_input %d", ret);
	}

	address_readable(ip, &conn->address);
	DEBUG_PRINT(0, "admin_pay:ip=%s", ip);
	if (strstr(ip, "127.0.0.1") == NULL) {
		if (guest_conn != NULL) {
			NET_ERROR_PRINT(guest_conn, -9, "in_pay:invalid_ip %s", ip);
		}
		
		NET_ERROR_RETURN(conn, -9, "in_pay:invalid_ip %s", ip);
	}

	// this is only for check, wo use the params from pay_server
	design_pay_t * ppay = NULL;
	for (int i=0;i<MAX_PAY_NUM;i++) {
		ppay = g_design->pay_list + i;	
		// price and pay_price are in fen
		if (ppay->pay_code == pay_code) {
			break;
		}
	}
	if (ppay == NULL) {
		WARN_PRINT(-3, "admin_pay:design_pay_not_found pay_code=%d"
		, pay_code);	
	} else {
		// check ppay and parmas from pay_server are match	
		// game_money is from pay_server.EvilPay.pay()
		// use game_money, warn different
		if (ppay->money != game_money) {
			WARN_PRINT(-6, "admin_pay:design_pay_game_money_mismatch %d %d"
			, ppay->money, game_money);
		}

		if (ppay->money_type != game_money_type) {
			WARN_PRINT(-16, "admin_pay:design_pay_game_money_type_mismatch %d %d"
			, ppay->money_type, game_money_type);
		}

		if (ppay->price != price) {
			WARN_PRINT(-26, "admin_pay:design_pay_price_mismatch %d %d"
			, ppay->price, price);
		}
	}

	monthly_flag = __check_in_monthly_list(pay_code);
	if (guest_conn != NULL)
	{
		user_end_date = guest_conn->euser.monthly_end_date;
	}

	tmp_ptr = tmp_buffer;
	tmp_ptr += sprintf(tmp_ptr, "%d", g_design->constant.first_vip_extra_card_kind);
	for (int i = 0; i < g_design->constant.first_vip_extra_card_kind; i++)
	{
		tmp_ptr += sprintf(tmp_ptr, " %d %d"
		, g_design->constant.first_vip_extra_card_list[i][0]
		, g_design->constant.first_vip_extra_card_list[i][1]);
	}
	DEBUG_PRINT(0, "tmp_buffer[%s]", tmp_buffer);

	// DEBUG_PRINT(0, "admin_pay:money_type = %d, money = %d", money_type, money); 
	ret = dbin_write(conn, cmd, DB_PAY, IN_PAY_PRINT, order_no, eid
	, game_money_type, game_money, channel, price, pay_code
	, g_design->constant.first_pay_double, monthly_flag
	, user_end_date, g_design->constant.monthly_increase_date
	, g_design->constant.first_vip_extra_gold
	, g_design->constant.first_vip_extra_crystal
	, tmp_buffer);
	return ret;

}

int admin_chat(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int show_type;

	ret = sscanf(buffer, "%d %n", &show_type, &n);

	if (ret != 1) {
		NET_ERROR_RETURN(conn, -3, "admin_chat:sscanf %d", ret);
	}
	sys_wchat(show_type, "%s", buffer+n);

	net_writeln(conn, "%s 0", cmd);
	return ret;
}

int sys_close(connect_t* conn, const char *cmd, const char *buffer) {
	net_writeln(conn, "%s 0 OK bye!", cmd);
	do_clean_disconnect( conn );
	return ST_NULL;
}

int auto_fold(time_t now) ;
int auto_fold_guest(time_t now, int eid, time_t & offline_time);

// CMD: @autofold [eid] [time] 
// [eid] = guest eid,  if eid=0, do autofold all room, all guest
// [time] =  hh:ii   // optional, default is NOW
// 
int admin_autofold(connect_t *conn, const char *cmd, const char *buffer) 
{
	int ret;
	int eid = 0;
	time_t now = time(NULL);
	int hh = 0, ii = 0;
	struct tm tm;
    localtime_r(&now, &tm);

	sscanf(buffer, "%d %d:%d", &eid, &hh, &ii);

	if (hh > 0 || ii > 0) {
		if (hh >= 24 || ii >= 60) {
			net_writeln(conn, "%s [eid] [hh:ii]  hh or ii error");
			return 0;
		}
		tm.tm_sec = 0;
		tm.tm_min = ii;
		tm.tm_hour = hh;
		now = mktime(&tm); // replace now
	}

	// now is either NOW() or input hh:ii of today

	if (eid == 0) {
		ret = auto_fold(now);
		net_writeln(conn, "%s %d OK @ %s", cmd, ret, ctime(&now));
		return 0;
	}

	
	for (int c=0; c<MAX_CHANNEL; c++) {
		for (int r=1; r<=MAX_ROOM; r++) {
			room_t *proom = &(g_room_list[c][r]);
			if (proom==NULL) { continue; }
			if (proom->num_guest < 2) {continue; }
			if (proom->state!=ST_GAME) {continue; }

			if (proom->guest[0] == eid) {
				char nowstr[100], offstr[100];
				ret = auto_fold_guest(now, eid, proom->offline_time[0]);
				ctime_r(&now, nowstr); 
				ctime_r(&proom->offline_time[0], offstr);
				net_writeln(conn, "%s eid=%d  ret=%d  offline=%s @now %s off_time_t=%ld"
				, cmd, eid, ret, offstr
				, nowstr, proom->offline_time[0]);
				return 0;
			}
			if (proom->guest[1] == eid) {
				char nowstr[100], offstr[100];
				ret = auto_fold_guest(now, eid, proom->offline_time[0]);
				ctime_r(&now, nowstr); 
				ctime_r(&proom->offline_time[0], offstr);
				net_writeln(conn, "%s eid=%d  ret=%d  offline=%s @now %s off_time_t=%ld"
				, cmd, eid, ret, offstr
				, nowstr, proom->offline_time[1]);
				return 0;
			}
		}
	}

	net_writeln(conn, "%s cannot found eid=%d", cmd, eid);
	return 0;
}

int admin_win(connect_t *conn, const char *cmd, const char *buffer) 
{
	if (conn->st<ST_GAME) {
		NET_ERROR_RETURN(conn, -9, "admin_win:st %d", conn->st);	
	}

	room_t *proom = conn->room;
	if (NULL == proom) {
		NET_ERROR_RETURN(conn, -3, "admin_win:null_room");
	}

	if (NULL == proom->lua) {
		NET_ERROR_RETURN(conn, -13, "admin_win:null_lua");
	}

	// DEBUG_PRINT(0, "admin_win:eid1=%d eid2=%d", proom->guest[0], proom->guest[1]);
	if (proom->guest[1] > MAX_AI_EID) {
		NET_ERROR_RETURN(conn, -16, "admin_win:only_ai_work");
	}

	int ret;
	int eid = get_eid(conn);
	int side = 0;
	int win = 0;
	if (eid == proom->guest[0]) { side = 1; }
	if (eid == proom->guest[1]) { side = 2; }

	if (0 == side) {
		NET_ERROR_RETURN(conn, -19, "admin_win:non_player %d", eid);
	}


	win = side; // caller win

	char str[21];
	int cmd_size = (int) (proom->cmd_list.size() + 1);
	sprintf(str, "%d @win %d", cmd_size, side);
	proom->cmd_list.push_back(str);

	ret = win_game(conn, proom, win, WIN_GAME_NORMAL, GAME_END_NORMAL);  
	if (ret < 0)  {
		NET_ERROR_RETURN(conn, -6, "admin_win:win_game %d", ret);
	}

	return 0;
}


// note: call by crontab
// CMD: @rank_reward
int admin_rank_reward(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;

	char out_buffer[EVIL_MESSAGE_MAX+EVIL_MESSAGE_TITLE_MAX+200];

	int eid = 0;
	char alias[EVIL_ALIAS_MAX] = "系统";

	// base 1
	for (int i=1; i<=g_design->max_rank_reward_level; i++) {
		design_rank_reward_t & reward = g_design->rank_reward_list[i];
		if (reward.id <= 0) {
			WARN_PRINT(-5, "admin_rank_reward:reward.id neg %d", reward.id);
			continue;
		}
		sprintf(out_buffer, "%d %s %d %d %d %d %s %s"
		, eid, alias, reward.start, reward.end
		, reward.gold, reward.crystal, reward.title, reward.message);
		ret = dbin_write(conn, cmd, DB_RANK_REWARD, out_buffer);
	}

	net_writeln(conn, "%s OK", cmd);
	return ret;
}

// note: call by crontab
// CMD: @tower_reward
int admin_tower_reward(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;

	// base 1
	for (int i=1; i<=g_design->max_tower_reward_level; i++) {
		design_tower_reward_t & reward = g_design->tower_reward_list[i];
		if (reward.id <= 0) {
			WARN_PRINT(-5, "admin_tower_reward:reward.id neg %d", reward.id);
			continue;
		}
		ret = dbin_write(conn, cmd, DB_TOWER_REWARD
		, "%d %d %d", reward.start, reward.end, reward.battle_coin);
	}

	net_writeln(conn, "%s OK", cmd);
	return ret;
}


// CMD: @send_message [recv_eid] [title] [message]
int admin_send_message(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int recv_eid;
	int send_eid = 0;
	char alias[EVIL_ALIAS_MAX] = "系统";

	char title[EVIL_MESSAGE_TITLE_MAX];
	char message[EVIL_MESSAGE_TITLE_MAX];
	char fmt[50];
	sprintf(fmt, "%%d %%%ds %%%ds", EVIL_MESSAGE_TITLE_MAX, EVIL_MESSAGE_MAX);
	ret = sscanf(buffer, fmt, &recv_eid, title, message);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -3, "admin_send_message:input_error");
	}
	DEBUG_PRINT(0, "admin_send_message:%d %s %s", recv_eid, title, message);

	char out_buffer[1000];

	sprintf(out_buffer, "%d %d %s %s %s"
	, recv_eid, send_eid, alias, title, message);

	ret = dbin_write(conn, cmd, DB_ADD_EVIL_MESSAGE, out_buffer);

	// net_writeln(conn, "%s OK", cmd);
	return ret;
}

int sys_kickall(connect_t* conn, const char *cmd, const char *buffer) {
	int counter = 0;
	// dump way!  loop all in g_connect_list
	for (int i=0; i<MAX_CONNECT; i++) {
		// TODO get_conn
		connect_t* tmp_conn = g_connect_list + i;

		if (tmp_conn->state == STATE_FREE) {
			continue;
		}
		do_clean_disconnect( tmp_conn );
		counter ++;
	}
	net_writeln(conn, "kickall %d OK", counter);
	return 0;
}


int sys_quitall(connect_t* conn, const char *cmd, const char *buffer) {
	net_writeln(conn, "%s 0 OK", cmd);
	return LEVEL_QUITALL; // quit signal
}


int sys_echo(connect_t* conn, const char *cmd, const char *buffer) {
	// printf("sys_echo:buffer=%s\n", buffer);
	char str[200];
	sprintf(str, "echo 100 %.90s", buffer);
	net_writeln(conn, str);
	return 0;
}


int sys_info(connect_t* conn, const char *cmd, const char *buffer) {
	static char str[SMALL_BUFFER_SIZE + 1];
	static char str2[SMALL_BUFFER_SIZE + 1];
	int i;
	int guild_lv = 0;
	evil_user_t & euser = conn->euser;

	if (euser.gid > 0 && euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(euser.gid);
		guild_lv = guild.glevel;
	}
	sprintf(str, "%s 0 OK  "
		"eid=%d  username=%s  alias=%s  lv=%d rating=%lf exp=%d icon=%d sex=%d signature=%s gid=%d gpos=%d gname=%s guild_lv=%d "
		"cid=%d  st=%d  fd=%d  db_flag=%ld gold=%d crystal=%d chapter_pos=%d offset=%d  outlen=%d  "
		"daily_login:%d db[%d] reward_day[%d][%d][%d][%d][%d][%d][%d]  "
		, cmd, euser.eid, euser.username, euser.alias, euser.lv
		, euser.rating, euser.exp
		, euser.icon, euser.sex, euser.signature
		, euser.gid, euser.gpos, euser.gname, guild_lv
		, get_conn_id(conn), conn->st, conn->conn_fd, conn->db_flag
		, euser.gold, euser.crystal
		, euser.chapter_pos
		,  conn->offset , conn->outlen
		, euser.daily_login.log_day, euser.daily_login.load_from_db
		, euser.daily_login.reward_day[1]
		, euser.daily_login.reward_day[2]
		, euser.daily_login.reward_day[3]
		, euser.daily_login.reward_day[4]
		, euser.daily_login.reward_day[5]
		, euser.daily_login.reward_day[6]
		, euser.daily_login.reward_day[7]
		);


	if (conn->room == NULL) {
		strcat(str, "no_room");
	} else {
		room_t * proom = conn->room;
		sprintf(str2, "room channel=%d  id=%d   num_guest=%d  "
			"state=%d seed=%d  guest=["
			, proom->channel, proom->rid, proom->num_guest
			, proom->state , proom->seed);
		strcat(str, str2);
		for (i=0; i<proom->num_guest; i++) {
			sprintf(str2, "%s(%d) ", proom->alias[i], proom->guest[i]);
			strcat(str, str2);
		}
		strcat(str, "]");
	}

	net_writeln(conn, str);
	net_writeln(conn, "info card=[%s]", conn->euser.card);
	return ST_LOGIN;
}


int sys_status(connect_t* conn, const char *cmd, const char* buffer) {
	int ret;
	evil_user_t * user = &(conn->euser);
	// assert: user != NULL, impossible, it is pointer to concret structure

	// ret = db_load_status(user);  // XXX remove this, use memory
	ret = 0;
	if (ret == 0) {
		// TODO lv should be after eid (more important than gold)
		// 2 decimal for rating
		net_writeln(conn, "%s %d %d %.2lf %d %d %d %d %d %d %d", cmd
		, user->eid, user->lv, user->rating, user->gold, user->crystal 
		, user->game_count, user->game_win, user->game_lose
		, user->game_draw, user->game_run);
		return 0;
	}

	// error case:
	net_write_space(conn, cmd);
	switch (ret) {
		case -9: 	net_writeln(conn, "-9 not login"); 			break;
		case -55: 	net_writeln(conn, "-55 database error"); 	break;
		case -3: 	net_writeln(conn, "-3 result null"); 		break;
		case -6: 	net_writeln(conn, "-6 empty row"); 			break;
		case -7:	net_writeln(conn, "-7 field_count unmatch"); break;
		case -17:	net_writeln(conn, "-17 eid unmatch"); break;
		default:
					net_writeln(conn, "-1 unknown error %d", ret); break;
	}
	return ret;
}


// for new register user, init the user lv, gold etc
// except: eid, username, password, alias
int init_euser_sta(evil_user_t *user)
{
	if (NULL == user) {
		return -3;
	}
	user->lv = 1;
	user->rating = 1000.0; // init rating
	user->crystal = 0;
	user->gold = 100;  // free gift!

	user->gid = 0;
	user->gpos = 0;

	user->game_count = 0;
	user->game_win = 0;
	user->game_lose = 0;
	user->game_draw = 0;
	user->game_run = 0;
	return 0;
}


int sys_ver(connect_t* conn, const char *cmd, const char* buffer) {
	int ret;
	int ver = -10; // nio version
	int logic_ver = -10; 
	char tmp[SMALL_BUFFER_SIZE];
	
	lua_State * lua;
	lua = luaL_newstate();
	assert(lua != NULL);
	luaL_openlibs(lua);
	lu_set_int(lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(lua, "res/lua/logic.lua");
	logic_ver = lu_get_int(lua, "LOGIC_VERSION");
	lua_close(lua);  // clean up lua VM

	ret = sscanf(SERVER_VER, "%20s %d", tmp, &ver);
	net_writeln(conn, "ver %d %d logic_server", logic_ver, ver);

	ret = 0;
	return ret;
}

int sys_website(connect_t* conn, const char *cmd, const char* buffer) {

	int ret;
	int device_id = 0;
	design_website_t *pwebsite;

	ret = sscanf(buffer, "%d", &device_id);
	if (device_id < 0 || device_id >= MAX_WEBSITE_NUM) {
		NET_ERROR_RETURN(conn, -5, "sys_website:invalid_device_id %d"
		, device_id);
	}

	pwebsite = g_design->website_list + device_id;
	if (pwebsite->device_id < 0 || pwebsite->website[0] == '\0') {
		NET_ERROR_RETURN(conn, -15, "sys_website:invalid_device_id %d"
		, device_id);
	}

	net_writeln(conn, "%s %d %s", "getsite", pwebsite->device_id
	, pwebsite->website);

	ret = 0;
	return ret;
}


////////// SYSTEM COMMAND END /////////



//////////////////////////////////////////////////
//////////////////// DBIO START [ /////////////////
// @see dbio.cpp  IN START

int out_msg(connect_t * conn, const char *cmd, const char *buffer)
{
	// simply output the msg
	net_write_space(conn, cmd);
	net_write(conn, buffer, '\n');
	return 0; 
}

int out_test(connect_t * conn, const char *cmd, const char *buffer)
{
	net_write(conn, "test ", ' ');
	net_write(conn, buffer, '\n');
	return 0;
}

// caller will setup conn->db_flag = 0
int out_register(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int code = -75, n = 0;
//	char msg[100] = {'\0'};
	evil_user_t *puser;
	int exp_next;
	int exp_this;
	int glevel;
	int has_get_reward;
	int has_card;


	ret = sscanf(buffer, "%d %n", &code, &n);
	if (code < 0) {
		if (code==-6) {  // -6 is normal, dup username / alias
			net_writeln(conn, "%s %s", cmd, buffer);
			return code;
		}
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}

	puser = &(conn->euser); 	// take a quick pointer reference
	bzero(puser, sizeof(evil_user_t)); // clear old thing (include deck!)

	// tested:  &puser->rating means &(puser->rating)
	ret = sscanf(buffer, OUT_REGISTER_SCAN, &(puser->eid)
	, puser->username, puser->password, puser->alias, &puser->lv
	, &puser->rating, &puser->gold, &puser->crystal, &puser->gid
	, &puser->gpos, &puser->game_count, &puser->game_win
	, &puser->game_lose, &puser->game_draw, &puser->game_run
	, &puser->icon, &puser->exp, &puser->sex, &puser->course, puser->signature, puser->gname
	, &glevel, &puser->gate_pos);

	if (ret != 23) {
		NET_ERROR_RETURN(conn, -15, "out_register:sscanf %d", ret);
	}

	// printf("out_register: username=%s  pass=%s  alias=%s  lv=%d  rating=%lf\n" , puser->username, puser->password, puser->alias, puser->lv, puser->rating);

	g_user_index[conn->euser.eid] = get_conn_id(conn);

	// setup login state
	conn->st = ST_LOGIN;

	if (puser->lv >= g_design->max_level) {
		exp_next = g_design->exp_list[g_design->max_level];
	} else {
		exp_next = g_design->exp_list[puser->lv + 1];
	}
	exp_this = 0;

	// less memory than using net_writeln
	// net_write(conn, buffer, '\0');
	// TODO do we need "st" ?
	/*
	net_writeln(conn, "%s %d %s %d %d %d %d", cmd, puser->eid, puser->alias
	, puser->lv, puser->exp, exp_next, exp_this);
	*/

	has_get_reward = 0;
	has_card = 0;
	net_writeln(conn, "%s %d %d %s %d %d %d %s %d %d %d %d %d %d %d %d %d"
	, "log", puser->eid, conn->st, puser->alias
	, puser->icon, puser->gid, puser->gpos, puser->gname
	, puser->lv, puser->exp, exp_next, exp_this, LOGIC_VER
	, g_design->version.game_version, g_design->version.client_version
	, has_get_reward, has_card);

	return 0;
}


// 
int out_login(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int code = -75, n = 0;
//	char msg[100] = "invalid_error";
	evil_user_t *puser;
	int exp_next;
	int exp_this;
	int glevel;
	int has_get_reward;
	int has_card;
	char hero_buffer[BUFFER_SIZE+1];
	char *hero_ptr;

	puser = &conn->euser;  // take reference to connect_t.euser

	ret = sscanf(buffer, "%d %n", &code, &n); // using n maybe faster
	if (code < 0) {
		if (code==-6) {
			net_writeln(conn, "%s %s", cmd, buffer);
			return code;
		}
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}


	if (puser->eid != 0) {
		// last conn is login, need to do logic clean up
		printf("re-login:same conn, different login\n");
		do_clean(conn);
	}

	// duplicate login check, kick the old user if it is not my
	connect_t * old_conn = get_conn_by_eid(code);
	// printf("--- out_login get_conn_by_eid(%d) = %p\n", code, old_conn);
	if (old_conn != NULL) {
		if (old_conn != conn) {
			printf("dup-login: kick old conn\n");
			// net_writeln(old_conn, "gerr -99 duplicate_login");
			const char * out_buffer = "gerr -99 你的账户已在另一台设备上登录\n";
			write(old_conn->conn_fd, out_buffer, strlen(out_buffer));
			do_clean_disconnect(old_conn);  // force kick!
		} else {
			printf("dup-login: same conn, room_clean\n");
			do_clean(conn);
		}
	}

	bzero(puser, sizeof(evil_user_t)); // clear old thing (include deck!)
	// tested:  &puser->rating means &(puser->rating)
	ret = sscanf(buffer, OUT_LOGIN_SCAN, &(puser->eid)
	, puser->username, puser->password, puser->alias, &puser->lv
	, &puser->rating, &puser->gold, &puser->crystal, &puser->gid
	, &puser->gpos, &puser->game_count, &puser->game_win
	, &puser->game_lose, &puser->game_draw, &puser->game_run
	, &puser->icon, &puser->exp, &puser->sex, &puser->course
	, &puser->fight_ai_time, &puser->fight_gold_time
	, &puser->fight_crystal_time, puser->signals
	, &puser->monthly_end_date, puser->signature
	, puser->gname, &glevel, puser->card, &puser->gate_pos
	, &puser->chapter_pos, &has_get_reward, &has_card
	);

	if (ret != 32) {
		NET_ERROR_RETURN(conn, -15, "out_login:sscanf %d", ret);
	}

//	printf("out_login: username=%s pass=%s alias=%s lv=%d rating=%lf\n"
//	, puser->username, puser->password, puser->alias, puser->lv, puser->rating);

	// guild-patch
	if (puser->gid > 0 && puser->gpos < 9) {
		gid_add_eid(puser->gid, puser->eid);
		guild_t & guild = get_guild_by_gid(puser->gid);
		// set glevel here, so gpos can get memeber_max by glevel
		guild.glevel = glevel;
		// DEBUG_PRINT(0, "out_log:guild.glevel=%d", guild.glevel);
	}

	// TODO re-conn logic  : need test
	// re-conn logic
	g_user_index[conn->euser.eid] = get_conn_id(conn);
	if (conn->room != NULL) {
		// this is possible if we use the same conn to login two times
		conn->room = NULL;
		// BUG_PRINT(-77, "out_login:room_not_null eid=%d", ret);
	}


	conn->room = g_user_room[conn->euser.eid];
	room_t *proom = conn->room;
	int st = ST_LOGIN;

	// setup the right level
	if (NULL != proom) {
		// with room not null, check wheether the game has been started
		if (proom->state==0) {
			BUG_PRINT(-37, "login:room_state_0");
			// shall we report to end user?
		} else {
			printf("re-conn: room state %d\n", proom->state);
			st = proom->state;

			// if is player, reset offline_time
			// more info in auto_fold
			if (proom->guest[0] == puser->eid) {
				proom->offline_time[0] = 0;
			} else if (proom->guest[1] == puser->eid) {
				proom->offline_time[1] = 0;
			}
			
		}
	}
	conn->st = st; // finally, core logic

	if (puser->lv >= g_design->max_level) {
		exp_next = g_design->exp_list[g_design->max_level];
	} else {
		exp_next = g_design->exp_list[puser->lv + 1];
	}
	exp_this = 0;

	if (has_card) {
		// load mission
		ret = dbin_write(conn, "lmis", DB_LOAD_MISSION, IN_LOAD_MISSION_PRINT
		, puser->eid);

		// load hero data
		bzero(hero_buffer, sizeof(hero_buffer));
		hero_ptr = hero_buffer;
		for (int i = 0; i <HERO_MAX; i++) {
			design_hero_t *hero = g_design->hero_list + i + 1;
			hero_ptr += sprintf(hero_ptr, " %d %d", hero->hp, hero->energy);
		}
		ret = dbin_write(conn, "lhero", DB_LOAD_HERO_DATA, IN_LOAD_HERO_DATA_PRINT
		, puser->eid, hero_buffer);
	}
	
	// if st is the first parameter: net_write_space(conn, "%d", st);
	net_writeln(conn, "%s %d %d %s %d %d %d %s %d %d %d %d %d %d %d %d %d %d"
	, cmd, puser->eid, st, puser->alias
	, puser->icon, puser->gid, puser->gpos, puser->gname
	, puser->lv, puser->exp, exp_next, exp_this, LOGIC_VER
	, g_design->version.game_version, g_design->version.client_version
	, has_get_reward, has_card, puser->course);

	return 0;
}


int out_load_card(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int code = -75, n = 0;
	sscanf(buffer, "%d %n", &code, &n); // using n maybe faster
	// new user has no evil_card
	if (code == -6) {
		net_writeln(conn, "%s %s", cmd, buffer);
		if (conn->euser.lv != 1) {
			ERROR_PRINT(code, "out_load_card:%s", buffer);
		}
		return code;
	}
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}

	// result is in lcard 400 format
	ret = sscanf(buffer, "%d %n", &code, &n);
	net_write_space(conn, "%s 400 1", cmd);
//	net_write_space(conn, "%s", cmd); // TODO change to remove 400 1
	net_write(conn, buffer + n, '\n');     // share memory style, need \n
	sprintf(conn->euser.card, "%400s", buffer+n);
//	printf("------ card = [%s]\n", conn->euser.card);

//	int total = 0;
//	total = get_collection(conn->euser.card);
	// DEBUG_PRINT(0, "out_load_card:total=%d", total);
	ret = 0;
	return ret; 
}


int out_load_deck(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int len;
	int code = -75, n = 0;
	int slot;
	char name[EVIL_ALIAS_MAX + 5];
	sscanf(buffer, "%d %n", &code, &n); // using n maybe faster
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}

	// result is in lcard 400 format
	ret = sscanf(buffer, "%d %d %s %n", &code, &slot, name, &n);
	net_write_space(conn, "%s %d %s", cmd, slot, name);
	net_write(conn, buffer+n, '\n');  //  buffer already got \n
	// core logic: cache the deck in memory
	len = sprintf(conn->euser.deck, "%.400s", buffer+n);
	if (len != 400) {
		BUG_PRINT(-7, "out_load_deck:len=%d", len);
	}
	// DEBUG_PRINT(0, "out_load_deck:deck=%.400s", conn->euser.deck);

	ret = 0;
	return ret; 
}


// this is :  job 0 ok  (was job [job_id] [lv])
// TODO it may be appropriate to return "lcard" AND "sta"
int out_save_card(connect_t * conn, const char *cmd, const char *buffer)
{
	int code = -75, n = 0;
	sscanf(buffer, "%d %n", &code, &n); // using n maybe faster
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}

	evil_user_t &euser = conn->euser;
	sprintf(euser.card, "%.400s", buffer + n);
	// DEBUG_PRINT(0, "out_save_card:euser.card = %s", euser.card);

	// load mission
	// int ret = dbin_write(conn, cmd, DB_LOAD_MISSION, IN_LOAD_MISSION_PRINT, euser.eid);

	// TODO update deck_mission here

	net_writeln(conn, "%s %d", cmd, code);

	// assert new reg no need to load mission
	int change = 0;
	change_mission(conn, change);

	char hero_buffer[BUFFER_SIZE+1];
	bzero(hero_buffer, sizeof(hero_buffer));
	char *hero_ptr = hero_buffer;
	for (int i = 0; i <HERO_MAX; i++) {
		design_hero_t *hero = g_design->hero_list + i + 1;
		hero_ptr += sprintf(hero_ptr, " %d %d", hero->hp, hero->energy);
	}
	dbin_write(conn, cmd, DB_LOAD_HERO_DATA, IN_LOAD_HERO_DATA_PRINT
	, euser.eid, hero_buffer);

	return 0;
}


int out_save_deck(connect_t * conn, const char *cmd, const char *buffer)
{
	int eid;
	int slot;
	int ret;
	char card[EVIL_CARD_MAX+5];  // share with error message
	int code = -75, n = 0;  // simple error handling
	sscanf(buffer, "%d %n", &code, &n); // using n maybe faster
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}

	ret = sscanf(buffer, OUT_SAVE_DECK_SCAN, &eid, &slot, card);
	if (ret < 3) {
		NET_ERROR_RETURN(conn, -77, "save_deck:sscanf2 %s", buffer);
	}

	if (get_eid(conn)!=eid) {
		NET_ERROR_RETURN(conn, -87, "save_deck:eid_mismatch %d %d"
			, eid, get_eid(conn));
	}

	ret = check_deck(card, conn->euser.card, eid);
	if (ret < 0) {
		NET_ERROR_RETURN(conn, -97, "save_deck:check_deck %d", ret);
	}

	sprintf(conn->euser.deck, "%.400s", card);
	// TODO update deck_mission here
	int change = 0;
	change |= deck_mission_update(conn);
	change_mission(conn, change); // refresh, mchange

	// simply "sdeck slot deck400"
	net_writeln(conn, "%s %d %.400s", cmd, slot, card);
	return 0; 
}

int out_alias(connect_t * conn, const char *cmd, const char *buffer)
{
	int code = -75, n = 0;  // simple error handling
	int ret;
	int eid;
	char alias[EVIL_ALIAS_MAX+5];

	sscanf(buffer, "%d %n", &code, &n); // using n maybe faster
	if (code < 0) {
		if (code==-26) { // normal: -26 is duplicate alias
			net_writeln(conn, "%s %s", cmd, buffer);
			return code;
		}
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}
	ret = sscanf(buffer, OUT_ALIAS_SCAN, &eid, alias);
	if (ret == 2 && eid==get_eid(conn)) {
		strcpy(conn->euser.alias, alias);
	}

	net_write_space(conn, cmd);
	net_write(conn, buffer, '\n');
	return 0; 
}


int __save_deck_back_to_user(room_t *proom, int eid1, int eid2)
{
	if (proom == NULL) {
		return -3;
	}
	// save the deck back to euser  (eid1, deck[0])
	if (proom->deck[0][0] >= '0' && proom->deck[0][0] <= '9') {
		connect_t * guest_conn = get_conn_by_eid(eid1);
		if (guest_conn != NULL) {
			sprintf(guest_conn->euser.deck, "%.400s", proom->deck[0]);
		}
	}
	// save the deck back to euser (eid2, deck[1])
	if (proom->deck[1][0] >= '0' && proom->deck[1][0] <= '9') {
		connect_t * guest_conn = get_conn_by_eid(eid2);
		if (guest_conn != NULL) {
			sprintf(guest_conn->euser.deck, "%.400s", proom->deck[1]);
		}
	}
	return 0;
}

int __load_deck_from_ai_design(connect_t *conn, const char * cmd
, room_t *proom, int deck_pos, int eid)
{
	int ret;
	if (proom->deck[deck_pos][0] != 'A') { // should be 'AI'
		WARN_PRINT(-57, "out_game:ai_deck_mismatch");
	}
	if (g_design->ai_list[eid].id != eid) {
		ret = -67;
		NET_ERROR_RETURN(conn, ret, "out_game:g_design->ai_list eid=%d id=%d"
		, eid, g_design->ai_list[eid].id);
	}
	sprintf(proom->deck[deck_pos], "%.400s", g_design->ai_list[eid].deck);

	return 0;
}

int __update_match_game_data(room_t *proom, int eid1, int eid2, int *first_player)
{
	match_t & match = get_match(proom->match_id);
	if (match.match_id == 0) {
		BUG_RETURN(-27, "game:match_null match_id=%ld", proom->match_id);
	}

	match_player_t & player1 = get_player_last_round(match, eid1);
	match_player_t & player2 = get_player_last_round(match, eid2);
	if (player1.eid == 0 || player2.eid == 0) {
		BUG_RETURN(-36, "game:player_null eid1=%d eid2=%d"
		, eid1, eid2);
	}
	if (player1.round != player2.round) {
		BUG_RETURN(-46, "game:player_round_mismatch eid1=%d eid2=%d", eid1, eid2);
	}

	// in team round
	if (match.round <= MAX_TEAM_ROUND && player1.tid != player2.tid) {
		BUG_RETURN(-56, "game:team_player_mismatch eid1=%d round=%d tid=%d eid2=%d round=%d tid=%d", eid1, player1.round, player1.tid, eid2, player2.round, player2.tid);
	}
	// in eli round
	if (match.round > MAX_TEAM_ROUND) {
		int max_tid = max(player1.tid, player2.tid);
		int min_tid = min(player1.tid, player2.tid);
		if (max_tid - min_tid != 1 || min_tid % 2 != 0) {
			BUG_RETURN(-66, "game:eli_player_mismatch eid1=%d round=%d tid=%d eid2=%d round=%d tid=%d", eid1, player1.round, player1.tid, eid2, player2.round, player2.tid);
		}
	}

	// get which player play first
	int game_count1 = player1.win + player1.lose + player1.draw;
	int game_count2 = player2.win + player2.lose + player2.draw;
	if (game_count1 != game_count2) { 
		BUG_PRINT(-37, "game:player_game_count_mismatch:%d %d"
		, game_count1, game_count2);
	}
	// set first_player in game:
	// 1. in game1, min(eid).pos is first_player
	// 2. in game2, max(eid).pos is first_player
	// 3. in game3, min(eid).pos is first_player

	// e.g.:
	// eid1 = 1 ,eid2 = 2, game_count = 0: first_player = 0
	// eid1 = 1 ,eid2 = 2, game_count = 1: first_player = 1
	// eid1 = 1 ,eid2 = 2, game_count = 2: first_player = 0

	// eid1 = 2 ,eid2 = 1, game_count = 0: first_player = 1
	// eid1 = 2 ,eid2 = 1, game_count = 1: first_player = 0
	// eid1 = 2 ,eid2 = 1, game_count = 2: first_player = 1
	int game_count = max(game_count1, game_count2);
	// DEBUG_PRINT(0, "game:game_count = %d", game_count);
	game_count += eid1 < eid2 ? 0 : 1;
	*first_player = game_count % 2;
	// DEBUG_PRINT(0, "game:first_player = %d", first_player);
	return 0;
}

int out_game(connect_t * conn, const char *cmd, const char *buffer)
{
	// DEBUG_PRINT(0, "-----------out_game--------");
	// ref: room_game(), cmd_solo()
	int eid1, eid2;
	int ret = -75, n = 0;  // simple error handling
	int first_player = 0;
	char deck0[EVIL_CARD_MAX + 1];
	char deck1[EVIL_CARD_MAX + 1];
	room_t *proom = NULL;
	room_t *proom1;
	room_t *proom2;
	// ranking_pair_t *rankpair;
	if (conn != NULL) {
		proom = conn->room;
	}

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
		goto error_game;
	}


	if (proom==NULL) {
		// ret = -7;
		// NET_ERROR_PRINT(conn, ret, "game:null_room");
		// goto error_game;
		// WARN_PRINT(-6, "out_game:proom_null");
	}

// #define OUT_GAME_SCAN	"%d %d %400s %400s"	//  eid1, eid2, deck1, deck2
	// ret = sscanf(buffer, OUT_GAME_SCAN, &eid1, proom->deck[0], &eid2, proom->deck[1]);
	ret = sscanf(buffer, OUT_GAME_SCAN, &eid1, deck0, &eid2, deck1);
	if (ret != 4) {
		NET_ERROR_PRINT(conn, -55, "game:sscanf %d", ret);
		ret = -55; // order is important
		goto error_game;
	}


	/*
	rankpair = __get_rankpair_in_list(eid1, false);
	if (rankpair == NULL) {
		INFO_PRINT(1, "out_game: rankpair null");
	} else {
		INFO_PRINT(1, "out_game: rankpair id1[%d] id2[%d] status[%d]"
		, rankpair->eid_challenger, rankpair->eid_receiver, rankpair->status);
	}
	*/

	// get room by eid1 , eid2
	proom1 = (eid1 > MAX_AI_EID) ? g_user_room[eid1] : NULL;
	proom2 = (eid2 > MAX_AI_EID) ? g_user_room[eid2] : NULL;

	/*
	if ((rankpair == NULL) || (rankpair != NULL && rankpair->status == RANKING_CHALLENGE_STATUS_ACCEPT))
	{
		proom2 = (eid2 > MAX_AI_EID) ? g_user_room[eid2] : NULL;
	} else {
		proom2 = (eid2 > MAX_AI_EID) ? g_user_room[-eid2] : NULL;
	}
	*/
//	INFO_PRINT(1, "out_game: proom1[%d] proom2[%d] room-2[%d]"
//	, g_user_room[eid1], g_user_room[eid2], g_user_room[-eid2]);
	if (eid1 > MAX_AI_EID && eid2 > MAX_AI_EID && proom1 != proom2) {
		ret = -7;
		NET_ERROR_PRINT(conn, ret, "out_game:room_mismatch %d %d", eid1, eid2);
		goto error_game;
	}

	if (proom1 == NULL && proom2 == NULL) {
		ret = -17;
		NET_ERROR_PRINT(conn, ret, "out_game:two_room_null %d %d", eid1, eid2);
		goto error_game;
	}

	proom = (proom1 != NULL) ? proom1 : proom2;
	sprintf(proom->deck[0], "%.400s", deck0);
	sprintf(proom->deck[1], "%.400s", deck1);
	
	// save the deck back to euser
	__save_deck_back_to_user(proom, eid1, eid2);

	// need to check eid2 is AI, use g_design->ai_list[x]
	if (eid2 > 0 && eid2 <= MAX_AI_EID) {
		ret = __load_deck_from_ai_design(conn, cmd, proom, 1, eid2);
		if (ret < 0) {
			goto error_game;
		}
	}

	// TODO consider to cache the deck

	// TODO game_init() should init: gameid, state, seed
	proom->gameid = get_gameid();
	proom->state = ST_GAME; // let out_game() make it a game

	// [ for match game start
	if (proom->game_type == GAME_MATCH) {
		ret = __update_match_game_data(proom, eid1, eid2, &first_player);
		if (ret < 0) {
			goto error_game;
		}
	}
	// for match game end ]

	// __remove_from_rankpair_list(eid1, false);

	room_set_hero_info(proom, NULL);
	ret = game_init(proom, proom->seed, first_player);
	if (ret < 0) {
		NET_ERROR_PRINT(conn, -66, "game:init %d", ret);
		ret = -66;  // order is important, need to retain 'ret'
		goto error_game;
	}

	// set all guest into ST_GAME,  AI will have null conn
	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i];
		connect_t * guest_conn = get_conn_by_eid(eid);
		if (guest_conn != NULL) {
			guest_conn->st = ST_GAME;
			// printf("---- set eid=%d  to ST_GAME\n", eid);
		} else {
			// printf("---- cannot set eid=%d  to ST_GAME\n", eid);
		}
	}

	// if solo or quick channel, need to send room info
	if (proom->channel == CHANNEL_SOLO || proom->channel==CHANNEL_QUICK
	|| proom->channel == CHANNEL_MATCH) {
		room_info_broadcast(proom, 0); // 0 means all
	} else {
		game_info_broadcast(conn, proom, 0);
	}
	// TODO send game_info for normal game

//  NOTE error case should do:
//	if (ret < 0) {
//		const char *errstr = "-18 subfun_err %d";
//		if (conn!=NULL) { net_writeln(conn, errstr, ret); }
//		ret = free_room(proom->channel, proom);  // order is important
//		return -18;
//	}

	return 0; // normal case

error_game:
	// channel quick and solo: dynamic created room before out_game, so we need
	// to destroy it, for custom room, we leave it no change
	if (proom != NULL && 
	(proom->channel==CHANNEL_QUICK || proom->channel==CHANNEL_SOLO)) {
		// TODO free_room should clean kick all guests and
		// make sure g_user_room[guest_eid] = NULL
		free_room(proom->channel, proom);  // order is important
	}
	return ret; // not yet
}

int out_gate(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int eid;
	int gate_id;
	char deck[EVIL_CARD_MAX+1];
	room_t *proom;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %400s", &eid, &gate_id, deck);
	if (ret != 3) {
		ERROR_RETURN(-55, "out_gate:sscanf %d", ret);
	}

	if (eid != conn->euser.eid) {
		return 0;
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -56, "out_gate:room_not_null");
	}

	proom = new_room(CHANNEL_SOLO);
	if (proom == NULL) {
		NET_ERROR_RETURN(conn, -66, "out_gate:new_room_fail");
	}
	proom->num_guest = 2;
	proom->guest[0] = eid;
	proom->guest[1] = gate_id; // save as gate_id
	proom->rating[0] = conn->euser.rating;
	proom->rating[1] = 1000;
	strcpy(proom->alias[0], conn->euser.alias);
	// strcpy(proom->alias[1], "gate");
	sprintf(proom->alias[1], "gate%d", gate_id);
	proom->icon[0] = conn->euser.icon;
	proom->icon[1] = 1;
	proom->lv[0] = conn->euser.lv;
	proom->lv[1] = 1;
	proom->game_type = GAME_GATE;
	sprintf(proom->title, "%s~VS~%s", proom->alias[0], proom->alias[1]);
	proom->seed = 0;  // set zero and init in gate_init
	conn->room = proom;  // for out_game() 
	g_user_room[eid] = proom;

	bzero(proom->deck[1], EVIL_CARD_MAX);

	strncpy(proom->deck[1], g_design->design_gate_list[gate_id].gate_info, EVIL_CARD_MAX);

	// DEBUG_PRINT(0, "out_gate:proom->deck[1]=%s", proom->deck[1]);

	proom->gameid = get_gameid();
	proom->state = ST_GAME;
	sprintf(proom->deck[0], "%.400s", deck);
	// sprintf(proom->deck[1], "%.400s", pai->deck);
	ret = gate_init(proom, proom->seed, 0);
	// DEBUG_PRINT(0, "out_gate:st=%d", conn->st);
	conn->st = ST_GAME; // set me as GAME state
	room_info_broadcast(proom, 0); // 0 means all

	return 0;
}

int out_tower(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int eid;
	int ai_eid;

	int hero_id;
	int tower_index;
	int hp;
	int hp_current;
	int hp_offset; // update in tower_result
	int res;
	int energy;
	char deck[EVIL_CARD_MAX+1];
	room_t *proom;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %d %d %d %d %400s"
	, &eid, &tower_index, &hp_current, &hp_offset, &res, &energy, deck);
	if (ret != 7) {
		ERROR_RETURN(-55, "out_tower:sscanf %d", ret);
	}

	if (eid != conn->euser.eid) {
		return 0;
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -56, "out_tower:room_not_null");
	}

	hero_id = get_hero(deck);
	if (hero_id < 0 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -76, "out_tower:hero_not_found");
	}
	hp = g_design->hero_hp_list[hero_id];
	DEBUG_PRINT(0, "out_tower:hp=%d", hp);

	// new round
	if (hp_current == 0) {
		hp_current = hp;
	}

	if (hp + hp_offset < hp_current) {
		hp += hp_offset;
	} else {
		hp = hp_current;
	}

	ai_eid = 1; // XXX update ai ;
	ai_t *pai;
	pai = g_design->ai_list + ai_eid;

	proom = new_room(CHANNEL_SOLO);
	if (proom == NULL) {
		NET_ERROR_RETURN(conn, -66, "out_tower:new_room_fail");
	}
	proom->num_guest = 2;
	proom->guest[0] = eid;
	proom->guest[1] = ai_eid; 
	proom->rating[0] = conn->euser.rating;
	proom->rating[1] = pai->rating;
	strcpy(proom->alias[0], conn->euser.alias);
	strcpy(proom->alias[1], pai->alias);
	proom->icon[0] = conn->euser.icon;
	proom->icon[1] = pai->icon;
	proom->lv[0] = conn->euser.lv;
	proom->lv[1] = pai->lv;
	proom->game_type = GAME_TOWER;
	sprintf(proom->title, "%s~VS~%s", proom->alias[0], proom->alias[1]);
	proom->seed = 0;  // set zero and init in gate_init
	conn->room = proom;  // for out_game() 
	g_user_room[eid] = proom;

	sprintf(proom->deck[0], "%.400s", deck);
	sprintf(proom->deck[1], "%.400s", pai->deck);
	DEBUG_PRINT(0, "out_tower:proom->deck[0]=%s", proom->deck[0]);
	DEBUG_PRINT(0, "out_tower:proom->deck[1]=%s", proom->deck[1]);

	proom->tower_pos = tower_index;
	proom->tower_hp = hp;
	proom->tower_res = res;
	proom->tower_energy = energy;

	proom->gameid = get_gameid();
	proom->state = ST_GAME;

	ret = tower_init(proom, proom->seed, hp, res, energy);
	conn->st = ST_GAME; // set me as GAME state
	room_info_broadcast(proom, 0); // 0 means all

	return 0;
}

int out_tower_info(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int eid;
	int tower_pos;
	int tower_times;
	long tower_set_time;
	int pos;
	int hp_current;
	int hp_offset; 
	int res;
	int energy;
	int buff_flag;
	char deck[EVIL_CARD_MAX+1];
	deck[0] = '\0';
	int hero_id;
	int hp;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %d %ld %d %d %d %d %d %d %400s"
	, &eid, &tower_pos, &tower_times, &tower_set_time
	, &pos, &hp_current, &hp_offset, &res, &energy, &buff_flag, deck);
	if (ret != 10 && ret != 11) {
		ERROR_RETURN(-55, "out_tower_info:sscanf %d", ret);
	}

	if (eid != conn->euser.eid) {
		return 0;
	}

	if (deck[0] == '\0') {
		net_writeln(conn, "%s %d %d %d %d %d %d %d %d %d"
		, cmd, eid, tower_pos, tower_times, pos, hp_current, hp_offset
		, res, energy, buff_flag);
		return 0;
	}

	
	hero_id = get_hero(deck);
	if (hero_id < 0 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -76, "out_tower_info:hero_not_found");
	}
	hp = g_design->hero_hp_list[hero_id];
	DEBUG_PRINT(0, "out_tower_info:hp=%d", hp);

	if (hp + hp_offset < hp_current) {
		hp += hp_offset;
	} else {
		hp = hp_current;
	}

	net_writeln(conn, "%s %d %d %d %d %d %d %d %d %d"
	, cmd, eid, tower_pos, tower_times, pos, hp, hp_offset
	, res, energy, buff_flag);

	return 0;
}

int out_tower_buff(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int eid;
	int pos;
	int hp_current;
	int hp_offset; 
	int res;
	int energy;
	int buff_flag;
	char deck[EVIL_CARD_MAX+1];
	deck[0] = '\0';
	int hero_id;
	int hp;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %d %d %d %d %d %400s"
	, &eid, &pos, &hp_current, &hp_offset, &res, &energy, &buff_flag, deck);
	if (ret != 8) {
		ERROR_RETURN(-55, "out_tower_buff:sscanf %d", ret);
	}

	if (eid != conn->euser.eid) {
		return 0;
	}

	hero_id = get_hero(deck);
	if (hero_id < 0 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -76, "out_tower_buff:hero_not_found");
	}
	hp = g_design->hero_hp_list[hero_id];
	DEBUG_PRINT(0, "out_tower_buff:hp=%d", hp);

	if (hp + hp_offset < hp_current) {
		hp += hp_offset;
	} else {
		hp = hp_current;
	}

	net_writeln(conn, "%s %d %d %d %d %d %d %d"
	, cmd, eid, pos, hp, hp_offset
	, res, energy, buff_flag);

	return 0;
}

int out_tower_result(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		BUG_RETURN(ret, "%s", buffer+n);
	}
	return 0;
}

int out_tower_ladder(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n = 0;
	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_write_space(conn, cmd);
	net_write(conn, buffer, '\n');
	return 0; 
}

room_t * create_solo_plus_room(connect_t *conn, solo_t *solo, int game_type)
{
	room_t *proom = NULL;
	proom = new_room(CHANNEL_SOLO);

	if (proom == NULL) {
		ERROR_PRINT(-6, "init_solo_plus_room:room_null");
		return NULL;
	}

	if (conn->euser.eid == 0) {
		ERROR_PRINT(-16, "init_solo_plus_room:not_login");
		return NULL;
	}

	if (conn->room != NULL) {
		ERROR_PRINT(-26, "init_solo_plus_room:already_has_room");
		return NULL;
	}

	if (solo->id <= 0) {
		ERROR_PRINT(-36, "init_solo_plus_room:solo_null");
		return NULL;
	}

	int my_hero = 0;
	int solo_hero = 0;
	sscanf(solo->deck2, "%d", &solo_hero);

	proom->num_guest = 2;
	proom->guest[0] = conn->euser.eid;
	proom->guest[1] = solo->id;
	proom->rating[0] = conn->euser.rating;
	proom->rating[1] = 1000;
	strcpy(proom->alias[0], conn->euser.alias);
	strcpy(proom->alias[1], solo->alias);
	proom->icon[0] = conn->euser.icon;
	proom->icon[1] = solo_hero;
	proom->lv[0] = conn->euser.lv;
	proom->lv[1] = 1;
	sprintf(proom->title, "%s~VS~%s", proom->alias[0], proom->alias[1]);
	proom->seed = 0;

	proom->game_type = game_type;
	proom->gameid = get_gameid();
	proom->state = ST_GAME;

	// get player deck and hero, use real deck or use solo->my_deck
	sscanf(solo->deck1, "%d", &my_hero);
	if (my_hero == 0) {
		// use real deck
		sprintf(proom->deck[0], "%.400s", conn->euser.deck);
		my_hero = get_hero(proom->deck[0]);
	} else {
		// use solo my_deck
		sprintf(proom->deck[0], "%.400s", solo->deck1);
	}
	if (my_hero <= 0 || my_hero > HERO_MAX) {
		BUG_PRINT(-66, "init_solo_plus_room:my_hero_null");
		return NULL;
	}
	sprintf(proom->deck[1], "%.400s", solo->deck2);

	// reset both hero hp, energy in room
	room_set_hero_info(proom, solo);
	INFO_PRINT(0, "init_solo_plus_room:hero1 hp[%d] energy[%d], hero2 hp[%d] energy[%d]"
	, proom->hp1, proom->energy1, proom->hp2, proom->energy2);

	/*
	// get my hero hp and energy
	evil_hero_data_t &hero_data = conn->euser.hero_data_list[my_hero];
	int hp = solo->hp1;
	int energy = 99;

	// use player's hero
	if (hp == 0) {
		hp = hero_data.hero.hp;
		energy = hero_data.hero.energy;

		if (hero_data.hero.hero_id == 0) {
			// player has no hero in design.design_solo.my_hero
			WARN_PRINT(-7, "init_solo_plus_room:hero_data_error hero_id %d", my_hero);
			design_hero_t &dhero = g_design->hero_list[my_hero];
			if (dhero.hero_id == 0) {
				BUG_PRINT(-17, "init_solo_plus_room:no_such_design_hero hero_id %d"
				, my_hero);
			}
			hp = dhero.hp;
			energy = dhero.energy;
		}
	} 

	proom->hp1 = hp;
	proom->hp2 = solo->hp2;
	proom->energy1 = energy;
	*/


	proom->game_flag = solo->solo_type;
	proom->ai_max_ally = solo->ai_max_ally;

	proom->solo_start_side = solo->start_side;
	strcpy(proom->type_list, solo->type_list);

	conn->room = proom;  // for out_game() 
	// eid2 is always AI, so we don't need to assign to g_user_room[eid]
	g_user_room[conn->euser.eid] = proom;

	return proom;
}

int out_solo_plus(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int len;
	int n;
	int eid1;
	int solo_id;
	char deck[EVIL_CARD_MAX+1];
	room_t *proom;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %400s", &eid1, &solo_id, deck);
	if (ret != 3) {
		ERROR_RETURN(-55, "out_solo_plus:sscanf %d", ret);
	}

	if (eid1 != conn->euser.eid) {
		return 0;
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -56, "out_solo_plus:room_not_null");
	}

	solo_t * solo = get_design_solo(solo_id);

	if (solo == NULL) {
		NET_ERROR_RETURN(conn, -5, "out_solo_plus:solo_id_invalid %d", solo_id);
	}

	len = sprintf(conn->euser.deck, "%.400s", deck);
	if (len != 400) {
		BUG_PRINT(-7, "out_solo_plus:deck_len=%d", len);
	}


	proom = create_solo_plus_room(conn, solo, GAME_SOLO_PLUS);
	if (proom == NULL) {
		BUG_PRINT(-66, "out_solo_plus:create_solo_plus_room_fail %d", solo_id);
		NET_ERROR_RETURN(conn, -66, "out_solo_plus:create_solo_plus_room_fail %d", solo_id);
	}

	ret = solo_plus_init(proom, proom->seed);
	if (ret != 0) {
		force_room_clean(proom);
		NET_ERROR_RETURN(conn, -76, "out_solo_plus:solo_plus_init %d %d", solo_id, conn->euser.eid);
	}

	conn->st = ST_GAME; // set me as GAME state
	room_info_broadcast(proom, 0); // 0 means all
	return ST_GAME;

	/*
	// old logic
	
	proom = new_room(CHANNEL_SOLO);
	proom->num_guest = 2;
	proom->guest[0] = eid1;
	proom->guest[1] = solo->id;
	proom->rating[0] = conn->euser.rating;
	proom->rating[1] = 1000;
	strcpy(proom->alias[0], conn->euser.alias);
	strcpy(proom->alias[1], solo->alias);
	proom->icon[0] = conn->euser.icon;
	proom->icon[1] = 0;
	int hero_id = 0;
	sscanf(solo->deck, "%d", &hero_id);
	proom->icon[1] = hero_id;
	proom->lv[0] = conn->euser.lv;
	proom->lv[1] = 1;
	proom->game_type = GAME_SOLO_PLUS;
	sprintf(proom->title, "%s~VS~%s", proom->alias[0], proom->alias[1]);

	// fixed random seed (if >0)
	proom->seed = 0;
	conn->room = proom;  // for out_game() 
	// re-conn logic ?  
	g_user_room[eid1] = proom;
	// eid2 is always AI, so we don't need to assign to g_user_room[eid]

	proom->gameid = get_gameid();
	proom->state = ST_GAME;
	int my_hero = 0;
	sscanf(solo->my_deck, "%d", &my_hero);
	if (my_hero == 0) {
		// use real deck
		sprintf(proom->deck[0], "%.400s", deck);
		my_hero = get_hero(proom->deck[0]);
	} else {
		// use solo my_deck
		sprintf(proom->deck[0], "%.400s", solo->my_deck);
	}

	evil_hero_data_t &hero_data = conn->euser.hero_data_list[my_hero];
	int hp = hero_data.hero.hp;
	int energy = hero_data.hero.energy;
	if (hero_data.hero.hero_id == 0) {
		// player has no hero in design.design_solo.my_hero
		WARN_PRINT(-7, "out_solo_plus:hero_data_error hero_id %d", my_hero);
		design_hero_t &dhero = g_design->hero_list[my_hero];
		if (dhero.hero_id == 0) {
			BUG_PRINT(-17, "out_solo_plus:no_such_design_hero hero_id %d"
			, my_hero);
		}
		hp = dhero.hp;
		energy = dhero.energy;
	}


	sprintf(proom->deck[1], "%.400s", solo->deck);
	proom->solo_type = solo->solo_type;
	proom->ai_max_ally = solo->ai_max_ally;
	proom->hp2 = solo->max_hp;
	proom->solo_hero = hero_id;
	proom->hp1 = hp;
	proom->energy1 = energy;
	proom->solo_start_side = solo->start_side;
	strcpy(proom->type_list, solo->type_list);


	ret = solo_plus_init(proom, proom->seed);
	conn->st = ST_GAME; // set me as GAME state
	room_info_broadcast(proom, 0); // 0 means all
	return ST_GAME;
	*/

}


int out_update_solo_pos(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		BUG_RETURN(ret, "%s", buffer+n);
	}
	return 0;
}

int out_win(connect_t * conn, const char *cmd, const char *buffer)
{
	// INFO_PRINT(0, "out_win:start");
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	// do not write anything to client, silent output when normal
	
	// TODO need to update the local copy of conn->euser
	// ref: win_game(), we may return "sta" directly
	return 0; // not yet
}

int out_save_replay(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n = 0;
	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	// for normal case, it send nothing to client!
	/**
	net_write_space(conn, cmd);
	net_write(conn, buffer, '\n');
	**/
	return 0; 
}

// it is like fill up the deck and go straight to quick game logic
int out_quick(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n = 0;  // simple error handling
	const char *ptr;
	int slot;
	char name[EVIL_ALIAS_MAX+5];
	sscanf(buffer, "%d %n", &ret, &n); // no need to catch ret=sscanf
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	// for code >= 0 : yes we have load the card, do quick_game
	// also n is ready by above sscanf

	// get slot, it is useless here
	ptr = buffer + n;
	ret = sscanf(ptr, "%d %s %n", &slot, name, &n);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -65, "out_quick:sscanf %d", ret);
	}
	ptr += n;
	// core logic: cache the deck in memory
	sprintf(conn->euser.deck, "%.400s", ptr); //buffer+n);

	// do check_deck in quick_game()
	/*
	ret = check_deck(conn->euser.deck, conn->euser.card, conn->euser.eid);
	if (ret < 0 ) {
		// this is rather buggy
		NET_ERROR_RETURN(conn, -55, "out_quick:check_deck=%d", ret);
	}
	*/

	return quick_game(conn, cmd, "");
}

// just print the buffer
int out_save_debug(connect_t * conn, const char *cmd, const char *buffer)
{
	int code = -75, n = 0;
	int ret;
	ret = sscanf(buffer, "%d %n", &code, &n); // using n maybe faster
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}

	net_write_space(conn, cmd);
	net_write(conn, buffer, '\n');

	ret = 0;
	return ret; 
}

int out_load_debug(connect_t * conn, const char *cmd, const char *buffer)
{
	int code = -75, n = 0;
	int ret;
	ret = sscanf(buffer, "%d %n", &code, &n); // using n maybe faster
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}

	net_write_space(conn, cmd);
	net_write(conn, buffer, '\n');
	ret = 0;
	return ret;
}

int out_status(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int code = -1;
	int exp_next;
	int exp_this;
	char msg[100] = {'\0'};
	int unread_count;
	double power;
	long power_set_time;

	evil_user_t *puser = &conn->euser;
	ret = sscanf(buffer, "%d %80[^\n]", &code, msg);
	if (ret < 2) {
		NET_ERROR_RETURN(conn, -75, "sscanf %d", ret);
	}
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", msg);
	}

	// DEBUG_PRINT(0, "out_status:buffer=%s", buffer);

	// TODO check user
	// donnot set icon, use memory user's icon
	// STATUS_ADD_FIELD
	ret = sscanf(buffer, OUT_STATUS_SCAN
	, &puser->eid, &puser->lv, &puser->rating, &puser->gold, &puser->crystal
	, &puser->gid, &puser->gpos, &puser->game_count, &puser->game_win
	, &puser->game_lose, &puser->game_draw
	, &puser->game_run, &puser->exp, &puser->sex
	, &puser->fight_ai_time, &puser->fight_gold_time
	, &puser->fight_crystal_time, puser->signals
	, &puser->monthly_end_date, puser->signature, &unread_count
	, &power, &power_set_time, &puser->gate_pos, &puser->chapter_pos
	, &puser->course);

	if (ret != 26) {
		NET_ERROR_RETURN(conn, -95, "status:out_sscanf %d", ret);
	}

	if (puser->lv >= g_design->max_level) {
		exp_next = g_design->exp_list[g_design->max_level];
	} else {
		exp_next = g_design->exp_list[puser->lv + 1];
	}
	exp_this = 0;

	int card_collection = 0;
	card_collection = get_collection(conn->euser.card);

	net_writeln(conn, "%s %d %d %.2lf %d %d %d %d %d %d %d %d %s %d %d %s %d %d %d %d %s %d %.2lf %ld %ld %d %d %d", cmd
	, puser->eid, puser->lv, puser->rating, puser->gold, puser->crystal 
	, puser->game_count, puser->game_win, puser->game_lose
	, puser->game_draw, puser->game_run, puser->icon
	, puser->alias, puser->gid, puser->gpos
	, puser->gname, puser->exp, exp_next, exp_this
	, puser->sex, puser->signature, unread_count, power, power_set_time
	, puser->monthly_end_date, puser->chapter_pos, puser->course
	, card_collection);
	
	return 0;
}

int out_buy_card(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int code = -1;
	char msg[100] = {'\0'};
	ret = sscanf(buffer, "%d %80[^\n]", &code, msg);
	if (ret < 2) {
		NET_ERROR_RETURN(conn, -75, "sscanf %d", ret);
	}
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", msg);
	}

	int eid;
	int card_id;
	int card_type;
	int money_type;
	int buy_count;
	int gold;
	int crystal;
	int change;


	ret = sscanf(buffer, OUT_BUY_CARD_SCAN, &eid, &card_id, &card_type
	, &money_type, &buy_count, &gold, &crystal);

	if (ret != 7) {
		NET_ERROR_RETURN(conn, -95, "out_buy_card:out_sscanf %d", ret);
	}

	if (conn->euser.eid != eid) {
		return 0;
	}

	if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
		NET_ERROR_RETURN(conn, -7, "out_buy_card:card_id %d", card_id);
	}

	// TODO need to update card?
	conn->euser.gold += gold;
	conn->euser.crystal += crystal;

	// buy a piece
	if (card_type == BUY_TYPE_PIECE) {
		net_writeln(conn, "%s %d %d %d %d %d %d %d", cmd, eid, card_id
		, card_type, money_type, buy_count, gold, crystal);
	}

	add_card(conn->euser, card_id, buy_count);

	// update MISSION_SHOP
	change = 0;
	change |= shop_mission_update(conn, buy_count, card_id);

	// update MISSION_CARD
	int cc = 0;
	cc = conn->euser.card[card_id - 1] - '0';
	// DEBUG_PRINT(0, "out_buy_card:cc=%d", cc);
	change |= card_mission_update(conn, cc, card_id);

	// update MISSION_COLLECTION
	int total = 0;
	total = get_collection(conn->euser.card);
	// DEBUG_PRINT(0, "out_buy_card:total=%d", total);
	change |= collection_mission_update(conn, total);
	change_mission(conn, change); // refresh, mchange


	//DEBUG_PRINT(0, "out_buy_card:euser->gold=%d, euser->crystal=%d", conn->euser.gold, conn->euser.crystal);

	net_writeln(conn, "%s %d %d %d %d %d %d %d", cmd, eid, card_id
	, card_type, money_type, buy_count, gold, crystal);

	const card_t *pcard = get_card_by_id(card_id);
	if (pcard == NULL) {return 0;}
	// DEBUG_PRINT(0, "get_card_by_id:%d %d %d %d %s", pcard->id, pcard->star, pcard->job, pcard->cost, pcard->name);
	if (pcard->star >= 4) {
		sys_wchat(1, SYS_WCHAT_GET_CARD, conn->euser.alias, pcard->star, pcard->name);
	}
	return 0; 
}

// XXX need to clear user.deck[0] = null
int out_sell_card(connect_t * conn, const char *cmd, const char *buffer)
{
	/*
	net_write_space(conn, cmd);
	net_write(conn, buffer, '\0');
	*/

	int ret;
	int code = -1;
	char msg[100] = {'\0'};
	ret = sscanf(buffer, "%d %80[^\n]", &code, msg);
	if (ret < 2) {
		NET_ERROR_RETURN(conn, -75, "sscanf %d", ret);
	}
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", msg);
	}

	int eid;
	int card_id;
	int card_type;
	int money_type;
	int sell_count;
	int gold;
	int crystal;

	ret = sscanf(buffer, OUT_SELL_CARD_SCAN, &eid, &card_id
	, &card_type, &money_type, &sell_count, &gold, &crystal);

	if (ret != 7) {
		NET_ERROR_RETURN(conn, -95, "out_sell_card:out_sscanf %d", ret);
	}

	if (conn->euser.eid != eid) {
		return 0;
	}

	char * deck = conn->euser.deck;
	deck[0] = '\0'; 

	// TODO need to update card?  
	// TODO need to update deck?
	conn->euser.gold += gold;
	conn->euser.crystal += crystal;
	add_card(conn->euser, card_id, -sell_count);  // negative means del

DEBUG_PRINT(0, "out_sell_card:euser->gold=%d, euser->crystal=%d", conn->euser.gold, conn->euser.crystal); 
	// net_write_space(conn, cmd);
	// net_write(conn, buffer, '\0');
	net_writeln(conn, "%s %d %d %d %d %d %d %d"
	, cmd, eid, card_id, card_type, money_type, sell_count, gold, crystal);
	return 0; 
}

int get_random_loc(pick_t *pick, int * loc)
{
	int ret;
	int sum;
	int r;
	int rate;

	if (pick == NULL) {
		ret = -3;
		BUG_RETURN(ret, "get_random_loc:pick_null");
	}

	
	sum = 0;
	for (int l=0; l<MAX_LOC; l++) {
		sum += pick->pick_rate[l];	
	}
	

	r = (abs(random()) % sum) + 1;
	DEBUG_PRINT(0, "get_random_loc:sum=%d r=%d", sum, r);

	rate = 0;
	for (int l=0; l<MAX_LOC; l++) {
		rate += pick->pick_rate[l];
		if (r <= rate) {
			*loc = l;
			return 0;
		}
	}

	BUG_RETURN(-6, "get_random_loc:no_loc");

	return ret;
}

int get_random_batch(pick_t * pick, int *star_list, int *card_list) 
{
	int ret;
	int r;
	int sum;
	int *star_level;
	int *card_id;

	if (pick == NULL) {
		ret = -3;
		BUG_RETURN(ret, "get_random_batch:pick_null");
	}
			
	// prepare star_list  e.g.:
	// star1 star2 star3 star4 star5
	// 3     2     0     2     3          	sum = 10
	// 1-3   4-6   X     7-8   8-10			range
	// random => 6  -> fit into 4-6 : star2
	// random => 9  -> fit into 8-10 : star5
	for (int loc=0; loc<MAX_LOC; loc++) {
		star_level = star_list + loc;
		sum = 0;
		for (int star=0; star<MAX_STAR; star++) {
			sum += pick->batch_rate[loc][star];
		}

		r = (abs(random()) % sum) + 1;
		DEBUG_PRINT(0, "get_random_batch:r=%d", r);
		int rate = 0;
		for (int star=0; star<MAX_STAR; star++) {
			rate += pick->batch_rate[loc][star];
			DEBUG_PRINT(0, "get_random_batch:batach_rate[%d][%d]=%d, rate=%d"
			, loc, star, pick->batch_rate[loc][star], rate);
			if (r <= rate) {
				*star_level = star;
				break;
			}
		}
	}

	// prepare card_list  (according to star_list)
	int size;
	for (int loc=0; loc<MAX_LOC; loc++) {
		card_id = card_list + loc;
		int star;
		card_t card;
		star = star_list[loc];
		size = g_design->extract_list[star].size; // .size() maybe in size_t (int)
		r = (abs(random()) % size);  // r = 0 to size-1
		card = g_design->extract_list[star].card_list[r];
		if (card.id == 0) {
			ret = -7;
			BUG_RETURN(ret, "get_random_batch:card_null");
		}
		if (card.star != star+1) { // star + 1 (base0),  card.star is base1 
			ret = -17;
			BUG_RETURN(ret, "get_random_batch:star_mismatch %d %d", card.star, star+1);
		}
			
		DEBUG_PRINT(0, "card.id=%d, card.name=%s, card.star=%d"
			, card.id, card.name, card.star);

		*card_id = card.id;  // core logic
	}

	// debug print
	printf("------ star_list -------\n");
	for (int loc=0; loc<MAX_LOC; loc++) {
		printf("%d\t", *(star_list+loc));
	}
	printf("\n");
	printf("------ card_list -------\n");
	for (int loc=0; loc<MAX_LOC; loc++) {
		printf("%d\t", *(card_list+loc));
	}
	printf("\n");

	return 0;  // must be 0,  for NET_ERROR_RETURN
}


// net_write the batch list
int write_batch(connect_t *conn, const char *cmd, int batch_type, int * card_list)
{
	int ret;
	int refresh;
	// XXX write euser batch_list here?
	if (conn->euser.eid <= 0) {
		ret = -3;
		BUG_PRINT(ret, "write_batch:euser_null eid=%d", get_eid(conn));
		NET_ERROR_RETURN(conn, ret, "write_batch:euser_null");
	}

	for (int loc=0; loc<MAX_LOC; loc++) {
		conn->euser.batch_list[batch_type][loc] = card_list[loc];
	}

	// debug
	printf("------- write_batch -------\ncard_list:\t");
	for (int loc=0; loc<MAX_LOC; loc++) {
		printf("%d\t", conn->euser.batch_list[batch_type][loc]);
	}
	printf("\n");
	//


	// output new generate random batch
	refresh = 0;
	net_write_space(conn, "%s %d %d %d %d", cmd, batch_type, refresh, 0, 0);
	for (int i=0; i<MAX_LOC; i++) {
		net_write_space(conn, "%d", card_list[i]);
	}
	net_write(conn, "", '\n');
	return 0;
}

int out_batch(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int batch_type;
	int card_list[MAX_LOC];
	bzero(card_list, sizeof(card_list));
	int code = -1;
	const char *ptr;
	int n = 0;
	ret = sscanf(buffer, "%d %n", &code, &n);
	if (ret <= 0) {
		NET_ERROR_RETURN(conn, -75, "sscanf %d", ret);
	}

	ptr = buffer + n;
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", ptr);
	}

	DEBUG_PRINT(0, "out_batch:buffer=%s", buffer);
	if (code == 0) { // TODO no batch, need to make a random batch and save
		ret = 0;  // ptr contains ptype
		batch_type = 0; //default 0
		sscanf(ptr, "%d", &batch_type);
		if (batch_type < 0 || batch_type > MAX_PICK) {
			BUG_PRINT(-7, "out_batch:batch_type_invalid %d", batch_type);
			batch_type = 0;
		}
		int star_list[MAX_LOC];
		int card_list[MAX_LOC];
		pick_t *pick;
		pick = g_design->pick_list+batch_type;
		ret = get_random_batch(pick, star_list, card_list);
		NET_ERROR_RETURN(conn, ret, "out_batch:random");

		// TODO get_eid is not safe
		ret = dbin_save_batch(conn, get_eid(conn), batch_type, card_list, 0, 0); // db_flag = 1 is useless here

		// output new generate random batch
		write_batch(conn, cmd, batch_type, card_list);
		return ret;
	}

	eid = code; // code is overloaded with eid
	ret = sscanf(ptr, "%d %n", &batch_type, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -95, "out_batch:out_sscanf %d", ret);
	}

	printf("----- batch_type = %d\n", batch_type);

	// since eid is mismtach, only print err in server, not report to client
	if (get_eid(conn) != eid) {
		WARN_PRINT(-9, "out_batch:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}
		

	ptr += n;
	for (int loc = 0; loc < MAX_LOC; loc++) {
		ret = sscanf(ptr, "%d %n", card_list+loc, &n);
		if (ret <= 0) {
			NET_ERROR_RETURN(conn, -17, "out_batch:out_batch:sscanf %d", loc);
		}
		ptr += n;
	}
		
	// debug print
	printf("------ out_batch -------\n");
	for (int loc = 0; loc < MAX_LOC; loc++) {
		printf("card_list[%d]=%d\t", loc, card_list[loc]);
	}
	printf("\n");


	// output batch from database
	write_batch(conn, cmd, batch_type, card_list);

	return ret;
}

int out_save_batch(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	DEBUG_PRINT(0, "GOOD:out_save_batch %s", buffer);

	// do not write anything to client, silent output when normal
	return 0; 
}

int out_pick(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int batch_type;
	int card_id;
	int loc;
	int gold;
	int crystal;
	int code = -1;
	const char *ptr;
	int n = 0;
	ret = sscanf(buffer, "%d %n", &code, &n);
	if (ret <= 0) {
		NET_ERROR_RETURN(conn, -75, "sscanf %d", ret);
	}
	ptr = buffer + n;

	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", ptr);
	}

	if (code == 99) {
		DEBUG_PRINT(ret, "out_pick:card_count=9");
	}

	DEBUG_PRINT(0, "out_pick:buffer=%s", buffer);

	ptr = buffer + n;
	ret = sscanf(ptr, OUT_PICK_SCAN, &eid, &batch_type
	, &loc, &card_id, &gold, &crystal);

	// since eid is mismtach, only print err in server, not report to client
	if (get_eid(conn) != eid) {
		WARN_PRINT(-9, "out_pick:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}

	conn->euser.gold += gold;
	conn->euser.crystal += crystal;

	net_writeln(conn, "%s %d %d %d %d %d %d %d", cmd, code, eid
	, batch_type, loc, card_id, gold, crystal);

	return ret;
}

int out_xcadd(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	char * deck = conn->euser.deck;
	deck[0] = '\0'; 

	net_writeln(conn, "%s 0 OK", cmd);
	return 0;
}


// report error and update gold/crystal?
// TODO send stat for seller
int out_xcbuy(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int buyer_eid, seller_eid;
	int gold, crystal;
	int cardid;
	int count;
	connect_t *buyer_conn;
	connect_t *seller_conn;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer+n, OUT_BUY_EXCHANGE_SCAN, &buyer_eid
	, &seller_eid, &cardid, &count, &gold, &crystal);
	if (ret != 6) {
		BUG_PRINT(-7, "out_xcbuy:sscanf OUT_BUY_EXCHANGE_SCAN %d", ret);
		net_writeln(conn, "%s %d %s", cmd, -7, "unknown_error_sscanf");
		return -7;
	}

	buyer_conn = get_conn_by_eid(buyer_eid);
	if (buyer_conn != NULL) {
		buyer_conn->euser.gold -= gold;
		buyer_conn->euser.crystal -= crystal;
		// this is usually conn
		net_writeln(buyer_conn, "%s %d %s", cmd, 0, buffer+n);
	}

	seller_conn = get_conn_by_eid(seller_eid);
	if (seller_conn != NULL) {
		seller_conn->euser.gold += gold;
		seller_conn->euser.crystal += crystal;
		// TODO seller: send status ?  or xcsell ?
		net_writeln(seller_conn, "%s %d %s", cmd, 0, buffer+n);
	}

	// above already feedback to client, do not net_writeln here
	return 0;
}

int out_xclist(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_xcreset(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_list_guild(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_create_guild(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int gid = 0;
	int gold;
	int crystal;
	char gname[EVIL_ALIAS_MAX + 1] = "_";
	int change;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	DEBUG_PRINT(0, "out_create_guild:buffer=%s", buffer);

	ret = sscanf(buffer, OUT_CREATE_GUILD_SCAN, &gid, &gold, &crystal, gname);
	if (ret != 4) {
		WARN_PRINT(-95, "out_create_guild:sscanf %d", ret);
	}
	if (conn->euser.eid == gid) {
		conn->euser.gold += gold;
		conn->euser.crystal += crystal;
		conn->euser.gid = gid; 
		conn->euser.gpos = GUILD_POS_MASTER; // assume master create
		strcpy(conn->euser.gname, gname);

		gid_add_eid(gid, gid); // gid==eid guild-patch

		// update MISSION_GUILD
		change = 0;
		change |= guild_mission_update(conn);
		change_mission(conn, change); // refresh, mchange
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_delete_guild(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	// now, we only handle master gid, later, we may handle all online user
	if (conn->euser.gid == ret) {
		guild_clear(ret);
		conn->euser.gid = 0;
		conn->euser.gpos = 0;
		strcpy(conn->euser.gname, "_no_guild");
	}

	// send in guild_clear()
	// net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_glist(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int flag;
	int start_id;
	int total;
	int eid;
	int lv;
	int gpos;
	char alias[EVIL_ALIAS_MAX+5];
	int icon;
	double rating;
	time_t last_login;
	double gshare;
	const char *ptr;
	char out_buffer[DB_BUFFER_MAX + 1];
	char *out_ptr;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, OUT_LIST_GMEMBER_SCAN, &flag, &start_id, &total, &n);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -87, "scanf:%s", buffer);
	}

	out_buffer[0] = '\0';
	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, "%d %d %d", flag, start_id, total);

	ptr = buffer;
	for (int i=0; i<total; i++) {
		ptr += n;
		ret = sscanf(ptr, OUT_LIST_GMEMBER_ROW_SCAN, &eid, &gpos, alias, &icon, &rating, &last_login, &gshare, &lv, &n);
		if (ret != 8) {
			NET_ERROR_RETURN(conn, -97, "scanf2: %d %d %s", i, ret, ptr);
		}
		// online check:  (set last_login == 0)
		if (get_conn_by_eid(eid) != NULL) {
			last_login = 0;
		}
		out_ptr += sprintf(out_ptr, " %d %d %s %d %lf %ld %lf %d", eid, gpos, alias, icon, rating
		, last_login, gshare, lv);
	}

	net_writeln(conn, "%s %s", cmd, out_buffer);

	return 0;
}

int out_gapply(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	// output:
	int eid, gid, gpos;
	char gname[EVIL_ALIAS_MAX+5];

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, OUT_GUILD_APPLY_SCAN, &eid, &gid, &gpos, gname);
	if (ret != 4) {
		WARN_PRINT(-95, "out_gapply:sscanf %d", ret);
	} else {
		if (eid == get_eid(conn)) {
			conn->euser.gid = gid;
			conn->euser.gpos = gpos;
			strcpy(conn->euser.gname, gname);
			// do not do: gid_add_eid()
		}
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_gpos(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	// output:
	int eid, gid, gpos;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	// either from APPLY to MEMBER/SENIOR, or MEMBER/SENIOR to SENIOR/MEMBER
	ret = sscanf(buffer, OUT_GUILD_POS_SCAN, &eid, &gpos, &gid);
	if (ret != 3) {
		WARN_PRINT(-95, "out_gpos:sscanf %d", ret);
	} else {
		// change the target eid connect, which is usually not my conn
		connect_t * guest_conn = get_conn_by_eid(eid);
		if (guest_conn != NULL) {
			guest_conn->euser.gid = gid;
			guest_conn->euser.gpos = gpos;
			gid_add_eid(gid, eid); // guild-patch
			// signal the member
			if (guest_conn != conn) {
				net_writeln(guest_conn, "%s %s", cmd, buffer);
			}

			// update MISSION_GUILD
			int change;
			change = 0;
			change |= guild_mission_update(guest_conn);
			change_mission(guest_conn, change); // refresh, mchange
		}
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_gquit(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	// output:
	int eid;
	int gid;
//	char gname[EVIL_ALIAS_MAX+5];

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, OUT_GUILD_QUIT_SCAN, &eid, &gid);
	if (ret != 2) {
		WARN_PRINT(-95, "out_gquit:sscanf %d", ret);
	} else {
		connect_t * guest_conn = get_conn_by_eid(eid);
		if (guest_conn != NULL) {
			if (guest_conn->euser.gpos != GUILD_POS_APPLY) {
				gid_del_eid(gid, eid); // guild-patch
			}
			guest_conn->euser.gid = 0;
			guest_conn->euser.gpos = 0;
			strcpy(guest_conn->euser.gname, "_no_guild");

			// signal the client
			if (guest_conn != conn) {
				net_writeln(guest_conn, "%s %s", cmd, buffer);
			}
		}
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_gdeposit(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	// out:
	int eid, gid, gold;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}


	ret = sscanf(buffer, OUT_GUILD_DEPOSIT_SCAN, &eid, &gid, &gold);
	if (ret != 3) {
		WARN_PRINT(-95, "out_gdeposit:sscanf %d", ret);
	} else {
		// logic: update memory euser status
		if (get_eid(conn) == eid) {
			conn->euser.gold -= gold;
		}
	}

	net_writeln(conn, "%s %s", cmd, buffer); 
	return 0;
}

int out_gbonus(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	// out:
	int eid, guild_gold, bonus_gold;
	int get_flag;
	double rate;
	double gshare;
	long last_bonus_time;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

// eid, get_flag, guild_gold, rate, gshare, bonus_gold, last_bonus_time(unix)
	ret = sscanf(buffer, OUT_GUILD_BONUS_SCAN, &eid, &get_flag, &guild_gold, &rate
	, &gshare, &bonus_gold, &last_bonus_time);
	if (ret != 7) {
		WARN_PRINT(-95, "out_gbonus:sscanf %d", ret);
	} else {
		// logic: update memory euser status
		if (get_flag==1 && get_eid(conn) == eid) {
			conn->euser.gold += bonus_gold;
		}
	}

	net_writeln(conn, "%s %s", cmd, buffer); 
	return 0;
}


int out_ldeposit(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	// out:

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer); 
	return 0;
}


int out_deposit(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	// out:

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer); 
	return 0;
}

int out_glv(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	// out:
	int gid, glevel, gold; // current gold
	design_guild_t * gcurrent, *gnext;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	// gid, glevel, gold
	ret = sscanf(buffer, OUT_GLV_SCAN, &gid, &glevel, &gold);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -77, "out_glv:sscanf");
	}


	gcurrent = g_design->guild_list + glevel;
	if (glevel >= g_design->guild_max_level) {
		gnext = g_design->guild_list + glevel;
	} else {
		gnext = g_design->guild_list + (glevel + 1);
	}

	// RET: glv gid current_level current_gold current_member_max current_consume_gold next_level levelup_gold next_member_max next_consume_gold
	net_writeln(conn, "%s %d %d %d %d %d %d %d %d %d", cmd, gid
	, glevel, gold, gcurrent->member_max, gcurrent->consume_gold
	, gnext->lv, gcurrent->levelup_gold, gnext->member_max, gnext->consume_gold
	);

	guild_t & guild = get_guild_by_gid(gid);
	if (guild.gid!=gid) { 
		BUG_PRINT(-73, "out_glv:guild_not_found %d", gid);
	} else {
		guild.glevel = glevel;
		guild.gold = gold;
	}
	return 0;
}

int out_glevelup(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	// out:
	int gid, glevel, gold; // current gold
	design_guild_t * gcurrent;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	// gid, glevel, gold
	ret = sscanf(buffer, OUT_GLEVELUP_SCAN, &gid, &glevel, &gold);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -77, "out_glevelup:sscanf");
	}

	// assert: gold<0

	gcurrent = g_design->guild_list + glevel;

	// RET: glevelup gid new_level -gold new_member_max new_consume_gold
	net_writeln(conn, "%s %d %d %d %d %d", cmd, gid
	, glevel, gold, gcurrent->member_max, gcurrent->consume_gold
	);

	guild_t & guild = get_guild_by_gid(gid);
	if (guild.gid != gid) {
		BUG_PRINT(-73, "out_glevelup:get_guild_by_gid %d %d", gid, guild.gid);
	} else {
		guild.glevel = glevel;
		guild.gold += gold;
	}

	return 0;
}


// CMD: @ladder 
// RET: @ladder rating_total [r_info1] [r_info2] ... level_total [l_info1] [l_info2]..
// r_info: eid, rank, rating, alias
// l_info: eid, rank, level, alias
int out_create_ladder(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int total;
	const char *ptr;
	ladder_rating_t *pladder1;
	ladder_level_t *pladder2;
	ladder_guild_t *pladder3;
	ladder_collection_t *pladder4;
	ladder_gold_t *pladder5;
	ladder_chapter_t *pladder_chapter;

	// DEBUG_PRINT(0, "out_create_ladder:%s", buffer);

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ptr = buffer;

	// load rating ladder
	ret = sscanf(ptr, OUT_RATING_LADDER_SCAN, &total, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -87, "ladder:scanf1 %s", ptr);
	}

	// DEBUG_PRINT(0, "out_create_ladder:total=%d", total);
	if (total < 0) {
		NET_ERROR_RETURN(conn, -3, "ladder:total1 %d %s", total, ptr);
	}
	for (int i=1; i<=total; i++) {
		ptr += n;
		pladder1 = g_ladder_rating_list + i;
		ret = sscanf(ptr, OUT_RATING_LADDER_ROW_SCAN
		, &pladder1->eid, &pladder1->rank, &pladder1->rating
		, pladder1->alias, &pladder1->icon, &n);
		if (ret != 5) {
			NET_ERROR_RETURN(conn, -97, "ladder:scanf3 %d %d %s", i, ret, ptr);
		}
	}
	ptr += n;
	////

	// load level ladder
	ret = sscanf(ptr, OUT_LEVEL_LADDER_SCAN, &total, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -87, "ladder:scanf2 %s", buffer);
	}
	if (total < 0) {
		NET_ERROR_RETURN(conn, -3, "ladder:total2 %d %s", total, buffer);
	}
	for (int i=1; i<=total; i++) {
		ptr += n;
		pladder2 = g_ladder_level_list + i;
		ret = sscanf(ptr, OUT_LEVEL_LADDER_ROW_SCAN, &pladder2->eid
		, &pladder2->rank, &pladder2->lv, pladder2->alias
		, &pladder2->icon, &n);
		if (ret != 5) {
			NET_ERROR_RETURN(conn, -97, "ladder:scanf2 %d %d %s", i, ret, ptr);
		}
	}
	ptr += n;
	////

	// load guild ladder
	ret = sscanf(ptr, OUT_GUILD_LADDER_SCAN, &total, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -87, "ladder:scanf3 %s", buffer);
	}
	// guild ladder total may 0
	if (total < 0) {
		NET_ERROR_RETURN(conn, -3, "ladder:total3 %d %s", total, buffer);
	}
	for (int i=1; i<=total; i++) {
		ptr += n;
		pladder3 = g_ladder_guild_list + i;
		ret = sscanf(ptr, OUT_GUILD_LADDER_ROW_SCAN, &pladder3->gid
		, &pladder3->rank, &pladder3->glevel, pladder3->gname
		, &pladder3->icon, &n);
		if (ret != 5) {
			NET_ERROR_RETURN(conn, -97, "ladder:scanf3 %d %d %s", i, ret, ptr);
		}
	}
	ptr += n;
	////

	// load collection ladder
	ret = sscanf(ptr, OUT_COLLECTION_LADDER_SCAN, &total, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -87, "ladder:scanf2 %s", buffer);
	}
	if (total < 0) {
		NET_ERROR_RETURN(conn, -3, "ladder:total2 %d %s", total, buffer);
	}
	for (int i=1; i<=total; i++) {
		ptr += n;
		pladder4 = g_ladder_collection_list + i;
		ret = sscanf(ptr, OUT_COLLECTION_LADDER_ROW_SCAN, &pladder4->eid
		, &pladder4->rank, &pladder4->count, pladder4->alias
		, &pladder4->icon, &n);
		if (ret != 5) {
			NET_ERROR_RETURN(conn, -97, "ladder:scanf2 %d %d %s", i, ret, ptr);
		}
	}
	ptr += n;
	////

	// load gold ladder
	ret = sscanf(ptr, OUT_GOLD_LADDER_SCAN, &total, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -87, "ladder:scanf2 %s", buffer);
	}
	if (total < 0) {
		NET_ERROR_RETURN(conn, -3, "ladder:total2 %d %s", total, buffer);
	}
	for (int i=1; i<=total; i++) {
		ptr += n;
		pladder5 = g_ladder_gold_list + i;
		ret = sscanf(ptr, OUT_GOLD_LADDER_ROW_SCAN, &pladder5->eid
		, &pladder5->rank, &pladder5->gold, pladder5->alias
		, &pladder5->icon, &n);
		if (ret != 5) {
			NET_ERROR_RETURN(conn, -97, "ladder:scanf2 %d %d %s", i, ret, ptr);
		}
	}
	ptr += n;
	////

	// load chapter ladder
	ret = sscanf(ptr, "%d %n", &total, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -87, "ladder:chapter_scanf1 %s", buffer);
	}
	if (total < 0) {
		NET_ERROR_RETURN(conn, -3, "ladder:chapter_total2 %d %s", total, buffer);
	}
	for (int i=1; i<=total; i++) {
		ptr += n;
		pladder_chapter = g_ladder_chapter_list + i;
		// eid,rank,chapter_id,stage_id,count,alias,icon
		ret = sscanf(ptr, "%d %d %d %d %d %s %d %n"
		, &pladder_chapter->eid, &pladder_chapter->rank
		, &pladder_chapter->chapter_id, &pladder_chapter->stage_id
		, &pladder_chapter->count
		, pladder_chapter->alias
		, &pladder_chapter->icon, &n);
		if (ret != 7) {
			NET_ERROR_RETURN(conn, -97, "ladder:chapter_scanf2 %d %d %s", i, ret, ptr);
		}
	}
	ptr += n;
	////

	net_writeln(conn, "%s %s", cmd, buffer);

	return 0;
}

int get_rating_ladder(connect_t *conn, const char *cmd, int ladder_type, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int eid;
	int rank;
	double rating;
	int count;
	ladder_rating_t *pladder;
	evil_user_t *puser;
	char *out_ptr;

	ret = sscanf(in_buffer, OUT_GET_RATING_LADDER_SCAN, &eid, &rank, &rating);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -67, "get_ladder:scanf %s", in_buffer);
	}

	if (eid != get_eid(conn)) {
		WARN_PRINT(-9, "get_ladder:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}
	
	if (rank < 0) {
		NET_ERROR_RETURN(conn, -77, "get_ladder:rank<= 0 %d", rank);
	}

	puser = &conn->euser;
	if (puser->eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "get_ladder:not_login %d", eid);
	}

	count = 0;
	out_ptr = out_buffer;
	for (int i=1; i<=MAX_LADDER; i++) {
		pladder = &g_ladder_rating_list[i];	
		if (pladder->eid > 0) {
			count++;
			out_ptr += sprintf(out_ptr, "%d %d %lf %s %d "
			, pladder->eid, pladder->rank
			, pladder->rating, pladder->alias, pladder->icon);
		}
	}
	count++;
	sprintf(out_ptr, "%d %d %lf %s %d", eid, rank, rating, puser->alias, puser->icon);

	net_writeln(conn, "%s %d %d %s", cmd, ladder_type, count, out_buffer);

	ret = 0;
	return ret;
}

int get_level_ladder(connect_t *conn, const char *cmd, int ladder_type, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int eid;
	int rank;
	int lv;
	int count;
	ladder_level_t *pladder;
	evil_user_t *puser;
	char *out_ptr;

	ret = sscanf(in_buffer, OUT_GET_LEVEL_LADDER_SCAN, &eid, &rank, &lv);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -67, "get_ladder:scanf %s", in_buffer);
	}

	if (eid != get_eid(conn)) {
		WARN_PRINT(-9, "get_ladder:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}
	
	if (rank < 0) {
		NET_ERROR_RETURN(conn, -77, "get_ladder:rank<= 0 %d", rank);
	}

	puser = &conn->euser;
	if (puser->eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "get_ladder:not_login %d", eid);
	}

	count = 0;
	out_ptr = out_buffer;
	for (int i=1; i<=MAX_LADDER; i++) {
		pladder = &g_ladder_level_list[i];	
		if (pladder->eid > 0) {
			count++;
			out_ptr += sprintf(out_ptr, "%d %d %d %s %d "
			, pladder->eid, pladder->rank
			, pladder->lv, pladder->alias, pladder->icon);
		}
	}
	count++;
	sprintf(out_ptr, "%d %d %d %s %d", eid, rank, lv, puser->alias, puser->icon);

	net_writeln(conn, "%s %d %d %s", cmd, ladder_type, count, out_buffer);

	ret = 0;
	return ret;
}

int get_guild_ladder(connect_t *conn, const char *cmd, int ladder_type, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int gid;
	int rank;
	int glevel;
	int count;
	ladder_guild_t *pladder;
	evil_user_t *puser;
	char *out_ptr;

	ret = sscanf(in_buffer, OUT_GET_GUILD_LADDER_SCAN, &gid, &rank, &glevel);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -67, "get_ladder:scanf %s", in_buffer);
	}
	
	if (rank < 0) {
		NET_ERROR_RETURN(conn, -77, "get_ladder:rank<= 0 %d", rank);
	}

	puser = &conn->euser;
	if (puser->eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "get_ladder:not_login %d", puser->eid);
	}

	count = 0;
	out_ptr = out_buffer;
	for (int i=1; i<=MAX_LADDER; i++) {
		pladder = &g_ladder_guild_list[i];	
		if (pladder->gid > 0) {
			count++;
			out_ptr += sprintf(out_ptr, "%d %d %d %s %d "
			, pladder->gid, pladder->rank
			, pladder->glevel, pladder->gname, pladder->icon);
		}
	}
	count++;
	sprintf(out_ptr, "%d %d %d %s %d", gid, rank, glevel, puser->gname, 0);

	net_writeln(conn, "%s %d %d %s", cmd, ladder_type, count, out_buffer);

	ret = 0;
	return ret;
}

int get_collection_ladder(connect_t *conn, const char *cmd, int ladder_type, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int eid;
	int rank;
	int card_count;
	int count;
	ladder_collection_t *pladder;
	evil_user_t *puser;
	char *out_ptr;

	ret = sscanf(in_buffer, OUT_GET_COLLECTION_LADDER_SCAN, &eid, &rank, &card_count);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -67, "get_ladder:scanf %s", in_buffer);
	}

	if (eid != get_eid(conn)) {
		WARN_PRINT(-9, "get_ladder:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}
	
	if (rank < 0) {
		NET_ERROR_RETURN(conn, -77, "get_ladder:rank<= 0 %d", rank);
	}

	puser = &conn->euser;
	if (puser->eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "get_ladder:not_login %d", eid);
	}

	count = 0;
	out_ptr = out_buffer;
	for (int i=1; i<=MAX_LADDER; i++) {
		pladder = &g_ladder_collection_list[i];	
		if (pladder->eid > 0) {
			count++;
			out_ptr += sprintf(out_ptr, "%d %d %d %s %d "
			, pladder->eid, pladder->rank
			, pladder->count, pladder->alias, pladder->icon);
		}
	}
	count++;
	sprintf(out_ptr, "%d %d %d %s %d", eid, rank, card_count, puser->alias, puser->icon);

	net_writeln(conn, "%s %d %d %s", cmd, ladder_type, count, out_buffer);

	ret = 0;
	return ret;
}

int get_gold_ladder(connect_t *conn, const char *cmd, int ladder_type, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int eid;
	int rank;
	int gold;
	int count;
	ladder_gold_t *pladder;
	evil_user_t *puser;
	char *out_ptr;

	ret = sscanf(in_buffer, OUT_GET_GOLD_LADDER_SCAN, &eid, &rank, &gold);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -67, "get_ladder:scanf %s", in_buffer);
	}

	if (eid != get_eid(conn)) {
		WARN_PRINT(-9, "get_ladder:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}
	
	if (rank < 0) {
		NET_ERROR_RETURN(conn, -77, "get_ladder:rank<= 0 %d", rank);
	}

	puser = &conn->euser;
	if (puser->eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "get_ladder:not_login %d", eid);
	}

	count = 0;
	out_ptr = out_buffer;
	for (int i=1; i<=MAX_LADDER; i++) {
		pladder = &g_ladder_gold_list[i];	
		if (pladder->eid > 0) {
			count++;
			out_ptr += sprintf(out_ptr, "%d %d %d %s %d "
			, pladder->eid, pladder->rank
			, pladder->gold, pladder->alias, pladder->icon);
		}
	}
	count++;
	sprintf(out_ptr, "%d %d %d %s %d", eid, rank, gold, puser->alias, puser->icon);

	net_writeln(conn, "%s %d %d %s", cmd, ladder_type, count, out_buffer);

	ret = 0;
	return ret;
}

int get_chapter_ladder(connect_t *conn, const char *cmd, int ladder_type, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int eid;
	int rank;
	int chapter_id;
	int stage_id;
	int star_count;
	int count;
	ladder_chapter_t *pladder;
	evil_user_t *puser;
	char *out_ptr;

	ret = sscanf(in_buffer, "%d %d %d %d %d", &eid, &rank, &chapter_id, &stage_id, &star_count);
	if (ret != 5) {
		NET_ERROR_RETURN(conn, -67, "get_chapter_ladder:scanf %s", in_buffer);
	}

	if (eid != get_eid(conn)) {
		WARN_PRINT(-9, "get_chapter_ladder:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}
	
	if (rank < 0) {
		NET_ERROR_RETURN(conn, -77, "get_chapter_ladder:rank<= 0 %d", rank);
	}

	puser = &conn->euser;
	if (puser->eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "get_chapter_ladder:not_login %d", eid);
	}

	count = 0;
	out_ptr = out_buffer;
	for (int i=1; i<=MAX_LADDER; i++) {
		pladder = &g_ladder_chapter_list[i];	
		if (pladder->eid > 0) {
			count++;
			// eid,rank,chapter_id,stage_id,count,alias,icon
			out_ptr += sprintf(out_ptr, "%d %d %d %d %d %s %d "
			, pladder->eid, pladder->rank
			, pladder->chapter_id, pladder->stage_id, pladder->count
			, pladder->alias, pladder->icon);
		}
	}
	count++;
	sprintf(out_ptr, "%d %d %d %d %d %s %d"
	, eid, rank, chapter_id, stage_id, star_count, puser->alias, puser->icon);

	net_writeln(conn, "%s %d %d %s", cmd, ladder_type, count, out_buffer);

	ret = 0;
	return ret;
}


// CMD: ladder [ladder_type]
// RET: ladder total [info1] [info2] ...
// ladder_type == 0, info: eid, rank, rating, alias, icon
// ladder_type == 1, info: eid, rank, level, alias, icon
// ladder_type == 2, info: gid, rank, glevel, gname
// ladder_type == 3, info: eid, rank, count, alias, icon
// ladder_type == 4, info: eid, rank, gold, alias, icon
// ladder_type == 5, info: eid, rank, chapter_id, stage_id, count, alias, icon
int out_get_ladder(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int ladder_type;
	char out_buffer[DB_BUFFER_MAX + 1];
	bzero(out_buffer, sizeof(out_buffer));

	ret = sscanf(buffer, "%d %n", &ladder_type, &n); // using n maybe faster
	if (ladder_type < 0) {
		NET_ERROR_RETURN(conn, ladder_type, "%s", buffer+n);
	}

	// DEBUG_PRINT(0, "out_get_ladder:buffer=%s", buffer);

	switch (ladder_type) {
	case LADDER_RATING:
		ret = get_rating_ladder(conn, cmd, ladder_type, buffer+n, out_buffer);
		break;
	case LADDER_LEVEL:
		ret = get_level_ladder(conn, cmd, ladder_type, buffer+n, out_buffer);
		break;
	case LADDER_GUILD:
		ret = get_guild_ladder(conn, cmd, ladder_type, buffer+n, out_buffer);
		break;
	case LADDER_COLLECTION:
		ret = get_collection_ladder(conn, cmd, ladder_type, buffer+n, out_buffer);
		break;
	case LADDER_GOLD:
		ret = get_gold_ladder(conn, cmd, ladder_type, buffer+n, out_buffer);
		break;
	case LADDER_CHAPTER:
		ret = get_chapter_ladder(conn, cmd, ladder_type, buffer+n, out_buffer);
		break;
	
	}

	ret = 0;
	return ret;
}


int out_list_replay(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_load_replay(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	/*

	int change = 0;
	int hero1 = 0;
	int hero2 = 0;
	int gameid;
	int winner;
	int seed;
	int start_side;
	int ver;
	int eid1;
	int eid2;
	int lv1;
	int lv2;
	int icon1;
	int icon2;
	char alias1[EVIL_ALIAS_MAX + 3];
	char alias2[EVIL_ALIAS_MAX + 3];
	char deck1[EVIL_CARD_MAX + 3];
	char deck2[EVIL_CARD_MAX + 3];

	// gameid, winner, seed, start_side, ver, eid1, eid2
	// , lv1, lv2, icon1, icon2, alias1, alias2, deck1, deck2
	ret = sscanf(buffer + n, "%d %d %d %d %d %d %d %d %d %d %d %s %s %400s %400s"
	, &gameid, &winner, &seed, &start_side
	, &ver, &eid1, &eid2, &lv1, &lv2, &icon1, &icon2, alias1, alias2
	, deck1, deck2);
	if (ret != 15) {
		ERROR_RETURN(-6, "out_load_replay:sscanf_error %d", ret);
	}
	// DEBUG_PRINT(0, "out_load_replay:deck1=[%s]", deck1);
	// DEBUG_PRINT(0, "out_load_replay:deck2=[%s]", deck2);

	// TODO get hero in card400 or in card list
	hero1 = get_hero(deck1);
	hero2 = get_hero(deck2);
	// DEBUG_PRINT(0, "out_load_replay:hero1=%d hero2=%d", hero1, hero2);
	change |= replay_mission_update(conn, hero1, hero2);
	change_mission(conn, change);
	*/

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_update_profile(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int icon;
	int sex;
	char signature[EVIL_SIGNATURE_MAX+1];
	evil_user_t *puser;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, OUT_UPDATE_PROFILE_SCAN
	, &eid, &icon, &sex, signature);

	if (ret != 4) {
		NET_ERROR_RETURN(conn, -67, "out_update_profile:scanf %s", buffer);
	}

	if (eid != get_eid(conn)) {
		WARN_PRINT(-9, "out_update_profile:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}
	
	if (icon < 0 || icon > EVIL_ICON_MAX) {
		NET_ERROR_RETURN(conn, -77, "out_update_profile:icon_error %d", icon);
	}
	
	if (sex < 0 || sex > 1) {
		NET_ERROR_RETURN(conn, -77, "out_update_profile:sex_error %d", sex);
	}

	if (strlen(signature) == 0) {
		NET_ERROR_RETURN(conn, -77, "out_update_profile:signature_error %.100s", signature);
	}

	puser = &conn->euser;
	if (puser->eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "out_update_profile:not_login %d", eid);
	}

	puser->icon = icon;
	puser->sex = sex;
	sprintf(puser->signature, "%.100s", signature);

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_friend_add(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	// update MISSION_FRIEND
	int change = 0;
	change |= friend_mission_update(conn);
	change_mission(conn, change); // refresh, mchange

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_friend_list(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int total;
	int start_num;
	int page_size;
	char alias[EVIL_ALIAS_MAX+1];
	int icon;
	int flag = 0;	// 0==offline, 1==online
	char *out_ptr;
	char out_buffer[BUFFER_SIZE+1];
	out_buffer[0]='\0';
	const char *ptr;
	connect_t *friend_conn;

	// DEBUG_PRINT(0, "out_friend_list:buffer=%s", buffer);

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, OUT_FRIEND_LIST_SCAN
	, &eid, &total, &start_num, &page_size, &n);

	if (ret != 4) {
		NET_ERROR_RETURN(conn, -67, "out_friend_list:scanf %s", buffer);
	}

	if (eid != get_eid(conn)) {
		WARN_PRINT(-9, "out_friend_list:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}

	if (total < 0) {
		NET_ERROR_RETURN(conn, -77, "out_friend_list:total %s", buffer);
	}

	if (total == 0) {
		net_writeln(conn, "%s %s", cmd, buffer);
		return 0;
	}

	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, "%d %d %d %d", eid, total, start_num, page_size);

	ptr = buffer;
	for (int i=0;i<page_size;i++) {
		ptr += n;
		ret = sscanf(ptr, OUT_FRIEND_LIST_ROW_SCAN, &eid, alias, &icon, &n);
		if (ret != 3) {
			NET_ERROR_RETURN(conn, -66, "out_friend_list:info_scan %s", buffer);
		}

		friend_conn = get_conn_by_eid(eid);
		if (friend_conn == NULL) {
			flag = 0;
		} else {
			flag = 1;
		}
		
		out_ptr += sprintf(out_ptr, " %d %d %s %d", eid, flag, alias, icon);
	}

	// DEBUG_PRINT(0, "out_friend_list:out_buffer=%s", out_buffer);
		
	net_writeln(conn, "%s %s", cmd, out_buffer);

	return 0;
}

int out_friend_sta(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int friend_flag = 0;
	int exp_next = 0;
	int exp_this = 0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	evil_user_t euser;
	ret = sscanf(buffer, OUT_FRIEND_STA_SCAN
	, &euser.eid, euser.alias, &euser.lv, &euser.rating, &euser.gold
	, &euser.crystal, &euser.gid, &euser.gpos, euser.gname
	, &euser.game_count, &euser.game_win
	, &euser.game_lose, &euser.game_draw
	, &euser.game_run, &euser.icon, &euser.exp
	, &euser.sex, &friend_flag, euser.signature);

	if (ret != 19) {
		NET_ERROR_RETURN(conn, -95, "fsta:out_sscanf %d", ret);
	}

	if (euser.lv >= g_design->max_level) {
		exp_next = g_design->exp_list[g_design->max_level];
	} else {
		exp_next = g_design->exp_list[euser.lv + 1];
	}
	exp_this = 0;

	net_writeln(conn, "%s %d %s %d %lf %d %d %d %d %s %d %d %d %d %d %d %d %d %d %d %d %s", cmd, euser.eid, euser.alias, euser.lv, euser.rating
	, euser.gold, euser.crystal, euser.gid, euser.gpos, euser.gname
	, euser.game_count, euser.game_win
	, euser.game_lose, euser.game_draw
	, euser.game_run, euser.icon, euser.exp, exp_next, exp_this
	, euser.sex, friend_flag, euser.signature);
	// net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_friend_search(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	const char * ptr;

	connect_t *friend_conn;
	int eid;
	int total;
	char alias[EVIL_ALIAS_MAX+1];
	int icon;
	int lv;
	int flag = 0;	// 0==offline, 1==online

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	
	// out_buffer = [count] [info1] [info2] ...
	// info = [eid] [alias] [icon] [lv]

	ret = sscanf(buffer, "%d %n", &total, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -67, "out_friend_search:scanf %s", buffer);
	}

	if (total < 0) {
		NET_ERROR_RETURN(conn, -77, "out_friend_search:total %s", buffer);
	}

	net_write_space(conn, "%s %d", cmd, total);

	ptr = buffer;
	for (int i=0; i<total; i++) {
		ptr += n;
		ret = sscanf(ptr, "%d %30s %d %d %n", &eid, alias, &icon, &lv, &n);
		if (ret != 4) {
			NET_ERROR_RETURN(conn, -66, "out_friend_search:info_scan %s", buffer);
		}

		friend_conn = get_conn_by_eid(eid);
		if (friend_conn == NULL) {
			flag = 0;
		} else {
			flag = 1;
		}
		net_write_space(conn, "%d %d %.30s %d %d", eid, flag, alias, icon, lv);
	}
	net_writeln(conn, "");

	// net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_guild(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int gid;
	int total_member;
	int glevel;
	int gold;
	int crystal;
	int consume;
	int member_max;
//	const char * ptr;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %d %d %d %n", &gid, &total_member, &glevel
	, &gold, &crystal, &n); 
	if (ret != 5) {
		NET_ERROR_RETURN(conn, -67, "%s", buffer);
	}

	if (gid <= 0) {
		BUG_PRINT(-6, "out_guild:gid_bug %d", gid);
		NET_ERROR_RETURN(conn, -6, "gid_bug %d", gid);
	}

	if (total_member <= 0) {
		BUG_PRINT(-16, "out_guild:total_member_bug %d", total_member);
		NET_ERROR_RETURN(conn, -16, "total_member_bug %d", total_member);
	}

	if (glevel <= 0) {
		BUG_PRINT(-26, "out_guild:glevel_bug %d", glevel);
		NET_ERROR_RETURN(conn, -26, "glevel_bug %d", glevel);
	}

	if (gold < 0) {
		BUG_PRINT(-36, "out_guild:gold_bug %d", gold);
		NET_ERROR_RETURN(conn, -36, "gold_bug %d", gold);
	}

	if (crystal < 0) {
		BUG_PRINT(-46, "out_guild:crystal_bug %d", crystal);
		NET_ERROR_RETURN(conn, -46, "crystal_bug %d", crystal);
	}

	consume = 0;
	member_max = 0;
	if (glevel >= g_design->guild_max_level) {
		consume = g_design->guild_list[g_design->guild_max_level].consume_gold;
		member_max = g_design->guild_list[g_design->guild_max_level].member_max;
	} else {
		consume = g_design->guild_list[glevel].consume_gold;
		member_max = g_design->guild_list[glevel].member_max;
	}

	guild_t & guild = g_guild_map[gid];
	if (guild.gid != 0) {
		guild.glevel = glevel;
	}

//	ptr = buffer + n;

	net_writeln(conn, "%s %d %d %d %d %d %d %d %s"
	, cmd, gid, total_member, member_max, glevel
	, gold, crystal, consume, buffer+n);
	return 0;
}


int out_load_piece(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_pick_piece(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int pick_type;
	int loc;
	int card_id;
	int count;
	int gold;
	int crystal;
	int n = 0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	DEBUG_PRINT(0, "out_pick_piece:buffer=%s", buffer);

	ret = sscanf(buffer, OUT_PICK_PIECE_SCAN, &eid, &pick_type
	, &loc, &card_id, &count, &gold, &crystal);

	// since eid is mismtach, only print err in server, not report to client
	if (get_eid(conn) != eid) {
		WARN_PRINT(-9, "out_pick_piece:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}

	conn->euser.gold += gold;
	conn->euser.crystal += crystal;

	net_writeln(conn, "%s %d %d %d %d %d %d %d", cmd
	, eid, pick_type, loc, card_id, count, gold, crystal);

	return ret;
}


int out_merge_piece(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int card_id;
	int count;
	int gold;
	int crystal;
	int n = 0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	DEBUG_PRINT(0, "out_merge_piece:buffer=%s", buffer);

	ret = sscanf(buffer, OUT_MERGE_PIECE_SCAN, &eid
	, &card_id, &count, &gold, &crystal);

	// since eid is mismtach, only print err in server, not report to client
	if (get_eid(conn) != eid) {
		WARN_PRINT(-9, "out_merge_piece:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}

	conn->euser.gold += gold;
	conn->euser.crystal += crystal;

	net_writeln(conn, "%s %d %d %d %d %d", cmd
	, eid, card_id, count, gold, crystal);

	return ret;
}

// result always >= 0
int safe_add(int a, int b)
{
	long sum = (long)a + (long)b;
	if (sum > INT32_MAX) {
		sum = INT32_MAX;
	}
	if (sum < 0) {
		sum = 0;
	}
	return (int)sum;
}

int out_pay(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	long pay_id;
	int eid;
	int money_type;
	int money;
	int channel;
	int price;
	int extra_gold;
	int extra_crystal;
	int monthly_flag;
	time_t monthly_end_date;
	int extra_card_kind;
	int extra_card_list[MAX_FIRST_VIP_CARD_KIND][2];
	int change;
	int n = 0;
	const char *in_ptr;
	char *tmp_ptr;
	char tmp_buffer[100];
	bzero(tmp_buffer, sizeof(tmp_buffer));

	sscanf(buffer, "%ld %n", &pay_id, &n); // using n maybe faster
	if (pay_id < 0) {
		NET_ERROR_RETURN(conn, (int)pay_id, "%s", buffer+n);
	}

	DEBUG_PRINT(0, "out_pay:buffer=%s", buffer);

	in_ptr = buffer;
	ret = sscanf(in_ptr, OUT_PAY_SCAN, &pay_id, &eid
	, &money_type, &money, &channel, &price
	, &extra_gold, &extra_crystal
	, &monthly_flag, &monthly_end_date, &extra_card_kind, &n);
	if (ret != 11) {
		NET_ERROR_RETURN(conn, -5, "out_pay_error[%s]", buffer);
	}
	in_ptr += n;
	for (int i = 0; i < extra_card_kind; i++)
	{
		ret = sscanf(in_ptr, " %d %d %n", &extra_card_list[i][0]
		, &extra_card_list[i][1], &n);
		if (ret != 2) {
			NET_ERROR_RETURN(conn, -5, "out_pay_card_list_error eid[%d]", eid);
		}
		in_ptr += n;
	}
	sprintf(tmp_buffer, "%d", 0);

	connect_t* player_conn = get_conn_by_eid(eid);
	if (player_conn == NULL) {
		WARN_PRINT(-9, "out_pay:player_offline %d", eid);
	} else {
		
		if (money_type == 0) {
			player_conn->euser.gold = safe_add(player_conn->euser.gold, money);
		} else if (money_type == 1) {
			player_conn->euser.gold = safe_add(player_conn->euser.gold
			, extra_gold);
			player_conn->euser.crystal = safe_add(player_conn->euser.crystal
			, money + extra_crystal);
		}
		player_conn->euser.monthly_end_date = monthly_end_date;
		for (int i = 0; i < extra_card_kind; i++)
		{
			int card_id = extra_card_list[i][0];
			int card_count = extra_card_list[i][1];
			if (player_conn->euser.card[card_id-1] + card_count > '9') {
				player_conn->euser.card[card_id-1] = '9';
			} else {
				player_conn->euser.card[card_id-1] += card_count;
			}
		}

		tmp_ptr = tmp_buffer;
		tmp_ptr += sprintf(tmp_ptr, "%d", extra_card_kind);
		for (int i = 0; i < extra_card_kind; i++)
		{
			tmp_ptr += sprintf(tmp_ptr, " %d %d"
			, extra_card_list[i][0], extra_card_list[i][1]);
		}
		// notice to online player
		net_writeln(player_conn, "%s %ld %d %d %d %d %d %d %d %ld %s"
		, cmd, pay_id, eid, money_type, money, channel, price
		, extra_gold, extra_crystal, monthly_end_date, tmp_buffer);

	}

	// this feed back to pay_server
	net_writeln(conn, "%s %ld %d %d %d %d %d"
	, cmd, pay_id, eid, money_type, money, channel, price);

	if (monthly_flag)
	{
		change = 0;
		change |= monthly_mission_update(player_conn);
		change_mission(player_conn, change); // refresh, mchange
	}

	return ret;
}


int out_get_course(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int course;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, OUT_GET_COURSE_SCAN, &eid, &course);
	if (get_eid(conn) != eid) {
		WARN_PRINT(-9, "out_get_course:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_save_course(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int course;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, OUT_SAVE_COURSE_SCAN, &eid, &course);
	if (get_eid(conn) != eid) {
		WARN_PRINT(-9, "out_save_course:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int start_challenge(int eid1, int eid2) 
{
	int ret;
	connect_t *conn1, *conn2;
	conn1 = get_conn_by_eid(eid1);
	conn2 = get_conn_by_eid(eid2);
	if (NULL==conn1 || NULL==conn2) { // means it is offline
		const char *errstr = "challenge -26 opponent_offline";
		if (conn1!=NULL) { net_writeln(conn1, errstr); }
		if (conn2!=NULL) { net_writeln(conn2, errstr); }
		return -26;
	}
	if (NULL != conn1->room || NULL!= conn2->room) {
		const char *errstr = "challenge -36 player already in room";
		if (conn1!=NULL) { net_writeln(conn1, errstr); }
		if (conn2!=NULL) { net_writeln(conn2, errstr); }
		return -36;
	}

	room_t *proom;
	proom = new_room(CHANNEL_QUICK);
	proom->gameid = get_gameid();
	proom->state = ST_GAME; // make it a game!
	proom->num_guest = 2;
	proom->guest[0] = eid1; // challenger
	proom->guest[1] = eid2; // receiver
	proom->rating[0] = conn1->euser.rating;
	proom->rating[1] = conn2->euser.rating;
	strcpy(proom->alias[0], conn1->euser.alias);
	strcpy(proom->alias[1], conn2->euser.alias);
	proom->icon[0] = conn1->euser.icon;
	proom->icon[1] = conn2->euser.icon;
	proom->lv[0] = conn1->euser.lv;
	proom->lv[1] = conn2->euser.lv;
	proom->game_type = GAME_CHALLENGE;
	// copy the deck to proom deck
	sprintf(proom->deck[0], "%.400s", conn1->euser.deck);
	sprintf(proom->deck[1], "%.400s", conn2->euser.deck);
	sprintf(proom->title, "%s~VS~%s", conn1->euser.alias, conn2->euser.alias);

	room_set_hero_info(proom, NULL);
	ret = game_init(proom, 0, 0); // init proom->deck
	if (ret < 0) {
		const char *errstr = "challenge -18 subfun_err %d";
		// TODO how to 
		if (conn1!=NULL) { net_writeln(conn1, errstr, ret); }
		if (conn2!=NULL) { net_writeln(conn2, errstr, ret); }
		ret = free_room(proom->channel, proom);  // order is important
		return -18;
	}
	// note: order is important, it must be after game_init()
	conn1->room = proom;
	conn2->room = proom;
	conn1->st = proom->state;
	conn2->st = proom->state;
	// re-conn logic
	g_user_room[eid1] = proom;
	g_user_room[eid2] = proom;
	room_info_broadcast(proom, 0); // 0 means all
	INFO_PRINT(0, "room_title=%s (%d vs %d)", proom->title, proom->guest[0], proom->guest[1]);
	// game_broadcast(proom);

	return 0;
}

// it is like fill up the deck and go straight to quick game logic
int out_challenge(connect_t * conn, const char *cmd, const char *buffer)
{

	// DEBUG_PRINT(0, "out_challenge:buffer=%s\n", buffer);
	int len;
	int eid1;
	int eid2;
	connect_t *conn_player;
	int ret = -75, n = 0;  // simple error handling
	sscanf(buffer, "%d %n", &ret, &n); // no need to catch ret=sscanf
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	eid1 = ret;
	// DEBUG_PRINT(0, "out_challenge:eid1 = %d", eid1);
	conn_player = get_conn_by_eid(eid1);
	if (conn_player == NULL) {
		NET_ERROR_RETURN(conn, -6, "out_challenge:oppo_not_login");
	}
	buffer = buffer + n;
	len = sprintf(conn_player->euser.deck, "%.400s", buffer);
	if (len != 400) {
		// this is rather buggy
		WARN_PRINT(-5, "out_challenge:deck_len=%d", len);
	}
	// DEBUG_PRINT(0, "out_challenge:deck=%s", conn_player->euser.deck);

	buffer += len;
	ret = sscanf(buffer, "%d %n", &eid2, &n); 
	// DEBUG_PRINT(0, "out_challenge:eid2 = %d", eid2);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	conn_player = get_conn_by_eid(eid2);
	if (conn_player == NULL) {
		NET_ERROR_RETURN(conn, -6, "out_challenge:oppo_not_login");
	}
	len = sprintf(conn_player->euser.deck, "%.400s", buffer+n);
	if (len != 400) {
		// this is rather buggy
		WARN_PRINT(-5, "out_challenge:deck_len=%d", len);
	}
	// DEBUG_PRINT(0, "out_challenge:deck=%s", conn_player->euser.deck);
	
	return start_challenge(eid1, eid2);
}


// out_login will send load mission, finally go here
int out_load_mission(connect_t * conn, const char *cmd, const char *buffer)
{
	int eid;
	int count;
	int change = 0;
	int ret = -75, n = 0;  // simple error handling
	const char * ptr;
	mission_t mis;
	sscanf(buffer, "%d %n", &ret, &n); // no need to catch ret=sscanf
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	// DEBUG_PRINT(0, "out_load_mission:buffer=%s", buffer);

	eid = ret;
	if (eid != get_eid(conn)) {
		// normal if the user disconnect and conn is re-used
		WARN_PRINT(-9, "out_load_mission:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}
	ptr = buffer + n;
	count = -1;
	ret = sscanf(ptr, "%d %n", &count, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -85, "scanf_count_error");
	}
	if (count < 0) {
		NET_ERROR_RETURN(conn, -95, "count_error");
	}
	ptr += n;

	bzero(conn->euser.mission_list, sizeof(mission_t) * MAX_MISSION);
	// count = 0 fall through
	for (int i=0; i<count; i++) {
		ret = sscanf(ptr, OUT_LOAD_MISSION_SCAN
			, &mis.mid, &mis.n1
			, &mis.status, &mis.last_update, &n);
		if (ret != 4) {
			NET_ERROR_RETURN(conn, -95, "mission_sscanf %d", ret);
		}
		ptr += n;
		// mission-fix
		conn->euser.mission_list[mis.mid] = mis; // memory copy
	}

 	// TODO remove this later
	// net_writeln(conn, "%s 0 OK", cmd);

	// for debug
	// printf("-------- before mission_refresh --------\n");
	// print_mission_list(conn->euser.mission_list);

	/*
	change = mission_refresh(conn->euser.mission_list
	, g_design->mission_list, conn->euser.lv, conn->euser.card);
	// only call when mission target finish
	if (change == 2) {
		net_writeln(conn, "mchange %d", change);
	}
	*/
	// mission refresh and save mission change

	// printf("-------- after mission_refresh --------\n");
	// print_mission_list(conn->euser.mission_list);

	change = 0;

	// update MISSION_GUILD
	if (conn->euser.gid > 0 && conn->euser.gpos < 9 
	&& conn->euser.gpos > 0) {
		change |= guild_mission_update(conn);
	}

	if (conn->euser.monthly_end_date > time(NULL))
	{
		change |= monthly_mission_update(conn);
	}

	change_mission(conn, change);
	
	return 0;
}

int out_save_mission(connect_t * conn, const char *cmd, const char *buffer)
{
	int eid;
	int count;
	int ret = -75, n = 0;  // simple error handling
	const char * ptr;
	sscanf(buffer, "%d %n", &ret, &n); // no need to catch ret=sscanf
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	eid = ret;
	if (eid != get_eid(conn)) {
		// normal if the user disconnect and conn is re-used
		WARN_PRINT(-9, "out_save_mission:eid_mismatch %d %d", get_eid(conn), eid);
		return 0;
	}
	ptr = buffer + n;
	count = -1;
	ptr += sscanf(ptr, "%d %n", &count, &n);
	if (count == 0) {
		NET_ERROR_RETURN(conn, -95, "save_no_mission");
	}
	if (count < 0) {
		NET_ERROR_RETURN(conn, -85, "count_error");
	}

	// no need send to client
	// net_writeln(conn, "%s 0 OK", cmd);
	
	return 0;
}


int out_load_slot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	
	if (ret != get_eid(conn)) {
		WARN_PRINT(-9, "out_load_slot:eid_mismatch %d %d"
		, get_eid(conn), ret);
		return 0;
	}

	net_writeln(conn, "%s %s", cmd, buffer + n);
	return 0;
}

int out_slot_list(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	
	if (ret != get_eid(conn)) {
		WARN_PRINT(-9, "out_slot_list:eid_mismatch %d %d"
		, get_eid(conn), ret);
		return 0;
	}

	net_writeln(conn, "%s %s", cmd, buffer + n);
	return 0;
}

int out_save_slot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	
	if (ret != get_eid(conn)) {
		WARN_PRINT(-9, "out_save_slot:eid_mismatch %d %d"
		, get_eid(conn), ret);
		return 0;
	}
	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_rename_slot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	
	if (ret != get_eid(conn)) {
		WARN_PRINT(-9, "out_rename_slot:eid_mismatch %d %d"
		, get_eid(conn), ret);
		return 0;
	}
	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_buy_slot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	int eid;
	int flag;
	int id;
	int gold;
	int crystal;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	
	if (ret != get_eid(conn)) {
		WARN_PRINT(-9, "out_buy_slot:eid_mismatch %d %d"
		, get_eid(conn), ret);
		return 0;
	}

	ret = sscanf(buffer, OUT_BUY_SLOT_SCAN
	, &eid, &flag, &id, &gold, &crystal);

	if (ret != 5) {
		ERROR_RETURN(-6, "out_buy_slot:out_scan_err eid=%d", get_eid(conn));
	}
	if (flag == 1 || flag == 2) {
		// update memory money	
		conn->euser.gold -= gold;
		conn->euser.crystal -= crystal;
	}

	net_writeln(conn, "%s %s", cmd, buffer + n);
	return 0;
}

int out_mission_reward(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer + n);
	return 0;
}

int out_add_match(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	return 0; // not yet
}

int out_match_apply(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	return 0; // not yet
}

int out_match_cancel(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	return 0; // not yet
}

int out_match_team_init(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	INFO_PRINT(0, ">>>>>>>>>>>>>>>>>admin_match_init:end=%ld", time(NULL));

	return 0; // not yet
}

int out_update_player(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		ERROR_RETURN(ret, "%s", buffer+n);
	}

	return 0; // not yet
}

int out_match_eli_init(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	return 0; // not yet
}

int out_match_apply_ai(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	return 0; // not yet
}

int out_update_match_status(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		ERROR_RETURN(ret, "%s", buffer+n);
	}

	return 0; // not yet
}

int out_friend_del(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_init_ranking(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int __get_rank_reward_by_rank(design_rank_reward_t &reward, int rank)
{
	for (int i=g_design->max_rank_reward_level; i > 0; i--) {
		design_rank_reward_t & tmp_reward = g_design->rank_reward_list[i];
		if (tmp_reward.id <= 0) {
			WARN_PRINT(-5
			, "__get_rank_reward_by_rank:reward.id[%d]<=0", tmp_reward.id);
			continue;
		}
//		if (rank >= tmp_reward.start) {
//			reward = tmp_reward;
//			return 0;
//		}
		if ((rank >= tmp_reward.start)
		&& ((tmp_reward.end == 0)
			|| ((tmp_reward.end != 0) && (rank <= tmp_reward.end)))) {
			reward = tmp_reward;
			return 0;
		}
	}
	return -1;
}

int __get_next_rank_reward_remain_time(time_t &remain, time_t now)
{
	struct tm timestruct;
	time_t today;
	localtime_r(&now, &timestruct);
	timestruct.tm_sec = 0;
	timestruct.tm_min = 0;
	timestruct.tm_hour = 0;
	today = mktime(&timestruct);
	int now_duration = now - today;

	int min_timestamp = __INT_MAX__;
	int tomorrow_rank_time;

	for (int i=0; i < g_design->max_rank_time; i++) {
		design_rank_time_t &rank_time = g_design->rank_time_list[i];
		if (rank_time.id <= 0) {
			continue;
		}
		int tmp_time = (rank_time.time / 100) * 60 * 60
		+ (rank_time.time % 100) * 60;

		if (min_timestamp > tmp_time) {
			min_timestamp = tmp_time;
		}

		// INFO_PRINT(1, "get_reward_remain_time:now_duration[%d] tmp_time[%d]"
		// , now_duration, tmp_time);
		if (now_duration <= tmp_time) {
			remain = tmp_time - now_duration;
			return 0;
		}
	}

	tomorrow_rank_time = min_timestamp + 24 * 60 * 60;
	remain = tomorrow_rank_time - now_duration;

	return 0;
}

int out_ranking_list(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int rank;
	time_t remain;
	design_rank_reward_t rank_reward;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	rank = ret;
	ret = __get_rank_reward_by_rank(rank_reward, rank);
	NET_ERROR_RETURN(conn, ret
	, "out_ranking_list:__get_rank_reward_by_rank rank[%d]", rank);
	ret = __get_next_rank_reward_remain_time(remain, time(NULL));
	NET_ERROR_RETURN(conn, ret
	, "out_ranking_list:__get_next_rank_reward_remain_time");

	net_writeln(conn, "%s %d %d %d %s", cmd, rank_reward.gold
	, rank_reward.crystal, remain, buffer);
	return 0;
}

int out_ranking_targets(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int rank;
	time_t remain;
	design_rank_reward_t rank_reward;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	rank = ret;
	ret = __get_rank_reward_by_rank(rank_reward, rank);
	NET_ERROR_RETURN(conn, ret
	, "out_ranking_list:__get_rank_reward_by_rank rank[%d]", rank);
	ret = __get_next_rank_reward_remain_time(remain, time(NULL));
	NET_ERROR_RETURN(conn, ret
	, "out_ranking_list:__get_next_rank_reward_remain_time");

	net_writeln(conn, "%s %d %d %d %s", cmd, rank_reward.gold
	, rank_reward.crystal, remain, buffer);
	return 0;
}


int __add_eid_to_rank_room(room_t *proom, int eid, int icon, const char * name)
{
	// assert(NULL != proom); // caller must ensure non-null

	if ( proom->num_guest >= MAX_GUEST ) {
		return -2;
	}

//	if (eid <= 0) {
//		return -16;
//	}
	// TODO euser add to proom
	// re-conn logic
	proom->guest[ proom->num_guest ] = eid;  // euser.eid
	const char * alias = name;
	if (strlen(alias)==0) {
		alias = VISITOR_ALIAS;
	}
	strcpy(proom->alias[ proom->num_guest ], alias);
	// TODO
	proom->icon[ proom->num_guest] = icon;
	proom->lv[ proom->num_guest] = 1;
	// printf("do_room_add:icon=%d lv=%d\n"
	// , proom->icon[proom->num_guest]
	// , proom->lv[proom->num_guest]);
	if (proom->num_guest == 0) {
		sprintf(proom->title, "%s_room", proom->alias[0]);
	}
	if (proom->num_guest == 1) {
		sprintf(proom->title, "%s_v_%s"
		, proom->alias[0], proom->alias[1]);
	}
	proom->num_guest ++;

	// fill up "conn" 
	connect_t * conn;
	conn = get_conn_by_eid(eid);
	if (conn != NULL) {
		INFO_PRINT(1, "add_eid_to_rank_room: eid[%d] st[%d]", eid, proom->state);
		conn->room = proom;
		conn->st = proom->state;
	}
	// re-conn logic
	if (eid < 0 || eid > MAX_AI_EID) {
		g_user_room[eid] = proom;
	}
	return 0;
}

room_t * create_rank_room(int eid1, int eid2, int icon1, int icon2, const char * alias1, const char * alias2)
{
	int ret;
	room_t *proom = NULL;
	proom = new_room(CHANNEL_QUICK);
	if (proom==NULL) {
		ERROR_PRINT(-7, "create_rank_room:bug");
		return NULL;
	}
	proom->game_type = GAME_RANK;

	// add player to proom
	ret = __add_eid_to_rank_room(proom, eid1, icon1, alias1);
	if (ret < 0) {
		ERROR_PRINT(-16, "create_rank_room:eid1 %d", eid1);
		free_room(proom->channel, proom);
		return NULL;
	}

	ret = __add_eid_to_rank_room(proom, eid2, icon2, alias2);
	if (ret < 0) {
		ERROR_PRINT(-26, "create_rank_room:eid2 %d", eid2);
		free_room(proom->channel, proom);
		return NULL;
	}

	sprintf(proom->title, "%s~VS~%s", alias1, alias2);
	return proom;
}

/*
int __start_ranking_challenge(int eid, int target_eid, int icon, int target_icon, const char* alias, const char *target_alias, int challenge_time, int resp)
{
	int ret;
	
	int oppo_eid = (resp == RANKING_CHALLENGE_STATUS_ACCEPT)
	? target_eid : -target_eid;
	// 2. create a ranking room, join these 2 player
	room_t *proom = create_rank_room(eid
	, oppo_eid, icon, target_icon, alias
	, target_alias);	
	if (proom == NULL) {
		ERROR_RETURN(-6, "out_rank_game:create_rank_room_fail");
	}

	// challenge_time will auto descrease when eid win the game
//	challenge_time -= 1;
//	ret = dbin_write(NULL, "rank_challenge", DB_SAVE_RANKING_CHALLENGE
//	, IN_SAVE_RANKING_CHALLENGE_PRINT, eid, challenge_time);

	// 3. start game
	// order is important, room creater eid must before room joiner eid
	ret = dbin_write(NULL, "game", DB_GAME, IN_GAME_PRINT
	, eid, target_eid);

//	net_writeln(conn, "%s %s", cmd, buffer);
	return ret;
}
*/

int is_ranking_robot(int eid)
{
	if (eid >= 3127 && eid <= 4126) {
		return 1;
	}
	return 0;
}

int update_ranking_hero(int eid, int hero_id, int rank, int &hp, int &energy)
{
	const int TOTAL_RANK = 10000;
	int std_hero_hp_max = g_design->hero_hp_list[hero_id];
	int std_hero_hp_min = g_design->hero_list[hero_id].hp;

	if (hp != 0) {
		return 0;
	}

	// for robot in ranking game
	if (is_ranking_robot(eid)) {
		hp = min((int)((1 - (double)rank/TOTAL_RANK) * std_hero_hp_max), std_hero_hp_max);
		hp = max(hp, std_hero_hp_min);
		return 0;
	}

	// for player who has no hero in evil.evil_hero
	hp = std_hero_hp_min;

	INFO_PRINT(0, "update_ranking_hero:hero_id=%d hp=%d energy=%d", hero_id, hp, energy);
	
	return 0;
}

int out_ranking_game(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid, icon;
	int target_eid, target_icon;
	int hero_id, hp, energy, rank;
	int target_hero_id, target_hp, target_energy, target_rank;
	char deck1[EVIL_CARD_MAX+1], deck2[EVIL_CARD_MAX+1];
	char alias[EVIL_ALIAS_MAX + 10], target_alias[EVIL_ALIAS_MAX + 10];
	int challenge_time;
	int is_in_ranking_game;
	int is_in_rankpair_list;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer+n, "%d %d %d %d %d %s %s %d %d %d %d %d %d %d %d %400s %400s"
	, &eid, &target_eid, &challenge_time
	, &icon, &target_icon, alias, target_alias
	, &hero_id, &target_hero_id, &hp, &target_hp
	, &energy, &target_energy, &rank, &target_rank
	, deck1, deck2);

	if (ret != 17) {
		ERROR_PRINT(ret, "out_buffer[%s]", buffer);
		NET_ERROR_RETURN(conn, -7, "out_ranking_game:input_count_invalid %d", ret);
	}
	
	// eid mismatch
	if (eid != get_eid(conn)) {
		return 0;
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "out_ranking_game:already_in_room");
	}

	// is_in_ranking_game = __is_in_ranking_game(eid);
	is_in_rankpair_list = __is_in_rankpair_list(eid);
	if (is_in_rankpair_list) {
		NET_ERROR_RETURN(conn, -16, "out_ranking_game:already_in_rankpair_list");
	}

	// player.rank_time not enough
	if (challenge_time <= 0) {
		NET_ERROR_RETURN(conn, -26, "out_ranking_game:challenge_time<0 %d"
		, challenge_time);
	}

	// note:
	// 1. if target offline, ai(-eid) playing ranking game, then donot create new raking game and return
	// 2. if target online, playing ranking game, then donot create new raking game and return

	// 3. if target offline, then challenger play with ai
	// 4. if target online, and playing game but not ranking game, then challenger play with ai

	// 5. else, target online and free, send invite to target
	// XXX 
	// what if challenger is in ranking game and play by ai?


	connect_t *target_conn = get_conn_by_eid(target_eid);
	is_in_ranking_game = __is_in_ranking_game(target_eid);
	is_in_rankpair_list = __is_in_rankpair_list(target_eid);

	if (is_in_ranking_game || is_in_rankpair_list) {
		NET_ERROR_RETURN(conn, -6, "out_ranking_game:target_in_rank_game %d", target_eid);
	}

	if (target_conn != NULL && g_user_room[target_eid] == NULL) {
		// save in g_rankpair_list and send invite
		__add_to_rankpair_list(eid, target_eid);
		net_writeln(conn, "rgame %d %d %d %s %d", eid, target_eid, target_icon, target_alias, RANKING_CHALLENGE_TIMEOUT);
		net_writeln(target_conn, "rchallenge %d %d %d %s %d", target_eid, eid, icon, alias, RANKING_CHALLENGE_TIMEOUT);
		return 0;
	}

	// now, play with ai(-target_eid)
	room_t *proom = create_rank_room(eid, -target_eid, icon, target_icon, alias, target_alias);	
	if (proom == NULL) {
		ERROR_RETURN(-6, "out_rank_game:create_rank_room_fail");
	}
	sprintf(proom->deck[0], "%.400s", deck1);
	sprintf(proom->deck[1], "%.400s", deck2);
	proom->gameid = get_gameid();
	proom->state = ST_GAME; // let out_game() make it a game

	// TODO hp, energy may == 0, logic will use standard hero card hp energy
	proom->hp1 = hp;
	proom->energy1 = energy;
	proom->hp2 = target_hp;
	proom->energy2 = target_energy;

	update_ranking_hero(eid, hero_id, rank, proom->hp1, proom->energy1);
	update_ranking_hero(target_eid, target_hero_id, target_rank, proom->hp2, proom->energy2);

	// init logic
	room_set_hero_info(proom, NULL);
	ret = game_init(proom, proom->seed, 0);
	if (ret < 0) {
		NET_ERROR_PRINT(conn, -66, "game:init %d", ret);
		ret = -66;  // order is important, need to retain 'ret'
		free_room(proom->channel, proom);  // order is important
		return ret;
	}
	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i];
		connect_t * guest_conn = get_conn_by_eid(eid);
		if (guest_conn != NULL) {
			guest_conn->st = ST_GAME;
		}
	}
	room_info_broadcast(proom, 0); // 0 means all

	return 0;

	/*
	// old logic

	// out_ranking_game
	target_status = __check_rank_game_valid(target_eid);
	if (target_status < 0) {
		NET_ERROR_RETURN(conn, -6, "out_ranking_game:target_in_rank_game %d"
		, target_eid);
	}

	// game with ai
	if (target_status == ST_NULL) {
		room_t *proom = create_rank_room(eid
		, -target_eid, icon, target_icon, alias
		, target_alias);	
		if (proom == NULL) {
			ERROR_RETURN(-6, "out_rank_game:create_rank_room_fail");
		}
		sprintf(proom->deck[0], "%.400s", deck1);
		sprintf(proom->deck[1], "%.400s", deck2);
		proom->gameid = get_gameid();
		proom->state = ST_GAME; // let out_game() make it a game

		proom->hp1 = hp;
		proom->energy1 = energy;
		proom->hp2 = target_hp;
		proom->energy2 = target_energy;

		// init logic
		room_set_hero_info(proom, NULL);
		ret = game_init(proom, proom->seed, 0);
		if (ret < 0) {
			NET_ERROR_PRINT(conn, -66, "game:init %d", ret);
			ret = -66;  // order is important, need to retain 'ret'
			free_room(proom->channel, proom);  // order is important
			return ret;
		}
		for (int i=0; i<proom->num_guest; i++) {
			int eid = proom->guest[i];
			connect_t * guest_conn = get_conn_by_eid(eid);
			if (guest_conn != NULL) {
				guest_conn->st = ST_GAME;
			}
		}
		room_info_broadcast(proom, 0); // 0 means all
		return 0;
	}

	__add_to_rankpair_list(eid, target_eid);
	rankpair = __get_rankpair_in_list(eid, false);
	if (rankpair == NULL) {
		ERROR_RETURN(-17
		, "out_ranking_game:get_rankpair_in_list_fail eid[%d] target_eid[%d]"
		, eid, target_eid);
	}

	// game with real player, send invite
	if (target_status == ST_LOGIN) {
		// if oppo player is online, and not in room/game
		// and has not be challenged by others,
		// then normally do challenge

		net_writeln(conn, "rgame %d %d %d %s %d", eid, target_eid
		, target_icon, target_alias, RANKING_CHALLENGE_TIMEOUT);

		// TODO notify target_eid
		conn2 = get_conn_by_eid(target_eid);
		if (conn2 == NULL) {
			ERROR_RETURN(-7, "out_ranking_game:target_eid_off_line");
		}

		net_writeln(conn2, "rchallenge %d %d %d %s %d", target_eid, eid, icon, alias, RANKING_CHALLENGE_TIMEOUT);

	} else {
		rankpair->status = RANKING_CHALLENGE_STATUS_REFUSE;
		ret = __start_ranking_challenge(eid, target_eid, icon, target_icon, alias, target_alias, challenge_time, RANKING_CHALLENGE_STATUS_REFUSE);
	}

	return ret;
	*/
}

// note: conn may NULL
int out_resp_ranking_challenge(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int my_eid;
	int eid, icon, hero_id, hp, energy, rank;
	int challenger_eid, challenger_icon, challenger_hero_id, challenger_hp, challenger_energy, challenger_rank;
	char deck1[EVIL_CARD_MAX+1], deck2[EVIL_CARD_MAX+1];
	char alias[EVIL_ALIAS_MAX + 10], challenger_alias[EVIL_ALIAS_MAX + 10];
	int challenge_time;
	int resp;

	connect_t *player_conn;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
//		if (ret == -1) {
//			// refresh 
//			ranking_targets(conn, "rtarlist", "");
//		}
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer+n, "%d %d %d %d %d %d %s %s %d %d %d %d %d %d %d %d %400s %400s"
	, &eid, &challenger_eid, &challenge_time, &resp
	, &icon, &challenger_icon, alias, challenger_alias
	, &hero_id, &challenger_hero_id, &hp, &challenger_hp
	, &energy, &challenger_energy, &rank, &challenger_rank
	, deck1, deck2);

	if (ret != 18) {
		ERROR_PRINT(ret, "out_buffer[%s]", buffer);
		NET_ERROR_RETURN(conn, -7, "out_resp_ranking_challenge:input_count_invalid %d"
		, ret);
	}

	player_conn = get_conn_by_eid(eid);

	if (challenge_time <= 0) {
		NET_ERROR_RETURN(conn, -17
		, "out_resp_ranking_challenge:challenge_time<0 %d"
		, challenge_time);
	}

	ranking_pair_t *rpt = __get_rankpair_in_list(eid, true);
	if (rpt == NULL) {
		NET_ERROR_RETURN(conn, -27
		, "out_resp_ranking_challenge:not_in_rankpair_list eid[%d] oppo_eid[%d]"
		, eid, challenger_eid);
	}
	if (rpt->eid_challenger != challenger_eid) {
		NET_ERROR_RETURN(conn, -16
		, "out_resp_ranking_challenge:oppo_eid_not_match eid[%d]!=oppo_eid[%d]"
		, challenger_eid, rpt->eid_challenger);
	}

	if (rpt->status != RANKING_CHALLENGE_STATUS_REFUSE
	&& rpt->status != RANKING_CHALLENGE_STATUS_ACCEPT) {
		NET_ERROR_RETURN(conn, -26
		, "out_resp_ranking_challenge:error_status eid[%d] oppo_eid[%d] status[%d]"
		, eid, challenger_eid, rpt->status);
	}

	if (player_conn != NULL) {
		ret = net_writeln(player_conn, "%s %d", cmd, rpt->status);
	}

	if (resp == RANKING_CHALLENGE_STATUS_ACCEPT){
		// rpt->status = RANKING_CHALLENGE_STATUS_ACCEPT;
		my_eid = eid;
	} else {
		// rpt->status = RANKING_CHALLENGE_STATUS_REFUSE;
		my_eid = -eid;
	}

	room_t *proom = create_rank_room(challenger_eid, my_eid
	, challenger_icon, icon, challenger_alias, alias);
	if (proom == NULL) {
		ERROR_RETURN(-6, "out_resp_ranking_challenge:create_rank_room_fail");
	}
	sprintf(proom->deck[0], "%.400s", deck2);
	sprintf(proom->deck[1], "%.400s", deck1);
	proom->gameid = get_gameid();
	proom->state = ST_GAME; // let out_game() make it a game

	proom->hp1 = challenger_hp;
	proom->energy1 = challenger_energy;
	proom->hp2 = hp;
	proom->energy2 = energy;

	update_ranking_hero(challenger_eid, challenger_hero_id, challenger_rank, proom->hp1, proom->energy1);
	update_ranking_hero(eid, hero_id, rank, proom->hp2, proom->energy2);

	// init logic
	connect_t * challenger_conn = get_conn_by_eid(challenger_eid);
	room_set_hero_info(proom, NULL);
	ret = game_init(proom, proom->seed, 0);
	if (ret < 0) {
		NET_ERROR_PRINT(challenger_conn, -66, "game:init %d", ret);
		ret = -66;  // order is important, need to retain 'ret'
		free_room(proom->channel, proom);  // order is important
		return ret;
	}
	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i];
		connect_t * guest_conn = get_conn_by_eid(eid);
		if (guest_conn != NULL) {
			guest_conn->st = ST_GAME;
		}
	}
	room_info_broadcast(proom, 0); // 0 means all
	__remove_from_rankpair_list(challenger_eid, false);
	return 0;

//	// game with ai, no matter 
//	ret = __start_ranking_challenge(challenger_eid, eid, challenger_icon, icon
//	, challenger_alias, alias, challenger_time, resp);
//
//	ret = net_writeln(conn, "%s 0", cmd);
//	return ret;
}

int out_ranking_data(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

//	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_get_ranking_history(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_ranking_challenge(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

//	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_reset_ranking_time(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_check_login(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_list_message(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);

	return 0; 
}

int out_read_message(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);

	return 0; 
}

int out_admin_rank_reward(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);

	return 0; 
}

int out_admin_tower_reward(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);

	return 0; 
}

int out_admin_send_message(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);

	return 0; 
}


int add_card_list(connect_t *conn, int * card_list, int list_size)
{

	int card_id;
	int change;
	evil_user_t &euser = conn->euser;
	
	change = 0;
	for (int i=0; i<list_size; i++) {
		card_id = card_list[i];

		if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
			ERROR_RETURN(-7, "add_card_list:card_id %d", card_id);
		}

		add_card(euser, card_id, 1);

		// update MISSION_SHOP
		change |= shop_mission_update(conn, 1, card_id);

		// update MISSION_CARD
		int cc = 0;
		cc = conn->euser.card[card_id - 1] - '0';
		// DEBUG_PRINT(0, "out_buy_card:cc=%d", cc);
		change |= card_mission_update(conn, cc, card_id);

		// update MISSION_COLLECTION
		int total = 0;
		total = get_collection(euser.card);
		// DEBUG_PRINT(0, "out_buy_card:total=%d", total);
		change |= collection_mission_update(conn, total);

		const card_t *pcard = get_card_by_id(card_id);
		if (pcard == NULL) {return 0;}
		// DEBUG_PRINT(0, "get_card_by_id:%d %d %d %d %s", pcard->id, pcard->star, pcard->job, pcard->cost, pcard->name);
		if (pcard->star >= 4) {
			sys_wchat(1, SYS_WCHAT_GET_CARD, conn->euser.alias, pcard->star, pcard->name);
		}
	}
	change_mission(conn, change); // refresh, mchange

	return 0;
}

int out_lottery(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int code = -1;
	ret = sscanf(buffer, "%d %n", &code, &n);
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}

	// DEBUG_PRINT(0, "out_lottery:buffer=%s", buffer);

	const char * in_ptr;
	int eid;
	int type;
	int times;
	int gold;
	int crystal;
	int card_list[10];
	bzero(card_list, sizeof(card_list));
	int card_id;

	char out_buffer[300];
	char *out_ptr;


	in_ptr = buffer;
	ret = sscanf(in_ptr, "%d %d %d %d %d %n", &eid, &type, &gold, &crystal, &times, &n);

	if (ret != 5) {
		NET_ERROR_RETURN(conn, -95, "out_lottery:out_sscanf %d", ret);
	}

	if (conn->euser.eid != eid) {
		return 0;
	}

	for (int i=0; i<times; i++) {
		in_ptr += n;
		sscanf(in_ptr, "%d %n", &card_id, &n);
		if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
			NET_ERROR_RETURN(conn, -7, "out_lottery:card_id_invalid %d", card_id);
		}
		card_list[i] = card_id;
	}

	in_ptr += n;
	sscanf(in_ptr, "%s", conn->euser.signals);
	// DEBUG_PRINT(0, "euser.signals[%s]", conn->euser.signals);


	// TODO need to update card?
	conn->euser.gold += gold;
	conn->euser.crystal += crystal;

	ret = add_card_list(conn, card_list, times);
	if (ret != 0) {
		NET_ERROR_RETURN(conn, -6, "out_lottery:add_card_list_error %d", eid);
	}

	//DEBUG_PRINT(0, "out_buy_card:euser->gold=%d, euser->crystal=%d", conn->euser.gold, conn->euser.crystal);

	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, "%s %d %d %d %d %d", cmd, eid, type, gold, crystal, times);
	for (int i=0; i<times; i++) {
		out_ptr += sprintf(out_ptr, " %d", card_list[i]);
	}

	// DEBUG_PRINT(0, "out_lottery:out_buffer=%s", out_buffer);

	net_writeln(conn, "%s", out_buffer);
	return 0; 
}

// RET: pshop/rpshop [remain_time] [cost_gold] [refresh_gold] [piece_info1] ... [piece_info6]
// piece_info: [piece_id] [piece_count] [gold] [crystal]
int __send_piece_shop(connect_t *conn, const char* cmd, int cost_gold)
{

	if (conn == NULL)
	{
		return -3;
	}

	// XXX move to design_constant

	time_t now = time(NULL);
	int pcount;
	int gold, crystal;
	int isvip = check_vip(conn);
	evil_piece_shop_t &shop = conn->euser.piece_shop;

	int refresh_gold = min(((shop.refresh_times == 0) ? g_design->constant.pshop_refresh_gold : (2 << (shop.refresh_times-1)) * g_design->constant.pshop_refresh_gold)
	, g_design->constant.pshop_max_refresh_gold);
	time_t remain_time = max(shop.last_time + g_design->constant.pshop_reset_interval - now, 0l);

	net_write_space(conn, "%s %ld %d %d"
	, cmd, remain_time, cost_gold, refresh_gold);

	for (int i = 0; i < MAX_PIECE_SHOP_SLOT; i++)
	{
		design_piece_shop_t &ditem = g_design->piece_shop_list[shop.pid_list[i]];
		pcount = (shop.buy_flag_list[i] == 0) ? ditem.count : -ditem.count;

		gold = ditem.gold;
		crystal = ditem.crystal;
		if (!isvip && i > 3) {	// vip user can buy slot 5, slot 6
			gold = 0;
			crystal = 0;
		}
		net_write_space(conn, "%d %d %d %d"
		, ditem.card_id, pcount
		, gold, crystal);
	}
	net_writeln(conn, "%s", "");

	return 0;
}

// get yesterday 23:59:59
time_t get_yesterday_end(time_t tt)
{
    time_t yesterday;
    struct tm timestruct;

    // second become m_day
    localtime_r(&tt, &timestruct);

    // clear hour:min:second to 0
    timestruct.tm_sec = 0;
    timestruct.tm_min = 0;
    timestruct.tm_hour = 0;

    yesterday = mktime(&timestruct);
    yesterday --;

    localtime_r(&yesterday, &timestruct);
//  printf("yesterday %ld %s\n", yesterday, asctime(&timestruct));

    return yesterday;
}

int update_piece_shop(evil_piece_shop_t &shop)
{
	int ret;
	int change;
	change = 0;

	time_t now = time(NULL);
	time_t yesterday_end = get_yesterday_end(now);
	time_t last_time = shop.last_time;

	if (last_time <= yesterday_end && shop.refresh_times != 0)
	{
		// zero daily referesh piece shop times
		shop.refresh_times = 0;
		change++;
	}

	if (last_time == 0 || now - last_time >= g_design->constant.pshop_reset_interval) {
		// add total refresh piece shop times
		shop.show_times++;
		// get a new random piece shop
		ret = __random_piece_shop(shop.pid_list, shop.show_times
		, MAX_PIECE_SHOP_SLOT);
		ERROR_NEG_RETURN(ret, "update_piece_shop:random_piece_shop");
		// zero all buy flag
		bzero(shop.buy_flag_list, sizeof(shop.buy_flag_list));
		// update refresh piece time
		shop.last_time = now;
		change++;
	}

	return change;
}

int out_piece_shop(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int code = -1;
	int eid;

	evil_piece_shop_t shop;
	ret = sscanf(buffer, "%d %n", &code, &n);
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}
	if (code != conn->euser.eid)
	{
		return 0;
	}
	eid = code;

	ret = sscanf(buffer+n, "%ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, &shop.last_time, &shop.refresh_times, &shop.show_times
	, &shop.pid_list[0], &shop.buy_flag_list[0]
	, &shop.pid_list[1], &shop.buy_flag_list[1]
	, &shop.pid_list[2], &shop.buy_flag_list[2]
	, &shop.pid_list[3], &shop.buy_flag_list[3]
	, &shop.pid_list[4], &shop.buy_flag_list[4]
	, &shop.pid_list[5], &shop.buy_flag_list[5]
	);
	if (ret != 15) {
		NET_ERROR_RETURN(conn, -5, "out_piece_shop:input_error %d", eid);
	}

	int change = 0;
	// check shop need to update
	change = update_piece_shop(shop);
	if (change < 0) {
		ERROR_PRINT(ret, "out_piece_shop:update_piece_shop_fail");
	}

	// if shop update, save to db
	if (change) {
		ret = dbin_write(conn, cmd, DB_UPDATE_PIECE_SHOP
		, "%d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
		, eid, shop.last_time, shop.refresh_times, shop.show_times
		, shop.pid_list[0], shop.buy_flag_list[0]
		, shop.pid_list[1], shop.buy_flag_list[1]
		, shop.pid_list[2], shop.buy_flag_list[2]
		, shop.pid_list[3], shop.buy_flag_list[3]
		, shop.pid_list[4], shop.buy_flag_list[4]
		, shop.pid_list[5], shop.buy_flag_list[5]
		);
		return 0;
	}

	conn->euser.piece_shop = shop;

	// send to client
	__send_piece_shop(conn, cmd, 0);

	return 0;
}

int out_refresh_piece_shop(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int code = -1;
	int eid;
	int gold;

	evil_piece_shop_t shop;
	ret = sscanf(buffer, "%d %n", &code, &n);
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}
	if (code != conn->euser.eid)
	{
		return 0;
	}
	eid = code;

	ret = sscanf(buffer+n, "%ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, &shop.last_time, &shop.refresh_times, &shop.show_times
	, &shop.pid_list[0], &shop.buy_flag_list[0]
	, &shop.pid_list[1], &shop.buy_flag_list[1]
	, &shop.pid_list[2], &shop.buy_flag_list[2]
	, &shop.pid_list[3], &shop.buy_flag_list[3]
	, &shop.pid_list[4], &shop.buy_flag_list[4]
	, &shop.pid_list[5], &shop.buy_flag_list[5]
	, &gold
	);
	if (ret != 16) {
		NET_ERROR_RETURN(conn, -5, "out_piece_shop:input_error %d", eid);
	}

	conn->euser.piece_shop = shop;
	conn->euser.gold += gold;

	// send to client
	__send_piece_shop(conn, cmd, gold);

	return 0;
}

int out_piece_buy(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int n;
	int code = -1;
	int eid;
	int pos;
	int card_id;
	int count;
	int gold;
	int crystal;

	ret = sscanf(buffer, "%d %n", &code, &n);
	if (code < 0) {
		NET_ERROR_RETURN(conn, code, "%s", buffer+n);
	}
	if (code != conn->euser.eid)
	{
		return 0;
	}
	eid = code;

	ret = sscanf(buffer+n, "%d %d %d %d %d"
	, &pos, &card_id, &count, &gold, &crystal);
	if (ret != 5) {
		NET_ERROR_RETURN(conn, -5, "out_buy_piece:input_error %d", eid);
	}

	conn->euser.gold += gold;
	conn->euser.crystal += crystal;
	
	conn->euser.piece_shop.buy_flag_list[pos-1] = 1;

	net_writeln(conn, "%s %s", cmd, buffer+n);

	return 0;
}

int out_exchange_gift(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	const char *in_ptr;
	int eid;
	int gold, crystal;
	int card_count;
	int card_id;
	int card_list[10];
	bzero(card_list, sizeof(card_list));

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	in_ptr = buffer;
	// [gold] [crystal] [card_count] [card_id] [card_id] ...
	ret = sscanf(in_ptr, "%d %d %d %d %n", &eid, &gold, &crystal, &card_count, &n);

	if (ret != 4) {
		NET_ERROR_RETURN(conn, -95, "out_exchange_gift:out_sscanf %d", ret);
	}

	if (conn->euser.eid != eid) {
		return 0;
	}

	for (int i = 0; i < card_count; i++) {
		in_ptr += n;
		sscanf(in_ptr, "%d %n", &card_id, &n);
		if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
			NET_ERROR_RETURN(conn, -7
			, "out_exchange_gift:card_id_invalid %d", card_id);
		}
		card_list[i] = card_id;
	}

	conn->euser.gold += gold;
	conn->euser.crystal += crystal;

	ret = add_card_list(conn, card_list, card_count);
	if (ret != 0) {
		NET_ERROR_RETURN(conn, -6
		, "out_exchange_gift:add_card_list_error %d", eid);
	}

	net_writeln(conn, "%s %s", cmd, buffer);

	return 0; 
}

int out_reset_fight_times(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	connect_t *pconn;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	for (int i=0; i<MAX_CONNECT; i++) {
		pconn = g_connect_list + i;
		// skip free or not-login
		if (pconn->state==STATE_FREE || pconn->euser.eid<=0) {
			continue;
		}
		pconn->euser.fight_ai_time		= FIGHT_AI_MAX_TIME;
		pconn->euser.fight_gold_time	= FIGHT_GOLD_MAX_TIME;
		pconn->euser.fight_crystal_time	= FIGHT_CRYSTAL_MAX_TIME;
	}

	net_writeln(conn, "%s %s", cmd, buffer);

	return 0; 
}



int out_update_gate_pos(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int gate_pos;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d", &eid, &gate_pos);
	if (ret !=2) {
		ERROR_RETURN(-6, "out_update_gate_pos:sscanf_error");
	}

	if (eid <= MAX_AI_EID) {
		BUG_RETURN(-16, "out_update_gate_pos:eid_bug %d", eid);
	}

	if (gate_pos > g_design->design_gate_size) {
		BUG_RETURN(-26, "out_update_gate_pos:gate_pos_bug %d %d", eid, gate_pos);
	}

	connect_t *pconn;
	pconn = get_conn_by_eid(eid);
	if (pconn == NULL) {
		return 0;
	}

	if (pconn->euser.gate_pos < gate_pos) {
		pconn->euser.gate_pos = gate_pos;
	}
	DEBUG_PRINT(0, "out_update_gate_pos:eid=%d gate_pos=%d", eid, gate_pos);

	return 0;
}

int out_fight(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n = 0;  // simple error handling
	const char *ptr;
	int slot;
	char name[EVIL_ALIAS_MAX+5];
	int eid;
	int game_type;
	sscanf(buffer, "%d %n", &ret, &n); // no need to catch ret=sscanf
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	if (conn == NULL) {
		return 0;
	}


	// get slot, it is useless here
	ptr = buffer;
	ret = sscanf(ptr, "%d %d %d %s %n", &game_type, &eid, &slot, name, &n);
	if (ret != 4) {
		NET_ERROR_RETURN(conn, -65, "out_fight:sscanf %d", ret);
	}
	ptr += n;

	if (conn->euser.eid != eid) {
		return 0;
	}

	// core logic: cache the deck in memory
	sprintf(conn->euser.deck, "%.400s", ptr); //buffer+n);
//	printf("out_fight game_type[%d] slot[%d] name[%s] deck[%s]\n"
//	, game_type, slot, name, conn->euser.deck);
	ret = check_deck(conn->euser.deck, conn->euser.card, conn->euser.eid);
	if (ret < 0 ) {
		// this is rather buggy
		NET_ERROR_RETURN(conn, -55, "out_fight:check_deck=%d", ret);
	}
			
	ret = fight_with_player(conn, cmd, eid, game_type);
	if (ret < 0) {
		NET_ERROR_RETURN(conn, -6, "out_fight:fight_with_player_fail=%d", ret);
	}
	return ret;

	// return fight_with_type(conn, cmd, game_type);
}

int out_update_signals(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		BUG_RETURN(ret, "%s", buffer+n);
	}

	return 0;
}

int out_chapter(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	int eid;
	int len;
	int chapter_id;
	int stage_id;
	int solo_id;
	char deck[EVIL_CARD_MAX+1];
	room_t *proom;

	ret = sscanf(buffer, "%d %d %d %d %400s", &eid, &chapter_id, &stage_id, &solo_id, deck);
	if (ret != 5) {
		ERROR_RETURN(-55, "out_chapter:sscanf %d", ret);
	}

	if (eid != conn->euser.eid) {
		return 0;
	}

	// DEBUG_PRINT(0, "out_chapter:eid=%d chapter_id=%d stage_id=%d robot=%d deck=%s", eid, chapter_id, stage_id, solo_id, deck);

	solo_t * solo;
	solo = get_design_solo(solo_id);

	if (solo==NULL) {
		// BUG_PRINT(-17, "out_solo_plus:no_such_solo index=%d", solo_index);
		NET_ERROR_RETURN(conn, -17, "out_chapter:invalid_solo_id %d", solo_id);
	}

	len = sprintf(conn->euser.deck, "%.400s", deck);
	if (len != 400) {
		BUG_PRINT(-7, "out_chapter:deck_len=%d", len);
	}

	proom = create_solo_plus_room(conn, solo, GAME_CHAPTER);
	if (proom == NULL) {
		// BUG_PRINT(-66, "out_chapter:create_solo_plus_room_fail %d", solo_id);
		NET_ERROR_RETURN(conn, -66, "out_chapter:create_solo_plus_room_fail %d", solo_id);
	}

	proom->chapter_id = chapter_id;
	proom->stage_id = stage_id;
	proom->solo_id = solo_id;

	ret = solo_plus_init(proom, proom->seed);
	if (ret != 0) {
		force_room_clean(proom);
		NET_ERROR_RETURN(conn, -76, "out_chapter:solo_plus_init %d %d", solo_id, conn->euser.eid);
	}

	conn->st = ST_GAME; // set me as GAME state
	room_info_broadcast(proom, 0); // 0 means all
	return ST_GAME;
	
	/*
	// old logic
	room_t *proom = new_room(CHANNEL_SOLO);
	proom->num_guest = 2;
	proom->guest[0] = eid;
	proom->guest[1] = solo->id;
	proom->rating[0] = conn->euser.rating;
	proom->rating[1] = 1000;
	strcpy(proom->alias[0], conn->euser.alias);
	strcpy(proom->alias[1], solo->alias);
	proom->icon[0] = conn->euser.icon;
	proom->icon[1] = 0;
	int hero_id = 0;
	sscanf(solo->deck, "%d", &hero_id);
	proom->icon[1] = hero_id;
	proom->lv[0] = conn->euser.lv;
	proom->lv[1] = 1;
	proom->game_type = GAME_CHAPTER;
	sprintf(proom->title, "%s~VS~%s", proom->alias[0], proom->alias[1]);

	// fixed random seed (if >0)
	proom->seed = 0;
	conn->room = proom;  // for out_game() 
	// re-conn logic ?  
	g_user_room[eid] = proom;
	// eid2 is always AI, so we don't need to assign to g_user_room[eid]

	proom->gameid = get_gameid();
	proom->state = ST_GAME;
	int my_hero = 0;
	sscanf(solo->my_deck, "%d", &my_hero);
	if (my_hero == 0) {
		// use real deck
		sprintf(proom->deck[0], "%.400s", deck);
		my_hero = get_hero(proom->deck[0]);
	} else {
		// use solo my_deck
		sprintf(proom->deck[0], "%.400s", solo->my_deck);
	}

	evil_hero_data_t &hero_data = conn->euser.hero_data_list[my_hero];
	int hp = hero_data.hero.hp;
	int energy = hero_data.hero.energy;
	if (hero_data.hero.hero_id == 0) {
		// player has no hero in design.design_solo.my_hero
		WARN_PRINT(-7, "out_chapter:hero_data_error hero_id %d", my_hero);
		design_hero_t &dhero = g_design->hero_list[my_hero];
		if (dhero.hero_id == 0) {
			BUG_PRINT(-17, "out_chapter:no_such_design_hero hero_id %d"
			, my_hero);
		}
		hp = dhero.hp;
		energy = dhero.energy;
	}

	sprintf(proom->deck[1], "%.400s", solo->deck);
	proom->solo_type = solo->solo_type;
	proom->ai_max_ally = solo->ai_max_ally;
	proom->hp2 = solo->max_hp;
	proom->solo_hero = hero_id;
	proom->hp1 = hp;
	proom->energy1 = energy;
	proom->solo_start_side = solo->start_side;
	strcpy(proom->type_list, solo->type_list);
	ret = solo_plus_init(proom, proom->seed);

	proom->chapter_id = chapter_id;
	proom->stage_id = stage_id;

	conn->st = ST_GAME; // set me as GAME state
	room_info_broadcast(proom, 0); // 0 means all
	return ST_GAME;
	*/
}

int __reset_chapter(connect_t *conn)
{
	int change = 0;
	if (conn == NULL) {
		return 0;
	}

	if (conn->euser.flag_load_chapter == 0) {
		// user not load chapter data
		return 0;
	}
	for (int i=1; i<=g_design->design_chapter_size; i++) {
		evil_chapter_data_t & chapter = conn->euser.chapter_list[i];
		design_chapter_t &dchapter = g_design->design_chapter_list[i];
		if (dchapter.chapter_id == 0) { continue;}	
		int size = strlen(chapter.data);

		if (chapter.id == 0 || size == 0) {
			// user chapter data is empty
			chapter.id = dchapter.chapter_id;
			memset(chapter.data, CHAPTER_DATA_LOCK, sizeof(char) * dchapter.stage_size);
			if (dchapter.chapter_id == 1) {
				// need to init first chapter
				chapter.data[0] = CHAPTER_DATA_START;
			}
			chapter.data[dchapter.stage_size] = '\0';
			change++;
			continue;
		}

		if (size > dchapter.stage_size) {
			// user chapter data longer then design
			chapter.data[dchapter.stage_size] = '\0';
			change++;
			continue;
		}

		if (size < dchapter.stage_size) {
			// user chapter data short then design
			int offset = dchapter.stage_size - size;
			memset(chapter.data+size, CHAPTER_DATA_LOCK, sizeof(char) * offset);
			chapter.data[dchapter.stage_size] = '\0';
			change++;
			continue;
		}
	}
	return change;
}

int __update_chapter(connect_t *conn, int chapter_id, int stage_id, int star)
{
	int change = 0;
	if (conn == NULL) {
		return 0;
	}
	if (chapter_id <= 0 || stage_id <= 0) {
		return 0;
	}
	if (chapter_id > g_design->design_chapter_size) {
		return 0;
	}
	if (star != CHAPTER_DATA_STATUS_STAR_0
	&& star != CHAPTER_DATA_STATUS_STAR_1
	&& star != CHAPTER_DATA_STATUS_STAR_2
	&& star != CHAPTER_DATA_STATUS_STAR_3) {
		BUG_PRINT(-6, "__update_chapter:invalid_star %d", star);
		return 0;
	}

	evil_chapter_data_t & chapter = conn->euser.chapter_list[chapter_id];
	design_chapter_t & dchapter = g_design->design_chapter_list[chapter_id];

	if (conn->euser.flag_load_chapter == 0) {
		// user chapter data is empty
		dbin_write(conn, "chapter_data", DB_GET_CHAPTER, "%d %d %d %d", conn->euser.eid, chapter_id, stage_id, star);
		return 0;
	}

	if (stage_id > dchapter.stage_size) {
		return 0;
	}

	int now_star = chapter.data[stage_id-1] - CHAPTER_DATA_STAR_0;
	INFO_PRINT(0, "__update_chapter:now_star=%d", now_star);
	if (now_star != CHAPTER_DATA_STATUS_STAR_0
	&& now_star != CHAPTER_DATA_STATUS_STAR_1
	&& now_star != CHAPTER_DATA_STATUS_STAR_2
	&& now_star != CHAPTER_DATA_STATUS_STAR_3
	&& now_star != CHAPTER_DATA_STATUS_START
	&& now_star != CHAPTER_DATA_STATUS_LOCK) {
		BUG_PRINT(-7, "__update_chapter:star_bug %d %d %d", now_star, chapter_id, conn->euser.eid);
		// over write it
		chapter.data[stage_id-1] = CHAPTER_DATA_STATUS_START;
		now_star = chapter.data[stage_id-1] - CHAPTER_DATA_STAR_0;
		change = 1;
	}

	if (now_star == CHAPTER_DATA_STATUS_LOCK) {
		ERROR_PRINT(-16, "__update_chapter:stage_is_lock %d %d %d", chapter_id, stage_id, conn->euser.eid);
		return change;
	}

	// update stage star
	if (star > now_star || now_star == CHAPTER_DATA_STATUS_START) {
		chapter.data[stage_id-1] = CHAPTER_DATA_STAR_0 + star;
		change = 1;
	}

	// maybe need to unlock next stage
	if (stage_id < dchapter.stage_size && chapter.data[stage_id] == CHAPTER_DATA_LOCK) {
		chapter.data[stage_id] = CHAPTER_DATA_START;
		change = 1;
	}

	if (stage_id != dchapter.stage_size) {
		// not last stage
		return change;
	}

	if (chapter_id == g_design->design_chapter_size) {
		// this is last chapter 
		return change;
	}

	// maybe need to unlock next chapter
	int next_chapter_id = chapter_id + 1;
	evil_chapter_data_t & next_chapter = conn->euser.chapter_list[next_chapter_id];
	if (next_chapter.data[0] == CHAPTER_DATA_LOCK) {
		next_chapter.data[0] = CHAPTER_DATA_START;
		change = 1;
	}

	return change;
}

int __save_chapter(connect_t *conn, int change)
{
	if (conn == NULL) {
		return 0;
	}

	if (change == 0) {
		return 0;
	}

	char buffer[DB_BUFFER_MAX];
	char *ptr;
	ptr = buffer;
	ptr += sprintf(ptr, "%d %d", conn->euser.eid, g_design->design_chapter_size);
	for (int i=1; i<=g_design->design_chapter_size; i++) {
		evil_chapter_data_t &chapter = conn->euser.chapter_list[i];
		ptr += sprintf(ptr, " %d %s", chapter.id, chapter.data);
	}

	DEBUG_PRINT(0, "__save_chapter:buffer=[%s]", buffer);

	dbin_write(conn, "chapter_replace", DB_REPLACE_CHAPTER, "%s", buffer);

	return 0;
}

int refresh_chapter_data(connect_t *conn, int chapter_id, int stage_id, int star)
{

	int change = 0;
	change |= __reset_chapter(conn);
	// if user chapter data is empty , __update_chapter() will get data in db 
	change |= __update_chapter(conn, chapter_id, stage_id, star);
	__save_chapter(conn, change);

	return change;
}


int __get_chapter_pos(connect_t * conn) 
{
	int pos = 0;
	if (conn == NULL) {
		return 0;
	}
	if (conn->euser.flag_load_chapter == 0) {
		return 0;
	}

	for (int i=1; i<=g_design->design_chapter_size; i++) {
		evil_chapter_data_t & chapter = conn->euser.chapter_list[i];	
		DEBUG_PRINT(0, "__get_chapter_pos:data[0]=%c", chapter.data[0]);
		if (chapter.data[0] == CHAPTER_DATA_LOCK) {
			return pos;
		}
		pos++;
	}
	DEBUG_PRINT(0, "__get_chapter_pos:pos=%d", pos);
	return pos;
}

// if stage_id, star != 0, means this is comes from win_game() update chapter data, then no need send to client
int out_chapter_data(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	const char *in;
	int eid;
	int chapter_id;
	int stage_id;
	int star;
	int size;
	int mchange = 0;

	in = buffer;
	ret = sscanf(in, "%d %d %d %d %d %n", &eid, &chapter_id, &stage_id, &star, &size, &n);
	if (ret != 5) {
		ERROR_RETURN(-55, "out_chapter_data:sscanf %d", ret);
	}

	if (conn == NULL) {
		return 0;
	}

	if (eid != conn->euser.eid) {
		return 0;
	}

	if (chapter_id > g_design->design_chapter_size || chapter_id < 0) {
		NET_ERROR_RETURN(conn, -15, "out_chapter_data:no_such_chapter");
	}

	int id;
	for (int i=0; i<size; i++) {
		in += n;
		ret = sscanf(in, "%d %n", &id, &n);
		if (ret != 1) {
			NET_ERROR_RETURN(conn, -25, "out_chapter_data:scan_id_fail %d", eid);
		}
		if (id <= 0 || id > MAX_CHAPTER) {
			NET_ERROR_RETURN(conn, -66, "out_chapter_data:invalid_id %d %d", id, eid);
		}

		evil_chapter_data_t & chapter = conn->euser.chapter_list[id];
		chapter.id = id;
		in += n;
		ret = sscanf(in, "%20s %n", chapter.data, &n);
		if (ret != 1) {
			NET_ERROR_RETURN(conn, -35, "out_chapter_data:scan_data_fail %d", eid);
		}
	}

	conn->euser.flag_load_chapter = 1;

	refresh_chapter_data(conn, chapter_id, stage_id, star);
	if (chapter_id == 0) {
		chapter_id = __get_chapter_pos(conn);
	}
	change_mission(conn, mchange); // refresh, mchange

	if (stage_id == 0) {
		evil_chapter_data_t * chapter = &conn->euser.chapter_list[chapter_id];
		design_chapter_t * dchapter = &g_design->design_chapter_list[chapter_id];
		net_write_space(conn, "%s %d %d %s %d"
		, cmd, chapter_id
		, g_design->design_chapter_size
		, dchapter->name, dchapter->stage_size);

		for (int i=1; i<=dchapter->stage_size; i++) {
			design_chapter_stage_t * stage = dchapter->stage_list + i;
			if (stage->stage_id <= 0) {continue;};
			net_write_space(conn, "%d", stage->stage_id);
		}
		net_writeln(conn, "%s", chapter->data);
	}

	/*
	net_write_space(conn, "%s %d %d %s %d"
	, cmd, chapter_id
	, g_design->design_chapter_size
	, chapter->name, chapter->stage_size);
	*/
	mchange = chapter_mission_update(conn);
	change_mission(conn, mchange); // refresh, mchange

	return 0;
}

int out_chapter_update_data(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		BUG_RETURN(ret, "%s", buffer+n);
	}

	int mchange = chapter_mission_update(conn);
	change_mission(conn, mchange); // refresh, mchange
	/*
	int eid;
	int chapter_id;
	int stage_id;
	int stage_size;
	char stage_data[MAX_CHAPTER_STAGE+1];
	int mchange = 0;
	ret = sscanf(buffer, "%d %d %d %d %s", &eid, &chapter_id, &stage_id
	, &stage_size, stage_data);
	if (ret != 5) {
		BUG_RETURN(-55, "out_chapter_update_data:sscanf %d", ret);
	}
	if (eid != conn->euser.eid) {
		return 0;
	}

	evil_user_t *euser = &conn->euser;
	// update chapter_pos
	if (euser->chapter_pos < chapter_id) {
		euser->chapter_pos = chapter_id;
	}

	for (int i = 1; i <= stage_size; i++)
	{
		mchange |= chapter_stage_mission_update(conn, chapter_id
		, i, stage_data[i-1]);
	}
	mchange |= chapter_mission_update(conn, chapter_id, stage_size
	, stage_data);
	change_mission(conn, mchange); // refresh, mchange
	*/

	return 0;
}

int out_chapter_reward(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		BUG_RETURN(ret, "%s", buffer+n);
	}

	int eid;
	int chapter_id;
	int stage_id;
	int reward;
	int count;
	int ext1;
	int ext2;

	ret = sscanf(buffer, "%d %d %d %d %d %d %d", &eid, &chapter_id, &stage_id, &reward, &count, &ext1, &ext2);
	if (ret != 7) {
		ERROR_RETURN(-55, "out_chapter_reward:sscanf %d", ret);
	}

	if (eid != conn->euser.eid) {
		return 0;
	}

	switch (reward) {
	case CHAPTER_REWARD_GOLD:
		conn->euser.gold += count;
		break;
	case CHAPTER_REWARD_CRYSTAL:
		conn->euser.crystal += count;
		break;
	case CHAPTER_REWARD_PIECE:
		break;
	case CHAPTER_REWARD_CARD:
		add_card(conn->euser, count, 1);
		break;
	case CHAPTER_REWARD_EXP:
		conn->euser.lv = ext1;
		conn->euser.exp = ext2;
		break;
	case CHAPTER_REWARD_POWER:
		break;
	default:
		break;
	}

	// conn->euser.reward_chapter = 0;
	// conn->euser.reward_stage = 0;

	// net_writeln(conn, "chapter_reward 1 %d %d %d %d", chapter_id, stage_id, reward, count);

	return 0;
}

int out_load_hero_deck(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		BUG_RETURN(ret, "%s", buffer+n);
	}

	int eid;
	int hero_id;
	int slot_id;
	char slot[EVIL_CARD_MAX+1];
	ret = sscanf(buffer, "%d %d %400s", &eid, &slot_id, slot);
	if (ret != 3) {
		ERROR_RETURN(-55, "out_load_hero_deck:sscanf %d", ret);
	}

	if (eid != conn->euser.eid) {
		return 0;
	}

	hero_id = get_hero(slot); // ???
	if (hero_id < 0) {
		BUG_PRINT(-7, "out_load_hero_deck:hero_id");
		hero_id = 0;  // fail safe use 0
	}
	evil_hero_data_t &hero_data = conn->euser.hero_data_list[hero_id];
	int hp = hero_data.hero.hp;
	int energy = hero_data.hero.energy;
	if (hero_data.hero.hero_id == 0) {
		// player has no hero in design.design_solo.my_hero
		WARN_PRINT(-17, "out_load_hero_deck:hero_data_error hero_id %d", hero_id);
		design_hero_t &dhero = g_design->hero_list[hero_id];
		if (dhero.hero_id == 0) {
			BUG_PRINT(-27, "out_load_hero_deck:no_such_design_hero hero_id %d"
			, hero_id);
		}
		hp = dhero.hp;
		energy = dhero.energy;
	}
	sprintf(conn->euser.deck, "%.400s", slot);

	net_writeln(conn, "%s %d %d %d %d %s", cmd, hero_id, slot_id
	, hp, energy, slot);

	return 0;
}


int out_list_hero_slot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int hero_id;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %n", &eid, &hero_id, &n);
	if (ret != 2) {
		ERROR_RETURN(-55, "out_list_hero_slot:sscanf %d", ret);
	}
	if (eid != conn->euser.eid) {
		return 0;
	}
	if (hero_id < 1 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -2, "out_list_hero_slot:hero_id_out_bound %d"
		, hero_id);
	}

	if (g_design->hero_slot_list[hero_id].id == 0) {
		NET_ERROR_RETURN(conn, -7
		, "out_list_hero_slot:design_hero_slot_null %d"
		, hero_id);
	}
	int slot_percent = __get_recommand_card_percent(conn->euser.card
	, g_design->hero_slot_list[hero_id].deck);
	DEBUG_PRINT(0, "out_list_hero_slot:slot_percent[%d]", slot_percent);

	net_writeln(conn, "%s %d %d %s", cmd, hero_id, slot_percent, buffer + n);
	return 0;
}

int out_get_hero_slot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int hero_id;
	int slot_id;
	int percent = 101; // only for client display, no real logic in server

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %d %n", &eid, &hero_id, &slot_id, &n);
	if (ret != 3) {
		ERROR_RETURN(-55, "out_get_hero_slot:sscanf %d", ret);
	}
	if (eid != conn->euser.eid) {
		return 0;
	}

	net_writeln(conn, "%s %d %d %d %s", cmd, hero_id, slot_id, percent, buffer + n);
	return 0;
}

int out_insert_hero_slot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %n", &eid, &n);
	if (ret != 1) {
		ERROR_RETURN(-55, "out_insert_hero_slot:sscanf %d", ret);
	}
	if (eid != conn->euser.eid) {
		return 0;
	}

	net_writeln(conn, "%s %s", cmd, buffer + n);
	return 0;
}

int out_update_hero_slot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int hero_id;
	int slot_id;
	int update_hero_flag;
	char slot[EVIL_CARD_MAX+1];

	ret = sscanf(buffer, "%d %n", &eid, &n);
	if (ret != 1) {
		ERROR_RETURN(-55, "out_update_hero_slot:sscanf %d", ret);
	}
	if (eid != conn->euser.eid) {
		return 0;
	}
	ret = sscanf(buffer+n, "%d %d %d %400s", &hero_id, &slot_id
	, &update_hero_flag, slot);
	if (ret != 4) {
		ERROR_RETURN(-55, "out_update_hero_slot:sscanf %d", ret);
	}
	if (update_hero_flag)
	{
		sprintf(conn->euser.deck, "%.400s", slot);
	}

	net_writeln(conn, "%s %d %d", cmd, hero_id, slot_id);
	return 0;
}

int out_choose_hero_slot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int hero_id;
	int slot_id;
	char deck[EVIL_CARD_MAX+1];

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %d %400s", &eid, &hero_id, &slot_id, deck);
	if (ret != 4) {
		ERROR_RETURN(-55, "out_choose_hero_slot:sscanf %d", ret);
	}
	if (eid != conn->euser.eid) {
		return 0;
	}

	sprintf(conn->euser.deck, "%.400s", deck);

	net_writeln(conn, "%s %d %d", cmd, hero_id, slot_id);
	return 0;
}



int out_get_hero_list(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %n", &eid, &n);
	if (ret != 1) {
		ERROR_RETURN(-55, "out_get_hero_list:sscanf %d", ret);
	}
	if (eid != conn->euser.eid) {
		return 0;
	}

	net_writeln(conn, "%s %s", cmd, buffer + n);
	return 0;
}

int out_load_hero_data(connect_t * conn, const char *cmd, const char *buffer)
{
	int eid;
	int count;
	int change = 0;
	int ret = -75, n = 0;  // simple error handling
	const char * ptr;
	evil_hero_t hero;
	int mis_count;
	evil_hero_mission_t mis;

	sscanf(buffer, "%d %n", &ret, &n); // no need to catch ret=sscanf
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	eid = ret;
	if (eid != get_eid(conn)) {
		// normal if the user disconnect and conn is re-used
		WARN_PRINT(-9, "out_load_hero_data:eid_mismatch %d %d"
		, get_eid(conn), eid);
		return 0;
	}
	ptr = buffer + n;
	count = -1;
	ret = sscanf(ptr, "%d %n", &count, &n);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -85, "out_load_hero_data:scanf_count_error");
	}
	if (count < 0) {
		NET_ERROR_RETURN(conn, -95, "out_load_hero_data:count_error");
	}
	ptr += n;

	bzero(conn->euser.hero_data_list, sizeof(conn->euser.hero_data_list));
	// count = 0 fall through
	for (int i=0; i<count; i++) {
		ret = sscanf(ptr, OUT_LOAD_HERO_DATA_SCAN
			, &hero.hero_id, &hero.hp
			, &hero.energy, &mis_count, &n);
		if (ret != 4) {
			NET_ERROR_RETURN(conn, -95, "hero_data_sscanf %d", ret);
		}
		ptr += n;
		// mission-fix
		conn->euser.hero_data_list[hero.hero_id].hero = hero; // memory copy
		for (int mi = 0; mi < mis_count; mi++) {
			ret = sscanf(ptr, OUT_LOAD_HERO_MISSION_SCAN
				, &mis.mission_id, &mis.status, &mis.n1, &n);
			if (ret != 3) {
				NET_ERROR_RETURN(conn, -95, "hero_mission_sscanf %d", ret);
			}
			ptr += n;
			mis.hero_id = hero.hero_id;
			conn->euser.hero_data_list[mis.hero_id].mission_list[mis.mission_id] = mis;
		}
	}

	change |= hero_hp_mission_update(conn);
	change_mission(conn, change);
	
	return 0;
}


int out_submit_hero_mission(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int hero_id;
	int mission_id;
	int hp;
	int energy;
	const char *ptr;
//	char tmp_buffer[BUFFER_SIZE+1];

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	eid = ret;
	if (eid != conn->euser.eid) {
		return 0;
	}

	ptr = buffer + n;
//	ret = sscanf(ptr, "%d %d %n", &hero_id, &mission_id, &n);
	ret = sscanf(ptr, "%d %d %d %d", &hero_id, &mission_id, &hp, &energy);
	if (ret != 4) {
		ERROR_RETURN(-65, "out_submit_hero_mission:sscanf %d", ret);
	}

//	ptr += n;
//	ret = out_load_hero_data(conn, cmd, ptr);
//	ERROR_RETURN(-6, "out_submit_hero_mission:load_hero_mission_fail %d", ret);

	evil_hero_data_t &hero_data = conn->euser.hero_data_list[hero_id];
	hero_data.hero.hp = hp;
	hero_data.hero.energy = energy;

	evil_hero_mission_t &mission = hero_data.mission_list[mission_id];
	mission.status = MISSION_STATUS_FINISH;

	net_writeln(conn, "%s %d %d %d %d", cmd, hero_id, mission_id
	, hero_data.hero.hp, hero_data.hero.energy);

	char hero_buffer[BUFFER_SIZE+1];
	bzero(hero_buffer, sizeof(hero_buffer));
	char *hero_ptr = hero_buffer;
	for (int i = 0; i <HERO_MAX; i++) {
		design_hero_t *hero = g_design->hero_list + i + 1;
		hero_ptr += sprintf(hero_ptr, " %d %d", hero->hp, hero->energy);
	}
	ret = dbin_write(conn, cmd, DB_LOAD_HERO_DATA, IN_LOAD_HERO_DATA_PRINT
	, conn->euser.eid, hero_buffer);

//	sprintf(tmp_buffer, "%d", hero_id);
//	ret = get_hero_mission_list(conn, "hero_mlist", tmp_buffer);

	int change = 0;
	change |= hero_hp_mission_update(conn);
	change_mission(conn, change);

	return 0;
}

int out_update_hero_mission_list(connect_t * conn, const char *cmd, const char *buffer) {
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		ERROR_RETURN(ret, "%s", buffer+n);
	}

	return 0; // not yet
}

int out_load_card_piece(connect_t * conn, const char *cmd, const char *buffer) {
	int ret = -75, n=0;
//	int eid;
	int card_id;
	int size;
	int count;
	int piece_count;
	char upiece_buffer[10];
	char *ptr;
	char piece_buffer[BUFFER_SIZE+1];
	char tmp_buffer[BUFFER_SIZE+1];
	bzero(upiece_buffer, sizeof(upiece_buffer));
	bzero(piece_buffer, sizeof(piece_buffer));
	bzero(tmp_buffer, sizeof(tmp_buffer));

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		ERROR_RETURN(ret, "%s", buffer+n);
	}
//	eid = ret;

	ret = sscanf(buffer+n, "%s %d %d", piece_buffer, &card_id, &size);
	if (ret != 3) {
		DEBUG_PRINT(1, "out_load_card_piece:buffer[%s]", buffer);
		DEBUG_PRINT(1, "out_load_card_piece:buffer+n[%s]", buffer+n);
		ERROR_RETURN(-65, "out_load_card_piece:sscanf %d", ret);
	}

	int max_size = 50; // base on BUFFER_SIZE / sizeof(card_info)
	if (size > max_size) {
		size = max_size;
	}

	count = 0;
	ptr = tmp_buffer;
	for (int i=card_id; i < EVIL_CARD_MAX; i++) {
		design_merge_t &merge = g_design->merge_list[i];
		if (merge.card_id == 0) {
			continue;
		}

		upiece_buffer[0] = piece_buffer[(i-1) * 2];
		upiece_buffer[1] = piece_buffer[i * 2 - 1];
		piece_count = strtol_safe(upiece_buffer, 0);

		ptr += sprintf(ptr, "%d %d %d %d %d ", merge.card_id
		, piece_count, merge.count, merge.gold, merge.crystal);
		count++;
		if (count >= size) {
			break;
		}
	}
	net_writeln(conn, "%s %d %s", cmd, count, tmp_buffer);

	return 0; // not yet
}


int out_get_daily_login(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	const char* ptr;
	int has_get_reward;
	evil_daily_login_t daily_login;
	char tmp_buffer[BUFFER_SIZE+1];
	bzero(&daily_login, sizeof(daily_login));
	bzero(tmp_buffer, sizeof(tmp_buffer));

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ptr = buffer+n;
	ret = sscanf(ptr, "%d %d %n", &(daily_login.log_day)
	, &(has_get_reward), &n);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -65, "out_get_daily_login:sscanf %d", ret);
	}

	ptr += n;
	for (int i = 1; i <= MAX_DAILY_LOGIN; i++) {
		ret = sscanf(ptr, "%d %n", &(daily_login.reward_day[i]), &n);
		if (ret != 1) {
			NET_ERROR_RETURN(conn, -75, "out_get_daily_login:day_sscanf %d", ret);
		}
		ptr += n;
	}
	daily_login.load_from_db = 1;
	conn->euser.daily_login = daily_login;


	__get_daily_login_output_data(tmp_buffer, daily_login, has_get_reward);

	net_writeln(conn, "%s %s", cmd, tmp_buffer);

	return 0;
}

int out_get_daily_reward(connect_t * conn, const char *cmd, const char *buffer) {
	int ret = -75, n=0;
	const char *ptr;
	int limit_day;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	evil_daily_login_t &daily_login = conn->euser.daily_login;
	ptr = buffer+n;
	ret = sscanf(ptr, "%d", &(daily_login.log_day));
	limit_day = (daily_login.log_day > MAX_DAILY_LOGIN) ? MAX_DAILY_LOGIN
	: daily_login.log_day;
	daily_login.reward_day[limit_day]++;

	net_writeln(conn, "%s %s", cmd, buffer + n);
	return 0; // not yet
}

int out_admin_add_robot(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0; 
}

int out_init_arena(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}

int out_arena_top_list(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0; 
}

int out_arena_target(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int rank;
	design_arena_reward_t *target_reward = NULL;

	// out_buffer = [my_rank] [reward_gold] [reward_crystal]
	//				[buy_times_count] [buy_times_crystal] 
	//				[has_reward] [reward_time_offset] [arena_times] 
	//				[size] [arena_info1] [arena_info2] ...
	// arena_info = [rank] [eid] [icon] [lv] [win_rate(double)] [alias]
	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	rank = ret;
	for (int i = 1; i <= g_design->max_arena_reward_level; i++)
	{
		if ((rank >= g_design->arena_reward_list[i].start && rank <= g_design->arena_reward_list[i].end) 
		|| (rank >= g_design->arena_reward_list[i].start && g_design->arena_reward_list[i].end == 0)) {
			target_reward = g_design->arena_reward_list + i;
			break;
		}
	}
	if (target_reward == NULL) {
		NET_ERROR_RETURN(conn, -7, "out_arean_target:reward_data_null");
	}

	net_writeln(conn, "%s %d %d %d %d %d %s", cmd
	, rank, target_reward->gold, target_reward->crystal
	, ARENA_TIMES_BUY_COUNT, ARENA_TIMES_BUY_CRYSTAL
	, buffer+n);
	return 0; 
}


int __add_to_arena_room(room_t *proom, int eid, int icon, const char * name)
{
	if ( proom->num_guest >= MAX_GUEST ) {
		return -2;
	}
	proom->guest[ proom->num_guest ] = eid;  // euser.eid
	const char * alias = name;
	if (strlen(alias)==0) {
		alias = VISITOR_ALIAS;
	}
	strcpy(proom->alias[ proom->num_guest ], alias);
	proom->icon[ proom->num_guest] = icon;
	proom->lv[ proom->num_guest] = 1;
	proom->num_guest ++;

	// fill up "conn" 
	connect_t * conn;
	conn = get_conn_by_eid(eid);
	if (conn != NULL) {
		conn->room = proom;
		conn->st = proom->state;
	}
	// re-conn logic
	if (eid < 0 || eid > MAX_AI_EID) {
		g_user_room[eid] = proom;
	}
	return 0;
}

room_t * create_arena_room(int eid1, int eid2, int rank1, int rank2, int icon1, int icon2, const char * alias1, const char * alias2)
{
	int ret;
	room_t *proom = NULL;
	proom = new_room(CHANNEL_QUICK);
	if (proom==NULL) {
		ERROR_PRINT(-7, "create_arena_room:bug");
		return NULL;
	}
	proom->game_type = GAME_ARENA;

	// add player to proom
	ret = __add_to_arena_room(proom, eid1, icon1, alias1);
	if (ret < 0) {
		ERROR_PRINT(-16, "create_arena_room:eid1 %d", eid1);
		free_room(proom->channel, proom);
		return NULL;
	}

	ret = __add_to_arena_room(proom, eid2, icon2, alias2);
	if (ret < 0) {
		ERROR_PRINT(-26, "create_arena_room:eid2 %d", eid2);
		free_room(proom->channel, proom);
		return NULL;
	}
	sprintf(proom->title, "%s~VS~%s", alias1, alias2);

	if (rank1 <= ARENA_AUTO_RANK || rank2 <= ARENA_AUTO_RANK) {
		proom->auto_battle = GAME_PLAY_AUTO;
	}

	return proom;
}

// ret = 1: player is playing ranking game or ai is playing ranking game
int __is_in_arena_game(int eid)
{
	room_t *proom;
	proom = g_user_room[eid];

	// player in ranking game
	if (proom != NULL && proom->game_type == GAME_ARENA) {
		return 1;
	}

	// player' ai in ranking game
	proom = g_user_room[-eid];
	if (proom != NULL && proom->game_type == GAME_ARENA) {
		return 1;
	}

	return 0;
}

int out_arena_game(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	int eid, icon;
	int target_eid, target_icon;
	int hero_id, hp, energy, rank;
	int target_hero_id, target_hp, target_energy, target_rank;
	char deck1[EVIL_CARD_MAX+1], deck2[EVIL_CARD_MAX+1];
	char alias[EVIL_ALIAS_MAX + 10], target_alias[EVIL_ALIAS_MAX + 10];

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer+n, "%d %d %d %d %s %s %d %d %d %d %d %d %d %d %400s %400s"
	, &eid, &target_eid
	, &icon, &target_icon
	, alias, target_alias
	, &hero_id, &target_hero_id
	, &hp, &target_hp
	, &energy, &target_energy
	, &rank, &target_rank
	, deck1, deck2);

	if (ret != 16) {
		ERROR_PRINT(ret, "out_buffer[%s]", buffer);
		NET_ERROR_RETURN(conn, -7, "out_arena_game:input_count_invalid %d", ret);
	}
	
	// eid mismatch
	if (eid != get_eid(conn)) {
		return 0;
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "out_arena_game:already_in_room");
	}

	if (__is_in_arena_game(target_eid)) {
		NET_ERROR_RETURN(conn, -16, "out_arena_game:target_in_room");
	}

	room_t *proom = create_arena_room(eid, -target_eid, rank, target_rank, icon, target_icon, alias, target_alias);	
	if (proom == NULL) {
		ERROR_RETURN(-6, "out_rank_game:create_rank_room_fail");
	}
	sprintf(proom->deck[0], "%.400s", deck1);
	sprintf(proom->deck[1], "%.400s", deck2);
	proom->gameid = get_gameid();
	proom->state = ST_GAME; // let out_game() make it a game

	proom->hp1 = hp;
	proom->energy1 = energy;
	proom->hp2 = target_hp;
	proom->energy2 = target_energy;

	// init logic
	room_set_hero_info(proom, NULL);
	ret = game_init(proom, proom->seed, 0);
	if (ret < 0) {
		NET_ERROR_PRINT(conn, -66, "game:init %d", ret);
		ret = -66;  // order is important, need to retain 'ret'
		free_room(proom->channel, proom);  // order is important
		return ret;
	}
	for (int i=0; i<proom->num_guest; i++) {
		int eid = proom->guest[i];
		connect_t * guest_conn = get_conn_by_eid(eid);
		if (guest_conn != NULL) {
			guest_conn->st = ST_GAME;
		}
	}
	room_info_broadcast(proom, 0); // 0 means all

	dbin_write(conn, "arenatimes", DB_UPDATE_ARENA_TIMES, "%d %d %d %d", eid, -1, 0, 0);
	return 0; 
}

int out_update_arena_times(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int offset;
	int gold;
	int crystal;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %d %d", &eid, &offset, &gold, &crystal);
	if (ret != 4) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	
	// eid mismatch
	if (eid != get_eid(conn)) {
		return 0;
	}

	conn->euser.gold += gold;
	conn->euser.crystal += crystal;

	net_writeln(conn, "%s 0 %d %d %d", cmd, offset, gold, crystal);
	return 0; 
}

// buffer = [0] [eid_challenger] [eid_receiver] [rank_receiver] [rank_challenger]
int out_arena_exchange(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer+n);
	return 0; 
}

int out_get_arena_reward(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	eid = ret;
	// eid mismatch
	if (eid != get_eid(conn)) {
		return 0;
	}

	net_writeln(conn, "%s %s", cmd, buffer+n);
	return 0; 
}

int out_money_exchange(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int eid;
	int gold;
	int crystal;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, "%d %d %d", &eid, &gold, &crystal);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}
	
	// eid mismatch
	if (eid != get_eid(conn)) {
		return 0;
	}

	conn->euser.gold += gold;
	conn->euser.crystal += crystal;

	net_writeln(conn, "%s %d %d", cmd, gold, crystal);
	return 0; 
}

int out_reset_arena_times(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0;
}


int out_gsearch(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;
	int flag;
	int start_id;
	int total;
	int eid;
	int lv;
	int gpos;
	char alias[EVIL_ALIAS_MAX+5];
	int icon;
	double rating;
	time_t last_login;
	double gshare;
	const char *ptr;
	char out_buffer[DB_BUFFER_MAX + 1];
	char *out_ptr;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	ret = sscanf(buffer, OUT_LIST_GSEARCH_SCAN, &flag, &start_id, &total, &n);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -87, "scanf:%s", buffer);
	}

	out_buffer[0] = '\0';
	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, "%d %d %d", flag, start_id, total);

	ptr = buffer;
	for (int i=0; i<total; i++) {
		ptr += n;
		ret = sscanf(ptr, OUT_LIST_GSEARCH_ROW_SCAN, &eid, &gpos, alias, &icon, &rating, &last_login, &gshare, &lv, &n);
		if (ret != 8) {
			NET_ERROR_RETURN(conn, -97, "scanf2: %d %d %s", i, ret, ptr);
		}
		// online check:  (set last_login == 0)
		if (get_conn_by_eid(eid) != NULL) {
			last_login = 0;
		}
		out_ptr += sprintf(out_ptr, " %d %d %s %d %lf %ld %lf %d", eid, gpos, alias, icon, rating
		, last_login, gshare, lv);
	}

	net_writeln(conn, "%s %s", cmd, out_buffer);

	return 0;
}


/*
int out_simple(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret = -75, n=0;

	sscanf(buffer, "%d %n", &ret, &n); // using n maybe faster
	if (ret < 0) {
		NET_ERROR_RETURN(conn, ret, "%s", buffer+n);
	}

	net_writeln(conn, "%s %s", cmd, buffer);
	return 0; 
}
*/

// put it at last
int out_notyet(connect_t * conn, const char *cmd, const char *buffer)
{
	net_writeln(conn, "%s -10 not_yet_implement", cmd);
	return -10;
}

typedef struct {
	const char * name;
	int (* fun)(connect_t*, const char*, const char *);
} dbout_t;

// @see dbio.cpp : in_worker_t
dbout_t dbout_list[] = {
	{ "test",	out_test}	// DB_TEST
,	{ "reg", out_register}	// DB_REGISTER
,	{ "log", out_login}		// DB_LOGIN
,	{ "lcard", out_load_card} // DB_LOAD_CARD
,	{ "ldeck", out_load_deck} // DB_LOAD_DECK
//////////////////////////
,	{ "job", out_save_card} // DB_SAVE_CARD // choose job   (return : sta ?)
,	{ "sdeck", out_save_deck}	// DB_SAVE_DECK
,	{ "alias", out_alias}		// DB_ALIAS
,   { "game",  out_game}     	// DB_GAME  // this is tricky
,   { "win",  out_win }  		// DB_WIN,  this may not have db_flag = 1 ?
//////////////////////////
,   { "sreplay",  out_save_replay }  		// DB_SAVE_REPLAY
,   { "lreplay",  out_list_replay }  		
,   { "replay",  out_load_replay }  	
,   { "sdebug",  out_save_debug }  	// DB_SAVE_DEBUG
,   { "gerr",  out_notyet } 
//////////////////////////
,	{ "quick", out_quick }		// same as out_load_deck, but no print deck
,	{ "sta", 	out_status }
,	{ "buy", 	out_buy_card }
,	{ "sell", 	out_sell_card }
,	{ "@cccard", out_msg }
//////////////////////////
,	{ "batch", 	out_batch }
,	{ "batch", 	out_save_batch } // same as batch, cmd only for error report
,	{ "pick", 	out_pick }
,   { "xcadd",  out_xcadd } // simply OK or error
,   { "xcbuy",  out_xcbuy } // will deduct gold and crystal for buyer/seller
////////////////////////////
,   { "xclist",  out_xclist } // simply OK or error
,   { "@xcreset",  out_xcreset } // simply OK or error
,   { "lguild",  out_list_guild } 
,   { "cguild",  out_create_guild } 
,   { "dguild",  out_delete_guild } 
////////////////////////////
,   { "gapply",  out_gapply } 
,   { "gpos",  out_gpos } 
,   { "gquit",  out_gquit } 
,   { "glist",  out_glist } 
,   { "gdeposit",  out_gdeposit } 
////////////////////////////
,   { "gbonus",  out_gbonus } 
,	{ "ldeposit", out_ldeposit}
,	{ "@ladder", out_create_ladder}
,	{ "ladder", out_get_ladder}
,	{ "sprofile", out_update_profile}
////////////////////////////
,	{ "fadd", out_friend_add }
,	{ "flist", out_friend_list }
,	{ "fsta", out_friend_sta }
,	{ "fsearch", out_friend_search }
,	{ "guild", out_guild}
////////////////////////////
,	{ "deposit", out_deposit}
,	{ "glv", out_glv}
,	{ "glevelup", out_glevelup}
,	{ "lpiece", out_load_piece }
,	{ "ppiece", out_pick_piece }
////////////////////////////
,	{ "mpiece", out_merge_piece }
,	{ "@pay", out_pay }
,	{ "course", out_get_course }
,	{ "scourse", out_save_course }
,	{ "challenge", out_challenge }
////////////////////////////
,	{ "lmis", out_load_mission }
,	{ "smis", out_save_mission }
,	{ "lslot", out_load_slot }
,	{ "sslot", out_save_slot }
,	{ "rslot", out_rename_slot }
////////////////////////////
,	{ "bslot", out_buy_slot }
,	{ "mreward", out_mission_reward }	
,	{ "slotlist", out_slot_list }
,	{ "@match_add", out_add_match }	
,	{ "match_apply", out_match_apply }	
////////////////////////////
,	{ "match_cancel", out_match_cancel }	
,	{ "@match_init", out_match_team_init }	
,	{ "update_player", out_update_player }	
,	{ "@match_eli_init", out_match_eli_init }	
,	{ "match_apply_ai", out_match_apply_ai }	
////////////////////////////
,	{ "@match_status", out_update_match_status }	
,	{ "fdel", out_friend_del }
,	{ "@ranking", out_init_ranking }
,	{ "rlist", out_ranking_list }
,	{ "rtarlist", out_ranking_targets }
////////////////////////////
,	{ "rgame", out_ranking_game }
,	{ "rank_data", out_ranking_data }
,	{ "ranklog", out_get_ranking_history }
,	{ "rank_challenge", out_ranking_challenge }
,	{ "@reset_ranktime", out_reset_ranking_time }
////////////////////////////
,	{ "@login", out_check_login}		// DB_CHECK_LOGIN
,	{ "list_message", out_list_message}
,	{ "read_message", out_read_message}
,	{ "@rank_reward", out_admin_rank_reward}
,	{ "rresp", out_resp_ranking_challenge }
////////////////////////////
,	{ "@send_message", out_admin_send_message}
,	{ "lottery", out_lottery}
,	{ "gift", out_exchange_gift }
,	{ "gate", out_gate}
,	{ "@reset_fighttimes", out_reset_fight_times }
////////////////////////////
,	{ "update_gate_pos", out_update_gate_pos}
,	{ "fight", out_fight }
,	{ "tower", out_tower}
,	{ "tower_result", out_tower_result}
,	{ "tower_info", out_tower_info}
////////////////////////////
,	{ "tower_buff", out_tower_buff}
,	{ "tower_ladder", out_tower_ladder}
,	{ "@tower_reward", out_admin_tower_reward}
,	{ "solo_plus", out_solo_plus}
,	{ "update_solo_pos", out_update_solo_pos}
////////////////////////////
,	{ "fight_robot", out_fight_robot}
,	{ "update_signals", out_update_signals}
,	{ "chapter", out_chapter}
,	{ "chapter_data", out_chapter_data}
,	{ "chapter_replace", out_chapter_update_data}
////////////////////////////
,	{ "chapter_reward", out_chapter_reward}
,	{ "lhero", out_load_hero_data }
,	{ "shero_mis", out_submit_hero_mission }
,	{ "up_hero_mlist", out_update_hero_mission_list }
,	{ "cpiece", out_load_card_piece }
////////////////////////////
,	{ "load_hero_deck", out_load_hero_deck }
,	{ "list_hero_slot", out_list_hero_slot }
,	{ "get_hero_slot", out_get_hero_slot }
,	{ "insert_hero_slot", out_insert_hero_slot }
,	{ "update_hero_slot", out_update_hero_slot }
////////////////////////////
,	{ "choose_hero_slot", out_choose_hero_slot }
,	{ "daily_log", out_get_daily_login }
,	{ "daily_reward", out_get_daily_reward }
,	{ "pshop", out_piece_shop }
,	{ "pshop", out_piece_shop }
////////////////////////////
,	{ "rpshop", out_refresh_piece_shop }
,	{ "pbuy", out_piece_buy }
,	{ "@addrobot", out_admin_add_robot}
,	{ "@arena", out_init_arena }
,	{ "arenatop", out_arena_top_list }
////////////////////////////
,	{ "arenatarget", out_arena_target }
,	{ "arenaexc", out_arena_exchange }
,	{ "arenagame", out_arena_game }
,	{ "arenatimes", out_update_arena_times }
,	{ "arenareward", out_get_arena_reward }
////////////////////////////
,	{ "moneyexchange", out_money_exchange }
,	{ "@reset_arenatimes", out_reset_arena_times }
,   { "gsearch",  out_gsearch } 

};

const int TOTAL_DBOUT = sizeof(dbout_list) / sizeof(dbout_t);

// dbout [dbtype] [out_buffer]
int dbout(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int dbtype = -1;
	int n;


	// client may exit earlier than dbout, it is normal
	if (conn != NULL && conn->state == STATE_FREE) {
		WARN_PRINT(-1, "dbout:conn->state_0 cid=%d  buff=%s", get_conn_id(conn), buffer);
		return -1;
	}

	// divert to out_*
	ret = sscanf(buffer, "%d %n", &dbtype, &n);
	if (ret <= 0) {
		ERROR_PRINT(-5, "dbout:input_sscanf %d", ret);
		NET_ERROR_PRINT(conn, -5, "dbout:input_sscanf %d", ret);
		goto cleanup;
	}

	// DEBUG_PRINT(0, "dbout:dbtype=%d cmd=%s buffer=%s", dbtype, cmd, buffer);

	if (dbtype < 0 || dbtype>=TOTAL_DBOUT) {
		// may be not yet implement
		ERROR_PRINT(-2, "dbout:dbtype_overflow %d", dbtype);
		NET_ERROR_PRINT(conn, -2, "dbout:dbtype_overflow %d", dbtype);
		goto cleanup;
	}

	// NOTE: only for auto fold a match game and start a new match game
	// other dbout with null conn will be blocked
	if (conn == NULL && dbtype != DB_WIN && dbtype != DB_SAVE_REPLAY 
	&& dbtype != DB_GAME && dbtype != DB_UPDATE_MATCH_PLAYER
	&& dbtype != DB_MATCH_TEAM_INIT && dbtype != DB_MATCH_ELI_INIT 
	&& dbtype != DB_MATCH_APPLY_AI && dbtype != DB_UPDATE_MATCH
	&& dbtype != DB_SAVE_RANKING_CHALLENGE && dbtype != DB_RANKING_CHALLENGE
	&& dbtype != DB_FIGHT_ROBOT
	&& dbtype != DB_UPDATE_HERO_MISSION
	) {
		ERROR_PRINT(-13, "dbout:conn_null_dtype_mismatch %d", dbtype);
		goto cleanup;
	}

//	net_write(conn, dbout_list[dbtype].name, ' ');	// reg/log/lcard command
	ret = dbout_list[dbtype].fun(conn, dbout_list[dbtype].name, buffer+n);

cleanup:
	if (conn == NULL) {
		return ret;
	}
	conn->db_flag = 0; // set 0
	if (conn->read_buffer[0] != '\0') {
		char temp_buffer[READ_BUFFER_SIZE + 1];
		strcpy(temp_buffer, conn->read_buffer);
		conn->read_buffer[0] = '\0';  conn->read_offset = 0;
		ret = do_batch_command(conn, temp_buffer); // may modify read_buf
		ERROR_NEG_PRINT(ret, "dbout:do_batch_command");
	}
	return ret;
}


// this is for testing
int dbin(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	int cid, dbtype;
	char out_buffer[4000]; // DB_BUFFER_MAX ?

	ret = sscanf(buffer, "%d %d", &cid, &dbtype);
	if (ret < 2) {
		net_writeln(conn, "dbin -5 at least 2 parameters [cid] [dbtype]");
		return 0;
	}

	ret = dbin_once(buffer, out_buffer);

	// do not write $cmd
	net_write_space(conn, "dbout %d", dbtype);	// explicitly use dbout for testing!
	net_write(conn, out_buffer, '\n');
	return 0;
}



//////////////////// DBIN START [ //////////////////

int dbin_get_db_index(connect_t *conn)
{

	int eid = 0;
	int index = 0;
	if (conn != NULL) {
		eid = conn->euser.eid;
	}

	if (eid > 0) {
		index = eid % MAX_DB_THREAD;
	} else {
		// this is for login and register, and offline user
		// if reconn, eid != 0
		index = abs(random()) % MAX_DB_THREAD;
	}

	return index;
}

// 1. write the db command to dbio_data->db_buffer[index] (according to buffer_index)
// 2. save the buffer_index to unsigned char num[2] 2-bytes short structure
// 3. update the buffer_index (++ , DB_TRANS_MAX round-wrap)
// 4. update conn->db_flag to time now
// 5. write num[2] to main_fd, signal dbio() thread to work on it
// in case of error: 
//    before write to db_buffer, nothing to lose, just return error
//    after write to db_buffer, must reset the db_buffer[0]=0
//    after update buffer_index++, it is ok, no need to resume
int dbin_write(connect_t *conn, const char *cmd, int dbtype
, const char *fmt, ...)
{
	// -3 is possible for win_game if both player is already offline
	int cid = -3;  // peter: this is dangerous, consider to use a normal cid?
	int dbio_index = 0;
	if (conn != NULL) {
		cid = get_conn_id(conn);
	} 
	dbio_index = dbin_get_db_index(conn);
	INFO_PRINT(0, "dbin_write:dbio_index=%d cmd=[%s] dbtype=%d", dbio_index, cmd, dbtype);
	// TODO more dbio
	dbio_init_t * dbio_data = g_dbio_data + dbio_index;
	int ret, len;
	char *in_buffer;
	unsigned char num[2]; // for sending buffer_index
    va_list argptr;

	if (dbio_data->buffer_index < 0 || dbio_data->buffer_index >= DB_TRANS_MAX) {
		if (conn != NULL ) {
			NET_ERROR_RETURN(conn, -72, "buffer_index_outbound %d"
			,  dbio_data->buffer_index);
		}
	}
	in_buffer = dbio_data->db_buffer[dbio_data->buffer_index];
	if (in_buffer[0] != 0) {
		// TODO : check db_buffer[ next_index ][0] == 0 ?
		// this is bad, but possible if request is too fast and db slow
		if (conn != NULL) {
			NET_ERROR_RETURN(conn, -102, "db_buffer_full %d"
			, dbio_data->buffer_index);
		}
	}

	len = sprintf(in_buffer, "%d %d ", cid, dbtype);

    va_start(argptr, fmt);
    len += vsnprintf(in_buffer + len, DB_BUFFER_MAX, fmt, argptr);
    va_end(argptr);

	// for debug
//	printf("dbin_write: in_buff(%d)=[%s]", dbio_data->buffer_index, in_buffer);  
	// core logic:  send the buffer_index, and buffer_index++(round-wrap)
	short_to_char(num, dbio_data->buffer_index);  // must before index++
	// ++ and round-wrap   ADD_BOUND(&buffer_index, DB_TRANS_MAX)
	dbio_data->buffer_index++;  
	if (dbio_data->buffer_index >= DB_TRANS_MAX) dbio_data->buffer_index = 0;
	if (conn != NULL) {
		conn->db_flag = time(NULL);  // core logic!
	}
	ret = write(dbio_data->main_fd, num, 2);  // core logic
	if (ret != 2) {
		// this should be fatal!
		BUG_PRINT(-95, "dbin_write_2 %d", ret);
		if (conn != NULL) {
			net_writeln(conn, "%s -95 dbio_down", cmd);
			conn->db_flag = 0;  // core logic!
		}
		in_buffer[0] = 0;  // reset the buffer
		return -95;
	}

	return 0;
}


// usage:  log [name] [password] [platform] [channel]
// reconnect logic :
// - after successful login, check my eid -> <channel.id, room.id> pair
// - need a map<  eid,   pair<channel.id,room.id>  > = map<int,int> g_user_room
//   or we use    channel_room_id = (channel.id << 8) + room.id;
// - in room_create / room_join / room_leave / room_clean :  
//   update g_user_room  (add / remove mapping)
//   room_create:  g_user_room[eid] = cr_id = (channel.id << 8) + room.id
//   room_join:    g_user_room[eid] = cr_id = (channel.id << 8) + room.id
//   room_leave:   g_user_room.erase(eid);  // remove it
//   room_clean:   for (i=0; i<proom->num_guest; i++) { 
//						eid = proom->guest[i];
//						g_user_room.erase(eid);
//					}
//   note: g_user_room[eid] may return 0 for not found, which is channel 0,room 0
//   NEED to use room 1,  skip room 0
// - login : check  ret = g_user_room[eid]  if non-zero, we have a room, 
//   channel_id = (ret >> 8) & 0xff;
//   room_id = (ret & 0xff)
//   conn->room = g_room_list[channel_id] + room_id;
//   g_user_index[eid] = conn_id  // default must have
// @see sys_login
int dbin_login(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	char username[EVIL_USERNAME_MAX+1];
	char password[EVIL_PASSWORD_MAX+1];
	char fmt[100];
	int platform = 0;
	int channel = 0;

	sprintf(fmt, "%%%ds %%%ds %%d %%d", EVIL_USERNAME_MAX
	, EVIL_PASSWORD_MAX);
	ret = sscanf(buffer, fmt, username, password, &platform, &channel);
	if (ret < 2) {
		// include net_writeln, local printf, return code
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	char ip[EVIL_ADDRESS_MAX + 1]; // 255.255.255.255:65535 
	address_readable(ip, &conn->address);
	// DEBUG_PRINT(0, "dbin_login: cid=%d ip=%s", get_conn_id(conn), ip);

	ret = dbin_write(conn, cmd, DB_LOGIN, IN_LOGIN_PRINT
	, username, password, ip, platform, channel);
	return ret;
}

// usage:  log [name] [password] [platform] [channel]
int dbin_register(connect_t * conn, const char *cmd, const char *buffer)
{
	int ret;
	char fmt[100];
	char username[EVIL_USERNAME_MAX+1];
	char password[EVIL_PASSWORD_MAX+1];
	char uid[EVIL_UID_MAX+1] = "_";
	int platform = 0;
	int channel = 0;

	sprintf(fmt, "%%%ds %%%ds %%d %%d", EVIL_USERNAME_MAX, EVIL_PASSWORD_MAX);
	ret = sscanf(buffer, fmt,  username, password, &platform, &channel); 
	if (ret < 2) {
		NET_ERROR_RETURN(conn, -3, "empty_password");
	}

	char ip[EVIL_ADDRESS_MAX + 1]; // 255.255.255.255:65535 
	address_readable(ip, &conn->address);
	// DEBUG_PRINT(0, "dbin_login: cid=%d ip=%s", get_conn_id(conn), ip);

	ret = dbin_write(conn, cmd, DB_REGISTER, IN_REGISTER_PRINT
	, username, password, ip, platform, channel, uid);
	return ret;
}

int dbin_load_card(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	// no input, default to conn->euser.eid
	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "lcard:not_login");
	}

	ret = dbin_write(conn, cmd, DB_LOAD_CARD, IN_LOAD_CARD_PRINT, eid);

	return ret;
}

int dbin_load_deck(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	// no input, default to conn->euser.eid
	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "ldeck:not_login");
	}

	// peter: speed up, use cache if the euser.deck is not empty [0] != 0
	// keep this, but do not use (fresh load is good)
//	if (conn->euser.deck[0] != '\0') {
//	}

	ret = dbin_write(conn, cmd, DB_LOAD_DECK, IN_LOAD_DECK_PRINT, eid);
	return ret;
}

// select job
int dbin_job(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int hero_id, hero_level;

	// require: eid(from conn), hero_id, level (from buffer)
	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "job:not_login");
	}

	ret = sscanf(buffer, "%d %d",  &hero_id, &hero_level); 
	// no hero_id, must fail
	if (ret < 1) {
		NET_ERROR_RETURN(conn, -5, "job:invalid_input %d", ret);
		return ERR_IO;
	}
	if (ret < 2) {
		hero_level = 1; // default
	}


	// range check
	if (hero_id < 1 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -2, "job:hero_id out of range");
		return ERR_OUTBOUND;
	}
	/*
	if (0 == hero_valid[hero_id]) {
		net_writeln(conn, "-12 hero_id not valid");
		return ERR_OUTBOUND - 10;  // this is -12
	}
	*/
	std_deck_t std_deck = g_design->std_deck_list[hero_id];
	if (std_deck.id == 0) {
		NET_ERROR_RETURN(conn, -9, "job:null_deck for hero_id %d", hero_id);
		return ERR_IMPOSSIBLE;
	}
	if (hero_level < 1 || hero_level > 3) {
		NET_ERROR_RETURN(conn, -22, "job:hero_level out of range 1 to 3 only");
		return ERR_OUTBOUND - 20;  // this is -22
	}

	ret = dbin_write(conn, cmd, DB_SAVE_CARD, IN_SAVE_CARD_PRINT
	, eid, std_deck.deck);


	/*
	const int *deck = standard_deck[hero_id];
	if (deck == NULL) {
		net_writeln(conn, "-7 null_deck for hero_id %d", hero_id);
		return ERR_IMPOSSIBLE;
	}

	char array[EVIL_CARD_MAX+1];
	// TODO we should have standard deck in 400 form
	ret = card_list_digit(array, deck, STD_DECK_MAX);
	if (ret < 0) {
		net_writeln(conn, "-17 card_list_array error");
		return ERR_IMPOSSIBLE - 10;
	}

	// setup hero card
	array[hero_id - 1] = '1';  // zero-base, core logic

	// TODO create DB_JOB, to save card + save level
	// core logic
	ret = dbin_write(conn, cmd, DB_SAVE_CARD, IN_SAVE_CARD_PRINT, eid, array);
	*/
	return ret;
}

// check:
// a. eid > 0 (login'ed)
// b. user not in room (conn->room == NULL)
// c. hero_id (ai) is valid
// core logic:
// 1. setup the room in CHANNEL_SOLO (assign to conn->room)
// 2. setup 2 guests in room, one for me, one for AI
// 3. link g_user_room[eid] = room
// 4. broadcast room status  (room 2 1 ...)  st=15 (ST_GAME)
// -------
// part2:  for out_game
// 1. setup room->gameid, random seed
// TODO check whether eid1 has already read the deck to euser.deck
int dbin_solo(connect_t *conn, const char *cmd, const char *buffer)
{
	int eid1 = get_eid(conn);
	int eid2 = 1; // hard coded, solo1
	int seed = 0;	 // default is 0, means random @see game_init()
	int ret;
	evil_user_t * puser = &conn->euser;
	if (eid1 <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "already_has_room");
	}
	ret = sscanf(buffer, "%d %d", &eid2, &seed);

	// eid2 >= sizeof(g_ai_list) / sizeof(g_ai_list[0])
	// DEBUG_PRINT(0, "dbin_solo:check  MAX_AI_EID=%d  sizeof(xxx)=%zd"
	// , MAX_AI_EID, sizeof(g_ai_list) / sizeof(g_ai_list[0]));
	// not more than >MAX_AI_EID  or >=sizeof(...)/sizeof(...)
	if (eid2 <=0 || eid2 >= (int)(sizeof(g_design->ai_list) / sizeof(g_design->ai_list[0]))) {
		NET_ERROR_RETURN(conn, -2, "hero_id_out_bound %d", eid2);
	}
	if (g_design->ai_list[eid2].id != eid2) {
		NET_ERROR_RETURN(conn, -5, "hero_id_invalid %d", eid2);
	}

	ai_t *pai;
	pai = g_design->ai_list+eid2;
	
	room_t *proom;
	proom = new_room(CHANNEL_SOLO);
	proom->num_guest = 2;
	proom->guest[0] = eid1;
	proom->guest[1] = eid2;  // hard coded TODO check evil_user,deck eid=1
	proom->rating[0] = conn->euser.rating;
	proom->rating[1] = pai->rating; // hard coded for solo
	strcpy(proom->alias[0], conn->euser.alias);
	strcpy(proom->alias[1], pai->alias);
	proom->icon[0] = conn->euser.icon;
	proom->icon[1] = pai->icon; 
	proom->lv[0] = conn->euser.lv;
	proom->lv[1] = pai->lv; 
	proom->game_type = GAME_SOLO;
	sprintf(proom->title, "%s~VS~%s", proom->alias[0], proom->alias[1]);

	// fixed random seed (if >0)
	proom->seed = seed;  // this is special for solo, testing random seed

	// WARN_PRINT(-1, "NOT warning: dbin_solo room title: %s", proom->title);
	conn->room = proom;  // for out_game() 
	// re-conn logic ?  
	g_user_room[eid1] = proom;
	// eid2 is always AI, so we don't need to assign to g_user_room[eid]


	// TODO:  if conn->euser.deck[0] != 0 : go directly to game
	// need copy euser.deck[0] to proom->deck[0] and AI deck to deck[1]
	// need: gameid = get_gameid(),  state = ST_GAME,  
	// then: game_init(proom, proom->seed)
	if (puser->deck[0] != 0) {
		// DEBUG_PRINT(eid1, "dbin_solo:deck_ready");
		// @see out_game()
		proom->gameid = get_gameid();
		proom->state = ST_GAME;
		sprintf(proom->deck[0], "%.400s", puser->deck);
		sprintf(proom->deck[1], "%.400s", pai->deck);
		room_set_hero_info(proom, NULL);
		ret = game_init(proom, proom->seed, 0);
		conn->st = ST_GAME; // set me as GAME state
		room_info_broadcast(proom, 0); // 0 means all
		return ST_GAME;
	}

	ret = dbin_write(conn, cmd, DB_GAME, IN_GAME_PRINT, eid1, eid2);
	return ret;
}

int dbin_list_solo(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int solo_id;
	int page_size = 20;
	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = sscanf(buffer, "%d", &solo_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_list_solo:input_error %d", ret);
	}

	if (solo_id <=0 || solo_id > MAX_AI_EID) {
		NET_ERROR_RETURN(conn, -2, "dbin_list_solo:solo_id_out_bound %d", solo_id);
	}

	int hero_id = 0;
	int count = 0;
	char out_buffer[1000];
	char *ptr = out_buffer;
	solo_t *solo;
	for (int i=solo_id; i>0; i--) {
		solo = &g_design->design_solo_list[i];
		if (solo->id == 0) {
			continue;
		}
		sscanf(solo->deck2, "%d", &hero_id);
		ptr += sprintf(ptr, "%d %d %s ", solo->id, hero_id, solo->alias);
		count++;
		if (count > page_size) {
			break;
		}
	}

	net_writeln(conn, "%s %d %d %s", cmd, solo_id, count, out_buffer);
	

	return 0;
}

int dbin_solo_plus(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int solo_id;
	int eid = get_eid(conn);

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "already_has_room");
	}

	ret = sscanf(buffer, "%d", &solo_id);
	if (solo_id <=0 || solo_id > MAX_AI_EID) {
		NET_ERROR_RETURN(conn, -2, "dbin_solo_plus:solo_id_out_bound %d", solo_id);
	}

	solo_t * solo = get_design_solo(solo_id);
	if (solo == NULL) {
		NET_ERROR_RETURN(conn, -5, "dbin_solo_plus:solo_id_invalid %d", solo_id);
	}

	ret = dbin_write(conn, cmd, DB_SOLO, "%d %d", eid, solo_id);

	return ret;
}


int get_chapter_robot(design_chapter_stage_t * stage)
{
	int r = abs(random()) % stage->solo_size;
	int id = stage->solo_list[r];
	// DEBUG_PRINT(0, "get_chapter_robot:r=%d", r);

	// peter: using solo_id instead of index
	solo_t * solo = get_design_solo(id);
	if (solo == NULL) {
		ERROR_RETURN(-5, "get_chapter_robot:invalid_solo_id %d  r %d stage %d", 
			id, r, stage->stage_id);
	}

	return solo->id;

	/**
	if (stage->solo_list[r] > g_design->design_solo_size) {
		BUG_PRINT(-5, "get_chapter_robot:solo_size_out_bound %d", stage->stage_id);
		return -5;
	}
	return stage->solo_list[r];
	**/
}

int dbin_chapter(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int chapter_id;
	int stage_id;
	int eid = get_eid(conn);
	int solo_id;

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_chapter:not_login");
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "dbin_chapter:already_has_room");
	}

	ret = sscanf(buffer, "%d %d", &chapter_id, &stage_id);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "dbin_chapter:input_error");
	}

	if (chapter_id > g_design->design_chapter_size || chapter_id <= 0) {
		NET_ERROR_RETURN(conn, -15, "dbin_chapter:no_such_chapter");
	}

	design_chapter_t * chapter = g_design->design_chapter_list + chapter_id;
	if (chapter->chapter_id <= 0) {
		BUG_PRINT(-16, "dbin_chapter:chapter_error %d", chapter_id);
		NET_ERROR_RETURN(conn, -16, "dbin_chapter:chapter_error");
	}

	if (stage_id > chapter->stage_size || stage_id <= 0) {
		NET_ERROR_RETURN(conn, -25, "dbin_chapter:no_such_stage");
	}

	design_chapter_stage_t * stage = chapter->stage_list + stage_id;
	if (stage->stage_id <= 0) {
		BUG_PRINT(-26, "dbin_chapter:stage_error %d", stage_id);
		NET_ERROR_RETURN(conn, -26, "dbin_stage:stage_error");
	}

	solo_id = get_chapter_robot(stage);
	if (solo_id <= 0) {
		BUG_PRINT(-36, "dbin_chapter:solo_id_error %d", stage_id);
		NET_ERROR_RETURN(conn, -36, "dbin_stage:solo_id_error %d", stage_id);
	}

	if (conn->euser.flag_load_chapter == 0) {
		NET_ERROR_RETURN(conn, -3, "dbin_chapter:empty_chapter_data");
	}

	evil_chapter_data_t & uchapter = conn->euser.chapter_list[chapter_id];
	char info = uchapter.data[stage_id-1];
	if (info == CHAPTER_DATA_LOCK) {
		NET_ERROR_RETURN(conn, -46, "dbin_chapter:chapter_lock");
	}

	// DEBUG_PRINT(0, "dbin_chapter:stage_id=%d stage_name=%s t1=%d p1=%d t2=%d p2=%d t3=%d p3=%d solo_id=%d", stage->stage_id, stage->name, stage->target_list[0], stage->point_list[0], stage->target_list[1], stage->point_list[1], stage->target_list[2], stage->point_list[2], solo_id);

	ret = dbin_write(conn, cmd, DB_CHAPTER, "%d %d %d %d %d %d", eid, chapter_id, stage_id, -stage->power, solo_id, chapter->stage_size);

	return ret;
}

// CMD: lchapter
// RET: lchapter [chaper_pos] [size] [chapter_info1] [chapter_info2] ...
// chapter_info: chapter_id, chapter_name
int dbin_list_chapter(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_list_chapter:not_login");
	}

	net_write_space(conn, "%s %d %d", cmd, conn->euser.chapter_pos, g_design->design_chapter_size);
	for (int i=1; i<=g_design->design_chapter_size; i++) {
		design_chapter_t * chapter = g_design->design_chapter_list + i;
		if (chapter->chapter_id <= 0) {continue;};
		net_write_space(conn, "%d %s", chapter->chapter_id, chapter->name);
	}
	net_write(conn, "", '\n');

	ret = 0;
	return ret;
}

// CMD: chapter_data [chapter_id]
// if chapter_id == 0, use player.chapter_pos
// RET: chapter_data [chapter_id] [total_chapter_size] [chapter_name] [stage_size] [stage_data]
int dbin_chapter_data(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int chapter_id;
	int stage_id = 0; // useless here, only for win_game when user.chapter_list null
	int star = 0; // useless here, only for win_game when user.chapter_list null
	int eid = get_eid(conn);

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_chapter_data:not_login");
	}

	ret = sscanf(buffer, "%d", &chapter_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_chapter_data:input_error");
	}

	if (chapter_id > g_design->design_chapter_size || chapter_id < 0) {
		NET_ERROR_RETURN(conn, -15, "dbin_chapter_data:no_such_chapter");
	}

	// already load in memory, no need to get in db
	if (conn->euser.flag_load_chapter) {
		int change = refresh_chapter_data(conn, chapter_id, stage_id, star);
		if (change) {
			return 0;
		}

		if (chapter_id == 0) {
			chapter_id = __get_chapter_pos(conn);
		}

		evil_chapter_data_t * chapter = &conn->euser.chapter_list[chapter_id];
		design_chapter_t * dchapter = &g_design->design_chapter_list[chapter_id];
		net_write_space(conn, "%s %d %d %s %d"
		, cmd, chapter_id
		, g_design->design_chapter_size
		, dchapter->name, dchapter->stage_size);

		for (int i=1; i<=dchapter->stage_size; i++) {
			design_chapter_stage_t * stage = dchapter->stage_list + i;
			if (stage->stage_id <= 0) {continue;};
			net_write_space(conn, "%d", stage->stage_id);
		}
		net_writeln(conn, "%s", chapter->data);
		return 0;
	}

	/*
	design_chapter_t * chapter = g_design->design_chapter_list + chapter_id;
	if (chapter->chapter_id <= 0) {
		BUG_PRINT(-16, "dbin_chapter_data:chapter_error %d", chapter_id);
		NET_ERROR_RETURN(conn, -16, "dbin_chapter_data:chapter_error");
	}
	*/

	ret = dbin_write(conn, cmd, DB_GET_CHAPTER, "%d %d %d %d", eid, chapter_id, stage_id, star);

	return ret;
}

// CMD: chapter_stage [chapter_id] [stage_id]
// RET: chapter_stage [chapter_id] [stage_id] [stage_name] [stage_msg] 
//				[MAX_CHAPTER_TARGET] [target_info1] [target_info2] [target_info3]
//				[exp] [power]
//				[tips_size] [tips_card1] [tips_card2]
//				[MAX_CHAPTER_REWARD] [reward_info1] [reward_info2] ...
// target_info: target_type, p1, p2
// reward_info: reward_type, count
int dbin_chapter_stage(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int chapter_id;
	int stage_id;
	int eid = get_eid(conn);

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_chapter_stage:not_login");
	}

	ret = sscanf(buffer, "%d %d", &chapter_id, &stage_id);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "dbin_chapter_stage:input_error");
	}

	if (chapter_id > g_design->design_chapter_size || chapter_id <= 0) {
		NET_ERROR_RETURN(conn, -15, "dbin_chapter_stage:no_such_chapter");
	}

	design_chapter_t * chapter = g_design->design_chapter_list + chapter_id;
	if (chapter->chapter_id <= 0) {
		NET_ERROR_RETURN(conn, -25, "dbin_chapter_stage:chapter_error");
	}

	design_chapter_stage_t * stage = chapter->stage_list + stage_id;

	if (stage->stage_id <= 0) {
		NET_ERROR_RETURN(conn, -35, "dbin_chapter_stage:no_such_stage");
	}

	int solo_id = get_chapter_robot(stage);
	if (solo_id <= 0) {
		BUG_PRINT(-36, "dbin_chapter:solo_id_error %d", stage_id);
		NET_ERROR_RETURN(conn, -36, "dbin_stage:solo_id_error %d", stage_id);
	}
	solo_t * solo = get_design_solo(solo_id);

	net_write_space(conn, "%s %d %d %s %s", cmd, chapter_id, stage_id, stage->name, stage->stage_msg);
	net_write_space(conn, "%d", MAX_SOLO_TARGET);
	solo_target_t *target = NULL;
	for (int i=0; i<MAX_SOLO_TARGET; i++) {
		target = solo->target_list + i;
		net_write_space(conn, "%d %d %d", target->target, target->p1, target->p2);
	}

	net_write_space(conn, "%d %d", stage->exp, stage->power);

	net_write_space(conn, "%d", stage->tips_size);
	for (int i=0; i<stage->tips_size; i++) {
		net_write_space(conn, "%d", stage->tips_list[i]);
	}

	net_write_space(conn, "%d", MAX_CHAPTER_REWARD);
	design_chapter_reward_t *reward = NULL;
	for (int i=0; i<MAX_CHAPTER_REWARD; i++) {
		reward = stage->reward_list + i;
		net_write_space(conn, "%d %d", reward->reward, reward->count);
	}

	design_stage_dialog_t &dialog = g_design->chapter_dialog_list[chapter_id].stage_dialog_list[stage_id];
	net_write_space(conn, "%d %s", dialog.dialog_count, dialog.dialog);

	net_write(conn, "", '\n');

	return ret;
}


// CMD: load_hero_deck
// RET: load_hero_deck [hero_id] [slot_id] [hp] [energy] [deck]
int dbin_load_hero_deck(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_load_hero_deck:not_login");
	}

	ret = dbin_write(conn, cmd, DB_LOAD_HERO_DECK, "%d", eid);

	ret = 0;
	return ret;
}

// CMD: list_hero_slot [hero_id]
// RET: list_hero_slot [hero_id] [slot_percent] [num_row] [slot_info]...
// slot_info: [slot_id] [card_count]
int dbin_list_hero_slot(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int hero_id;

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_list_hero_slot:not_login");
	}
	ret = sscanf(buffer, "%d", &hero_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_list_hero_slot:invalid_input");
	}
	if (hero_id < 1 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -2, "dbin_list_hero_slot:hero_id_out_bound %d"
		, hero_id);
	}

	ret = dbin_write(conn, cmd, DB_LIST_HERO_SLOT, "%d %d", eid, hero_id);

	ret = 0;
	return ret;
}

// CMD: get_hero_slot [hero_id] [slot_id]
// RET: get_hero_slot [hero_id] [slot_id] [slot]
int dbin_get_hero_slot(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int hero_id;
	int slot_id;

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_get_hero_slot:not_login");
	}
	ret = sscanf(buffer, "%d %d", &hero_id, &slot_id);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "dbin_get_hero_slot:invalid_input");
	}
	if (hero_id < 1 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -2, "dbin_get_hero_slot:hero_id_out_bound %d"
		, hero_id);
	}
	if ((slot_id <= 0 || slot_id > EVIL_HERO_SLOT_MAX)
	&& (slot_id != HERO_SLOT_RECOMMAND)) {
		NET_ERROR_RETURN(conn, -12, "dbin_get_hero_slot:slot_id_out_bound %d"
		, slot_id);
	}


	if (slot_id == HERO_SLOT_RECOMMAND) {
		if (g_design->hero_slot_list[hero_id].id == 0) {
			NET_ERROR_RETURN(conn, -7
			, "dbin_get_hero_slot:design_hero_slot_null %d"
			, hero_id);
		}
		int slot_percent = __get_recommand_card_percent(conn->euser.card
		, g_design->hero_slot_list[hero_id].deck);

		DEBUG_PRINT(0, "dbin_get_hero_slot:slot_percent[%d]", slot_percent);
		
		net_writeln(conn, "%s %d %d %d %.400s", cmd, hero_id, slot_id, slot_percent
		, g_design->hero_slot_list[hero_id].deck);
		return 0;
	}

	ret = dbin_write(conn, cmd, DB_GET_HERO_SLOT, "%d %d %d", eid
	, hero_id, slot_id);

	ret = 0;
	return ret;
}

// CMD: insert_hero_slot [hero_id]
// RET: insert_hero_slot [hero_id]
int dbin_insert_hero_slot(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int hero_id;

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_insert_hero_slot:not_login");
	}
	ret = sscanf(buffer, "%d", &hero_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_insert_hero_slot:invalid_input");
	}
	if (hero_id < 1 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -2, "dbin_insert_hero_slot:hero_id_out_bound %d"
		, hero_id);
	}

	ret = dbin_write(conn, cmd, DB_INSERT_HERO_SLOT, "%d %d", eid
	, hero_id);

	ret = 0;
	return ret;
}

// CMD: update_hero_slot [hero_id] [slot_id] [slot]
// RET: update_hero_slot [hero_id] [slot_id]
int dbin_update_hero_slot(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int hero_id;
	int slot_id;
	char slot[EVIL_CARD_MAX+1];

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_update_hero_slot:not_login");
	}
	ret = sscanf(buffer, "%d %d %400s", &hero_id, &slot_id, slot);
	if (ret != 3) {
		NET_ERROR_RETURN(conn, -5, "dbin_update_hero_slot:invalid_input");
	}
	if (hero_id < 1 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -2, "dbin_update_hero_slot:hero_id_out_bound %d"
		, hero_id);
	}
	if (slot_id <= 0 || slot_id > EVIL_HERO_SLOT_MAX) {
		NET_ERROR_RETURN(conn, -12, "dbin_update_hero_slot:slot_id_out_bound %d"
		, slot_id);
	}
	if (strlen(slot) != EVIL_CARD_MAX) {
		NET_ERROR_RETURN(conn, -15, "dbin_update_hero_slot:slot_error");
	}

	ret = check_deck(slot, conn->euser.card, eid);
	if (ret < 0 ) {
		if (ret == E_RETURN_CHECK_DECK_OVER_MAX) {
			NET_ERROR_RETURN(conn, E_RETURN_CHECK_DECK_OVER_MAX, "%s", E_CHECK_DECK_OVER_MAX);
		}
		if (ret == E_RETURN_CHECK_DECK_LESS_MIN) {
			NET_ERROR_RETURN(conn, E_RETURN_CHECK_DECK_LESS_MIN, "%s", E_CHECK_DECK_LESS_MIN);
		}
		if (ret == E_RETURN_CHECK_DECK_LESS_QUICK_MIN) {
			NET_ERROR_RETURN(conn, E_RETURN_CHECK_DECK_LESS_QUICK_MIN, "%s", E_CHECK_DECK_LESS_QUICK_MIN);
		}
		// this is rather buggy
		NET_ERROR_RETURN(conn, -25, "dbin_update_hero_slot:check_deck");
	}

	if (slot[hero_id-1] != '1') {
		NET_ERROR_RETURN(conn, -35, "dbin_update_hero_slot:hero_mismatch");
	}

	ret = dbin_write(conn, cmd, DB_UPDATE_HERO_SLOT, "%d %d %d %.400s", eid
	, hero_id, slot_id, slot);

	ret = 0;
	return ret;
}

// CMD: choose_hero_slot [hero_id] [slot_id]
// RET: choose_hero_slot [hero_id] [slot_id]
int dbin_choose_hero_slot(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int hero_id;
	int slot_id;

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "dbin_choose_hero_slot:not_login");
	}
	ret = sscanf(buffer, "%d %d", &hero_id, &slot_id);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "dbin_choose_hero_slot:invalid_input");
	}
	if (hero_id < 1 || hero_id > HERO_MAX) {
		NET_ERROR_RETURN(conn, -2, "dbin_choose_hero_slot:hero_id_out_bound %d"
		, hero_id);
	}
	if ((slot_id <= 0 || slot_id > EVIL_HERO_SLOT_MAX)
	&& slot_id != HERO_SLOT_RECOMMAND) {
		NET_ERROR_RETURN(conn, -12, "dbin_choose_hero_slot:slot_id_out_bound %d"
		, slot_id);
	}

	if (slot_id == HERO_SLOT_RECOMMAND
	&& g_design->hero_slot_list[hero_id].id == 0) {
		NET_ERROR_RETURN(conn, -7, "dbin_choose_hero_slot:design_hero_slot_null %d"
		, hero_id);
	}

	if (slot_id == HERO_SLOT_RECOMMAND) {
		int slot_percent = __get_recommand_card_percent(conn->euser.card
		, g_design->hero_slot_list[hero_id].deck);
		DEBUG_PRINT(0, "dbin_choose_hero_slot:slot_percent[%d]", slot_percent);
		if (slot_percent < 100) {
			NET_ERROR_RETURN(conn, -17
			, "dbin_choose_hero_slot:no_enough_hero_slot %d %d %d"
			, hero_id, slot_id, slot_percent);
		}
	}

	ret = dbin_write(conn, cmd, DB_CHOOSE_HERO_SLOT, "%d %d %d %.400s", eid
	, hero_id, slot_id, g_design->hero_slot_list[hero_id].deck);

	ret = 0;
	return ret;
}

// cmd gate [gate_id]
int dbin_gate(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int gate_id = 0;
	// evil_user_t * puser = &conn->euser;
	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "already_has_room");
	}

	ret = sscanf(buffer, "%d", &gate_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_gate:input_error");
	}

	if (gate_id <= 0 || gate_id > g_design->design_gate_size) {
		NET_ERROR_RETURN(conn, -25, "dbin_gate:invalid_gate_id");
	}

	if (gate_id > conn->euser.gate_pos) {
		// -99 is unique in this case for client gate mission go to	gate_list
		NET_ERROR_RETURN(conn, -99, "dbin_gate:gate_id_out_bound");
	}

	design_gate_t & gate = g_design->design_gate_list[gate_id];

	// get deck in db, core login in out_gate
	ret = dbin_write(conn, cmd, DB_GATE, "%d %d %d", conn->euser.eid, gate_id, -gate.power);

	return ret;
}

// cmd lgate [start_id] [page_size]
// ret lgate [my_gate_pos] [all_gate_size] [start_id] [page_size] [gate_info1] [gate_info2] ... [gate_infon]
// gate_info = [id] [title] [gold] [crystal] [exp] [focus_card] [power]
int dbin_list_gate(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int start_id;
	int page_size;
	int num;
	num = 0;

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = sscanf(buffer, "%d %d", &start_id, &page_size);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "dbin_list_gate:input_error");
	}

	if (start_id == 0) {
//		start_id = 1; // base 1
		start_id = conn->euser.gate_pos;
	}

	if (start_id > g_design->design_gate_size || start_id <= 0) {
		NET_ERROR_RETURN(conn, -15, "dbin_list_gate:input_start_id");
	}

	// max is 20
	if (page_size > 20) {
		page_size = 20;
	}

	char out_buffer[1000];
	char *ptr;
	ptr = out_buffer;

//	for (int i=start_id; i<=g_design->design_gate_size; i++) {
//		design_gate_t &gate = g_design->design_gate_list[i];
//		ptr += sprintf(ptr, "%d %s %d %d %d %d %d ", i, gate.title, gate.gold, gate.crystal, gate.exp, gate.focus_card, gate.power);
//		num ++;
//		if (num >= page_size) {
//			break;
//		}
//	}
	// use reverse order
	for (int i=start_id; i > 0; i--) {
		design_gate_t &gate = g_design->design_gate_list[i];
		ptr += sprintf(ptr, "%d %s %d %d %d %d %d ", i, gate.title, gate.gold, gate.crystal, gate.exp, gate.focus_card, gate.power);
		num ++;
		if (num >= page_size) {
			break;
		}
	}

	net_writeln(conn, "%s %d %d %d %d %s"
	, cmd, conn->euser.gate_pos, g_design->design_gate_size, start_id, num, out_buffer);
	ret = 0;
	return ret;
}

int dbin_gate_msg(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int gate_id;

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = sscanf(buffer, "%d", &gate_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_gate_msg:input_error");
	}

	if (gate_id == 0) {
		gate_id = 1; // base 1
	}

	if (gate_id <= 0 || gate_id >= MAX_GATE_LIST) {
		NET_ERROR_RETURN(conn, -5, "dbin_gate_msg:invalid_gate_id");
	}

	design_gate_msg_t &gate = g_design->design_gate_msg_list[gate_id];

	net_writeln(conn, "%s %d %d %s"
	, cmd, gate_id, gate.size, gate.msg);
	ret = 0;
	return ret;
}

// CMD: tower [tower_index]
int dbin_tower(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int tower_index = 0;

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "already_has_room");
	}

	ret = sscanf(buffer, "%d", &tower_index);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_tower:invalid_input %d", ret);
	}

	if (tower_index <= 0) {
		NET_ERROR_RETURN(conn, -16, "dbin_tower:invalid_tower_index %d", tower_index);
	}

	ret = dbin_write(conn, cmd, DB_TOWER, "%d %d", conn->euser.eid, tower_index);

	return 0;
}

// CMD: tower_info 
// RET: tower_info [eid] [tower_pos] [tower_times] [pos] [hp] [hp_offset] [res] [energy] [buff_flag]
int dbin_tower_info(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = dbin_write(conn, cmd, DB_TOWER_INFO, "%d", conn->euser.eid);

	return ret;
}

// CMD: tower_buff [buff_index]
int dbin_tower_buff(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);
	int buff_index = 0;
	int hp_buff = 0;
	int res_buff = 0;
	int energy_buff = 0;

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "already_has_room");
	}

	ret = sscanf(buffer, "%d", &buff_index);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_tower_buff:invalid_input %d", ret);
	}

	if (buff_index <= 0) {
		NET_ERROR_RETURN(conn, -16, "dbin_tower_buff:invalid_buff_index %d", buff_index);
	}

	switch (buff_index) {
	case 1:
		hp_buff = 10;
		break;
	case 2:
		res_buff = 1;
		break;
	case 3:
		energy_buff = 1;
		break;
	default:
		NET_ERROR_RETURN(conn, -6, "dbin_tower_buff:invalid_buff_index %d", buff_index);
		break;
	}


	ret = dbin_write(conn, cmd, DB_TOWER_BUFF, "%d %d %d %d", conn->euser.eid
	, hp_buff, res_buff, energy_buff);

	return 0;
}

// CMD: tower_ladder 
// RET: tower_ladder [eid] [index] [size] [index] [eid] [alias] [icon] [lv] [tower_pos]
int dbin_tower_ladder(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = get_eid(conn);

	if (eid <= 0)  {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = dbin_write(conn, cmd, DB_TOWER_LADDER, "%d", conn->euser.eid);

	return ret;
}

// @see room_game
int dbin_game(connect_t *conn, const char *cmd, const char *buffer)
{
	DEBUG_PRINT(0, "dbio_game");
	room_t * proom = conn->room;
	int eid1 = get_eid(conn);
	int eid2;
	int ret;

	if (proom == NULL) {
		// NET_ERROR_RETURN(conn, -3, "game:null_room");
		NET_ERROR_RETURN(conn, -3, "%s", E_GAME_NULL_ROOM);
	}
	// implicit:  conn->room is non-null

	if (eid1 != proom->guest[0]) {
		// NET_ERROR_RETURN(conn, -6, "game:only_master_can_start");
		NET_ERROR_RETURN(conn, -6, "%s", E_GAME_ONLY_MASTER_START);
	}
	if (proom->num_guest < 2) {
		// NET_ERROR_RETURN(conn, -2, "game:less_guest");
		NET_ERROR_RETURN(conn, -2, "%s", E_GAME_LESS_PLAYER);
	}

	// check opponent guest[1] is online
	connect_t *oppo_conn = get_conn_by_eid(proom->guest[1]);
	if (oppo_conn == NULL) {
		// NET_ERROR_RETURN(conn, -22, "game_opponent_offline");
		NET_ERROR_RETURN(conn, -22, "%s", E_GAME_OPPO_OFFLINE);
	}

	if (proom->state==ST_GAME) {
		// TODO this is ginfo ?
		// NET_ERROR_RETURN(conn, -16, "game_already_started");
		NET_ERROR_RETURN(conn, -16, "%s", E_GAME_ALREADY_START);
	}

	proom->rating[0] = conn->euser.rating;
	proom->rating[1] = oppo_conn->euser.rating;
	proom->game_type = GAME_ROOM;
	sprintf(proom->title, "%s~VS~%s", conn->euser.alias, oppo_conn->euser.alias);
	eid2 = get_eid(oppo_conn);
	// shall we init gameid ?  NO, let out_game do


	ret = dbin_write(conn, cmd, DB_GAME, IN_GAME_PRINT, eid1, eid2);
	return ret;
}

int dbin_quick(connect_t *conn, const char *cmd, const char *buffer)
{
	int eid;
	int ret;
	const char * deck;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "quick:not_login");
	}

	// usually 9 : means leave the quick list
	if (atoi(buffer) > 0)  {
		ret = quick_del(eid);
		if (ret != 0) {
			NET_ERROR_RETURN(conn, -96, "quick_del %d eid=%d", ret, eid);
		}
		// implicit else : return "quick 9", 9 means run away
		net_writeln(conn, "%s 9", cmd);
		return 0;
	}

	room_t * proom = conn->room;
	if (NULL != proom) {
		NET_ERROR_RETURN(conn, -16, "quick:has_room");
	}

	deck = conn->euser.deck;
	if (deck[0] >= '0' && strlen(deck)==EVIL_CARD_MAX) { // 400
		// re-use old code
		WARN_PRINT(0, "not_warn:quick_db:NO eid=%d", eid);
		return quick_game(conn, cmd, buffer);
	}
	WARN_PRINT(0, "not_warn:quick_db:YES eid=%d", eid);

	eid = get_eid(conn);
	ret = dbin_write(conn, cmd, DB_QUICK, IN_LOAD_DECK_PRINT, eid);
	return ret;
}

int update_gate_pos(connect_t *conn, room_t *proom, int winner)
{

	int ret;
	if (proom->game_type != GAME_GATE) {
		return 0;
	}

	if (winner != 1) {
		return 0;
	}

	int gate_pos = proom->guest[1] + 1; // save in out_gate
	if (gate_pos > g_design->design_gate_size) {
		return 0;
	}

	connect_t *pconn;
	pconn = get_conn_by_eid(proom->guest[0]);
	if (pconn != NULL && pconn->euser.gate_pos > gate_pos) {
		// user gate_pos bigger
		return 0;
	}

	// update gate_pos
	ret = dbin_write(conn, "update_gate_pos", DB_UPDATE_GATE_POS
	, "%d %d", proom->guest[0], gate_pos);
	
	return ret;
}


// check range of value: <=p1 or >=p2
// return 1 done,  0 not reach target
int target_range(int value, int p1, int p2)
{
	if (p1>0) {
		if (value < p1) return 0;
	}
	if (p2 > 0) {
		if (value > p2) return 0;
	}
	return 1;
}

/*
// old logic, remove later
int old_get_chapter_star(room_t *proom, int winner, chapter_result_t &chapter_result)
{
	int ret;
	int hero_hp = 0;
	int round = 0;
	int chapter_up_ally = 0;
	int chapter_up_support = 0;
	int chapter_up_ability = 0;
	int chapter_down_ally = 0;
	int chapter_down_support = 0;
	int chapter_down_ability = 0;

	int chapter_up_card_id1 = 0;
	int chapter_up_card_count1 = 0;
	int chapter_up_card_id2 = 0;
	int chapter_up_card_count2 = 0;
	int chapter_up_card_id3 = 0;
	int chapter_up_card_count3 = 0;
	int chapter_down_card_id1 = 0;
	int chapter_down_card_count1 = 0;
	int chapter_down_card_id2 = 0;
	int chapter_down_card_count2 = 0;
	int chapter_down_card_id3 = 0;
	int chapter_down_card_count3 = 0;
	int star = 0;
	ret = lu_get_chapter_target(proom->lua, &hero_hp, &round, &chapter_up_ally, &chapter_up_support, &chapter_up_ability, &chapter_down_ally, &chapter_down_support, &chapter_down_ability, &chapter_up_card_id1, &chapter_up_card_count1, &chapter_up_card_id2, &chapter_up_card_count2, &chapter_up_card_id3, &chapter_up_card_count3, &chapter_down_card_id1, &chapter_down_card_count1, &chapter_down_card_id2, &chapter_down_card_count2, &chapter_down_card_id3, &chapter_down_card_count3);
	if (ret != 0) {
		BUG_PRINT(-6, "get_chapter_star:lu_get_chapter_target_bug");
		return 0;
	}

	DEBUG_PRINT(0, "get_chapter_star:hero_hp=%d round=%d chapter_up_ally=%d chapter_up_support=%d chapter_up_ability=%d chapter_down_ally=%d chapter_down_support=%d chapter_down_ability=%d chapter_up_card_id1=%d chapter_up_card_count1=%d chapter_up_card_id2=%d chapter_up_card_count2=%d chapter_up_card_id3=%d chapter_up_card_count3=%d chapter_down_card_id1=%d chapter_down_card_count1=%d chapter_down_card_id2=%d chapter_down_card_count2=%d chapter_down_card_id3=%d chapter_down_card_count3=%d"
	, hero_hp, round, chapter_up_ally, chapter_up_support, chapter_up_ability, chapter_down_ally, chapter_down_support, chapter_down_ability
	, chapter_up_card_id1, chapter_up_card_count1
	, chapter_up_card_id2, chapter_up_card_count2
	, chapter_up_card_id3, chapter_up_card_count3
	, chapter_down_card_id1, chapter_down_card_count1
	, chapter_down_card_id2, chapter_down_card_count2
	, chapter_down_card_id3, chapter_down_card_count3
	);

	// design_chapter_stage_t *stage = &g_design->design_chapter_list[proom->chapter_id].stage_list[proom->stage_id];
	// design_chapter_target_t *target;

	solo_t * solo = get_design_solo(proom->solo_id);
	solo_target_t *target;


	for (int i=0; i<MAX_SOLO_TARGET; i++) {
		target = solo->target_list + i;

		// value = lu_get_target( target );

		chapter_result.target_list[i] = *target;
		switch (target->target) {
		case CHAPTER_TARGET_MY_HERO_HP:					

			chapter_result.value_list[i] = hero_hp;
			if ((hero_hp <= target->p1 && target->p1 > 0) 
			|| (hero_hp >= target->p2 && target->p2 > 0)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		case CHAPTER_TARGET_ROUND :						
			chapter_result.value_list[i] = round;
			if ((round <= target->p1 && target->p1 > 0) 
			|| (round >= target->p2 && target->p2 > 0)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		case CHAPTER_TARGET_WIN	:
			chapter_result.value_list[i] = winner;
			if (winner == 1) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			// done = (winner == 1);
			break;
		case CHAPTER_TARGET_MY_HAND_ALLY :
			chapter_result.value_list[i] = chapter_up_ally;
			if ((chapter_up_ally <= target->p1 && target->p1 > 0) 
			|| (chapter_up_ally >= target->p2 && target->p2 > 0)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		case CHAPTER_TARGET_MY_HAND_SUPPORT :
			chapter_result.value_list[i] = chapter_up_support;
			if ((chapter_up_support <= target->p1 && target->p1 > 0)
			|| (chapter_up_support >= target->p2 && target->p2 > 0)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		case CHAPTER_TARGET_MY_HAND_ABILITY :
			chapter_result.value_list[i] = chapter_up_ability;
			if ((chapter_up_ability <= target->p1 && target->p1 > 0) 
			|| (chapter_up_ability >= target->p2 && target->p2 > 0)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		case CHAPTER_TARGET_MY_CARD	:
			if (target->p1 == chapter_up_card_id1
			&& target->p2 == chapter_up_card_count1) {
				chapter_result.value_list[i] = chapter_up_card_count1;
				++star;
				chapter_result.done_list[i] = 1;
			}
			if (target->p1 == chapter_up_card_id2
			&& target->p2 == chapter_up_card_count2) {
				chapter_result.value_list[i] = chapter_up_card_count2;
				++star;
				chapter_result.done_list[i] = 1;
			}
			if (target->p1 == chapter_up_card_id3
			&& target->p2 == chapter_up_card_count3) {
				chapter_result.value_list[i] = chapter_up_card_count3;
				++star;
				chapter_result.done_list[i] = 1;
			}
			
			break;
		case CHAPTER_TARGET_OPPO_ALLY :
			chapter_result.value_list[i] = chapter_down_ally;
			if ((chapter_down_ally <= target->p1 && target->p1 > 0)
			|| (chapter_down_ally >= target->p2 && target->p2 > 0)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		case CHAPTER_TARGET_OPPO_SUPPORT :
			chapter_result.value_list[i] = chapter_down_support;
			if ((chapter_down_support <= target->p1 && target->p1 > 0)
			|| (chapter_down_support >= target->p2 && target->p2 > 0)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		case CHAPTER_TARGET_OPPO_ABILITY :
			chapter_result.value_list[i] = chapter_down_ability;
			if ((chapter_down_ability <= target->p1 && target->p1 > 0)
			|| (chapter_down_ability >= target->p2 && target->p2 > 0)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		case CHAPTER_TARGET_OPPO_CARD :	
			if (target->p1 == chapter_down_card_id1
			&& target->p2 == chapter_down_card_count1) {
				chapter_result.value_list[i] = chapter_down_card_count1;
				++star;
				chapter_result.done_list[i] = 1;
			}
			if (target->p1 == chapter_down_card_id2
			&& target->p2 == chapter_down_card_count2) {
				chapter_result.value_list[i] = chapter_down_card_count2;
				++star;
				chapter_result.done_list[i] = 1;
			}
			if (target->p1 == chapter_down_card_id3
			&& target->p2 == chapter_down_card_count3) {
				chapter_result.value_list[i] = chapter_down_card_count3;
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		}
	}

	DEBUG_PRINT(0, "get_chapter_star:star=%d", star);

	// get_chapter_star2(proom, winner, chapter_result);

	return star;
}
*/

// situation 1, check range, p1 and p2 is range, if p1/p2 = 99, means p1/p2 is useless
// 1. p1=0, p2=10,		p1<=counter<=p2 is ok
// 2. p1=99, p2=10,		counter>=p2 is ok
// 3. p1=10, p2=99,		counter<=p1 is ok
int check_target_range(int counter, int p1, int p2) 
{
	if (p1 == CHAPTER_TARGET_POINT_NULL && p2 == CHAPTER_TARGET_POINT_NULL) {
		BUG_PRINT(-6, "check_target_range:both_target_point_null");
		return 0;
	}

	if (p1 != CHAPTER_TARGET_POINT_NULL && p2 != CHAPTER_TARGET_POINT_NULL) {
		if (p1 <= counter && counter <= p2) {
			return 1;
		} 
		return 0;
	}

	if (p1 == CHAPTER_TARGET_POINT_NULL && counter >= p2) {
		return 1;
	}

	if (p2 == CHAPTER_TARGET_POINT_NULL && counter <= p1) {
		return 1;
	}

	return 0;
}

// situation 2, for count card, p1 is card_id, use p2 get check_type and num
// type = p2/10, num = p2%10, so num only can be 0~9
// 1. type = 0, same type, counter == num is ok, e.g. p1=23, p2=3: type=0, num=3
// 2. type = 1, less type, counter <= num is ok, e.g. p1=23, p2=13: type=1, num=3
// 3. type = 2, more type, counter >= num is ok, e.g. p1=23, p2=23: type=2, num=3
int check_target_count(int counter, int p1, int p2) 
{
	if (p2 == CHAPTER_TARGET_POINT_NULL) {
		BUG_PRINT(-6, "check_target_count:target_p2_null");
		return 0;
	}
	int type = p2/10;
	int num = p2%10;
	
	// 0, 1, 2
	if (type != CHAPTER_TARGET_P2_TYPE_SAME
	&& type != CHAPTER_TARGET_P2_TYPE_LESS
	&& type != CHAPTER_TARGET_P2_TYPE_MORE
	) {
		BUG_PRINT(-16, "check_target_count:target_p2_type %d", p2);
		return 0;
	}

	switch (type) {
		case CHAPTER_TARGET_P2_TYPE_SAME:
			if (counter == num) { return 1;}
			break;
		case CHAPTER_TARGET_P2_TYPE_LESS:
			if (counter <= num) { return 1;}
			break;
		case CHAPTER_TARGET_P2_TYPE_MORE:
			if (counter >= num) { return 1;}
			break;
	}

	return 0;
}

int get_chapter_star(room_t *proom, int winner, chapter_result_t &chapter_result)
{
	int star = 0;

	// new logic to get solo target star
	solo_t * solo = get_design_solo(proom->solo_id);
	solo_target_t *target;

	for (int i=0; i<MAX_SOLO_TARGET; i++) {
		target = solo->target_list + i;
		chapter_result.target_list[i] = *target;
		int target_type = target->target;
		int p1 = target->p1;
		int p2 = target->p2;
		int counter = lu_get_solo_target(proom->lua, target_type, p1, p2);
		if (counter < 0) {
			BUG_PRINT(-6, "get_chapter_star:get_solo_target %d %d", solo->id, i);
			counter = 0;
		}

		switch (target_type) {
		case CHAPTER_TARGET_WIN	:
			chapter_result.value_list[i] = winner;
			if (winner == 1) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;

		case CHAPTER_TARGET_MY_CARD	:
		case CHAPTER_TARGET_OPPO_CARD :	
		case CHAPTER_TARGET_MY_CARD_GRAVE :
		case CHAPTER_TARGET_OPPO_CARD_GRAVE :
		case CHAPTER_TARGET_MY_HERO :
		case CHAPTER_TARGET_OPPO_HERO :
			chapter_result.value_list[i] = counter;
			if (check_target_count(counter, p1, p2)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;

		case CHAPTER_TARGET_MY_HERO_HP:					
		case CHAPTER_TARGET_ROUND :						
		case CHAPTER_TARGET_MY_HAND_ALLY :
		case CHAPTER_TARGET_MY_HAND_SUPPORT :
		case CHAPTER_TARGET_MY_HAND_ABILITY :
		case CHAPTER_TARGET_OPPO_ALLY :
		case CHAPTER_TARGET_OPPO_SUPPORT :
		case CHAPTER_TARGET_OPPO_ABILITY :
		case CHAPTER_TARGET_MY_GRAVE :
		case CHAPTER_TARGET_OPPO_GRAVE :
		case CHAPTER_TARGET_MY_GRAVE_ALLY :
		case CHAPTER_TARGET_OPPO_GRAVE_ALLY :
			chapter_result.value_list[i] = counter;
			if (check_target_range(counter, p1, p2)) {
				++star;
				chapter_result.done_list[i] = 1;
			}
			break;
		}
	}

	DEBUG_PRINT(0, "get_chapter_star:star=%d", star);

	return star;
}

// @see logic.lua : game_param_split()
// count(7), game_flag, type_list, ai_max_ally, hp1, hp2, energy1, energy2
int game_param_string(char *param, int game_flag, const char * type_list
, int ai_max_ally, int hp1, int hp2
, int energy1, int energy2) {
	const char *save_type_list = "0";
	if (type_list != NULL && strlen(type_list) > 0) {
		// XXX what if type_list has space?
		save_type_list = type_list;
	}

	sprintf(param, GAME_PARAM_PRINT
	, game_flag, save_type_list
	, ai_max_ally
	, hp1, hp2
	, energy1, energy2);
	return 0;
}

// TODO add chapter star
int dbin_save_replay(connect_t * conn, room_t *proom, int winner
, int ver, int star, const char *cmd_list)
{

	/*
	// fight with robot will not save replay
	if ((proom->game_type == GAME_VS_GOLD 
	|| proom->game_type == GAME_VS_CRYSTAL
	|| proom->game_type == GAME_VS_FREE
	|| proom->game_type == GAME_SOLO_GOLD
	|| proom->game_type == GAME_SOLO_FREE
	) && proom->guest[1] <= MAX_AI_EID) {
		return 0;
	}
	*/

	int ret;
	// gameid, game_type, winner, star, seed, 
	// start_side, ver, eid1, eid2, lv1
	// lv2, icon1, icon2, alias1, alias2
	// deck1, deck2, param, cmd

	char deck1[EVIL_CARD_MAX + 5];
	char deck2[EVIL_CARD_MAX + 5];
	char param[REPLAY_PARAM_MAX + 5];

	int card_len1 = 1;
	int card_len2 = 1;
	if (strlen(proom->deck[0]) != EVIL_CARD_MAX) {
		card_len1 = count_card(proom->deck[0]);
	}
	sprintf(deck1, "%d %s", card_len1, proom->deck[0]);

	if (strlen(proom->deck[1]) != EVIL_CARD_MAX) {
		card_len2 = count_card(proom->deck[1]);
	}
	sprintf(deck2, "%d %s", card_len2, proom->deck[1]);

	game_param_string(param, proom->game_flag, proom->type_list
	, proom->ai_max_ally, proom->hp1, proom->hp2, proom->energy1, proom->energy2);

	// save_replay fail is not important
	ret = dbin_write(conn, "sreplay", DB_SAVE_REPLAY, IN_SAVE_REPLAY_PRINT
	, proom->gameid, proom->game_type, winner, star, proom->seed
	, proom->start_side, ver, proom->guest[0], proom->guest[1], proom->lv[0]
	, proom->lv[1], proom->icon[0], proom->icon[1], proom->alias[0], proom->alias[1]
	, deck1, deck2, param, cmd_list);
	// db_save_replay(proom->gameid, win, proom->seed, ver
	ERROR_NEG_PRINT(ret, "dbin_win:save_replay");

	return ret;
}

int dbin_win_param(connect_t *conn, room_t *proom, int winner, double rating
, int eid1, int eid2, int gold1, int crystal1, int gold2, int crystal2
, int exp1, int lv_offset1, int exp2, int lv_offset2
, int lv1, int lv2
, int card_id1, int card_id2
, int icon1, int icon2, char *alias1, char *alias2
, int ai_times1, int ai_times2, int gold_times1, int gold_times2
, int crystal_times1, int crystal_times2
, long gameid, int seed, int start_side, int ver, int chapter_star
, const char *deck1, const char *deck2, const char *cmd_list)
{
	int ret;

	// update euser in memory (for reference) @see dbin_status()
	connect_t *conn1, *conn2;
	conn1 = get_conn_by_eid(eid1);
	conn2 = get_conn_by_eid(eid2);
	if (conn1 != NULL) {
		conn1->euser.gold += gold1;
		conn1->euser.crystal += crystal1;

		conn1->euser.fight_ai_time += ai_times1;
		conn1->euser.fight_gold_time += gold_times1;
		conn1->euser.fight_crystal_time += crystal_times1;
		// conn1 conn2 are different
		if (winner == 1) {
			conn1->euser.rating += rating;

			if (eid2 == FIGHT_SIGNAL_AI_EID) {
				if ((proom->game_type == GAME_SOLO_GOLD)
				&& (user_signals_check(conn1->euser, SIGNAL_FIGHT_AI) == 0))
				{
					conn1->euser.signals[SIGNAL_FIGHT_AI] = '1';
				}
				else if ((proom->game_type == GAME_SOLO_FREE)
				&& (user_signals_check(conn1->euser, SIGNAL_FIGHT_AI_FREE) == 0))
				{
					conn1->euser.signals[SIGNAL_FIGHT_AI_FREE] = '1';
				}
			}

		} else if (winner == 2) {
			conn1->euser.rating -= rating;
		} // no bonus for draw

		dbin_write(conn, "update_signals", DB_UPDATE_SIGNALS, "%d %.30s"
		, conn1->euser.eid, conn1->euser.signals);
		// DEBUG_PRINT(0, "win_param:eid=%d winner=%d rating=%lf", eid1, winner, conn1->euser.rating);
	}
	if (conn2 != NULL) {
		conn2->euser.gold += gold2;
		conn2->euser.crystal += crystal2;

		conn2->euser.fight_ai_time += ai_times2;
		conn2->euser.fight_gold_time += gold_times2;
		conn2->euser.fight_crystal_time += crystal_times2;

		if (winner == 1) {
			conn2->euser.rating -= rating;
		} else if (winner == 2) {
			conn2->euser.rating += rating;
		}

		// TODO db_write(update_signal)
		dbin_write(conn, "update_signals", DB_UPDATE_SIGNALS, "%d %.30s"
		, conn2->euser.eid, conn2->euser.signals);

		// DEBUG_PRINT(0, "win_param:eid=%d winner=%d rating=%lf", eid2, winner, conn2->euser.rating);
	}

	// DEBUG_PRINT(0, "exp1=%d, exp2=%d, lv_offset1=%d, lv_offset2=%d", exp1, exp2, lv_offset1, lv_offset2);
	// DEBUG_PRINT(0, "icon1=%d, icon2=%d, alias1=%s, alias2=%s", icon1, icon2
	// , alias1, alias2);
	

	// core logic:
	ret = dbin_write(conn, "win", DB_WIN, IN_WIN_PRINT, winner, rating
	, eid1, eid2, gold1, crystal1, gold2, crystal2
	, exp1, lv_offset1, exp2, lv_offset2, card_id1, card_id2
	, ai_times1, ai_times2, gold_times1, gold_times2
	, crystal_times1, crystal_times2);
	ERROR_NEG_PRINT(ret, "dbin_win:win_write");

	// update gate_pos
	ret = update_gate_pos(conn, proom, winner);
	ERROR_NEG_PRINT(ret, "dbin_win:update_gate_pos");

	if (proom->game_type == GAME_TOWER) {
		int win_flag = 0;
		if (winner == 1) { win_flag = 1;}
		lua_getglobal(proom->lua, "get_hero_hp");
		lua_pushinteger(proom->lua, 1); 
		ret = lua_pcall(proom->lua, 1, 1, 0); //1=input param, 1=output return
		if (0 != ret) {
			lua_pop(proom->lua, 1); 
			BUG_PRINT(-66, "get_hero_hp_fail");
		}
		int hero_hp = lua_tointeger(proom->lua, -1);
		lua_pop(proom->lua, 1);
		DEBUG_PRINT(0, "hero_hp=%d", hero_hp);
		LU_CHECK_MAGIC(proom->lua);
		ret = dbin_write(conn, "tower_result", DB_TOWER_RESULT
		, "%d %d %d %d", proom->guest[0], proom->tower_pos, win_flag, hero_hp);
	}

	dbin_save_replay(conn, proom, winner, ver, chapter_star, cmd_list);

	return ret;
}


/***  sample game start flow
cli 0:
log x x
log 47 5 x		// out
room 1
room 1 1 10 47 x		// out
join 1 1 10 47 x 48 y	// out (after y join)
game	// in
game 1 0 3122 1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 0000100000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000


cli 1:
log y y  
log 48 5 y   // out
join 1 1
join 1 1 10 47 x 48 y  // out
game 2 0 122 1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000 0000100000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
***/



int dbin_alias(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int len;
	evil_user_t *puser;
	char alias[EVIL_ALIAS_MAX+5];
	

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "alias:not_login");
	}
	len = strlen(buffer);
	if (len <= 0) {
		NET_ERROR_RETURN(conn, -5, "alias:invalid_input");
	}
	if (len > EVIL_ALIAS_MAX) {
		NET_ERROR_RETURN(conn, -2, "alias:too_long %d %d", EVIL_ALIAS_MAX, len);
	}
	if (buffer[0] =='_') {
		NET_ERROR_RETURN(conn, -15, "alias:invalid_char[_]");
	}
	
	// if alias same as now alias, return
	puser = &conn->euser;
	if (puser->eid <= 0) {
		NET_ERROR_RETURN(conn, -19, "alias:not_login %d", puser->eid);
	}

	ret = sscanf(buffer, "%30s", alias);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -25, "alias:invalid_input");
	}

	if (strlen(puser->alias) == strlen(alias)) {
		for (int i=0;i<(int)strlen(alias);i++) {
			if (puser->alias[i] != alias[i]) {
				ret = dbin_write(conn, cmd, DB_ALIAS
				, IN_ALIAS_PRINT, eid, buffer);
				return ret;
			}
		}
		net_writeln(conn, "%s %d %s", cmd, 0, alias); 	
	} else {
		ret = dbin_write(conn, cmd, DB_ALIAS, IN_ALIAS_PRINT, eid, buffer);
	}


	return ret;
}

// get random alias
// CMD: ralais [type]
// type: 0 for male, 1 for female
int cmd_random_alias(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int type;
	int r1, r2;
	char alias[EVIL_ALIAS_MAX];
	bzero(alias, sizeof(alias));

	ret = sscanf(buffer, "%d", &type);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "ralias:invalid_input");
	}

	if (type != 0 && type != 1) {
		NET_ERROR_RETURN(conn, -15, "ralias:invalid_type");
	}

	if (type == 0) {
		r1 = abs(random()) % g_name_xing.count;
		r2 = abs(random()) % g_name_boy.count;
		sprintf(alias, "%s%s", g_name_xing.name_list[r1]
		, g_name_boy.name_list[r2]);
	} 
	if (type == 1) {
		r1 = abs(random()) % g_name_xing.count;
		r2 = abs(random()) % g_name_girl.count;
		sprintf(alias, "%s%s", g_name_xing.name_list[r1]
		, g_name_girl.name_list[r2]);
	} 

	// DEBUG_PRINT(0, "cmd_random_alias:alias=%s", alias);

	net_writeln(conn, "%s %d %s", cmd, type, alias); 	

	return ret;
}

// CMD: sdebug [eid] [filename] [content]
int dbin_save_debug(connect_t *conn, const char *cmd, const char *buffer)
{
	int eid;
	int ret;
	int n;
	char filename[100];

	ret = sscanf(buffer, "%d %99s %n", &eid, filename, &n);
	if (ret < 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input [eid] [filename] [content]");
	}


	ret = dbin_write(conn, cmd, DB_SAVE_DEBUG, IN_SAVE_DEBUG_PRINT
		, eid, filename, buffer+n);
	return ret;
}

// CMD: ldebug [filename]
// RET: ldebug [eid] [filename] [content]
int dbin_load_debug(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	char filename[100];

	ret = sscanf(buffer, "%99s", filename);
	if (ret < 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input [filename]");
	}

	ret = dbin_write(conn, cmd, DB_LOAD_DEBUG, IN_LOAD_DEBUG_PRINT, filename);
	return ret;
}

int dbin_status(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "sta:not_login");
	}

	ret = dbin_write(conn, cmd, DB_STATUS, IN_STATUS_PRINT, eid);
	return ret;
}

// buy card
// ----- buy [card_id] [card_type] [money_type]
// CMD: buy [card_id] [card_type] [money_type]
// card_type : 0==card, 1==piece
// RET: buy [card_id] [card_type] [money_type] [gold_offset] [crystal_offset]
// RET: buy [err_code] [err_msg]
int dbin_buy_card(connect_t *conn, const char *cmd, const char *buffer)
{
//	int fd = g_dbio_fd[0];   // hard code, TODO round robin
//	int cid = get_conn_id(conn);
//	int size, len;
	int ret;
	int eid;
	int card_id;
	int card_type = -1; //0=card, 1=piece
	int money_type = -1; //0=gold, 1=crystal
	int buy_count;
	int gold;
	int crystal;
//	char in_buffer[DB_BUFFER_MAX];

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_buy_card:not_login");
	}

	// DEBUG_PRINT(0, "dbin_buy_card:buffer=%s", buffer);

	ret = sscanf(buffer, "%d %d %d %d", &card_id, &card_type, &money_type, &buy_count);
	if (ret != 4) {
		NET_ERROR_RETURN(conn, -5, "dbin_buy:input %d", ret);
	}

	if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
		NET_ERROR_RETURN(conn, -15, "dbin_buy:invalid card_id=%d", card_id);
	}

	shop_t c;
	c = g_design->shop_list[card_id];
	if (c.card_id == 0) {
		NET_ERROR_RETURN(conn, -6, "dbin_buy_card:no_such_card %d", card_id);
	}
//	DEBUG_PRINT(0, "dbin_buy:c.card_buy_gold=%d, c.card_buy_crystal=%d", c.card_buy_gold, c.card_buy_crystal);
	
	// check card_type
	if (card_type != BUY_TYPE_CARD && card_type != BUY_TYPE_PIECE) {
		NET_ERROR_RETURN(conn, -25, "dbin_buy:card_type %d", card_type);
	}

	// check money_type
	if (money_type != BUY_TYPE_GOLD && money_type != BUY_TYPE_CRYSTAL) {
		NET_ERROR_RETURN(conn, -35, "dbin_buy:money_type %d", money_type);
	}

	// check card/piece can buy
	if (money_type == BUY_TYPE_GOLD && card_type == BUY_TYPE_CARD && c.card_buy_gold >= 0) {
		NET_ERROR_RETURN(conn, -16, "dbin_buy:card_cannot_buy_by_gold %d %d %d"
		, card_id, money_type, c.card_buy_gold);
	}

	if (money_type == BUY_TYPE_CRYSTAL && card_type == BUY_TYPE_CARD && c.card_buy_crystal >= 0) {
		NET_ERROR_RETURN(conn, -16, "dbin_buy:card_cannot_buy_by_crystal %d %d %d"
		, card_id, money_type, c.card_buy_crystal);
	}

	if (money_type == BUY_TYPE_GOLD && card_type == BUY_TYPE_PIECE && c.piece_buy_gold >= 0) {
		NET_ERROR_RETURN(conn, -16, "dbin_buy:piece_cannot_buy_by_gold %d %d %d"
		, card_id, money_type, c.piece_buy_gold);
	}

	if (money_type == BUY_TYPE_CRYSTAL && card_type == BUY_TYPE_PIECE && c.piece_buy_crystal >= 0) {
		NET_ERROR_RETURN(conn, -16, "dbin_buy:piece_cannot_buy_by_crystal %d %d %d"
		, card_id, money_type, c.piece_buy_crystal);
	}


	// get need money
	if (money_type == BUY_TYPE_GOLD) {
		if (card_type == BUY_TYPE_CARD) {
			gold = c.card_buy_gold * buy_count;
			crystal = 0;
		} else {
			gold = c.piece_buy_gold * buy_count;
			crystal = 0;
		}
	} else {
		if (card_type == BUY_TYPE_CARD) {
			gold = 0;
			crystal = c.card_buy_crystal * buy_count;
		} else {
			gold = 0;
			crystal = c.piece_buy_crystal * buy_count;
		}
	}

	if (gold > 0 || crystal > 0) {
		BUG_PRINT(-6, "dbin_buy:buy_money_positive %d %d %d", card_id, gold, crystal);
		NET_ERROR_RETURN(conn, -16, "dbin_buy:card_cannot_buy %d", card_id);
	}


	// check money enough
	// peter: client should already block this
	if (conn->euser.gold + gold < 0) {
		NET_ERROR_RETURN(conn, -25, "dbin_buy_card:gold_not_enough euser.gold=%d, need_gold=%d", conn->euser.gold, gold);
	}

	if (conn->euser.crystal + crystal < 0){
		NET_ERROR_RETURN(conn, -35, "dbin_buy_card:crystal_not_enough euser.crystal=%d, need_crystal=%d", conn->euser.crystal, crystal);
	}


	ret = dbin_write(conn, cmd, DB_BUY_CARD, IN_BUY_CARD_PRINT
		, eid, card_id, card_type, money_type, buy_count, gold, crystal);
	return ret;
/****
	// beware of double \n
	len = sprintf(in_buffer, "%d %d ", cid, DB_BUY_CARD);
	len += sprintf(in_buffer+len, IN_BUY_CARD_PRINT
		, eid, card_id, money_type, gold, crystal);

	conn->db_flag = time(NULL); // core logic

	DEBUG_PRINT(0, "dbin_buy_card:in_buffer=%s", in_buffer);
	size = write(fd, in_buffer, len);
	FATAL_EXIT(size-len, "dbin_buy_card:write %d / %d %s", size, len, in_buffer);
	return 0;
****/
}

// sell card
// ----- sell [card_id] [card_type] [money_type]
// CMD: sell [card_id] [card_type] [money_type]
// card_type : 0==card, 1==piece
// RET: sell [card_id] [card_type] [money_type] [gold_offset] [crystal_offset]
// RET: sell [err_code] [err_msg]
int dbin_sell_card(connect_t *conn, const char *cmd, const char *buffer)
{
//	int fd = g_dbio_fd[0];   // hard code, TODO round robin
//	int cid = get_conn_id(conn);
//	int size, len;
	int ret;
	int eid;
	int card_id;
	int card_type; //0=card, 1=piece
	int money_type = -1; //0=gold, 1=crystal
	int sell_count;
	int gold;
	int crystal;
//	char in_buffer[DB_BUFFER_MAX];

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_sell_card:not_login");
	}

	ret = sscanf(buffer, "%d %d %d %d", &card_id, &card_type, &money_type
	, &sell_count);
	if (ret != 4) {
		NET_ERROR_RETURN(conn, -5, "dbin_sell:input %d", ret);
	}

	if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
		NET_ERROR_RETURN(conn, -15, "dbin_sell:invalid card_id=%d", card_id);
	}

	if (money_type != 0 && money_type != 1) {
		NET_ERROR_RETURN(conn, -25, "dbin_sell:money_type %d", money_type);
	}

	if (sell_count <= 0) {
		NET_ERROR_RETURN(conn, -35, "dbin_sell:sell_count %d", sell_count);
	}

	shop_t c;
	c = g_design->shop_list[card_id];
	if (c.card_id == 0) {
		// net_write_space(conn, cmd);
		NET_ERROR_RETURN(conn, -6, "dbin_sell_card:no_such_card %d", card_id);
	}
	DEBUG_PRINT(0, "dbin_buy:c.card_buy_gold=%d, c.card_buy_crystal=%d", c.card_buy_gold, c.card_buy_crystal);
	
	// check card_type
	if (card_type != 0 && card_type != 1) {
		NET_ERROR_RETURN(conn, -55, "dbin_sell:card_type %d", card_type);
	}

	// check money_type
	if (money_type != 0 && money_type != 1) {
		NET_ERROR_RETURN(conn, -65, "dbin_sell:money_type %d", money_type);
	}


	if (money_type == 0) {
		if (card_type == 0) {
			gold = c.card_sell_gold * sell_count;
			crystal = 0;
		} else {
			gold = c.piece_sell_gold * sell_count;
			crystal = 0;
		}
	} else {
		if (card_type == 0) {
			gold = 0;
			crystal = c.card_sell_crystal * sell_count;
		} else {
			gold = 0;
			crystal = c.piece_sell_crystal * sell_count;
		}
	}

	if (gold < 0 || crystal < 0) {
		BUG_PRINT(-6, "dbin_sell:sell_money_negative %d %d %d", card_id, gold, crystal);
		NET_ERROR_RETURN(conn, -16, "dbin_sell:card_cannot_sell %d", card_id);
	}
		
		

	ret = dbin_write(conn, cmd, DB_SELL_CARD, IN_SELL_CARD_PRINT
		, eid, card_id, card_type, money_type, sell_count, gold, crystal);
	return ret;
/***
	// beware of double \n
	len = sprintf(in_buffer, "%d %d ", cid, DB_SELL_CARD);
	len += sprintf(in_buffer+len, IN_BUY_CARD_PRINT	// peter: SELL ?
		, eid, card_id, money_type, gold, crystal);

	conn->db_flag = time(NULL); // core logic

	DEBUG_PRINT(0, "dbin_sell_card:in_buffer=%s", in_buffer);
	size = write(fd, in_buffer, len);
	FATAL_EXIT(size-len, "dbin_sell_card:write %d / %d %s", size, len, in_buffer);

	return 0;
**/
}


int dbin_save_batch(connect_t *conn, int eid, int ptype, int *card_list, int gold, int crystal) {
	int ret;
	
	ret = dbin_write(conn, "batch", DB_SAVE_BATCH, IN_SAVE_BATCH_PRINT, eid, ptype
	, card_list[0], card_list[1], card_list[2], card_list[3], card_list[4], card_list[5], gold, crystal);
	
	ERROR_NEG_PRINT(ret, "dbin_save_batch:write");

	return ret;
}


// CMD: batch [type] [refresh_optional]
// if refresh == 1 then always refresh random batch
// if refresh != 1 (or missing) : load from database
int dbin_batch(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int batch_type;
	int refresh; // default no refresh
	int card_list[MAX_LOC];
	int star_list[MAX_LOC];
	bzero(card_list, MAX_LOC);
	bzero(star_list, MAX_LOC);

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_batch:not_login");
	}

	DEBUG_PRINT(0, "dbin_batch:buffer=%s", buffer);

	refresh = 0;
	ret = sscanf(buffer, "%d %d", &batch_type, &refresh);
	DEBUG_PRINT(0, "dbin_batch:sscanf=%d", ret);
	if (ret < 1) {  // refresh is optional ret=1 or 2 are ok
		NET_ERROR_RETURN(conn, -5, "dbin_batch:input %d", ret);
	}

	if (batch_type >= MAX_PICK || batch_type < 0) {
		NET_ERROR_RETURN(conn, -5, "dbin_batch:batch_type_wrong %d", batch_type);
	}
	DEBUG_PRINT(0, "dbin_batch:batch_type=%d", batch_type);


	if (refresh == 1) {
		// 1. generate random batch
		// 2. save to database
		// 3. change euser money
		// 4. write to client

		// 1:
		pick_t *pick;
		pick = g_design->pick_list+batch_type;
		ret = get_random_batch(pick, star_list, card_list);
		NET_ERROR_RETURN(conn, ret, "dbin_batch:random");

		// 2:
		// this get_eid() is safe!
		// need money
		int gold = -g_design->constant.batch_refresh_gold;
		int crystal = -g_design->constant.batch_refresh_crystal;

		if (batch_type == 0 && (conn->euser.gold+gold)<0) {
			NET_ERROR_RETURN(conn, -25, "dbin_batch:gold_not_enough euser.gold=%d, gold=%d eid=%d", conn->euser.gold, gold, eid);
		}
		if (batch_type == 1 && (conn->euser.crystal+crystal)<0){
			NET_ERROR_RETURN(conn, -35, "dbin_batch:crystal_not_enough euser.crystal=%d, crystal=%d eid=%d", conn->euser.crystal, crystal, eid);
		}

		if (batch_type == 1) {
			gold = 0;
		} else {
			crystal = 0;
		}

		ret = dbin_save_batch(conn, get_eid(conn), batch_type, card_list
		, gold, crystal); // db_flag = 1 is useless here

		// 3:
		conn->euser.gold += gold;
		conn->euser.crystal += crystal;

		// 4:
		// write_batch(conn, cmd, batch_type, card_list);
		for (int loc=0; loc<MAX_LOC; loc++) {
			conn->euser.batch_list[batch_type][loc] = card_list[loc];
		}
		// output new generate random batch
		net_write_space(conn, "%s %d %d %d %d", cmd, batch_type, refresh, gold, crystal);
		for (int i=0; i<MAX_LOC; i++) {
			net_write_space(conn, "%d", card_list[i]);
		}
		net_write(conn, "", '\n');
		return ret;
	}

	evil_user_t *euser = &conn->euser;

	// debug
	printf("------- in_batch -------\ncard_list:\t");
	for (int loc=0; loc<MAX_LOC; loc++) {
		printf("%d\t", euser->batch_list[batch_type][loc]);
	}
	printf("\n");

	/*
	pick_t *pick;
	pick = g_pick_list+batch_type;
	ret = get_random_loc(pick);
	printf("in_batch:get_random_loc=%d\n", ret);
	*/
	//

	int count = 0;
	for (int loc=0; loc<MAX_LOC; loc++) {
		if(0 == euser->batch_list[batch_type][loc]
		|| EVIL_CARD_MAX < euser->batch_list[batch_type][loc]) {
			count++;
		}
	}
	if (count == 0) {
		write_batch(conn, cmd, batch_type, euser->batch_list[batch_type]);	
		return 0;
	}
	if (count > 0 && count < MAX_LOC) {
		BUG_PRINT(-6, "in_batch:batch_list_bug %d", eid);
	}


	ret = dbin_write(conn, cmd, DB_LOAD_BATCH, IN_LOAD_BATCH_PRINT
		, eid, batch_type);
	
	// return ret;

	/*

	ret = get_random_batch(g_pick_list+batch_type, star_list, card_list);
	if (ret != 0) {
		BUG_PRINT(ret, "dbin_batch:get_random_batch_bug %d", eid);
		NET_ERROR_RETURN(conn, -5, "dbin_batch:input %d", ret);
	}
	*/

	/*
	pick = g_pick_list + batch_type;

	if (pick == NULL) {
		ret = -3;
		BUG_PRINT(ret, "dbin_batch:pick_null %d", batch_type);
		NET_ERROR_RETURN(conn, ret, "dbin_batch:pick_null %d", batch_type);
	}
			
	// prepare star_list  e.g.:
	// star1 star2 star3 star4 star5
	// 3     2     0     2     3          	sum = 10
	// 1-3   4-6   X     7-8   8-10			range
	// random => 6  -> fit into 4-6 : star2
	// random => 9  -> fit into 8-10 : star5
	for (int loc=0; loc<MAX_LOC; loc++) {
		sum = 0;
		for (int star=0; star<MAX_STAR; star++) {
			sum += pick->batch_rate[loc][star];
		}

		r = (abs(random()) % sum) + 1;
		DEBUG_PRINT(0, "dbin_batch:r=%d", r);
		int rate = 0;
		for (int star=0; star<MAX_STAR; star++) {
			rate += pick->batch_rate[loc][star];
			DEBUG_PRINT(0, "dbin_batch:batach_rate[%d][%d]=%d, rate=%d"
			, loc, star, pick->batch_rate[loc][star], rate);
			if (r <= rate) {
				star_list[loc] = star;
				break;
			}
		}
	}

	// prepare card_list  (according to star_list)
	int size;
	for (int loc=0; loc<MAX_LOC; loc++) {
		int star;
		card_t card;
		star = star_list[loc];
		size = g_star_list[star].size(); // .size() maybe in size_t (int)
		r = (abs(random()) % size);  // r = 0 to size-1
		card = g_star_list[star][r];
		if (card.id == 0) {
			ret = -7;
			BUG_PRINT(ret, "dbin_batch:card_null");
			NET_ERROR_RETURN(conn, ret, "dbin_batch:card_id_null");
		}
		if (card.star != star+1) { // star + 1 (base0),  card.star is base1 
			ret = -17;
			BUG_PRINT(ret, "dbin_batch:star_mismatch %d %d", card.star, star+1);
			NET_ERROR_RETURN(conn, ret, "dbin_batch:star_mismatch");
		}
			
		DEBUG_PRINT(0, "card.id=%d, card.name=%s, card.star=%d"
			, card.id, card.name, card.star);

		card_list[loc] = card.id;  // core logic
	}

	*/

	/*
	// debug print
	printf("------ star_list -------\n");
	for (int loc=0; loc<MAX_LOC; loc++) {
		printf("%d\t", star_list[loc]);
	}
	printf("\n");
	printf("------ card_list -------\n");
	for (int loc=0; loc<MAX_LOC; loc++) {
		printf("%d\t", card_list[loc]);
	}
	printf("\n");
	*/


	/*
	// output 
	net_write_space(conn, "%s %d", cmd, batch_type);
	for (int i=0; i<MAX_LOC; i++) {
		net_write_space(conn, "%d", card_list[i]);
	}
	for (int i=0; i<MAX_LOC; i++) {
		net_write_space(conn, "%d", star_list[i]);
	}
	net_write(conn, "", '\n');
	*/

	return ret;
}

int dbin_pick(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int batch_type;
	int card_id;
	int gold;
	int crystal;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "in_pick:not_login");
	}

	DEBUG_PRINT(0, "in_pick:buffer=%s", buffer);

	ret = sscanf(buffer, "%d", &batch_type);
	DEBUG_PRINT(0, "in_pick:sscanf=%d", ret);
	if (ret != 1) {  // refresh is optional ret=1 or 2 are ok
		NET_ERROR_RETURN(conn, -5, "in_pick:input %d", ret);
	}

	if (batch_type >= MAX_PICK || batch_type < 0) {
		NET_ERROR_RETURN(conn, -5, "in_pick:batch_type_wrong %d", batch_type);
	}
	DEBUG_PRINT(0, "in_pick:batch_type=%d", batch_type);

	evil_user_t *euser = &conn->euser;
	// debug
	printf("------- in_pick -------\ncard_list:\t");
	for (int loc=0; loc<MAX_LOC; loc++) {
		printf("%d\t", euser->batch_list[batch_type][loc]);
	}
	printf("-------\n");
	//

	// check batch
	int count = 0;
	for (int loc=0; loc<MAX_LOC; loc++) {
		if(0 == euser->batch_list[batch_type][loc] 
		|| EVIL_CARD_MAX < euser->batch_list[batch_type][loc]) {
			count++;
		}
	}

	if (count > 0) {
		NET_ERROR_RETURN(conn, -6, "in_pick:batch_null");
	}
	//

	// check money
	gold = -g_design->constant.pick_gold;
	crystal = -g_design->constant.pick_crystal;
	DEBUG_PRINT(0, "in_pick:gold=%d crystal=%d", gold, crystal);

	if (batch_type == 0 && (conn->euser.gold+gold)<0) {
		NET_ERROR_RETURN(conn, -25, "in_pick:gold_not_enough euser.gold=%d, gold=%d eid=%d", conn->euser.gold, gold, eid);
	}
	if (batch_type == 1 && (conn->euser.crystal+crystal)<0){
		NET_ERROR_RETURN(conn, -35, "in_pick:crystal_not_enough euser.crystal=%d, crystal=%d eid=%d", conn->euser.crystal, crystal, eid);
	}
	//

	if (batch_type == 1) {
		gold = 0;
	} else {
		crystal = 0;
	}

	// get card_id
	pick_t *pick;
	pick = g_design->pick_list+batch_type;
	int l;
	ret = get_random_loc(pick, &l);
	printf("in_pick:get_random_loc=%d\n", l);
	card_id = euser->batch_list[batch_type][l]; 
	printf("in_pick:card_id=%d\n", card_id);

	ret = dbin_write(conn, cmd, DB_PICK, IN_PICK_PRINT
		, eid, batch_type, l, card_id, gold, crystal);

	return ret;
}

int get_lottery_card(int team, int money_type)
{

	design_lottery_t *lottery = &g_design->lottery_info;

	design_lottery_single_t *lottery_list = NULL;
	int *lottery_list_size = NULL;
	int *lottery_list_max_weight = NULL;

	int card_id = 0;

	if (team == 1) {
		if (money_type == 1) {
			lottery_list = lottery->gold_normal_lottery;
			lottery_list_size = &(lottery->gold_normal_lottery_size);
			lottery_list_max_weight = &(lottery->gold_normal_lottery_max_weight);
		} else if (money_type == 2) {
			lottery_list = lottery->crystal_normal_lottery;
			lottery_list_size = &(lottery->crystal_normal_lottery_size);
			lottery_list_max_weight = &(lottery->crystal_normal_lottery_max_weight);
		}
	} else if (team == 2) {
		if (money_type == 1) {
			lottery_list = lottery->gold_special_lottery;
			lottery_list_size = &(lottery->gold_special_lottery_size);
			lottery_list_max_weight = &(lottery->gold_special_lottery_max_weight);
		} else if (money_type == 2) {
			lottery_list = lottery->crystal_special_lottery;
			lottery_list_size = &(lottery->crystal_special_lottery_size);
			lottery_list_max_weight = &(lottery->crystal_special_lottery_max_weight);
		}
	} else if (team == 3) {
		if (money_type == 1) {
			lottery_list = lottery->gold_once_lottery;
			lottery_list_size = &(lottery->gold_once_lottery_size);
			lottery_list_max_weight = &(lottery->gold_once_lottery_max_weight);
		} else if (money_type == 2) {
			lottery_list = lottery->crystal_once_lottery;
			lottery_list_size = &(lottery->crystal_once_lottery_size);
			lottery_list_max_weight = &(lottery->crystal_once_lottery_max_weight);
		}
	}

	if (lottery_list == NULL || lottery_list_size == NULL || lottery_list_max_weight == NULL) {
		ERROR_RETURN(-3, "get_lottery_card:lottery_null");
	}

	if (*lottery_list_size <= 0) {
		BUG_RETURN(-6, "get_lottery_card:lottery_list_size_negative");
	}

	if (*lottery_list_max_weight <= 0) {
		BUG_RETURN(-16, "get_lottery_card:lottery_list_max_weight_negative");
	}


	// e.g. lottery_list_max_weight = 50;
	// weight_start = 1, weight = 30, weight_end = 31;
	// weight_start = 31, weight = 20, weight_end = 51;
	// r = 1 ~ 50
	// if (r>=weight_start && r<weight_end)

	int r = (abs(random() % *lottery_list_max_weight-1)) + 1;
	// DEBUG_PRINT(0, "in_lottery:r=%d", r);
	int list_id = -1;

	for (int i=0; i<*lottery_list_size; i++) {
		if (r >= lottery_list[i].weight_start && r < lottery_list[i].weight_end) {
			list_id = i;
			break;
		}
	}
	// DEBUG_PRINT(0, "get_lottery_card:list_id=%d", list_id);


	int size = lottery_list[list_id].size; // .size() maybe in size_t (int)
	int pos = (abs(random()) % size);  // r = 0 to size-1
	card_id = lottery_list[list_id].cards[pos];

	// DEBUG_PRINT(0, "get_lottery_card:r=%d list_id=%d size=%d pos=%d card_id=%d"
	// , r, list_id, size, pos, card_id);

	return card_id;
}


// CMD:lottery [type] [times]
// type: 0 for get price
// RET:lottery [0] [gold_one] [crystal_one] [gold_ten] [crystal_ten]
// type: 1 for gold lottery, 2 for crystal lottery
int dbin_lottery(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int type;
	int times;

	int MAX_LOTTERY_TIMES = 10;
	int LOTTERY_TEAM_NORMAL = 1;
	int LOTTERY_TEAM_SPECIAL = 2;
	int LOTTERY_TEAM_ONCE = 3;

	int gold;
	int crystal;

	int gold_one;
	int crystal_one;
	int gold_ten;
	int crystal_ten;

	int once_signal = -1;

	int card_id = -1;
	// int card_list[MAX_LOTTERY_TIMES];

	char out_buffer[200];
	char * ptr;

	gold = 0;
	crystal = 0;

	gold_one = -g_design->constant.lottery_one_gold;
	crystal_one = -g_design->constant.lottery_one_crystal;
	gold_ten = -g_design->constant.lottery_ten_gold;
	crystal_ten = -g_design->constant.lottery_ten_crystal;

	// DEBUG_PRINT(0, "dbin_lottery:gold_one=%d crystal_one=%d gold_ten=%d crystal_ten=%d"
	// , gold_one, crystal_one, gold_ten, crystal_ten);

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "dbin_lottery:not_login");
	}

	ret = sscanf(buffer, "%d %d", &type, &times);
	if (type == 0) {
		net_writeln(conn, "%s %d %d %d %d %d %d", cmd, eid, type
		, gold_one, crystal_one, gold_ten, crystal_ten);
		return 0;
	}

	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "dbin_lottery:input_error");
	}

	if (type != 1 && type != 2) {
		NET_ERROR_RETURN(conn, -15, "dbin_lottery:money_type_error");
	}

	if (times != 1 && times != MAX_LOTTERY_TIMES) {
		NET_ERROR_RETURN(conn, -25, "dbin_lottery:times_error");
	}

	if (type == 1) {
		// gold
		if (times == 1) {
			gold = gold_one;
			crystal = 0;
			once_signal = user_signals_check(conn->euser
			, SIGNAL_LOTTERY_GOLD_ONE);
		} else {
			gold = gold_ten;
			crystal = 0;
		}
	} else {
		// crystal
		if (times == 1) {
			gold = 0;
			crystal = crystal_one;
			once_signal = user_signals_check(conn->euser
			, SIGNAL_LOTTERY_CRYSTAL_ONE);
		} else {
			gold = 0;
			crystal = crystal_ten;
		}
	}

	// TODO check user money enough
	if (conn->euser.gold < gold || conn->euser.crystal < crystal) {
		NET_ERROR_RETURN(conn, -35, "dbin_lottery:money_not_enough");
	}

	// eid, gold, crystal, times
	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d %d %d %d", eid, type, gold, crystal, times);

	if (times == 1) {
		if (once_signal) {
			card_id = get_lottery_card(LOTTERY_TEAM_NORMAL, type);
		} else {
			card_id = get_lottery_card(LOTTERY_TEAM_ONCE, type);
		}
		if (card_id <= 0) {
			NET_ERROR_RETURN(conn, -6, "dbin_lottery:get_lottery_card_bug");
		}
		// card_list[i] = card_id;
		ptr += sprintf(ptr, " %d", card_id);
	} else {
		for (int i=0; i<9; i++) {
			card_id = get_lottery_card(LOTTERY_TEAM_NORMAL, type);
			if (card_id <= 0) {
				NET_ERROR_RETURN(conn, -26, "dbin_lottery:get_lottery_card_bug");
			}
			// card_list[i] = card_id;
			ptr += sprintf(ptr, " %d", card_id);
		}

		for (int i=0; i<1; i++) {
			card_id = get_lottery_card(LOTTERY_TEAM_SPECIAL, type);
			if (card_id <= 0) {
				NET_ERROR_RETURN(conn, -36, "dbin_lottery:get_lottery_card_bug");
			}
			// card_list[i] = card_id;
			ptr += sprintf(ptr, " %d", card_id);
		}

	}
	for (int i = 0; i < EVIL_SIGNAL_MAX; i++)
	{
		if (conn->euser.signals[i] != '1') {
			conn->euser.signals[i] = '0';
		}
	}
	conn->euser.signals[EVIL_SIGNAL_MAX] = '\0';
	ptr += sprintf(ptr, " %30s", conn->euser.signals);
	
	// DEBUG_PRINT(0, "dbin_lottery:out_buffer=%s", out_buffer);
	ret = dbin_write(conn, cmd, DB_LOTTERY, "%s", out_buffer); 

	return 0;
}




// CMD: pshop
// RET: pshop [remain_time] [refresh_gold] [piece_info1] ... [piece_info6]
// piece_info: [piece_id] [piece_count] [gold] [crystal]
// piece_count: if < 0, means this piece has already buy.
// gold/crystal: if < 0, means this piece should only buy by vip.
int dbin_piece_shop(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	evil_piece_shop_t shop;
	bzero(&shop, sizeof(shop));

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	evil_user_t *puser = &conn->euser;
	shop = puser->piece_shop;

	time_t now = time(NULL);
	time_t last_time = shop.last_time;
	time_t yesterday_end = get_yesterday_end(now);

	// case 1: user.shop is new, no need to refresh, just send to client
	if (last_time != 0 && now - last_time < g_design->constant.pshop_reset_interval
	&& last_time > yesterday_end
	) {
		__send_piece_shop(conn, cmd, 0);
		return 0;
	}

	// case 2: last_time == 0, user.shop is empty, select piece shop in db
	if (shop.last_time == 0) {
		ret = dbin_write(conn, cmd, DB_GET_PIECE_SHOP, "%d", eid);
		return 0;
	}

	// case 3: user.shop need update, update refresh_times or refresh piece_shop data

	// 1.update refresh_times
	// 2.random a new pid_list, clean buy_flag_list
	// 3.save in db
	// 4.out_piece_shop save in memory
	// 5.send to client

	/*
	if (last_time <= yesterday_end && shop.refresh_times != 0)
	{
		// zero daily referesh piece shop times
		shop.refresh_times = 0;
	}

	if (last_time == 0 || now - last_time >= g_design->constant.pshop_reset_interval) {
		// add total refresh piece shop times
		shop.show_times++;
		// get a new random piece shop
		ret = __random_piece_shop(shop.pid_list, shop.show_times
		, MAX_PIECE_SHOP_SLOT);
		ERROR_NEG_RETURN(ret, "dbin_piece_shop:random_piece_shop");
		// zero all buy flag
		bzero(shop.buy_flag_list, sizeof(shop.buy_flag_list));
		// update refresh piece time
		shop.last_time = now;
	}
	*/
	ret = update_piece_shop(shop);
	if (ret < 0) {
		ERROR_PRINT(ret, "dbin_piece_shop:update_piece_shop_fail");
	}
	if (ret == 0) {
		WARN_PRINT(-66, "dbin_piece_shop:piece_shop_no_change");
	}

	ret = dbin_write(conn, cmd, DB_UPDATE_PIECE_SHOP
	, "%d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, eid, shop.last_time, shop.refresh_times, shop.show_times
	, shop.pid_list[0], shop.buy_flag_list[0]
	, shop.pid_list[1], shop.buy_flag_list[1]
	, shop.pid_list[2], shop.buy_flag_list[2]
	, shop.pid_list[3], shop.buy_flag_list[3]
	, shop.pid_list[4], shop.buy_flag_list[4]
	, shop.pid_list[5], shop.buy_flag_list[5]
	);

	return 0;
}


// CMD: rpshop
// RET: rpshop [remain_time] [cost_gold] [refresh_gold] [piece_info1] ... [piece_info6]
// piece_info: [piece_id] [piece_count] [gold] [crystal]
// piece_count: if < 0, means this piece has already buy.
// gold/crystal: if < 0, means this piece should only buy by vip.
int dbin_refresh_piece_shop(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	evil_piece_shop_t shop;
	bzero(&shop, sizeof(shop));

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	evil_user_t *puser = &conn->euser;
	shop = puser->piece_shop;

	if (shop.last_time == 0) {
		NET_ERROR_RETURN(conn, -3, "dbin_refresh_piece_shop:user_shop_empty");
	}

	int refresh_gold = min(((shop.refresh_times == 0) ? g_design->constant.pshop_refresh_gold : (2 << (shop.refresh_times-1)) * g_design->constant.pshop_refresh_gold)
	, g_design->constant.pshop_max_refresh_gold);

	if (conn->euser.gold < refresh_gold) {
		NET_ERROR_RETURN(conn, -5, "dbin_refresh_piece_shop:money_not_enough");
	}

	shop.refresh_times++;
	shop.show_times++;
	ret = __random_piece_shop(shop.pid_list, shop.show_times
	, MAX_PIECE_SHOP_SLOT);
	ERROR_NEG_RETURN(ret, "dbin_refresh_piece_shop:random_piece_shop");
	bzero(shop.buy_flag_list, sizeof(shop.buy_flag_list));
	shop.last_time = time(NULL);

	ret = dbin_write(conn, cmd, DB_REFRESH_PIECE_SHOP
	, "%d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, eid, shop.last_time, shop.refresh_times, shop.show_times
	, shop.pid_list[0], shop.buy_flag_list[0]
	, shop.pid_list[1], shop.buy_flag_list[1]
	, shop.pid_list[2], shop.buy_flag_list[2]
	, shop.pid_list[3], shop.buy_flag_list[3]
	, shop.pid_list[4], shop.buy_flag_list[4]
	, shop.pid_list[5], shop.buy_flag_list[5]
	, -refresh_gold
	);

	return 0;
}

// CMD: lpshop
// RET: lpshop 0 [card_deck400]
int dbin_get_piece_list(connect_t *conn, const char *cmd, const char *buffer)
{
	int eid;
	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	net_writeln(conn, "%s 0 %s", cmd, g_design->pshop_piece_list);
	return 0;
}

// CMD: pbuy [pos]
// RET: pbuy [pos] [card_id] [count] [gold] [crystal]
int dbin_piece_buy(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int pos;
	int isvip;
	evil_piece_shop_t shop;
	bzero(&shop, sizeof(shop));

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	evil_user_t *puser = &conn->euser;
	shop = puser->piece_shop;
	if (shop.last_time == 0) {
		NET_ERROR_RETURN(conn, -3, "dbin_piece_buy:user_shop_empty");
	}

	ret = sscanf(buffer, "%d", &pos);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "dbin_piece_buy:invalid_input");
	}

	if (pos <= 0 || pos > MAX_PIECE_SHOP_SLOT) {
		NET_ERROR_RETURN(conn, -5, "dbin_piece_buy:pos_out_bound");
	}
	isvip = check_vip(conn);
	if (!isvip && pos > MAX_PIECE_SHOP_SLOT - 2) {
		NET_ERROR_RETURN(conn, -5, "dbin_piece_buy:pos_invalid");
	}
	if (shop.buy_flag_list[pos-1]) {
		NET_ERROR_RETURN(conn, -5, "dbin_piece_buy:item_already_buy");
	}

	int pid = shop.pid_list[pos-1];
	design_piece_shop_t &ditem = g_design->piece_shop_list[pid];
	if (ditem.gold > puser->gold) {
		NET_ERROR_RETURN(conn, -6, "dbin_piece_buy:gold_not_enough");
	}
	if (ditem.crystal > puser->crystal) {
		NET_ERROR_RETURN(conn, -16, "dbin_piece_buy:crystal_not_enough");
	}

	ret = dbin_write(conn, cmd, DB_PIECE_BUY, "%d %d %d %d %d %d"
	, eid, pos, ditem.card_id, ditem.count, -ditem.gold, -ditem.crystal);

	return 0;
}


int dbin_save_deck(connect_t *conn, const char *cmd, const char *buffer)
{
//	int fd = g_dbio_fd[0];   // hard code, TODO round robin
//	int cid = get_conn_id(conn);
//	int size, len;
	int eid;
	int slot;
	int n;
	int ret;
	char deck[EVIL_CARD_MAX + 5];

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
		
	}

	ret = sscanf(buffer, "%d %n", &slot, &n);
	if (ret < 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	if (slot < 0) {
		NET_ERROR_RETURN(conn, -25, "in_save_deck:slot_invalid %d", slot);
	}

	// deck = buffer + n;
	sprintf(deck, "%.400s", buffer + n);

	ret = check_deck(deck, conn->euser.card, eid);
	// deck count < EVIL_DECK_CARD_MIN
	if (ret == -52) {
		NET_ERROR_RETURN(conn, -35, E_SAVE_DECK_COUNT_LESS);
	}

	if (ret < 0) {
		NET_ERROR_RETURN(conn, -45, "invalid_deck %d", ret);
	}

	ret = dbin_write(conn, cmd, DB_SAVE_DECK, IN_SAVE_DECK_PRINT, eid, slot, deck);
	return ret;

/***
	// beware of double \n
	len = sprintf(in_buffer, "%d %d ", cid, DB_SAVE_DECK);
	len += sprintf(in_buffer+len, IN_SAVE_DECK_PRINT, eid, deck);
	conn->db_flag = time(NULL); // core logic

	size = write(fd, in_buffer, len);
	FATAL_EXIT(size-len, "dbin_save_deck:write %d / %d %s", size, len, in_buffer);
	return 0;
****/
}

// peter: this may cause error, too many short lua calls (400) is slow
int XXX_dbin_save_deck(connect_t *conn, const char *cmd, const char *buffer)
{
//	int fd = g_dbio_fd[0];   // hard code, TODO round robin
//	int cid = get_conn_id(conn);
//	int size, len;
	int eid;
	int total, from_id;
	int n;
	int ret;
	const char *deck; // deck ptr to buffer + n
//	char in_buffer[DB_BUFFER_MAX];

	const char *ptr;
	char str_count[2];
	int count;
	int hero;
	lua_State * lua;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
		
	}

	ret = sscanf(buffer, "%d %d %n", &total, &from_id, &n);
	if (ret < 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}
	// only accept 400 mode
	if (total != EVIL_CARD_MAX && from_id != 1) {
		NET_ERROR_RETURN(conn, -15, "only_accept_400_1_form");
	}
	deck = buffer + n;

	// check card fit hero
	lua = luaL_newstate();
	if (lua == NULL) {
		BUG_PRINT(-7, "in_save_deck:lua_null");
	}
	luaL_openlibs(lua);
	lu_set_int(lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(lua, "res/lua/logic.lua");
	lua_pushinteger(lua, 1974);

	total = 0;
	hero = 0;
	ptr = deck;
	for (int i=1;i<=EVIL_CARD_MAX;i++) {
		ret = sscanf(ptr, "%1s", str_count);
		if (ret != 1) {
			NET_ERROR_RETURN(conn, -25, "in_save_deck:invalid_deck %d", ret);
		}
		count = atoi(str_count);

		if (i <= HERO_MAX && count > 0 && count <= 9) {
			if (hero != 0) {
				NET_ERROR_RETURN(conn, -35, "in_save_deck:two_hero %d %d %d"
				, hero, i, count);
			}
			if (count > 1) {
				NET_ERROR_RETURN(conn, -45, "in_save_deck:two_same_hero %d %d"
				, i, count);
			}
			hero = i;
			total++;
		}

		if (i > HERO_MAX && count > 0 && count <= 9) {
			if (hero == 0) {
				NET_ERROR_RETURN(conn, -45, "in_save_deck:no_hero");
			}

			lua_getglobal(lua, "fit_hero_id");
			lua_pushinteger(lua, hero); 
			lua_pushinteger(lua, i); 
			ret = lua_pcall(lua, 2, 1, 0);  // 2=input param, 1=output return
			if (0 != ret) {
				lua_pop(lua, 1); 
				ERROR_PRINT(ret, "lu_print_index_both");
				NET_ERROR_RETURN(conn, -6, "in_save_deck:lua_bug");
			}
			ret = lua_toboolean(lua, -1);
			if (ret == 0) {
				NET_ERROR_RETURN(conn, -45, "in_save_deck:card_not_fit_hero %d %d", i, hero);
			}
			lua_pop(lua, 1);
			total++;
		}

		ptr += 1;
	}

	ret = lua_tointeger(lua, -1);
	LU_CHECK_MAGIC(lua);
	lua_pop(lua, 1);
	lua_close(lua);  // clean up lua VM
	lua = NULL;  

	DEBUG_PRINT(0, "in_save_deck:hero=%d total=%d", hero, total);
	//

	ret = dbin_write(conn, cmd, DB_SAVE_DECK, IN_SAVE_DECK_PRINT, eid, deck);
	return ret;

/***
	// beware of double \n
	len = sprintf(in_buffer, "%d %d ", cid, DB_SAVE_DECK);
	len += sprintf(in_buffer+len, IN_SAVE_DECK_PRINT, eid, deck);
	conn->db_flag = time(NULL); // core logic

	size = write(fd, in_buffer, len);
	FATAL_EXIT(size-len, "dbin_save_deck:write %d / %d %s", size, len, in_buffer);
	return 0;
****/
}


int dbin_cccard(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int card_id, count;
	int eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = sscanf(buffer, "%d %d", &card_id, &count);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}
	if (card_id <= 0 || card_id > EVIL_CARD_MAX || count < 0 || count > 9) {
		NET_ERROR_RETURN(conn, -15, "invalid_card_id_count %d %d", card_id, count);
	}


	ret = dbin_write(conn, cmd, DB_CCCARD
		, IN_CCCARD_PRINT, eid, card_id, count);
	return ret;
	
}

// CMD: xcadd cardid count gold crystal
// RET: xcadd 0 OK
// ERR: xcadd -9 not_login
// ERR: xcadd -5 invalid_input  (less than 3 number)
// ERR: xcadd -15 invalid_cardid
// ERR: xcadd -25 invalid_count
// ERR: xcadd -35 invalid_money
// ERR: xcadd -6 not_enough_piece
int dbin_xcadd(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int cardid, gold, crystal, count;
	const char *name;

	ret = sscanf(buffer, "%d %d %d %d", &cardid, &count, &gold, &crystal);
	if (ret != 4) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login %d", eid);
	}

	if (cardid <= 0 || cardid > EVIL_CARD_MAX) {
		NET_ERROR_RETURN(conn, -15, "invalid_cardid %d", cardid);
	}

	if (count <= 0 || count > EVIL_NUM_PIECE_MAX) {
		NET_ERROR_RETURN(conn, -25, "invalid_count %d", count);
	}

	// invalid1:  gold or crystal < 0
	// invalid2:  gold>0 and crystal>0
	// invalid3:  gold==0 and crystal==0
	// valid:  (gold>0 and crystal==0)  || (gold==0 and crystal>0)
	if (gold < 0 || crystal < 0 || (gold>0 && crystal>0) 
	|| (gold==0 && crystal==0))  {
		NET_ERROR_RETURN(conn, -25, "invalid_gold_crystal %d %d"
		, gold, crystal);
	}


	// cardid is base 1
	name = g_design->card_list[cardid].name;
	if (name==NULL || name[0]=='\0') {
		BUG_PRINT(-7, "xcadd:invalid_card_name");
		name = "no_name";  // actually is buggy!
	}

	ret = dbin_write(conn, cmd, DB_ADD_EXCHANGE
		, IN_ADD_EXCHANGE_PRINT, eid, cardid, count, gold, crystal, name);
	return ret;
}


// @see OUT_BUY_EXCHANGE_PRINT
// CMD: xcbuy [xcid] [count]
// RET: xcbuy 0 buyer_eid seller_eid cardid gold crystal   (positive)
// note:  buyer and seller will receive the RET when they are online,
// client should update depends on whether I am buyer or seller,
// if my_eid == buyer_eid :  my.gold-=gold, my.crystal-=crystal
//      also cardid count++
// if my_eid == seller_eid:  my.gold+=gold, my.crystal+=crystal
//      cardid count do not need to change (already update when xcadd)
// ERR: xcbuy -9 not_login
// ERR: xcbuy -5 invalid_input
// ERR: xcbuy -15 invalid_xicd (negative or 0)
// ERR: xcbuy -6 xcid_not_exists 
// ERR: xcbuy -26 not_enough_money
// ERR: xcbuy -2 too_many_card cardid=xx count=9 (9 is max)
int dbin_xcbuy(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	long xcid;
	int count;

	ret = sscanf(buffer, "%ld %d", &xcid, &count);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	if (xcid <= 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_xcid %ld", xcid);
	}

	if (count <= 0 || count > EVIL_NUM_PIECE_MAX) {
		NET_ERROR_RETURN(conn, -25, "invalid_count %d", count);
	}

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login %d", eid);
	}
	

	ret = dbin_write(conn, cmd, DB_BUY_EXCHANGE
		, IN_BUY_EXCHANGE_PRINT, eid, xcid, count);
	return ret;
}


// @see OUT_LIST_EXCHANGE_ROW PRINT
// CMD: xclist [start_id] [page_size] [search_key30]
// e.g. xclist 0 10 人类
// RET: xclist [start_id] [page_size] [row1] [row2] ... [rowN]
// row = [xcid] [seller_eid] [cardid] [count] [gold] [crystal]
// ERR: xclist -5 invalid_input (must have start_id and page_size)
// ERR: xclist -15 invalid_start_id  (>=0)
// ERR: xclist -25 invalid_page_size  (>0 and <=50)
int dbin_xclist(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int start_id, page_size;
	char key[40]; // 30 is the max

	key[0] = 0;
	ret = sscanf(buffer, IN_LIST_EXCHANGE_SCAN, &start_id, &page_size, key);
	if (ret < 2) {  // last key is optional
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	if (start_id < 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_start_id %d", start_id);
	}
	if (page_size <= 0 || page_size>50) {
		NET_ERROR_RETURN(conn, -25, "invalid_page_size %d", page_size);
	}

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login %d", eid);
	}

	ret = dbin_write(conn, cmd, DB_LIST_EXCHANGE, IN_LIST_EXCHANGE_PRINT
	, start_id, page_size, key);
	return ret;
}


// CMD: @xcreset [second_timeout]
// e.g. @xcrest 86400  means one day, record older than one day will reset
// RET: @xcreset 0 OK
// ERR: @xcreset -5 invalid_input
// ERR: @xcreset -15 invalid_second  (maybe reported by dbio)
int admin_xcreset(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int second = 86400; // default to one day
	time_t now;
	long pts;


	ret = sscanf(buffer, "%d", &second);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input @xcrest [second]");
	}

	if (second <= 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_second %d", second);
	}


	now = time(NULL);
	now -= second;  // go back to the past
	pts = unix_to_pts(now);

	ret = dbin_write(conn, cmd, DB_RESET_EXCHANGE, IN_RESET_EXCHANGE_PRINT, pts);
	return ret;
}

// CMD: @ladder
// RET: @ladder [row_num] [info1] [info2] ...
// info = eid, rank, rating, alias
int admin_create_ladder(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;

	ret = dbin_write(conn, cmd, DB_CREATE_LADDER, "");
	return ret;
}


// CMD: lguild [start_id] [page_size] [key_optional]
// e.g: lguild 0 10 
// RET: lguild [start_id] [page_size] [g_info1] [g_info2] ...
// g_info = gid gname glevel gold crystal total_member leader_name
// ERR: lguild -5 invalid_input (must have start_id and page_size)
// ERR: lguild -15 invalid_start_id  (>=0)
// ERR: lguild -25 invalid_page_size  (>0 and <=50)
int dbin_list_guild(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int start_id, page_size;
	char key[EVIL_ALIAS_MAX+5] = {'\0'}; // search key, optional

	ret = sscanf(buffer, IN_LIST_GUILD_SCAN, &start_id, &page_size, key);
	if (ret < 2) {  
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	if (start_id < 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_start_id %d", start_id);
	}
	if (page_size <= 0 || page_size>50) {
		NET_ERROR_RETURN(conn, -25, "invalid_page_size %d", page_size);
	}

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login %d", eid);
	}

	ret = dbin_write(conn, cmd, DB_LIST_GUILD, IN_LIST_GUILD_PRINT
	, start_id, page_size, key);
	return ret;
}


// CMD: cguild [gname]
// e.g: cguild yoyoyo
// RET: cguild [gid] [gname]
// ERR: cguild -6 already_has_guild
// ERR: cguild -3 eid_not_found
// ERR: cguild -22 duplicate_gname
int dbin_create_guild(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int gid;
	int gold;
	int crystal;
	char gname[EVIL_ALIAS_MAX + 5];

	ret = sscanf(buffer, "%30s", gname);
	if (ret != 1) {  
		// NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
		net_writeln(conn, "%s 0 %d %d"
		, cmd, CREATE_GUILD_GOLD, CREATE_GUILD_CRYSTAL);	
		return 0;
	}

	eid = conn->euser.eid;
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login %d", eid);
	}

	gid = conn->euser.gid;
	if (gid > 0) {
		NET_ERROR_RETURN(conn, -6, "%s %d", E_CREATE_GUILD_ALREADY_HAS_GUILD, gid);
	}

	gold = conn->euser.gold;
	if (gold < g_design->constant.create_guild_gold) {
		NET_ERROR_RETURN(conn, -16, "%s %d %d",
		E_CREATE_GUILD_MONEY_NOT_ENOUGH, gold, g_design->constant.create_guild_gold);
	}

	crystal = conn->euser.crystal;
	if (crystal < g_design->constant.create_guild_crystal) {
		NET_ERROR_RETURN(conn, -26, "%s %d %d",
		E_CREATE_GUILD_MONEY_NOT_ENOUGH, crystal, g_design->constant.create_guild_crystal);
	}
		
	ret = dbin_write(conn, cmd, DB_CREATE_GUILD, IN_CREATE_GUILD_PRINT
	, eid, g_design->constant.create_guild_gold, g_design->constant.create_guild_crystal, gname);
	return ret;
}


//  CMD: dguild  (only master can do)
//  RET: dguild gid
//  ERR: dguild -6 not_master
//  ERR: dguild -3 no_guild
//  change: evil_status, evil_guild, evil_guild_member
int dbin_delete_guild(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int gid;

	eid = get_eid(conn);
	gid = conn->euser.gid;
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	if (gid <= 0) {
		NET_ERROR_RETURN(conn, -3, "no_guild");
	}
	if (eid != gid) {
		NET_ERROR_RETURN(conn, -6, "not_master %d %d", eid, gid);
	}


	ret = dbin_write(conn, cmd, DB_DELETE_GUILD, IN_DELETE_GUILD_PRINT
	, gid);  // gid == eid
	return ret;
}

//CMD: glist [flag] [start_id] [page_size] [gid_optional] (guild member list)
//if gid_optional is not given, list my guild
//RET: glist [flag] [total] [member_info1] [member_info2] ...
//member_info = eid pos alias last_login gshare
int dbin_glist(connect_t *conn, const char *cmd, const char *buffer)
{
	int gid;
	int flag;
	int start_id;
	int page_size;
	int ret;

	gid = conn->euser.gid; // default is my gid
	ret = sscanf(buffer, "%d %d %d %d", &flag, &start_id, &page_size, &gid);
	if (ret < 3) {
		NET_ERROR_RETURN(conn, -5, "invalid_input");
	}

	if (get_eid(conn)<=0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	if (flag != 0 && flag != 1 && flag != 9) {
		NET_ERROR_RETURN(conn, -25, "invalid_flag %d", flag);
	}
	if (gid <= 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_gid %d", gid);
	}
	if (start_id < 0) {
		NET_ERROR_RETURN(conn, -35, "invalid_start_id %d", start_id);
	}
	if (page_size <= 0 || page_size>50) {
		NET_ERROR_RETURN(conn, -45, "invalid_page_size %d", page_size);
	}


	ret = dbin_write(conn, cmd, DB_GUILD_LMEMBER, IN_LIST_GMEMBER_PRINT
	, flag, start_id, page_size, gid);  

	return ret;
}


//CMD: gapply [gid]
//RET: gapply [eid] [gid] [gpos] [gname]
//ERR: gapply -6 already_has_guild
//ERR: gapply -3 guild_not_exist
//ERR: gapply -9 not_login ...
int dbin_gapply(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int gid;

	ret = sscanf(buffer, "%d", &gid);
	if (ret < 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input");
	}

	if (get_eid(conn)<=0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	if (gid <= 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_gid %d", gid);
	}
	if (conn->euser.gid > 0) {
		NET_ERROR_RETURN(conn, -6, "%s %d %d",
		E_GUILD_APPLY_ALREADY_HAS_GUILD
		, get_eid(conn), conn->euser.gid);
	}

	ret = dbin_write(conn, cmd, DB_GUILD_APPLY, IN_GUILD_APPLY_PRINT
	, gid, conn->euser.eid);  

	return ret;
}


//  CMD: gpos [eid] [pos]
//  e.g. approve apply pos = 3  (from 9 to 
//  upgrade normal member to senior pos = 2
//  pos=0 means kick the member
//  pos=1 is invalid
//  RET: gpos [eid] [pos] [gid]
//  ERR: gpos -19 not_master
//  ERR: gpos -3 eid_not_in_guild
//  ERR: gpos -6 invalid_pos
int dbin_gpos(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;  // target eid (not my eid)
	int gid;
	int pos;	// target pos
	///// master (my info)
	int my_eid; // usually master
	int my_gpos;
	int member_max;


	my_eid = get_eid(conn);
	gid = conn->euser.gid;
	my_gpos = conn->euser.gpos;

	if (my_eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	// if (gid != my_eid || my_gpos!=GUILD_POS_MASTER) {
	if (my_gpos!=GUILD_POS_MASTER && my_gpos!=GUILD_POS_SENIOR) {
		NET_ERROR_RETURN(conn, -19, "not_master %d %d %d", my_eid
		, gid, my_gpos);
	}


	ret = sscanf(buffer, "%d %d", &eid, &pos);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "gpos:invalid_input %d", ret);
	}

	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -15, "gpos:invalid_eid %d", eid);
	}
	// valid pos:  2, 3 
	if (pos < GUILD_POS_SENIOR || pos > GUILD_POS_MEMBER) {
		NET_ERROR_RETURN(conn, -25, "gpos:invalid_pos %d", pos);
	}
	if (gid <= 0) {
		NET_ERROR_RETURN(conn, -35, "gpos:invalid_gid %d", gid);
	}

	guild_t & guild = g_guild_map[gid];
	// DEBUG_PRINT(0, "gpos:glevel=%d", guild.glevel);
	if (guild.glevel == 0) {
		NET_ERROR_RETURN(conn, -45, "gpos:invalid_glevel %d", guild.glevel);
	}

	if (guild.glevel >= g_design->guild_max_level) {
		member_max = g_design->guild_list[g_design->guild_max_level].member_max;
	} else {
		member_max = g_design->guild_list[guild.glevel].member_max;
	}

	DEBUG_PRINT(0, "gpos:member_max=%d", member_max);
		
	ret = dbin_write(conn, cmd, DB_GUILD_POS, IN_GUILD_POS_PRINT
	, eid, pos, gid, member_max);
	return ret;
}


// CMD: gquit [eid_optional]
// 1.if eid_optional is not null, only master can kick
// 2.if eid_optional is null, master cannot quit
// RET: gquit [eid]
// ERR: gpos -3 no_guild
// ERR: gpos -19 master_cannot_quit
int dbin_gquit(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;  // target eid (not my eid)
	int my_eid;  
	int gid;
	int gpos;
	int member_max;

	my_eid = get_eid(conn);
	gid = conn->euser.gid;
	gpos = conn->euser.gpos;

	ret = sscanf(buffer, "%d", &eid);
	if (ret != 1) {
		eid = my_eid;
	} else {
		// only master can kick
		if (gpos!=GUILD_POS_MASTER && gpos != GUILD_POS_SENIOR) {
			NET_ERROR_RETURN(conn, -29, "only_master_can_kick");
		}
		if (eid == gid) {
			NET_ERROR_RETURN(conn, -39, "master_cannot_kicked");
		}
	}

	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	if (gid <= 0) {
		NET_ERROR_RETURN(conn, -3, "no_guild");
	}

	if (eid==my_eid && gpos==GUILD_POS_MASTER) {
		NET_ERROR_RETURN(conn, -19, "master_cannot_quit");
	}

	if (gpos == GUILD_POS_APPLY) {
		// when gpos == GUILD_POS_APPLY, member_max is useless
		member_max = 0;
	} else {
		guild_t & guild = g_guild_map[gid];
		DEBUG_PRINT(0, "gquit:glevel=%d", guild.glevel);
		if (guild.glevel == 0) {
			NET_ERROR_RETURN(conn, -45, "gquit:invalid_glevel %d", guild.glevel);
		}

		if (guild.glevel >= g_design->guild_max_level) {
			member_max = g_design->guild_list[g_design->guild_max_level].member_max;
		} else {
			member_max = g_design->guild_list[guild.glevel].member_max;
		}
		DEBUG_PRINT(0, "gquit:member_max=%d", member_max);
	}

	ret = dbin_write(conn, cmd, DB_GUILD_QUIT, IN_GUILD_QUIT_PRINT
	, eid, gid, member_max);
	return ret;
}


// CMD: gdeposit [gold]
// RET: gdeposit eid gid gold
// ERR: gdeposit -2 not_enough_money
// ERR: gdeposit -3 no_guild
int dbin_gdeposit(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;  
	int gid;
	int gpos;
	int gold;

	ret = sscanf(buffer, "%d", &gold);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}
	if (gold <= 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_gold %d", gold);
	}

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	gid = conn->euser.gid;
	gpos = conn->euser.gpos;
	if (gid <= 0 || gpos==GUILD_POS_APPLY) {
		NET_ERROR_RETURN(conn, -3, "no_guild");
	}

	if (gold > conn->euser.gold) {
		NET_ERROR_RETURN(conn, -2, "%s", E_GUILD_DEPOSIT_MONEY_NOT_ENOUGH);
	}

	ret = dbin_write(conn, cmd, DB_GUILD_DEPOSIT, IN_GUILD_DEPOSIT_PRINT
	, eid, gid, gold);
	return ret;
}

// CMD: gbonus [get_flag]
// get_flag=0 : check only, not get the bonus,  get_flag=1:get bonus
// RET: gbonus eid get_flag gshare gold last_bonus_time
// ERR: gbonus -6 already_get_bonus
int dbin_gbonus(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;  
	int gid;
	int gpos;
	int get_flag;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	gid = conn->euser.gid;
	gpos = conn->euser.gpos;
	if (gid <= 0 || gpos==GUILD_POS_APPLY) {
		NET_ERROR_RETURN(conn, -3, "no_guild");
	}

	get_flag = 0; // default
	ret = sscanf(buffer, "%d", &get_flag);
	if (get_flag < 0 || get_flag > 1) {
		NET_ERROR_RETURN(conn, -15, "gbonus:invalid_get_flag %d", get_flag);
	}

	ret = dbin_write(conn, cmd, DB_GUILD_BONUS, IN_GUILD_BONUS_PRINT
	, eid, g_design->constant.guild_bonus_rate, get_flag);
	return ret;
}

// CMD: ldeposit start_id page_size 
// RET: ldeposit start_id page_size [deposit_info1] [deposit_info2]
// deposit_info = deposit_date(unix_ts)  eid alias gold crystal
int dbin_ldeposit(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int gid, gpos;
	int eid;
	int start_id, page_size;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	gid = conn->euser.gid;
	gpos = conn->euser.gpos;
	if (gid <= 0 || gpos== GUILD_POS_APPLY) {
		NET_ERROR_RETURN(conn, -3, "no_guild");
	}

	ret = sscanf(buffer, "%d %d", &start_id, &page_size);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}
	if (start_id < 0) {
		NET_ERROR_RETURN(conn, -25, "invalid_start_id %d", start_id);
	}
	if (page_size <= 0 || page_size>50) {
		NET_ERROR_RETURN(conn, -35, "invalid_page_size %d", page_size);
	}

	ret = dbin_write(conn, cmd, DB_LIST_DEPOSIT, IN_LIST_DEPOSIT_PRINT
	, gid, start_id, page_size);

	return ret;
}

// CMD: deposit 
// RET: deposit eid gid gold crystal gshare guild_gold guild_crystal
int dbin_deposit(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int gid;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	gid = conn->euser.gid;
	if (gid <= 0) {
		NET_ERROR_RETURN(conn, -3, "no_guild");
	}

	ret = dbin_write(conn, cmd, DB_DEPOSIT, IN_DEPOSIT_PRINT
	, eid, gid);

	return ret;
}

// CMD: guild gid gnotice_optional 
// RET: guild gid, total_member, member_max, glevel, gold, crystal, consume(%d), gname, notice
int dbin_guild(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int in_gid, gid, gpos;
	int eid;
	int n;
	const char *gnotice;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	gid = conn->euser.gid;
	gpos = conn->euser.gpos;

	in_gid = 0; // default not use in_gid 
	ret = sscanf(buffer, "%d %n", &in_gid, &n);
	if (ret >= 1) {
		if (in_gid <= 0) {
			NET_ERROR_RETURN(conn, -15, "invalid_in_gid %d", in_gid);
		}
	}
	gnotice = "";
	if (gid==in_gid && gpos==GUILD_POS_MASTER) {
		gnotice = buffer + n; // only master can modify gnotice
	}
	if (in_gid > 0) {
		gid = in_gid;
	}
	ret = dbin_write(conn, cmd, DB_GUILD, IN_GUILD_PRINT
	, gid, gnotice);

	return ret;
}

// CMD: glv [gid_optional] 
// RET: glv gid current_level current_gold current_member_max current_consume_gold next_level levelup_gold next_member_max next_consume_gold
int dbin_glv(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int gid;

	gid = conn->euser.gid;
	sscanf(buffer, "%d", &gid); // optional

	if (gid <= 0) {
		NET_ERROR_RETURN(conn, -3, "no_guild %d", gid);
	}
	ret = dbin_write(conn, cmd, DB_GLV, IN_GLV_PRINT, gid);

	return ret;
}

// CMD: glevelup
// RET: glevelup gid new_level gold_reduce(-negative) new_member_max new_consume 
int dbin_glevelup(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid, gid;
	int gpos;
	int levelup_gold;

	eid = conn->euser.eid;
	gid = conn->euser.gid;
	gpos = conn->euser.gpos;
	if (gid <= 0) {
		NET_ERROR_RETURN(conn, -3, "no_guild %d", gid);
	}

	if (eid != gid || gpos != GUILD_POS_MASTER) {
		NET_ERROR_RETURN(conn, -9, "not_master %d %d %d", eid, gid, gpos);
	}


	guild_t &guild = get_guild_by_gid(gid);
	// XXX this check is never true!!!
	if (guild.gid != gid) {
		NET_ERROR_RETURN(conn, -6, "guild_not_load try glv first %d"
		, guild.gid);
	}
	if (guild.glevel <= 0) {
		NET_ERROR_RETURN(conn, -16, "invalid_glevel %d try glv first"
		, guild.glevel);
	}
	if (guild.glevel >= g_design->guild_max_level) {
		// NET_ERROR_RETURN(conn, -2, "glevel_max %d", guild.glevel);
		NET_ERROR_RETURN(conn, -2, "%s %d", E_GLEVELUP_MAX_LEVEL, guild.glevel);
	}

	levelup_gold = g_design->guild_list[guild.glevel].levelup_gold;
	if (guild.gold <= levelup_gold) {
		NET_ERROR_RETURN(conn, -12, "not_enough_gold %d %d"
		, guild.gold, levelup_gold);
	}


	ret = dbin_write(conn, cmd, DB_GLEVELUP, IN_GLEVELUP_PRINT
	, gid, guild.glevel, levelup_gold);

	return ret;
}

//CMD: gsearch [flag] [start_id] [page_size] [search_data] [gid_optional] (guild member list)
//if gid_optional is not given, list my guild
//RET: gsearch [flag] [total] [member_info1] [member_info2] ...
//member_info = eid pos alias last_login gshare
int dbin_gsearch(connect_t *conn, const char *cmd, const char *buffer)
{
	int gid;
	int flag;
	int start_id;
	int page_size;
	int ret;
	char search_data[101];

	gid = conn->euser.gid; // default is my gid
	ret = sscanf(buffer, "%d %d %d %50s %d", &flag, &start_id, &page_size
	, search_data, &gid);
	if (ret < 4) {
		NET_ERROR_RETURN(conn, -5, "invalid_input");
	}

	if (get_eid(conn)<=0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}
	if (flag != 0 && flag != 1 && flag != 9) {
		NET_ERROR_RETURN(conn, -25, "invalid_flag %d", flag);
	}
	if (gid <= 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_gid %d", gid);
	}
	if (start_id < 0) {
		NET_ERROR_RETURN(conn, -35, "invalid_start_id %d", start_id);
	}
	if (page_size <= 0 || page_size>50) {
		NET_ERROR_RETURN(conn, -45, "invalid_page_size %d", page_size);
	}

	ret = dbin_write(conn, cmd, DB_GUILD_SEARCH, IN_LIST_GSEARCH_PRINT
	, flag, start_id, page_size, search_data, gid);

	return ret;
}



// CMD: ladder [ladder_type]
// ladder_type: 0==rating ladder, 1==level ladder
//		2==guild ladder, 3==collection ladder
//		4==gold ladder, 5==chapter ladder
// RET: 
// 0: ladder total [info1] [info2] ...
// info: eid, rank, rating, alias, icon
int dbin_get_ladder(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int ladder_type;
	evil_user_t *puser;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "get_ladder:not_login %d", eid);
	}

	puser = &conn->euser;
	if (puser->eid <= 0) {
		NET_ERROR_RETURN(conn, -19, "get_ladder:not_login %d", puser->eid);
	}

	ret = sscanf(buffer, "%d", &ladder_type);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	if (ladder_type < LADDER_RATING || ladder_type > LADDER_CHAPTER) {
		NET_ERROR_RETURN(conn, -15, "invalid_ladder_type %d", ret);
	}

	if (ladder_type == LADDER_GUILD && puser->gid == 0) {
		
		// DEBUG_PRINT(0, "in_get_ladder:no_guild");
		char out_buffer[BUFFER_SIZE];
		bzero(out_buffer, BUFFER_SIZE);
		char *out_ptr;
		out_ptr = out_buffer;
		ladder_guild_t *pladder;
		int count;
		count = 0;
		
		for (int i=1; i<=MAX_LADDER; i++) {
			pladder = &g_ladder_guild_list[i];	
			if (pladder->gid > 0) {
				count++;
				out_ptr += sprintf(out_ptr, "%d %d %d %s %d "
				, pladder->gid, pladder->rank
				, pladder->glevel, pladder->gname, pladder->icon);
			}
		}
		net_writeln(conn, "%s %d %d %s"
		, cmd, ladder_type, count, out_buffer);
		return 0;
	}

	ret = dbin_write(conn, cmd, DB_GET_LADDER, IN_GET_LADDER_PRINT
	, ladder_type, eid, puser->gid);

	return ret;
}

// peter: add [eid] as parameter for listing others gameid
// CMD: lreplay [eid]
// RET: eid total [game_info1], [game_info2]
// game_info=gameid, winner, eid1, eid2, icon1, icon2, alias1, alias2
int dbin_list_replay(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid = 0;

	ret = sscanf(buffer, "%d", &eid);

	if (ret <= 0 || eid <= 0) {
		eid = get_eid(conn);
	}
	
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "list_replay:not_login %d", eid);
	}
	ret = dbin_write(conn, cmd, DB_LIST_REPLAY, IN_LIST_REPLAY_PRINT
	, eid);

	return ret;
}

// CMD: replay [gameid]
// RET: gameid, winner, seed, start_side, ver, eid1, eid2, alias1, alias2,deck1, deck2, cmd
int dbin_load_replay(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	long gameid;

	ret = sscanf(buffer, IN_LOAD_REPLAY_SCAN, &gameid);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	if (gameid <= 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_gameid %ld", gameid);
	}
		
	ret = dbin_write(conn, cmd, DB_LOAD_REPLAY, IN_LOAD_REPLAY_PRINT
	, gameid);

	return ret;
}

// CMD: sprofile [icon] [sex] [signature]
// RET: sprofile [eid] [icon] [sex] [signature]
int dbin_update_profile(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int icon;
	int sex;
	char signature[EVIL_SIGNATURE_MAX+1];
	bzero(signature, sizeof(signature));

	ret = sscanf(buffer, "%d %d %100s", &icon, &sex, signature);
	if (ret > 3 || ret < 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	// DEBUG_PRINT(0, "signature len=%d", (int)strlen(signature));
	trim(signature, strlen(signature));

	if (ret == 2 && strlen(signature) == 0) {
		sprintf(signature, "%.100s", "_no_signature");
	}

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "update_profile:not_login %d", eid);
	}

	//EVIL_ICON_MAX = 100
	if (icon < 0 || icon > EVIL_ICON_MAX) {
		NET_ERROR_RETURN(conn, -15, "invalid_icon %d", icon);
	}

	// sex 0==male, 1==female
	if (sex < 0 || sex > 1) {
		NET_ERROR_RETURN(conn, -25, "invalid_sex %d", sex);
	}

		
	ret = dbin_write(conn, cmd, DB_UPDATE_PROFILE, IN_UPDATE_PROFILE_PRINT
	, eid, icon, sex, signature);

	return ret;
}


// CMD: fadd [eid]
// RET: fadd [eid] [friend_eid]
int dbin_friend_add(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int friend_eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = sscanf(buffer, "%d", &friend_eid);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input");
	}

	if (friend_eid <= MAX_AI_EID || friend_eid == eid) {
		NET_ERROR_RETURN(conn, -15, "invalid_eid %d", friend_eid);
	}

	ret = dbin_write(conn, cmd, DB_FRIEND_ADD, IN_FRIEND_ADD_PRINT, eid, friend_eid);
	return ret;
}

// CMD: flist [start_num] [page_size] [optional_alias]
// RET: flist [eid] [total] [start_num] [size] [friend_info1] [friend_info2]...
// friend_info = [eid] [alias] [icon]
int dbin_friend_list(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int start_num;
	int page_size;
	char alias[EVIL_ALIAS_MAX];
	alias[0] = '\0';

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = sscanf(buffer, "%d %d %30s", &start_num, &page_size, alias);
	if (ret < 2) {
		NET_ERROR_RETURN(conn, -5, "list_message:invalid_input %d", ret);
	}

	if (page_size >= 20) {
		page_size = 20; // max is 20
	}

	ret = dbin_write(conn, cmd, DB_FRIEND_LIST, IN_FRIEND_LIST_PRINT, eid, start_num, page_size, alias);
	return ret;
}


int dbin_friend_sta(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int friend_eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "fsta:not_login");
	}

	ret = sscanf(buffer, "%d", &friend_eid);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input");
	}

	if (friend_eid <= MAX_AI_EID || friend_eid == eid) {
		NET_ERROR_RETURN(conn, -15, "invalid_eid %d", friend_eid);
	}

	ret = dbin_write(conn, cmd, DB_FRIEND_STA, IN_FRIEND_STA_PRINT, eid, friend_eid);
	return ret;
}


int dbin_friend_search(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int len;
	char alias[EVIL_ALIAS_MAX];

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "friend_search:not_login");
	}

	ret = sscanf(buffer, "%s", alias);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input");
	}

	len = strlen(alias);
	trim(alias, len);
	
	if ( len <= 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_alias %s", alias);
	}

	ret = dbin_write(conn, cmd, DB_FRIEND_SEARCH, IN_FRIEND_SEARCH_PRINT, alias);
	return ret;
}

/**
 * delete_friend
 * CMD: fdel [friend_eid]
 * RET: fdel [eid] [friend_eid]
 */
int dbin_friend_del(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int friend_eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "friend_search:not_login");
	}

	ret = sscanf(buffer, "%d", &friend_eid);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "invalid_input");
	}

	if (friend_eid <= MAX_AI_EID || friend_eid == eid) {
		NET_ERROR_RETURN(conn, -15, "invalid_eid %d", friend_eid);
	}

	ret = dbin_write(conn, cmd, DB_FRIEND_DEL, IN_FRIEND_DEL_PRINT
	, eid, friend_eid);
	return ret;
}

// CMD:lpiece
// RET:lpiece eid piece_list
int dbin_load_piece(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "load_piece:not_login");
	}

	ret = dbin_write(conn, cmd, DB_LOAD_PIECE, IN_LOAD_PIECE_PRINT, eid);
	return ret;
}

// CMD: cpiece
// RET: cpiece [eid] [piece_list]
int load_card_piece(connect_t *conn, const char *cmd, const char *buffer)
{
//	int ret;
	int eid;
//	int card_id;
//	int size;
//	int count;
	char *ptr;
	char tmp_buffer[BUFFER_SIZE+1];
	bzero(tmp_buffer, sizeof(tmp_buffer));

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "load_card_piece:not_login");
	}

//	ret = sscanf(buffer, "%d %d", &card_id, &size);
//	if (ret < 2) {
//		NET_ERROR_RETURN(conn, -5, "load_card_piece:input %d", ret);
//	}
//
//	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
//		NET_ERROR_RETURN(conn, -15, "load_card_piece:invalid_card_id %d", card_id);
//	}

//	count = 0;
	ptr = tmp_buffer;
//	for (int i=card_id; i < EVIL_CARD_MAX; i++) {
	for (int i=1; i <= EVIL_CARD_MAX; i++) {
		design_merge_t &merge = g_design->merge_list[i];

		ptr += sprintf(ptr, "%.02d", merge.count);
//		if (merge.card_id == 0) {
//			continue;
//		}
//
//		ptr += sprintf(ptr, "%d %d %d %d ", merge.card_id
//		, merge.count, merge.gold, merge.crystal);
//		count++;
//		if (count >= size) {
//			break;
//		}
	}
	net_writeln(conn, "%s %d %s", cmd, eid, tmp_buffer);
//	net_writeln(conn, "%s %d %s", cmd, count, tmp_buffer);


//	ret = dbin_write(conn, cmd, DB_LOAD_CARD_PIECE, "%d %d %d"
//	, eid, card_id, size);
	return 0;
}

// CMD: ppiece ptype 
// RET: ppiece [eid] [ptype] [loc] [card_id] [get_count] [gold] [crystal]
// loc=0 to 5 : refer to card[loc] that is picked
// gold / crystal : change in gold or crystal (negative or zero)
int dbin_pick_piece(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int ptype;
	int card_id;
	int gold;
	int crystal;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "in_pick_piece:not_login");
	}

	DEBUG_PRINT(0, "in_pick_piece:buffer=%s", buffer);

	ret = sscanf(buffer, "%d", &ptype);
	DEBUG_PRINT(0, "in_pick_piece:sscanf=%d", ret);
	if (ret != 1) {  // refresh is optional ret=1 or 2 are ok
		NET_ERROR_RETURN(conn, -5, "in_pick_piece:input %d", ret);
	}

	if (ptype >= MAX_PICK || ptype < 0) {
		NET_ERROR_RETURN(conn, -5, "in_pick_piece:ptype %d", ptype);
	}
	DEBUG_PRINT(0, "in_pick_piece:ptype=%d", ptype);

	evil_user_t *euser = &conn->euser;
	// debug
	printf("------- in_pick_piece -------\ncard_list:\t");
	for (int loc=0; loc<MAX_LOC; loc++) {
		printf("%d\t", euser->batch_list[ptype][loc]);
	}
	printf("-------\n");
	//

	// check batch
	int count = 0;
	for (int loc=0; loc<MAX_LOC; loc++) {
		if(0 == euser->batch_list[ptype][loc] 
		|| EVIL_CARD_MAX < euser->batch_list[ptype][loc]) {
			count++;
		}
	}

	if (count > 0) {
		NET_ERROR_RETURN(conn, -6, "in_pick_piece:batch_null");
	}
	//

	// check money
	gold = -g_design->constant.pick_gold;
	crystal = -g_design->constant.pick_crystal;
	DEBUG_PRINT(0, "in_pick_piece:gold=%d crystal=%d", gold, crystal);

	if (ptype == 0 && (conn->euser.gold+gold)<0) {
		NET_ERROR_RETURN(conn, -25, "in_pick_piece:gold_not_enough euser.gold=%d, gold=%d eid=%d", conn->euser.gold, gold, eid);
	}
	if (ptype == 1 && (conn->euser.crystal+crystal)<0){
		NET_ERROR_RETURN(conn, -35, "in_pick_piece:crystal_not_enough euser.crystal=%d, crystal=%d eid=%d", conn->euser.crystal, crystal, eid);
	}
	//

	if (ptype == 1) {
		gold = 0;
	} else {
		crystal = 0;
	}

	// get card_id
	pick_t *pick;
	pick = g_design->pick_list+ptype;
	int l;
	ret = get_random_loc(pick, &l);
	printf("in_pick_piece:get_random_loc=%d\n", l);
	card_id = euser->batch_list[ptype][l]; 
	printf("in_pick_piece:card_id=%d\n", card_id);

	// TODO set pick_piece count
	ret = dbin_write(conn, cmd, DB_PICK_PIECE, IN_PICK_PIECE_PRINT
		, eid, ptype, l, card_id, 1, gold, crystal);

	return ret;
}


// CMD:mpiece card_id
// RET:mpiece eid card_id count gold crystal
int dbin_merge_piece(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int card_id;

	// TODO set in design
	int count;
	int gold;
	int crystal;

	ret = sscanf(buffer, "%d", &card_id);
	DEBUG_PRINT(0, "in_merge_piece:sscanf=%d", ret);
	if (ret != 1) {  // refresh is optional ret=1 or 2 are ok
		NET_ERROR_RETURN(conn, -5, "in_merge_piece:input %d", ret);
	}

	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
		NET_ERROR_RETURN(conn, -15, "in_merge_piece:ptype %d", card_id);
	}
	DEBUG_PRINT(0, "in_merge_piece:card_id=%d", card_id);

	design_merge_t *t;
	t = g_design->merge_list + card_id;	
	count = t->count;
	DEBUG_PRINT(0, "in_merge_piece:count=%d", count);
	if (count <= 0) {
		NET_ERROR_RETURN(conn, -25, "in_merge_piece:card_canot_merge %d %d"
		, card_id, count);
	}


	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "in_merge_piece:not_login");
	}

	gold = -t->gold;
	crystal = -t->crystal;
	if (conn->euser.gold + gold < 0) {
		NET_ERROR_RETURN(conn, -6, "%s %d"
		, E_MERGE_PIECE_MONEY_NOT_ENOUGH, -gold);
	}
	if (conn->euser.crystal + crystal < 0) {
		NET_ERROR_RETURN(conn, -16, "%s %d"
		, E_MERGE_PIECE_CRYSTAL_NOT_ENOUGH, -crystal);
	}

	ret = dbin_write(conn, cmd, DB_MERGE_PIECE, IN_MERGE_PIECE_PRINT
	, eid, card_id, count, gold, crystal);
	return ret;
}

// CMD: piece_chapter [card_id]
// RET: piece_chapter [card_id] [count] [chapter_info1] [chapter_info2]...
// chapter_info: [chapter_name] [chapter_id] [stage_id]
int get_piece_chapter(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int card_id;
	char cbuffer[BUFFER_SIZE+1];
	char *ptr;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "get_piece_chapter:not_login");
	}

	ret = sscanf(buffer, "%d", &card_id);
	if (ret < 1) {
		NET_ERROR_RETURN(conn, -5, "get_piece_chapter:input %d", ret);
	}

	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
		NET_ERROR_RETURN(conn, -15, "get_piece_chapter:invalid_card_id %d"
		, card_id);
	}

	design_card_chapter_t &card_chapter = g_design->card_chapter_list[card_id];
	if (card_chapter.card_id == 0) {
		NET_ERROR_RETURN(conn, -6, "get_piece_chapter:not_such_card_chapter %d"
		, card_id);
	}

	bzero(cbuffer, sizeof(cbuffer));
	ptr = cbuffer;
	ptr += sprintf(ptr, "%d %d", card_chapter.card_id, card_chapter.count);
	for (int i = 0; i < card_chapter.count; i++) {
		design_piece_chapter_t &chapter = card_chapter.chapter_list[i];
		ptr += sprintf(ptr, " %s %d %d", chapter.name, chapter.chapter_id
		, chapter.stage_id);
	}

	net_writeln(conn, "%s %s", cmd, cbuffer);
	return ret;
}


// CMD:course 
// RET:course eid course_num
int dbin_get_course(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "in_get_course:not_login");
	}

	ret = dbin_write(conn, cmd, DB_GET_COURSE, IN_GET_COURSE_PRINT
	, eid);
	return ret;
}


// CMD:course course_num
// RET:course eid course_num
int dbin_save_course(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int course;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "in_save_course:not_login");
	}

	ret = sscanf(buffer, "%d", &course);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -25, "in_save_course:input %d", ret);
	}
	if (course < 0 || course > INT32_MAX) {
		NET_ERROR_RETURN(conn, -35, "in_save_course:input %d", course);
	}

	ret = dbin_write(conn, cmd, DB_SAVE_COURSE, IN_SAVE_COURSE_PRINT
	, eid, course);
	return ret;
}

int add_challenge(int eid_my, int eid_oppo, const char *alias_my
, const char *alias_oppo)
{
	int ret; // 0:normal add; 1:challenge again; 2:challenge by oppo sametime
	ret = 0;
	int challenge_again = 0;
	int challenged_by_oppo = 0;
	CHALLENGE_LIST::iterator it;
	for (it=g_challenge_list.begin(); it < g_challenge_list.end(); it++) {
		challenge_t &ct = *it;
		// challenge same player again
		if (ct.eid_challenger == eid_my && ct.eid_receiver == eid_oppo) {
			challenge_again = 1;
			if (challenged_by_oppo == 1) {
				g_challenge_list.erase(it);
			}
		}
		// challenged by eid_oppo same time
		if (ct.eid_challenger == eid_oppo && ct.eid_receiver == eid_my) {
			challenged_by_oppo = 1;
			g_challenge_list.erase(it);
		}
	}

	// challenge by oppo same time, start game
	if (challenged_by_oppo == 1) {
		return 2;
	}

	// challenge again, warn client
	if (challenge_again == 1) {
		return 1;
	}

	// normal add challenge in g_challenge_list
	ret = 0;
	challenge_t ct;
	ct.eid_challenger = eid_my;
	ct.eid_receiver = eid_oppo;
	sprintf(ct.alias_challenger, "%s", alias_my);
	sprintf(ct.alias_receiver, "%s", alias_oppo);
	ct.challenge_time = time(NULL);
	g_challenge_list.push_back(ct);

	return ret;
}


int auto_del_challenge(time_t now)
{
	static time_t last_del_challenge = 0;
	if (now - last_del_challenge < 1) {
		return 0;
	}
	last_del_challenge = now;
	connect_t *conn;
	CHALLENGE_LIST::iterator it;
	for (it=g_challenge_list.begin(); it < g_challenge_list.end(); it++) {
		challenge_t &ct = *it;
		// DEBUG_PRINT(0, "auto_del_challenge:ct.challenge_time = %ld, now = %ld"
		// , ct.challenge_time, now);
		if (now - ct.challenge_time > KEEP_CHALLENGE_TIME) {
			// call challenger
			conn= get_conn_by_eid(ct.eid_challenger);
			if (conn != NULL) {
				net_writeln(conn, "challenge %d %d %d %s"
				, TYPE_CHALLENGE_CANCEL
				, 0, ct.eid_receiver, ct.alias_receiver);
			}
			// call receiver
			conn= get_conn_by_eid(ct.eid_receiver);
			if (conn != NULL) {
				net_writeln(conn, "challenge %d %d %d %s"
				, TYPE_CHALLENGE_CANCEL
				, 1, ct.eid_challenger, ct.alias_challenger);
			}
			// TODO this may be buggy,  when we erase something
			// we should not do it++  (for loop will do)
			g_challenge_list.erase(it);
		}
	}
	return 0;
}

// CMD: challenge challenge_type eid
// RET: challenge challenge_type flag eid alias
// challenger_type: TYPE_CHALLENGE_SEND, TYPE_CHALLENGE_CANCEL
//					, TYPE_CHALLENGE_ACCEPT, TYPE_CHALLENGE_REFUSE
// flag: 0:cmd sender, 1:cmd receiver
int dbin_challenge(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int type;
	int eid_my;
	int eid_oppo;
	int count;
	count = 0;
	int flag_sender = 0;
	int flag_receiver = 1;

	eid_my = get_eid(conn);
	if (eid_my <= 0) {
		NET_ERROR_RETURN(conn, -9, "in_challenge:not_login");
	}

	ret = sscanf(buffer, "%d %d", &type, &eid_oppo);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -25, "in_challenge:input %d", ret);
	}

	if (type != TYPE_CHALLENGE_SEND && type != TYPE_CHALLENGE_CANCEL 
	&& type != TYPE_CHALLENGE_ACCEPT && type != TYPE_CHALLENGE_REFUSE
	) {
		NET_ERROR_RETURN(conn, -35, "in_challenge:ivalid_type %d", type);
	}

	if (eid_oppo == eid_my || eid_oppo <= MAX_AI_EID) {
		NET_ERROR_RETURN(conn, -45, "in_challenge:ivalid_eid %d", eid_oppo);
	}
	
	if (type == TYPE_CHALLENGE_SEND) {
		const char *alias_my;
		const char *alias_oppo;
		connect_t *conn_oppo;

		if (conn->room != NULL) {
			NET_ERROR_RETURN(conn, -16, E_CHALLENGE_IN_ROOM);
		}
		conn_oppo = get_conn_by_eid(eid_oppo);
		if (conn_oppo == NULL) {
			NET_ERROR_RETURN(conn, -26, E_CHALLENGE_OPPO_OFFLINE);
		}
		if (conn_oppo->room != NULL) {
			NET_ERROR_RETURN(conn, -36, E_CHALLENGE_OPPO_IN_ROOM);
		}
		alias_my = conn->euser.alias;
		alias_oppo = conn_oppo->euser.alias;
		ret = add_challenge(eid_my, eid_oppo, alias_my, alias_oppo);

		// challenge already in list
		if (ret == 1) {
			NET_ERROR_RETURN(conn, -46, "in_challenge:send_already_challenge");
		}

		// game start
		if (ret == 2) {
			net_writeln(conn, "%s %d %d %d %s", cmd, TYPE_CHALLENGE_ACCEPT
			, flag_receiver, eid_oppo, conn_oppo->euser.alias);
			net_writeln(conn_oppo, "%s %d %d %d %s", cmd, TYPE_CHALLENGE_ACCEPT
			, flag_receiver, eid_my, conn->euser.alias);

			// load both players deck
			// make sure eid1 is challenger, eid2 is receiver
			ret = dbin_write(conn, cmd, DB_CHALLENGE, IN_CHALLENGE_PRINT
			, eid_oppo, eid_my);
			// start_challenge(eid_my, eid_oppo);
			return 0;
		}

		// add challenge in list
		net_writeln(conn, "%s %d %d %d %s", cmd, TYPE_CHALLENGE_SEND
		, flag_sender, eid_oppo, conn_oppo->euser.alias);
		net_writeln(conn_oppo, "%s %d %d %d %s", cmd, TYPE_CHALLENGE_SEND
		, flag_receiver, eid_my, conn->euser.alias);
		return 0;

		return 0;
	}
	
	if (type == TYPE_CHALLENGE_CANCEL) {
		int count = 0;
		CHALLENGE_LIST::iterator it;
		char alias_oppo[EVIL_USERNAME_MAX + 1];
		bzero(alias_oppo, sizeof(alias_oppo));
		for (it=g_challenge_list.begin(); it < g_challenge_list.end(); it++) {
			challenge_t &ct = *it;
			if (ct.eid_challenger == eid_my && ct.eid_receiver == eid_oppo) {
				sprintf(alias_oppo, "%s", ct.alias_receiver);	
				g_challenge_list.erase(it);
				count ++;
			}
		}
		if (count == 0) {
			NET_ERROR_RETURN(conn, -6, "in_challenge:cancel_no_challenge");
		}
		net_writeln(conn, "%s %d %d %d %s", cmd, TYPE_CHALLENGE_CANCEL
		, flag_sender, eid_oppo, alias_oppo);
		connect_t *conn_oppo;
		conn_oppo = get_conn_by_eid(eid_oppo);
		if (conn_oppo != NULL) {
			net_writeln(conn_oppo, "%s %d %d %d %s", cmd, TYPE_CHALLENGE_CANCEL
			, flag_receiver, eid_my, conn->euser.alias);
		}
		return 0;
	}
	
	if (type == TYPE_CHALLENGE_ACCEPT) {
		if (eid_oppo <= MAX_AI_EID) {
			NET_ERROR_RETURN(conn, -6
			, "in_challenge:accept_cannot_challenge_ai %d", eid_oppo);
		}
		if (conn->room != NULL) {
			NET_ERROR_RETURN(conn, -16, "in_challenge:accept_in_room");
		}
		connect_t *conn_oppo;
		conn_oppo = get_conn_by_eid(eid_oppo);
		if (conn_oppo == NULL) {
			NET_ERROR_RETURN(conn, -26, "in_challenge:accept_oppo_not_login");
		}
		if (conn_oppo->room != NULL) {
			NET_ERROR_RETURN(conn, -36, "in_challenge:accept_oppo_in_room");
		}
		CHALLENGE_LIST::iterator it;
		for (it=g_challenge_list.begin(); it < g_challenge_list.end(); it++) {
			challenge_t &ct = *it;
			if (ct.eid_challenger == eid_oppo && ct.eid_receiver == eid_my) {
				g_challenge_list.erase(it);
				count++;
			}
		}
		if (count == 0) {
			NET_ERROR_RETURN(conn, -46, "in_challenge:accept_no_challenge");
		}
		net_writeln(conn, "%s %d %d %d %s", cmd, TYPE_CHALLENGE_ACCEPT
		, flag_sender, eid_oppo, conn_oppo->euser.alias);
		net_writeln(conn_oppo, "%s %d %d %d %s", cmd
		, TYPE_CHALLENGE_ACCEPT
		, flag_receiver, eid_my, conn->euser.alias);
		// load both players deck
		// make sure eid1 is challenger, eid2 is receiver
		ret = dbin_write(conn, cmd, DB_CHALLENGE, IN_CHALLENGE_PRINT
		, eid_oppo, eid_my);
		// start_challenge(eid_oppo, eid_my);
		return 0;
	}
	
	if (type == TYPE_CHALLENGE_REFUSE) {
		connect_t *conn_oppo;
		conn_oppo = get_conn_by_eid(eid_oppo);
		if (conn_oppo == NULL) {
			NET_ERROR_RETURN(conn, -26, "in_challenge:refuse_oppo_not_login");
			return 0;
		}
		if (conn_oppo->room != NULL) {
			NET_ERROR_RETURN(conn, -36, "in_challenge:refuse_oppo_in_room");
			return 0;
		}
		int count = 0;
		CHALLENGE_LIST::iterator it;
		for (it=g_challenge_list.begin(); it < g_challenge_list.end(); it++) {
			challenge_t &ct = *it;
			if (ct.eid_challenger == eid_oppo && ct.eid_receiver == eid_my) {
				count ++;
				g_challenge_list.erase(it);
			}
		}
		if (count == 0) {
			NET_ERROR_RETURN(conn, -6, "in_challenge:refuse_no_challenge");
		}
		net_writeln(conn, "%s %d %d %d %s", cmd, TYPE_CHALLENGE_REFUSE
		, flag_sender, eid_oppo, conn_oppo->euser.alias);
		net_writeln(conn_oppo, "%s %d %d %d %s", cmd, TYPE_CHALLENGE_REFUSE
		, flag_receiver, eid_my, conn->euser.alias);
		return 0;
	}

	return 0;
}

// load mission
// CMD: lmis
int dbin_load_mission(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -8, "not_login");
	}

	ret = dbin_write(conn, cmd, DB_LOAD_MISSION, IN_LOAD_MISSION_PRINT
	, eid);
	
	return ret;
}

// copy from in_save_mission
int save_mission(connect_t *conn)
{
	int ret;
	int eid;
	char line[DB_BUFFER_MAX]; // maybe too large?
	char * ptr;
	int msize;
	eid = get_eid(conn);
	if (eid <= 0) {
		ERROR_RETURN(-8, "not_login");
	}
	int guild_lv = 0;
	if (conn->euser.gid > 0 && conn->euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(conn->euser.gid);
		guild_lv = guild.glevel;
	}

	mission_t * mlist = conn->euser.mission_list;
	msize = mlist_size(mlist); // this is slow
	// DEBUG_PRINT(0, "dbin_save_mission: mlist.size=%d", msize);

	// DEBUG : remove later:
	ret = mission_refresh(mlist, g_design->mission_list, 
	conn->euser.lv, guild_lv, conn->euser.card); // card already bzero
	if (conn->euser.monthly_end_date > time(NULL))
	{
		ret |= monthly_mission_update(conn);
	}
	change_mission(conn, ret); // refresh, mchange
	// print_mission_list(mlist);

	ptr = line;

	// TODO mlist may include  00 mission
	ptr += sprintf(line, "%d %d", eid, msize);
	// each record < 30 bytes (7 num, each num 3 digits)
	for (int i=0; i<MAX_MISSION; i++) {
		if (mlist[i].mid == 0) {
			continue;
		}
		mission_t & mis = mlist[i];
		BUG_PRINT(mis.mid == 0, "save_mission found 00 mission");

		// mission-fix
		ptr += sprintf(ptr, IN_SAVE_MISSION_PRINT
		, mis.mid, mis.status, mis.n1, mis.last_update); 

		if (ptr - line > DB_BUFFER_MAX - 300) {
			ERROR_RETURN(-22, "save_mission:buffer_overflow %zu"
			, (ptr - line));
			// safety, use return
		}
	}

	// TODO find a way to avoid copy large buffer
	ret = dbin_write(conn, "smis", DB_SAVE_MISSION, "%s", line);

	return ret;
}

// save mission
// CMD: smis
int dbin_save_mission(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	char line[DB_BUFFER_MAX]; // maybe too large?
	char * ptr;
	int msize;
	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -8, "not_login");
	}
	int guild_lv = 0;
	if (conn->euser.gid > 0 && conn->euser.gpos < 9) {
		// assert guild not null, init in out_login
		guild_t & guild = get_guild_by_gid(conn->euser.gid);
		guild_lv = guild.glevel;
	}

	mission_t * mlist = conn->euser.mission_list;
	msize = mlist_size(mlist); // this is slow
	DEBUG_PRINT(0, "dbin_save_mission: mlist.size=%d", msize);

	// DEBUG : remove later:
	ret = mission_refresh(mlist, g_design->mission_list, 
	conn->euser.lv, guild_lv, conn->euser.card); // card already bzero
	print_mission_list(mlist, g_design->mission_list);
	if (conn->euser.monthly_end_date > time(NULL))
	{
		ret |= monthly_mission_update(conn);
	}
	change_mission(conn, ret); // refresh, mchange

	ptr = line;

	// TODO mlist may include  00 mission
	ptr += sprintf(line, "%d %d", eid, msize);
	// each record < 30 bytes (7 num, each num 3 digits)
	for (int i=0; i<MAX_MISSION; i++) {
		if (mlist[i].mid == 0) {
			continue;
		}
		mission_t & mis = mlist[i];
		BUG_PRINT(mis.mid == 0, "save_mission found 00 mission");

		// mission-fix
		ptr += sprintf(ptr, IN_SAVE_MISSION_PRINT
		, mis.mid, mis.status, mis.n1, mis.last_update);

		if (ptr - line > DB_BUFFER_MAX - 300) {
			ERROR_RETURN(-22, "save_mission:buffer_overflow %zu"
			, (ptr - line));
			// safety, use return
		}
	}

	// TODO find a way to avoid copy large buffer
	ret = dbin_write(conn, cmd, DB_SAVE_MISSION, "%s", line);

	return ret;
}


/**
 * CMD: mreward [mid]
 * RET: mreward [mid] [reward_exp] [gold] [crystal] [power] [lv_offset]
 *				[card_count] [card_list] [piece_count] [piece_list]
 * card_list: card_id1 card_id2 ...
 * piece_list: piece_id1 count1 piece_id2 count2 ...
 * ERR: mreward -6  mission_not_ok
 * ERR: mreward -3  mission_not_found (BUG)
 * ERR: mreward -5  empty_mid_input
 * ERR: mreward -15  invalid_mid  (<=0)
 */
int dbin_mission_reward(connect_t *conn, const char *cmd, const char *buffer) 
{
	int ret;
	int mid;
	char *tmp_ptr;
	char tmp_buffer[1000];
	int change = MISSION_UP_NUM;  // notify to save_mission
	int eid = get_eid(conn);

	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -8, "mreward:not_login");
	}

	ret = sscanf(buffer, "%d", &mid);
	if (ret < 1) {
		NET_ERROR_RETURN(conn, -5, "mreward:input %d", ret);
	}
	if (mid <= 0) {
		NET_ERROR_RETURN(conn, -15, "invalid_mid %d", mid);
	}

	evil_user_t &euser = conn->euser;
	mission_t * mlist = euser.mission_list;
	mission_t &mission = mlist[mid];

	if (mission.mid <= 0) {
		NET_ERROR_RETURN(conn, -3, "mreward:mission_not_found %d eid=%d"
		, mid, eid);
	}

	if (mission.status != MISSION_STATUS_OK) {
		NET_ERROR_RETURN(conn, -6, "mreward:mission_not_ok %d eid=%d"
		, mid, eid);
	}

	design_mission_t &dmis = g_design->mission_list[mid];
	if (dmis.mid == 0) {
		NET_ERROR_RETURN(conn, -7, "mreward:design_mission_not_found %d eid=%d"
		, mid, eid);
	}

	// core logic:
	// 1. update mlist status
	// 2. update gold, crystal in memory (euser.xxx)
	// 3. update exp in memory (euser.xxx), check levelup (add_exp)
	//    change |= levelup_mission_update()
	// 4. update card in memory (check 9?)
	//    change |= card_mission_update()
	// 5. change_mission(conn, change);  // already save_mission
	// 6. save status to database (gold,crystal,exp >0)
	//    save card list to database (reward_card>0)
	//    

	// update mission list
	ret = mission_finish(mlist, g_design->mission_list, mid);
	if (ret < 0) {
		NET_ERROR_RETURN(conn, -6, E_MISSION_NOT_REACH);
	}

	// TODO gold, crystal and card should be in out_mission_reward
	euser.gold += dmis.reward_gold;   
	euser.crystal += dmis.reward_crystal;   
	// add_card(card, card_id);
	for (int cdx = 0; cdx < dmis.card_count; cdx++) {
		add_card(euser, dmis.reward_card[cdx], 1);  // reward_card=0 is ok
		if (dmis.reward_card[cdx] != 0) {
			const card_t *pcard = get_card_by_id(dmis.reward_card[cdx]);
			if (pcard != NULL) {
				if (pcard->star >= 4) {
					sys_wchat(1, SYS_WCHAT_GET_CARD
					, conn->euser.alias, pcard->star, pcard->name);
				}
			}
		}

		// update MISSION_CARD
		int cc = 0;
		if (dmis.reward_card[cdx] != 0) {
			cc = conn->euser.card[dmis.reward_card[cdx] - 1] - '0';
			// DEBUG_PRINT(0, "in_mission_reward:cc=%d", cc);
			change |= card_mission_update(conn, cc, dmis.reward_card[cdx]);
		}
	}
//	change_mission(conn, change); // refresh, mchange

	// update MISSION_COLLECTION
	int total = 0;
	total = get_collection(conn->euser.card);
	// DEBUG_PRINT(0, "in_mission_reward:total=%d", total);
	change |= collection_mission_update(conn, total);
	// change_mission(conn, change); // refresh, mchange

	// update MISSION_LEVEL
	int lv_offset = 0; // should be zero at this point
	int exp_offset = 0;
	int old_exp = 0;
	old_exp = conn->euser.exp;
	add_exp(conn, dmis.reward_exp, &lv_offset, &exp_offset);
	int diff_exp = conn->euser.exp - old_exp;

	change |= levelup_mission_update(conn, lv_offset);

	// DEBUG_PRINT(change, "mission_reward:before change");
	change_mission(conn, change);
	// DEBUG_PRINT(change, "mission_reward:after change");


	tmp_ptr = tmp_buffer;
	tmp_ptr += sprintf(tmp_ptr, "%d", dmis.card_count);
	for (int cdx = 0; cdx < dmis.card_count; cdx++) {
		tmp_ptr += sprintf(tmp_ptr, " %d", dmis.reward_card[cdx]);
	}
	tmp_ptr += sprintf(tmp_ptr, " %d", dmis.piece_count);
	for (int pdx = 0; pdx < dmis.piece_count; pdx++) {
		tmp_ptr += sprintf(tmp_ptr, " %d %d", dmis.reward_piece[pdx][0]
		, dmis.reward_piece[pdx][1]);
	}

	ret = dbin_write(conn, cmd, DB_MISSION_REWARD, IN_MISSION_REWARD_PRINT
	, eid, mid, diff_exp, dmis.reward_gold, dmis.reward_crystal
	, dmis.reward_power, lv_offset, tmp_buffer);
	return 0;
}

// load slot
// CMD: lslot id
int dbin_load_slot(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int id;
	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -8, "not_login");
	}

	ret = sscanf(buffer, "%d", &id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "load_slot:input_error");
	}

	if (id < 0) {
		NET_ERROR_RETURN(conn, -15, "load_slot:invalid_id %d", id);
	}

	ret = dbin_write(conn, cmd, DB_LOAD_SLOT, IN_LOAD_SLOT_PRINT
	, eid, id);
	
	return ret;
}

// slot list
// CMD: slotlist
int dbin_slot_list(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -8, "not_login");
	}

	ret = dbin_write(conn, cmd, DB_SLOT_LIST, IN_SLOT_LIST_PRINT
	, eid);
	
	return ret;
}

int dbin_save_slot(connect_t *conn, const char *cmd, const char *buffer)
{
	int n;
	int ret;
	int eid;
	int id;
	char name[EVIL_ALIAS_MAX+3];
	char slot[EVIL_CARD_MAX + 5];

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
		
	}

	ret = sscanf(buffer, "%d %30s %n", &id, name, &n);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	if (id <= 0) {
		NET_ERROR_RETURN(conn, -15, "save_slot:invalid_id %d", id);
	}

	if (name[0] == '\0') {
		NET_ERROR_RETURN(conn, -15, "save_slot:invalid_name");
	}

	sprintf(slot, "%.400s", buffer+n);

	ret = check_deck(slot, conn->euser.card, eid);
	// slot count < 40
	if (ret == -52) {
		NET_ERROR_RETURN(conn, -25, E_SAVE_DECK_COUNT_LESS);
	}

	if (ret < 0) {
		NET_ERROR_RETURN(conn, -35, "save_slot:invalid_slot %d", ret);
	}

	ret = dbin_write(conn, cmd, DB_SAVE_SLOT, IN_SAVE_SLOT_PRINT
	, eid, id, name, slot);
	return ret;
}

int dbin_rename_slot(connect_t *conn, const char *cmd, const char *buffer)
{
	int n;
	int ret;
	int eid;
	int id;
	char name[EVIL_ALIAS_MAX+3];
	// wchar_t wstr[EVIL_ALIAS_MAX+3];
	// size_t wlen;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
		
	}

	ret = sscanf(buffer, "%d %30s %n", &id, name, &n);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	if (id <= 0) {
		NET_ERROR_RETURN(conn, -15, "save_slot:invalid_id %d", id);
	}

	if (name[0] == '\0') {
		NET_ERROR_RETURN(conn, -25, "save_slot:invalid_name");
	}

	// _l extension not work on Linux (CentOS)
	// mbstowcs_l(wstr, name, EVIL_ALIAS_MAX+3, g_locale); 
	/*
	mbstowcs(wstr, name, EVIL_ALIAS_MAX+3);
	wlen = wcslen(wstr);
	if (wlen > 10) {
		NET_ERROR_RETURN(conn, -35, "save_slot:name_too_long %zu>10"
		, wlen);
	}
	wcstombs(name, wstr, EVIL_ALIAS_MAX+3);
	*/

	size_t len = strlen(name);
	if (len >= 20) {
		NET_ERROR_RETURN(conn, -35, "save_slot:name_too_long %zu>20"
		, len);
	}

	ret = dbin_write(conn, cmd, DB_RENAME_SLOT, IN_RENAME_SLOT_PRINT
	, eid, id, name);
	return ret;
}

// cmd: bslot flag id
// flag: ==0 get slot prize; ==1 buy by gold; ==2 buy by crystal
int dbin_buy_slot(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	int flag;
	int id;
	int gold;
	int crystal;
	design_slot_t * pslot;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
		
	}

	ret = sscanf(buffer, "%d %d", &flag, &id);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "invalid_input %d", ret);
	}

	if (flag != 0 && flag != 1 && flag != 2) {
		NET_ERROR_RETURN(conn, -15, "save_slot:invalid_flag %d", flag);
	}

	if (id <= 0 || id >= MAX_SLOT_NUM) {
		NET_ERROR_RETURN(conn, -25, "save_slot:invalid_id %d", id);
	}

	pslot = g_design->slot_list + id;	
	if (pslot->id == 0) {
		NET_ERROR_RETURN(conn, -35, "save_slot:no_such_slot eid=%d %d", eid, id);
	}

	if (pslot->id != id) {
		BUG_PRINT(-6, "save_slot:slot_err %d", id);
		NET_ERROR_RETURN(conn, -45, "save_slot:slot_err %d", id);
	}

	if (flag == 0) {
		net_writeln(conn, "%s %d %d %d %d", cmd
		, flag, pslot->id, pslot->gold, pslot->crystal);
		return 0;
	}

	// buy slot by gold
	if (flag == 1) {
		gold = pslot->gold;
		crystal = 0;
	}
	// buy slot by crystal
	if (flag == 2) {
		gold = 0;
		crystal = pslot->crystal;
	}
	if (flag == 1 && gold <= 0) {
		NET_ERROR_RETURN(conn, -45, "buy_slot:slot_gold_err %d %d %d"
		, flag, id, gold);
	}

	if (flag == 2 && crystal <= 0) {
		NET_ERROR_RETURN(conn, -45, "buy_slot:slot_crystal_err %d %d %d"
		, flag, id, crystal);
	}

	if (conn->euser.gold < gold || conn->euser.crystal < crystal) {
		NET_ERROR_RETURN(conn, -6, "%s %d %d %d %d"
		, E_MONEY_NOT_ENOUGH, flag, id, gold, crystal);
	}
		
	ret = dbin_write(conn, cmd, DB_BUY_SLOT, IN_BUY_SLOT_PRINT
	, eid, flag, id, gold, crystal);
	return ret;
}

match_t * get_match_by_eid(int eid)
{
	if (eid <= MAX_AI_EID) {
		ERROR_PRINT(-6, "get_match_by_eid:invalid_eid %d", eid);
		return NULL;
	}

	for (int i = 0; i < MAX_MATCH; i++) {
		match_t &match = g_match_list[i];
		if (match.match_id == 0) {
			continue;
		}

		for (int j = 0; j < match.max_player; j++) {
			match_player_t &tmp_player = match.player_list[j];
			if (tmp_player.eid == eid) {
				return &match;
			}
		}
	}

	return NULL;
}


/*
 * CMD: lmatch
 * RET: lmatch [applied_match_id] [match_count] [match_info]
 * match_info = [match_id] [match_status] [title] [max_player] [current_player] [start_time] [MATCH_ROUND_TIME] [daily_info]
 * daily_info = max_daily_round t1 t2 ... tn (n=max_daily_round)
 * t1, t2 ... tn = time_t, base 1970-01-01, 0 <= ti < 24 * 60 * 60
*/
int list_match(connect_t *conn, const char *cmd, const char *buffer)
{
	int count;
	int eid; 

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "list_match:not_login");
	}

	char out_buffer[MAX_MATCH * 105];
	bzero(out_buffer, sizeof(out_buffer));
	char * ptr;

	count = 0;
	ptr = out_buffer;
	for (int i = 0; i < MAX_MATCH; i++) {
		match_t &match = g_match_list[i];
		if (match.match_id == 0) {
			continue;
		}
		count ++;

		int num = 0; // match apply player num
		for(int j=0; j < match.max_player; j++) {
			match_player_t &player = match.player_list[j];
			if (player.eid == 0) {
				continue;
			}
			num ++;
		}
		ptr += sprintf(ptr, "%ld %d %d %s %d %d %ld %d %d "
		, match.match_id
		, match.status, match.round, match.title, match.max_player
		, num, match.start_time, MATCH_ROUND_TIME, MAX_DAILY_ROUND);
	
		for (int j=0; j < MAX_DAILY_ROUND; j++) {
			ptr += sprintf(ptr, "%ld ", match.round_time_list[j]);
		}

	}

	long match_id = 0;
	match_t *pmatch = get_match_by_eid(eid);
	if (pmatch != NULL) {
		match_id = pmatch->match_id;
	}

	net_writeln(conn, "%s %ld %d %s", cmd, match_id, count, out_buffer);

	return 0;
}


/**
 * player_data
 * CMD: player_data [eid_option]
 * no need send match_id, because we set one player only can join one match at the same time
 * RET: player_data [match_data] [round_date] [my_info] [oppo_info] 
 * match_data = [match_id] [status] [round] [title] [max_player] [start_time] [MATCH_ROUND_TIME] [MAX_DAILY_ROUND] [t1] [t2] [t3] [t4] [tn]
 * info : [eid] [round] [team_id] [win] [lose] [draw] [tid] [point] 
 * [oppo_info] may null, only match.status == MATCH_STATUS_ROUND_START/MATCH_STATUS_ROUND_END can get [oppo_info], [round_date]
 */
int get_match_player_data(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	char match_buffer[200];
	char player_buffer[1000];
	bzero(match_buffer, sizeof(match_buffer));
	bzero(player_buffer, sizeof(player_buffer));
	char * ptr;
	match_player_t oppo_player = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "_"};
	time_t round_date = 0;

	ret = sscanf(buffer, "%d", &eid);
	if (ret == 1 && eid <= MAX_AI_EID) {
		NET_ERROR_RETURN(conn, -6, "match_player_data:invalid_eid %d", eid);
	}

	if (ret <= 0) {
		eid = get_eid(conn);
		if (eid <= 0) {
			NET_ERROR_RETURN(conn, -9, "match_player_data:not_login");
		}
	}

	DEBUG_PRINT(0, "match_player_data:%d", eid);

	match_t *match = get_match_by_eid(eid);
	if (match == NULL) {
		NET_ERROR_RETURN(conn, -3, "match_player_data:not_in_match");
	}


	ptr = match_buffer;
	// match_data
	ptr += sprintf(ptr, "%ld %d %d %s %d %ld %d %d"
	, match->match_id, match->status, match->round
	, match->title, match->max_player
	, match->start_time, MATCH_ROUND_TIME, MAX_DAILY_ROUND);
	for (int i=0; i < MAX_DAILY_ROUND; i++) {
		ptr += sprintf(ptr, " %ld", match->round_time_list[i]);
	}
	
	ptr = player_buffer;
	time_t this_round_time = 0;
	time_t next_round_time = 0;
	time_t third_round_time = 0;
	time_t now = time(NULL);
	struct tm timestruct;
	time_t today;
	time_t one_day_sec = 60 * 60 *24;
	localtime_r(&now, &timestruct);
	timestruct.tm_sec = 0;
	timestruct.tm_min = 0;
	timestruct.tm_hour = 0;
	today = mktime(&timestruct); // get today 00:00 time

	for (int i = 0; i < MAX_DAILY_ROUND; i++) {
		// round_time_list[i+1] always bigger than round_time_list[i], except 0
		time_t tt = match->round_time_list[i];
		if (tt == 0) {
			continue;
		}
		if (now >= today + tt && now < today + tt + MATCH_ROUND_TIME) {
			this_round_time = today + tt;
		}
		if (now < today + tt && next_round_time == 0) {
			next_round_time = today + tt;
		}
		if (now < today + tt && next_round_time != today + tt) {
			third_round_time = today + tt;
			break;
		}
	}
	// INFO_PRINT(0, "round=%d this_r=%ld next_r=%ld %ld", match->round, this_round_time, next_round_time, time(NULL));

	// get yesterday last round
	if (this_round_time == 0) {
		for (int i = MAX_DAILY_ROUND - 1; i >= 0; i--) {
			time_t tt = match->round_time_list[i];
			if (tt == 0) {
				continue;
			}
			this_round_time = today - one_day_sec + tt;
			break;
		}
	}
	// get tommrow first round
	if (next_round_time == 0) {
		next_round_time = today + one_day_sec + match->round_time_list[0];
	}

	if (third_round_time == 0) {
		if (next_round_time 
		!= today + one_day_sec + match->round_time_list[0]) {
			third_round_time = today + one_day_sec + match->round_time_list[0];
		} else if (match->round_time_list[1] != 0) {
			third_round_time = today + one_day_sec + match->round_time_list[1];
		} else {
			third_round_time = today + 2 * one_day_sec + match->round_time_list[0];
		} 
	}

	INFO_PRINT(0, "round=%d this_r=%ld next_r=%ld third_r=%ld %ld", match->round, this_round_time, next_round_time, third_round_time, time(NULL));

	
	match_player_t &player = get_player_last_round(*match, eid);
	if (player.eid == 0) {
		NET_ERROR_RETURN(conn, -6, "match_player_data:no_such_player");
	}
	DEBUG_PRINT(0, "my_player round=%d point=%d", player.round, player.point);

	// TODO add icon in match_player_t
	ptr += sprintf(ptr, "%d %d %d %d %d %d %d %d %d %s"
	, player.eid, player.round, player.team_id
	, player.win, player.lose, player.draw, player.tid, player.point
	, player.icon, player.alias);

	if (match->status == MATCH_STATUS_READY) {
		net_writeln(conn, "%s %s %ld %s"
		, cmd, match_buffer, 0, player_buffer);
		return 0;
	}

	ret = match_next(*match, eid, oppo_player);
	DEBUG_PRINT(0, "oppo_player round=%d point=%d"
	, oppo_player.round, oppo_player.point);
	// if oppo_player.round == match.round, oppo player is this round oppo_player, round_date = this round start time
	// if oppo_player.round > match.round, oppo player is next round oppo player, round_date = next round start time
	if (oppo_player.round == match->round) {
		if (oppo_player.point >= 5) {
			// no more game
			round_date = 0;
		} else {
			round_date = this_round_time;
		}
	}
	if (oppo_player.round > match->round) {
		round_date = next_round_time;
	}
	// win first and second round in match, will pass next round
	if (oppo_player.eid == 0 && player.point >= 5 && player.round == MAX_TEAM_ROUND) {
		if (match->round == MAX_TEAM_ROUND) {
			round_date = next_round_time;
		} else {
			round_date = third_round_time;
		}
	}

	INFO_PRINT(0, "round=%d this_r=%ld next_r=%ld %ld", match->round, this_round_time, next_round_time, time(NULL));
	
	if (ret == 0) {
		// TODO add icon in match_player_t
		ptr += sprintf(ptr, " %d %d %d %d %d %d %d %d %d %s"
		, oppo_player.eid, oppo_player.round, oppo_player.team_id
		, oppo_player.win, oppo_player.lose, oppo_player.draw
		, oppo_player.tid, oppo_player.point, oppo_player.icon, oppo_player.alias);
	}

	net_writeln(conn, "%s %s %ld %s"
	, cmd, match_buffer, round_date, player_buffer);

	return 0;
}

/**
 * match data
 * CMD: match_data [match_id]
 * RET: match_data 
 */

int get_match_data(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	long match_id;
	int flag, flag_id;
	char out_buffer[3000];
	bzero(out_buffer, sizeof(out_buffer));

	ret = sscanf(buffer, "%ld %d %d", &match_id, &flag, &flag_id);

	// for debug print
	if (ret == 1) {
		match_t *match = NULL;
		for (int i = 0; i < MAX_MATCH; i++) {
			if (g_match_list[i].match_id == match_id) {
				match = g_match_list + i;
				break;
			}
		}
		if (match == NULL) {
			NET_ERROR_RETURN(conn, -3, "match_data:match_null");
		}
		match_info(*match, match->player_list);
		match_eli_info(*match, match->e_player_list);
		print_all_record(*match, match->player_list);
		print_all_eli_record(*match, match->e_player_list);
		return 0;
	}

	if (ret != 3) {
		NET_ERROR_RETURN(conn, -5, "match_data:invalid_input");
	}
	if (flag != MATCH_DATA_TEAM_FLAG && flag != MATCH_DATA_ELI_FLAG) {
		NET_ERROR_RETURN(conn, -15, "match_data:invalid_flag");
	}

	match_t *match = NULL;
	for (int i = 0; i < MAX_MATCH; i++) {
		if (g_match_list[i].match_id == match_id) {
			match = g_match_list + i;
			break;
		}
	}
	if (match == NULL) {
		NET_ERROR_RETURN(conn, -3, "match_data:match_null");
	}

	DEBUG_PRINT(0, "match_data:status=%d round=%d", match->status, match->round);

	if (flag == MATCH_DATA_TEAM_FLAG) {
		// team_id in match.player_list record is base 1
		if (flag_id <= 0 || flag_id > match->max_team) {
			NET_ERROR_RETURN(conn, -25, "match_data:invalid_team_id");
		}
		match_team_data(*match, flag_id, out_buffer);
	}
	if (flag == MATCH_DATA_ELI_FLAG) {
		if (flag_id < 0 || flag_id >= match->max_player) {
			NET_ERROR_RETURN(conn, -25, "match_data:invalid_tid");
		}
		if (match->round < MAX_TEAM_ROUND
		|| (match->round == MAX_TEAM_ROUND && match->status != MATCH_STATUS_ROUND_END)) {
			NET_ERROR_RETURN(conn, -6, "match_data:eli_match_not_started %d"
			, match->status);
		}
		match_eli_data(*match, flag_id, out_buffer);
	}
	// DEBUG_PRINT(0, "match_data:%s", out_buffer);

	match_info(*match, match->player_list);
	match_eli_info(*match, match->e_player_list);

	net_writeln(conn, "%s %s", cmd, out_buffer);

	return 0;
}



// XXX useless now
// start a match game
// assume only match_player can join in match_room before game start (check by room->match_eid), other guest only join in match_room after game start
// 1.search oppo_player already in match channel, and wait in a match_room, if true, join in that room and start game
// 2.if oppo_player not in match_room, create a match_room and wait
int match_game_start(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	long match_id;
	char line[SMALL_BUFFER_SIZE];

	match_player_t oppo_player = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "_"};

	if (conn->room != NULL) {
		NET_ERROR_RETURN(conn, -6, "match_start:already_in_room");
	}

	ret = sscanf(buffer, "%ld", &match_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -9, "match_start:invalid_input");
	}
	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "match_start:not_login");
	}

	if (match_id == 0) {
		NET_ERROR_RETURN(conn, -15, "match_start:invalid_match_id");
	}

	match_t &match = get_match(match_id);
	DEBUG_PRINT(0, "match_game_start:match_id=%ld title=%s max_player=%d max_team=%d start_time=%ld status=%d", match.match_id, match.title, match.max_player, match.max_team, match.start_time, match.status);
	if (match.match_id == 0) {
		NET_ERROR_RETURN(conn, -6, "match_start:no_such_match");
	}

	if (match.status != MATCH_STATUS_ROUND_START) {
		NET_ERROR_RETURN(conn, -16, "match_start:match_not_start");
	}

	match_player_t &player = get_player_last_round(match, eid);
	if (player.eid == 0) {
		NET_ERROR_RETURN(conn, -26, "match_start:no_such_player");
	}

	printf("match_start:below is my player\n");
	print_player(player);

	ret = match_next(match, eid, oppo_player);
	if (ret != 0) {
		NET_ERROR_RETURN(conn, -36, "match_start:no_oppo_player");
	}
	// if player.point >= 5, means this round is over, and no more game for now
	if (player.point >= 5 || oppo_player.point >= 5) {
		NET_ERROR_RETURN(conn, -56, "match_start:no_more_game_point %d %d", player.point, oppo_player.point);
	}
	// oppo_player.eid maybe a fake eid(<0)
	if (oppo_player.eid < 0) {
		NET_ERROR_RETURN(conn, -46, "match_start:oppo_player_not_ready %d", oppo_player.eid);
	}
	if (oppo_player.eid == 0) {
		NET_ERROR_RETURN(conn, -76, "match_start:no_more_game 0");
	}
	printf("match_start:below is oppo player %d \n", oppo_player.eid);
	print_player(oppo_player);


	// assume only match_player can join in match_room before game start (check by room->match_eid), other guest only join in match_room after game start
	// 1.search oppo_player already in match channel, and wait in a match_room, if true, join in that room and start game
	// 2.if oppo_player not in match_room, create a match_room and wait

	room_t * proom = NULL;
	connect_t *oppo_conn = get_conn_by_eid(oppo_player.eid);
	// below situation should create a match room
	// 1. oppo_player offline
	// 2. oppo_player->room == NULL
	// 3. oppo_player->room->channel != CHANNEL_MATCH
	// 4. oppo_player->room->match_eid[0], [1] != my eid (oppo_player still in before round)
	if (oppo_conn == NULL || oppo_conn->room == NULL 
	|| oppo_conn->room->channel != CHANNEL_MATCH
	|| (oppo_conn->room->match_eid[0] != eid && oppo_conn->room->match_eid[1] != eid)
	) {

		/*
		proom = create_match_room(match, eid, oppo_player.eid);	
		if (proom == NULL) {
			NET_ERROR_RETURN(conn, -7, "match_game_start:create_room");
		}
		*/
	} else {
		proom = oppo_conn->room;
	}

	if (proom->state == ST_GAME) {
		NET_ERROR_RETURN(conn, -16, "match_game_start:game_already_start");
	}

	// add player to match_room
	ret = do_room_add(proom, conn);
	if (ret < 0) {
		NET_ERROR_RETURN(conn, -17, "new_room_add_2 %d", ret);
	}

	// only one player waitting, and oppo player is not ai
	if (proom->guest[1] == 0
	&& proom->match_eid[0] > MAX_AI_EID
	&& proom->match_eid[1] > MAX_AI_EID) {
		str_room_info(line, proom);
		net_write_space(conn, cmd);
		net_write(conn, line, '\n');
		return 0;
	}

	// now have 2 situation
	// 1. 2 match player2 inside
	// 2. 1 match player inside, oppo player is ai
	if (proom->guest[1] == 0) {
		if (proom->match_eid[0] > 0
		&& proom->match_eid[0] <= MAX_AI_EID) {
			proom->guest[1] = proom->match_eid[0];
		} 
		if (proom->match_eid[1] > 0
		&& proom->match_eid[1] <= MAX_AI_EID) {
			proom->guest[1] = proom->match_eid[1];
		}
		if (proom->guest[1] <= 0 || proom->guest[1] > MAX_AI_EID) {
			NET_ERROR_PRINT(conn, -27, "match_game_start:oppo_player_eid_error %d", proom->guest[1]);
			force_room_clean(proom);
			return -27;
		}

		// check ai invalid
		int ai_eid = proom->guest[1];
		if (ai_eid <=0 || ai_eid >= (int)(sizeof(g_design->ai_list) / sizeof(g_design->ai_list[0]))) {
			force_room_clean(proom);
			NET_ERROR_RETURN(conn, -2, "hero_id_out_bound %d", ai_eid);
		}
		if (g_design->ai_list[ai_eid].id != ai_eid) {
			force_room_clean(proom);
			NET_ERROR_RETURN(conn, -5, "hero_id_invalid %d", ai_eid);
		}

		ai_t *pai;
		pai = g_design->ai_list+ai_eid;
		proom->num_guest = 2;

		// start AI match
		proom->rating[1] = pai->rating; // hard coded for solo
		strcpy(proom->alias[1], pai->alias);
		proom->icon[1] = pai->icon; 
		proom->lv[1] = pai->lv; 
		sprintf(proom->title, "%s~VS~%s", conn->euser.alias, pai->alias);
		// shall we init gameid ?  NO, let out_game do
		// order is important, room creater eid must before room joiner eid
		ret = dbin_write(conn, cmd, DB_GAME, IN_GAME_PRINT
		, proom->guest[0], proom->guest[1]);

		return 0;
	}

	// 2 guest eid mismatch to match player eid
	// [1,2] [1,2] ==> false
	// [1,2] [2,1] ==> false
	// [1,2] [1,3] ==> ture
	if ((proom->guest[0] != proom->match_eid[0] 
	|| proom->guest[1] != proom->match_eid[1]) 
	&& (proom->guest[0] != proom->match_eid[1] 
	|| proom->guest[1] != proom->match_eid[0])) {

		NET_ERROR_PRINT(conn, -27
		, "match_game_start:player_eid_mismatch [%d,%d] [%d,%d]"
		, proom->guest[0], proom->guest[1]
		, proom->match_eid[0], proom->match_eid[1]);
		force_room_clean(proom);
		return -27;
	}

	// start match game with 2 player
	// same as dbin_game
	proom->rating[0] = conn->euser.rating;
	proom->rating[1] = oppo_conn->euser.rating;
	sprintf(proom->title, "%s~VS~%s", conn->euser.alias, oppo_conn->euser.alias);
	// shall we init gameid ?  NO, let out_game do
	// order is important, room creater eid must before room joiner eid
	ret = dbin_write(conn, cmd, DB_GAME, IN_GAME_PRINT
	, proom->guest[0], proom->guest[1]);

	return 0;
}

/**
 * match_apply
 * CMD: match_apply [match_id]
 * RET: match_apply [match_id] [max_player] [current_player]
 */
int dbin_match_apply(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	long match_id;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
		
	}

	match_t *pmatch = NULL;
	pmatch = get_match_by_eid(eid);
	if (pmatch != NULL) {
		NET_ERROR_RETURN(conn, -6, "match_apply:player_already_in_match %d", eid);
	} 

	ret = sscanf(buffer, "%ld", &match_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "match_apply:invalid_input %d", ret);
	}

	match_t &match = get_match(match_id);
	if (match.match_id == 0) {
		NET_ERROR_RETURN(conn, -3, "match_apply:match_null %ld"
		, match_id);
	}

	ret = match_apply(match, eid, conn->euser.icon, conn->euser.alias);
	if (ret != 0) {
		NET_ERROR_RETURN(conn, -6, "match_apply:apply_error %ld %d"
		, match.match_id, eid);
	}

	// if match applyer = match.max_player, init this match
	int num = 0; // match apply player num
	for(int j=0; j < match.max_player; j++) {
		match_player_t &player = match.player_list[j];
		if (player.eid == 0) {
			continue;
		}
		num ++;
	}
	DEBUG_PRINT(0, "match_apply:apply_player_num = %d", num);


	/*
	match_player_t &player = get_player_last_round(*match, match->player_list, eid);
	if (player.eid == 0) {
		NET_ERROR_RETURN(conn, -7, "match_apply:player_null %d", eid);
	}
	*/

	ret = dbin_write(conn, cmd, DB_MATCH_APPLY, IN_ADD_MATCH_PLAYER_PRINT
	, match_id, eid, 0, 0, 0, 0, 0, 0, 0, conn->euser.icon, conn->euser.alias);


	net_writeln(conn, "%s %ld %d %d"
	, cmd, match_id, match.max_player, num);

	return 0;
}

// cmd: match_cancel match_id 
/**
 * match_cancel
 * CMD: match_cancel [match_id]
 * RET: match_cancel [match_id] [max_player] [current_player]
 *
 */
int dbin_match_cancel(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;

	long match_id;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
		
	}

	ret = sscanf(buffer, "%ld", &match_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "match_cancel:invalid_input %d", ret);
	}


	match_t &match = get_match(match_id);
	if (match.match_id == 0) {
		NET_ERROR_RETURN(conn, -3, "match_cancel:match_null %ld"
		, match_id);
	}
	if (match.status != MATCH_STATUS_READY) {
		NET_ERROR_RETURN(conn, -6, "match_cancel:match_no_ready");
	}

	match_player_t &player = get_player_last_round(match, eid);
	if (player.eid == 0) {
		NET_ERROR_RETURN(conn, -16, "match_cancel:not_in_match %d", eid);
	}

	ret = match_cancel(match, eid);
	if (ret != 0) {
		NET_ERROR_RETURN(conn, -26, "match_cancel:cancel_fail %ld %d"
		, match_id, eid);
	}

	int num = 0; // match apply player num
	for(int j=0; j < match.max_player; j++) {
		match_player_t &player = match.player_list[j];
		if (player.eid == 0) {
			continue;
		}
		num ++;
	}
	DEBUG_PRINT(0, "match_cancel:apply_player_num = %d", num);

	ret = dbin_write(conn, cmd, DB_MATCH_CANCEL
	, IN_DELETE_MATCH_PLAYER_PRINT
	, match_id, eid);

	net_writeln(conn, "%s %ld %d %d", cmd, match_id, match.max_player
	, num);

	return 0;
}


int dbin_list_message(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	int start_num;
	int page_size;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = sscanf(buffer, "%d %d", &start_num, &page_size);
	if (ret != 2) {
		NET_ERROR_RETURN(conn, -5, "list_message:invalid_input %d", ret);
	}

	if (page_size >= 10) {
		page_size = 10; // max is 10
	}

	ret = dbin_write(conn, cmd, DB_LIST_EVIL_MESSAGE
	, "%d %d %d"
	, eid, start_num, page_size);

	return 0;
}


int dbin_read_message(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int eid;
	long message_id;

	eid = get_eid(conn);
	if (eid <= 0) {
		NET_ERROR_RETURN(conn, -9, "not_login");
	}

	ret = sscanf(buffer, "%ld", &message_id);
	if (ret != 1) {
		NET_ERROR_RETURN(conn, -5, "list_message:invalid_input %d", ret);
	}

	ret = dbin_write(conn, cmd, DB_READ_EVIL_MESSAGE
	, "%d %ld"
	, eid, message_id);

	return 0;
}


/////////////////// DBIN END ] ///////////////////


/////////////// DBIO MAIN START [ ////////////////
int dbio_init_server_socket()
{ // TODO (int db_id)  UNIX_DOMAIN %d=db_id
	int ret;
	int fd;
	struct sockaddr_un addr;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	FATAL_NEG_EXIT(fd, "dbio_init_server_socket:socket");
	// INFO_PRINT(0, "dbio_init_server_socket:fd=%d", fd);

	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, DBIO_UNIX_DOMAIN);
	unlink(DBIO_UNIX_DOMAIN);	// lets clean up

	ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	FATAL_NEG_EXIT(ret, "dbio_init_server_socket:bind %s", DBIO_UNIX_DOMAIN);

	ret = listen(fd, MAX_DB_THREAD); // usually 1 is enough
	FATAL_NEG_EXIT(ret, "dbio_init_server_socket:listen");

	return fd;
}

int dbio_init_client_socket()
{
	int ret;
	int fd;
	struct sockaddr_un addr;

	fd  = socket(PF_UNIX, SOCK_STREAM, 0); 
	FATAL_NEG_EXIT(fd, "dbio_init_client_socket:fd=%d", fd);

	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, DBIO_UNIX_DOMAIN);
	ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
	FATAL_NEG_EXIT(ret, "dbio_init_client_socket:fd=%d", fd);
	return fd;
}

// main_fd[]:  must have array size = MAX_DB_THREAD
// db_thread[] : also have array size = MAX_DB_THREAD
// caller: 
// int main_fd[MAX_DB_THREAD];
// pthread_t dbio_thread[MAX_DB_THREAD];
// dbio_init(main_fd, dbio_thread);
// TODO use pointer to dbio_data 
int dbio_init(pthread_t *dbio_thread)
{
	int ret;
	int db_fd;
	int main_fd;
	int master_fd = dbio_init_server_socket();
	INFO_PRINT(0, "dbio_init:master_fd=%d", master_fd);
	struct sockaddr_un unix_addr;
	socklen_t addr_len;

	for (int i=0; i<MAX_DB_THREAD; i++) {
		bzero(g_dbio_data+i, sizeof(dbio_init_t));  // must be done first!
		main_fd = dbio_init_client_socket();
		addr_len = sizeof(struct sockaddr_un);
		db_fd = accept(master_fd, (struct sockaddr*)&unix_addr, &addr_len);
		// db_fd is passing to dbio_thread
		g_dbio_data[i].fd = db_fd;
		g_dbio_data[i].main_fd = main_fd;
		// TODO store pthread struct into g_dbio_data too
		INFO_PRINT(0, "dbio_init:i=[%d] main_fd=%d db_fd=%d", i, main_fd, db_fd);

		ret = pthread_create(dbio_thread + i, NULL, dbio
		, (void*)(g_dbio_data+i));
		// , (void *)(size_t)(db_fd));
		FATAL_NEG_EXIT(ret, "dbio_init:pthread_create");

		// connect_t for dbio is not important: TODO generalize it 
		bzero(g_dbio_conn+i, sizeof(connect_t));
		g_dbio_conn[i].state = STATE_DBIO;
		g_dbio_conn[i].conn_fd = -10; // TODO make it constant
		// core logic:
		fdwatch_add_fd(main_fd, g_dbio_conn + i, FDW_READ);  // this related to global!
	}

	// testing ?  dbin 0 1 ?

	return 0; // not yet ready
}

/////////////// DBIO MAIN END ] //////////////////
//////////////////// DBIO END ] //////////////////
//////////////////////////////////////////////////

// CMD: shop [start_card_id] [size]
// RET: shop [count] [card_info1] [card_info2] ...
// card_info: card_buy_gold card_sell_gold card_buy_crystal card_sell_crystal
// 		piece_buy_gold piece_sell_gold piece_buy_crystal piece_sell_crystal
int load_shop(connect_t *conn, const char *cmd, const char *buffer)
{
	int ret;
	int card_id;
	int size;
	int count;

	ret = sscanf(buffer, "%d %d", &card_id, &size);
	if (ret < 2) {
		// net_write_space(conn, cmd);
		NET_ERROR_RETURN(conn, -5, "shop:input %d", ret);
	}

	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
		NET_ERROR_RETURN(conn, -15, "shop:invalid_card_id %d", card_id);
	}

	char card_buff[BUFFER_SIZE];
	bzero(card_buff, BUFFER_SIZE);
	int max_size = 50; // base on BUFFER_SIZE / sizeof(card_info)

	if (size > max_size) {
		size = max_size;
	}

	char * ptr;
	ptr = card_buff;

	count = 0;
	for(int i=card_id;i<400;i++) {
		shop_t *t;
		t = &g_design->shop_list[i];
		if (t->card_id == 0) { //XXX
			continue; // no sell
		}

		ptr += sprintf(ptr, "%d %d %d %d %d %d %d %d %d "
		, t->card_id
		, t->card_buy_gold, t->card_sell_gold
		, t->card_buy_crystal, t->card_sell_crystal
		, t->piece_buy_gold, t->piece_sell_gold
		, t->piece_buy_crystal, t->piece_sell_crystal
		);

		count++;
		if (count >= size) {
			break;
		}
	}

	net_write_space(conn, cmd);
	net_write_space(conn, "%d", count);
	net_writeln(conn, card_buff);
	return ret;
}


int load_merge(connect_t *conn, const char *cmd, const char *buffer)
{
	char card_buff[BUFFER_SIZE];
	card_buff[0] = '\0';

	char * ptr;
	ptr = card_buff;

	for(int i=1;i<=EVIL_CARD_MAX;i++) {
		design_merge_t *t;
		t = g_design->merge_list + i;
		ptr += sprintf(ptr, "%02d", t->count);
	}

	net_write_space(conn, cmd);
	net_writeln(conn, card_buff);
	return 0;
}


// TODO rename command to command_t, stub to connect_t
typedef struct {
	int st;  // 0=nologin, 5=login, 10=in_room, 99=system
	const char * name;
	int (* fun)(connect_t*, const char *, const char *);
} command_t;


command_t command_list[] = {
	{ ST_NULL, "echo", sys_echo }
// dbio new command
,	{ ST_NULL, "log", dbin_login }
,	{ ST_NULL, "reg", dbin_register }
,	{ ST_NULL, "notice", cmd_notice }
,   { ST_LOGIN, "lcard", dbin_load_card }
,   { ST_LOGIN, "job", dbin_job }
,   { ST_LOGIN, "ldeck", dbin_load_deck }
,   { ST_LOGIN, "solo_plus", dbin_solo_plus } // new solo logic
,   { ST_LOGIN, "list_solo", dbin_list_solo }
,   { ST_LOGIN, "solo", dbin_solo }
,	{ ST_LOGIN, "gate_msg", dbin_gate_msg }
,   { ST_LOGIN, "gate", dbin_gate }
,   { ST_LOGIN, "lgate", dbin_list_gate }

	// tower is useless for now
,   { ST_LOGIN, "tower_ladder", dbin_tower_ladder }
,   { ST_LOGIN, "tower_info", dbin_tower_info }
,   { ST_LOGIN, "tower_buff", dbin_tower_buff }
,   { ST_LOGIN, "tower", dbin_tower }

,   { ST_LOGIN, "lchapter", dbin_list_chapter}
,   { ST_LOGIN, "chapter_stage", dbin_chapter_stage }
,   { ST_LOGIN, "chapter_data", dbin_chapter_data }
// ,   { ST_LOGIN, "chapter_reward", dbin_chapter_reward }
,   { ST_LOGIN, "chapter", dbin_chapter }

,   { ST_LOGIN, "alias", dbin_alias }
,   { ST_LOGIN, "ralias", cmd_random_alias }
,   { ST_ROOM, "game", dbin_game }
,   { ST_LOGIN, "quick", dbin_quick }
,   { ST_NULL, "sdebug", dbin_save_debug }
,	{ ST_NULL, "ldebug", dbin_load_debug }
,   { ST_LOGIN, "sta", dbin_status }
,   { ST_LOGIN, "sdeck", dbin_save_deck }
,   { ST_LOGIN, "buy", dbin_buy_card }
,	{ ST_LOGIN, "sell", dbin_sell_card }
,	{ ST_LOGIN, "batch", dbin_batch } 
,	{ ST_LOGIN, "pick", dbin_pick } 
,	{ ST_LOGIN, "lottery", dbin_lottery } 
,	{ ST_LOGIN, "pshop", dbin_piece_shop} 
,	{ ST_LOGIN, "rpshop", dbin_refresh_piece_shop} 
,	{ ST_LOGIN, "lpshop", dbin_get_piece_list }
,	{ ST_LOGIN, "pbuy", dbin_piece_buy }
,	{ ST_LOGIN, "xcadd", dbin_xcadd } 
,	{ ST_LOGIN, "xcbuy", dbin_xcbuy } 
,	{ ST_LOGIN, "xclist", dbin_xclist } 

,	{ ST_LOGIN, "lguild", dbin_list_guild } 
,	{ ST_LOGIN, "cguild", dbin_create_guild } 
,	{ ST_LOGIN, "dguild", dbin_delete_guild } 
,	{ ST_LOGIN, "glist", dbin_glist } 
,	{ ST_LOGIN, "gapply", dbin_gapply } 
,	{ ST_LOGIN, "gpos", dbin_gpos } 
,	{ ST_LOGIN, "gquit", dbin_gquit } 
,	{ ST_LOGIN, "gdeposit", dbin_gdeposit } 
,	{ ST_LOGIN, "gbonus", dbin_gbonus } 
,	{ ST_LOGIN, "ldeposit", dbin_ldeposit } 
,	{ ST_LOGIN, "deposit", dbin_deposit } 
,	{ ST_LOGIN, "guild", dbin_guild } 
,	{ ST_LOGIN, "glv", dbin_glv } 
,	{ ST_LOGIN, "glevelup", dbin_glevelup } 
,	{ ST_LOGIN, "gsearch", dbin_gsearch } 

,	{ ST_LOGIN, "ladder", dbin_get_ladder }

,	{ ST_LOGIN, "lreplay", dbin_list_replay }
,	{ ST_LOGIN, "replay", dbin_load_replay }

,	{ ST_LOGIN, "sprofile", dbin_update_profile }

,	{ ST_LOGIN, "fadd", dbin_friend_add }
,	{ ST_LOGIN, "flist", dbin_friend_list }
,	{ ST_LOGIN, "fsta", dbin_friend_sta }
,	{ ST_LOGIN, "fsearch", dbin_friend_search }
,	{ ST_LOGIN, "fdel", dbin_friend_del }

,	{ ST_LOGIN, "lpiece", dbin_load_piece }
,	{ ST_LOGIN, "ppiece", dbin_pick_piece }
,	{ ST_LOGIN, "mpiece", dbin_merge_piece }

,	{ ST_LOGIN, "cpiece", load_card_piece }
,	{ ST_LOGIN, "piece_chapter", get_piece_chapter }


,	{ ST_LOGIN, "course", dbin_get_course }
,	{ ST_LOGIN, "scourse", dbin_save_course }

,	{ ST_LOGIN, "challenge", dbin_challenge }
///////////////////////////////////
,   { ST_NULL, "lmerge", load_merge }
,	{ ST_NULL, "lconstant", cmd_list_constant }
,   { ST_LOGIN, "shop", load_shop }
,	{ ST_NULL, "close", 	sys_close }
,	{ ST_ADMIN, "info", sys_info }
,	{ ST_NULL, "ver", sys_ver }
,	{ ST_NULL, "getsite", sys_website }
,	{ ST_NULL, "get_site", sys_website }
//,	{ ST_NULL, "replay", sys_load_replay}
// TODO move these to ST_ADMIN, add a command "admin [password]" for that level
,	{ ST_NULL, "@admin", admin_login }
,	{ ST_ADMIN, "test", admin_test }
,	{ ST_ADMIN, "@flush", admin_flush }
,	{ ST_ADMIN, "@online", admin_online }
,	{ ST_ADMIN, "@list_quick", admin_list_quick }
,	{ ST_ADMIN, "kill", admin_kill }
,	{ ST_ADMIN, "pgame", admin_pgame }

,	{ ST_ADMIN, "@login", admin_check_login }

,	{ ST_ADMIN, "@match_add", admin_add_match }
,	{ ST_ADMIN, "@match_delete", admin_delete_match }
,	{ ST_ADMIN, "@match_init", admin_match_init }
//,	{ ST_ADMIN, "@round_start", admin_round_start }
//,	{ ST_ADMIN, "@round_end", admin_round_end }

,	{ ST_ADMIN, "@round", admin_match_round }

,	{ ST_ADMIN, "@lquick", admin_lquick }
,	{ ST_ADMIN, "@cccard", dbin_cccard }
,	{ ST_ADMIN, "@dbstress", admin_dbstress }
,	{ ST_ADMIN, "@fatal", admin_fatal }
,	{ ST_ADMIN, "@ckroom", admin_check_room }
,	{ ST_ADMIN, "@ckdb", admin_check_db }
,	{ ST_ADMIN, "@ckconn", admin_check_conn }
,	{ ST_ADMIN, "@kconn", admin_kill_conn }
,	{ ST_ADMIN, "@scard", admin_save_card }
,	{ ST_ADMIN, "@addrobot", admin_add_robot }
,	{ ST_ADMIN, "@dbrestart", admin_dbrestart }
,	{ ST_ADMIN, "@reload", admin_reload }
,	{ ST_ADMIN, "@xcreset", admin_xcreset } 
,	{ ST_ADMIN, "@ladder", admin_create_ladder } 
,	{ ST_ADMIN, "@lconstant", admin_lconstant } 
,	{ ST_ADMIN, "@pay", admin_pay }
,	{ ST_ADMIN, "@autofold", admin_autofold }
,	{ ST_ADMIN, "@win", admin_win }
,	{ ST_ADMIN, "@ranking", admin_init_ranking } 
,	{ ST_ADMIN, "@reset_ranktime", admin_reset_ranking_time } 
,	{ ST_ADMIN, "@reset_arenatimes", admin_reset_arena_times } 
,	{ ST_ADMIN, "@rank_reward", admin_rank_reward } 
,	{ ST_ADMIN, "@arena", admin_init_arena } 
,	{ ST_ADMIN, "@tower_reward", admin_tower_reward } 
,	{ ST_ADMIN, "@send_message", admin_send_message } 
,	{ ST_ADMIN, "@chat", admin_chat }
////////////////////////////////////////
,	{ ST_LOGIN, "room", cmd_room }  
,	{ ST_LOGIN, "sta", sys_status } 	 
// ,	{ ST_LOGIN, "room", room_create } 
,	{ ST_LOGIN, "join", room_join }
,	{ ST_LOGIN, "lroom", room_list }
,	{ ST_LOGIN, "lchan", channel_list }
,	{ ST_LOGIN, "wchat", cmd_wchat }	// world chat
,	{ ST_LOGIN, "gchat", guild_chat }  	// guild chat
,	{ ST_LOGIN, "lai", cmd_list_ai }
,	{ ST_LOGIN, "ljob", cmd_list_job }
,	{ ST_LOGIN, "fchat", friend_chat }
,	{ ST_LOGIN, "lpay", pay_list }
,	{ ST_LOGIN, "mis", cmd_mission }		// for debug, will be mlist
,	{ ST_LOGIN, "lmis", dbin_load_mission }  // for debug will be in login
,	{ ST_LOGIN, "smis", dbin_save_mission }	// for debug
,	{ ST_LOGIN, "mlist", cmd_mlist }	// not work
,	{ ST_LOGIN, "mreward", dbin_mission_reward }	
,	{ ST_LOGIN, "lslot", dbin_load_slot }
,	{ ST_LOGIN, "slotlist", dbin_slot_list }
,	{ ST_LOGIN, "sslot", dbin_save_slot }
,	{ ST_LOGIN, "rslot", dbin_rename_slot }
,	{ ST_LOGIN, "bslot", dbin_buy_slot }

,	{ ST_LOGIN, "lmatch", list_match }
,	{ ST_LOGIN, "player_data", get_match_player_data }
,	{ ST_NULL, "match_data", get_match_data }
// ,	{ ST_LOGIN, "gstart", match_game_start } 
,	{ ST_LOGIN, "match_apply", dbin_match_apply }
,	{ ST_LOGIN, "match_cancel", dbin_match_cancel }

,	{ ST_LOGIN, "list_message", dbin_list_message }
,	{ ST_LOGIN, "read_message", dbin_read_message }

//	solo
,	{ ST_ROOM, "kick", room_kick }
,	{ ST_ROOM, "lguest", guest_list}	// is it still useful??? use room ?
,   { ST_ROOM, "ginfo", game_info}  // load game info for re-conn
,	{ ST_ROOM, "leave", room_leave }  // TODO check game started, observe leave
,	{ ST_LOGIN, "rchat", room_chat }  // ST_ROOM -> ST_LOGIN (check inside)
// ,	{ ST_ROOM, "game", room_game } // start game (was start)
,	{ ST_ROOM, "greconn", game_reconn } 

////////////////////////////////////////
,	{ ST_LOGIN, "rlist", ranking_list }
,	{ ST_LOGIN, "rtarlist", ranking_targets }
,	{ ST_LOGIN, "rgame", dbin_ranking_game }
,	{ ST_LOGIN, "rresp", dbin_resp_ranking_game }
,	{ ST_LOGIN, "rcancel", cancel_ranking_game }
,	{ ST_LOGIN, "ranklog", get_ranking_history }

////////////////////////////////////////

,	{ ST_LOGIN, "gift", exchange_gift }

////////////////////////////////////////

,	{ ST_LOGIN, "fdata", get_fight_data }
,	{ ST_ADMIN, "@reset_fighttimes", reset_fight_times }
,	{ ST_LOGIN, "fight", dbin_fight }
,	{ ST_LOGIN, "fcancel", fight_cancel }

////////////////////////////////////////

,	{ ST_LOGIN, "hero_mlist", get_hero_mission_list }
,	{ ST_LOGIN, "lhero", get_hero_data_list }
,	{ ST_LOGIN, "shero_mis", submit_hero_mission }


////////////////////////////////////////
,   { ST_LOGIN, "load_hero_deck", dbin_load_hero_deck }
,   { ST_LOGIN, "list_hero_slot", dbin_list_hero_slot }
,   { ST_LOGIN, "get_hero_slot", dbin_get_hero_slot }
,   { ST_LOGIN, "insert_hero_slot", dbin_insert_hero_slot }
,   { ST_LOGIN, "update_hero_slot", dbin_update_hero_slot }
,   { ST_LOGIN, "choose_hero_slot", dbin_choose_hero_slot }

////////////////////////////////////////

,	{ ST_LOGIN, "daily_log", get_daily_login }
,	{ ST_LOGIN, "daily_reward", get_daily_reward }
////////////////////////////////////////
// arena logic
,	{ ST_LOGIN, "arenatop", dbin_arena_top_list }
,	{ ST_LOGIN, "arenatarget", dbin_arena_target }
,	{ ST_LOGIN, "arenatimes", dbin_arena_times }
,	{ ST_LOGIN, "arenagame", dbin_arena_game }
,	{ ST_LOGIN, "arenareward", dbin_get_arena_reward }
////////////////////////////////////////
,	{ ST_LOGIN, "moneyexchange", dbin_money_exchange }
////////////////////////////////////////

// dbout for testing
,	{ ST_DB, 	"dbout", dbout }	// dbout [dbtype] [out_buffer]
,	{ ST_DB, 	"dbin", dbin }	// dbin [dbtype] [out_buffer]
// TODO surrender / leave ?  guest[0],[1] need surrender, observer:leave
,	{ ST_ADMIN, "kickall", sys_kickall }   // TODO use admin_
,	{ ST_ADMIN, "quitall", sys_quitall } 	// TODO use admin_
,	{ ST_NULL, "q", 	sys_close }  // must be after quitall

// change to "t", "b", "s", "n"
,	{ ST_GAME, "fold", game_fold }  // fold=surrender
,	{ ST_GAME, "play_list", get_play_list }
,	{ ST_GAME, "t", play_cmd }  // put it at last, short cmd
,	{ ST_GAME, "b", play_cmd }
,	{ ST_GAME, "s", play_cmd }
,	{ ST_GAME, "n", play_cmd } 
,	{ ST_GAME, "f", play_cmd } 
};
const int TOTAL_COMMAND = sizeof(command_list) / sizeof(command_t);


int process_command(connect_t* conn, const char *buffer) 
{
	int n;
	int ret;
	char cmd[21];
	const char *ptr ;
	// potential issue:  this will be same as [log n p]
	// login678901234567890n p
	// log n p -> cmd="log"  n=4
	// logn p -> cmd="logn" n=5
	// DEBUG_PRINT(0, "---- process_command: %s", buffer);
	ret = sscanf(buffer, "%20s %n", cmd, &n);
	if (ret <= 0) {
		// net_writeln(conn, "unknown -11 ERROR unknown command");
		return 0;
	}
	ptr = buffer + n; // ptr is the rest of parameters after cmd

	if (conn->db_flag != 0) {
		WARN_PRINT(-1, "db_flag=%ld, conn.id=%d, conn.state=%d", conn->db_flag, get_conn_id(conn), conn->state);
	}

	for (int i=0; i<TOTAL_COMMAND; i++) {
		// peter: change to strcasestr to ignore case (Solo=solo)
		if (cmd != strcasestr(cmd, command_list[i].name)) {
			continue; // early continue
		}

		if (conn->st < command_list[i].st) {
			net_writeln(conn, "%s -8 not_enough_st %d < %d"
			, command_list[i].name, conn->st, command_list[i].st);
			return 0;
		}


		// ST_NULL will not print command
//		if (command_list[i].st != ST_GAME && command_list[i].st > ST_NULL) {
//			net_write_space(conn, command_list[i].name);
//		}

//		if (command_list[i].st == ST_GAME) {
			// e.g. buffer = "at 1201 2301"   ptr = "1201 2301"
			// for ST_GAME, we need the full buffer command, include "at"
//			ptr = buffer;  // reset ptr to include [cmd]
//		}
		////
		INFO_PRINT(0, "process_command:cmd=%s fd=%d", cmd, conn->conn_fd);

		int new_state;
		new_state = command_list[i].fun(conn, command_list[i].name, ptr);
		if (new_state > conn->st) {
			conn->st = new_state;
		}

		return new_state;  // early exit
	}

	net_writeln(conn, "unknown -1 ERROR unknown command");
	return 0;
}


int do_wchat_send()
{
	connect_t * conn;
	int tail_ts = wchat_tail_ts();
	int head_ts = wchat_head_ts();
	for (int i=0; i<MAX_CONNECT; i++) {
		conn = g_connect_list + i;
		// skip free or not-login
		if (conn->state==STATE_FREE || conn->euser.eid<=0) {
			continue;
		}
		if (conn->wchat_ts < head_ts) {
			conn->wchat_ts = tail_ts;  // skip, or make it head_ts ?
		}
		if (conn->wchat_ts >= tail_ts || conn->state==STATE_SENDING) {
			continue; // already sending, no need to change state
		}
		// implicit: conn ts < tail_ts && conn->state==STATE_READING
		// state change even there is no pending write, need to send wchat
		conn->state = STATE_SENDING;
		fdwatch_del_fd( conn->conn_fd );
		fdwatch_add_fd( conn->conn_fd, conn, FDW_WRITE );  // only write 
	}
	return 0;
}

#define RESET_BUFFER(conn)	do {conn->offset = 0; conn->outlen = 0; conn->buffer[0] = 0; } while (0);

#define WRITE_BUFFER(conn)  write(conn->conn_fd, conn->buffer + conn->offset, conn->outlen - conn->offset)

int do_send(connect_t* conn)
{
	int size = 0;
	int len = conn->outlen - conn->offset;
	// const char * ptr = conn->buffer + conn->offset;
	// global chat priority is always the lowest, when we have cmd, we send cmd 
	// first, len=0 means no more pending cmd to send
	if (len == 0) {
		if (conn->wchat_ts >= wchat_tail_ts()) {
			// DEBUG_PRINT(0, "do_send: no more chat, resume FDW_READ cid %d wchat_tail %d  wchat_ts %d", get_conn_id(conn), wchat_tail_ts(), conn->wchat_ts);
			RESET_BUFFER(conn);
			conn->state = STATE_READING;  // resume read cycle
			fdwatch_del_fd( conn->conn_fd );
			fdwatch_add_fd( conn->conn_fd, conn, FDW_READ );
			return 0;
		}

		// now we do wchat
		// for fast calculation:
		// typedef struct msg_struct { const char *msg;  int len; } MSG_T;
		if (conn->wchat_ts < wchat_head_ts()) {
			conn->wchat_ts = wchat_tail_ts();  // busy server, use tail
			return 0; // early exit, because tail_ts render no msg
		}
		const char * msg = wchat_get(conn->wchat_ts);
		if (msg == NULL) {
			BUG_RETURN(-7, "do_send:wchat_get NULL");
		}
		conn->wchat_ts ++;
		int msglen = strlen(msg);
		size = write(conn->conn_fd, msg, msglen); // XXX dangerous 
		if (size < 0) {
			// @see thttpd.c:handle_send
			if (errno==EINTR || errno==EWOULDBLOCK || errno==EAGAIN) {
				errno = 0;
				return 0 ; // no need to report these error
			}
			ERROR_NEG_RETURN(size, "do_send:wchat msg %d", msglen);
		}
		// XXX try this in real client
		write(conn->conn_fd, "\n", 1);

		return size;
	}
	BUG_NEG_PRINT(len, "do_send:negative_len");

	// size = write(conn_fd, buffer + offset, outlen - offset);
	// assert(conn->outlen - conn->offset > 0);
	// INFO_PRINT(0, "do_send:write=[%s]", conn->buffer + conn->offset);
	size = WRITE_BUFFER(conn);
	// DEBUG_PRINT(0, "conn outlen=%d, offset=%d, size=%d", conn->outlen, conn->offset, size);
	// assert(size > 0); // user may offline
	BUG_NEG_PRINT(size, "do_send:write_size_negative conn_id = %d", get_conn_id(conn));

	if (size < 0) {
		// @see thttpd.c:handle_send
		if (errno==EINTR || errno==EWOULDBLOCK || errno==EAGAIN) {
			errno = 0; // reset this
			return 0 ; // no need to report these error
		}
		ERROR_PRINT(size, "do_send:return");
		return size;
	}

	// DEBUG_PRINT(size, "do_send:size");
	conn->offset += size; // shift buffer offset

	// reset the offset when all write is done
	if (conn->offset >= conn->outlen) {
		if (conn->offset > conn->outlen) {
			// peter: what should we do?
			BUG_PRINT(conn->outlen - conn->offset, "offset>outlen");
		}
		RESET_BUFFER(conn);
		
		// peter: we will do another logic to cope with wchat
		// after the conn is all written, we write the wchat message

		// early exit if we still have chat msg to send
		if (conn->wchat_ts < wchat_tail_ts()) {
			return size;
		}

		// implicit: we do not have chat msg
		conn->state = STATE_READING;  // resume read cycle
		fdwatch_del_fd( conn->conn_fd );
		fdwatch_add_fd( conn->conn_fd, conn, FDW_READ );
		// DEBUG_PRINT(0, "do_send:done");
	}

	// FATAL_NEG_EXIT(-size, "DEBUG STOP");
	return size;
}



/**
 * XXX this has an issue:  if we disconnect, the pending "net_write" will be
 * discarded, so we should find a way to flush the write buffer before a real 
 * disconnect.
 */
void do_disconnect( connect_t* conn )
{
	INFO_PRINT(0, "do_disconnect");
	// core 2.  disconnect 
	// DEBUG_PRINT(0, "del_fd=%d", conn->conn_fd);
	fdwatch_del_fd( conn->conn_fd );
	close(conn->conn_fd); 


	free_conn(conn);
	/**
	// init. connect_t
	// order is important: clean conn->room first!
	bzero(conn, sizeof(connect_t)); // order is important
	//conn->id = conn - g_connect_list; // safe!
	conn->state = STATE_FREE; // actually not necessary, STATE_FREE=0
	conn->next_free = g_free_connect;  // order: after bzero
	// conn - g_connect_list = index , where g_connect_list[index] == conn
	g_free_connect = conn - g_connect_list;  // pointer arithmetics
	--g_num_connect;
	**/
}


// clean connection without disconnect
// logic clean
void do_clean(connect_t *conn)
{
	// peter: try to flush the last message to client if possible
//	do_send( conn );  // this is dangerous!

	// core 1. game logic @see relogin logic
	// clean eid in quick_list
	quick_del(conn->euser.eid);
	fight_del(&g_fight_gold_list, conn->euser.eid);
	fight_del(&g_fight_crystal_list, conn->euser.eid);
	fight_del(&g_fight_free_list, conn->euser.eid);
	// clean guild   guild-patch
	guild_clean( conn );  // this may use conn->euser.eid
	// clean up room
	room_clean( conn );  // this may use conn->euser.eid
	// XXX non_thread_safe @see dbproxy_login
	g_user_index.erase(conn->euser.eid); // last:clean up eid->connect_t.id map
	conn->euser.eid = 0;
}

void do_clean_disconnect( connect_t* conn ) 
{
	INFO_PRINT(0, "do_clean_disconnect");
	// core logic!
	// 1. clean up game logic related ( eid -> connect_t.id mapping)
	//    connect_t.room if all guest disconnect ?
	// 2. clean up connection related  : fdwatch, close(sock)
	// ORDER is important
	do_clean(conn);  // logic clean

	// core 2. clean up
	do_disconnect( conn );
	return ;
}

// finish call clear, 
// in some case (usually error) clear can be called individually
int do_finish( connect_t* conn )
{
	// TODO do lingering, we may do lingering close in batch
	// 
	// if ( conn->state == STATE_LINGERING ) {
	//  	conn->should_linger = 0;
	// }
	// if ( conn->should_linger )  {   conn->state = LINGERING... }
	// else do_real_close()

	// if conn->state != STATE_PAUSING) 

	do_clean_disconnect( conn );  // really_clear_connection
	return 0;
}


// return 0 for success
// return -ve for error (read_buffer may be partially appended)
// return len of append string, event for error return -len
// store the remaining string tok to read_buffer, up to size
int store_read_buffer(char *read_buffer, int *poffset, char *tok, const char *sep, char * context)
{
	int len;
	int tok_len;
	len = 0;
	read_buffer[0] = '\0'; // ok

	if (tok == NULL) {
		BUG_PRINT(ERR_NULL, "store_read_buffer:tok_null");
		return ERR_NULL;
	}

	// first tok is always valid and need to store
	do {
		tok_len = strlen(tok);
		if (tok_len + len + 2 >= READ_BUFFER_SIZE) {  // + '\n' + '\0'
			BUG_PRINT(ERR_OUTBOUND, "read_buffer_overflow");
			return ERR_OUTBOUND; // out bound = -2;
		}
		strcat(read_buffer, tok);
		strcat(read_buffer, "\n");
		len += tok_len + 1;
		*poffset += tok_len + 1; // same as len
	}
	while ((tok = strtok_r(NULL, sep, &context)) != NULL) ;
	// WARN_PRINT(-1, "store_read_buffer: [%s]", read_buffer);
	return 0;
}


int do_batch_command(connect_t *conn, char *buffer) {
	char * tok = NULL;

	// WARN_PRINT(-1, "do_batch_command: [%s]", buffer);

	// in case we have more than 1 command-line from the buffer, we need
	// to separate it into several commands
	// buffer = "room\nstatus\nleave"  strtok(buffer, "\n") -> "room"
	// strtok(NULL, "\n")->"status" ...
	const char * sep = "\n";
	char * context;
	for ( tok = strtok_r(buffer, sep, &context);  // loop init
	tok != NULL;	// loop check
	tok = strtok_r(NULL, sep, &context)) { // loop ++
		int ret;
		// DEBUG_PRINT(0, "TOK[%s]  db_flag=%d", tok, conn->db_flag);
		// check db_flag before process command

		// reset db_flag = 0 for long db connection (5 seconds is very long)
		if (conn->db_flag != 0 && (time(NULL) - conn->db_flag) > 5) {
			BUG_PRINT(-55, "reset jammed db_flag cid=%d now=%ld db_flag=%ld"
			, get_conn_id(conn), time(NULL), conn->db_flag);
			conn->db_flag = 0;
		}
		if (conn->db_flag != 0) {
			ret = store_read_buffer(conn->read_buffer + conn->read_offset
			, &(conn->read_offset), tok, sep, context);
			if (ret < 0) {
				BUG_PRINT(-52, "do_batch_cmd:read_buffer_overflow %d", ret);
				net_writeln(conn, "gerr -52 read_buffer_overflow");
			}
			// WARN_PRINT(1, "cid=%d db_flag=1 store_read_buffer=%s", get_conn_id(conn), conn->read_buffer);
			break;
		}

		if (tok[0]=='\0' || tok[0]=='\n') continue; // skip

		ret = process_command(conn, tok);  // temp
		if (ret == LEVEL_CLOSE) {
			DEBUG_PRINT(0, "PROCESS_COMMAND ret CLOSE");
			break; 	// close no need to handle the rest of command
		}
		if (ret == LEVEL_QUITALL) {
			DEBUG_PRINT(0, "PROCESS_COMMAND ret QUITALL");
			return ret;
		}

	}

	return 0;
}

#ifdef HCARD

/*
input:
GET /ws/ws HTTP/1.1
Upgrade: websocket
Connection: Upgrade
Host: 127.0.0.1:7710
Origin: file://
Pragma: no-cache
Cache-Control: no-cache
Sec-WebSocket-Key: kkuI1H15j2y+Uuv+JxRo9w==
Sec-WebSocket-Version: 13
Sec-WebSocket-Extensions: x-webkit-deflate-frame
User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_3) AppleWebKit/600.6.3 (KHTML, like Gecko) Version/8.0.6 Safari/600.6.3
*/

int websocket_getkey(char *key, const char * buf)
{
    const char *begin;
    const char *flag = "Sec-WebSocket-Key: ";
    int ret;

    begin = strstr(buf, flag);
    if(NULL==begin) {
        return -3;
    }
    begin += strlen(flag);

    ret = sscanf(begin, "%99s", key);
    if (ret != 1) {
        return -6;
    }
    return 0; // OK
}


// hex to integer?
int htoi(const char s[], int start, int len)
{
    int i,j;
    int n = 0;

    if (s[0] == '0' && (s[1]=='x' || s[1]=='X')) //判断是否有前导0x或者0X
    {
        i = 2;
    }
    else {
        i = 0;
    }

    i+=start;
    j=0;
    for (; (s[i] >= '0' && s[i] <= '9')
       || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >='A' && s[i] <= 'F');++i)
    {
        if(j>=len) {
            break;
        }

        if (tolower(s[i]) > '9') {
            n = 16 * n + (10 + tolower(s[i]) - 'a');
        }
        else {
            n = 16 * n + (tolower(s[i]) - '0');
        }

        j++;
    }
    return n;
}


// input: client_key 
// output: key  (caller allocate 100 or up)
int websocket_computekey(char *key, const char *client_key)
{
    const char * GUID="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char hashkey[200];
    char sha[200];
    char sha_data[400]; // x2 of the sha[] size
    int len;
    int ret;

    if (client_key == NULL) {
        return -3;
    }
    if (key == NULL) {
        return -13;
    }

    // combine client_key and GUID to become hashkey
    sprintf(hashkey, "%s%s", client_key, GUID);

    ret = sha1_hash(sha, hashkey);
    ERROR_NEG_RETURN(ret, "computekey:sha1_hash");

    printf("--- sha = %s\n", sha);
    len = strlen(sha);

    memset(sha_data, 0, 400);
    for (int i=0; i<len; i+=2) {
        sha_data[i/2] = htoi(sha, i, 2);
    }


    len = strlen(sha_data);
    ret = base64_encode(key, sha_data, len);
    ERROR_NEG_RETURN(ret, "computekey:base64");

    return 0;
}


int websocket_sendheader(connect_t *conn, const char *server_key)
{
    char header[500];

    if (conn == NULL) {
        return -3;
    }
    if (server_key == NULL) {
        return -13;
    }

    sprintf(header, "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        // "Sec-WebSocket-Accept: %s\r\n\r\n", server_key
        "Sec-WebSocket-Accept: %s\r\n\r", server_key   // one less \n
    );

    // one more enter
    net_write(conn, header, '\n');  // last char can be \0 for no last char
    return 0;
}

int websocket_read(char *out_buffer, const char *buffer, int size)
{
    if (size < 2) {
        BUG_RETURN(-5, "websocket_read:size<2");
    }

    char fin = (buffer[0] & 0x80) == 0x80;
    if (!fin) {
        ERROR_RETURN(-15, "websocket_read:fin==0x80");
    }

    char mask_flag = (buffer[1] & 0x80) == 0x80;
    if (!mask_flag) {
        BUG_RETURN(-25, "websocket_read:mask_flag==0x80");
    }

    unsigned long data_len = buffer[1] & 0x7F;
    char mask[4];
    if (data_len == 126) {
        memcpy(mask, buffer+4, 4);
        data_len = (buffer[2] & 0xFF) << 8 | (buffer[3] & 0xFF);
        memcpy(out_buffer, buffer+8, data_len);
        out_buffer[data_len] = '\0';
    } else if (data_len == 127) {
        BUG_RETURN(-6, "websocket_read:data_too_large");
    } else {
        // data_len below 125
        memcpy(mask, buffer+2, 4);
        memcpy(out_buffer, buffer+6, data_len);
        out_buffer[data_len] = '\0';
    }

    for (unsigned long i=0; i<data_len; i++) {
        out_buffer[i] = (char)(out_buffer[i] ^ mask[i%4]);
    }

    INFO_PRINT(0, "websocket_read:out_buffer=%s", out_buffer);

    if (data_len >= 1 && out_buffer[0] == 3) {
        ERROR_RETURN(-55, "websocket_read:end_of_text");
    }

    return data_len;
}

#endif

// return 0 for OK, normal
// return -1 for bug
int do_read(connect_t* conn)
{
	int size;
	char buffer[BUFFER_SIZE + 1];

	size = read( conn->conn_fd, buffer, BUFFER_SIZE-1);
	if ( size == 0 ) {  // EOF, client disconnect
		INFO_PRINT(size, "do_read size=0_finish");
		do_finish( conn );
		return 0;
	}
	if (size < 0) {
		// ignore EINTR, EAGAIN.  also ignore EWOULDBLOCK
		// @see thttpd.c:1625
		if ( errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK ) {
			errno = 0;
			return 0;
		}
		DEBUG_PRINT(size, "do_read:error_finish");
		do_finish( conn );
		return 0;
	}


	buffer[size] = '\0'; // null terminate it to become a string

	// XXX debug print, remove later
	// INFO_PRINT(0, "do_read:buffer[%d]=%s", size, buffer);

#ifdef HCARD
	int ret;
    if (conn->websocket_flag == CONN_NONE) {
        char client_key[200];
        char server_key[200];
        ret = websocket_getkey(client_key, buffer);
        if (ret < 0) {
            conn->websocket_flag = CONN_NORMAL;
        } else {
            ret = websocket_computekey(server_key, client_key);
            ERROR_NEG_RETURN(ret, "do_read:websocket_computekey");

            ret = websocket_sendheader(conn, server_key);
            ERROR_NEG_RETURN(ret, "do_read:websocket_sendheader");
            conn->websocket_flag = CONN_WEBSOCKET; // order is important
            return 0; // early exit, no batch command
        }
    }
    if (conn->websocket_flag == CONN_WEBSOCKET) {
        char out_buffer[BUFFER_SIZE + 1];
        ret = websocket_read(out_buffer, buffer, size);
        ERROR_NEG_RETURN(ret, "do_read:websocket_read");
        return do_batch_command(conn, out_buffer);
    }
#endif

//  peter: wasting cpu time for every read, keep for debug
//	char ip[50];  // max 16  255.255.255.255
//	int port;
//	address_readable(ip, &port, &(conn->address));
//      DEBUG_PRINT(0, "READ from %.30s:%d  [%.80s]", ip, port, buffer);
    //  DEBUG_PRINT(0, "READ [%.80s]", buffer);

	return do_batch_command(conn, buffer);
}



// struct addrinfo     include <netdb.h>
void lookup_hostname()
{
	// not yet 
}



/**
 * init the listen file descriptor 
 * do the following:
 * 1. open a socket
 * 2. bind
 * 3. setsockopt
 * 4. fcntl
 * 5. listen
 */
int init_listen(const char *ip, int port)
{
	// @see libhttpd.c:351  listen_fd = initialize_listen_socket( sa4P)
	// httpd_sockaddr* sa4P
	// httpd_initialize(hostname, sa4P, sa6P, port, cgi_pattern, ...)
	// core: listen_fd = socket( saP->sa.sa_family, SOCK_STREAM, 0)
	// core:  fcntl( listen_fd, F_SETFD, 1);
	// setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, 
	// 			(char*)&on, sizeof(on))
	// 
	// core:  bind( listen_fd, &saP->sa, sockaddr_len(saP))
	// fcntl( listen_fd, F_GETFL, 0) // no-delay / non-blocking mode
	// fcntl( listen_fd, F_SET_FL, flag | O_NDELAY)
	// core: listen( listen_fd, LISTEN_BACKLOG) 
	// core: use accept filtering, if available
	// SO_ACCEPTFILTER:  libhttpd.c:444

	int listen_fd = 0;  
	int ret = 0;
	int flag = 0;
	struct sockaddr address;  
	address = prepare_addr( ip, port );  // use: ip

	// sa_family_t sin_family
	listen_fd = socket( address.sa_family, SOCK_STREAM, 0);
	FATAL_NEG_EXIT(listen_fd, "socket");


	ret = fcntl( listen_fd, F_SETFD, 1);  // ??? 
	FATAL_NEG_EXIT(ret, "fcntl F_SETFD");

	int on = 1;
	// make it easy to re-use the same address (avoid stop/start bind issue)
	ret = setsockopt( listen_fd, SOL_SOCKET, SO_REUSEADDR, 
		(const void *)&on, sizeof(on));

	FATAL_NEG_EXIT(ret, "setsockopt");

	ret = bind( listen_fd, &address, sizeof(address));
	FATAL_NEG_EXIT(ret, "socket bind IP (already running)");


	flag = fcntl( listen_fd, F_GETFL, 0); // no-delay / non-blocking mode
	FATAL_NEG_EXIT(flag, "fcntl F_GETFL");
	// flag is from last return value
	ret = fcntl( listen_fd, F_SETFL, flag | O_NDELAY);  
	FATAL_NEG_EXIT(ret, "fcntl F_SETFL NDELAY");

	ret = listen( listen_fd, 1024); // backlog=1024
	FATAL_NEG_EXIT(ret, "listen");

	// printf("GOOD listen_fd setup done\n");

	return listen_fd;
}


int set_nonblock(int fd)
{
	int flag, new_flag;
	flag = fcntl( fd, F_GETFL, 0);
	ERROR_NEG_RETURN(flag, "set_nonblock fnctl"); // if flag < 0 early exit

	new_flag = flag | (int) O_NONBLOCK; // ==O_NDELAY
	if (new_flag != flag) {
		fcntl( fd, F_SETFL, new_flag );
	}
	return 0;
}

int do_linger(connect_t* conn)
{
	ERROR_RETURN(-10, "do_linger");
}



// output: conn->conn_fd
// return fd,  sockaddr_in address(output param)
// since listen_fd is non-blocking, it may return ACCEPT_NO_MORE (normal)
int handle_accept(int listen_fd, struct sockaddr_in *address)
{
	socklen_t address_len = sizeof(struct sockaddr_in);  
	int fd;
	fd = accept( listen_fd, (struct sockaddr*) address, &address_len);
	if (fd < 0) {
		if (errno==EWOULDBLOCK || errno==EAGAIN) {  
			errno = 0;
			return ACCEPT_NO_MORE;  // normal
		}
		ERROR_PRINT( errno, "handle_accept:accept");
		return ACCEPT_FAIL;
	}


	int flag = 1;
	int ret;
	ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
	ERROR_NEG_PRINT(ret, "handle_accept:setsockopt");

#ifdef __linux__
	// keepalive
	flag = 1;
	ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(int));
	ERROR_NEG_PRINT(ret, "handle_accept:setsockopt_keepalive");
	// flag = 8; // max is 8 ?
	flag = 5; // masha update
	ret = setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &flag, sizeof(int));
	ERROR_NEG_PRINT(ret, "handle_accept:setsockopt_keepcnt %d", flag);
	// flag = 3;  // after 3 seconds of idle, start keepalive
	flag = 5;  // masha update: after 60 seconds of idle, start keepalive
	ret = setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &flag, sizeof(int));
	ERROR_NEG_PRINT(ret, "handle_accept:setsockopt_keepidle %d", flag);
	// flag = 3;  // interval is 3 seconds
	flag = 5;  // masha update
	ret = setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &flag, sizeof(int));
	ERROR_NEG_PRINT(ret, "handle_accept:setsockopt_keepintvl %d", flag);
#endif

	fcntl( fd, F_SETFD, FD_CLOEXEC); // 1 == FD_CLOEXEC peter: fork->close
	// non blocking here
	set_nonblock( fd );  // fcntl(fd, F_SETFL, oldflag | O_NONBLOCK)


	return fd; // ACCEPT_OK;  // fd >= 0
}


// return non-zero: continue, do priority
// return 0: fall through to do read/write
int handle_newconnect(int listen_fd)
{
	int ret;
	for (;;) {  // while true
	if (g_num_connect >= MAX_CONNECT) {
		struct sockaddr address;
		socklen_t addrlen = sizeof(address);
		WARN_PRINT(-2, "too many connections");
		ret = accept( listen_fd, &address, &addrlen);
		// TODO shall we send : "sys -2 too many users"  will it be slow?
		printf("TOO many accept ret %d\n", ret);
		if (ret >= 0) {
			const char *errstr = "sys -2 too many users\n";
			write(ret, errstr, strlen(errstr));
			close(ret);
		}
		return 0;  // early exit
	}


	// new way, handle_accept return fd and address, new_conn() take them
	struct sockaddr_in address;
	int fd = handle_accept(listen_fd, &address);
	switch (fd) {
		case ACCEPT_FAIL:
			ERROR_PRINT(-5, "handle_accept FAIL");
			return 0;  // let the main looop fall through
		case ACCEPT_NO_MORE:
			// DEBUG_PRINT(0, "handle_accept NO_MORE normal?");
			return 1;   // keep the main loop retry
		default:
			// fall through
			break;
	}

	connect_t * conn = new_conn(fd, &address);
	// double check, sanity check
	if (conn==NULL) { 
		BUG_PRINT(-77, "handle_newconn null");
		return -77; // this never run!   
	}

	// TODO for busy server, remove these print address
	char ip[50]; // 255.255.255.255:65535  max = 16 include \0
	address_readable(ip, &address);
	DEBUG_PRINT(fd, "ACCEPT cid=%d %s", get_conn_id(conn), ip);

	
	// network related init  (obsolete: remove later)
	// set_nonblock( conn->conn_fd );  // fcntl(fd, F_SETFL, oldflag | O_NDELAY)
	
	conn->state = STATE_READING;
	// 2nd param is the client data which is the conn
	fdwatch_add_fd( conn->conn_fd, conn, FDW_READ );
	}

	return 0;
}


// normal shutdown should call this with code = 0
int app_shutdown(int code) {
	// normal shutdown
	for (int i=0; i<MAX_CONNECT; i++) {
		connect_t* conn = g_connect_list + i;
		if (conn->state == STATE_FREE) {
			continue;
		}
		do_clean_disconnect(conn);
	}


	// clean dbio
	printf("----- cleaning: dbio thread\n");
	for (int i=0; i<MAX_DB_THREAD; i++) {
		void *ptr;
		close(g_dbio_data[i].main_fd);
		pthread_join(g_dbio_thread[i], &ptr);
	}
	// close(master_fd);  // not really needed
	unlink(DBIO_UNIX_DOMAIN);

	printf("--------- nio END shutdown ---------\n");
	INFO_PRINT(code, "malloc=%ld  free=%ld  diff=%ld", g_malloc_count, 
		g_free_count, (g_malloc_count - g_free_count));
	
	
	exit(code);
	return code;
}


static void handle_term(int sig) {
	INFO_PRINT(0, "handle_term: exit by signal %d", sig);
	app_shutdown(0);
}


// @see thttpd.c : 191
static void handle_chld(int sig) {
	printf("INFO handle_chld: got signal %d\n", sig);
	const int oerrno = errno;
	pid_t pid;
	int status;

	(void) signal(SIGCHLD, handle_chld); // ??? 
	// app_shutdown();

	for (;;) {
		pid = waitpid( (pid_t) -1, &status, WNOHANG);
		if ((int)pid == 0)  {
			break;	// none left
		}
		if ((int) pid < 0) {
			if (errno==EINTR || errno==EAGAIN) {
				continue;
			}

			// ECHILD should not happen with WNOHANG option
			// but some kernels does anyway, ignore it
			if (errno != ECHILD) {
				// TODO take log 
				DEBUG_PRINT(errno, "child wait");
				break;
			}

		}
	}
	errno = oerrno;  // restore errno, why ???
}

static void handle_hup(int sig) {
	printf("INFO handle_hup: exit due to signal %d\n", sig);
	const int oerrno = errno;

	(void) signal(SIGHUP, handle_hup); // setup handler again

	// re-open log file ?
	// what should we do here? 
	// got_hup = 1; 


	errno = oerrno;
}

static void handle_usr1(int sig) {
	printf("INFO handle_user sig: %d\n", sig);
	app_shutdown(0); // TODO do close(conn->conn_fd) unlisten(listen_fd)
// for i=0 to max_connect
//		conn = g_connect_list[i]
//      if conn->conn_fd > 0 then
//			close(conn->conn_fd);
//		endif
/** @see thttpd.c : 825
        if ( hs != (httpd_server*) 0 )
 826         {
 827         if ( hs->listen4_fd != -1 )
 828             fdwatch_del_fd( hs->listen4_fd );
 829         if ( hs->listen6_fd != -1 )
 830             fdwatch_del_fd( hs->listen6_fd );
 831         httpd_unlisten( hs );
			...}
**/
	
}


// TODO handle statistics 
static void handle_usr2(int sig)
{
	const int oerrno = errno;
	signal(SIGUSR2, handle_usr2);

	printf("INFO handle_usr2 TODO handle statistics\n");
	errno = oerrno;
}


// alarm is used as watchdog ping
static void handle_alrm(int sig)
{
	// const int oerrno = errno;
	printf("INFO handle_alrm: g_malloc %zu  g_free %zu  diff %zu\n",
		g_malloc_count, g_free_count, g_malloc_count - g_free_count);


	signal(SIGALRM, handle_alrm);
	alarm(WATCH_TIMEOUT * 2 / 1000) ;
	// errno = oerrno;
}


// SIGSEGV, SIGBUS, SIGILL etc
static void handle_fatal(int sig)
{
	BUG_PRINT(sig, "signal_fatal");
	
	int total_game = 0;
	for (int c=0; c<MAX_CHANNEL; c++) {
		for (int r=1; r<=MAX_ROOM; r++) {
			room_t *proom = &(g_room_list[c][r]);
			if (proom==NULL) { continue; }
			if (proom->state==0) {continue; }

			printf("fat__room(%d,%d): num_guest=%d title=%s lua=%s gameid=%ld seed=%d create_time=%ld\n"
			, c, r, proom->num_guest, proom->title
			, (proom->lua==NULL) ? "null" : "yes", proom->gameid, proom->seed, proom->create_time);

			if (proom->lua != NULL) { total_game++; }
		}
	}

	printf("---- fat__total_game %d -----\n", total_game);

	// print the cmd_list:
	for (int c=0; c<MAX_CHANNEL; c++) {
		for (int r=1; r<=MAX_ROOM; r++) {
			room_t *proom = &(g_room_list[c][r]);
			if (proom==NULL) { continue; }
			if (proom->state==0) {continue; }
			if (proom->lua==NULL) {continue;}

			printf("fat__room(%d,%d): title=%s eid1=%d eid2=%d "
			"gameid=%ld seed=%d create_time=%ld  cmd_list.size=%zu\n"
			, c, r, proom->title, proom->guest[0], proom->guest[1]
			, proom->gameid, proom->seed, proom->create_time, proom->cmd_list.size());

			for (int i=0; i<(int)proom->cmd_list.size(); i++) {
				printf("__fat__cmd[%d] = %s\n", i, proom->cmd_list[i].c_str());
				
			}

		}
	}

	INFO_PRINT(0, "handle_fatal shutdown with signal %d", sig);
	app_shutdown(-1);
}



//////////////////////////////////////////////////
//////////////////////////////////////////////////

// @see thttpd.c:main()
int main(int argc, char *argv[]) {
	char *ptr;
	int ret;
	int server_port = SERVER_PORT;
	// g_locale = newlocale(LC_ALL_MASK, "zh_CN.UTF-8", NULL); // init g_loc
	// setlocale(LC_ALL, "zh_CN.UTF-8"); // set default system locale

	if (argc <= 1) {
		ret = daemon(1, 1); // 1=nochdir(no cd /), 1=noclose(no close stdin/out)
		FATAL_EXIT(ret, "daemon");
	}
	if (argc > 1) {
		int port = atoi(argv[1]);
		FATAL_EXIT(port<=0, "port <= 0");
		if (port > 1024) {
			server_port = port;
		}
	}

	
	// TODO open this later (we just hard code now!)
	// g_callback_read = game_callback_read;


	// related to g_room_list[][], g_used_room[], g_free_room[]
	init_room_list();

	// clean g_ladder_list
	bzero(g_ladder_rating_list, sizeof(g_ladder_rating_list));
	bzero(g_ladder_level_list, sizeof(g_ladder_level_list));
	bzero(g_ladder_guild_list, sizeof(g_ladder_guild_list));
	bzero(g_ladder_collection_list, sizeof(g_ladder_collection_list));
	bzero(g_ladder_gold_list, sizeof(g_ladder_gold_list));

	// clean name list
	bzero(&g_name_xing, sizeof(g_name_xing));
	bzero(&g_name_boy, sizeof(g_name_boy));
	bzero(&g_name_girl, sizeof(g_name_girl));
	// load name in name_xxx.txt
	// ret = load_local_name(g_name_xing, "name_xxx.txt");
	ret = load_local_name(g_name_xing, "res/txt/name_xing.txt");
	FATAL_EXIT(ret, "load_local_name_xing_error");
	ret = load_local_name(g_name_boy, "res/txt/name_boy.txt");
	FATAL_EXIT(ret, "load_local_name_boy_error");
	ret = load_local_name(g_name_girl, "res/txt/name_girl.txt");
	FATAL_EXIT(ret, "load_local_name_girl_error");


	// clean g_match_list and g_match_player_list
	bzero(g_match_list, sizeof(g_match_list));
	// bzero(g_match_player_list, sizeof(g_match_player_list));

	
	// get logic.lua version
	lua_State * lua;
	lua = luaL_newstate();
	assert(lua != NULL);
	luaL_openlibs(lua);
	lu_set_int(lua, "g_ui", 1);  // non gui
	ret = luaL_dofile(lua, "res/lua/logic.lua");
	LOGIC_VER= lu_get_int(lua, "LOGIC_VERSION");
	// LOGIC_VER=2141; // XXX for client v1.2
	lua_close(lua);  // clean up lua VM
	// DEBUG_PRINT(0, "LOGIC_VER = %d", LOGIC_VER);


	

	// RLIM_INFINITY, which resolve to 8192 hard coded
	ret = fdwatch_get_nfiles();  // also init fdwatch() important!!
	FATAL_NEG_EXIT(ret - MAX_CONNECT, "MAX_CONNECT %d over fdwatch_nfiles %d"
		, MAX_CONNECT, ret);
	INFO_PRINT(0, "nio START pid=%d fdwatch=[%s] max_nfiles=%d max_connect=%d  timeout=%d(ms)"
		, getpid(), fdwatch_which(), ret, MAX_CONNECT, WATCH_TIMEOUT);

	// init the g_connect_list[], g_free_connect
	init_conn();

	// init signal
	signal(SIGTERM, handle_term); // 15 is normal
	signal(SIGINT, handle_term); //  2 interrupt  is normal?
	signal(SIGCHLD, handle_chld);	// 20
	signal(SIGPIPE, SIG_IGN); // get EPIPE instead
	signal(SIGHUP, handle_hup);
	signal(SIGUSR1, handle_usr1);
	signal(SIGUSR2, handle_usr2);
	signal(SIGALRM, handle_alrm);

	// do not catch 6=ABRT (control-c)
	// fatal signal handler : 8=FPE, 10=BUS, 11=SEGV, 12=SYS, 4=ILL
	// gdb will do full path, usually run by hand will be ./nio
	ptr = strstr(argv[0], "nio");
	if (ptr != NULL && (ptr - argv[0]) < 3) {
		// INFO_PRINT(0, "non-gdb:turn on sig handle_fatal for SEGV BUS SYS FPE ILL");
		signal(SIGSEGV, handle_fatal);  // 11 segment, turn off if run in gdb
		signal(SIGBUS, handle_fatal); // 10
		signal(SIGSYS, handle_fatal); // 12
		signal(SIGFPE, handle_fatal); // 8 FPE (e.g. division by zero?)
		signal(SIGILL, handle_fatal); // ILL = 4
	}

	// do not catch the others, usually default is IGN

	// keep this: maybe useful
	// alarm(WATCH_TIMEOUT * 2 / 1000) ;  @see setitimer, interrupt fdwatch() -1


	// masha edit for load design database
	srandom(time(NULL)); // random seed
	// use fatal_exit
	g_design = (design_t*)malloc(sizeof(design_t));
	ret = load_design(1, g_design, NULL);
	FATAL_EXIT(ret, "load_design_error");


	////////// DBIO INIT //////////
	// TODO shutdown clean up
	ret = dbio_init(g_dbio_thread);
	FATAL_EXIT(ret, "dbio_init");


	////////// init network socket /////////
	// core logic for init the listen fd
	int listen_fd = init_listen(SERVER_IP, server_port);
	INFO_PRINT(0, "server_ip:port=%.80s:%d listen_fd=%d", SERVER_IP, server_port, listen_fd);
	connect_t conn_listen;
	bzero(&conn_listen, sizeof(connect_t));
	conn_listen.state = STATE_LISTEN;
	fdwatch_add_fd(listen_fd, (void*) &conn_listen, FDW_READ); 


	int running = 1;
	int num_ready = 0;
	int busy_count = 0;
	connect_t* conn;
	struct timeval tv;  // seems useless now
	(void)gettimeofday( &tv, (struct timezone*) 0); // no check return value

	// int handle_db_num = 0; // for handle_db

	//int count = 0;
	while (running) {
		num_ready = fdwatch(WATCH_TIMEOUT);

		// FATAL_NEG_EXIT(num_ready, "fdwatch");  // it may return EAGAIN
		ERROR_NEG_PRINT(num_ready, "mainloop:num_ready");

		// temp comment auto_quick_match() for debug
		auto_quick_match(time(NULL)); //  now = time(NULL)

		auto_fight(time(NULL)); //  now = time(NULL)

		auto_ranking_challenge_resp(time(NULL));

		// delete overtime challenge
		auto_del_challenge(time(NULL));

		// force fold offline player
		auto_fold(time(NULL));

		// auto do match round
		auto_match_round(time(NULL));

		// threshold of busy_count should be larger if the server is too busy
		if (num_ready==0 || busy_count>50) {  // consider: num_ready<=0
			//WARN_PRINT(busy_count>=50, "main:busy_count=%d", busy_count);
			busy_count = 0;
			do_wchat_send();
			// continue; // must, why?  even we set FDW_WRITE, we may out of buffer
		}
		busy_count ++;
		// TODO handle ret (number of available fd)

		// 1st priority to handle listen!
		// listen_fd is in the watch list, so need to handle new conn
		if (0 != fdwatch_check_fd( listen_fd )) {
			if ( 0 != (ret=handle_newconnect(listen_fd)) ) {
				// for return != 0
				// DEBUG_PRINT(ret, "handle_newconnect:continue");
				continue;
			}
			// for return 0, fall through down
			DEBUG_PRINT(ret, "handle_newconnect:fall");
		}


		// dbio_check
		for (int i=0; i<MAX_DB_THREAD; i++) {
			char *ptr;
			unsigned char num[2];
			int index;
			dbio_init_t *dbio_data = g_dbio_data + i;
			if (0 == fdwatch_check_fd( dbio_data->main_fd )) {
				continue; // skip
			}

			ret = read(dbio_data->main_fd, num, 2);
			// fatal, but can be non-fatal later
			FATAL_EXIT(ret != 2, "main:dbio_read_2 %d", ret);
			index = char_to_short(num);
			if (index < 0 || index >= DB_TRANS_MAX) {
				FATAL_EXIT(-2, "main:dbio_buffer_index %d (%d,%d)"
				, index, num[0], num[1]);
			}

			ptr = dbio_data->db_buffer[index];
			// DEBUG_PRINT(0, "main:ptr=%s", ptr);

			int cid = -1, n = 0;
			sscanf(ptr, "%d %n", &cid, &n);

			// NOTE: cid = -3, may comes from auto_fold and two players are offline. 
			if (cid < 0 && cid != -3) {
				// if both player offline in win_game, cid == -3
				ERROR_PRINT(cid!=-3, "main_dbio:sscanf cid<0 buff=%s"
				, ptr);
				ptr[0] = 0; // FIX 2014-10-14 21:30 server down
				continue; 
			}

			// NOTE: in match game, 2player offline and auto fold, if they have game in this round, win_game will call db_write(DB_GAME) to start another game, even though two player still offline. in this case, let it do dbout to start a new game;
			// XXX the dbout functions should check if conn is null

			connect_t *conn = get_conn(cid); // conn may null
			ret = dbout(conn, "gerr", ptr+n);
			/////// NOTE: if any code above this has a continue, must
			/////// do a ptr[0] = 0;
			ptr[0] = 0;  // core logic, clear buffer for next use
			if (ret==LEVEL_QUITALL) {
				running = 0; // seems useless ?
				break;  // this is already out of running loop
			}
		} // end for i=0 to MAX_DB_THREAD (dbio_check)


		// TODO : need to read the source in fdwatch_get_next_client_data()
		// implicit: non-listen_fd
		while ((conn=(connect_t*)fdwatch_get_next_client_data()) 
		!= (connect_t*)-1) {
			static int error_count = 0;
			if ( conn == (connect_t*)0 ) {
				// printf("--- conn = 0 is it normal ?\n");
				// XXX client: "q" will trigger this ? 2 times!
				error_count ++;
				if (error_count > 50 && error_count < 600) {
					DEBUG_PRINT(error_count, "get_next_client_data()=0");
				}
				continue;
			}
			if (conn->state == STATE_DBIO || conn->state==STATE_LISTEN) {
				// skip this, normal, a proxy conn for DBIO and LISTEN
				continue;
			}

			ret = fdwatch_check_fd( conn->conn_fd );
			if ( 0==ret ) //  && conn->state==STATE_READING) 
			{

				// something went wrong, some kernel may return EGAIN
				// WARN_PRINT(errno==EAGAIN, "main:fdwatch_check_fd EAGAIN cid=%d fd=%d state=%d", get_conn_id(conn), conn->conn_fd, conn->state);
				// XXX here server will disconnect client
				WARN_PRINT(-6, "fdwatch_check_fd:errno=%d fd=%d state=%d fd_errno=%d", errno, conn->conn_fd, conn->state, conn->fd_errno);
				conn->fd_errno++;
				// disconnect to avoid loop fdwatch_check_fd()==0
				if (conn->fd_errno > 1) {
					WARN_PRINT(-666, "fdwatch_check_fd:disconnect_fd=%d state=%d fd_errno=%d", conn->conn_fd, conn->state, conn->fd_errno);
					do_clean_disconnect( conn );
				}
				/*
				// bug will loop forever
				if (errno != EAGAIN && errno != 0) {
					do_clean_disconnect( conn );
				}
				*/
				continue; // break; // continue or break ?
			}

			// reset fd_errno
			conn->fd_errno = 0;

			if (STATE_READING==conn->state) {
				ret = do_read( conn ); 
				// this is ugly way to do quit!
				if (ret==LEVEL_QUITALL) {
					running = 0;
					break;
				}
				if (ret < 0) {  // disconnect when error
					// no clean up this is no help
					do_disconnect( conn );
				}
				ERROR_NEG_PRINT(ret, "do_read");
				error_count = 0; // normal case
				continue;
			}

			if (STATE_SENDING==conn->state) {
				ret = do_send( conn ); 
				if (ret < 0) {  // disconnect when error
					// no clean up this is no help
					do_disconnect( conn );
				}
				ERROR_NEG_PRINT(ret, "do_write");
				continue;  // why break?  why not continue?
			}



		} // while() .. fdwatch_get_next_client_data

		// TODO how to write a loop label? loop_io:

	} // while(running)



	// normal shutdown
	app_shutdown(0);
	return 0;
}

