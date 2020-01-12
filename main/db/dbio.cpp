/**
 * dbio.cpp
 *
 * somedays, when really free, look into prepared statement for faster 
 * long query (select c1,c2....)
 *
 */

extern "C" {
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>	// for console_main testing
#include <fcntl.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>	// for socket in general (console)
#include <sys/un.h> // for unix domain socket (console)
#include <sys/select.h>
#include "fatal.h"
}

#include "mysql.h"
#include "evil.h"

#ifndef INT32_MAX
#define INT32_MAX		(2147483647)
#endif

#define MYSQL_HOST	"127.0.0.1"
#define MYSQL_PORT	3306
#define	MYSQL_USER	"evil"
#define MYSQL_PASSWORD	"1" //"1"
#define MYSQL_DATABASE	"evil_base"

// one card max 4 bytes ",NULL" + 100 overhead
// #define QUERY_MAX (EVIL_CARD_MAX*5 + 100 + EVIL_CMD_MAX)
// for evil_ladder_collection "if(c1>0,1,0)+if(c2>0,1,0)...."
#define QUERY_MAX (EVIL_CARD_MAX*15 + 100 + EVIL_CMD_MAX)
// TODO load game, deck * 2 = 800,  command_list = many
// @see evil.h
// #define DB_BUFFER_MAX	(EVIL_CARD_MAX*2 + 100 + EVIL_CMD_MAX) // around 3000

#define DB_ER_PRINT(ret, ...)  do { if (0 != (ret)) { db_output(out_buffer, ret, __VA_ARGS__);  ERROR_PRINT(ret, __VA_ARGS__);  } } while(0)

// out must be a char * buffer
#define DB_ER_RETURN(ret, ...)  do { if (0 != (ret)) { db_output(out_buffer, ret, __VA_ARGS__);  ERROR_RETURN(ret, __VA_ARGS__);  } } while(0)

#define DB_ER_NEG_PRINT(ret, ...)  do { if ((ret) < 0) { db_output(out_buffer, ret, __VA_ARGS__);  ERROR_PRINT(ret, __VA_ARGS__);  } } while(0)

//match:ERROR_NEG_RETURN
#define DB_ER_NEG_RETURN(ret, ...)  do { if ((ret) < 0) { db_output(out_buffer, ret, __VA_ARGS__);  ERROR_NEG_RETURN(ret, __VA_ARGS__);  } } while(0)

//////////////////////////////////////////////////
//////////////////// DB START [ //////////////////
//////////////////////////////////////////////////


MYSQL * my_open()
{
	MYSQL * conn = NULL;
	INFO_PRINT(0, "my_open: mysql version: %s", mysql_get_client_info());

	//  ----------------------
	conn = mysql_init(NULL);  // NULL ?
	FATAL_EXIT(conn==NULL, "my_open:mysql_init");


	// ref: setup utf8 connection
	// http://stackoverflow.com/questions/8112153/process-utf-8-data-from-mysql-in-c-and-give-result-back
	mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8");   // was "utf8"
	// peter: SET NAMES is optional:
//    mysql_options(conn, MYSQL_INIT_COMMAND, "SET NAMES utf8");  // was "utf8"
	MYSQL * ret_conn;
	ret_conn = mysql_real_connect(conn, MYSQL_HOST, MYSQL_USER, 
		MYSQL_PASSWORD, MYSQL_DATABASE, MYSQL_PORT, NULL, 0);
	if (ret_conn == NULL) {
		ERROR_PRINT(-55, "my_open:mysql_real_connect %d %s", mysql_errno(conn), MYSQL_HOST);
		mysql_close(conn);
		return NULL;
	}

	
	MY_CHARSET_INFO charset_info;
	mysql_get_character_set_info(conn, &charset_info);
	// pls check:  charset=utf8_general_ci  collate=utf8  num=33
	// DEBUG_PRINT(0, "mysql charset=%s collate=%s num=%d"
	// , charset_info.name, charset_info.csname, charset_info.number);
	return conn;
}

int my_close(MYSQL *conn)
{
	mysql_close(conn);
	return 0;
}

int my_reconn(MYSQL **pconn)
{
	MYSQL *new_conn = NULL;
	new_conn = my_open();
	if (new_conn == NULL) {
		return -55;
	}
	// only close and re-new the *pconn when new_conn is ready!
	if (*pconn != NULL) {
		my_close(*pconn);
	}
	*pconn = new_conn;
	return 0;
}

int my_query(MYSQL **pconn, const char *q, int len)
{
	int ret;
	int count = 0;
again:
	if (*pconn != NULL) {
		ret = mysql_real_query(*pconn, q, len);
		if (ret == 0) {
			return 0;
		}
	}
	// implicit: ret != 0 : means error
	ret = 8888;  // means *pconn is null
	
	if (*pconn != NULL) {
		ret = mysql_errno(*pconn);
	}
	if (ret==2013 || ret==2006 || ret==8888) {
		// sleep depends on count
		WARN_PRINT(-1, "my_query:retry %d", count);
		sleep(count);
		my_reconn(pconn);
		count++;
		if (count >= 3) {
			return ret;
		}
		goto again;
	}
	return ret;
}

// keep some misc utlity here
// usage:  (return int)
// strtol_safe("123abc", -1) => 123
// strtol_safe("abc", -1) => -1   // strtok_safe("56.9", -1) => 56
static int strtol_safe(char *str, int default_value) {
    char *end;
    long value;
	if (str==NULL) {
		BUG_PRINT(-777, "dbio:strtol_safe:null_str");
		return default_value;
	}
    value = strtol(str, &end, 10); 
    if (end==str) {
		WARN_PRINT(-1, "strtol_safe %s", str);
        return default_value;
    }    
    // partial conversion is allowed, where end > str
    return (int) value;
}

static long strtolong_safe(char *str, long default_value) {
    char *end;
    long value;
    value = strtol(str, &end, 10); 
    if (end==str) {
		WARN_PRINT(-1, "strtolong_safe %s", str);
        return default_value;
    }    
    // partial conversion is allowed, where end > str
    return value;
}

static double str_double_safe(const char *str, double default_value)
{
	char *end;
	double value;
	value = strtod(str, &end);
	if (end==str) {
		return default_value;
	}
	return value;
}

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

int get_random_list(int *random_list, int count, int min_v, int max_v)
{
	if (count <= 0 || min_v > max_v) {
		return 0;
	}
	int random_range = max_v - min_v + 1;
	int min_count = min(count, random_range);
	int fix_list[random_range];
	for (int i = 0; i < random_range; i++) {
		fix_list[i] = min_v + i;
	}

	int random_count = 0;
	int cur_count = min_count;
	int fix_count = random_range;
	int idx;
	while (cur_count > 0) {
		int rand_value = rand();
		idx = rand_value % (fix_count--);
		random_list[random_count] = fix_list[idx];
		random_count ++;

		fix_list[idx] = fix_list[cur_count-1];
		cur_count--;
	}
	return random_count;
}

//////////////////////////////////////////////////
//////////////////// DB END ] ////////////////////
//////////////////////////////////////////////////


//////////////////////////////////////////////////
//////////////////// IN START [ //////////////////
//////////////////////////////////////////////////


int insert_first_slot(MYSQL **pconn, char *q_buffer, int eid);
time_t get_yesterday(time_t tt);

// item_id, item_count, remark for evil_buy
int update_money(MYSQL **pconn, char *q_buffer, int eid, int gold, int crystal, int item_id=0, int item_count=0, const char *remark="_");

// usually for error, can be use for [code] [message] output
int db_output(char *buffer, int errcode, const char *fmt, ...) 
{
	char *ptr;
	ptr = buffer + sprintf(buffer, "%d ",  errcode);
    va_list argptr;
    va_start(argptr, fmt);
	// TODO need check overflow ?
    vsnprintf(ptr, DB_BUFFER_MAX-10, fmt, argptr);
    va_end(argptr);
	return 0;
}

int init_user_status(evil_user_t *user)
{
	// STATUS_ADD_FIELD
	if (NULL == user) {
		return -3;
	}
	user->lv = 1;
	user->rating = 1000.0; // init rating
	user->exp = 0;
	user->gold = 100;  // free gift!
	user->crystal = 0;	// INIT_CRYSTAL

	user->gid = 0;
	user->gpos = 0;

	user->game_count = 0;
	user->game_win = 0;
	user->game_lose = 0;
	user->game_draw = 0;
	user->game_run = 0;

	user->icon = 0; // default icon 0
	user->sex = 0; // default sex 0
	user->course = 0;

	user->power = MAX_USER_POWER;
	user->power_set_time = 0L;
	user->gate_pos = 1;

	user->fight_ai_time			= FIGHT_AI_MAX_TIME;
	user->fight_gold_time		= FIGHT_GOLD_MAX_TIME;
	user->fight_crystal_time	= FIGHT_CRYSTAL_MAX_TIME;

	user->tower_pos			= 0;
	user->tower_times		= 3;
	user->tower_set_time	= 0;

	user->arena_times 		= ARENA_TIMES_STD;

	sprintf(user->signature, "%s", "-");
	return 0;
}

#define SQL_SAVE_STATUS 	"REPLACE INTO evil_status VALUES (%d,%d,%lf,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lf,%ld,%d,%d,%d,%d,'%s',%d,%d,%ld,%d,FROM_UNIXTIME(%ld),%d,%ld,'%s')"
// internal use, only use in in_register
int save_status(MYSQL **pconn, char *q_buffer, evil_user_t* user)
{
	int len;
	int err;
	int ret;

	if (user==NULL) {
		ERROR_NEG_RETURN(-3, "save_status:null_user");
		return -3;
	}
	if (user->eid <= 0) {
		ERROR_NEG_RETURN(-9, "save_status:invalid_eid %d", user->eid);
		return -9;
	}

	// STATUS_ADD_FIELD
	// global access: using g_query
	len = sprintf(q_buffer, SQL_SAVE_STATUS, user->eid
	, user->lv, user->rating , user->gold, user->crystal
	, user->gid, user->gpos
	, user->game_count, user->game_win
	, user->game_lose, user->game_draw, user->game_run
	// , user->icon
	, user->exp, user->sex, user->course
	// , MAX_USER_POWER, 0L
	, user->power, user->power_set_time
	, user->gate_pos
	, user->fight_ai_time, user->fight_gold_time
	, user->fight_crystal_time
	, user->signals
	, user->tower_pos, user->tower_times, user->tower_set_time
	, user->battle_coin
	, user->monthly_end_date
	, user->arena_times
	, user->arena_last_reward_time
	, user->signature);

	// DEBUG_PRINT(0, "save_status: [%s]", q_buffer);

	ret = my_query(pconn, q_buffer, len);

	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "save_status:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		ERROR_NEG_RETURN(-6, "save_status:affected_row wrong %d\n", ret);
	}
	return 0 ;
}



#define SQL_SAVE_PIECE	"INSERT INTO evil_piece (eid) VALUES (%d)"
// eid
int save_piece(MYSQL **pconn, char *q_buffer, int eid)
{

	int ret;
	int len;

	if (eid <= MAX_AI_EID) {
		ERROR_RETURN(-15, "save_piece:invalid_eid %d", eid);
	}

	len = sprintf(q_buffer, SQL_SAVE_PIECE, eid);

	// DEBUG_PRINT(0, "q_buffer:%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "save_piece:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-16, "save_piece:affected_row %d \n", eid);
	}

	ret = 0;

	return ret;
}


// basic echo
int in_test(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;
	char msg[100];
	msg[0] = '\0';
	ret = sscanf(in_buffer, "%80s", msg);

	len = sprintf(q_buffer, "SELECT CONNECTION_ID()");
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-55, "in_test:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "in_test:null_result");
	}

	////////// NOTE: from here, we should use: 
	////////// 1. db_output()  + goto cleanup   or
	////////// 2. DB_ERR_PRINT() + goto cleanup
	////////// do not use DB_ERR_RETURN() - this will mess up database

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;	// normal user not found or password incorrect
		DB_ER_PRINT(ret, "in_test:num_row_zero");
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		DB_ER_PRINT(ret, "in_test:null_row");
		goto cleanup; // cleanup and early exit
	}
	len = mysql_field_count(*pconn);
	if (1!=len) {
		ret = -65;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "in_test:field_count %d", len);
		goto cleanup;
	}

	len = strtol_safe(row[0], -7);
	sprintf(out_buffer, "0 %s connection_id=%d", msg, len);
	ret = 0;  // make sure it is OK ret

cleanup:
	mysql_free_result(result);

	return ret;
}


int insert_login(MYSQL **pconn, char *q_buffer, int eid, char *ip, char *remark);
int __insert_daily_login(MYSQL **pconn, char *q_buffer, int eid);

// in_* is called by dbio() thread
// out_* is called by nio main thread

// eid, username, password, alias, reg_date, last_login, icon, platform, channel, last_platform, rank_time, anysdk_uid
#define	SQL_REGISTER	"INSERT INTO evil_user VALUES (NULL, '%s', '%s', '%s', NOW(), NOW(), 0, %d, %d, %d, %d, '%s')"

// MYSQL ** pconn is double pointer, because my_query may re-connect
// q_buffer is the query, can be re-use after this function (non_thread_safe)
// in_buffer: input buffer
// out_buffer: output buffer
// @return 0 for success
int in_register(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	// escape use more characters, assume double!
	int err;
	int len;
	char username[EVIL_USERNAME_MAX + 5];
	char password[EVIL_PASSWORD_MAX + 5];
	char alias[EVIL_ALIAS_MAX + 5];
	char ip[EVIL_ADDRESS_MAX + 1];
	char esc_username[EVIL_USERNAME_MAX * 2 + 5];
	char esc_password[EVIL_PASSWORD_MAX * 2 + 5];
	char esc_alias[EVIL_ALIAS_MAX * 2 + 5]; 
	int platform = 0;
	int channel = 0;
	char uid[EVIL_UID_MAX + 5];


	alias[0] = '\0';
	ret = sscanf(in_buffer, IN_REGISTER_SCAN, username, password, ip
	, &platform, &channel, uid);
	if (ret < 3) {
		// DB_ER_RETURN(-5, "register:less_input %d", ret);
		DB_ER_RETURN(-5, "%s %d", E_REGISTER_LESS_INPUT, ret);
	}

	// first character of username and alias should not be '_'
	if (alias[0]=='_' || username[0]=='_') {
		// DB_ER_RETURN(-15, "register:invalid_char[_]");
		DB_ER_RETURN(-15, "%s", E_REGISTER_INVALID_NAME);
	}

	// _ means visitor
	if (alias[0]=='\0') {
		// note: 29 depends on EVIL_ALIAS_MAX
		sprintf(alias, "_%.29s", username);  // _${username} means visitor
	}

	// DEBUG_PRINT(0, "in_reg: username=%s  password=%s  alias=%s\n", username, password, alias);
	len = mysql_real_escape_string(*pconn, esc_username, username, 
		strlen(username));
	DB_ER_NEG_RETURN(len, "esc_username");
	len = mysql_real_escape_string(*pconn, esc_password, password, 
		strlen(password));
	DB_ER_NEG_RETURN(len, "esc_password");
	len = mysql_real_escape_string(*pconn, esc_alias, alias, 
		strlen(alias));
	DB_ER_NEG_RETURN(len, "esc_alias");

	len = sprintf(q_buffer, SQL_REGISTER, esc_username, esc_password
	, esc_alias, platform, channel, platform, MAX_RANKING_CHALLENGE_TIME
	, uid);

	// DEBUG_PRINT(0, "register:q_buffer=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	// this return is usually duplicated username
	// printf("reg: ret=%d  esc username=%s  password=%s  alias=%s\n", ret, esc_username, esc_password, esc_alias);

	if (ret!=0) {
		err = mysql_errno(*pconn);
		// -6 is username duplicate error
		if (err==1062) {  // duplicate entry
			// db_output(out_buffer, -6, "register:duplicate_username_alias");
			db_output(out_buffer, -6, E_REGISTER_DUPLICATE_USERNAME);
			// peter: do not log duplicate, this is not an error
			// DB_ER_RETURN(-6, "register:duplicate_username_alias");
			return -6;  // username and/or alias duplicate
		} else {
			// more info on server log
			ERROR_PRINT(-55, "register: username=%s password=%s alias=%s query=%s"
			, username, password, alias, q_buffer);
			DB_ER_RETURN(-55, "register:mysql_errno %d", err);
			return -55;
		}
	}

	ret = (int)mysql_insert_id(*pconn);
	if (ret <= 0) {
		DB_ER_RETURN(-7, "register:mysql_insert_id %d", ret);
	}


	evil_user_t user;
	bzero(&user, sizeof(user));
	user.eid = ret;  // from above: mysql_insert_id()
	init_user_status(&user);
	ret = save_status(pconn, q_buffer, &user);
	DEBUG_PRINT(0, "in_register:save_status");

	if (ret < 0) {
		// note: this error return may stop user from entering the system
		// anyway, without status, the user is bad
		DB_ER_RETURN(-16, "register:save_status %d", ret);
	}

	ret = save_piece(pconn, q_buffer, user.eid);
	if (ret < 0) {
		// anyway, without piece_list, the user is bad too?
		DB_ER_RETURN(-16, "register:save_piece %d", ret);
	}

	// insert into evil_login
	char str_remark[105];
	sprintf(str_remark, "%d", user.lv);
	ret = insert_login(pconn, q_buffer, user.eid, ip, str_remark);
	if (ret < 0) {
		WARN_PRINT(ret, "register:insert_login_error");
	}

	ret = __insert_daily_login(pconn, q_buffer, user.eid);
	if (ret < 0) {
		WARN_PRINT(ret, "register:insert_daily_login_error");
	}

	/*
	// insert new slot in in_save_card
	ret = insert_first_slot(pconn, q_buffer, user.eid);
	if (ret < 0) {
		// anyway, without first_slot, the user is bad too?
		DB_ER_RETURN(-16, "register:insert_first_slot %d", ret);
	}
	*/

	// output
	sprintf(out_buffer, OUT_REGISTER_PRINT, user.eid, username, password, alias
	, user.lv , user.rating, user.gold, user.crystal
	, user.gid, user.gpos
	, user.game_count, user.game_win, user.game_lose
	, user.game_draw, user.game_run, user.icon, user.exp
	, user.sex, user.course, user.signature
	, "_no_guild", 0, user.gate_pos
	);

	return 0; // no need to return 'ret', as we do not have cleanup jump
}


#define SQL_UPDATE_LAST_LOGIN	"UPDATE evil_user SET last_login=NOW(), last_platform=%d WHERE eid=%d"
// internal use
int update_last_login(MYSQL **pconn, char *q_buffer, int eid, time_t last_login
, int platform)
{
	int len;
	int err;
	int ret;

	if (eid <= 0) {
		ERROR_NEG_RETURN(-5, "update_last_login:invalid_eid %d", eid);
		return -9;
	}

	if (last_login <= 0) {
		ERROR_NEG_RETURN(-15, "update_last_login:invalid_last_login %ld", last_login);
		return -9;
	}

	// global access: using g_query
	len = sprintf(q_buffer, SQL_UPDATE_LAST_LOGIN, platform, eid);
	// DEBUG_PRINT(0, "update_last_login: [%s]", q_buffer);

	ret = my_query(pconn, q_buffer, len);

	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "update_last_login:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret < 0 || ret > 1) {
		ERROR_NEG_RETURN(-6, "update_last_login:affected_row wrong %d", ret);
	}
	return 0 ;
}

#define SQL_INSERT_LOGIN	"INSERT INTO evil_login (eid, login_date, ip, remark) VALUES(%d, NOW(), '%s', '%s')"

int insert_login(MYSQL **pconn, char *q_buffer, int eid, char *ip, char *remark)
{
	int len;
	int err;
	int ret;
	char esc_remark[100 * 2 + 5];

	if (eid <= 0) {
		ERROR_NEG_RETURN(-5, "insert_login:invalid_eid %d", eid);
		return -9;
	}

	// global access: using g_query

	len = mysql_real_escape_string(*pconn, esc_remark, remark, strlen(remark)); 
	len = sprintf(q_buffer, SQL_INSERT_LOGIN, eid, ip, esc_remark);
	// DEBUG_PRINT(0, "insert_login: [%s]", q_buffer);

	ret = my_query(pconn, q_buffer, len);

	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "insert_login:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_NEG_RETURN(-6, "insert_login:affected_row wrong %d\n", ret);
	}
	return 0 ;
}

int load_card(MYSQL **pconn, char *q_buffer, char *card, int eid);

//#define SQL_LOGIN 	"SELECT evil_user.eid,username,password,alias,reg_date,last_login,icon,evil_status.*,evil_guild.gname,IFNULL(evil_guild.glevel,0) FROM evil_user LEFT JOIN evil_status ON evil_user.eid=evil_status.eid LEFT JOIN evil_guild ON evil_status.gid=evil_guild.gid WHERE evil_user.username='%s' AND evil_user.password='%s'"
//#define SQL_LOGIN "SELECT evil_user.eid,username,password,alias,reg_date,UNIX_TIMESTAMP(last_login),icon,lv,rating,evil_status.gold,evil_status.crystal,evil_status.gid,gpos,game_count,game_win,game_lose,game_draw,game_run,exp,sex,course,power,power_set_time,gate_pos,fight_ai_time,fight_gold_time,fight_crystal_time,signals,tower_pos,tower_times,tower_set_time,battle_coin,UNIX_TIMESTAMP(monthly_end_date),(SELECT IFNULL(MAX(chapter_id),1) from evil_chapter WHERE evil_chapter.eid=evil_user.eid) AS chapter_pos,signature,evil_guild.gname,ifnull(evil_guild.glevel,0) FROM evil_user LEFT JOIN evil_status ON evil_user.eid=evil_status.eid LEFT JOIN evil_guild ON evil_status.gid=evil_guild.gid WHERE evil_user.username='%s' AND evil_user.password='%s'"
#define SQL_LOGIN "SELECT evil_user.eid,username,password,alias,reg_date,UNIX_TIMESTAMP(last_login),icon,evil_status.*,UNIX_TIMESTAMP(evil_status.monthly_end_date),(SELECT IFNULL(MAX(chapter_id),1) from evil_chapter WHERE evil_chapter.eid=evil_user.eid) AS chapter_pos,evil_guild.gname,ifnull(evil_guild.glevel,0),IFNULL(DATEDIFF(NOW(),last_reward_date),1) FROM evil_user LEFT JOIN evil_status ON evil_user.eid=evil_status.eid LEFT JOIN evil_guild ON evil_status.gid=evil_guild.gid LEFT JOIN evil_daily_login ON evil_user.eid=evil_daily_login.eid WHERE evil_user.username='%s' AND evil_user.password='%s'"
int in_login(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{

	int ret;
	int len;
	char username[EVIL_USERNAME_MAX + 1];
	char password[EVIL_PASSWORD_MAX + 1];
	char ip[EVIL_ADDRESS_MAX + 1];
	char esc_username[EVIL_USERNAME_MAX * 2 + 1];
	char esc_password[EVIL_PASSWORD_MAX * 2 + 1];
	double power; // no need send to nio
	const char *gname; // guild name
	int glevel;
	evil_user_t user;
	int platform;
	int channel;
	int has_card;
	int has_get_reward;

	ret = sscanf(in_buffer, IN_LOGIN_SCAN, username, password, ip, &platform
	, &channel);
	if (ret != 5) {
		DB_ER_RETURN(-5, "%s", E_LOGIN_LESS_INPUT);
	}


	len = mysql_real_escape_string(*pconn, esc_username, username, 
		strlen(username));
	DB_ER_NEG_RETURN(len, "esc_username");

	len = mysql_real_escape_string(*pconn, esc_password, password, 
		strlen(password));
	DB_ER_NEG_RETURN(len, "esc_password");


	len = sprintf(q_buffer, SQL_LOGIN, esc_username, esc_password);

	// DEBUG_PRINT(0, "login query: %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (0 != ret) {
		DB_ER_RETURN(-55, "login:mysql_errno %d", mysql_errno(*pconn));
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		// DB_ER_RETURN(-53, "login:null_result");
		DB_ER_RETURN(-53, "%s", E_LOGIN_NULL_RESULT);
	}
	////////// NOTE: from here, we should use: 
	////////// 1. db_output()  + goto cleanup   or
	////////// 2. DB_ERR_PRINT() + goto cleanup
	////////// do not use DB_ERR_RETURN() - this will mess up database

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;	// normal user not found or password incorrect
		// db_output(out_buffer, -6, "login:user_password_incorrect");
		db_output(out_buffer, -6, "%s", E_LOGIN_INCORRECT);

		// username=%s, password=%s", username, password
		// DEBUG_PRINT(ret, "login:user_password_incorrect"); // comment later
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		DB_ER_PRINT(ret, "login:null_row");
		goto cleanup; // cleanup and early exit
	}

	// STATUS_ADD_FIELD
	len = mysql_field_count(*pconn);
	if (42!=len) { 
		ret = -65;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "login:field_count %d", len);
		goto cleanup;
	}

	bzero(&user, sizeof(user)); // clean it!
	
	// [0]==eid, [1]==username, [2]==password, [3]==alias, 
	// [4]==reg_date	[5]==last_login
	// [6]==icon, [7]==eid [8]==lv, [9]==rating, [10]==gold
	// [11]==crystal, [12]==gid, [13]==gpos, [14]==game_count, [15]==game_win
	// [16]==game_lose, [17]==game_draw, [18]==game_run, [19]==exp, [20]==sex
	// [21]==course, [22]==power, [23]==power_set_time, [24]==gate_pos
	// [25]==fight_ai_time, [26]==fight_gold_time
	// [27]==fight_crystal_time [28]==signals
	// [29]==tower_pos [30]==tower_times [31]==tower_set_time
	// [32]==battle_coin
	// [33]==monthly_end_date(useless) [34]==arena_times
	// [35]==arena_last_reward_time
	// [36]==signature [37]==monthly_end_date [38]==chapter_pos
	// [39]==gname [40]==glevel
	// [41]==last_reward_date
	// TOTAL=42
	user.eid = strtol_safe(row[0], -7);
	if (user.eid<0) {
		ret = -7;
		DB_ER_PRINT(-7, "login:strtol_safe eid %s", row[0]);
		goto cleanup;
	}
	user.icon = strtol_safe(row[6], 0);
	user.lv = strtol_safe(row[8], 1);
	sscanf(row[9], "%lf", &(user.rating)); // beware there is no default
	user.gold = strtol_safe(row[10], 0);
	user.crystal = strtol_safe(row[11], 0);
	user.gid = strtol_safe(row[12], 0);
	user.gpos = strtol_safe(row[13], 0);
	user.game_count = strtol_safe(row[14], 0);
	user.game_win = strtol_safe(row[15], 0);
	user.game_lose = strtol_safe(row[16], 0);
	user.game_draw = strtol_safe(row[17], 0);
	user.game_run = strtol_safe(row[18], -7);
	user.exp = strtol_safe(row[19], 0);
	user.sex = strtol_safe(row[20], 0);
	user.course = strtol_safe(row[21], 0);
	sscanf(row[22], "%lf", &power); // beware there is no default
	user.power_set_time = strtolong_safe(row[23], 0);
	user.gate_pos = strtol_safe(row[24], 0);
	user.fight_ai_time = strtol_safe(row[25], 0);
	user.fight_gold_time = strtol_safe(row[26], 0);
	user.fight_crystal_time = strtol_safe(row[27], 0);
	sprintf(user.signals, "%s", row[28]);
	for (int i = 0; i < EVIL_SIGNAL_MAX; i++)
	{
		if (user.signals[i] != '1')
		{
			user.signals[i] = '0';
		}
	}
	user.tower_pos = strtol_safe(row[29], 0);
	user.tower_times = strtol_safe(row[30], 0);
	user.tower_set_time = strtolong_safe(row[31], 0);
	user.battle_coin = strtol_safe(row[32], 0);
	user.arena_times = strtol_safe(row[34], 0);
	user.arena_last_reward_time = strtolong_safe(row[35], 0);
	sprintf(user.signature, "%s", row[36]);
	if (user.signature[0] == '\0') {
		strcpy(user.signature, "_");
	}
	user.monthly_end_date = strtolong_safe(row[37], 0);
	user.chapter_pos = strtol_safe(row[38], 0);
	DEBUG_PRINT(0, "user.chapter_pos=%d", user.chapter_pos);
	gname = row[39]; // guild patch
	if (gname==NULL) gname = "_no_guild";
	glevel = strtol_safe(row[40], 0);
	has_get_reward = (strtol_safe(row[41], -1) <= 0);

	// load card
	has_card = 1;
	ret = load_card(pconn, q_buffer, user.card, user.eid);
	if (ret < 0) {
		if (ret == -6) {
			has_card = 0;
		} else {
			DB_ER_PRINT(-26, "login:load_card eid=%d  ret=%d", user.eid, ret);
		}
	}

	// alias is row[3]
	len = sprintf(out_buffer, OUT_LOGIN_PRINT, user.eid, username, password, row[3]
	, user.lv , user.rating, user.gold, user.crystal
	, user.gid, user.gpos
	, user.game_count, user.game_win, user.game_lose
	, user.game_draw, user.game_run, user.icon
	, user.exp, user.sex, user.course
	, user.fight_ai_time, user.fight_gold_time, user.fight_crystal_time
	, user.signals, user.monthly_end_date, user.signature, gname, glevel
	, user.card
	, user.gate_pos
	, user.chapter_pos
	, has_get_reward
	, has_card
	);

	ret = update_last_login(pconn, q_buffer, user.eid, time(NULL), platform);
	if (ret < 0) {
		WARN_PRINT(ret, "login:update_last_login_error");
	}

	char str_remark[105];
	sprintf(str_remark, "%d", user.lv);
	ret = insert_login(pconn, q_buffer, user.eid, ip, str_remark);
	if (ret < 0) {
		WARN_PRINT(ret, "login:insert_login_error");
	}

	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}


int row_to_hero_slot(MYSQL_ROW row, int *eid, int *hero_id, int *slot_id, char *card, int max_card)
{
	int ret;
//	int slot;
	*eid = strtol_safe(row[0], -7);
	if (*eid < 0) {
		ERROR_PRINT(-7, "row_to_hero_slot:strtol %s", row[0]);
		return -7;
	}
	*hero_id = strtol_safe(row[1], -17);
	if (*hero_id < 0) {
		ERROR_PRINT(-17, "row_to_hero_slot:strtol %s", row[1]);
		return -17;
	}
	*slot_id = strtol_safe(row[2], -27);
	if (*slot_id < 0) {
		ERROR_PRINT(-27, "row_to_hero_slot:strtol %s", row[2]);
		return -27;
	}
	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+3];
		if (NULL==data) {
			card[i] = '0' + 0;
		} else {
			ret = strtol_safe(data, -1);
			if (ret < 0) {
				WARN_PRINT(ret, "row_to_hero_slot:strtol %s card_id=%d", data, i);
				ret = 0;  // manual fix it
			}
			if (ret > max_card)  {
				WARN_PRINT(ret, "row_to_hero_slot:max_card card_id=%d : %d", i, ret);
				ret = max_card;
			}
			card[i] = '0' + (char)ret;
		}
	}
	card[EVIL_CARD_MAX] = '\0'; // null terminate it
	return 0;
}
// max_card limit the number of card for the same id
// load_card: max_card = 9
// load_deck: max_card = 4
// row[0] is eid, so we skip,  row[1] is the first card!
// note: row must have EVIL_CARD_MAX + 1 fields (else, this is broken!)
int row_to_deck(MYSQL_ROW row, int *eid, char *card, int max_card)
{
	int ret;
	int slot;
	*eid = strtol_safe(row[0], -7);
	if (*eid < 0) {
		ERROR_PRINT(-7, "row_to_deck:strtol %s", row[0]);
		return -7;
	}
	slot = strtol_safe(row[1], -7);
	if (slot < 0) {
		ERROR_PRINT(-17, "row_to_deck:strtol %s", row[1]);
		return -7;
	}
	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+2];
		if (NULL==data) {
			card[i] = '0' + 0;
		} else {
			ret = strtol_safe(data, -1);
			if (ret < 0) {
				WARN_PRINT(ret, "row_to_deck:strtol %s card_id=%d", data, i);
				ret = 0;  // manual fix it
			}
			if (ret > max_card)  {
				WARN_PRINT(ret, "row_to_deck:max_card card_id=%d : %d", i, ret);
				ret = max_card;
			}
			card[i] = '0' + (char)ret;
		}
	}
	card[EVIL_CARD_MAX] = '\0'; // null terminate it
	return 0;
}

// max_card limit the number of card for the same id
// load_card: max_card = 9
// load_deck: max_card = 4
// row[0] is eid, so we skip,  row[1] is the first card!
// note: row must have EVIL_CARD_MAX + 1 fields (else, this is broken!)
int row_to_card(MYSQL_ROW row, int *eid, char *card, int max_card)
{
	int ret;
	*eid = strtol_safe(row[0], -7);
	if (*eid < 0) {
		ERROR_PRINT(-7, "row_to_card:strtol %s", row[0]);
		return -7;
	}

	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+1];
		if (NULL==data) {
			card[i] = '0' + 0;
		} else {
			ret = strtol_safe(data, -1);
			if (ret < 0) {
				WARN_PRINT(ret, "row_to_card:strtol %s card_id=%d", data, i);
				ret = 0;  // manual fix it
			}
			if (ret > max_card)  {
				WARN_PRINT(ret, "row_to_card:max_card card_id=%d : %d", i, ret);
				ret = max_card;
			}
			card[i] = '0' + (char)ret;
		}
	}
	card[EVIL_CARD_MAX] = '\0'; // null terminate it
	return 0;
}

#define SQL_LOAD_CARD 	"SELECT * FROM evil_card WHERE eid=%d"

int load_card(MYSQL **pconn, char *q_buffer, char *card, int eid)
{
	int ret;
	int len;

	len = sprintf(q_buffer, SQL_LOAD_CARD, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "load_card:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "load_card:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		// peter: this is normal, since user may disconnect 
		// before job selection, lcard can be empty!
		// sprintf(out_buffer, "%d load_card:empty_row eid=%d", ret, eid);

		// give an empty card structure
		for (int i=0; i<EVIL_CARD_MAX; i++) {
			card[i] = '0';
		}
		card[EVIL_CARD_MAX] = '\0';
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid)
	if (field_count != EVIL_CARD_MAX + 1) {
		ret = -7;
		ERROR_PRINT(ret, "load_card:field_count %d != card_count+1 %d",
			field_count, EVIL_CARD_MAX+1);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	// what's the implication?
	ret = strtol_safe(row[0], -1);
	WARN_PRINT(eid-ret, "load_card:eid %d != row[0] %d", eid, ret);
	
	// null terminate 
	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+1];
		if (NULL==data) {
			card[i] = '0' + 0;
		} else {
			ret = strtol_safe(data, -1);
			if (ret < 0) {
				WARN_PRINT(ret, "load_card:strtol_safe %s card_id=%d", data, i);
				ret = 0;  // manual fix it
			}
			card[i] = '0' + (char)ret;
		}
	}
	card[EVIL_CARD_MAX] = '\0'; // null terminate it

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}


int in_load_card(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	char * card;

	ret = sscanf(in_buffer, IN_LOAD_CARD_SCAN, &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "in_load_card:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "in_load_card:invalid_eid");
	}
	
	len = sprintf(q_buffer, SQL_LOAD_CARD, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "in_load_card:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "db_load_card query: %s", g_query);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "in_load_card:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		// peter: this is normal, since user may disconnect 
		// before job selection, lcard can be empty!
		// sprintf(out_buffer, "%d in_load_card:empty_row eid=%d", ret, eid);
		sprintf(out_buffer, "%d %s eid=%d", ret, E_LOAD_CARD_EMPTY_ROW, eid);
		// DB_ER_PRINT(ret, "in_load_card:empty_row eid=%d", eid);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid)
	if (field_count != EVIL_CARD_MAX + 1) {
		ret = -7;
		DB_ER_PRINT(ret, "in_load_card:field_count %d != card_count+1 %d",
			field_count, EVIL_CARD_MAX+1);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	// what's the implication?
	ret = strtol_safe(row[0], -1);
	WARN_PRINT(eid-ret, "in_load_card:eid %d != row[0] %d", eid, ret);
	
	
	len = sprintf(out_buffer, "%d ", eid);
	card = out_buffer + len;
	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+1];
		if (NULL==data) {
			card[i] = '0' + 0;
		} else {
			ret = strtol_safe(data, -1);
			if (ret < 0) {
				WARN_PRINT(ret, "in_load_card:strtol_safe %s card_id=%d", data, i);
				ret = 0;  // manual fix it
			}
			card[i] = '0' + (char)ret;
		}
	}
	card[EVIL_CARD_MAX] = '\0'; // null terminate it

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_LOAD_DECK 	"SELECT IFNULL(es.name,'_slot_'), ed.* FROM evil_deck ed LEFT JOIN evil_slot es ON ed.eid = es.eid AND es.slot=IF(ed.slot=0,1,ed.slot) WHERE ed.eid=%d"
// SELECT eid, MIN(c1,4)

// load deck is similar to load card, TODO we may have slot
// TODO we may limit the deck by either 4 or less than num of card, auto-trim
int in_load_deck(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	int slot = 0;
	char * card;
	char name[EVIL_ALIAS_MAX + 5];

	ret = sscanf(in_buffer, IN_LOAD_DECK_SCAN, &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "load_deck:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "load_deck:invalid_eid");
	}
	
	len = sprintf(q_buffer, SQL_LOAD_DECK, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "load_deck:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "load_deck:query: %s", q_buffer);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "load_deck:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		// peter: this is normal, since user may disconnect 
		// before job selection, lcard can be empty!
		// DB_ER_PRINT(ret, "load_deck:empty_row eid=%d", eid);
		DB_ER_PRINT(ret, "%s eid=%d", E_LOAD_DECK_EMPTY_ROW, eid);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// 1(slot.name) + card_count + 1 (eid) + 1 (slot)
	if (field_count != EVIL_CARD_MAX + 1 + 1 + 1) {
		ret = -7;
		DB_ER_PRINT(ret, "load_deck:field_count %d != card_count+3 %d",
			field_count, EVIL_CARD_MAX+3);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	sprintf(name, "%s", row[0]);

	// what's the implication?
	ret = strtol_safe(row[1], -1);
	WARN_PRINT(eid-ret, "load_deck:eid %d != row[0] %d", eid, ret);

	// slot
	slot = strtol_safe(row[2], -1);
	if (slot < 0) {
		ret = -6;
		DB_ER_PRINT(ret, "load_deck:slot(%d) < 0 eid=%d", slot, eid);
		goto cleanup;
	}
	if (slot == 0) {
		WARN_PRINT(-3, "load_deck:slot=0  eid=%d", eid);
		slot = 1;
		// continue with slot=1
	}
	
	len = sprintf(out_buffer, "%d %d %s ", eid, slot, name);
	card = out_buffer + len;
	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+3];
		if (NULL==data) {
			card[i] = '0' + 0;
		} else {
			ret = strtol_safe(data, -1);
			if (ret < 0) {
				WARN_PRINT(ret, "load_deck:strtol_safe %s card_id=%d", data, i);
				ret = 0;  // manual fix it
			}
			if (ret > 4) {	// limit the num of same card in deck to 4
				WARN_PRINT(ret, "load_deck:card[%d]>4 (%d) eid=%d"
				, i, ret, eid);
				ret = 4;
			}
			card[i] = '0' + (char)ret;
		}
	}
	card[EVIL_CARD_MAX] = '\0'; // null terminate it

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}

// must have " " as suffix, note: there should be 
// no : REPLACE INTO evil_card
// no : INSERT INTO evil_deck 
#define SQL_SAVE_CARD	"INSERT INTO evil_card "
#define SQL_SAVE_DECK	"REPLACE INTO evil_deck "
#define SQL_INSERT_SLOT	"INSERT INTO evil_slot "
#define SQL_SAVE_HERO_SLOT	"INSERT INTO evil_hero_slot "

// peter: __query_card_list() new spec: using '0001000001...' not using
// binary
// construct the query for insert/replace to evil_card/evil_deck
// @param query is the output param
// @param card card[0] = c1,  card[1]=c2, ..., card[399]=c400etc
// @param card_count  number of card (card[card_count-1] is valid!)
// @param prefix = "REPLACE INTO evil_card ", or INSERT, or evil_deck
// return length of query
static int __query_card_list(const char* prefix, char* query, int query_max, 
int eid, const char *card, int card_count)
{
	// @see mysql-ref-51.pdf : p.2907   for mysql_escape_string
	char *end;
	char numstr[20];

	if (card_count < 0) {
		return -2;
	}
	if (card==NULL) {
		return -3;
	}
	if (prefix==NULL) {
		return -13;
	}
	// not enough for base 
	if (query_max < card_count * 5) { 
		ERROR_RETURN(-12, "query_card:query_max_too_small %d", query_max);
	}

	// why -10 ? -- Each card equals ",1" use two bytes
	// Last ) and '\0' use two bytes.
	char * const query_end_max = query + query_max - 10;


	// (to, from from_len)
	// mysql_escape_string( )

	end = query;  // make it like recursive
	end = stpcpy(end, prefix);
	////////// 
	//for (int i=1; i<=card_count; i++) {
	//	sprintf(numstr, ",c%d", i);
	//	end = stpcpy(end, numstr);
	//}
	//////////

	// remove ) before VALUES
	sprintf(numstr, " VALUES(%d", eid); // close ) and start (
	end = stpcpy(end, numstr);

	// TODO check card_count
	for (int i=0; i<card_count; i++) {
		if (card[i]<'0' || card[i]>'9') {
			ERROR_RETURN(-32, "query_card:0-9 outbound eid=%d i=%d ascii=%d"
			, eid, i, card[i]);
		}
		*end = ','; 	end++;	// keep ++ in same line
		*end = card[i];	end++;
		if (end > query_end_max) {
			ERROR_PRINT(-22, "query_card:overflow %d i=%d", query_max, i);
			return -22;
		}
	}
	// close )
	*end = ')';		end++;
	*end = '\0'; 		//null term but not ++, MUST HAVE!

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}

static int __query_deck_list(const char* prefix, char* query, int query_max, 
int eid, int slot, const char *card, int card_count)
{
	// @see mysql-ref-51.pdf : p.2907   for mysql_escape_string
	char *end;
	char numstr[200];

	if (card_count < 0) {
		return -2;
	}
	if (card==NULL) {
		return -3;
	}
	if (prefix==NULL) {
		return -13;
	}
	// not enough for base 
	if (query_max < card_count * 5) { 
		ERROR_RETURN(-12, "query_deck:query_max_too_small %d", query_max);
	}

	char * const query_end_max = query + query_max - 10;


	// (to, from from_len)
	// mysql_escape_string( )

	end = query;  // make it like recursive
	end = stpcpy(end, prefix);
	////////// 
	//for (int i=1; i<=card_count; i++) {
	//	sprintf(numstr, ",c%d", i);
	//	end = stpcpy(end, numstr);
	//}
	//////////

	// remove ) before VALUES
	sprintf(numstr, " VALUES(%d,%d", eid, slot); // close ) and start (
	end = stpcpy(end, numstr);

	// TODO check card_count
	for (int i=0; i<card_count; i++) {
		if (card[i]<'0' || card[i]>'9') {
			ERROR_RETURN(-32, "query_deck:0-9 outbound eid=%d i=%d ascii=%d"
			, eid, i, card[i]);
		}
		*end = ','; 	end++;	// keep ++ in same line
		*end = card[i];	end++;
		if (end > query_end_max) {
			ERROR_PRINT(-22, "query_deck:overflow %d i=%d", query_max, i);
			return -22;
		}
	}
	// close )
	*end = ')';		end++;
	*end = '\0'; 		//null term but not ++, MUST HAVE!

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}

// useless now
int __query_slot_list(const char* prefix, char* query, int query_max
, int eid, int id, const char *name, const char *card, int card_count)
{
	// @see mysql-ref-51.pdf : p.2907   for mysql_escape_string
	char *end;
	char numstr[200];

	if (card_count < 0) {
		return -2;
	}
	if (card==NULL) {
		return -3;
	}
	if (prefix==NULL) {
		return -13;
	}
	// not enough for base 
	if (query_max < card_count * 5) { 
		ERROR_RETURN(-12, "query_card:query_max_too_small %d", query_max);
	}

	char * const query_end_max = query + query_max - 10;


	// (to, from from_len)
	// mysql_escape_string( )

	end = query;  // make it like recursive
	end = stpcpy(end, prefix);
	////////// 
	//for (int i=1; i<=card_count; i++) {
	//	sprintf(numstr, ",c%d", i);
	//	end = stpcpy(end, numstr);
	//}
	//////////

	// remove ) before VALUES
	sprintf(numstr, " VALUES(%d,%d,'%s'", eid, id, name); // close ) and start (
	end = stpcpy(end, numstr);

	// TODO check card_count
	for (int i=0; i<card_count; i++) {
		if (card[i]<'0' || card[i]>'9') {
			ERROR_RETURN(-32, "query_card:0-9 outbound eid=%d i=%d ascii=%d"
			, eid, i, card[i]);
		}
		*end = ','; 	end++;	// keep ++ in same line
		*end = card[i];	end++;
		if (end > query_end_max) {
			ERROR_PRINT(-22, "query_card:overflow %d i=%d", query_max, i);
			return -22;
		}
	}
	// close )
	*end = ')';		end++;
	*end = '\0'; 		//null term but not ++, MUST HAVE!

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}

static int __query_hero_slot_list(const char* prefix, char* query, int query_max, 
int eid, int hero_id, int slot_id, const char *card, int card_count)
{
	// @see mysql-ref-51.pdf : p.2907   for mysql_escape_string
	char *end;
	char numstr[20];

	if (card_count < 0) {
		return -2;
	}
	if (card==NULL) {
		return -3;
	}
	if (prefix==NULL) {
		return -13;
	}
	// not enough for base 
	if (query_max < card_count * 5) { 
		ERROR_RETURN(-12, "query_hero_slot:query_max_too_small %d", query_max);
	}

	// why -10 ? -- Each card equals ",1" use two bytes
	// Last ) and '\0' use two bytes.
	char * const query_end_max = query + query_max - 10;

	// (to, from from_len)
	// mysql_escape_string( )
	end = query;  // make it like recursive
	end = stpcpy(end, prefix);
	////////// 
	//for (int i=1; i<=card_count; i++) {
	//	sprintf(numstr, ",c%d", i);
	//	end = stpcpy(end, numstr);
	//}
	//////////

	// remove ) before VALUES
//	sprintf(numstr, " SET ");
//	end = stpcpy(end, numstr);
	sprintf(numstr, " VALUES(%d,%d,%d,", eid, hero_id, slot_id); // close ) and start (
	end = stpcpy(end, numstr);


	// TODO check card_count
	for (int i=0; i<card_count; i++) {
		if (card[i]<'0' || card[i]>'9') {
			ERROR_RETURN(-32, "query_card:0-9 outbound eid=%d i=%d ascii=%d"
			, eid, i, card[i]);
		}
		if (i > 0) {
			*end = ',';	end++;
		}
		end += sprintf(end, "%d", card[i]-'0');
//		*end = ','; 	end++;	// keep ++ in same line
//		*end = card[i];	end++;
		if (end > query_end_max) {
			ERROR_PRINT(-22, "query_card:overflow %d i=%d", query_max, i);
			return -22;
		}
	}
	*end = ')';		end++;
	*end = '\0'; 		//null term but not ++, MUST HAVE!

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}


int __get_hero(const char* deck);
int __add_player_ranking(MYSQL **pconn, char *q_buffer, int eid);
int __add_player_arena(MYSQL **pconn, char *q_buffer, int eid);

int __save_card(MYSQL **pconn, char *q_buffer, int eid, const char * card)
{
	int len;
	int err;
	int ret;

	if (eid <= 0) {
		ERROR_RETURN(-15, "save_card:invalid_eid %d", eid);
	}

	// peter: save is only once!  use insert
	len = __query_card_list(SQL_SAVE_CARD, q_buffer, QUERY_MAX, eid
	, card, EVIL_CARD_MAX);
	if (len < 0) {
		ERROR_NEG_RETURN(-25, "save_card:card %d", len);
	}
	// printf("--- save_card: query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		if (err==1062) {
			ERROR_RETURN(-6, "save_card:already_choose_job");
		}
		ERROR_RETURN(-55, "save_card:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		ERROR_RETURN(-7, "save_card:affected_row wrong %d\n", ret);
	}

	////////////////////// also save deck
	len = __query_deck_list(SQL_SAVE_DECK, q_buffer, QUERY_MAX, eid
		, 1, card, EVIL_CARD_MAX);
	if (len < 0) {
		ERROR_NEG_RETURN(-35, "save_card:deck %d", len);
	}

	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-65, "save_card:deck:mysql_errno %d", err);
	}

	// printf("--- save_card: query=%s\n", q_buffer);
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		ERROR_RETURN(-27, "save_card:deck:affected_row wrong %d\n", ret);
	}

	////////////////////// also save hero_slot
	int hero_id = __get_hero(card);
	len = __query_hero_slot_list(SQL_SAVE_HERO_SLOT, q_buffer, QUERY_MAX, eid
		, hero_id, 1, card, EVIL_CARD_MAX);
	if (len < 0) {
		ERROR_NEG_RETURN(-45, "save_card:hero_slot %d", len);
	}
	// printf("--- save_card: query=%s\n", q_buffer);
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-75, "save_card:hero_slot:mysql_errno %d", err);
	}
	ret = mysql_affected_rows(*pconn);
	if (ret != 1) {
		ERROR_RETURN(-37, "save_card:hero_slot:affected_row wrong %d\n", ret);
	}


	// update player ranking data
	ret = __add_player_ranking(pconn, q_buffer, eid);
	WARN_PRINT(ret, "save_card:update_player_ranking_error eid=%d", eid);

	// update player arena data
	ret = __add_player_arena(pconn, q_buffer, eid);
	WARN_PRINT(ret, "save_card:update_player_arena_error eid=%d", eid);

	return 0;
}

// @see dbin_job() 
int in_save_card(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	char card[EVIL_CARD_MAX + 5]; // +1 is ok, +5 for safety 

	ret = sscanf(in_buffer, IN_SAVE_CARD_SCAN, &eid, card);
	if (ret <= 0) {
		DB_ER_RETURN(-5, "save_card:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "save_card:invalid_eid %d", eid);
	}

	ret = __save_card(pconn, q_buffer, eid, card);
	DB_ER_RETURN(ret, "save_card:save_card_fail %d [%s]", eid, card);

	db_output(out_buffer, 0, "%.400s", card);

	/*

	// peter: save is only once!  use insert
//	len = query_insert_card(g_query, QUERY_MAX, eid, card, EVIL_CARD_MAX);
	len = __query_card_list(SQL_SAVE_CARD, q_buffer, QUERY_MAX, eid
		, card, EVIL_CARD_MAX);
	if (len < 0) {
		DB_ER_NEG_RETURN(-25, "save_card:card %d", len);
	}
	// printf("--- save_card: query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		if (err==1062) {
			DB_ER_RETURN(-6, "save_card:already_choose_job");
		}
		DB_ER_RETURN(-55, "save_card:mysql_errno %d", err);
	}


	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		DB_ER_RETURN(-7, "save_card:affected_row wrong %d\n", ret);
	}

	////////////////////// also save deck

	len = __query_deck_list(SQL_SAVE_DECK, q_buffer, QUERY_MAX, eid
		, 1, card, EVIL_CARD_MAX);
	if (len < 0) {
		DB_ER_NEG_RETURN(-35, "save_card:deck %d", len);
	}

	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-65, "save_card:deck:mysql_errno %d", err);
	}
	// printf("--- save_card: query=%s\n", q_buffer);
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		DB_ER_RETURN(-27, "save_card:deck:affected_row wrong %d\n", ret);
	}

	db_output(out_buffer, 0, "%.400s", card);

	// update player ranking data
	ret = __add_player_ranking(pconn, q_buffer, eid);
	WARN_PRINT(ret, "save_card:update_player_ranking_error eid=%d", eid);
	*/


	return 0;
}


int save_slot(MYSQL **pconn, char *q_buffer, int eid, int slot, const char *card);

int save_deck(MYSQL **pconn, char *q_buffer, int eid, int slot, const char *card) 
{
	int len;
	int err;
	int ret;
	len = __query_deck_list(SQL_SAVE_DECK, q_buffer, QUERY_MAX, eid
		, slot, card, EVIL_CARD_MAX);
	if (len < 0) {
		ERROR_NEG_RETURN(-25, "save_deck:query_card %d eid=%d", len, eid);
	}
	// printf("--- save_deck:query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no need to check 1062, as REPLACE is always replace dup row
		WARN_PRINT(err==1062, "save_deck:err=1026 impossible");
		ERROR_RETURN(-55, "save_deck:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		ERROR_RETURN(-7, "save_deck:affected_row wrong %d\n", ret);
	}

	return 0;
}

// TODO : need to check whether we have enough card in evil_card
int in_save_deck(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int len;
	int err;
	int ret;
	int eid;
	int slot;
	char card[EVIL_CARD_MAX + 5]; // +1 is ok, +5 for safety 

	ret = sscanf(in_buffer, IN_SAVE_DECK_SCAN, &eid, &slot, card);
	if (ret <= 0) {
		DB_ER_RETURN(-5, "save_deck:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "save_deck:invalid_eid %d", eid);
	}
	if (slot <= 0) {
		DB_ER_RETURN(-35, "save_deck:invalid_slot %d", slot);
	}
	// DEBUG_PRINT(0, "save_deck:eid=%d slot=%d", eid, slot);

	// peter: save is only once!  use insert
//	len = query_insert_card(g_query, QUERY_MAX, eid, card, EVIL_CARD_MAX);
	len = __query_deck_list(SQL_SAVE_DECK, q_buffer, QUERY_MAX, eid
		, slot, card, EVIL_CARD_MAX);
	if (len < 0) {
		DB_ER_NEG_RETURN(-25, "save_deck:query_card %d eid=%d", len, eid);
	}
	// printf("--- save_deck:query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no need to check 1062, as REPLACE is always replace dup row
		WARN_PRINT(err==1062, "save_deck:err=1026 impossible");
		DB_ER_RETURN(-55, "save_deck:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		DB_ER_RETURN(-7, "save_deck:affected_row wrong %d\n", ret);
	}


	// save to slot
	ret = save_slot(pconn, q_buffer, eid, slot, card);
	if (ret < 0) {
		DB_ER_RETURN(-81, "save_deck:save_slot error %d %d eid=%d"
		, ret, slot, eid);
	}

	sprintf(out_buffer, OUT_SAVE_DECK_PRINT, eid, slot, card);
	return 0;
}

#define SQL_ALIAS	"UPDATE evil_user SET alias='%s' WHERE eid=%d AND alias LIKE '\\_%%'" 

int in_alias(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int err;
	int len;
	char alias[EVIL_ALIAS_MAX + 5];

	ret = sscanf(in_buffer, IN_ALIAS_SCAN, &eid, alias);
	if (ret != 2) {
		DB_ER_RETURN(-5, "alias:invalid_input ret=%d", ret);
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "alias:invalid_eid eid=%d", eid);
	}

	char esc_alias[EVIL_ALIAS_MAX * 2 + 1]; 
	// TODO alias ? set_alias


	len = mysql_real_escape_string(*pconn, esc_alias, alias, 
		strlen(alias));
	DB_ER_NEG_RETURN(len, "alias:esc_alias");
	
	// only allow alias update, if old alias = _xxx   e.g. _$(username)
	// @see db_register_user

	// prepare the query
	len = sprintf(q_buffer, SQL_ALIAS, esc_alias, eid);
	// printf("in_alias : query = %s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	// this return is usually duplicated username
	if (ret!=0) {
		err = mysql_errno(*pconn);
		if (err==1062) {  // duplicate entry
			db_output(out_buffer, -6, "%s", E_ALIAS_DUPLICATE);
			return -6;  // possible when alias is duplicated
		} else {
			DB_ER_RETURN(-55, "alias:mysql_errno %d  query=%s", err, q_buffer);
		}
	}

	// note: if the alias is already set, this will be zero (0)
	ret = mysql_affected_rows(*pconn); // must be 1 for success
	if (ret == 0) {
		DB_ER_NEG_RETURN(-16, "alias:already_set");
	}

	// shall we check ret < 0 ?
	if (ret != 1) {
		DB_ER_NEG_RETURN(-17, "alias:impossible");
	}

	sprintf(out_buffer, OUT_ALIAS_PRINT, eid, alias);
	// finally  (assume ret == 1)
	return 0;
}


#define SQL_GAME		"SELECT * FROM evil_deck WHERE eid=%d OR eid=%d"

int in_game(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int eid1, eid2;
	int ret;
	int err;
	int len;
	int out_eid1, out_eid2;
	char deck1[EVIL_CARD_MAX+5];
	char deck2[EVIL_CARD_MAX+5];
	
	ret = sscanf(in_buffer, IN_GAME_SCAN, &eid1, &eid2);
	if (ret != 2) {
		DB_ER_RETURN(-5, "game:invalid_input ret=%d", ret);
	}
	if (eid1<=0 || eid2<=0) {
		DB_ER_RETURN(-15, "game:invalid_eid eid1=%d eid2=%d", eid1, eid2);
	}

	// if (eid1==eid2)   same eid, is it a bug?
	if (eid1==eid2) {
		DB_ER_RETURN(-25, "game:same_eid eid1=%d eid2=%d", eid1, eid2);
	}

	// note: eid2 may be within AI range

	len = sprintf(q_buffer, SQL_GAME, eid1, eid2);
	DB_ER_NEG_RETURN(len, "game:sprintf");

	// printf("in_game query: %s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "game:query err %d", mysql_errno(*pconn));
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "game:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid) + 1 (slot)
	if (field_count != EVIL_CARD_MAX + 1 + 1) {
		ret = -7;
		DB_ER_PRINT(ret, "game:field_count %d != card_count+2 %d",
			field_count, EVIL_CARD_MAX+2);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	// normally 2 rows, for eid1 and eid2, possible 1 row for eid2 is AI
	// 0 is impossible, should be error
	if ((num_row != 2 && eid2>MAX_AI_EID) || num_row <= 0) {
		ret = -6;	 // logical error
		DB_ER_PRINT(ret, "game:deck_not_enough row=%d %d %d", num_row, eid1, eid2);
		goto cleanup;
	}


	// ok, we are going to get the first row
	// this is eid1, deck1
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		DB_ER_PRINT(ret, "game:deck_null_row %d %d", eid1, eid2);
		goto cleanup;
	}

	err = row_to_deck(row, &out_eid1, deck1, EVIL_NUM_DECK_MAX);
	if (err < 0) {
		ret = -17;
		DB_ER_PRINT(ret, "game:row_to_deck %d", err);
		goto cleanup;
	}

	if (eid2<=MAX_AI_EID) {
		ret = 0;
		out_eid2 = eid2;
		strcpy(deck2, "AI");  // must have something, not \0
		// DEBUG_PRINT(eid2, "game:ai");
		goto output;
	}
	// eid2, deck2
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -23;
		DB_ER_PRINT(ret, "game:deck_null_row %d %d", eid1, eid2);
		goto cleanup;
	}

	err = row_to_deck(row, &out_eid2, deck2, EVIL_NUM_DECK_MAX);
	if (err < 0) {
		ret = -27;
		DB_ER_PRINT(ret, "game:row_to_deck %d", err);
		goto cleanup;
	}

output:
	if (out_eid1==eid1) {  // assume eid2==out_eid2
		sprintf(out_buffer, OUT_GAME_PRINT, out_eid1, deck1, out_eid2, deck2);
	} else {
		// order of eid1 and eid2 are reversed from the SELECT result
		sprintf(out_buffer, OUT_GAME_PRINT, out_eid2, deck2, out_eid1, deck1);
	}
	ret = 0; // ok 

	// DEBUG_PRINT(0, "in_game:out_buffer=%s", out_buffer);

cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_WIN		"UPDATE evil_status SET rating=rating+%lf,game_count=game_count+1,game_win=game_win+%d,game_lose=game_lose+%d,game_draw=game_draw+%d,game_run=game_run+%d,gold=gold+%d,crystal=crystal+%d,exp=exp+(%d),lv=lv+%d,fight_ai_time=fight_ai_time+(%d),fight_gold_time=fight_gold_time+(%d),fight_crystal_time=fight_crystal_time+(%d) WHERE eid=%d and gold+%d>=0 and crystal+%d>=0 and fight_ai_time+%d>=0 and fight_gold_time+%d>=0 and fight_crystal_time+%d>=0"

// internal function for in_win()
int update_win_status(MYSQL **pconn, char *q_buffer
, int eid, int gold, int crystal
, int exp, int lv
, double rating, int win, int lose, int draw, int run
, int ai_times, int gold_times, int crystal_times)
{
	int ret;
	int len ;
	if (eid <= MAX_AI_EID) {
		return 0; // skip AI eid
	}
	len = sprintf(q_buffer, SQL_WIN, rating, win, lose, draw, run
	, gold, crystal, exp, lv, ai_times, gold_times, crystal_times
	, eid, gold, crystal, ai_times, gold_times, crystal_times);
	// printf("update_win_status: query=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		return -55;
	}

	ret = mysql_affected_rows(*pconn);
	if (ret != 1) {
		BUG_PRINT(ret, "update_win_status:ret");
		return -3;  // empty row for status
	}
	return 0;
}

// input: in="3 abc 123 11110001 2 xxx yyy"
// out="3 abc 123 11110001"
// ret= point to in "2 xxx yyy"
// in and out should not be the same buffer
int nscan_copy(const char *in, char *out)
{
	int count;
	int n;
	int ret;
	const char *ptr;

	ret = sscanf(in, "%d %n", &count, &n);	
	if (ret != 1) {
		BUG_RETURN(-5, "nscan:sscanf_no_n [%s]", in);
	}

	out += sprintf(out, "%d", count);

	ptr = in + n;
	for (int i=0; i<count; i++) {
		out += sprintf(out, " ");
		ret = sscanf(ptr, "%s %n", out, &n);
		printf("n=%d ptr=[%s] out=[%s]\n", n, ptr, out);
		if (ret != 1) {
			BUG_RETURN(-15, "nscan:param[%d] count=%d [%s]", i, count, in);
		}

		out += strlen(out); 
		ptr += n;
	}
	return ptr - in;
}



int skip_space(char **pointer, const char *end_in)
{
	char *ptr;
	int count = 0;
	ptr = *pointer;

	while (*ptr == ' ') {
		ptr ++;
		count++;
		if (ptr >= end_in) {
			break;
		}
	}
	*pointer = ptr;
	return count;
}

int skip_alpha(char **pointer, const char *end_in)
{
	char *ptr;
	int count = 0;
	ptr = *pointer;

	// alpha can be skip, but \0 should not be skip
	while (*ptr != ' ' && *ptr != '\0') {
		ptr ++;
		count++;
		if (ptr >= end_in) {
			break;
		}
	}
	*pointer = ptr;
	return count;
}

int nscan2(char *in, char **out)
{
	int count;
	int n;
	int ret;
	const char * start_in = in;  // DO NOT update start_in
	// avoid out bound, end of in, point to \0
	char * end_in = in + strlen(in); 

	ret = skip_space(&in, end_in);
	*out = in;
	if (*in == '\0') {
		// this is normal, because it is the end
		return 0;
	}

	ret = sscanf(in, "%d %n", &count, &n);	
	if (ret != 1) {
		BUG_RETURN(-5, "nscan2:sscanf_no_n [%s]", in);
	}
	if (count == 0) {
		in++; // skip (0)
		goto cleanup;
	}
	// order is important
	in += n;

	// skip the content
	for (int i=0; i<count; i++) {
		skip_space(&in, end_in);
		ret = skip_alpha(&in, end_in);
		if (ret <= 0)  {
			BUG_RETURN(-12, "nscan2:less_count %d %d", i, count);
		}
	}

cleanup:
	// do not skip space here
	// in at most go to end_in position (original is \0)

	// printf("in=[%s]\n", in);
	// may not be necessary
	if (in >= end_in) {
		in = end_in ;
	}
	// order is important, avoid one more call to nscan2
	ret = skip_space(&in, end_in);

	if (in >= end_in) {
		return 0;
	} else {
		// skip space return
		if (ret >= 1) {
			in--;
		}
		*in = '\0';
		return in - start_in + 1 ;
	}
}


int nscan3(char *in, char **out)
{
	int count;
	int n;
	int ret;
	const char * start_in = in;  // DO NOT update start_in
	// avoid out bound, end of in, point to \0
	char * end_in = in + strlen(in); 

	skip_space(&in, end_in);
	*out = in;
	if (*in == '\0') {
		// this is normal, because it is the end
		return 0;
	}

	ret = sscanf(in, "%d %n", &count, &n);	
	if (ret != 1) {
		BUG_RETURN(-5, "nscan3:sscanf_no_n [%s]", in);
	}

	in += n;
	if (count == 0) {
		goto cleanup;
	}

	// count > 0
	if (*in == '\0') {
		BUG_RETURN(-15, "nscan3:no_param [%s]", start_in);
	}

	// skip the content
	for (int i=0; i<count; i++) {
		skip_alpha(&in, end_in);
		if (*in == '\0' && i != count -1) {
			BUG_RETURN(-25, "nscan3:param_not_enough %d %d", i, count);
		}
		skip_space(&in, end_in);
		if (*in == '\0' && i != count -1) {
			BUG_RETURN(-35, "nscan3:param_not_enough %d %d", i, count);
		}
	}

cleanup:
	if (*in != '\0') {
		in--;
		*in = '\0';
		in++;
	}
	if (in >= end_in) {
		return 0;
	}
	return in-start_in;
}

int nscan(char *in, char **out)
{
	return nscan3(in, out);
}

// XXX may need larger buffer for replay
// gameid, play_time, game_type, winner, seed, 
// start_side, ver, eid1, eid2, lv1
// lv2, icon1, icon2, alias1, alias2
// deck1, deck2, param, cmd
#define SQL_SAVE_REPLAY	"INSERT INTO evil_replay VALUES (%ld, '%s', %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, '%s', '%s', '%s', '%s', '%s', '%s')"

int in_save_replay(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
//, char *deck1, char *deck2, char *cmd)
{
	long gameid;
	char play_time[25];
	int game_type;
	int winner, star, seed, start_side, ver;
	int eid1, eid2;
	int lv1, lv2;
	int icon1, icon2;
	char alias1[EVIL_ALIAS_MAX+1];
	char alias2[EVIL_ALIAS_MAX+1];
	char esc_alias1[EVIL_ALIAS_MAX * 2 + 5]; 
	char esc_alias2[EVIL_ALIAS_MAX * 2 + 5]; 
	char *deck1;
	char *deck2;
	char *param;
	const char *cmd;

	char esc_cmd[EVIL_CMD_MAX * 2 + 1];
	int len_cmd;
	int len;
	int ret;
	int n;

	char *in_ptr;
	strcpy(out_buffer, in_buffer);
	in_ptr = out_buffer;

	// game_id, game_type, winner, star, seed
	// start_side, ver, eid1, eid2, lv1
	// lv2, icon1, icon2, alias1[30], alias2[30]
	ret = sscanf(in_ptr, "%ld %d %d %d %d %d %d %d %d %d %d %d %d %s %s %n"
	, &gameid, &game_type, &winner, &star, &seed
	, &start_side, &ver, &eid1, &eid2, &lv1
	, &lv2, &icon1, &icon2, alias1, alias2, &n
	);

	if (ret != 15) {
		DB_ER_RETURN(-5, "save_replay:invalid_input %d", ret);
	}

	sprintf(play_time, "20%02ld-%02ld-%02ld %02ld:%02ld:%02ld"
	, gameid  / 10000000000000L
	, (gameid % 10000000000000L) 	/ 100000000000L
	, (gameid % 100000000000L) 		/ 1000000000L
	, (gameid % 1000000000L) 		/ 10000000L
	, (gameid % 10000000L) 			/ 100000L
	, (gameid % 100000L) 			/ 1000L);

	// DEBUG_PRINT(0, "play_time=[%s]", play_time);

	in_ptr += n;

	// deck1[400], deck2[400], param[200]
	// they are in format nscan(), 
	// e.g.: n d1 d2 d3 ... dn
	// XXX deck1 deck2 maybe card list type
	ret = nscan(in_ptr, &deck1);
	DB_ER_NEG_RETURN(ret, "save_replay:invalid_deck1 %d", ret);

	in_ptr+=ret;
	ret = nscan(in_ptr, &deck2);
	DB_ER_NEG_RETURN(ret, "save_replay:invalid_deck2 %d", ret);

	in_ptr+=ret;
	ret = nscan(in_ptr, &param);
	DB_ER_NEG_RETURN(ret, "save_replay:invalid_param %d", ret);
	in_ptr+=ret;

	// INFO_PRINT(0, "save_replay:play_time[%s] deck1[%s] deck2[%s] param[%s]", play_time, deck1, deck2, param);

	// cmd is the reminding str of in_buffer
	cmd = in_ptr;
	len_cmd = strlen(cmd);
	if (len_cmd >= EVIL_CMD_MAX) {
		DB_ER_RETURN(-45, "save_replay:cmd_overflow %d", len_cmd);
	}
	// peter: no need for esc string for deck1, deck2

	len = mysql_real_escape_string(*pconn, esc_alias1, alias1, strlen(alias1));
	len = mysql_real_escape_string(*pconn, esc_alias2, alias2, strlen(alias2));

	len = mysql_real_escape_string(*pconn, esc_cmd, cmd, len_cmd);
	DB_ER_NEG_RETURN(len, "replay_esc_cmd");

	len = sprintf(q_buffer, SQL_SAVE_REPLAY
	, gameid, play_time, game_type, winner, star
	, seed, start_side, ver, eid1, eid2
	, lv1, lv2, icon1, icon2, esc_alias1
	, esc_alias2, deck1, deck2, param, esc_cmd);

	// DEBUG_PRINT(0, "INSERT_REPLAY:(%d) [%s]\n", len, q_buffer); 
	ret = my_query( pconn, q_buffer, len);

	if (ret != 0) {
		int err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		DB_ER_NEG_RETURN(-55, "save_replay:mysql_errno %d", err);
		return -55;
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret != 1) {
		DB_ER_NEG_RETURN(-6, "save_replay:affected_row %d %ld\n", ret, gameid);
	}
	db_output(out_buffer, 0, "ok");
	return 0;
}

#define SQL_LIST_REPALY "SELECT gameid,winner,ver,eid1,eid2,lv1,lv2,icon1,icon2,alias1,alias2 FROM evil_replay WHERE eid1=%d or eid2=%d ORDER BY gameid DESC LIMIT 10"

int in_list_replay(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	int count;
	char *ptr;
	const char *alias1;
	const char *alias2;

	ret = sscanf(in_buffer, IN_LIST_REPLAY_SCAN, &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "list_replay:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "list_replay:invalid_eid");
	}
	len = sprintf(q_buffer, SQL_LIST_REPALY, eid, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "list_replay:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}
	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "list_replay:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// gameid, winner, ver, eid1, eid2, lv1, lv2, icon1, icon2, alias1, alias2
	if (field_count != 11) {
		ret = -7;
		DB_ER_PRINT(ret, "list_replay:field_count %d != 9", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		// no replay, this is normal
		sprintf(out_buffer, "%d %d", eid, 0);
		goto cleanup;
	}

	ptr = out_buffer;
	ptr += sprintf(ptr, OUT_LIST_REPLAY_PRINT, eid, num_row);
	count = 0;	
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-7, "list_repaly:fetch_row_overflow %d", count);
			break;
		}
		alias1 = row[9];
		alias2 = row[10];
		if (alias1 == NULL || alias1[0] == '\0') {
			alias1 = "_no_alias";
		}
		if (alias2 == NULL || alias2[0] == '\0') {
			alias2 = "_no_alias";
		}
			
		ptr += sprintf(ptr, OUT_LIST_REPLAY_ROW_PRINT, row[0], row[1]
		, row[2], row[3], row[4], row[5], row[6], row[7], row[8]
		, alias1, alias2);
	}
	ret = 0;  // make sure ret is OK (0)

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}

#define SQL_LOAD_REPLAY "SELECT * FROM evil_replay WHERE gameid=%ld"

int in_load_replay(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	long gameid;
	const char *alias1;
	const char *alias2;

	ret = sscanf(in_buffer, IN_LOAD_REPLAY_SCAN, &gameid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "load_replay:invalid_input");
	}
	if (gameid <= 0) {
		DB_ER_RETURN(-15, "load_replay:invalid_gameid");
	}
	len = sprintf(q_buffer, SQL_LOAD_REPLAY, gameid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "load_replay:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}
	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "load_replay:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// field_count = 20
	// gameid, play_time, game_type, winner, star
	// seed, start_side, ver, eid1, eid2
	// lv1 ,lv2, icon1, icon2, alias1
	// alias2, deck1, deck2, param, cmd
	if (field_count != 20) {
		ret = -7;
		DB_ER_PRINT(ret, "load_replay:field_count %d != 20", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		DB_ER_PRINT(ret, "load_replay:no_such_replay %d", num_row);
		goto cleanup;
	}
	
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		DB_ER_PRINT(ret, "load_replay:null_row");
		goto cleanup;
	}
	alias1 = row[14];
	alias2 = row[15];
	if (alias1 == NULL) {
		alias1 = "_no_alias";
	}
	if (alias2 == NULL) {
		alias2 = "_no_alias";
	}
	sprintf(out_buffer, OUT_LOAD_REPLAY_PRINT
	, 0, row[0], row[2], row[3], row[4]
	, row[5], row[6], row[7], row[8], row[9]
	, row[10], row[11], row[12], row[13], alias1
	, alias2, row[16], row[17], row[18], row[19]);
	ret = 0;  // make sure ret is OK (0)

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}

//  eid, filename, content
#define SQL_SAVE_DEBUG	"INSERT INTO evil_debug VALUES (NULL, %d, NOW(), '%s', '%s')"

int in_save_debug(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int len;
	int ret;
	int eid;
	int n;
	char filename[100 + 5];
	const char *content;
	char esc_filename[200 + 5];
	char esc_content[8000 + 5]; // a bit too large
	int len_filename;
	int len_content;

	ret = sscanf(in_buffer, IN_SAVE_DEBUG_SCAN, &eid, filename, &n);
	if (ret != 2) {
		DB_ER_RETURN(-5, "save_debug:invalid_input %d", ret);
	}

	len_filename = strlen(filename);
	if (len_filename >= 100) {
		DB_ER_RETURN(-15, "save_debug:filename_too_long");
	}

	len = mysql_real_escape_string(*pconn, esc_filename
		, filename , len_filename);
	DB_ER_NEG_RETURN(len, "save_debug:esc_filename");

	// content is the reminding part
	content = in_buffer + n;
	len_content = strlen(content);
	if (len_content >= 4000) {
		DB_ER_RETURN(-25, "save_debug:content_too_long");
	}

	len = mysql_real_escape_string(*pconn, esc_content
		, content, len_content);
	DB_ER_NEG_RETURN(len, "save_debug:esc_content");

	
	len = sprintf(q_buffer, SQL_SAVE_DEBUG, eid, esc_filename, esc_content);
	DB_ER_NEG_RETURN(len, "save_debug:query");

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		DB_ER_RETURN(-55, "save_debug:mysql_errno %d", err);
		return -55;
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		DB_ER_NEG_RETURN(-6, "save_debug:affected_row wrong %d\n", ret);
	}

	db_output(out_buffer, 0, "ok");

	return 0; // not yet implement
}



#define SQL_LOAD_DEBUG	"SELECT eid, content FROM evil_debug WHERE filename='%s' ORDER BY bugtime DESC LIMIT 1"
// input: filename
// output content
// int in_load_debug(const char * filename, char *content) 
int in_load_debug(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	char filename[100+5];
	char esc_filename[200+5];
	int len_filename;

	ret = sscanf(in_buffer, IN_LOAD_DEBUG_SCAN, filename);
	if (ret != 1) {
		DB_ER_RETURN(-5, "load_debug:invalid_input %d", ret);
	}

	len_filename = strlen(filename);
	if (len_filename >= 100) {
		DB_ER_RETURN(-15, "load_debug:filename_too_long %zd", strlen(in_buffer));
	}

	len = mysql_real_escape_string(*pconn, esc_filename
		, filename, len_filename);

	len = sprintf(q_buffer, SQL_LOAD_DEBUG, esc_filename);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "load_debug:mysql_errno %d", mysql_errno(*pconn));
	}

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-3, "load_debug:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;
		DB_ER_PRINT(ret, "load_debug:filename_not_found %s", filename);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		DB_ER_PRINT(ret, "load_debug:null_row");
		goto cleanup;
	}

	// row[0]=eid,  row[1]=content
	ret = strtol_safe(row[0], -77);
	sprintf(out_buffer, OUT_LOAD_DEBUG_PRINT, ret, filename, row[1]);
	ret = 0; // means ok

cleanup:
	mysql_free_result(result);
	return ret;
}

// it is actually load_deck
int in_quick(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	return in_load_deck(pconn, q_buffer, in_buffer, out_buffer);
}

const char * SQL_GET_CARD_ALL_COUNT = 
"SELECT t1.eid, t1.c%d, t2.c%d FROM evil_card t1 LEFT JOIN evil_deck t2 ON t1.eid=t2.eid WHERE t1.eid=%d";

// return card_count (-negative means error)
int get_card_all_count(MYSQL **pconn, char *q_buffer, int eid, int card_id
, int* card_count, int* deck_count)
{
	int ret;
	int len;

	if (eid <= 0) {
		ERROR_RETURN(-5, "get_card_all_count:invalid_eid");
	}
	if (card_id <= 0) {
		ERROR_RETURN(-15, "get_card_all_count:invalid_card_id");
	}

	len = sprintf(q_buffer, SQL_GET_CARD_ALL_COUNT, card_id, card_id, eid);
//	DEBUG_PRINT(0, "%s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "get_card_all_count:query err=%d", mysql_errno(*pconn)); 
	}

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_card_all_count:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "get_card_all_count:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// eid, card_count, deck_count
	if (field_count != 3) { 
		ret = -7;
		ERROR_PRINT(ret, "get_card_all_count:field_count %d != 2", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);

	int eee;
	// if eee != eid....
	eee = strtol_safe(row[0], -5);
	if (eee != eid) {
		ret = -17;
		ERROR_PRINT(eid-eee, "get_card_all_count:eid_mismatch %d %d", eid, eee);
		goto cleanup;
	}

	
	char * data;
	data = row[1];
	if (NULL == data) {
		WARN_PRINT(-27, "get_card_all_count:card_count==NULL");
		*card_count = 0;
	} else {
		*card_count = strtol_safe(data, -1);
	}
	
	if (*card_count < 0) {
		ret = -27;
		ERROR_PRINT(*card_count, "get_card_all_count:negative_card_count");
		goto cleanup;
	}
	

	data = row[2];
	if (NULL == data) {
		WARN_PRINT(-37, "get_card_all_count:deck_count==NULL");
		*deck_count = 0;
	} else {
		*deck_count = strtol_safe(data, -1);
	}

	if (*deck_count < 0) {
		ret = -37;
		ERROR_PRINT(*deck_count, "get_card_all_count:negative_deck_count");
		goto cleanup;
	}
	
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}

const char * SQL_GET_CARD_COUNT = 
"SELECT eid, c%d FROM evil_card WHERE eid=%d";

// return card_count (-negative means error)
int get_card_count(MYSQL **pconn, char *q_buffer, int eid, int card_id)
{
	int ret;
	int len;
	int card_count;

	if (eid <= 0) {
		ERROR_RETURN(-5, "get_card_count:invalid_eid");
	}
	if (card_id <= 0) {
		ERROR_RETURN(-15, "get_card_count:invalid_card_id");
	}

	len = sprintf(q_buffer, SQL_GET_CARD_COUNT, card_id, eid);
//	DEBUG_PRINT(0, "%s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "get_card_count:query err=%d", mysql_errno(*pconn)); 
	}

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_card_count:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "get_card_count:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// eid, card_count
	if (field_count != 2) { 
		ret = -7;
		ERROR_PRINT(ret, "get_card_count:field_count %d != 2", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);

	int eee;
	// if eee != eid....
	eee = strtol_safe(row[0], -5);
	if (eee != eid) {
		ret = -17;
		ERROR_PRINT(eid-eee, "get_card_count:eid_mismatch %d %d", eid, eee);
		goto cleanup;
	}

	
	char * data;
	data = row[1];
	if (NULL == data) {
		WARN_PRINT(-7, "get_card_count:data==NULL");
		card_count = 0;
	} else {
		card_count = strtol_safe(data, -1);
	}	
	
	if (card_count < 0) {
		ret = -27;
		ERROR_PRINT(card_count, "get_card_count:negative_card_count");
		goto cleanup;
	}

	
	ret = card_count;

cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_GET_PIECE_COUNT	"SELECT c%d FROM evil_piece WHERE eid=%d LIMIT 1"
int get_piece_count(MYSQL **pconn, char *q_buffer, int eid, int card_id)
{

	int ret;
	int len;

	if (eid <= MAX_AI_EID) {
		ERROR_RETURN(-15, "select_piece:invalid_eid %d", eid);
	}

	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
		ERROR_RETURN(-25, "select_piece:invalid_card_id %d", card_id);
	}

	len = sprintf(q_buffer, SQL_GET_PIECE_COUNT, card_id, eid);

	// DEBUG_PRINT(0, "q_buffer:%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "select_piece:query err=%d", mysql_errno(*pconn)); 
		return -55; 
	}
	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "select_piece:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		// peter: this is normal, since user may disconnect 
		// before job selection, lcard can be empty!
		ERROR_PRINT(ret, "select_piece:empty_row eid=%d", eid);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!
	if (row == NULL) {
		ret = -13;
		ERROR_NEG_PRINT(ret, "select_piece:null_row");
		goto cleanup; // cleanup and early exit
	}

	ret = strtol_safe(row[0], -1);

cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_ADD_CARD "UPDATE evil_card SET c%d=c%d+1 WHERE eid=%d and c%d<9"
int db_add_card(MYSQL **pconn, char *q_buffer, int eid, int card_id)
{
	int ret;
	int len;

	if (eid <= MAX_AI_EID) {
		return 0; // skip AI eid
	}
	
	if (card_id < 0	|| card_id > EVIL_CARD_MAX) {
		ERROR_RETURN(-15, "add_card:invalid_card_id %d", card_id);
	}

	if (card_id == 0) { 
		return 0;
	}

	len = sprintf(q_buffer, SQL_ADD_CARD, card_id, card_id, eid, card_id);
	// DEBUG_PRINT(0, "%s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "add_card:query err=%d", mysql_errno(*pconn)); 
	}

	ret = mysql_affected_rows(*pconn);
	// max card_count = 9 is normal
	if (ret < 0) {
		ERROR_NEG_RETURN(-6, "add_card:affected_row wrong %d\n", ret);
	}

	ret = 0;
	return ret;
}


#define SQL_UPDATE_CARD_COUNT "UPDATE evil_card SET c%d=%d WHERE eid=%d"

// internal use (do not use for production code)
int update_card_count(MYSQL **pconn, char *q_buffer, int eid, int card_id, int count)
{
	int ret;
	int len;

	if (eid <= 0) {
		ERROR_RETURN(-5, "update_card_count:invalid_eid");
	}
	if (card_id <= 0) {
		ERROR_RETURN(-15, "update_card_count:invalid_card_id");
	}
	if (count < 0 || count>9) {
		ERROR_RETURN(-25, "update_card_count:invalid_count");
	}

	len = sprintf(q_buffer, SQL_UPDATE_CARD_COUNT, card_id, count, eid);
//	DEBUG_PRINT(0, "%s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "update_card_count:query err=%d", mysql_errno(*pconn)); 
	}

	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret != 1) {
		ERROR_NEG_RETURN(-6, "update_card_count:affected_row wrong %d\n", ret);
	}

	ret = 0;
	return ret;
}



// Record buy action
#define SQL_RECORD_BUY	"INSERT INTO evil_buy (eid, trans_date, item_id, item_count, gold, crystal, remark) VALUES(%d, NOW(), %d, %d, %d, %d, '%s')"

// caller should make sure gold and crystal are positive!
int record_buy(MYSQL **pconn, char *q_buffer, int eid, int item_id, int item_count, int gold, int crystal, const char *remark) {
	int len;
	int err;
	int ret;
	char esc_remark[205];

	if (eid <= 0) {
		ERROR_NEG_RETURN(-5, "record_buy:invalid_eid %d", eid);
		return -9;
	}

	if (remark == NULL) {
		remark = "";
	}
	len = strlen(remark);
	if (len > 100) {
		len = 100;
	}
	len = mysql_real_escape_string(*pconn, esc_remark, remark, len); 
	len = sprintf(q_buffer, SQL_RECORD_BUY, eid, item_id, item_count, gold, crystal, esc_remark);

	ret = my_query(pconn, q_buffer, len);

	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "record_buy:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_NEG_RETURN(-6, "record_buy:affected_row wrong %d", ret);
	}
	return 0 ;
}



// TODO use where t1.c%d < 9 to check card_count limit
// peter: need to check t2.gold + gold >= 0  : t2.gold >= -(gold)
const char * SQL_BUY_CARD =
"UPDATE evil_card t1, evil_status t2 SET t1.c%d=t1.c%d+%d, t2.gold=t2.gold+(%d), t2.crystal=t2.crystal+(%d) WHERE t2.gold >= -(%d) AND t2.crystal >= -(%d) AND t1.eid=%d AND t2.eid=%d";

int buy_card(MYSQL **pconn, char *q_buffer, int eid, int card_id, int money_type, int buy_count, int gold, int crystal) {
	int ret;
	int len;
	int err;
	int card_count;

	// there are 2 checks: 
	// 1. enough gold and crystal (status.gold >= -gold ...)
	// 2. num card < 9
	// so we may either: rely on client to check 1, or we need 2 SQLs

	// check if card_count out bound
	ret = get_card_count(pconn, q_buffer, eid, card_id);
	ERROR_NEG_RETURN(ret, "buy_piece:get_card_count %d", ret);
	card_count = ret;

	if (card_count + buy_count > EVIL_NUM_CARD_MAX) {
		// ERROR_RETURN(-52, "buy_card:card_count_out_bound %d %d", card_count, buy_count);
		ERROR_RETURN(-52, "%s %d %d", E_BUY_CARD_COUNT_OUT_BOUND, card_count, buy_count);
	}
	/////		


	// peter: need to give gold, crystal 2 times
	len = sprintf(q_buffer, SQL_BUY_CARD, card_id, card_id, buy_count
	, gold, crystal, gold, crystal, eid, eid);
	// DEBUG_PRINT(len, "buy_card:g_query = %s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		// more info on server log
		// ERROR_PRINT(-55, "buy_card:query=%s", q_buffer);
		ERROR_RETURN(-55, "buy_card:mysql_errno %d", err);
		return -55;
	}

	ret = mysql_affected_rows(*pconn); // must be 2 for success
	if (ret <= 0) {
		// ERROR_RETURN(-16, "buy_card:update_error");
		ERROR_RETURN(-16, "%s", E_BUY_CARD_FAIL);
	}
	// it is possible when cXX is null
	if (ret != 2) {
		ERROR_RETURN(-7, "buy_card:impossible_error %d", ret);
	}

	// ret = record_buy(pconn, q_buffer, eid, card_id, buy_count , -gold, -crystal, "");
	// ERROR_PRINT(ret, "buy_card:record_fail eid=%d card_id=%d buy_count=%d gold=%d crystal=%d", eid, card_id, buy_count, gold, crystal);

	ret = 0;
	return ret;
}

const char * SQL_BUY_PIECE =
"UPDATE evil_piece t1, evil_status t2 SET t1.c%d=t1.c%d+%d, t2.gold=t2.gold+(%d), t2.crystal=t2.crystal+(%d) WHERE t2.gold >= -(%d) AND t2.crystal >= -(%d) AND t1.eid=%d AND t2.eid=%d";

int buy_piece(MYSQL **pconn, char *q_buffer, int eid, int card_id, int money_type, int buy_count, int gold, int crystal) {
	int ret;
	int len;
	int err;
	int piece_count;

	// there are 2 checks: 
	// 1. enough gold and crystal (status.gold >= -gold ...)
	// 2. num piece < 99
	// so we may either: rely on client to check 1, or we need 2 SQLs

	// check if card_count out bound
	ret = get_piece_count(pconn, q_buffer, eid, card_id);
	ERROR_NEG_RETURN(ret, "buy_piece:get_piece_count %d", ret);
	piece_count = ret;

	if (piece_count + buy_count > EVIL_NUM_PIECE_MAX) {
		// ERROR_RETURN(-52, "buy_piece:piece_count_out_bound %d %d", piece_count, buy_count);
		ERROR_RETURN(-52, "%s %d %d", E_BUY_PIECE_COUNT_OUT_BOUND, piece_count, buy_count);
	}
	/////		


	// peter: need to give gold, crystal 2 times
	len = sprintf(q_buffer, SQL_BUY_PIECE, card_id, card_id, buy_count
	, gold, crystal, gold, crystal, eid, eid);
	DEBUG_PRINT(len, "buy_piece:g_query = %s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		// more info on server log
		// ERROR_PRINT(-55, "buy_card:query=%s", q_buffer);
		ERROR_RETURN(-55, "buy_piece:mysql_errno %d", err);
		return -55;
	}

	ret = mysql_affected_rows(*pconn); // must be 2 for success
	if (ret <= 0) {
		// ERROR_RETURN(-16, "buy_piece:update_error");
		ERROR_RETURN(-16, "%s", E_BUY_PIECE_FAIL);
	}
	// it is possible when cXX is null
	if (ret != 2) {
		ERROR_RETURN(-7, "buy_piece:impossible_error %d", ret);
	}

	ret = 0;
	return ret;
}

int in_buy_card(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer) {
	int ret;
	int eid;
	int card_id;
	int card_type;
	int money_type;
	int buy_count;
	int gold;
	int crystal;

	ret = sscanf(in_buffer, IN_BUY_CARD_SCAN, &eid, &card_id
	, &card_type, &money_type, &buy_count, &gold, &crystal);
	if (ret != 7) {
		DB_ER_RETURN(-5, "in_buy_card:less_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_buy_card:invalid_eid %d", eid);
	}

	if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
		DB_ER_RETURN(-25, "in_buy_card:invalid_card_id %d", card_id);
	}

	if (card_type != 0 && card_type != 1) {
		DB_ER_RETURN(-35, "in_buy_card:invalid_card_type %d", card_type);
	}

	if (money_type != 0 && money_type != 1) {
		DB_ER_RETURN(-45, "in_buy_card:invalid_money_type %d", money_type);
	}

	if (buy_count <= 0) {
		DB_ER_RETURN(-55, "in_buy_card:invalid_buy_count %d", buy_count);
	}

	if (card_type == 0) {
		ret = buy_card(pconn, q_buffer, eid, card_id, money_type, buy_count, gold, crystal);
		DB_ER_RETURN(ret, "in_buy_card:buy_card_error");
	} else if (card_type == 1) {
		ret = buy_piece(pconn, q_buffer, eid, card_id, money_type, buy_count, gold, crystal);
		DB_ER_RETURN(ret, "in_buy_card:buy_piece_error");
	}
		

	sprintf(out_buffer, OUT_BUY_CARD_PRINT, eid, card_id, card_type
	, money_type, buy_count, gold, crystal);

	ret = 0;
	return ret;
}


// XXX add WHERE t1.c%d>0
// const char * SQL_SELL_CARD = "UPDATE evil_card t1, evil_deck t2, evil_status t3 set t1.c%d=t1.c%d-%d, t2.c%d=t2.c%d-%d, t3.gold=t3.gold+(%d), t3.crystal=t3.crystal+(%d) where t1.eid=%d and t2.eid=%d and t3.eid=%d";

const char * SQL_SELL_CARD = "UPDATE evil_card t1, evil_status t2 set t1.c%d=t1.c%d-%d, t2.gold=t2.gold+(%d), t2.crystal=t2.crystal+(%d) where t1.eid=%d and t2.eid=%d";

int sell_card(MYSQL **pconn, char *q_buffer, int eid, int card_id, int money_type, int sell_count, int gold, int crystal)
{
	int ret;
	int len;
	int err;
	int all_count; // card all count
	int deck_count; // card count in deck

	/*
	//peter: why should we check the count for selling?
	ret = get_card_count(pconn, q_buffer, eid, card_id);
	ERROR_NEG_RETURN(ret, "sell_card:get_card_count %d", ret);
	card_count = ret;
	if (card_count < sell_count) {
		ERROR_RETURN(-52, "sell_card:card_count_out_bound %d %d", card_count, sell_count);
	}
	/////		
	*/
	ret = get_card_all_count(pconn, q_buffer, eid, card_id
	, &all_count, &deck_count);
	ERROR_NEG_RETURN(ret, "sell_card:get_card_all_count %d", ret);

	// DEBUG_PRINT(0, "sell_card:all_count=%d deck_count=%d", all_count
	// , deck_count);
	if (all_count < sell_count) {
		ERROR_RETURN(-52, "%s %d %d", E_SELL_CARD_COUNT_OUT_BOUND
		, all_count, sell_count);
	}

	if (all_count - deck_count < sell_count) {
		ERROR_RETURN(-62, "%s %d %d", E_SELL_CARD_DECK
		, all_count, sell_count);
	}
	

	// "UPDATE evil_card t1, evil_status t2 set t1.c%d=t1.c%d-%d, t2.gold=t2.gold+(%d), t2.crystal=t2.crystal+(%d) where t1.eid=%d and t2.eid=%d";
	len = sprintf(q_buffer, SQL_SELL_CARD, card_id, card_id, sell_count
	, gold, crystal, eid, eid);
	DEBUG_PRINT(len, "sell_card:g_query = %s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		// more info on server log
		// ERROR_PRINT(-55, "sell_card:query=%s", q_buffer);
		ERROR_RETURN(-55, "sell_card:mysql_errno %d", err);
		return -55;
	}

	ret = mysql_affected_rows(*pconn); // must be 2 for success
	if (ret == 0) {
		// ERROR_NEG_RETURN(-16, "sell_card:update_error");
		ERROR_NEG_RETURN(-16, "%s", E_SELL_CARD_FAIL);
	}

	// shall we check ret < 0 ?
	if (ret != 2 && ret != 3) {
		ERROR_NEG_RETURN(-17, "sell_card:update_impossible %d", ret);
	}

	ret = 0;

	return ret;
}


// XXX add WHERE t1.c%d>0
const char * SQL_SELL_PIECE =
"UPDATE evil_piece t1, evil_status t2 set t1.c%d=t1.c%d-%d, t2.gold=t2.gold+(%d), t2.crystal=t2.crystal+(%d) where t1.eid=%d and t2.eid=%d";

int sell_piece(MYSQL **pconn, char *q_buffer, int eid, int card_id, int money_type, int sell_count, int gold, int crystal)
{
	int ret;
	int len;
	int err;
	int piece_count;

	//peter: why should we check the count for selling?
	ret = get_piece_count(pconn, q_buffer, eid, card_id);
	ERROR_NEG_RETURN(ret, "sell_piece:get_piece_count %d", ret);
	piece_count = ret;
	if (piece_count < sell_count) {
		// ERROR_RETURN(-52, "sell_piece:piece_count_out_bound %d %d", piece_count, sell_count);
		ERROR_RETURN(-52, "%s %d %d", E_SELL_PIECE_COUNT_OUT_BOUND, piece_count, sell_count);
	}
	/////		


	len = sprintf(q_buffer, SQL_SELL_PIECE, card_id, card_id, sell_count
	, gold, crystal, eid, eid);
	DEBUG_PRINT(len, "sell_piece:g_query = %s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		// more info on server log
		// ERROR_PRINT(-55, "sell_piece:query=%s", q_buffer);
		ERROR_RETURN(-55, "sell_piece:mysql_errno %d", err);
		return -55;
	}

	ret = mysql_affected_rows(*pconn); // must be 2 for success
	if (ret == 0) {
		// ERROR_NEG_RETURN(-16, "sell_piece:update_error");
		ERROR_NEG_RETURN(-16, "%s", E_SELL_PIECE_FAIL);
	}

	// shall we check ret < 0 ?
	if (ret != 2) {
		ERROR_NEG_RETURN(-17, "sell_piece:update_impossible");
	}

	ret = 0;

	return ret;
}


int in_sell_card(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer) {
	int ret;
	int eid;
	int card_id;
	int card_type;
	int money_type;
	int sell_count;
	int gold;
	int crystal;

	ret = sscanf(in_buffer, IN_SELL_CARD_SCAN, &eid, &card_id
	, &card_type, &money_type, &sell_count, &gold, &crystal);
	if (ret < 7) {
		DB_ER_RETURN(-5, "in_sell_card:less_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_sell_card:invalid_eid %d", eid);
	}

	if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
		DB_ER_RETURN(-25, "in_sell_card:invalid_card_id %d", card_id);
	}

	if (card_type != 0 && card_type != 1) {
		DB_ER_RETURN(-35, "in_sell_card:invalid_card_type %d", card_type);
	}

	if (money_type != 0 && money_type != 1) {
		DB_ER_RETURN(-45, "in_sell_card:invalid_money_type %d", money_type);
	}

	if (sell_count <= 0) {
		DB_ER_RETURN(-55, "in_sell_card:invalid_sell_count %d", sell_count);
	}

	if (card_type == 0) {
		ret = sell_card(pconn, q_buffer, eid, card_id, money_type, sell_count
		, gold, crystal);
		if (ret == -62) {
			DB_ER_RETURN(ret, "%s eid=%d"
			, E_SELL_CARD_DECK, eid);
		}
		DB_ER_RETURN(ret, "%s eid=%d", E_SELL_CARD_COUNT_OUT_BOUND, eid);
	} else if (card_type == 1) {
		ret = sell_piece(pconn, q_buffer, eid, card_id, money_type, sell_count
		, gold, crystal);
		DB_ER_RETURN(ret, "in_sell_card:sell_piece_error");
	}
		

	sprintf(out_buffer, OUT_SELL_CARD_PRINT, eid, card_id, card_type
	, money_type, sell_count, gold, crystal);

	ret = 0;
	return ret;
}


int in_win(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int winner;
	int eid1, eid2;
	int gold1, gold2;
	int exp1, exp2;
	int lv1, lv2;
	int crystal1, crystal2;
	double rating;
	double rating1, rating2;  // for db update
	int card_id1, card_id2;
	int ai_times1, ai_times2;
	int gold_times1, gold_times2;
	int crystal_times1, crystal_times2;
//	char signals1[EVIL_SIGNAL_MAX+1], signals2[EVIL_SIGNAL_MAX+1];
	int win = 0, lose = 0, draw = 0, run = 0;
	char msg[100];
	strcpy(msg, "");

	ret = sscanf(in_buffer, IN_WIN_SCAN, &winner, &rating
	, &eid1, &eid2, &gold1, &crystal1, &gold2, &crystal2
	, &exp1, &lv1, &exp2, &lv2, &card_id1, &card_id2
	, &ai_times1, &ai_times2, &gold_times1
	, &gold_times2, &crystal_times1, &crystal_times2);
	
	if (ret != 20) {
		DB_ER_RETURN(-5, "win:invalid_input %d", ret);
	}
//	if (gold1 < 0 || gold2 < 0 || crystal1 < 0 || crystal2 < 0) {
//		DB_ER_RETURN(-15, "win:invalid_gold_crystal eid1=%d eid2=%d gold1=%d gold2=%d crystal1=%d crystal2=%d", eid1, eid2, gold1, gold2, crystal1, crystal2);
//	}
	if (lv1 < 0 || lv2 < 0) {
		DB_ER_RETURN(-25, "win:invalid_level eid1=%d eid2=%d lv1=%d lv2=%d", eid1, eid2, lv1, lv2);
	}
	// Note: Do not check exp < 0, it is normal
	// @see nio.cpp:win_game()

	switch(winner) {
		case 1:
		win = 1;  lose = 0; draw = 0; run = 0;
		rating1 = rating;
		break;

		case 2:
		win = 0;  lose = 1; draw = 0; run = 0;
		rating1 = -rating;
		break;

		case 9:	// draw game
		win = 0;  lose = 0; draw = 1; run = 0;
		rating1 = 0.0; // set it 0 by default
		break;

		default:
		DB_ER_RETURN(-7, "win:unknown_winner %d", winner);
		break;
	}

	ret = update_win_status(pconn, q_buffer, eid1, gold1, crystal1
	, exp1, lv1, rating1, win, lose, draw, run
	, ai_times1, gold_times1, crystal_times1);
	if (ret < 0) {
		ERROR_PRINT(ret, "win:update_eid1 %d", ret); // let it fall
		strcat(msg, "eid1_bug");
	}

	ret = db_add_card(pconn, q_buffer, eid1, card_id1);
	if (ret < 0) {
		ERROR_PRINT(ret, "win:add_card1 %d", ret); // let it fall
		strcat(msg, "card1_bug");
	}


	////////////////////////////////////////


	switch(winner) {
		case 2:
		win = 1;  lose = 0; draw = 0; run = 0;
		rating2 = rating;
		break;

		case 1:
		win = 0;  lose = 1; draw = 0; run = 0;
		rating2 = -rating;
		break;

		case 9:
		win = 0;  lose = 0; draw = 1; run = 0;
		rating2 = 0.0; // set it 0 by default
		break;

		default:
		DB_ER_RETURN(-17, "win:unknown_winner %d", winner);
		break;
	}

	ret = update_win_status(pconn, q_buffer, eid2, gold2, crystal2
	, exp2, lv2, rating2, win, lose, draw, run
	, ai_times2, gold_times2, crystal_times2);
	if (ret < 0) {
		ERROR_PRINT(ret, "win:update_eid2 %d", ret); // let it fall
		strcat(msg, "eid2_bug");
	}

	ret = db_add_card(pconn, q_buffer, eid2, card_id2);
	if (ret < 0) {
		ERROR_PRINT(ret, "win:add_card2 %d", ret); // let it fall
		strcat(msg, "card2_bug");
	}

	// TODO better error handling for eid1 and eid2 update issue
	db_output(out_buffer, 0, "ok %s", msg);

	return 0;
}

#define SQL_LOAD_BATCH 	"SELECT * FROM evil_batch WHERE eid=%d and ptype=%d"

int in_load_batch(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int eid;
//	int eee;
	int ptype;
	int ppp;
	int card_list[MAX_LOC];
	bzero(card_list, sizeof(card_list));
	char * ptr;

	// sleep(10); // DEBUG TODO REMOVE


	ret = sscanf(in_buffer, IN_LOAD_BATCH_SCAN, &eid, &ptype);
	if (ret != 2) {
		DB_ER_RETURN(-5, "load_batch:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "load_batch:invalid_eid");
	}
	
	len = sprintf(q_buffer, SQL_LOAD_BATCH, eid, ptype);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "load_batch:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "load_batch:query: %s", q_buffer);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "load_batch:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		// need to report ptype, cannot find batch, nio need to generate 
		sprintf(out_buffer, "%d %d %s", 0, ptype, E_LOAD_BATCH_NO_BATCH); // XXX 99 means no result
		ret = 0;
		// this is normal, user may not pick ever
		// DEBUG_PRINT(ret, "load_batch:empty_row eid=%d", eid);
		goto cleanup;
	}

	if (num_row>1) {
		ret = -16;
		DB_ER_PRINT(ret, "load_batch:row_bug eid=%d", eid);
		goto cleanup;
	}


	field_count = mysql_field_count(*pconn);
	// eid, ptype, card0, card1, card2, card3, card4, card5
	if (field_count != 8) {
		ret = -7;
		DB_ER_PRINT(ret, "load_batch:field_count %d != 8", field_count);
		goto cleanup;
	}


	row = mysql_fetch_row(result);
//	eee = strtol_safe(row[0], 0);
	ppp = strtol_safe(row[1], -5);
	ERROR_NEG_PRINT(ppp, "load_batch:negative_ptype");
	for (int loc=0; loc<MAX_LOC; loc++) {
		card_list[loc] = strtol_safe(row[loc+2], -5);
		ERROR_NEG_PRINT(card_list[loc], "laod_batch:negative_card_list[%d]", loc);

		// DEBUG_PRINT(0, "load_batch:card_list[%d]=%d", loc, card_list[loc]);
	}

	
	ptr = out_buffer;
	ret = sprintf(ptr, "%d %d ", eid, ptype);
	for (int loc=0; loc<MAX_LOC; loc++) {
		ptr = ptr+ret;
		ret = sprintf(ptr, "%d ", card_list[loc]);
	}

	// DEBUG_PRINT(0, "load_batch:out_buffer=%s", out_buffer);


	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}

// int update_money(MYSQL **pconn, char *q_buffer, int eid, int gold, int crystal, int item_id=0, int item_count=0, const char *remark="_");
#define SQL_UPDATE_MONEY   "UPDATE evil_status SET gold=gold+(%d), crystal=crystal+(%d) WHERE eid=%d AND gold>=-(%d) AND crystal>=-(%d)"
int update_money(MYSQL **pconn, char *q_buffer, int eid, int gold, int crystal, int item_id, int item_count, const char *remark)
{
	int ret;
	int len;
	int err;
	if (abs(gold) > 1000000 || abs(crystal) > 10000000){
		BUG_RETURN(-2, "update_money:money_out_bound %d %d", gold, crystal);
	}

	if (gold == 0 && crystal == 0) {
		ret = 0;
		// DEBUG_PRINT(ret, "update_money:money_no_change %d %d", gold, crystal);
		return 0;
	}

	len = sprintf(q_buffer, SQL_UPDATE_MONEY, gold, crystal, eid, gold, crystal);	

	// DEBUG_PRINT(len, "update_money:g_query = %s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		// more info on server log
		// ERROR_PRINT(-55, "buy_card:query=%s", q_buffer);
		ERROR_RETURN(-15, "update_money:mysql_errno %d", err);
		return -15;
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret <= 0) {
		ERROR_NEG_RETURN(-16, "update_money:update_error");
	}

	if (ret != 1) {
		ERROR_NEG_RETURN(-7, "update_money:impossible_error %d", ret);
	}

	if (item_id > 0) {
		record_buy(pconn, q_buffer, eid, item_id, item_count, gold, crystal, remark);
	}

	ret = 0;

	return ret;
}


// eid, ptype, card0-card5, eid
#define SQL_SAVE_BATCH	"REPLACE INTO evil_batch VALUES (%d,%d,%d,%d,%d,%d,%d,%d)"

// SQL_BATCH_MONEY ??  UPDATE evil_status SET gold=gold+X, ... WHERE eid=.. AND gold>... AND crystal>...
// gold=0, crystal=0 for the first time (database not exists)

int in_save_batch(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int len;
	int err;
	int ret;
	int eid;
	int ptype;
	int card_list[MAX_LOC];
	int gold;
	int crystal;

	ret = sscanf(in_buffer, IN_SAVE_BATCH_SCAN, &eid, &ptype, card_list
	, card_list+1, card_list+2 , card_list+3, card_list+4, card_list+5
	, &gold, &crystal);

	if (ret != 10) {
		DB_ER_RETURN(-5, "save_batch:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "save_batch:invalid_eid %d", eid);
	}


	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	ERROR_RETURN(ret, "save_batch:update_money_fail %d %d", gold, crystal);

	len = sprintf(q_buffer, SQL_SAVE_BATCH, eid, ptype, card_list[0], card_list[1], card_list[2]
	, card_list[3], card_list[4], card_list[5]);

	// debug:
	printf("--- save_batch: query=%s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-55, "save_batch:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		DB_ER_RETURN(-7, "save_batch:affected_row wrong %d\n", ret);
	}


	db_output(out_buffer, 0, "ok");
	return 0;
}


const char * SQL_PICK =
"UPDATE evil_card SET c%d=c%d+1 WHERE eid=%d AND c%d<9";

int in_pick(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer) {
	int ret;
	int len;
	int err;

	int eid;
	int batch_type;
	int loc;
	int card_id;
	int gold;
	int crystal;

	ret = sscanf(in_buffer, IN_PICK_SCAN, &eid, &batch_type, &loc
	, &card_id, &gold, &crystal);
	if (ret < 5) {
		DB_ER_RETURN(-5, "in_pick:less_input %d", ret);
	}

	// 1.update money, must
	// 2.update card count, can fail

	// 1.
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	// DB_ER_RETURN(ret, "in_pick:update_money_fail %d %d", gold, crystal);
	DB_ER_RETURN(ret, "%s %d %d", E_PICK_MONEY_NOT_ENOUGH, gold, crystal);


	// peter: need to give gold, crystal 2 times
	len = sprintf(q_buffer, SQL_PICK, card_id, card_id, eid, card_id);
	// DEBUG_PRINT(len, "in_pick:g_query = %s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		// more info on server log
		// ERROR_PRINT(-55, "buy_card:query=%s", q_buffer);
		DB_ER_RETURN(-55, "in_pick:mysql_errno %d", err);
		return -55;
	}

	ret = mysql_affected_rows(*pconn); // must be 2 for success
	if (ret < 0) {
		DB_ER_NEG_RETURN(-16, "in_pick:update_error");
	}

	// card count out bound
	if (ret == 0) {
		ret = 99;
		// DEBUG_PRINT(0, "in_pick:card_count=9 eid=%d", eid);
		db_output(out_buffer, ret, "%d %d %d %d %d %d", eid
		, batch_type, loc, card_id, gold, crystal);
		return ret;
	}

	// it is possible when cXX is null
	if (ret != 1) {
		DB_ER_NEG_RETURN(-7, "in_pick:impossible_error %d", ret);
	}

	ret = 0;
	db_output(out_buffer, 0, "%d %d %d %d %d %d", eid
	, batch_type, loc, card_id, gold, crystal);

	return ret;
}


//#define SQL_STATUS 	"SELECT * FROM evil_status WHERE eid=%d"
// #define SQL_STATUS 	"SELECT *,UNIX_TIMESTAMP(monthly_end_date) FROM evil_status WHERE eid=%d"
//#define SQL_STATUS 	"SELECT eid,lv,rating,gold,crystal,gid,gpos,game_count,game_win,game_lose,game_draw,game_run,exp,sex,course,power,power_set_time,gate_pos,fight_ai_time,fight_gold_time,fight_crystal_time,signals,tower_pos,tower_times,tower_set_time,battle_coin,monthly_end_date,(SELECT IFNULL(MAX(chapter_id),1) FROM evil_chapter WHERE evil_chapter.eid=evil_status.eid) AS chapter_pos,signature,UNIX_TIMESTAMP(monthly_end_date) FROM evil_status WHERE eid=%d"
#define SQL_STATUS "SELECT *,(SELECT IFNULL(MAX(chapter_id),1) FROM evil_chapter WHERE evil_chapter.eid=evil_status.eid) AS chapter_pos,UNIX_TIMESTAMP(monthly_end_date) FROM evil_status WHERE eid=%d"

// get the status from evil_status for eid, save in the puser pointer
// return 0 for OK, return -3 or -6 for eid not found
int get_status(MYSQL **pconn, char *q_buffer, evil_user_t *puser, int eid)
{
	int ret, len;

	len = sprintf(q_buffer, SQL_STATUS, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "get_status:mysql_errno %d", err);
	}

	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_status:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "get_status:empty_row %d", num_row);
		goto cleanup;
	}

	// TODO check mysql_field_count
	field_count = mysql_field_count(*pconn);
	// row[0]=eid,  [1]=lv, [2]=rating, [3]=gold, [4]=crystal, 
	// [5]=gid, [6]=gpos
	// [7]=game_count, [8]=win, [9]=lose, [10]=draw [11]=run 
	// [12]=exp [13]=sex [14]=course [15]=power [16]=power_set_time
	// [17]=gate_pos
	// [18]=fight_ai_time [19]=fight_gold_time [20]=fight_crystal_time
	// [21]=signals, [22]=tower_pos
	// [23]=tower_times, [24]=tower_set_time
	// [25]=battle_coin
	// [26]=monthly_end_date(not use this column!!)
	// [27]=arena_times
	// [28]=arena_last_reward_time
	// [29]=signature
	// [30]=chapter_pos
	// [31]=UNIX_TIMESTAMP(monthly_end_date)
	// (TOTAL=32)
	// STATUS_ADD_FIELD
	if (field_count != 32 ) { 
		ret = -7;
		ERROR_PRINT(ret, "get_status:field_count_mismatch %d", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!
	bzero(puser, sizeof(evil_user_t));

	// assert row[0] == eid
	ret = strtol_safe(row[0], -1);
	if (ret != eid) {  // this is really impossible
		ret = -17;
		ERROR_PRINT(ret, "get_status:eid_mismatch %d %d", eid, ret);
		goto cleanup;
	}
	
	puser->eid = eid;
	puser->lv 		= strtol_safe(row[1], 0);
	sscanf(row[2], "%lf", &puser->rating);  // beware, there is no default!
	puser->gold 		= strtol_safe(row[3], 0);
	puser->crystal 	= strtol_safe(row[4], 0);
	puser->gid 	= strtol_safe(row[5], 0);
	puser->gpos 	= strtol_safe(row[6], 0);
	//
	puser->game_count= strtol_safe(row[7], 0);
	puser->game_win  = strtol_safe(row[8], 0);
	puser->game_lose = strtol_safe(row[9], 0);
	puser->game_draw = strtol_safe(row[10], 0);
	puser->game_run  = strtol_safe(row[11], -77);
	// puser->icon  = strtol_safe(row[12], 0);
	puser->exp  = strtol_safe(row[12], 0);
	puser->sex  = strtol_safe(row[13], 0);
	puser->course  = strtol_safe(row[14], 0);
	sscanf(row[15], "%lf", &puser->power);  // beware, there is no default!
	puser->power_set_time  = strtolong_safe(row[16], 0);
	puser->gate_pos  = strtol_safe(row[17], 0);
	puser->fight_ai_time = strtol_safe(row[18], 0);
	puser->fight_gold_time = strtol_safe(row[19], 0);
	puser->fight_crystal_time = strtol_safe(row[20], 0);
	sprintf(puser->signals, "%.30s", row[21]);
	for (int i = 0; i < EVIL_SIGNAL_MAX; i++)
	{
		if (puser->signals[i] != '1')
		{
			puser->signals[i] = '0';
		}
	}
	puser->tower_pos = strtol_safe(row[22], 0);
	puser->tower_times = strtol_safe(row[23], 0);
	puser->tower_set_time = strtolong_safe(row[24], 0);
	puser->battle_coin = strtol_safe(row[25], 0);
	puser->arena_times = strtol_safe(row[27], 0);
	puser->arena_last_reward_time = strtol_safe(row[28], 0);
	sprintf(puser->signature, "%s", row[29]);
	if (puser->signature[0] == '\0') {
		strcpy(puser->signature, "_");
	}
	puser->chapter_pos = strtol_safe(row[30], 0);
	puser->monthly_end_date = strtolong_safe(row[31], 0);
		
	// DEBUG_PRINT(0, "get_status:tower_pos=%d tower_times=%d tower_set_time=%ld", puser->tower_pos, puser->tower_times, puser->tower_set_time);

	ret = 0; // ok to set ret = 0 here

cleanup:
	mysql_free_result(result);
	return ret;
}

int get_unread_message_count(MYSQL **pconn, char *q_buffer, int eid);

#define SQL_SET_POWER "UPDATE evil_status SET power=%lf,power_set_time=%ld WHERE eid=%d"
// inside function, ONLY call by update_power
int set_power(MYSQL ** pconn, char *q_buffer, int eid, double power)
{
	int ret;
	int len;
	int err;
	long now;

	if (power < 0) {
		BUG_PRINT(-5, "set_power:power_out_bound %lf", power);
		power = 0; // save it
	}

	now = time(NULL);

	len = sprintf(q_buffer, SQL_SET_POWER, power, now, eid);

	// DEBUG_PRINT(len, "set_power:g_query = %s", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		// more info on server log
		// ERROR_PRINT(-55, "buy_card:query=%s", q_buffer);
		ERROR_RETURN(-15, "set_power:mysql_errno %d", err);
		return -15;
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret <= 0) {
		ERROR_NEG_RETURN(-16, "set_power:update_error");
	}

	if (ret != 1) {
		ERROR_NEG_RETURN(-7, "set_power:impossible_error %d", ret);
	}

	ret = 0;

	return ret;
}

#define SQL_GET_POWER "SELECT eid,power,power_set_time FROM evil_status WHERE eid=%d LIMIT 1"
int update_power(MYSQL ** pconn, char *q_buffer, int eid, double power_offset)
{
	int ret, len;

	long yesterday;
	long now = time(NULL);
	double power = 0.0;
	long power_set_time = 0;
	double power_change = 0;

	len = sprintf(q_buffer, SQL_GET_POWER, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "update_power:mysql_errno %d", err);
	}

	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "update_power:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "update_power:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// eid, power, power_set_time
	if (field_count != 3 ) {
		ret = -7;
		ERROR_PRINT(ret, "update_power:field_count_mismatch %d", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!
	if (row == NULL) {
		ret = -17;
		ERROR_PRINT(ret, "update_power:null_result %d", eid);
		goto cleanup;
	}

	// assert row[0] == eid
	ret = strtol_safe(row[0], -1);
	if (ret != eid) {
		ret = -17;
		ERROR_PRINT(ret, "update_power:eid_mismatch %d %d", eid, ret);
		goto cleanup;
	}

	sscanf(row[1], "%lf", &power); // beware there is no default
	if (power < 0) {
		ret = -6;
		BUG_PRINT(ret, "update_power:power_BUG %d %lf", eid, power);
		goto cleanup;
	}

	power_set_time = strtol_safe(row[2], -1);
	if (power_set_time < 0) {
		ret = -6;
		BUG_PRINT(ret, "update_power:power_set_time_BUG %d %ld", eid, power_set_time);
		goto cleanup;
	}

	// DEBUG_PRINT(0, "update_power:power=%lf power_set_time=%ld", power, power_set_time);


	if (power == MAX_USER_POWER && power_offset == 0) {
		// no need to update power
		ret = 0;
		goto cleanup;
	}

	if (power > MAX_USER_POWER) {
		// avoid power recover
		power_set_time = now;
	}


	// 1.get now real power, check 24:00 refresh power
	// 2.real power add power_offset
	// 3.save power to db

	if (power_set_time > now) {
		BUG_PRINT(-66, "update_power:power_set_time_bug %d %ld %ld", eid, power_set_time, now);
		power_set_time = now; //let it update
	}
		
	power_change = (double)(now - power_set_time) / (double)POWER_RECOVER_SEC;
	// DEBUG_PRINT(0, "update_power:111power=%lf power_change=%lf", power, power_change);


	// daily refresh power
	yesterday = get_yesterday(time(NULL));
	// DEBUG_PRINT(0, "update_power:yesterday=%ld", yesterday);
	if (power_set_time <= yesterday) {
		// DEBUG_PRINT(0, "update_power:power_refersh %ld %ld", power_set_time, yesterday);
		power_change = MAX_USER_POWER;
	}
	

	if (power_change == 0 && power_offset == 0) {
		// early exit
		ret = 0;
		goto cleanup;
	}

	if (power < MAX_USER_POWER) {
		power += power_change;
		if (power > MAX_USER_POWER) {
			power = MAX_USER_POWER;
		}
	}
	// DEBUG_PRINT(0, "update_power:222power=%lf power_change=%lf", power, power_change);

	if (power + power_offset < 0) {
		ret = -2;
		ERROR_RETURN(ret, "update_power:power_nagetive %lf %lf", power, power_offset);
		goto cleanup;
	}

	power += power_offset;
	// DEBUG_PRINT(0, "update_power:333power=%lf power_offset=%lf", power, power_offset);
	// let power over MAX_USER_POWER;
	/*
	if (power > MAX_USER_POWER) {
		power = MAX_USER_POWER;
	}
	*/

	// DEBUG_PRINT(0, "update_power:444power=%lf", power);

	ret = set_power(pconn, q_buffer, eid, power);
	if (ret < 0) {
		BUG_PRINT(ret, "update_power:update_power_fail %d %lf", eid, power);
	}

	
	ret = power; // ok to set ret = 0 here

cleanup:
	mysql_free_result(result);
	return ret;
}


int in_status(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int unread_count;
	evil_user_t user;


	ret = sscanf(in_buffer, IN_STATUS_SCAN, &eid);
	if (ret != 1) {
		DB_ER_RETURN(ret, "status:invalid_input");
	}

	ret = update_power(pconn, q_buffer, eid, 0);
	if ( ret < 0 ) {
		// let it go
		BUG_PRINT(ret, "status:update_power_fail %d", eid);
	}

	ret = get_status(pconn, q_buffer, &user, eid);
	if ( ret < 0 ) {
		DB_ER_RETURN(-55, "status:db_err %d", ret);
	}

	unread_count = get_unread_message_count(pconn, q_buffer, eid);
	if ( unread_count < 0 ) {
		ERROR_PRINT(-65, "status:unread_message %d", unread_count);
		unread_count = 0;
	}

	// STATUS_ADD_FIELD
	sprintf(out_buffer, OUT_STATUS_PRINT, user.eid, user.lv, user.rating
	, user.gold, user.crystal, user.gid, user.gpos
	, user.game_count, user.game_win, user.game_lose
	, user.game_draw, user.game_run
	// , user.icon
	, user.exp, user.sex
	, user.fight_ai_time, user.fight_gold_time, user.fight_crystal_time
	, user.signals, user.monthly_end_date, user.signature, unread_count
	, user.power, user.power_set_time, user.gate_pos, user.chapter_pos
	, user.course);

	return 0;
}


#define SQL_CCCARD	"UPDATE evil_card SET c%d=%d WHERE eid=%d"
int in_cccard(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid, card_id, count;

	ret = sscanf(in_buffer, IN_CCCARD_SCAN, &eid, &card_id, &count);
	if (ret != 3) {
		DB_ER_RETURN(-5, "cccard:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "cccard:invalid_eid %d", eid);
	}
	if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
		DB_ER_RETURN(-25, "cccard:invalid_card_id %d", card_id);
	}
	if (count < 0 || count > 9) {
		DB_ER_RETURN(-35, "cccard:invalid_count %d", count);
	}

	len = sprintf(q_buffer, SQL_CCCARD, card_id, count, eid);
	printf("in_cccard:query=%s\n", q_buffer);
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "cccard:my_query %d", ret);
	}

	ret = mysql_affected_rows(*pconn); // must be 1 for success
	// 1 or 0 is ok, because the card may be there already
	if (ret < 0) {
		DB_ER_RETURN(-16, "cccard:update_error %d", ret);
	}

	db_output(out_buffer, ret, "%d %d", card_id, count);

	return 0;
}

time_t sqltime_to_time(const char *sqltime, time_t default_value)
{
    struct tm tm;
    time_t tt;
    int ret;

    bzero(&tm, sizeof(tm));  // 0 the time zone offset

    // strcpy(nowstr, "2015-07-18 12:27:31");
    ret = sscanf(sqltime, "%d-%d-%d %d:%d:%d"
    , &tm.tm_year, &tm.tm_mon, &tm.tm_mday
    , &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    if (ret != 6) {
        return default_value; // BUG case
    }

    tm.tm_year -= 1900;
    tm.tm_mon --;

    tt = mktime(&tm);

//  printf("sql=%s  time=%ld ctime=%s\n", sqltime, tt, ctime(&tt));
	if (tt < 0) {	tt = 0;	}

    return tt;
}

// unix time (second) -> pts ptimestamp format
// NOTE: this utility will be used in nio.cpp
long unix_to_pts(time_t now)
{
	struct tm tt;
	long pts;
	
	localtime_r(&now, &tt);
	pts = (long)(tt.tm_year-100) * 1000000000000L
	+ (long)(tt.tm_mon+1)	* 10000000000L
	+ (long)tt.tm_mday 	* 100000000L
	+ (long)tt.tm_hour 	* 1000000L
	+ (long)tt.tm_min 	* 10000L
	+ (long)tt.tm_sec 	* 100L;
	return pts;
}

// return a "near" unique timestamp YYMMDDhhiissxx
// where xx=00 to 99, if more than 99 calls to this function
// it return xx=99  : non-thread-safe
long ptimestamp()
{
	static long last_ts = 0;
	long pts;
	time_t now;
	
	now = time(NULL);
	pts = unix_to_pts(now);

	if (pts > last_ts) {
		last_ts = pts;
	} else {
		if ((last_ts % 100) >= 99) {
			pts = last_ts;
		} else {
			last_ts ++;
			pts = last_ts;
		}
	}
	
	return pts;
}


// unsigned integer + negative number == BUGBUG!!!
// example: AND c22+offset >= 0 AND c22 + offset <= 9
#define SQL_UPDATE_CARD   "UPDATE evil_card SET c%d=c%d+(%d) WHERE eid=%d AND CONVERT(c%d,SIGNED)+(%d)>=0 AND CONVERT(c%d,SIGNED)+(%d)<=9"
// AND c%d+%d>=0 AND c%d+%d<=9

int update_card(MYSQL **pconn, char *q_buffer, int eid, int cardid, int offset)
{
	int ret;
	int len;
	int err;

	if (eid <= 0) {
		ERROR_RETURN(-5, "update_card:invalid_eid %d", eid);
	}

	if (cardid <= 0 || cardid>EVIL_CARD_MAX) {
		ERROR_RETURN(-15, "update_card:invalid_cardid %d", cardid);
	}

	if (offset == 0) {
		WARN_PRINT(-2, "update_card:offset=%d", offset);
		return 0;
	}

	len = sprintf(q_buffer, SQL_UPDATE_CARD, cardid, cardid, offset, eid
	, cardid, offset, cardid, offset);

	// DEBUG_PRINT(len, "update_card:query = %s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "update_card:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret < 0) {
		// can be card number overflow or underflow
		ERROR_NEG_RETURN(-16, "update_card:update_error");
	}

	if (ret == 0) {
		WARN_PRINT(-2, "update_card:affect_rows=0");
		return 222;
	}

	if (ret != 1) {
		ERROR_NEG_RETURN(-7, "update_card:impossible_error %d", ret);
	}

	return 0;  // 0 means ok
}

#define SQL_UPDATE_CARD_LIST_START	"UPDATE evil_card SET "
#define SQL_UPDATE_CARD_LIST_ITEM	"c%d=IF((@_val:=CONVERT(c%d,SIGNED)+(%d))>9,9,@_val)"
#define SQL_UPDATE_CARD_LIST_END	" WHERE eid=%d"
int __update_card_list(MYSQL **pconn, char *q_buffer, int eid, int (*card_list)[2], int card_count)
{
	int ret;
	int len;
	int err;
	char *qptr;

	if (eid <= 0) {
		ERROR_RETURN(-5, "update_card_list:invalid_eid %d", eid);
	}
	if (card_count <= 0) {
		WARN_PRINT(-15, "update_card_list:card_count_negative %d", card_count);
		return 0;
	}

	for (int i = 0; i < card_count; i++) {
		if (card_list[i][0] <= 0 || card_list[i][0] > EVIL_CARD_MAX) {
			ERROR_RETURN(-15, "update_card:invalid_cardid %d", card_list[i][0]);
		}
	}

	qptr = q_buffer;
	qptr += sprintf(qptr, SQL_UPDATE_CARD_LIST_START);
	for (int i = 0; i < card_count; i++) {
		if (i > 0) {
			qptr += sprintf(qptr, "%s", ",");
		}
		qptr += sprintf(qptr, SQL_UPDATE_CARD_LIST_ITEM
		, card_list[i][0], card_list[i][0], card_list[i][1]);
	}
	qptr += sprintf(qptr, SQL_UPDATE_CARD_LIST_END, eid);
	len = qptr - q_buffer;
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "update_card_list:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret <= 0) {
		// can be card number overflow or underflow
		ERROR_NEG_RETURN(-16, "update_card_list:update_error");
	}

	if (ret != 1) {
		ERROR_NEG_RETURN(-7, "update_card_list:impossible_error %d", ret);
	}
	return 0;  // 0 means ok
}

// xcadd, xclist, xcreset
// ts, eid, cardid, count, gold, crystal, name
#define SQL_ADD_EXCHANGE	"INSERT INTO evil_exchange VALUES (%ld,%d,%d,%d,%d,%d,'%s')"

// this function exchange card, now useless
/*
// xcadd [cardid] [gold] [crystal] [name]
// note: eid is from connect_t, name is from g_card_list in nio
int in_add_exchange_card(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid, cardid;
	int count;
	count = 1;
	int gold = 979797, crystal=878787;
	char name[EVIL_ALIAS_MAX+5];	// card name assume has same len as alias
	char esc_name[EVIL_ALIAS_MAX*2 + 5];
	long ts;

	// non-thread-safe!  all exchange related DB must be in one-thread
	ts = ptimestamp();
	if ((ts % 100) >= 99) {
		DB_ER_RETURN(-2, "add_exchange:too_many_exchange");
	}

	ret = sscanf(in_buffer, IN_ADD_EXCHANGE_SCAN, &eid, &cardid, &count
	, &gold, &crystal, name);
	if (ret != 5) {
		DB_ER_RETURN(-5, "add_exchange:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "add_exchange:invalid_eid %d", eid);
	}
	if (cardid <= 0 || cardid > EVIL_CARD_MAX) {
		DB_ER_RETURN(-25, "add_exchange:invalid_cardid %d", cardid);
	}


	// reduce card by 1 
	ret = update_card(pconn, q_buffer, eid, cardid, -1);
	if (ret < 0) {
		DB_ER_RETURN(-6, "add_exchange:not_enough_cardid %d err %d"
		, cardid, ret);
	}

	// TODO update evil_card SET c%d=c%d-1 WHERE eid=%d
	len = mysql_real_escape_string(*pconn, esc_name, name, 
		strlen(name));
	len = sprintf(q_buffer, SQL_ADD_EXCHANGE, ts, eid, cardid, count, gold, crystal
	, esc_name);
	// DEBUG_PRINT(0, "in_add_xc:query=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "in_add_xc:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); // must be 2 for success
	if (ret <= 0) {
		// can be card number overflow or underflow
		ERROR_NEG_RETURN(-16, "in_add_xc:update_error");
	}

	if (ret != 1) {
		ERROR_NEG_RETURN(-7, "in_add_xc:impossible_error %d", ret);
	}

	return 0;
}
*/


#define SQL_UPDATE_PIECE	"UPDATE evil_piece SET c%d=IF(CONVERT(c%d,SIGNED)+(%d)>99,99,CONVERT(c%d,SIGNED)+(%d)) WHERE eid=%d AND CONVERT(c%d,SIGNED)+(%d)>=0"
int update_piece(MYSQL **pconn, char *q_buffer, int eid, int card_id, int num)
{

	int ret;
	int len;

	if (eid <= MAX_AI_EID) {
		ERROR_RETURN(-15, "update_piece:invalid_eid %d", eid);
	}

	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
		ERROR_RETURN(-25, "update_piece:invalid_card_id %d", card_id);
	}

	if (num > EVIL_NUM_PIECE_MAX || num < -EVIL_NUM_PIECE_MAX || num == 0) {
		ERROR_RETURN(-35, "update_piece:invalid_num %d", num);
	}


	len = sprintf(q_buffer, SQL_UPDATE_PIECE, card_id
	, card_id, num, card_id, num, eid, card_id, num);

	// DEBUG_PRINT(0, "q_buffer:%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "update_piece:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-16, "update_piece:affected_row %d %d %d \n"
		, eid, card_id, num);
	}

	ret = 0;
	return ret;
}

#define SQL_UPDATE_PIECE_LIST_START	"UPDATE evil_piece SET "
#define SQL_UPDATE_PIECE_LIST_ITEM	"c%d=IF((@_val:=CONVERT(c%d,SIGNED)+(%d))>99,99,@_val)"
#define SQL_UPDATE_PIECE_LIST_END	" WHERE eid=%d"
int __update_piece_list(MYSQL **pconn, char *q_buffer, int eid, int (*piece_list)[2], int piece_count)
{
	int ret;
	int len;
	char *qptr;

	if (eid <= MAX_AI_EID) {
		ERROR_RETURN(-15, "update_piece_list:invalid_eid %d", eid);
	}

	if (piece_count <= 0) {
		WARN_PRINT(-25, "update_piece_list:piece_count_0");
		return 0;
	}

	for (int i = 0; i < piece_count; i++) {
		if (piece_list[i][0] > EVIL_CARD_MAX || piece_list[i][0] <= 0) {
			ERROR_RETURN(-35, "update_piece_list:invalid_card_id %d"
			, piece_list[i][0]);
		}
	}

	qptr = q_buffer;
	qptr += sprintf(qptr, SQL_UPDATE_PIECE_LIST_START);
	for (int i = 0; i < piece_count; i++) {
		if (i > 0) {
			qptr += sprintf(qptr, "%s", ",");
		}
		qptr += sprintf(qptr, SQL_UPDATE_PIECE_LIST_ITEM
		, piece_list[i][0], piece_list[i][0], piece_list[i][1]);
	}
	qptr += sprintf(qptr, SQL_UPDATE_PIECE_LIST_END, eid);
	len = qptr - q_buffer;

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "update_piece_list:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret == 0)
	{
		WARN_PRINT(-2, "update_piece_list:affected_row_0");
		return 0;
	}
	if (ret != 1) {
		ERROR_RETURN(-16, "update_piece_list:affected_row %d"
		, eid);
	}

	ret = 0;
	return ret;
}

#define SQL_UPDATE_PIECE_ULT	"UPDATE evil_piece SET c%d=c%d+(%d) WHERE eid=%d AND CONVERT(c%d,SIGNED)+(%d)<=99 AND CONVERT(c%d,SIGNED)+(%d)>=0"
// when max_count=99, 98 + 5 = 99, 5 - 6 = 0
// return piece change count
// most time this function only handle addition, use update_piece() for subtraction
int update_piece_ultimate(MYSQL **pconn, char *q_buffer, int eid, int card_id, int num, int *change_count)
{

	int ret;
	int len;
	int count;

	if (eid <= MAX_AI_EID) {
		ERROR_RETURN(-15, "update_piece:invalid_eid %d", eid);
	}

	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
		ERROR_RETURN(-25, "update_piece:invalid_card_id %d", card_id);
	}

	if (num > EVIL_NUM_PIECE_MAX || num < -EVIL_NUM_PIECE_MAX || num == 0) {
		ERROR_RETURN(-35, "update_piece:invalid_num %d", num);
	}

	count = get_piece_count(pconn, q_buffer, eid, card_id);
	if (count < 0 || count > EVIL_NUM_PIECE_MAX) {
		ERROR_RETURN(-6, "update_piece:piece_count_bug %d", count);
	}

	if (count + num > EVIL_NUM_PIECE_MAX) {
		num = EVIL_NUM_PIECE_MAX - count;
	} else if (count + num < 0) {
		num = -count;
	}

	if (num == 0) {
		*change_count = 0;
		return 0;
	}

	len = sprintf(q_buffer, SQL_UPDATE_PIECE_ULT, card_id, card_id, num, eid
	, card_id, num, card_id, num);

	DEBUG_PRINT(0, "q_buffer:%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "update_piece:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-16, "update_piece:affected_row %d %d %d \n"
		, eid, card_id, num);
	}

	*change_count = num;
	ret = 0;
	return ret;
}

// this function exchange piece
// xcadd [cardid] [count] [gold] [crystal] [name]
// note: eid is from connect_t, name is from g_card_list in nio
int in_add_exchange_piece(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid, card_id;
	int count;
	int gold = 0, crystal=0;
	char name[EVIL_ALIAS_MAX+5];	// card name assume has same len as alias
	char esc_name[EVIL_ALIAS_MAX*2 + 5];
	long ts;

	// non-thread-safe!  all exchange related DB must be in one-thread
	ts = ptimestamp();
	if ((ts % 100) >= 99) {
		DB_ER_RETURN(-2, "add_exchange:too_many_exchange");
	}

	ret = sscanf(in_buffer, IN_ADD_EXCHANGE_SCAN, &eid, &card_id, &count
	, &gold, &crystal, name);
	if (ret != 6) {
		DB_ER_RETURN(-5, "add_exchange:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "add_exchange:invalid_eid %d", eid);
	}
	if (count <= 0 || count > EVIL_NUM_PIECE_MAX) {
		DB_ER_RETURN(-25, "add_exchange:invalid_count %d", count);
	}
	if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
		DB_ER_RETURN(-35, "add_exchange:invalid_card_id %d", card_id);
	}

	// 1.update piece
	// 2.add in evil_exchange

	// reduce piece
	ret = update_piece(pconn, q_buffer, eid, card_id, -count);
	if (ret < 0) {
		DB_ER_RETURN(-6, "%s %d", E_ADD_EXCHANGE_PIECE_NOT_ENOUGH, ret);
	}

	// TODO update evil_card SET c%d=c%d-1 WHERE eid=%d
	len = mysql_real_escape_string(*pconn, esc_name, name, 
		strlen(name));
	len = sprintf(q_buffer, SQL_ADD_EXCHANGE, ts, eid, card_id, count, gold, crystal
	, esc_name);
	// DEBUG_PRINT(0, "in_add_xc:query=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "in_add_xc:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); // must be 2 for success
	if (ret <= 0) {
		// can be card number overflow or underflow
		ERROR_NEG_RETURN(-16, "in_add_xc:update_error");
	}

	if (ret != 1) {
		ERROR_NEG_RETURN(-7, "in_add_xc:impossible_error %d", ret);
	}

	return 0;
}

#define SQL_GET_EXCHANGE	"SELECT * FROM evil_exchange WHERE xcid=%ld"

// from xcid -> exchange_t
// -6 : not exist
int get_exchange(MYSQL **pconn, char *q_buffer, exchange_t *exchange, long xcid)
{
	int ret;
	int len;
	int err;

	if (xcid <= 0) {
		ERROR_NEG_RETURN(-5, "get_exchange:invalid_xcid %ld", xcid);
	}
	
	len = sprintf(q_buffer, SQL_GET_EXCHANGE, xcid);
	DEBUG_PRINT(0, "get_exchange:q_buffer=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "get_exchange:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		ERROR_NEG_RETURN(-53, "get_exchange:null_result");
	}

	////////// NOTE: from here, we should use: 
	////////// 1. ERROR_PRINT + goto cleanup
	////////// do not use ERROR_XXX_RETURN() - this will mess up database

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;	// normal : for xcid not found (other people buy this!)
		ERROR_NEG_PRINT(ret, "get_exchange:num_row_zero %d", num_row);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_NEG_PRINT(ret, "get_exchange:null_row");
		goto cleanup; // cleanup and early exit
	}
	len = mysql_field_count(*pconn);

	if (len != 7) {
		ret = -57;
		ERROR_NEG_PRINT(ret, "get_exchange:field_count_mismatch %d", len);
		goto cleanup; // cleanup and early exit
	}
	// is it a must to check field_count _after_ fetch_row ???


	exchange->xcid = strtolong_safe(row[0], -1); 
	exchange->eid = strtol_safe(row[1], -2);
	exchange->cardid = strtol_safe(row[2], -3);
	exchange->count = strtol_safe(row[3], -3);
	exchange->gold = strtol_safe(row[4], 0);  // danger to use negative
	exchange->crystal = strtol_safe(row[5], 0);  // danger to use negative

cleanup:
	mysql_free_result(result);
	return ret;
}

// 1. update buyer cardid count++, update buyer money, seller money
// 2. update or delete evil_exchange, base on count
#define SQL_UPDATE_EXCHANGE	"UPDATE evil_exchange set count=%d where xcid=%ld"
#define SQL_DEL_EXCHANGE	"DELETE FROM evil_exchange WHERE xcid=%ld"

// pass-in positive  gold / crystal
// t2=buyer, t3=seller  : both buyer/seller gold/crystal will be update
// buyer card count will be updated  (seller card no need to update)
#define SQL_BUY_EXCHANGE "UPDATE evil_piece t1, evil_status t2, evil_status t3 SET t1.c%d=t1.c%d+%d, t2.gold=t2.gold-(%d), t2.crystal=t2.crystal-(%d), t3.gold=t3.gold+(%d), t3.crystal=t3.crystal+(%d) WHERE t2.gold-(%d)>=0 AND t2.crystal-(%d)>=0 AND t1.eid=t2.eid AND t1.eid=%d AND t1.c%d<99 AND t3.eid=%d"


int buy_exchange(MYSQL **pconn, char *q_buffer, int buyer_eid
, exchange_t *exchange, int count)
{
	int ret;
	int len;
	int seller_eid;

	seller_eid = exchange->eid;

	// 1.update buyer piece, seller money
	len = sprintf(q_buffer, SQL_BUY_EXCHANGE
	, exchange->cardid, exchange->cardid, count
	, exchange->gold * count, exchange->crystal * count
	, exchange->gold * count, exchange->crystal * count
	, exchange->gold * count, exchange->crystal * count
	, buyer_eid, exchange->cardid, seller_eid);

	// DEBUG_PRINT(0, "buy_xc:query=%s", q_buffer);


	ret = my_query(pconn, q_buffer, len);
	if (0 != ret) {
		ERROR_RETURN(-55, "buy_xc:mysql_errno %d", mysql_errno(*pconn));
	}

	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2

	if (ret == 0) {
		// -6 : xcid not exists
		// -16 : card count overflow (buyer)
		// -26 : money not enough (buyer)
		ERROR_RETURN(-26, "buy_xc:money_not_enough");
	}

	// should be 3 or 0?
	if (ret != 3) {
		ERROR_RETURN(-57, "buy_xc:affected_row wrong %d\n", ret);
	}


	// 2. check exchange.count > 0, if true, update count; if false, delete this row
	if (exchange->count - count > 0) {
		len = sprintf(q_buffer, SQL_UPDATE_EXCHANGE, exchange->count - count
		, exchange->xcid);
	} else {
		len = sprintf(q_buffer, SQL_DEL_EXCHANGE, exchange->xcid);
	}

	ret = my_query(pconn, q_buffer, len);
	if (0 != ret) {
		ERROR_RETURN(-57, "buy_xc:update_exchange errno %d", mysql_errno(*pconn));
	}
	ret = mysql_affected_rows(*pconn); // del must be 1
	if (1 != ret) {
		ERROR_RETURN(-67, "buy_xc:update_exchange row %d", ret);
	}

	return 0;
}

// -6 : xcid not exists  (other people buy this, when buyer viewing)
// -16 : piece count overflow (buyer)
// -26 : money not enough (buyer)
int in_buy_exchange_piece(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	// int len;
	long xcid;
	int eid; // buyer
	int count; 
	int piece_count;
	// int seller_eid; 
	exchange_t exchange;


	ret = sscanf(in_buffer, IN_BUY_EXCHANGE_SCAN, &eid, &xcid, &count);
	if (ret != 3) {
		DB_ER_RETURN(-5, "buy_exchange:invalid_input %d", ret);
	}


	bzero(&exchange, sizeof(exchange));
	ret = get_exchange(pconn, q_buffer, &exchange, xcid);
	if (ret < 0) {
		// logical error, record may be bought by other people
		// small chance
		DB_ER_RETURN(-6, "buy_xc:get_exchange %d", ret);
	}

	if (eid == exchange.eid) {
		// DB_ER_RETURN(-16, "buy_xc:cannot_buy_myself %d %d", eid, exchange.eid);
		DB_ER_RETURN(-16, "%s %d %d", E_BUY_EXCHANGE_CANNOT_BUY_MYSELF, eid, exchange.eid);
	}

	if (count > exchange.count) {
		// DB_ER_RETURN(-26, "buy_xc:buy_count_out_bound %d %d", count, exchange.count);
		DB_ER_RETURN(-26, "%s %d %d", E_BUY_EXCHANGE_BUY_TOO_MUCH, count, exchange.count);
	}

	// DEBUG_PRINT(ret, "buy_xc_xcid exchange: xcid=%ld  seller_eid=%d  cardid=%d  gold=%d  crystal=%d", exchange.xcid, exchange.eid, exchange.cardid, exchange.gold, exchange.crystal);
	

	// check can buy
	ret = get_piece_count(pconn, q_buffer, eid, exchange.cardid);
	ERROR_NEG_RETURN(ret, "buy_xc:get_piece_count %d", ret);
	piece_count = ret;

	if (piece_count + count > EVIL_NUM_PIECE_MAX) {
		// DB_ER_RETURN(-52, "buy_xc:piece_count_out_bound %d %d", piece_count, count);
		DB_ER_RETURN(-52, "%s %d %d", E_BUY_EXCHANGE_PIECE_OUT_BOUND, piece_count, count);
	}


	ret = buy_exchange(pconn, q_buffer, eid, &exchange, count);
	if (ret == -26) {
		// DB_ER_RETURN(ret, "buy_xc:money_not_enough");
		DB_ER_RETURN(ret, "%s", E_BUY_EXCHANGE_MONEY_NOT_ENOUGH);
	}
	if (ret < 0) {
		DB_ER_RETURN(ret, "buy_xc:unknown_error");
	}

	// positive number
	// buyer_eid seller_eid gold crystal
	// 
	db_output(out_buffer, 0, OUT_BUY_EXCHANGE_PRINT
	, eid, exchange.eid, exchange.cardid, count
	, exchange.gold, exchange.crystal);

	return ret ;
}


// %s=name_search_key   LIMIT %d,%d : start_id(0),page_size
// new entry on top (ORDER BY xcid DESC)
#define SQL_LIST_EXCHANGE "SELECT * FROM evil_exchange WHERE name LIKE '%%%s%%' ORDER BY xcid DESC LIMIT %d,%d"


//	xclist start_id page_size search_key
int in_list_exchange(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	// non-thread-safe!
	int ret;
	int len;
	int start_id, page_size;
	int count;
	char key[EVIL_ALIAS_MAX+5];	// card name assume has same len as alias
	char esc_key[EVIL_ALIAS_MAX*2+5];	
	char * ptr;


	key[0] = '\0';  // default empty key
	// ret==2 is ok for key=""
	ret = sscanf(in_buffer, IN_LIST_EXCHANGE_SCAN, &start_id, &page_size, key);
	if (ret < 2) {
		DB_ER_RETURN(-5, "list_xc:invalid_input");
	}
	if (start_id < 0) {  // 0 is valid
		DB_ER_RETURN(-15, "list_xc:invalid_start_id %d", start_id);
	}
	// hard code, max page size must be less than 50
	// 2900 / 50 = 58  
	// xcid(14) + eid(7) + gold+crystal(9) + cardid(4)=34
	if (page_size <= 0 || page_size >50) {
		DB_ER_RETURN(-25, "list_xc:invalid_page_size %d", page_size);
	}


	// escape the search_key
	len = mysql_real_escape_string(*pconn, esc_key, key, 
		strlen(key));
	
	len = sprintf(q_buffer, SQL_LIST_EXCHANGE, esc_key, start_id, page_size);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "list_xc:mysql_errno %d", err);
	}


	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "list_xc:null_result");
	}

	// use | as separator for each record
	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -13; // null case?
		DB_ER_PRINT(ret, "list_xc:negative_row %d", num_row);
		goto cleanup;
	}
	if (num_row==0) {  // normal case
		sprintf(out_buffer, OUT_LIST_EXCHANGE_PRINT, start_id, num_row);
		ret = 0;
		goto cleanup;
	}
	field_count = mysql_field_count(*pconn);
	// xcid, eid, cardid, count, gold, crystal, name
	if (field_count != 7) {
		ret = -17; // null case?
		DB_ER_PRINT(ret, "list_xc:invalid_field_count %d", field_count);
		goto cleanup;
	}

	// more than 1 returned
	len = sprintf(out_buffer, OUT_LIST_EXCHANGE_PRINT, start_id, num_row);
	ptr = out_buffer + len;
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-27, "list_xc:fetch_row_overflow %d", count);
			break;
		}
		// xcid
		len = sprintf(ptr, OUT_LIST_EXCHANGE_ROW_PRINT, row[0], row[1], row[2]
		, row[3], row[4], row[5]);
		ptr += len;
	}
	ret = 0;  // make sure ret is OK (0)


cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_GET_TIMEOUT_EXCHANGE "SELECT * FROM evil_exchange WHERE xcid<%ld"
// caller should free pointer after use!
exchange_t *get_timeout_exchange(MYSQL **pconn, char *q_buffer, long xcid, int *total)
{
	int ret;
	int len;
	int err;
	int count = 0;
	exchange_t * exchange = NULL;
	*total = 0; // default

	if (xcid <= 0) {
		*total = -5;
		ERROR_NEG_PRINT(-5, "get_timeout_exchange:invalid_xcid %ld", xcid);
		return NULL;
	}
	
	len = sprintf(q_buffer, SQL_GET_TIMEOUT_EXCHANGE, xcid);

	// DEBUG_PRINT(0, "get_timeout_exchange:q_buffer=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		*total = -55; 
		err = mysql_errno(*pconn);
		ERROR_NEG_PRINT(-55, "get_timeout_exchange:mysql_errno %d", err);
		return NULL;
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		*total = -53;
		ERROR_NEG_PRINT(-53, "get_timeout_exchange:null_result");
		return NULL;  
	}

	////////// NOTE: from here, we should use: 
	////////// 1. ERROR_PRINT + goto cleanup
	////////// do not use ERROR_xxx_RETURN() - this will mess up database
	len = mysql_field_count(*pconn);
	if (len != 7) {
		ret = -57;
		*total = -57;
		ERROR_NEG_PRINT(ret, "get_timeout_exchange:field_count_mismatch %d", len);
		goto cleanup; // cleanup and early exit
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		*total = 0; // normal case  (exchange may have no item older > 1 day)
		//ERROR_NEG_PRINT(ret, "get_timeout_exchange:num_row_zero %d", num_row);
		goto cleanup;
	}

	// exchange[N]  N = num_row
	exchange = (exchange_t*)malloc(num_row * sizeof(exchange_t));
	*total = num_row;

	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		exchange_t * ex = exchange + count;

		ex->xcid = strtolong_safe(row[0], -1); 
		ex->eid = strtol_safe(row[1], -2);
		ex->cardid = strtol_safe(row[2], -3);
		ex->count = strtol_safe(row[3], -3);
		ex->gold = strtol_safe(row[4], 0);  // danger to use negative
		ex->crystal = strtol_safe(row[5], 0);  // danger to use negative
		// DEBUG_PRINT(0, "get_timeout_exchange:exchange->xcid=%ld eid=%d cardid=%d gold=%d crysatl=%d", ex->xcid, ex->eid, ex->cardid, ex->gold, ex->crystal);
		count++;
		// TODO count overflow ?  check with num_row
	}

//	ret = num_row;

cleanup:
	mysql_free_result(result);
	return exchange;
}




// logic:
// @xcreset time_out (admin)
int in_reset_exchange(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int count;
	long pts;
//	long now_pts;
	exchange_t * exchange_list = NULL;
	
	ret = sscanf(in_buffer, IN_RESET_EXCHANGE_SCAN, &pts);
//	now_pts = ptimestamp();

	// DEBUG_PRINT(0, "reset_xc:pts=%ld  now_pts=%ld", pts, now_pts);

	// TODO : reset_exchange : timeout 
	// SELECT * FROM evil_exchange WHERE xcid < %ld (pts)
	// --- for each record (xcid, eid, cardid) 
	//    eid (seller) : update_card_offset ( +1 / -1)
	//    DELETE FROM evil_exchange WHERE xcid = %ld  
	// 

	// get_timeout_exchange
	if (pts <= 0) {
		DB_ER_NEG_RETURN(-15, "reset_xc:invalid_timeout %ld", pts);
	}
	// this is by malloc, remember to free !!!
	exchange_list = get_timeout_exchange(pconn, q_buffer, pts, &count);
	// DEBUG_PRINT(0, "reset_xc:get_timeout_exchange count=%d", count);

	if (exchange_list == NULL) {
		// 0 may be normal TODO make it normal
		DB_ER_NEG_RETURN(count, "reset_xc:get_timeout_exchange");
		db_output(out_buffer, count, "OK empty");
		INFO_PRINT(0, "@xcreset:ok count %d", count);
		return 0; // exchange_list is NULL, no free
	}


	for (int i=0; i<count; i++) {
		int change_count = 0;
		ret = update_piece_ultimate(pconn, q_buffer, exchange_list[i].eid
		, exchange_list[i].cardid, exchange_list[i].count, &change_count);

		// update_card may fail, normal  (when cardnum>=9)
		if (ret < 0) {
			WARN_PRINT(-6, "reset_xc:update_card_fail eid=%d cardid=%d"
			, exchange_list[i].eid, exchange_list[i].cardid);
		}

		// delete the evil_exchange record, base on xcid
		len = sprintf(q_buffer, SQL_DEL_EXCHANGE, exchange_list[i].xcid);
		// DEBUG_PRINT(0, "reset_xc:q_buffer del = %s", q_buffer);
		ret = my_query(pconn, q_buffer, len);
		if (0 != ret) {
			ERROR_PRINT(-57, "reset_xc:del_exchange errno %d", mysql_errno(*pconn));
			goto cleanup;
		}
		ret = mysql_affected_rows(*pconn); // del must be 1
		if (1 != ret) {
			ERROR_PRINT(-67, "reset_xc:del_exchange row %d", ret);
			goto cleanup;
		}

	}
	db_output(out_buffer, count, "OK");
	INFO_PRINT(0, "@xcreset:ok count %d", count);

cleanup:
	if (exchange_list != NULL) {
		free(exchange_list);
		exchange_list = NULL;
	}

	return 0;
}




/////////////////////////////
	
// for create_guild:  gid=master_eid, gpos=1(master), eid=master_eid
// also, check_gid = 0
// for apply/update gpos:  gid=gid,  check_gid=gid
#define SQL_UPDATE_STATUS_GUILD	"UPDATE evil_status SET gid=%d,gpos=%d WHERE eid=%d AND gid=%d AND gpos=%d"

int update_status_guild(MYSQL **pconn, char *q_buffer, int gid, int gpos
, int eid, int check_gid, int check_pos) {
	int len;
	int ret;
	if (gid < 0) {
		ERROR_RETURN(-15, "update_status_guild:invalid_gid %d", gid);
	}
	// gid > 0 : then gpos should be >= 1
	// gid==0 and gpos==0 OK, kick out of guild
	if ((gid > 0 && gpos <= 0) || (gid==0 && gpos!=0)) {
		ERROR_RETURN(-25, "update_status_guild:invalid_gpos gid,gpos %d %d"
		, gid, gpos);
	}
	if (eid <= 0) {
		ERROR_RETURN(-35, "update_status_guild:invalid_eid %d", eid);
	}

	len = sprintf(q_buffer, SQL_UPDATE_STATUS_GUILD, gid, gpos, eid
	, check_gid, check_pos);

	// DEBUG_PRINT(0, "update_status_guild:query=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "update_status_guild:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); 
	// should be 1
	if (ret != 1) {
		ERROR_RETURN(-65, "update_status_guild:affected_row %d", ret);
	}

	return 0;
}


#define SQL_CHECK_GUILD_NAME	"SELECT gid FROM evil_guild WHERE gname='%s' LIMIT 1"

// check the name of the guild exists? 
// check the master eid has gid == 0 or not?
// if both ok, return 0
// return -3 : eid not found
// return -6 : master gid != 0
// return -2 : gname duplicate
int check_create_guild(MYSQL **pconn, char *q_buffer, int eid, const char *gname)
{
	int ret;
	int len;
	evil_user_t user;
	char esc_gname[EVIL_ALIAS_MAX * 2 + 5];	
	
	// part1: check master gid != 0 
	ret = get_status(pconn, q_buffer, &user, eid);
	if (ret < 0 ) {
		ERROR_RETURN(-3, "check_create_guild:eid_not_found");
	}

	if (user.gid != 0) {
		// this is not an error ?  maybe warning later
		ERROR_RETURN(-6, "check_create_guild:already_has_guild");
	}

	// part2: check gname duplicate
	len = mysql_real_escape_string(*pconn, esc_gname, gname, 
		strlen(gname));
	len = sprintf(q_buffer, SQL_CHECK_GUILD_NAME, esc_gname);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "check_create_guild:mysql_errno %d", err);
	}
	

	// retrieve the data
	MYSQL_RES *result;
	int num_row;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-53, "check_create_guild:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row>=1) {
		ret = -22;
		ERROR_PRINT(ret, "check_create_guild:gname_duplicate %d", num_row);
		goto cleanup;
	}


	ret = 0; // ok to set ret = 0 here

cleanup:
	mysql_free_result(result);
	return ret;
}


// eid, gid, gshare, last_bonus
#define SQL_INSERT_GUILD_SHARE "INSERT INTO evil_guild_share VALUES (%d,%d,%lf,FROM_UNIXTIME(%ld))"


// like evil_card, insert only once, should never replace
int insert_guild_share(MYSQL **pconn, char *q_buffer, int gid, int eid
, double gshare, time_t last_bonus)
{
	int len;
	int ret;
	
	len = sprintf(q_buffer, SQL_INSERT_GUILD_SHARE, eid, gid, gshare
	, last_bonus);
	// DEBUG_PRINT(0, "insert_guild_share:query=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "insert_guild_share:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); // must be 2 for success
	if (ret <= 0) {
		// can be card number overflow or underflow
		ERROR_NEG_RETURN(-16, "insert_guild_share:update_error");
	}

	if (ret != 1) {
		ERROR_NEG_RETURN(-7, "insert_guild_share:impossible_error %d", ret);
	}

	return 0;
}


// gid, eid (gid is for safety)
#define SQL_DELETE_GUILD_SHARE	"DELETE FROM evil_guild_share WHERE gid=%d AND eid=%d"

int delete_guild_share(MYSQL **pconn, char *q_buffer, int gid, int eid)
{
	int len;
	int ret;
	
	len = sprintf(q_buffer, SQL_DELETE_GUILD_SHARE, gid, eid);
	// DEBUG_PRINT(0, "delete_guild_share:query=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "delete_guild_share:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); // must be 2 for success
	if (ret <= 0) {
		// can be card number overflow or underflow
		ERROR_NEG_RETURN(-16, "delete_guild_share:update_error");
	}

	if (ret != 1) {
		ERROR_NEG_RETURN(-7, "delete_guild_share:impossible_error %d", ret);
	}

	return 0;
}


// eid, gid, gold, crystal
#define SQL_SAVE_GUILD_DEPOSIT "INSERT INTO evil_guild_deposit VALUES(NOW(),%d,%d,%d,%d)"

int save_guild_deposit(MYSQL **pconn, char *q_buffer, int eid, int gid
, int gold, int crystal) 
{
	int ret;
	int len;

	if (eid <= 0) {
		ERROR_RETURN(-15, "save_guild_deposit:invalid_eid %d", eid);
	}
	if (gid <= 0) {
		ERROR_RETURN(-25, "save_guild_deposit:invalid_gid %d", gid);
	}
	if (gold < 0) {
		ERROR_RETURN(-35, "save_guild_deposit:invalid_gold %d", gold);
	}
	if (crystal < 0) {
		ERROR_RETURN(-45, "save_guild_deposit:invalid_crystal %d", crystal);
	}

	len = sprintf(q_buffer, SQL_SAVE_GUILD_DEPOSIT, eid, gid, gold, crystal);
	// DEBUG_PRINT(0, "query: %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-65, "save_guild_deposit:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret != 1) {
		ERROR_RETURN(-7, "save_guild_deposit:affected_row wrong %d\n", ret);
	}

	return 0;
}


// gid(NULL), total_member, glevel, master, gold, crystal, gname, gnotice
#define SQL_CREATE_GUILD	"INSERT INTO evil_guild VALUES (%d,%d,%d,%d,%d,'%s', '_')"


// gcreate [gname]
// gcreate [gid] [gold(-)] [crystal(-)] [gname]
int in_create_guild(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	int gold;
	int crystal;
	char gname[EVIL_ALIAS_MAX+5];	
	char esc_gname[EVIL_ALIAS_MAX*2 + 5];	


	ret = sscanf(in_buffer, IN_CREATE_GUILD_SCAN, &eid
	, &gold, &crystal, gname);
	if (ret != 4) {
		DB_ER_RETURN(-5, "create_guild:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "create_guild:invalid_eid %d", eid);
	}
	if (gold > 9999999 || crystal > 9999999) {
		DB_ER_RETURN(-25, "create_guild:invalid_money %d %d", gold, crystal);
	}


	// TODO check master gid==0 in evil_status
	// and gname not duplicate in evil_guild

	// gname is in plain format: do not pass esc_gname
	ret = check_create_guild(pconn, q_buffer, eid, gname) ;

	switch(ret) {
		case -3: // eid_not_found
		// DB_ER_NEG_RETURN(ret, "create_guild:eid_not_found %d", eid);
		DB_ER_NEG_RETURN(ret, "%s %d", E_CREATE_GUILD_EID_NOT_FOUND, eid);
		case -6: // already has guild
		// DB_ER_NEG_RETURN(ret, "create_guild:already_has_guild");
		DB_ER_NEG_RETURN(ret, "%s", E_CREATE_GUILD_ALREADY_HAS_GUILD);
		case -22: // dup gname
		// DB_ER_NEG_RETURN(ret, "create_guild:duplicate_gname");
		DB_ER_NEG_RETURN(ret, "%s", E_CREATE_GUILD_DUPLICATE_GUILD_NAME);
		default:
		DB_ER_NEG_RETURN(ret, "%s", E_CREATE_GUILD_OTHER_ERROR);
	}

	// part1 : update master money
	// ret = update_money(pconn, q_buffer, eid, -gold, -crystal);
	ret = update_money(pconn, q_buffer, eid, -gold, -crystal, EVIL_BUY_CREATE_GUILD, 1, "create_guild");
	if (ret < 0) {
		// member not enough money or other db error
		// DB_ER_NEG_RETURN(-2, "create_guild:not_enough_money %d", ret);
		DB_ER_NEG_RETURN(-2, "%s %d", E_CREATE_GUILD_MONEY_NOT_ENOUGH, ret);
	}

	// ret = record_buy(pconn, q_buffer, eid, EVIL_BUY_CREATE_GUILD, 1, -gold, -crystal, "create_guild");

	// part2 : update evil_status : set gid=my_eid,  gpos=master
	// check_pos=0 OK
	ret = update_status_guild(pconn, q_buffer, eid, GUILD_POS_MASTER, eid
	, 0, 0);
	if (ret != 0) {
		DB_ER_NEG_RETURN(-26, "create_guild:update_status_error %d", ret);
	}


	// part3: add record in evil_guild
	len = mysql_real_escape_string(*pconn, esc_gname, gname, 
		strlen(gname));
	// TODO input init gold
	len = sprintf(q_buffer, SQL_CREATE_GUILD, eid, 1, 1, gold, 0, esc_gname);
	// DEBUG_PRINT(0, "create_guild:query=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		DB_ER_RETURN(-75, "create_guild:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		DB_ER_NEG_RETURN(-77, "create_guild:impossible_error %d", ret);
	}

	// part4: add master deposit, money is create guild money
	ret = save_guild_deposit(pconn, q_buffer, eid, eid, gold, 0);
	if (ret < 0) {
		DB_ER_NEG_RETURN(-47, "create_deposit:save_guild_deposit:%d", ret);
	}

	// part5: insert_guild_share
	// last_bonus=0 : so we have bonus on the first day, if we use now, no bonus
	// on the creation date
	ret = insert_guild_share(pconn, q_buffer, eid, eid, 1.0, 0);   // gid==eid
	if ( ret < 0) { 
		DB_ER_NEG_RETURN(-85, "create_guild:insert_guild_share %d", ret);
	}

	sprintf(out_buffer, OUT_CREATE_GUILD_PRINT, eid, (-gold), (-crystal), gname);

	return 0;
}


// gpos < 9 is normal member
#define SQL_LIST_GMEMBER	"SELECT evil_status.eid, gpos, evil_user.alias, evil_user.icon, evil_status.rating, UNIX_TIMESTAMP(evil_user.last_login), IFNULL(evil_guild_share.gshare, 0), evil_status.lv FROM evil_status LEFT JOIN evil_user ON evil_status.eid = evil_user.eid LEFT JOIN evil_guild_share ON evil_status.eid = evil_guild_share.eid WHERE evil_status.gid=%d AND %s ORDER BY gpos ASC, last_login DESC LIMIT %d, %d"

// new glist with or without apply
// CMD: glist [flag] [start_id] [page_size] [optional_gid] 
// flag = 0 : all member include apply
// flag = 1 : all member without apply
// flag = 9 : all apply 
// RET: glist [flag] [start_id] [total] [member_info1] [member_info2] ...
// member_info = eid pos alias +icon +rating last_login gshare (lv)
//
// SQL : WHERE gid=%d AND %s ORDER BY gpos ASC ...
// %s is the range
// flag = 0: %s = gpos>=0
// flag = 1: %s = gpos < 9
// flag = 9: %s = gpos = 9
int in_guild_lmember(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int flag;
	int start_id;
	int page_size;
	int gid;
	const char *range;
	int total;
	char *ptr;
	const char *gshare;

	ret = sscanf(in_buffer, IN_LIST_GMEMBER_SCAN, &flag, &start_id
	, &page_size, &gid);
	if (ret != 4) {
		DB_ER_RETURN(-5, "list_gmember:invalid_input %d", ret);
	}
	if (gid <= 0) {
		DB_ER_RETURN(-15, "list_gmember:invalid_gid %d", gid);
	}
	if (flag != 0 && flag != 1 && flag != 9) {
		DB_ER_RETURN(-25, "list_gmember:invalid_flag %d", flag);
	}

	if (start_id < 0) {  // 0 is valid
		DB_ER_RETURN(-35, "list_gmember:invalid_start_id %d", start_id);
	}

	if (page_size <= 0 || page_size >50) {
		DB_ER_RETURN(-45, "list_gmember:invalid_page_size %d", page_size);
	}

	switch(flag) {
	case 0:
		range = "gpos>=0";
		break;
	case 1:
		range = "gpos<9";
		break;
	case 9:
		range = "gpos=9";
		break;
	}

	len = sprintf(q_buffer, SQL_LIST_GMEMBER, gid, range, start_id, page_size);

	// DEBUG_PRINT(0, "in_list_gmember:q_buffer=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (0 != ret) {
		DB_ER_RETURN(-55, "list_gmember:mysql_errno %d", mysql_errno(*pconn));
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "list_gmember:null_result");
	}
	////////// NOTE: from here, we should use: 
	////////// 1. db_output()  + goto cleanup   or
	////////// 2. DB_ERR_PRINT() + goto cleanup
	////////// do not use DB_ERR_RETURN() - this will mess up database

	len = mysql_field_count(*pconn);
	if (8!=len) {
		ret = -67;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "list_gmember:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -3;	// populer error, means no such guild
		DB_ER_PRINT(ret, "list_gmember:null_guild %d", num_row);
		goto cleanup;
	}

	len = sprintf(out_buffer, OUT_LIST_GMEMBER_PRINT, flag, start_id, num_row);
	ptr = out_buffer + len;

	// more than 1 rows is normal
	total = 0;  // later: check total == num_row
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		total ++;
		if (total > num_row) {
			BUG_PRINT(-77, "list_gmember:total_overflow %d %d", total, num_row);
			break;
		}
		
		gshare = row[6];
		if (gshare == NULL) {
			gshare = "0";
		}
		// row[0]=eid, row[1]=gpos, row[2]=alias, row[3]=icon, row[4]=rating
		// , row[5]=last_login, row[6]=gshare, row[7]=lv
		ptr += sprintf(ptr, OUT_LIST_GMEMBER_ROW_PRINT, row[0], row[1], row[2], row[3]
		, row[4], row[5], gshare, row[7]);
	}

	ret = 0; // good for here

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;

}


#define SQL_LIST_GUILD	"SELECT eg.gid,eg.total_member,eg.glevel,eg.gold,eg.crystal,eg.gname,eu.alias FROM evil_guild eg LEFT JOIN evil_user eu ON eg.gid=eu.eid %s ORDER BY eg.glevel DESC,eg.total_member DESC,eg.gold DESC,eg.crystal DESC LIMIT %d,%d"


// How to get total member from evil_status
// select evil_guild.*, count(evil_status.eid) from evil_guild left join  evil_status on evil_guild.gid = evil_status.gid and evil_status.gpos!=9 group by evil_status.gid;
// CMD: lguild start_id page_size [key_optional]
// RET: lguild start_id page_size [g_info1] [g_info2] ...
// g_info = gid total_member glevel gold crystal gname leader_name
int in_list_guild(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int start_id, page_size;
	int total;
	char *ptr;
	char key[EVIL_ALIAS_MAX + 5] = {'\0'};  // optional search key
	char esc_key[EVIL_ALIAS_MAX*2 + 25] = {'\0'};
	// +25 for : WHERE gname LIKE '%x%'


	ret = sscanf(in_buffer, IN_LIST_GUILD_SCAN, &start_id, &page_size, key);
	if (ret < 2) {
		DB_ER_RETURN(-5, "list_guild:invalid_input %d", ret);
	}

	if (start_id < 0) { 
		DB_ER_RETURN(-15, "list_guild:invalid_start_id %d", start_id);
	}
	if (page_size <= 0 || page_size > 50) { 
		DB_ER_RETURN(-25, "list_guild:invalid_page_size %d", page_size);
	}

	if (strlen(key)>0) {
		len = sprintf(esc_key, "WHERE eg.gname LIKE '%%");
		
		len = mysql_real_escape_string(*pconn, esc_key+len, key, 
			strlen(key));
		strcat(esc_key, "%'");
	}

	len = sprintf(q_buffer, SQL_LIST_GUILD, esc_key, start_id, page_size);
//	DEBUG_PRINT(0, "lguild SQL: %s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (0 != ret) {
		DB_ER_RETURN(-55, "list_guild:mysql_errno %d", mysql_errno(*pconn));
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "list_guild:null_result");
	}
	////////// NOTE: from here, we should use: 
	////////// 1. db_output()  + goto cleanup   or
	////////// 2. DB_ERR_PRINT() + goto cleanup
	////////// do not use DB_ERR_RETURN() - this will mess up database

	len = mysql_field_count(*pconn);
	if (7!=len) {
		ret = -67;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "list_guild:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		// this is normal: last page does not have a clear indication
		// return start_id,0
		ret = 0;
		sprintf(out_buffer, OUT_LIST_GUILD_PRINT, start_id, 0);
		goto cleanup;
	}

	// return page_size is the actual number of rows returned from db
	len = sprintf(out_buffer, OUT_LIST_GUILD_PRINT, start_id, num_row);
	ptr = out_buffer + len;

	// more than 1 rows is normal
	total = 0;  // later: check total == num_row
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		total ++;
		if (total > num_row) {
			BUG_PRINT(-77, "list_guild:total_overflow %d %d"
			, start_id, num_row);
			break;
		}
		
		// row[0]=gid, row[1]=total_member, row[2]=glevel, row[3]=gold
		// row[4]=crystal, row[5]=gname row[6]=leader_name
		ptr += sprintf(ptr, OUT_LIST_GUILD_ROW_PRINT, row[0], row[1], row[2]
		, row[3], row[4], row[5], row[6]);
	}

	ret = 0; // good for here

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}

#define SQL_CHECK_GUILD_EXIST	"SELECT gid,gname FROM evil_guild WHERE gid=%d LIMIT 1"

// check_guild_exist:
// return 0:  not exist
// return 1:  exist
// return negative for error (assume not exist)
// param: gname is output parameter
int check_guild_exist(MYSQL **pconn, char *q_buffer, int gid, char *gname)
{
	int ret;
	int len;

	
	if (gid <= 0) {
		ERROR_NEG_RETURN(-5, "check_guild_exist:invalid_gid %d", gid);
	}
	
	len = sprintf(q_buffer, SQL_CHECK_GUILD_EXIST, gid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "check_guild_exist:mysql_errno %d", err);
	}
	
	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-53, "check_guild_exist:null_result");
	}

	num_row = mysql_num_rows(result);
	ret = num_row;  // 0 not exists, 1 exists,  -ve error

	strcpy(gname, "_no_guild"); // default is no guild
	row = mysql_fetch_row(result);
	if (row != NULL) {
		// row[0]=gid, row[1]=gname
		sscanf(row[1], "%30s", gname);
	}

// cleanup:
	mysql_free_result(result);
	return ret;
}


// CMD: gapply [gid]
// RET: gapply 0 OK 
// ERR: gapply -6 already_has_guild
// ERR: gapply -3 guild_not_exist
// ERR: gapply -9 not_login 
int in_guild_apply(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int gid;  // core
	int eid;  // core
	char gname[EVIL_ALIAS_MAX + 5];

	
	ret = sscanf(in_buffer, IN_GUILD_APPLY_SCAN, &gid, &eid);
	if (ret != 2) {
		DB_ER_RETURN(-5, "guild_apply:invalid_input");
	}
	if (gid <= 0) {
		DB_ER_RETURN(-15, "guild_apply:invalid_gid %d", gid);
	}
	if (eid <= 0) {
		DB_ER_RETURN(-25, "guild_apply:invalid_eid %d", eid);
	}


	// TODO check guild exists : gid   return 0 OK
	ret = check_guild_exist(pconn, q_buffer, gid, gname);
	if (ret <= 0) {
		// DB_ER_RETURN(-3, "guild_apply:guild_not_exist %d", gid);
		DB_ER_RETURN(-3, "%s %d", E_GUILD_APPLY_GUILD_NOT_EXIST, gid);
	}

	// check_pos=0 OK
	ret = update_status_guild(pconn, q_buffer, gid, GUILD_POS_APPLY, eid
	, 0, 0);
	// -6 already_has_guild
	// 
	if (ret < 0) {
		// sprintf(out_buffer, "%d %s %d", -6, "already_has_guild", ret);
		sprintf(out_buffer, "%d %s %d", -6, E_GUILD_APPLY_ALREADY_HAS_GUILD, ret);
		ret = -6; // order is important
	} else {
		sprintf(out_buffer, OUT_GUILD_APPLY_PRINT, eid, gid
		, GUILD_POS_APPLY, gname);
	}

	return ret;
}

#define SQL_GUILD_MEMBER_CHANGE	"UPDATE evil_guild SET total_member=total_member+(%d) WHERE gid=%d AND total_member+(%d)<=%d"

int guild_member_change(MYSQL **pconn, char *q_buffer, int gid, int offset
, int member_max)
{
	int ret, len;

	if (gid <= 0) {
		ERROR_RETURN(-15, "guild_member_change:invalid_gid %d", gid);
	}
	if (offset!=-1 && offset!=1) {
		ERROR_RETURN(-25, "guild_member_change:invalid_offset %d", offset);
	}
	if (member_max <= 0) {
		ERROR_RETURN(-35, "guild_member_change:invalid_member_max %d", member_max);
	}

	len = sprintf(q_buffer, SQL_GUILD_MEMBER_CHANGE, offset, gid
	, offset, member_max);
		

	// DEBUG_PRINT(0, "guild_member_change:query=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "guild_member_change:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); 
	// should be 1,  may be guild not exists
	if (ret != 1) {
		ERROR_RETURN(-65, "guild_member_change:affected_row %d", ret);
	}
	return 0;
}



// pos: 9->3
// need update evil_guild.total_member++
// return -2 : MAX_GUILD_MEMBER overflow or db error
// return -6 : update_status_guild logic error
int guild_approve(MYSQL **pconn, char *q_buffer, int gid, int eid, int pos
, int member_max)
{
	// need to check MAX_GUILD_MEMBER ?
	int ret;

	// 2 to 3 only
	if (pos < GUILD_POS_SENIOR || pos > GUILD_POS_MEMBER) {
		ERROR_RETURN(-25, "guild_approve:invalid_pos %d", pos);
	}

	ret = guild_member_change(pconn, q_buffer, gid, 1, member_max); // offset
	if (ret < 0) {
		ERROR_RETURN(-2, "guild_approve:total_member_overflow %d", ret);
	}

	// core logic:  if fail, need to reduce the total_member
	// check_pos=9 OK
	ret = update_status_guild(pconn, q_buffer, gid, pos
	, eid, gid, GUILD_POS_APPLY);

	if (ret < 0) {
		// rollback total_member
		guild_member_change(pconn, q_buffer, gid, -1, member_max); // offset
		// no need to handle error in rollback

		ERROR_RETURN(-6, "guild_approve:update_status_guild %d", ret);
	}

	ret = insert_guild_share(pconn, q_buffer, gid, eid, 0.0, 0);
	if (ret < 0) {
		ERROR_RETURN(-85, "guild_approve:insert_guild_share %d", ret);
	}
	
	return 0;
}

// pos: 3->2,  2->3
// return -6: update_status_guild logic error
int guild_promote(MYSQL **pconn, char *q_buffer, int gid, int eid, int pos)
{
	int ret;
	int check_pos;

	if (gid <= 0) {
		ERROR_RETURN(-15, "guild_promote:invalid_gid %d", gid);
	}
	if (eid <= 0) {
		ERROR_RETURN(-25, "guild_promote:invalid_eid %d", eid);
	}
	if (pos != GUILD_POS_SENIOR && pos != GUILD_POS_MEMBER) {
		ERROR_RETURN(-35, "guild_promote:invalid_pos %d", pos);
	}

	if (pos == GUILD_POS_SENIOR) {
		check_pos = GUILD_POS_MEMBER;
	} else {
		check_pos = GUILD_POS_SENIOR;
	}

	// core logic:  if fail, need to reduce the total_member
	ret = update_status_guild(pconn, q_buffer, gid, pos
	, eid, gid, check_pos);

	if (ret < 0) {
		ERROR_RETURN(-6, "guild_promote:update_status_guild %d", ret);
	}

	return ret;
}



// CMD: gpos [eid] [pos]
// RET: gpos [eid] [pos]   // note: for refresh the member list UI

// if original pos is 9 and new_pos>0 and <9, need to update 
// evil_guild.total_member++
// if original pos is >0 and <9,  new_pos=0 : need 

int in_guild_pos(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int gid;  // core
	int pos;
	int eid;  // core
	int member_max;  // core
	evil_user_t user;


	ret = sscanf(in_buffer, IN_GUILD_POS_SCAN, &eid, &pos, &gid, &member_max);

	if (ret != 4) {
		DB_ER_RETURN(-5, "guild_pos:invalid_input %d", ret);
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "guild_pos:invalid_eid %d", eid);
	}
	// valid pos:  2, 3 
	if (pos < GUILD_POS_SENIOR || pos > GUILD_POS_MEMBER) {
		DB_ER_RETURN(-25, "guild_pos:invalid_pos %d", pos);
	}
	if (gid <= 0) {
		DB_ER_RETURN(-35, "guild_pos:invalid_gid %d", gid);
	}

	if (member_max <= 0) {
		DB_ER_RETURN(-45, "guild_pos:invalid_member_max %d", member_max);
	}

	ret = get_status(pconn, q_buffer, &user, eid);
	if (ret < 0) {
		DB_ER_RETURN(-3, "guild_pos:eid_not_found %d", eid);
	}

	if (user.gpos == GUILD_POS_MASTER) {
		DB_ER_RETURN(-9, "guild_pos:master_cannot_change_pos %d", eid);
	}


	// make sure guild_approve and guild_promote return error
	// are compatabile
	if (user.gpos == GUILD_POS_APPLY) {
		ret = guild_approve(pconn, q_buffer, gid, eid, pos, member_max);
	} else {
		ret = guild_promote(pconn, q_buffer, gid, eid, pos);
	}

	if (ret < 0) {
		if (ret == -2) {
			// DB_ER_RETURN(ret, "total_member_overflow");
			DB_ER_RETURN(ret, "%s", E_GUILD_POS_TOTAL_MEMBER_OVERFLOW);
		}
		// other error
		// DB_ER_RETURN(ret, "guild_pos:logic_error");
		DB_ER_RETURN(ret, "%s", E_GUILD_POS_OTHER_ERROR);
	}

	sprintf(out_buffer, OUT_GUILD_POS_PRINT, eid, pos, gid);
	return 0;
}


// CMD: gquit  (self quit)
// RET: gquit eid
// ERR: gquit -6 no_guild
int in_guild_quit(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int gid;  // core
	int eid;
	int member_max;
	evil_user_t user;

	ret = sscanf(in_buffer, IN_GUILD_QUIT_SCAN, &eid, &gid, &member_max);
	if (ret != 3) {
		DB_ER_RETURN(-5, "guild_quit:invalid_input %d", ret);
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "guild_quit:invalid_eid %d", eid);
	}

	if (gid <= 0) {
		DB_ER_RETURN(-25, "guild_quit:invalid_gid %d", gid);
	}

	if (member_max < 0) {
		DB_ER_RETURN(-45, "guild_quit:invalid_member_max %d", member_max);
	}


	ret = get_status(pconn, q_buffer, &user, eid);
	if ( ret < 0 ) {
		DB_ER_RETURN(-55, "guild_quit:eid_not_found %d", ret);
	}

	if (gid != user.gid) {
		// DB_ER_RETURN(-16, "guild_quit:eid_not_in_guild %d %d", user.gid, gid);
		DB_ER_RETURN(-16, "%s %d %d", E_GUILD_QUIT_NOT_IN_GUILD, user.gid, gid);
	}

	if (user.gpos == GUILD_POS_MASTER) {
		// DB_ER_RETURN(-9, "guild_quit:master_cannot_quit %d", gid);
		DB_ER_RETURN(-9, "%s %d", E_GUILD_QUIT_MASTER_CANNOT_QUIT, gid);
	}
	

	// part1: update status
	ret = update_status_guild(pconn, q_buffer, 0, GUILD_POS_NONE, eid
	, gid, user.gpos);

	if (ret != 0) {
		DB_ER_NEG_RETURN(-6, "guild_quit:update_status_error %d", ret);
	}

	if (user.gpos != GUILD_POS_APPLY) {
		// part2: delete_guild_share
		ret = delete_guild_share(pconn, q_buffer, gid, eid);
		if (ret != 0) {
			DB_ER_NEG_RETURN(-85, "guild_quit:delete_guild_share %d", ret);
		}
	
		// part3: update total_member in evil_guild
		ret = guild_member_change(pconn, q_buffer, gid, -1, member_max); // offset
		if (ret < 0) {
			DB_ER_RETURN(-2, "guild_quit:total_member_overflow %d", ret);
		}
	}

	sprintf(out_buffer, OUT_GUILD_QUIT_PRINT, eid, gid);

	ret = 0;
	return ret;
}



#define SQL_UPDATE_STATUS_GUILD_DELETE "UPDATE evil_status SET gid=0,gpos=0 WHERE gid=%d"
// #define SQL_GUILD_DELETE "DELETE FROM evil_guild WHERE gid=%d"
// delete guild_share and guild at once
#define SQL_GUILD_DELETE "DELETE gs,g FROM evil_guild_share gs JOIN evil_guild g ON gs.gid=g.gid WHERE gs.gid=%d"


int in_delete_guild(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	// TODO : gold, crystal from guild back to master
	// check: gid check_guild_exist
	// evil_status :  UPDATE evil_status gid,gpos=0 WHERE gid=%d
	// evil_guild : DELETE FROM evil_guild WHERE gid = %d
	int ret, len;
	int gid;
	char gname[EVIL_ALIAS_MAX + 5]; // useless only for check_guild_exist

	ret = sscanf(in_buffer, IN_DELETE_GUILD_SCAN, &gid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "guild_delete:invalid_input %d", ret);
	}
	if (gid <= 0) {
		DB_ER_RETURN(-15, "guild_delete:invalid_gid %d", gid);
	}


	// ==1 means exists, <=0 not_exists
	ret = check_guild_exist(pconn, q_buffer, gid, gname);
	if (ret <= 0) {
		// DB_ER_RETURN(-3, "guild_delete:guild_not_exist %d", gid);
		DB_ER_RETURN(-3, "%s %d", E_GUILD_DEL_GUILD_NOT_EXIST, gid);
	}

	// part1:  kick all members, set evil_status gid,gpos=0
	len = sprintf(q_buffer, SQL_UPDATE_STATUS_GUILD_DELETE, gid);
	// DEBUG_PRINT(0, "query: %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		DB_ER_NEG_RETURN(-55, "guild_delete:update_mysql_errno %d", err);
	}

	// part2 :
	// it delete guild and guild_share records
	len = sprintf(q_buffer, SQL_GUILD_DELETE, gid);
	// DEBUG_PRINT(0, "query: %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		DB_ER_NEG_RETURN(-65, "guild_delete:delete_mysql_errno %d", err);
	}

	// return the guild id (>=0)
	sprintf(out_buffer, OUT_DELETE_GUILD_PRINT, gid);
	return 0;
}


// make sure gold is not 0
#define SQL_UPDATE_GUILD_MONEY	"UPDATE evil_guild set gold=gold+(%d), crystal=crystal+(%d) WHERE gid=%d AND gold+(%d)>0 AND crystal+(%d)>=0"

int update_guild_money(MYSQL **pconn, char *q_buffer, int gid, int gold
, int crystal)
{
	int len;
	int ret;

	if (gid <= 0) {
		ERROR_RETURN(-15, "update_guild_money:invalid_gid %d", gid);
	}

	len = sprintf(q_buffer, SQL_UPDATE_GUILD_MONEY, gold, crystal, gid
	, gold, crystal);
	// DEBUG_PRINT(0, "query: %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-65, "update_guild_money:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret != 1) {
		ERROR_NEG_RETURN(-6, "update_guild_money:affected_row wrong %d\n", ret);
	}

	return 0;
}


#define SQL_GUILD_GOLD	"SELECT gold from evil_guild WHERE gid=%d"

int get_guild_gold(MYSQL **pconn, char *q_buffer, int gid) 
{

	int ret;
	int gold;
	int len;
	
	if (gid <= 0) {
		ERROR_RETURN(-15, "get_guild_gold:invalid_gid");
	}
	
	len = sprintf(q_buffer, SQL_GUILD_GOLD, gid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "get_guild_gold:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "db_load_card query: %s", g_query);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_guild_gold:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		// peter: this is normal, since user may disconnect 
		// before job selection, lcard can be empty!
		ERROR_PRINT(ret, "get_guild_gold:empty_row");
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!
	if (row == NULL) {
		ERROR_PRINT(ret, "get_guild_gold:null_row");
		goto cleanup;
	}

	gold = strtol_safe(row[0], -7);
	
	ret = gold;

cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_GUILD_DEPOSIT	"UPDATE evil_guild_share SET gshare=gshare*%lf WHERE gid=%d"
#define SQL_GUILD_DEPOSIT_ADD	"UPDATE evil_guild_share SET gshare=gshare+%lf WHERE eid=%d"

// CMD: gdeposit
int in_guild_deposit(MYSQL **pconn, char *q_buffer
, const char * in_buffer, char * out_buffer)
{
	int ret;
	int len;
	int eid;
	int gid;
	int gold;
	int guild_gold;
	double rate; // TODO rate=guild_gold/(guild_gold+gold)

	ret = sscanf(in_buffer, IN_GUILD_DEPOSIT_SCAN, &eid, &gid, &gold);
	if (ret != 3) {
		DB_ER_RETURN(-5, "guild_deposit:invalid_input %d", ret);
	}
	if (gid <= 0) {
		DB_ER_RETURN(-15, "guild_deposit:invalid_gid %d", gid);
	}
	if (eid <= 0) {
		DB_ER_RETURN(-25, "guild_deposit:invalid_eid %d", eid);
	}
	if (gold <= 0) {
		DB_ER_RETURN(-35, "guild_deposit:invalid_gold %d", gold);
	}


	guild_gold = get_guild_gold(pconn, q_buffer, gid);
	if (guild_gold < 0) {
		DB_ER_RETURN(-17, "guild_deposit:cannot_get_guild_gold %d", guild_gold);
	}

	if (guild_gold + gold <= 0) {
		DB_ER_RETURN(-27, "guild_deposit:guild_gold_add_gold %d %d"
		, guild_gold, gold);
	}

	// before this: read-only check
	// after this: write
	ret = update_money(pconn, q_buffer, eid, -gold, 0, EVIL_BUY_GUILD_DEPOSIT, 1, "guild_deposit");
	if (ret < 0) {
		// member not enough money or other db error
		// DB_ER_NEG_RETURN(-2, "guild_deposit:not_enough_money %d", ret);
		DB_ER_NEG_RETURN(-2, "%s %d", E_GUILD_DEPOSIT_MONEY_NOT_ENOUGH, ret);
	}

	// ret = record_buy(pconn, q_buffer, eid, EVIL_BUY_GUILD_DEPOSIT, 1, -gold, 0, "guild_deposit");

	// order is important, after get_guild_gold()
	ret = update_guild_money(pconn, q_buffer, gid, gold, 0);
	if (ret < 0) {
		DB_ER_NEG_RETURN(-37, "guild_deposit:update_guild_money:%d", ret);
	}

	// crystal=0
	ret = save_guild_deposit(pconn, q_buffer, eid, gid, gold, 0);
	if (ret < 0) {
		DB_ER_NEG_RETURN(-47, "guild_deposit:save_guild_deposit:%d", ret);
	}

		
	rate = (double)guild_gold / (double)(guild_gold+gold);

	len = sprintf(q_buffer, SQL_GUILD_DEPOSIT, rate, gid);
	// DEBUG_PRINT(0, "query: %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		DB_ER_NEG_RETURN(-65, "guild_deposit:mysql_errno %d", err);
	}


	rate = (double)gold / (double)(guild_gold+gold);

	len = sprintf(q_buffer, SQL_GUILD_DEPOSIT_ADD, rate, eid);
	// DEBUG_PRINT(0, "query: %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		DB_ER_NEG_RETURN(-75, "guild_deposit_add:mysql_errno %d", err);
	}
		
	sprintf(out_buffer, OUT_GUILD_DEPOSIT_PRINT, eid, gid, gold);

	return 0;
}


typedef struct _share_struct {
	int eid;
	int gid;
	double gshare;
	time_t last_bonus;
} share_t;


#define SQL_GET_SHARE 	"SELECT eid,gid,gshare,UNIX_TIMESTAMP(last_bonus) FROM evil_guild_share WHERE eid=%d LIMIT 1"

int get_guild_share(MYSQL **pconn, char *q_buffer, int eid, share_t *pshare)
{
	int ret;
	int len;
	
	len = sprintf(q_buffer, SQL_GET_SHARE, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "get_guild_share:mysql_errno %d", err);
	}
	
	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-53, "get_guild_share:null_result");
	}

	len = mysql_field_count(*pconn);
	if (4!=len) {
		ret = -67;  // goto cleanup need 'ret'
		ERROR_PRINT(ret, "get_guild_share:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	ret = num_row;  // 0 not exists, 1 exists,  -ve error

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -77;
		ERROR_PRINT(ret, "get_guild_share:null_row");
		goto cleanup;
	}

	// row[0]=eid,  row[1]=gid, row[2]=gshare,  row[3]=last_bonus
	pshare->eid = strtol_safe(row[0], -1);
	pshare->gid = strtol_safe(row[1], -2);
	pshare->gshare = str_double_safe(row[2], 0.0);
	pshare->last_bonus = strtolong_safe(row[3], -4);

	ret = 0; // ok

cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_LAST_BONUS 	"UPDATE evil_guild_share SET last_bonus=FROM_UNIXTIME(%ld) WHERE eid=%d"

int update_last_bonus(MYSQL **pconn, char *q_buffer, int eid, time_t last_bonus)
{
	int ret;
	int len;

	len = sprintf(q_buffer, SQL_LAST_BONUS, last_bonus, eid);
	DEBUG_PRINT(0, "last_bonus query: %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "update_last_bonus:mysql_errno %d", err);
	}
	
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret != 1) {
		ERROR_NEG_RETURN(-6, "update_last_bonus:affected_row wrong %d\n", ret);
	}

	return 0;
}


// get yesterday 23:59:59
time_t get_yesterday(time_t tt)
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
//	printf("yesterday %ld %s\n", yesterday, asctime(&timestruct));

	return yesterday;
}


#define SQL_GUILD_BONUS "xxx"


// CMD: gbonus [get_flag]
// RET: gbonus eid get_flag gshare gold last_bonus_time
// ERR: 
// logic:
// - must: have gid > 0, gpos != GUILD_POS_APPLY
// - must: last_bonus is (older) another day
// - time_t now,  change to YYYY-MM-DD hh:mi:ss, remove hh:mi:ss (00:00:00)
//   convert back to time_t format (today) 
//   yesterday(midnight) = today - 1 seconds 
// - if last_bonus <= FROM_UNIXTIME(yesterday) : give bonus, else no bonus!
int in_guild_bonus(MYSQL **pconn, char *q_buffer
, const char * in_buffer, char * out_buffer)
{
	int ret;
	// input:
	int eid;
	double rate; 
	int get_flag = 0;
	// db operation:
	share_t share;
	int gold;
	time_t yesterday;

	ret = sscanf(in_buffer, IN_GUILD_BONUS_SCAN, &eid, &rate, &get_flag);
	if (ret != 3) {
		DB_ER_RETURN(-5, "guild_bonus:invalid_input %d", ret);
	}
	if (eid <= 0) {
		DB_ER_RETURN(-25, "guild_bonus:invalid_eid %d", eid);
	}
	if (rate <= 0) {
		DB_ER_RETURN(-35, "guild_bonus:invalid_rate %lf", rate);
	}
	if (get_flag<0 || get_flag>1) {
		DB_ER_RETURN(-45, "guild_bonus:invalid_get_flag %d", get_flag);
	}
	



	ret = get_guild_share(pconn, q_buffer, eid, &share);
	if (ret < 0) {
		DB_ER_RETURN(-55, "guild_bonus:get_guild_share %d", ret);
	}
// --- after get share 

	if (share.gid <= 0) { // this is strange !!!
		DB_ER_RETURN(-15, "guild_bonus:invalid_gid %d", share.gid);
	}



	// get guild gold
	gold = get_guild_gold(pconn, q_buffer, share.gid);
	if (gold < 0) {
		DB_ER_RETURN(-65, "guild_bonus:get_guild_gold %d", gold);
	}

	int bonus = (int)floor((double)gold * share.gshare * rate);
	if (bonus < 0) {
		DB_ER_RETURN(-2, "guild_bonus:neg_bonus %d %lf %lf"
		, gold, share.gshare, rate);
	}

	////// get_flag logic
	if (get_flag == 0) {
		// early exit
		sprintf(out_buffer, OUT_GUILD_BONUS_PRINT, eid, get_flag, gold, rate, share.gshare
		, bonus, share.last_bonus);
		return 0;
	}


	// from here: get_flag == 1 (or non-zero)
	yesterday = get_yesterday(time(NULL));
	if (share.last_bonus > yesterday) {
		// DB_ER_RETURN(-6, "guild_bonus:already_give_bonus %ld", share.last_bonus);
		DB_ER_RETURN(-6, "%s %ld", E_GUILD_BONUS_ALREADY_GET, share.last_bonus);
	}

	ret = update_money(pconn, q_buffer, eid, bonus, 0);
	if (ret < 0) {
		DB_ER_RETURN(-77, "guild_bonus:update_money %d", ret);
	}

	ret = update_last_bonus(pconn, q_buffer, eid, time(NULL));
	if (ret < 0) {
		DB_ER_RETURN(-87, "guild_bonus:update_last_bonus %d", ret);
	}

	// approximate:  the last_bonus is not from database, after update_last_bonus()
	// this is for faster execution, no need to get database record again, approx same sec
	share.last_bonus = time(NULL); // actually this is useless, but we want the same sprintf
	sprintf(out_buffer, OUT_GUILD_BONUS_PRINT, eid, get_flag, gold, rate, share.gshare
	, bonus, share.last_bonus);
	
	return 0;
}


#define SQL_LIST_DEPOSIT "SELECT UNIX_TIMESTAMP(deposit_date),d.eid,u.alias,u.icon,d.gold,d.crystal,s.gpos FROM evil_guild_deposit d LEFT JOIN evil_user u ON d.eid=u.eid LEFT JOIN evil_status s ON d.eid=s.eid WHERE d.gid=%d ORDER BY deposit_date DESC LIMIT %d,%d"


// CMD:	gdeplist start_id page_size
int in_list_deposit(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	// non-thread-safe!
	int ret;
	int len;
	// input:
	int start_id, page_size;
	int gid;
	int count;
	char *ptr;


	ret = sscanf(in_buffer, IN_LIST_DEPOSIT_SCAN, &gid, &start_id, &page_size);
	if (ret != 3) {
		DB_ER_RETURN(-5, "list_deposit:invalid_input");
	}
	if (gid <= 0) { 
		DB_ER_RETURN(-15, "list_deposit:gid %d", gid);
	}
	if (start_id < 0) {  // 0 is valid
		DB_ER_RETURN(-25, "list_deposit:invalid_start_id %d", start_id);
	}
	// hard code, max page size must be less than 50
	// 2900 / 50 = 58  
	// xcid(14) + eid(7) + gold+crystal(9) + cardid(4)=34
	if (page_size <= 0 || page_size >50) {
		DB_ER_RETURN(-35, "list_deposit:invalid_page_size %d", page_size);
	}

	
	len = sprintf(q_buffer, SQL_LIST_DEPOSIT, gid, start_id, page_size);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "list_deposit:mysql_errno %d", err);
	}


	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "list_deposit:null_result");
	}

	// use | as separator for each record
	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -13; // null case?
		DB_ER_PRINT(ret, "list_deposit:negative_row %d", num_row);
		goto cleanup;
	}
	if (num_row==0) {  // normal case
		sprintf(out_buffer, OUT_LIST_DEPOSIT_PRINT, start_id, num_row);
		ret = 0;
		goto cleanup;
	}

	// deposit_time, eid, alias, icon, gold, crystal, gpos
	field_count = mysql_field_count(*pconn);
	if (field_count != 7) {
		ret = -17; // null case?
		DB_ER_PRINT(ret, "list_deposit:invalid_field_count %d", field_count);
		goto cleanup;
	}

	// more than 1 returned
	ptr = out_buffer;
	ptr += sprintf(out_buffer, OUT_LIST_DEPOSIT_PRINT, start_id, num_row);
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-27, "list_deposit:fetch_row_overflow %d", count);
			break;
		}
		ptr += sprintf(ptr, OUT_LIST_DEPOSIT_ROW_PRINT, row[0], row[1], row[2]
		, row[3], row[4], row[5], row[6]);
	}
	ret = 0;  // make sure ret is OK (0)
	// shall we check count == num_row ??

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}



#define SQL_MY_DEPOSIT "SELECT IFNULL(SUM(d.gold),0),IFNULL(SUM(d.crystal),0),IFNULL(s.gshare,0) FROM evil_guild_deposit d LEFT JOIN evil_guild_share s ON d.eid=s.eid WHERE d.eid=%d AND d.gid=%d LIMIT 1"

#define SQL_SUM_DEPOSIT "SELECT IFNULL(SUM(gold),0),IFNULL(SUM(crystal),0) FROM evil_guild_deposit WHERE gid=%d LIMIT 1"

// CMD:	deplist 
int in_deposit(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	// non-thread-safe!
	int ret;
	int len;
	// input:
	int eid;
	int gid;
	// output:
	int my_gold;
	int my_crystal;
	double gshare;
	int guild_gold;
	int guild_crystal;


	// 1.get my sum gold and crystal deposit, gshare
	ret = sscanf(in_buffer, IN_DEPOSIT_SCAN, &eid, &gid);
	if (ret != 2) {
		DB_ER_RETURN(-5, "deposit:invalid_input");
	}
	if (eid <= MAX_AI_EID) { 
		DB_ER_RETURN(-15, "deposit:eid %d", eid);
	}
	if (gid <= 0) { 
		DB_ER_RETURN(-25, "deposit:gid %d", gid);
	}

	
	len = sprintf(q_buffer, SQL_MY_DEPOSIT, eid, gid);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "my_deposit:mysql_errno %d", err);
	}

	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "my_deposit:null_result");
	}

	// use | as separator for each record
	num_row = mysql_num_rows(result);
	if (num_row<=0) {  // normal case
		ret = -13; // null case?
		DB_ER_PRINT(ret, "my_deposit:negative_row %d", num_row);
		goto cleanup;
	}
	// SUM(gold), SUM(crystal), gshare
	field_count = mysql_field_count(*pconn);
	if (field_count != 3) {
		ret = -17; // null case?
		DB_ER_PRINT(ret, "my_deposit:invalid_field_count %d", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -23;
		DB_ER_PRINT(ret, "my_deposit:null_row eid %d", eid);
		goto cleanup;
	}

	my_gold = strtol_safe(row[0], -1);
	if (my_gold < 0) {
		ret = -33;
		DB_ER_PRINT(ret, "my_deposit:null_result eid %d", eid);
		goto cleanup;
	}

	my_crystal = strtol_safe(row[1], -1);
	if (my_crystal < 0) {
		ret = -43;
		DB_ER_PRINT(ret, "my_deposit:null_result eid %d", eid);
		goto cleanup;
	}

	gshare = str_double_safe(row[2], -1);
	if (gshare < 0) {
		ret = -53;
		DB_ER_PRINT(ret, "my_deposit:null_result eid %d", eid);
		goto cleanup;
	}



	// 2.get guild deposite sum	
	len = sprintf(q_buffer, SQL_SUM_DEPOSIT, gid);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "sum_deposit:mysql_errno %d", err);
	}


	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "sum_deposit:null_result");
	}

	// use | as separator for each record
	num_row = mysql_num_rows(result);
	if (num_row<=0) {  // normal case
		ret = -13; // null case?
		DB_ER_PRINT(ret, "sum_deposit:negative_row %d", num_row);
		goto cleanup;
	}
	// SUM(guild_deposit_gold), SUM(guild_deposit_crystal)
	field_count = mysql_field_count(*pconn);
	if (field_count != 2) {
		ret = -17; // null case?
		DB_ER_PRINT(ret, "sum_deposit:invalid_field_count %d", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -23;
		DB_ER_PRINT(ret, "sum_deposit:null_row eid %d", eid);
		goto cleanup;
	}

	guild_gold = strtol_safe(row[0], -1);
	if (guild_gold < 0) {
		ret = -33;
		DB_ER_PRINT(ret, "sum_deposit:null_result eid %d", eid);
		goto cleanup;
	}

	guild_crystal = strtol_safe(row[1], -1);
	if (guild_crystal < 0) {
		ret = -43;
		DB_ER_PRINT(ret, "sum_deposit:null_result eid %d", eid);
		goto cleanup;
	}

	sprintf(out_buffer, OUT_DEPOSIT_PRINT, eid, gid, my_gold, my_crystal
	, gshare, guild_gold, guild_crystal);


	ret = 0;  // make sure ret is OK (0)

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_GUILD "SELECT g.gid,g.total_member,g.glevel,g.gold,g.crystal,u.alias,u.icon,g.gname,g.gnotice FROM evil_guild g LEFT JOIN evil_user u ON g.gid=u.eid WHERE gid=%d LIMIT 1"
#define SQL_UPDATE_GNOTICE	"UPDATE evil_guild SET gnotice='%s' WHERE gid=%d"

int in_guild(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int gid;
	int n;
	char gnotice[105]; // 100 is the max
	char esc_gnotice[210]; // double


	ret = sscanf(in_buffer, IN_GUILD_SCAN, &gid, &n);
	if (ret != 1) {
		DB_ER_RETURN(-5, "in_guild:input %d", ret);
	}
	if (gid < 0) {
		DB_ER_RETURN(-15, "in_guild:invalid_gid %d", gid);
	}
	gnotice[100] = 0;  // pre-null-terminate
	strncpy(gnotice, in_buffer + n, 100);
	trim(gnotice, 0);  // 0 means use strlen
	
	// if gnotice is not empty, update evil_guild.gnotice
	if (strlen(gnotice)>0) {
		
		// TODO need to escape gnotice
		len = mysql_real_escape_string(*pconn, esc_gnotice, gnotice, 
			strlen(gnotice));

		len = sprintf(q_buffer, SQL_UPDATE_GNOTICE, esc_gnotice, gid);
		ret = my_query(pconn, q_buffer, len);

		if (ret != 0) {
			int err = mysql_errno(*pconn);
			ERROR_NEG_RETURN(-75, "in_guild_update_gnotice:mysql_errno %d", err);
		}
		// no check on affected row (since gnotice may be duplicate)
	}


	len = sprintf(q_buffer, SQL_GUILD, gid);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "in_guild:mysql_errno %d", err);
	}


	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "in_guild:null_result");
	}

	// use | as separator for each record
	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -13; // null case?
		// DB_ER_PRINT(ret, "in_guild:gid_not_found %d", gid);
		DB_ER_PRINT(ret, "%s %d", E_GUILD_NOT_FOUND, gid);
		goto cleanup;
	}
	field_count = mysql_field_count(*pconn);
	if (field_count != 9) {
		ret = -17; // null case?
		DB_ER_PRINT(ret, "in_guild:invalid_field_count %d", field_count);
		goto cleanup;
	}

	// more than 1 returned
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -23;
		DB_ER_PRINT(ret, "in_guild:null_row gid %d", gid);
		goto cleanup;
	}

	// row[0]=gid, row[1]=total_member, row[2]=glevel, row[3]=gold
	// , row[4]=crystal, row[5]=alias, row[6]=icon
	// , row[7]=gname, row[8]=gnotice
	if (row[8] == NULL || row[8][0] == '\0') {
		sprintf(gnotice, "_");
	} else {
		sprintf(gnotice, "%s", row[8]);
	}
	sprintf(out_buffer, OUT_GUILD_PRINT, row[0], row[1], row[2], row[3], row[4]
	, row[5], row[6], row[7], gnotice);

	ret = 0;

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_GLV "SELECT gid,glevel,gold FROM evil_guild WHERE gid=%d LIMIT 1"

int in_glv(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int gid;

	ret = sscanf(in_buffer, IN_GLV_SCAN, &gid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "in_glv:input %d", ret);
	}
	if (gid < 0) {
		DB_ER_RETURN(-15, "in_glv:invalid_gid %d", gid);
	}

	len = sprintf(q_buffer, SQL_GLV, gid);
	DEBUG_PRINT(0, "glv query: %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "in_glv:mysql_errno %d", err);
	}


	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "in_glv:null_result");
	}

	// use | as separator for each record
	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -13; // null case?
		// DB_ER_PRINT(ret, "in_glv:gid_not_found %d", gid);
		DB_ER_PRINT(ret, "%s %d", E_GLV_GUILD_NOT_FOUND, gid);
		goto cleanup;
	}
	field_count = mysql_field_count(*pconn);
	if (field_count != 3) {
		ret = -17; // null case?
		DB_ER_PRINT(ret, "in_glv:invalid_field_count %d", field_count);
		goto cleanup;
	}

	// more than 1 returned
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -23;
		DB_ER_PRINT(ret, "in_glv:null_row gid %d", gid);
		goto cleanup;
	}

	// row[0]=gid, row[1]=glevel, row[2]=gold
	sprintf(out_buffer, OUT_GLV_PRINT, row[0], row[1], row[2]);

	ret = 0;

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


// gold must be >
#define SQL_GLEVELUP	"UPDATE evil_guild SET glevel=glevel+1,gold=gold-%d WHERE gid=%d AND glevel=%d AND gold>%d"

int in_glevelup(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret, len;
	int gid, glevel, gold;

	ret = sscanf(in_buffer, IN_GLEVELUP_SCAN, &gid, &glevel, &gold);

	if (gid <= 0) {
		ERROR_RETURN(-15, "glevelup:invalid_gid %d", gid);
	}
	if (glevel<=0) {
		ERROR_RETURN(-25, "glevelup:invalid_glevel %d", glevel);
	}
	if (gold<=0) {
		ERROR_RETURN(-35, "glevelup:invalid_gold %d", gold);
	}

	len = sprintf(q_buffer, SQL_GLEVELUP, gold, gid, glevel, gold);
	ret = my_query(pconn, q_buffer, len);
	
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "glevelup:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); 
	// should be 1,  may be guild not exists
	if (ret != 1) {
		ERROR_RETURN(-65, "glevelup:affected_row %d", ret);
	}

	sprintf(out_buffer, OUT_GLEVELUP_PRINT, gid, glevel+1, -gold);
	return 0;
}

// gpos < 9 is normal member
#define SQL_LIST_GSEARCH	"SELECT evil_status.eid, gpos, evil_user.alias, evil_user.icon, evil_status.rating, UNIX_TIMESTAMP(evil_user.last_login), IFNULL(evil_guild_share.gshare, 0), evil_status.lv FROM evil_status LEFT JOIN evil_user ON evil_status.eid = evil_user.eid LEFT JOIN evil_guild_share ON evil_status.eid = evil_guild_share.eid WHERE evil_status.gid=%d AND %s AND (evil_user.alias LIKE '%%%s%%' OR evil_status.eid LIKE '%%%s%%') ORDER BY gpos ASC, last_login DESC LIMIT %d, %d"

// new gsearch with or without apply
// CMD: gsearch [flag] [start_id] [page_size] [optional_gid] 
// flag = 0 : all member include apply
// flag = 1 : all member without apply
// flag = 9 : all apply 
// RET: glist [flag] [start_id] [total] [member_info1] [member_info2] ...
// member_info = eid pos alias +icon +rating last_login gshare (lv)
//
// SQL : WHERE gid=%d AND %s ORDER BY gpos ASC ...
// %s is the range
// flag = 0: %s = gpos>=0
// flag = 1: %s = gpos < 9
// flag = 9: %s = gpos = 9
int in_guild_lsearch(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int flag;
	int start_id;
	int page_size;
	int gid;
	const char *range;
	int total;
	char *ptr;
	const char *gshare;
	char search_data[101];

	ret = sscanf(in_buffer, IN_LIST_GSEARCH_SCAN, &flag, &start_id
	, &page_size, search_data, &gid);
	if (ret != 5) {
		DB_ER_RETURN(-5, "list_gsearch:invalid_input %d", ret);
	}
	if (gid <= 0) {
		DB_ER_RETURN(-15, "list_gsearch:invalid_gid %d", gid);
	}
	if (flag != 0 && flag != 1 && flag != 9) {
		DB_ER_RETURN(-25, "list_gsearch:invalid_flag %d", flag);
	}

	if (start_id < 0) {  // 0 is valid
		DB_ER_RETURN(-35, "list_gsearch:invalid_start_id %d", start_id);
	}

	if (page_size <= 0 || page_size >50) {
		DB_ER_RETURN(-45, "list_gsearch:invalid_page_size %d", page_size);
	}

	switch(flag) {
	case 0:
		range = "gpos>=0";
		break;
	case 1:
		range = "gpos<9";
		break;
	case 9:
		range = "gpos=9";
		break;
	}

	len = sprintf(q_buffer, SQL_LIST_GSEARCH, gid, range, search_data
	, search_data, start_id, page_size);

	// DEBUG_PRINT(0, "in_list_gmember:q_buffer=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (0 != ret) {
		DB_ER_RETURN(-55, "list_gsearch:mysql_errno %d", mysql_errno(*pconn));
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "list_gsearch:null_result");
	}
	////////// NOTE: from here, we should use: 
	////////// 1. db_output()  + goto cleanup   or
	////////// 2. DB_ERR_PRINT() + goto cleanup
	////////// do not use DB_ERR_RETURN() - this will mess up database

	len = mysql_field_count(*pconn);
	if (8!=len) {
		ret = -67;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "list_gsearch:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -3;	// populer error, means no such guild
		DB_ER_PRINT(ret, "list_gsearch:null_guild %d", num_row);
		goto cleanup;
	}

	len = sprintf(out_buffer, OUT_LIST_GSEARCH_PRINT, flag, start_id, num_row);
	ptr = out_buffer + len;

	// more than 1 rows is normal
	total = 0;  // later: check total == num_row
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		total ++;
		if (total > num_row) {
			BUG_PRINT(-77, "list_gsearch:total_overflow %d %d", total, num_row);
			break;
		}
		
		gshare = row[6];
		if (gshare == NULL) {
			gshare = "0";
		}
		// row[0]=eid, row[1]=gpos, row[2]=alias, row[3]=icon, row[4]=rating
		// , row[5]=last_login, row[6]=gshare, row[7]=lv
		ptr += sprintf(ptr, OUT_LIST_GSEARCH_ROW_PRINT
		, row[0], row[1], row[2], row[3]
		, row[4], row[5], gshare, row[7]);
	}

	ret = 0; // good for here

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;

}



#define SQL_DROP_RATING_LADDER "DROP TABLE IF EXISTS evil_ladder_rating"

#define SQL_CREATE_RATING_LADDER "CREATE TABLE evil_ladder_rating (eid INT UNSIGNED NOT NULL PRIMARY KEY,	rank INT UNSIGNED NOT NULL UNIQUE AUTO_INCREMENT, rating DOUBLE NOT NULL) ENGINE=MyISAM AUTO_INCREMENT=1"

#define SQL_INSERT_RATING_LADDER "INSERT INTO evil_ladder_rating (eid, rating) SELECT eid, rating FROM evil_status ORDER BY rating DESC"

#define SQL_SELECT_RATING_LADDER "SELECT evil_ladder_rating.eid,rank,rating,alias,icon FROM evil_ladder_rating LEFT JOIN evil_user ON evil_ladder_rating.eid=evil_user.eid ORDER BY rank LIMIT 0,%d"

int create_rating_ladder(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	const char *alias; 
	int err;
	int count;
	char *ptr;

	ret = my_query(pconn, SQL_DROP_RATING_LADDER, strlen(SQL_DROP_RATING_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "drop_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_CREATE_RATING_LADDER, strlen(SQL_CREATE_RATING_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "create_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_INSERT_RATING_LADDER, strlen(SQL_INSERT_RATING_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "insert_ladder:mysql_errno %d", err);
	}

	len = sprintf(q_buffer, SQL_SELECT_RATING_LADDER, MAX_LADDER);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "get_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "ladder:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -6;
		DB_ER_PRINT(ret, "ladder:num_negative");
		goto cleanup;
	}

	/*
	if (num_row==0) {
		WARN_PRINT(ret, "ladder:num_row_zero");
		ret = sprintf(out_buffer, OUT_RATING_LADDER_PRINT, num_row);
		goto cleanup;
	}
	*/

	len = mysql_field_count(*pconn);
	if (5!=len) {
		ret = -65;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "ladder:field_count %d", len);
		goto cleanup;
	}
	
	len = sprintf(out_buffer, OUT_RATING_LADDER_PRINT, num_row);
	ret = len;
	ptr = out_buffer + len;
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-27, "ladder:fetch_row_overflow %d", count);
			break;
		}

		alias = row[3];
		if (alias == NULL || alias[0] == '\0') alias = "_no_alias";
		// eid, rank, gold, alias, icon
		len = sprintf(ptr, OUT_RATING_LADDER_ROW_PRINT, row[0], row[1], row[2]
		, alias, row[4]);
		ptr += len;
		ret += len;
	}

	INFO_PRINT(0, "create_rating_ladder:out=%s", out_buffer);

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_DROP_LEVEL_LADDER "DROP TABLE IF EXISTS evil_ladder_level"

#define SQL_CREATE_LEVEL_LADDER "CREATE TABLE evil_ladder_level (eid INT UNSIGNED NOT NULL PRIMARY KEY,	rank INT UNSIGNED NOT NULL UNIQUE AUTO_INCREMENT, lv INT UNSIGNED NOT NULL) ENGINE=MyISAM AUTO_INCREMENT=1"

#define SQL_INSERT_LEVEL_LADDER "INSERT INTO evil_ladder_level (eid, lv) SELECT eid, lv FROM evil_status ORDER BY lv DESC"

#define SQL_SELECT_LEVEL_LADDER "SELECT evil_ladder_level.eid,rank,lv,alias,icon FROM evil_ladder_level LEFT JOIN evil_user ON evil_ladder_level.eid=evil_user.eid ORDER BY rank LIMIT 0,%d"

int create_level_ladder(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	const char *alias;
	int err;
	int count;
	char *ptr;

	ret = my_query(pconn, SQL_DROP_LEVEL_LADDER, strlen(SQL_DROP_LEVEL_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "drop_drop_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_CREATE_LEVEL_LADDER, strlen(SQL_CREATE_LEVEL_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "create_level_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_INSERT_LEVEL_LADDER, strlen(SQL_INSERT_LEVEL_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "add_level_ladder:mysql_errno %d", err);
	}

	len = sprintf(q_buffer, SQL_SELECT_LEVEL_LADDER, MAX_LADDER);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "select_level_get_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "create_level_ladder:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -6;
		DB_ER_PRINT(ret, "create_level_ladder:negative");
		goto cleanup;
	}

	if (num_row==0) {
		WARN_PRINT(ret, "create_level_ladder:num_row_zero");
		ret = sprintf(out_buffer, OUT_LEVEL_LADDER_PRINT, num_row);
		goto cleanup;
	}

	len = mysql_field_count(*pconn);
	if (5!=len) {
		ret = -65;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "create_level_ladder:field_count %d", len);
		goto cleanup;
	}
	
	len = sprintf(out_buffer, OUT_LEVEL_LADDER_PRINT, num_row);
	ret = len;
	ptr = out_buffer + len;
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-27, "create_level_ladder:fetch_row_overflow %d", count);
			break;
		}
		alias = row[3];
		if (alias == NULL || alias[0] == '\0') alias = "_no_alias";
		// eid, rank, rating, alias, icon
		len = sprintf(ptr, OUT_LEVEL_LADDER_ROW_PRINT, row[0], row[1], row[2]
		, alias, row[4]);
		ptr += len;
		ret += len;
	}

	INFO_PRINT(0, "create_level_ladder:out=%s", out_buffer);
	
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_DROP_COLLECTION_LADDER "DROP TABLE IF EXISTS evil_ladder_collection"

#define SQL_CREATE_COLLECTION_LADDER "CREATE TABLE evil_ladder_collection (eid INT UNSIGNED NOT NULL PRIMARY KEY,rank INT UNSIGNED NOT NULL UNIQUE AUTO_INCREMENT, count INT UNSIGNED NOT NULL) ENGINE=MyISAM AUTO_INCREMENT=1"

#define SQL_INSERT_COLLECTION_LADDER "INSERT INTO evil_ladder_collection (eid, count) SELECT eid,if(c1>0,1,0)+if(c2>0,1,0)+if(c3>0,1,0)+if(c4>0,1,0)+if(c5>0,1,0)+if(c6>0,1,0)+if(c7>0,1,0)+if(c8>0,1,0)+if(c9>0,1,0)+if(c10>0,1,0)+if(c11>0,1,0)+if(c12>0,1,0)+if(c13>0,1,0)+if(c14>0,1,0)+if(c15>0,1,0)+if(c16>0,1,0)+if(c17>0,1,0)+if(c18>0,1,0)+if(c19>0,1,0)+if(c20>0,1,0)+if(c21>0,1,0)+if(c22>0,1,0)+if(c23>0,1,0)+if(c24>0,1,0)+if(c25>0,1,0)+if(c26>0,1,0)+if(c27>0,1,0)+if(c28>0,1,0)+if(c29>0,1,0)+if(c30>0,1,0)+if(c31>0,1,0)+if(c32>0,1,0)+if(c33>0,1,0)+if(c34>0,1,0)+if(c35>0,1,0)+if(c36>0,1,0)+if(c37>0,1,0)+if(c38>0,1,0)+if(c39>0,1,0)+if(c40>0,1,0)+if(c41>0,1,0)+if(c42>0,1,0)+if(c43>0,1,0)+if(c44>0,1,0)+if(c45>0,1,0)+if(c46>0,1,0)+if(c47>0,1,0)+if(c48>0,1,0)+if(c49>0,1,0)+if(c50>0,1,0)+if(c51>0,1,0)+if(c52>0,1,0)+if(c53>0,1,0)+if(c54>0,1,0)+if(c55>0,1,0)+if(c56>0,1,0)+if(c57>0,1,0)+if(c58>0,1,0)+if(c59>0,1,0)+if(c60>0,1,0)+if(c61>0,1,0)+if(c62>0,1,0)+if(c63>0,1,0)+if(c64>0,1,0)+if(c65>0,1,0)+if(c66>0,1,0)+if(c67>0,1,0)+if(c68>0,1,0)+if(c69>0,1,0)+if(c70>0,1,0)+if(c71>0,1,0)+if(c72>0,1,0)+if(c73>0,1,0)+if(c74>0,1,0)+if(c75>0,1,0)+if(c76>0,1,0)+if(c77>0,1,0)+if(c78>0,1,0)+if(c79>0,1,0)+if(c80>0,1,0)+if(c81>0,1,0)+if(c82>0,1,0)+if(c83>0,1,0)+if(c84>0,1,0)+if(c85>0,1,0)+if(c86>0,1,0)+if(c87>0,1,0)+if(c88>0,1,0)+if(c89>0,1,0)+if(c90>0,1,0)+if(c91>0,1,0)+if(c92>0,1,0)+if(c93>0,1,0)+if(c94>0,1,0)+if(c95>0,1,0)+if(c96>0,1,0)+if(c97>0,1,0)+if(c98>0,1,0)+if(c99>0,1,0)+if(c100>0,1,0)+if(c101>0,1,0)+if(c102>0,1,0)+if(c103>0,1,0)+if(c104>0,1,0)+if(c105>0,1,0)+if(c106>0,1,0)+if(c107>0,1,0)+if(c108>0,1,0)+if(c109>0,1,0)+if(c110>0,1,0)+if(c111>0,1,0)+if(c112>0,1,0)+if(c113>0,1,0)+if(c114>0,1,0)+if(c115>0,1,0)+if(c116>0,1,0)+if(c117>0,1,0)+if(c118>0,1,0)+if(c119>0,1,0)+if(c120>0,1,0)+if(c121>0,1,0)+if(c122>0,1,0)+if(c123>0,1,0)+if(c124>0,1,0)+if(c125>0,1,0)+if(c126>0,1,0)+if(c127>0,1,0)+if(c128>0,1,0)+if(c129>0,1,0)+if(c130>0,1,0)+if(c131>0,1,0)+if(c132>0,1,0)+if(c133>0,1,0)+if(c134>0,1,0)+if(c135>0,1,0)+if(c136>0,1,0)+if(c137>0,1,0)+if(c138>0,1,0)+if(c139>0,1,0)+if(c140>0,1,0)+if(c141>0,1,0)+if(c142>0,1,0)+if(c143>0,1,0)+if(c144>0,1,0)+if(c145>0,1,0)+if(c146>0,1,0)+if(c147>0,1,0)+if(c148>0,1,0)+if(c149>0,1,0)+if(c150>0,1,0)+if(c151>0,1,0)+if(c152>0,1,0)+if(c153>0,1,0)+if(c154>0,1,0)+if(c155>0,1,0)+if(c156>0,1,0)+if(c157>0,1,0)+if(c158>0,1,0)+if(c159>0,1,0)+if(c160>0,1,0)+if(c161>0,1,0)+if(c162>0,1,0)+if(c163>0,1,0)+if(c164>0,1,0)+if(c165>0,1,0)+if(c166>0,1,0)+if(c167>0,1,0)+if(c168>0,1,0)+if(c169>0,1,0)+if(c170>0,1,0)+if(c171>0,1,0)+if(c172>0,1,0)+if(c173>0,1,0)+if(c174>0,1,0)+if(c175>0,1,0)+if(c176>0,1,0)+if(c177>0,1,0)+if(c178>0,1,0)+if(c179>0,1,0)+if(c180>0,1,0)+if(c181>0,1,0)+if(c182>0,1,0)+if(c183>0,1,0)+if(c184>0,1,0)+if(c185>0,1,0)+if(c186>0,1,0)+if(c187>0,1,0)+if(c188>0,1,0)+if(c189>0,1,0)+if(c190>0,1,0)+if(c191>0,1,0)+if(c192>0,1,0)+if(c193>0,1,0)+if(c194>0,1,0)+if(c195>0,1,0)+if(c196>0,1,0)+if(c197>0,1,0)+if(c198>0,1,0)+if(c199>0,1,0)+if(c200>0,1,0)+if(c201>0,1,0)+if(c202>0,1,0)+if(c203>0,1,0)+if(c204>0,1,0)+if(c205>0,1,0)+if(c206>0,1,0)+if(c207>0,1,0)+if(c208>0,1,0)+if(c209>0,1,0)+if(c210>0,1,0)+if(c211>0,1,0)+if(c212>0,1,0)+if(c213>0,1,0)+if(c214>0,1,0)+if(c215>0,1,0)+if(c216>0,1,0)+if(c217>0,1,0)+if(c218>0,1,0)+if(c219>0,1,0)+if(c220>0,1,0)+if(c221>0,1,0)+if(c222>0,1,0)+if(c223>0,1,0)+if(c224>0,1,0)+if(c225>0,1,0)+if(c226>0,1,0)+if(c227>0,1,0)+if(c228>0,1,0)+if(c229>0,1,0)+if(c230>0,1,0)+if(c231>0,1,0)+if(c232>0,1,0)+if(c233>0,1,0)+if(c234>0,1,0)+if(c235>0,1,0)+if(c236>0,1,0)+if(c237>0,1,0)+if(c238>0,1,0)+if(c239>0,1,0)+if(c240>0,1,0)+if(c241>0,1,0)+if(c242>0,1,0)+if(c243>0,1,0)+if(c244>0,1,0)+if(c245>0,1,0)+if(c246>0,1,0)+if(c247>0,1,0)+if(c248>0,1,0)+if(c249>0,1,0)+if(c250>0,1,0)+if(c251>0,1,0)+if(c252>0,1,0)+if(c253>0,1,0)+if(c254>0,1,0)+if(c255>0,1,0)+if(c256>0,1,0)+if(c257>0,1,0)+if(c258>0,1,0)+if(c259>0,1,0)+if(c260>0,1,0)+if(c261>0,1,0)+if(c262>0,1,0)+if(c263>0,1,0)+if(c264>0,1,0)+if(c265>0,1,0)+if(c266>0,1,0)+if(c267>0,1,0)+if(c268>0,1,0)+if(c269>0,1,0)+if(c270>0,1,0)+if(c271>0,1,0)+if(c272>0,1,0)+if(c273>0,1,0)+if(c274>0,1,0)+if(c275>0,1,0)+if(c276>0,1,0)+if(c277>0,1,0)+if(c278>0,1,0)+if(c279>0,1,0)+if(c280>0,1,0)+if(c281>0,1,0)+if(c282>0,1,0)+if(c283>0,1,0)+if(c284>0,1,0)+if(c285>0,1,0)+if(c286>0,1,0)+if(c287>0,1,0)+if(c288>0,1,0)+if(c289>0,1,0)+if(c290>0,1,0)+if(c291>0,1,0)+if(c292>0,1,0)+if(c293>0,1,0)+if(c294>0,1,0)+if(c295>0,1,0)+if(c296>0,1,0)+if(c297>0,1,0)+if(c298>0,1,0)+if(c299>0,1,0)+if(c300>0,1,0)+if(c301>0,1,0)+if(c302>0,1,0)+if(c303>0,1,0)+if(c304>0,1,0)+if(c305>0,1,0)+if(c306>0,1,0)+if(c307>0,1,0)+if(c308>0,1,0)+if(c309>0,1,0)+if(c310>0,1,0)+if(c311>0,1,0)+if(c312>0,1,0)+if(c313>0,1,0)+if(c314>0,1,0)+if(c315>0,1,0)+if(c316>0,1,0)+if(c317>0,1,0)+if(c318>0,1,0)+if(c319>0,1,0)+if(c320>0,1,0)+if(c321>0,1,0)+if(c322>0,1,0)+if(c323>0,1,0)+if(c324>0,1,0)+if(c325>0,1,0)+if(c326>0,1,0)+if(c327>0,1,0)+if(c328>0,1,0)+if(c329>0,1,0)+if(c330>0,1,0)+if(c331>0,1,0)+if(c332>0,1,0)+if(c333>0,1,0)+if(c334>0,1,0)+if(c335>0,1,0)+if(c336>0,1,0)+if(c337>0,1,0)+if(c338>0,1,0)+if(c339>0,1,0)+if(c340>0,1,0)+if(c341>0,1,0)+if(c342>0,1,0)+if(c343>0,1,0)+if(c344>0,1,0)+if(c345>0,1,0)+if(c346>0,1,0)+if(c347>0,1,0)+if(c348>0,1,0)+if(c349>0,1,0)+if(c350>0,1,0)+if(c351>0,1,0)+if(c352>0,1,0)+if(c353>0,1,0)+if(c354>0,1,0)+if(c355>0,1,0)+if(c356>0,1,0)+if(c357>0,1,0)+if(c358>0,1,0)+if(c359>0,1,0)+if(c360>0,1,0)+if(c361>0,1,0)+if(c362>0,1,0)+if(c363>0,1,0)+if(c364>0,1,0)+if(c365>0,1,0)+if(c366>0,1,0)+if(c367>0,1,0)+if(c368>0,1,0)+if(c369>0,1,0)+if(c370>0,1,0)+if(c371>0,1,0)+if(c372>0,1,0)+if(c373>0,1,0)+if(c374>0,1,0)+if(c375>0,1,0)+if(c376>0,1,0)+if(c377>0,1,0)+if(c378>0,1,0)+if(c379>0,1,0)+if(c380>0,1,0)+if(c381>0,1,0)+if(c382>0,1,0)+if(c383>0,1,0)+if(c384>0,1,0)+if(c385>0,1,0)+if(c386>0,1,0)+if(c387>0,1,0)+if(c388>0,1,0)+if(c389>0,1,0)+if(c390>0,1,0)+if(c391>0,1,0)+if(c392>0,1,0)+if(c393>0,1,0)+if(c394>0,1,0)+if(c395>0,1,0)+if(c396>0,1,0)+if(c397>0,1,0)+if(c398>0,1,0)+if(c399>0,1,0)+if(c400>0,1,0) AS count FROM evil_card ORDER BY count DESC"


#define SQL_SELECT_COLLECTION_LADDER "SELECT evil_ladder_collection.eid,rank,count,alias,icon FROM evil_ladder_collection LEFT JOIN evil_user ON evil_ladder_collection.eid=evil_user.eid ORDER BY rank LIMIT 0,%d"

int create_collection_ladder(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	const char *alias;
	int err;
	int count;
	char *ptr;

	ret = my_query(pconn, SQL_DROP_COLLECTION_LADDER, strlen(SQL_DROP_COLLECTION_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "drop_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_CREATE_COLLECTION_LADDER, strlen(SQL_CREATE_COLLECTION_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "create_collection_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_INSERT_COLLECTION_LADDER, strlen(SQL_INSERT_COLLECTION_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "add_collection_ladder:mysql_errno %d", err);
	}

	len = sprintf(q_buffer, SQL_SELECT_COLLECTION_LADDER, MAX_LADDER);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "select_collection_get_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "create_collection_ladder:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -6;
		DB_ER_PRINT(ret, "create_collection_ladder:negative");
		goto cleanup;
	}

	/*
	if (num_row==0) {
		WARN_PRINT(ret, "create_collection_ladder:num_row_zero");
		ret = sprintf(out_buffer, OUT_COLLECTION_LADDER_PRINT, num_row);
		goto cleanup;
	}
	*/

	len = mysql_field_count(*pconn);
	if (5!=len) {
		ret = -65;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "create_collection_ladder:field_count %d", len);
		goto cleanup;
	}
	
	len = sprintf(out_buffer, OUT_COLLECTION_LADDER_PRINT, num_row);
	ret = len;
	ptr = out_buffer + len;
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-27, "create_gold_ladder:fetch_row_overflow %d", count);
			break;
		}
		alias = row[3];
		if (alias == NULL || alias[0] == '\0') alias = "_no_alias";
		// eid, rank, count, alias, icon
		len = sprintf(ptr, OUT_COLLECTION_LADDER_ROW_PRINT, row[0], row[1], row[2]
		, alias, row[4]);
		ptr += len;
		ret += len;
	}

	INFO_PRINT(0, "create_collection_ladder:out=%s", out_buffer);
	
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_DROP_GUILD_LADDER "DROP TABLE IF EXISTS evil_ladder_guild"

#define SQL_CREATE_GUILD_LADDER "CREATE TABLE evil_ladder_guild (gid INT UNSIGNED NOT NULL PRIMARY KEY,	rank INT UNSIGNED NOT NULL UNIQUE AUTO_INCREMENT, glevel INT UNSIGNED NOT NULL) ENGINE=MyISAM AUTO_INCREMENT=1"

#define SQL_INSERT_GUILD_LADDER "INSERT INTO evil_ladder_guild (gid, glevel) SELECT gid, glevel FROM evil_guild ORDER BY glevel DESC, gold DESC, crystal DESC, total_member DESC"

#define SQL_SELECT_GUILD_LADDER "SELECT evil_ladder_guild.gid,rank,evil_ladder_guild.glevel,gname FROM evil_ladder_guild LEFT JOIN evil_guild ON evil_ladder_guild.gid=evil_guild.gid ORDER BY rank LIMIT 0,%d"

int create_guild_ladder(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	const char *gname;
	int err;
	int count;
	char *ptr;

	ret = my_query(pconn, SQL_DROP_GUILD_LADDER, strlen(SQL_DROP_GUILD_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "drop_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_CREATE_GUILD_LADDER, strlen(SQL_CREATE_GUILD_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "create_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_INSERT_GUILD_LADDER, strlen(SQL_INSERT_GUILD_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "insert_ladder:mysql_errno %d", err);
	}

	len = sprintf(q_buffer, SQL_SELECT_GUILD_LADDER, MAX_LADDER);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "select_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "select_ladder:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) { // may have no guild
		ret = -6;	
		DB_ER_PRINT(ret, "select_ladder:num_row_zero");
		goto cleanup;
	}

	len = mysql_field_count(*pconn);
	if (4!=len) {
		ret = -65;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "select_ladder:field_count %d", len);
		goto cleanup;
	}
	
	len = sprintf(out_buffer, OUT_GUILD_LADDER_PRINT, num_row);
	ret = len;
	ptr = out_buffer + len;
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-27, "select_ladder:fetch_row_overflow %d", count);
			break;
		}
		// eid, rank, rating, alias, "0"(now guild has no icon)
		gname = row[3];
		if (gname == NULL || gname[0] == '\0') gname = "_no_guild";
		len = sprintf(ptr, OUT_GUILD_LADDER_ROW_PRINT, row[0], row[1], row[2]
		, gname, "0");
		ptr += len;
		ret += len;
	}

	INFO_PRINT(0, "create_guild_ladder:out=%s", out_buffer);
	
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_DROP_GOLD_LADDER "DROP TABLE IF EXISTS evil_ladder_gold"

#define SQL_CREATE_GOLD_LADDER "CREATE TABLE evil_ladder_gold (eid INT UNSIGNED NOT NULL PRIMARY KEY,	rank INT UNSIGNED NOT NULL UNIQUE AUTO_INCREMENT, gold INT UNSIGNED NOT NULL) ENGINE=MyISAM AUTO_INCREMENT=1"

#define SQL_INSERT_GOLD_LADDER "INSERT INTO evil_ladder_gold (eid, gold) SELECT eid, gold FROM evil_status ORDER BY gold DESC"

#define SQL_SELECT_GOLD_LADDER "SELECT evil_ladder_gold.eid,rank,gold,alias,icon FROM evil_ladder_gold LEFT JOIN evil_user ON evil_ladder_gold.eid=evil_user.eid ORDER BY rank LIMIT 0,%d"

int create_gold_ladder(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	const char *alias;
	int err;
	int count;
	char *ptr;

	ret = my_query(pconn, SQL_DROP_GOLD_LADDER, strlen(SQL_DROP_GOLD_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "drop_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_CREATE_GOLD_LADDER, strlen(SQL_CREATE_GOLD_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "create_gold_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_INSERT_GOLD_LADDER, strlen(SQL_INSERT_GOLD_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "add_gold_ladder:mysql_errno %d", err);
	}

	len = sprintf(q_buffer, SQL_SELECT_GOLD_LADDER, MAX_LADDER);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "select_gold_get_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "create_gold_ladder:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -6;
		DB_ER_PRINT(ret, "create_gold_ladder:negative");
		goto cleanup;
	}

	len = mysql_field_count(*pconn);
	if (5!=len) {
		ret = -65;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "create_gold_ladder:field_count %d", len);
		goto cleanup;
	}
	
	len = sprintf(out_buffer, OUT_GOLD_LADDER_PRINT, num_row);
	ret = len;
	ptr = out_buffer + len;
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-27, "create_gold_ladder:fetch_row_overflow %d", count);
			break;
		}
		alias = row[3];
		if (alias == NULL || alias[0]=='\0') alias = "_no_alias";
		// eid, rank, gold, alias,icon
		len = sprintf(ptr, OUT_GOLD_LADDER_ROW_PRINT, row[0], row[1], row[2]
		, alias, row[4]);
		ptr += len;
		ret += len;
	}

	INFO_PRINT(0, "create_gold_ladder:out=%s", out_buffer);
	
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_DROP_CHAPTER_LADDER "DROP TABLE IF EXISTS evil_ladder_chapter"
#define SQL_CREATE_CHAPTER_LADDER "CREATE TABLE evil_ladder_chapter (eid INT UNSIGNED NOT NULL PRIMARY KEY, rank INT UNSIGNED NOT NULL UNIQUE AUTO_INCREMENT, chapter_id INT UNSIGNED NOT NULL ,stage_id INT UNSIGNED NOT NULL , count INT UNSIGNED NOT NULL)"
// #define SQL_INSERT_CHAPTER_LADDER "INSERT INTO evil_ladder_chapter (eid,chapter_id,stage_id,count) SELECT tmp.eid,tmp.chapter_id,tmp.stage_id,(LENGTH(group_concat(data))-LENGTH(replace(group_concat(data),'3',''))) AS count from (SELECT *,(IF((@tt:=INSTR(data,'8'))=0,LENGTH(data),@tt)) AS stage_id FROM evil_chapter WHERE data REGEXP '[^9]' ORDER BY chapter_id DESC , stage_id DESC) tmp GROUP BY eid ORDER BY count DESC"
#define SQL_INSERT_CHAPTER_LADDER "INSERT INTO evil_ladder_chapter (eid,chapter_id,stage_id,count) SELECT tmp.eid,tmp.chapter_id,tmp.stage_id,(LENGTH(group_concat(data))-LENGTH(replace(group_concat(data),'3',''))) AS count from (SELECT *,(IF((@tt:=INSTR(data,'8'))=0,LENGTH(data),@tt)) AS stage_id FROM evil_chapter WHERE data REGEXP '[^9]' ORDER BY chapter_id DESC , stage_id DESC) tmp GROUP BY eid ORDER BY chapter_id DESC, stage_id DESC, count DESC"
#define SQL_SELECT_CHAPTER_LADDER "SELECT el.eid,rank,chapter_id,stage_id,count,alias,icon FROM evil_ladder_chapter el LEFT JOIN evil_user eu ON el.eid=eu.eid ORDER BY rank ASC LIMIT 0,%d" 
int create_chapter_ladder(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	const char *alias;
	int err;
	int count;
	char *ptr;

	ret = my_query(pconn, SQL_DROP_CHAPTER_LADDER, strlen(SQL_DROP_CHAPTER_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "drop_chapter_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_CREATE_CHAPTER_LADDER, strlen(SQL_CREATE_CHAPTER_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "create_chapter_ladder:mysql_errno %d", err);
	}

	ret = my_query(pconn, SQL_INSERT_CHAPTER_LADDER, strlen(SQL_INSERT_CHAPTER_LADDER));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "add_chapter_ladder:mysql_errno %d", err);
	}

	len = sprintf(q_buffer, SQL_SELECT_CHAPTER_LADDER, MAX_LADDER);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "select_chapter_get_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	if (result == NULL) {
		DB_ER_RETURN(-53, "create_chapter_ladder:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -6;
		DB_ER_PRINT(ret, "create_chapter_ladder:num_row_zero");
		goto cleanup;
	}

	len = mysql_field_count(*pconn);
	// eid,rank,chapter_id,stage_id,count,alias,icon
	if (7!=len) {
		ret = -65;  // goto cleanup need 'ret'
		DB_ER_PRINT(ret, "create_chapter_ladder:field_count %d", len);
		goto cleanup;
	}
	
	len = sprintf(out_buffer, " %d", num_row);
	ret = len;
	ptr = out_buffer + len;
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-27, "create_chapter_ladder:fetch_row_overflow %d", count);
			break;
		}

		alias = row[5];
		if (alias == NULL || alias[0]=='\0') alias = "_no_alias";

		// eid,rank,chapter_id,stage_id,count,alias,icon
		len = sprintf(ptr, " %s %s %s %s %s %s %s"
		, row[0], row[1], row[2]
		, row[3], row[4]
		, alias, row[6]);
		ptr += len;
		ret += len;
	}

	INFO_PRINT(0, "create_chapter_ladder:out=%s", out_buffer);
	
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}



// 5 ladder should be made
// 1.user rating ladder
// 2.user level ladder
// 3.guild ladder (order by level, gold, crystal, total_member)
// 4.collection ladder
// 5.user gold ladder
int in_create_ladder(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{

	int ret;
	char *ptr;
	ret = 0;

	
	ptr = out_buffer + ret;
	ret = create_rating_ladder(pconn, q_buffer, in_buffer, ptr);
	if (ret < 0) {
		DB_ER_RETURN(ret, "in_rating_ladder:create_ladder_error");
	}

	ptr = ptr + ret;
	ret = create_level_ladder(pconn, q_buffer, in_buffer, ptr);
	if (ret < 0) {
		DB_ER_RETURN(ret, "in_level_ladder:create_ladder_error");
	}

	ptr = ptr + ret;
	ret = create_guild_ladder(pconn, q_buffer, in_buffer, ptr);
	if (ret < 0) {
		DB_ER_RETURN(ret, "in_guild_ladder:create_ladder_error");
	}

	ptr = ptr + ret;
	ret = create_collection_ladder(pconn, q_buffer, in_buffer, ptr);
	if (ret < 0) {
		DB_ER_RETURN(ret, "in_collection_ladder:create_ladder_error");
	}

	ptr = ptr + ret;
	ret = create_gold_ladder(pconn, q_buffer, in_buffer, ptr);
	if (ret < 0) {
		DB_ER_RETURN(ret, "in_gold_ladder:create_ladder_error");
	}

	ptr = ptr + ret;
	ret = create_chapter_ladder(pconn, q_buffer, in_buffer, ptr);
	if (ret < 0) {
		DB_ER_RETURN(ret, "in_chapter_ladder:create_ladder_error");
	}

	ret = 0;
	
	return 0;
}



#define SQL_GET_RATING_LADDER "SELECT * FROM evil_ladder_rating where eid=%d LIMIT 1"

int get_rating_ladder(MYSQL **pconn, char *q_buffer, char *out_buffer
, int ladder_type, int eid)
{
	int ret;
	int len;
	int err;
	int rank;
	double rating;

	len = sprintf(q_buffer, SQL_GET_RATING_LADDER, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "get_rating_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	len = mysql_field_count(*pconn);
	if (3!=len) {
		ret = -65;  // goto cleanup need 'ret'
		ERROR_PRINT(ret, "get_rating_ladder:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row==0) {
		ret = 0;
		sprintf(out_buffer, OUT_GET_RATING_LADDER_PRINT, ladder_type
		, eid, 0, 0.0); 
		goto cleanup;
	}
	if (num_row<0) {
		ret = -6;	
		ERROR_PRINT(-6, "get_rating_ladder:num_row<=0 %d", eid);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL ) {
		ret = -3;
		ERROR_PRINT(-3, "get_rating_ladder:null_row %d", eid);
		goto cleanup; // cleanup and early exit
	}

	eid = strtol_safe(row[0], -1);
	if (eid < 0) {
		ret = -55;
		ERROR_PRINT(ret, "get_rating_ladder:null_eid %d", eid);
		goto cleanup;
	}
	rank = strtol_safe(row[1], -1);
	if (rank < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_rating_ladder:null_rank %d", rank);
		goto cleanup;
	}
	rating = str_double_safe(row[2], -1.0);
	if (rating < 0) {
		ret = -75;
		ERROR_PRINT(ret, "get_rating_ladder:null_rating %lf", rating);
		goto cleanup;
	}

	sprintf(out_buffer, OUT_GET_RATING_LADDER_PRINT, ladder_type
	, eid, rank, rating); 
	
	ret = 0;
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_GET_LEVEL_LADDER "SELECT * FROM evil_ladder_level where eid=%d LIMIT 1"

int get_level_ladder(MYSQL **pconn, char *q_buffer, char *out_buffer
, int ladder_type, int eid)
{
	int ret;
	int len;
	int err;
	int rank;
	int lv;

	len = sprintf(q_buffer, SQL_GET_LEVEL_LADDER, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "get_level_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	len = mysql_field_count(*pconn);
	if (3!=len) {
		ret = -65;  // goto cleanup need 'ret'
		ERROR_PRINT(ret, "get_level_ladder:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row==0) {
		ret = 0;
		sprintf(out_buffer, OUT_GET_LEVEL_LADDER_PRINT, ladder_type
		, eid, 0, 0); 
		goto cleanup;
	}
	if (num_row<0) {
		ret = -6;	
		ERROR_PRINT(-6, "get_level_ladder:num_row<=0 %d", eid);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL ) {
		ret = -3;
		ERROR_PRINT(-3, "get_level_ladder:null_row %d", eid);
		goto cleanup; // cleanup and early exit
	}

	eid = strtol_safe(row[0], -1);
	if (eid < 0) {
		ret = -55;
		ERROR_PRINT(ret, "get_level_ladder:null_eid %d", eid);
		goto cleanup;
	}
	rank = strtol_safe(row[1], -1);
	if (rank < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_level_ladder:null_rank %d", rank);
		goto cleanup;
	}
	lv = strtol_safe(row[2], -1);
	if (lv < 0) {
		ret = -75;
		ERROR_PRINT(ret, "get_level_ladder:null_lv %d", lv);
		goto cleanup;
	}

	sprintf(out_buffer, OUT_GET_LEVEL_LADDER_PRINT, ladder_type
	, eid, rank, lv);
	
	ret = 0;
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_GET_GUILD_LADDER "SELECT * FROM evil_ladder_guild where gid=%d LIMIT 1"

int get_guild_ladder(MYSQL **pconn, char *q_buffer, char *out_buffer
, int ladder_type, int gid)
{
	int ret;
	int len;
	int err;
	int rank;
	int glevel;

	len = sprintf(q_buffer, SQL_GET_GUILD_LADDER, gid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "get_guild_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	len = mysql_field_count(*pconn);
	if (3!=len) {
		ret = -65;  // goto cleanup need 'ret'
		ERROR_PRINT(ret, "get_guild_ladder:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row==0) {
		ret = 0;
		sprintf(out_buffer, OUT_GET_GUILD_LADDER_PRINT, ladder_type
		, gid, 0, 0); 
		goto cleanup;
	}
	if (num_row<0) {
		ret = -6;	
		ERROR_PRINT(-6, "get_guild_ladder:num_row<=0 %d", gid);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL ) {
		ret = -3;
		ERROR_PRINT(-3, "get_guild_ladder:null_row %d", gid);
		goto cleanup; // cleanup and early exit
	}

	gid = strtol_safe(row[0], -1);
	if (gid < 0) {
		ret = -55;
		ERROR_PRINT(ret, "get_guild_ladder:null_gid %d", gid);
		goto cleanup;
	}
	rank = strtol_safe(row[1], -1);
	if (rank < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_guild_ladder:null_rank %d", rank);
		goto cleanup;
	}
	glevel = strtol_safe(row[2], -1);
	if (glevel < 0) {
		ret = -75;
		ERROR_PRINT(ret, "get_guild_ladder:null_glevel %d", glevel);
		goto cleanup;
	}

	sprintf(out_buffer, OUT_GET_GUILD_LADDER_PRINT, ladder_type
	, gid, rank, glevel);
	
	ret = 0;
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_GET_COLLECTION_LADDER "SELECT * FROM evil_ladder_collection where eid=%d LIMIT 1"

int get_collection_ladder(MYSQL **pconn, char *q_buffer, char *out_buffer
, int ladder_type, int eid)
{
	int ret;
	int len;
	int err;
	int rank;
	int count;

	len = sprintf(q_buffer, SQL_GET_COLLECTION_LADDER, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "get_collection_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	len = mysql_field_count(*pconn);
	if (3!=len) {
		ret = -65;  // goto cleanup need 'ret'
		ERROR_PRINT(ret, "get_collection_ladder:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row==0) {
		ret = 0;
		sprintf(out_buffer, OUT_GET_COLLECTION_LADDER_PRINT, ladder_type
		, eid, 0, 0); 
		goto cleanup;
	}
	if (num_row<0) {
		ret = -6;	
		ERROR_PRINT(-6, "get_collection_ladder:num_row<=0 %d", eid);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL ) {
		ret = -3;
		ERROR_PRINT(-3, "get_collection_ladder:null_row %d", eid);
		goto cleanup; // cleanup and early exit
	}

	eid = strtol_safe(row[0], -1);
	if (eid < 0) {
		ret = -55;
		ERROR_PRINT(ret, "get_collection_ladder:null_eid %d", eid);
		goto cleanup;
	}
	rank = strtol_safe(row[1], -1);
	if (rank < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_collection_ladder:null_rank %d", rank);
		goto cleanup;
	}
	count = strtol_safe(row[2], -1);
	if (count < 0) {
		ret = -75;
		ERROR_PRINT(ret, "get_collection_ladder:null_count %d", count);
		goto cleanup;
	}

	// eid, rank, count
	sprintf(out_buffer, OUT_GET_COLLECTION_LADDER_PRINT, ladder_type
	, eid, rank, count);
	
	ret = 0;
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_GET_GOLD_LADDER "SELECT * FROM evil_ladder_gold where eid=%d LIMIT 1"

int get_gold_ladder(MYSQL **pconn, char *q_buffer, char *out_buffer
, int ladder_type, int eid)
{
	int ret;
	int len;
	int err;
	int rank;
	int gold;

	len = sprintf(q_buffer, SQL_GET_GOLD_LADDER, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "get_gold_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	len = mysql_field_count(*pconn);
	if (3!=len) {
		ret = -65;  // goto cleanup need 'ret'
		ERROR_PRINT(ret, "get_gold_ladder:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row==0) {
		ret = 0;
		sprintf(out_buffer, OUT_GET_GOLD_LADDER_PRINT, ladder_type
		, eid, 0, 0); 
		goto cleanup;
	}
	if (num_row<0) {
		ret = -6;	
		ERROR_PRINT(-6, "get_gold_ladder:num_row<=0 %d", eid);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL ) {
		ret = -3;
		ERROR_PRINT(-3, "get_gold_ladder:null_row %d", eid);
		goto cleanup; // cleanup and early exit
	}

	eid = strtol_safe(row[0], -1);
	if (eid < 0) {
		ret = -55;
		ERROR_PRINT(ret, "get_gold_ladder:null_eid %d", eid);
		goto cleanup;
	}
	rank = strtol_safe(row[1], -1);
	if (rank < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_gold_ladder:null_rank %d", rank);
		goto cleanup;
	}
	gold = strtol_safe(row[2], -1);
	if (gold < 0) {
		ret = -75;
		ERROR_PRINT(ret, "get_gold_ladder:null_gold %d", gold);
		goto cleanup;
	}

	// eid, rank, gold
	sprintf(out_buffer, OUT_GET_GOLD_LADDER_PRINT, ladder_type
	, eid, rank, gold);
	
	ret = 0;
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}

#define SQL_GET_CHAPTER_LADDER "SELECT * FROM evil_ladder_chapter where eid=%d LIMIT 1"
int get_chapter_ladder(MYSQL **pconn, char *q_buffer, char *out_buffer
, int ladder_type, int eid)
{
	int ret;
	int len;
	int err;
	int rank;
	int chapter_id;
	int stage_id;
	int count;

	len = sprintf(q_buffer, SQL_GET_CHAPTER_LADDER, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "get_chapter_ladder:mysql_errno %d", err);
	}

	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(*pconn);

	len = mysql_field_count(*pconn);
	if (5!=len) {
		ret = -65;  // goto cleanup need 'ret'
		ERROR_PRINT(ret, "get_chapter_ladder:field_count %d", len);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row==0) {
		ret = 0;
		// ladder_type, eid, rank, chapter_id, stage_id, count
		sprintf(out_buffer, "%d %d %d %d %d %d"
		, ladder_type, eid, 0, 0, 0, 0); 
		goto cleanup;
	}
	if (num_row<0) {
		ret = -6;	
		ERROR_PRINT(-6, "get_chapter_ladder:num_row<=0 %d", eid);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL ) {
		ret = -3;
		ERROR_PRINT(-3, "get_chapter_ladder:null_row %d", eid);
		goto cleanup; // cleanup and early exit
	}

	eid = strtol_safe(row[0], -1);
	if (eid < 0) {
		ret = -55;
		ERROR_PRINT(ret, "get_chapter_ladder:null_eid %d", eid);
		goto cleanup;
	}

	rank = strtol_safe(row[1], -1);
	if (rank < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_chapter_ladder:null_rank %d", rank);
		goto cleanup;
	}

	chapter_id = strtol_safe(row[2], -1);
	if (chapter_id < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_chapter_ladder:null_chapter_id %d", chapter_id);
		goto cleanup;
	}

	stage_id = strtol_safe(row[3], -1);
	if (stage_id < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_chapter_ladder:null_stage_id %d", stage_id);
		goto cleanup;
	}

	count = strtol_safe(row[4], -1);
	if (count < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_chapter_ladder:null_count %d", count);
		goto cleanup;
	}

	// ladder_type, eid, rank, chapter_id, stage_id, count
	sprintf(out_buffer, "%d %d %d %d %d %d"
	, ladder_type, eid, rank, chapter_id, stage_id, count);
	
	ret = 0;
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}

// ladder_type:
// LADDER_RATING
// LADDER_LEVEL
// LADDER_GUILD
// LADDER_COLLECT
// LADDER_GOLD
// LADDER_CHAPTER
int in_get_ladder(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{

	int ret;
	int ladder_type;
	int eid;
	int gid;

	ret = sscanf(in_buffer, IN_GET_LADDER_SCAN
	, &ladder_type, &eid ,&gid);
	if (ret != 3) {
		DB_ER_RETURN(-5, "get_ladder:less_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "get_ladder:eid_error %d", ret);
	}

	switch (ladder_type) {
	case LADDER_RATING:
		ret = get_rating_ladder(pconn, q_buffer, out_buffer, ladder_type, eid);
		DB_ER_RETURN(ret, "get_rating_ladder:error %d", ret);
		break;
	case LADDER_LEVEL:
		ret = get_level_ladder(pconn, q_buffer, out_buffer, ladder_type, eid);
		DB_ER_RETURN(ret, "get_level_ladder:error %d", ret);
		break;
	case LADDER_GUILD:
		ret = get_guild_ladder(pconn, q_buffer, out_buffer, ladder_type, gid);
		DB_ER_RETURN(ret, "get_guild_ladder:error %d", ret);
		break;
	case LADDER_COLLECTION:
		ret = get_collection_ladder(pconn, q_buffer, out_buffer, ladder_type, eid);
		DB_ER_RETURN(ret, "get_gold_ladder:error %d", ret);
		break;
	case LADDER_GOLD:
		ret = get_gold_ladder(pconn, q_buffer, out_buffer, ladder_type, eid);
		DB_ER_RETURN(ret, "get_gold_ladder:error %d", ret);
		break;
	case LADDER_CHAPTER:
		ret = get_chapter_ladder(pconn, q_buffer, out_buffer, ladder_type, eid);
		DB_ER_RETURN(ret, "get_chapter_ladder:error %d", ret);
		break;
	}


	// DEBUG_PRINT(0, "get_ladder:out=%s", out_buffer);
	return ret;
}


#define SQL_UPDATE_PROFILE	"UPDATE evil_status SET sex=%d, signature='%s' WHERE eid=%d"
#define SQL_UPDATE_ICON	"UPDATE evil_user SET icon=%d WHERE eid=%d"

// internal use
int in_update_profile(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int len;
	int err;
	int ret;

	int eid;
	int icon;
	int sex;
	char signature[EVIL_SIGNATURE_MAX + 1];
	bzero(signature, sizeof(signature));

	ret = sscanf(in_buffer, IN_UPDATE_PROFILE_SCAN
	, &eid, &icon, &sex, signature);
	if (ret != 4) {
		DB_ER_RETURN(-5, "update_profile:less_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "update_profile:invalid_eid %d", eid);
	}

	if (icon < 0 || icon>EVIL_ICON_MAX) {
		// DB_ER_RETURN(-25, "update_profile:invalid_icon %d", icon);
		DB_ER_RETURN(-25, "%s %d", E_UPDATE_PROFILE_INVALID_ICON, icon);
	}

	if (sex != 0 && sex != 1) {
		// DB_ER_RETURN(-35, "update_profile:invalid_sex %d", sex);
		DB_ER_RETURN(-35, "%s %d", E_UPDATE_PROFILE_INVALID_SEX, sex);
	}

	if (strlen(signature) == 0) {
		sprintf(signature, "%s", "_no_signature");
	}
			

	// global access: using g_query
	len = sprintf(q_buffer, SQL_UPDATE_PROFILE, sex, signature, eid);

	// DEBUG_PRINT(0, "update_profile: [%s]", q_buffer);

	ret = my_query(pconn, q_buffer, len);

	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "update_profile:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2

	// global access: using g_query
	len = sprintf(q_buffer, SQL_UPDATE_ICON, icon, eid); 

	// DEBUG_PRINT(0, "update_profile_icon: [%s]", q_buffer);

	ret = my_query(pconn, q_buffer, len);

	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-65, "update_profile_icon:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2

	sprintf(out_buffer, OUT_UPDATE_PROFILE_PRINT, eid, icon, sex, signature);
	return 0 ;
}



#define SQL_CHECK_FRIEND "SELECT * FROM evil_friend WHERE (eid1=%d AND eid2=%d) OR (eid1=%d AND eid2=%d) LIMIT 1"

int check_friend(MYSQL **pconn, char *q_buffer, int eid1, int eid2)
{
	int ret;
	int len;

	if (eid1 <= MAX_AI_EID || eid2 <= MAX_AI_EID) {
		ERROR_RETURN(-5, "check_friend:invalid_eid %d %d", eid1, eid2);
	}

	len = sprintf(q_buffer, SQL_CHECK_FRIEND, eid1, eid2, eid2, eid1);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "check_friend:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	MYSQL_RES *result;
	int num_row;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "check_friend:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -6;
		ERROR_PRINT(ret, "check_friend:num_row<0 %d %d", eid1, eid2);
		goto cleanup;
	}

	if (num_row==0) {
		// not in friend list
		ret = 0;
	} else {
		// already in friend list
		ret = 1;
	}

cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_ADD_FRIEND "INSERT INTO evil_friend VALUES (%d, %d)"

//XXX should we add date in evil_friend?
int in_friend_add(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;

	int eid1;
	int eid2;

	ret = sscanf(in_buffer, IN_FRIEND_ADD_SCAN
	, &eid1, &eid2);
	if (ret != 2) {
		DB_ER_RETURN(-5, "add_friend:less_input %d", ret);
	}

	if (eid1 <= MAX_AI_EID || eid2 <= MAX_AI_EID || eid1 == eid2) {
		DB_ER_RETURN(-15, "add_friend:invalid_eid %d %d", eid1, eid2);
	}

	// 1. check two people are friend now?
	ret = check_friend(pconn, q_buffer, eid1, eid2);
	if (ret < 0) {
		DB_ER_RETURN(-55, "add_friend:check_frined_error %d %d", eid1, eid2);
	}

	if (ret != 0) {
		// DB_ER_RETURN(-6, "add_friend:already_friend %d %d", eid1, eid2);
		DB_ER_RETURN(-6, "%s %d %d", E_ADD_FRIEND_ALREADY_FRIEND, eid1, eid2);
	}
		
	/*
	len = sprintf(q_buffer, SQL_CHECK_FRIEND, eid1, eid2, eid2, eid1);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "add_friend:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "add_friend:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row>0) {
		ret = -6;
		// already in friend list
		DB_ER_PRINT(ret, "add_friend:already_friend %d %d", eid1, eid2);
		goto cleanup;
	}
	*/


	// 2.add friend
	len = sprintf(q_buffer, SQL_ADD_FRIEND, eid1, eid2);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-65, "add_friend:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		DB_ER_NEG_RETURN(-16, "add_friend:affected_row %d %d\n", eid1, eid2);
	}

	sprintf(out_buffer, OUT_FRIEND_ADD_PRINT, eid1, eid2);
	ret = 0;

	return ret;
}


#define SQL_GET_FRIEND_TOTAL "SELECT count(0) FROM evil_friend WHERE eid1=%d or eid2=%d"
#define SQL_GET_FRIEND "SELECT eid1, eid2, u1.alias, u2.alias, u1.icon, u2.icon from evil_friend LEFT JOIN evil_user u1 ON eid1 = u1.eid LEFT JOIN evil_user u2 ON eid2 = u2.eid WHERE eid1=%d OR eid2=%d LIMIT %d,%d"

#define SQL_GET_FRIEND_TOTAL_SEARCH "SELECT count(0) FROM evil_friend LEFT JOIN evil_user u1 ON eid1=u1.eid LEFT JOIN evil_user u2 ON eid2=u2.eid WHERE (eid1=%d or eid2=%d) AND (u1.alias LIKE '%%%s%%' OR u2.alias LIKE '%%%s%%' OR u1.eid='%s' OR u2.eid='%s')"

#define SQL_GET_FRIEND_SEARCH "SELECT eid1, eid2, u1.alias, u2.alias, u1.icon, u2.icon from evil_friend LEFT JOIN evil_user u1 ON eid1 = u1.eid LEFT JOIN evil_user u2 ON eid2 = u2.eid WHERE (eid1=%d OR eid2=%d) AND (u1.alias LIKE '%%%s%%' OR u2.alias LIKE '%%%s%%' OR u1.eid='%s' OR u2.eid='%s') LIMIT %d,%d"

int in_friend_list(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	int start_num;
	int page_size;
	int total;
	int eid1;
	int eid2;
	char alias[EVIL_ALIAS_MAX+1];
	alias[0] = '\0';
	char *ptr;
	int count;

	ret = sscanf(in_buffer, IN_FRIEND_LIST_SCAN, &eid, &start_num, &page_size, alias);
	if (ret < 3) {
		DB_ER_RETURN(-5, "friend_list:less_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "friend_list:invalid_eid %d", eid);
	}

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	ptr = out_buffer;

	// 1.get friend total count
	if (alias[0] == '\0') {
		len = sprintf(q_buffer, SQL_GET_FRIEND_TOTAL, eid, eid);
	} else {
		len = sprintf(q_buffer, SQL_GET_FRIEND_TOTAL_SEARCH, eid, eid, alias, alias, alias, alias);
	}

	DEBUG_PRINT(0, "q_buffer=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "friend_list:query total err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "friend_list:total_null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row==0) {
		// its normal
		ret = 0;
		sprintf(out_buffer, "%d %d", eid, num_row);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		DB_ER_RETURN(-6, "friend_list:total_row_null");
	}

	total = strtol_safe(row[0], -1);
	// DEBUG_PRINT(0, "friend_list:total=%d", total);
	if (total < 0) {
		DB_ER_RETURN(-16, "friend_list:total_negative");
	}

	if (total == 0) {
		// eid, total, start_num, page_size
		ret = 0;
		sprintf(ptr, "%d %d %d %d", eid, total, 0, 0);
		goto cleanup;
	} else { 
		ptr += sprintf(ptr, "%d %d ", eid, total);
	}


	// 2.get friend info
	if (alias[0] == '\0') {
		len = sprintf(q_buffer, SQL_GET_FRIEND, eid, eid, start_num, page_size);
	} else {
		len = sprintf(q_buffer, SQL_GET_FRIEND_SEARCH, eid, eid, alias, alias, alias, alias, start_num, page_size);
	}
	DEBUG_PRINT(0, "q_buffer2=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "friend_list:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "friend_list:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// eid1, eid2, alias1, alias2, icon1, icon2
	if (field_count != 6) {
		ret = -7;
		DB_ER_PRINT(ret, "friend_list:field_count %d != 6", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		DB_ER_PRINT(ret, "friend_list:friend_list %d", num_row);
		goto cleanup;
	}

	if (num_row==0) {
		// its normal
		ret = 0;
		sprintf(out_buffer, "%d %d %d %d", eid, total, start_num, num_row);
		goto cleanup;
	}

	ptr += sprintf(ptr, "%d %d", start_num, num_row);
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-17, "friend_list:fetch_row_overflow %d", count);
			break;
		}

		eid1 = strtol_safe(row[0], -1);
		eid2 = strtol_safe(row[1], -1);
		if (eid1 <= 0 || eid2 <= 0 || eid1 == eid2) {
			BUG_PRINT(-16, "friend_list:eid_bug %d %d", eid1, eid2);
			continue;
		}

		if (eid1 == eid) {
			// add eid2, alias2, icon2
			if (row[3] == NULL) {		
				sprintf(alias, "_no_alias");
			} else {
				sprintf(alias, "%s", row[3]);
			}
			len = sprintf(ptr, OUT_FRIEND_LIST_ROW_PRINT, row[1], alias, row[5]);
		} else {
			// add eid1, alias1, icon1
			if (row[2] == NULL) {		
				sprintf(alias, "_no_alias");
			} else {
				sprintf(alias, "%s", row[2]);
			}
			len = sprintf(ptr, OUT_FRIEND_LIST_ROW_PRINT, row[0], alias, row[4]);
		}
		ptr += len;
	}

	// DEBUG_PRINT(0, "in_friend_list:out_buffer=%s", out_buffer);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_FRIEND_STA 	"SELECT t1.eid,alias,t1.lv,t1.rating,t1.gold,t1.crystal,t1.gid,t1.gpos,gname,t1.game_count,t1.game_win,t1.game_lose,t1.game_draw,t1.game_run,icon,t1.exp,t1.sex,t1.signature FROM evil_status t1 LEFT JOIN evil_user ON t1.eid=evil_user.eid LEFT JOIN evil_guild ON t1.gid=evil_guild.gid WHERE t1.eid=%d"

int in_friend_sta(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int my_eid;
	int eid;
	int len;
	int friend_flag = 0; // 0==not friend, 1==friend
	evil_user_t user;

	ret = sscanf(in_buffer, IN_FRIEND_STA_SCAN, &my_eid, &eid);
	if (ret != 2) {
		DB_ER_RETURN(ret, "friend_sta:invalid_input");
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "friend_sta:invalid_eid %d", eid);
	}

	if (my_eid <= MAX_AI_EID) {
		DB_ER_RETURN(-25, "friend_sta:invalid_eid %d", eid);
	}

	if (my_eid == eid) {
		// DB_ER_RETURN(-35, "friend_sta:eid_same %d %d", my_eid, eid);
		DB_ER_RETURN(-35, "%s %d %d", E_FRIEND_STA_SAME_EID, my_eid, eid);
	}

	friend_flag = check_friend(pconn, q_buffer, my_eid, eid);
	if (friend_flag < 0) {
		DB_ER_RETURN(-55, "friend_sta:check_frined_error %d %d", my_eid, eid);
	}

	len = sprintf(q_buffer, SQL_FRIEND_STA, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		DB_ER_RETURN(-55, "friend_sta:mysql_errno %d", err);
	}

	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_PRINT(-3, "friend_sta:null_result");
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "friend_sta:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// row[0]=eid [1]=alias [2]=lv [3]=rating [4]=gold
	// [5]=crystal [6]=gid [7]=gpos [8]=gname
	// [9]=game_count, [10]=win, [11]=lose, [12]=draw
	// [13]=run [14]=icon [15]=exp 
	// [16]=sex [17]=signature (TOTAL=18)
	if (field_count != 18 ) {
		ret = -7;
		ERROR_PRINT(ret, "friend_sta:field_count %d != 18", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!
	bzero(&user, sizeof(evil_user_t));

	// assert row[0] == eid
	ret = strtol_safe(row[0], -1);
	if (ret != eid) {  // this is really impossible
		ret = -17;
		ERROR_PRINT(ret, "friend_sta:eid_mismatch %d %d", eid, ret);
		goto cleanup;
	}

	
	// row[0]=eid [1]=alias [2]=lv [3]=rating [4]=gold
	// [5]=crystal [6]=gid [7]=gpos [8]=gname
	// [9]=game_count, [10]=win, [11]=lose, [12]=draw
	// [13]=run [14]=icon [15]=exp 
	// [16]=sex [17]=signature (TOTAL=18)
	user.eid = eid;
	if (row[1] == NULL) {
		sprintf(user.alias, "_no_alias");
	} else {
		sprintf(user.alias, "%s", row[1]);
	}
	user.lv 		= strtol_safe(row[2], 0);
	sscanf(row[3], "%lf", &user.rating);  // beware, there is no default!
	user.gold 		= strtol_safe(row[4], 0);
	user.crystal 	= strtol_safe(row[5], 0);
	user.gid 	= strtol_safe(row[6], 0);
	user.gpos 	= strtol_safe(row[7], 0);
	if (row[8] == NULL) {
		sprintf(user.gname, "_no_guild");
	} else {
		sprintf(user.gname, "%s", row[8]);
	}
	//
	user.game_count= strtol_safe(row[9], 0);
	user.game_win  = strtol_safe(row[10], 0);
	user.game_lose = strtol_safe(row[11], 0);
	user.game_draw = strtol_safe(row[12], 0);
	user.game_run  = strtol_safe(row[13], -77);
	user.icon  = strtol_safe(row[14], 0);
	user.exp  = strtol_safe(row[15], 0);
	user.sex  = strtol_safe(row[16], 0);
	if (row[17] == NULL) {
		sprintf(user.signature, "_no_signature");
	} else {
		sprintf(user.signature, "%s", row[17]);
	}

	sprintf(out_buffer, OUT_FRIEND_STA_PRINT
	, user.eid, user.alias, user.lv, user.rating, user.gold
	, user.crystal, user.gid, user.gpos, user.gname, user.game_count
	, user.game_win, user.game_lose, user.game_draw, user.game_run, user.icon
	, user.exp, user.sex, friend_flag, user.signature);


	ret = 0; // ok to set ret = 0 here

cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_FRIEND_SEARCH "SELECT evil_user.eid,alias,icon,lv FROM evil_user LEFT JOIN evil_status ON evil_user.eid=evil_status.eid WHERE alias LIKE '%%%s%%' OR evil_user.eid = '%s' ORDER BY eid LIMIT 10"

int in_friend_search(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	char alias[EVIL_ALIAS_MAX+1];
	alias[0] = '\0';
	char *ptr;
	int count;
	char info[DB_BUFFER_MAX-10];
	info[0] = '\0';

	ret = sscanf(in_buffer, IN_FRIEND_SEARCH_SCAN, alias);
	if (ret != 1) {
		DB_ER_RETURN(-5, "friend_search:less_input %d", ret);
	}

	len = sprintf(q_buffer, SQL_FRIEND_SEARCH, alias, alias);

	// DEBUG_PRINT(0, "q_buffer:%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "friend_search:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}
	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "friend_search:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// eid, alias, icon, lv
	if (field_count != 4) {
		ret = -7;
		DB_ER_PRINT(ret, "friend_search:field_count %d != 4", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		DB_ER_PRINT(ret, "friend_search:friend_list %d", num_row);
		goto cleanup;
	}

	if (num_row==0) {
		// its normal
		ret = 0;
		sprintf(out_buffer, "%d", num_row);
		goto cleanup;
	}

	ptr = info;
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-17, "friend_search:fetch_row_overflow %d", count);
			break;
		}

		eid = strtol_safe(row[0], -1);
		if (eid <= MAX_AI_EID) {
			continue;
		}

		ptr += sprintf(ptr, OUT_FRIEND_SEARCH_ROW_PRINT, row[0], row[1]
		, row[2], row[3]);
	}

	sprintf(out_buffer, "%d %s", count, info);
	
	// out_buffer = [count] [info1] [info2] ...
	// info = [eid] [alias] [icon] [lv]

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}



#define SQL_FRIEND_DEL	"DELETE FROM evil_friend WHERE (evil_friend.eid1=%d and evil_friend.eid2=%d) or (evil_friend.eid1=%d and evil_friend.eid2=%d)"
int in_friend_del(MYSQL **pconn, char *q_buffer, const char *in_buffer
, char *out_buffer)
{
	int ret;
	int len;

	int eid1;
	int eid2;

	ret = sscanf(in_buffer, IN_FRIEND_DEL_SCAN, &eid1, &eid2);
	if (ret != 2) {
		DB_ER_RETURN(-5, "del_friend:less_input %d", ret);
	}

	if (eid1 <= MAX_AI_EID || eid2 <= MAX_AI_EID || eid1 == eid2) {
		DB_ER_RETURN(-15, "del_friend:invalid_eid %d %d", eid1, eid2);
	}

	// 1. check two people are friend now?
	ret = check_friend(pconn, q_buffer, eid1, eid2);
	if (ret < 0) {
		DB_ER_RETURN(-55, "del_friend:check_friend_error %d %d", eid1, eid2);
	}

	if (ret == 0) {
		DB_ER_RETURN(-6, "%s %d %d", E_DEL_FRIEND_NOT_FRIEND, eid1, eid2);
	}
		
	// 2.del friend
	len = sprintf(q_buffer, SQL_FRIEND_DEL, eid1, eid2, eid2, eid1);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-65, "del_friend:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		DB_ER_NEG_RETURN(-16, "del_friend:affected_row %d %d\n", eid1, eid2);
	}

	sprintf(out_buffer, OUT_FRIEND_DEL_PRINT, eid1, eid2);
	ret = 0;

	return ret;
}


#define SQL_LOAD_PIECE	"SELECT * FROM evil_piece where eid=%d LIMIT 1"
// eid
int in_load_piece(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{

	int ret;
	int len;
	int eid;
	char *ptr;

	ret = sscanf(in_buffer, IN_LOAD_PIECE_SCAN, &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "load_piece:less_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "load_piece:invalid_eid %d", eid);
	}

	len = sprintf(q_buffer, SQL_LOAD_PIECE, eid);

	DEBUG_PRINT(0, "q_buffer:%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "load_piece:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}
	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "load_piece:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		// peter: this is normal, since user may disconnect 
		// before job selection, lcard can be empty!
		// DB_ER_PRINT(ret, "load_piece:empty_row eid=%d", eid);
		DB_ER_PRINT(ret, "%s eid=%d", E_LOAD_PIECE_EMPTY_ROW, eid);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid)
	if (field_count != EVIL_CARD_MAX + 1) {
		ret = -7;
		DB_ER_PRINT(ret, "load_piece:field_count %d != card_count+1 %d",
			field_count, EVIL_CARD_MAX+1);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	// what's the implication?
	ret = strtol_safe(row[0], -1);
	WARN_PRINT(eid-ret, "load_piece:eid %d != row[0] %d", eid, ret);
	
	
	ptr = out_buffer;
	ptr += sprintf(ptr, "%d ", eid);
	for (int i=1; i<=EVIL_CARD_MAX; i++) {
		ret = strtol_safe(row[i], -1);
		if (ret < 0) {
			WARN_PRINT(ret, "load_piece:strtol_safe %s card_id=%d", row[i], i);
			ret = 0;  // manual fix it
		}
		ptr += sprintf(ptr, "%02d", ret);
	}

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}

int in_load_card_piece(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int eid;
	int card_id;
	int size;
	char tmp_buffer[DB_BUFFER_MAX+1];

	ret = sscanf(in_buffer, "%d %d %d", &eid, &card_id, &size);
	if (ret != 3) {
		DB_ER_RETURN(-5, "load_card_piece:less_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "load_piece:invalid_eid %d", eid);
	}

	ret = in_load_piece(pconn, q_buffer, in_buffer, tmp_buffer);
	ERROR_RETURN(ret, "load_card_piece:load_piece_fail");

	sprintf(out_buffer, "%s %d %d", tmp_buffer, card_id, size);
	// ok, we are good, set ret = 0
	ret = 0;
	return ret;
}


// eid, card_id, num
#define IN_UPDATE_PIECE_SCAN "%d %d %d"
#define IN_UPDATE_PIECE_PRINT "%d %d %d"
#define OUT_UPDATE_PIECE_SCAN "%d %d %d"
#define OUT_UPDATE_PIECE_PRINT "%d %d %d"
int in_update_piece(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{

	int ret;
	int eid;
	int card_id;
	int num;

	ret = sscanf(in_buffer, IN_UPDATE_PIECE_SCAN, &eid, &card_id, &num);
	if (ret != 3) {
		DB_ER_RETURN(-5, "update_piece:less_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "update_piece:invalid_eid %d", eid);
	}

	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
		DB_ER_RETURN(-25, "update_piece:invalid_card_id %d", card_id);
	}

	if (num > EVIL_NUM_PIECE_MAX || num < -EVIL_NUM_PIECE_MAX || num == 0) {
		DB_ER_RETURN(-35, "update_piece:invalid_num %d", num);
	}

	ret = update_piece(pconn, q_buffer, eid, card_id, num);
	if (ret < 0) {
		DB_ER_RETURN(-6, "update_piece:update_error %d %d %d", eid, card_id, num);
	}

	sprintf(out_buffer, OUT_UPDATE_PIECE_PRINT, eid, card_id, num);

	return 0;
}


int in_merge_piece(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{

	int ret;
	// int len;
	int eid;
	int card_id;
	int count;
	int gold;
	int crystal;

	ret = sscanf(in_buffer, IN_MERGE_PIECE_SCAN, &eid, &card_id
	, &count, &gold, &crystal);
	if (ret != 5) {
		DB_ER_RETURN(-5, "merge_piece:invalid_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "merge_piece:invalid_eid %d", eid);
	}

	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
		DB_ER_RETURN(-25, "merge_piece:invalid_card_id %d", card_id);
	}

	if (count > EVIL_NUM_PIECE_MAX || count < -EVIL_NUM_PIECE_MAX || count == 0) {
		DB_ER_RETURN(-35, "merge_piece:invalid_count %d", count);
	}

	if (gold < -9999999 || gold > 0) {
		DB_ER_RETURN(-45, "merge_piece:invalid_gold %d", gold);
	}

	if (crystal < -9999999 || crystal > 0) {
		DB_ER_RETURN(-55, "merge_piece:invalid_crystal %d", crystal);
	}
		
	// 1.update piece
	// 2.update card 
	// 3.update money

	// 1.update piece
	ret = update_piece(pconn, q_buffer, eid, card_id, -count);
	if (ret < 0) {
		// DB_ER_RETURN(-2, "merge_piece:piece_not_enough %d", ret);
		DB_ER_RETURN(-2, "%s %d", E_MERGE_PIECE_PIECE_NOT_ENOUGH, count);
	}

	// 2.update card 
	ret = update_card(pconn, q_buffer, eid, card_id, 1);
	if (ret != 0) {
		// roll back
		update_piece(pconn, q_buffer, eid, card_id, count);
		// DB_ER_RETURN(-12, "merge_piece:card_out_bound %d", ret);
		DB_ER_RETURN(-12, "%s %d", E_MERGE_PIECE_CARD_OUT_BOUND, ret);
	}

	// 3.update money
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	if (ret < 0) {
		update_piece(pconn, q_buffer, eid, card_id, count);
		update_card(pconn, q_buffer, eid, card_id, -1);
		// DB_ER_RETURN(-22, "merge_piece:money_not_enough %d %d", gold, crystal);
		DB_ER_RETURN(-22, "%s %d %d", E_MERGE_PIECE_MONEY_NOT_ENOUGH, gold, crystal);
	}

	sprintf(out_buffer, OUT_MERGE_PIECE_PRINT, eid, card_id
	, -count, gold, crystal);
	ret = 0;

	return ret;
}


int in_pick_piece(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer) {
	int ret;

	int eid;
	int pick_type; // 0 for gold pick, 1 for crystal pick
	int loc; 
	int card_id;
	int count;
	int gold;
	int crystal;
	int change_count;

	// eid, card_id, need_piece_count, gold(-) crystal(-)
	ret = sscanf(in_buffer, IN_PICK_PIECE_SCAN, &eid, &pick_type, &loc
	, &card_id, &count, &gold, &crystal);
	if (ret < 7) {
		DB_ER_RETURN(-5, "in_pick_piece:less_input %d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_pick_piece:invalid_eid %d", eid);
	}

	if (card_id > EVIL_CARD_MAX || card_id <= 0) {
		DB_ER_RETURN(-25, "in_pick_piece:invalid_card_id %d", card_id);
	}

	if (count > EVIL_NUM_PIECE_MAX || count < -EVIL_NUM_PIECE_MAX || count == 0) {
		DB_ER_RETURN(-35, "in_pick_piece:invalid_count %d", count);
	}

	if (gold < -9999999 || gold > 0) {
		DB_ER_RETURN(-45, "in_pick_piece:invalid_gold %d", gold);
	}

	if (crystal < -9999999 || crystal > 0) {
		DB_ER_RETURN(-55, "in_pick_piece:invalid_crystal %d", crystal);
	}

	// 1.update money, must
	// 2.update piece count, can fail

	// 1.
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	// DB_ER_RETURN(ret, "in_pick_piece:update_money_fail %d %d", gold, crystal);
	DB_ER_RETURN(ret, "%s %d %d", E_PICK_PIECE_MONEY_NOT_ENOUGH, gold, crystal);

	// 2.
	change_count = 0;
	ret = update_piece_ultimate(pconn, q_buffer, eid, card_id, count, &change_count);
	// DB_ER_RETURN(ret, "in_pick_piece:update_piece_ultimate_fail %d %d %d"
	// , eid, card_id, count);
	DB_ER_RETURN(ret, "%s %d %d %d", E_PICK_PIECE_UPDATE_PIECE_FAIL
	, eid, card_id, count);

	DEBUG_PRINT(0, "in_pick_piece:change_count = %d", change_count);

	// eid, pick_type, loc, card_id, change_count, gold(-), crystal(-)
	sprintf(out_buffer, OUT_PICK_PIECE_PRINT, eid, pick_type, loc
	, card_id, change_count, gold, crystal);

	return 0;
}

//#define SQL_GET_PAY_COUNT	"SELECT COUNT(*) FROM evil_pay WHERE player_id=%d AND game_money_type=%d AND status>=0"
#define SQL_GET_PAY_COUNT	"SELECT s1.*,s2.* FROM (SELECT COUNT(1) FROM evil_pay WHERE player_id=%d AND game_money_type=%d AND status>=0) AS s1 LEFT JOIN (SELECT COUNT(1) FROM evil_pay WHERE player_id=%d AND game_money_type=%d AND status=1) s2 ON 1=1"
int __get_pay_log_succ_count(MYSQL **pconn, char *q_buffer, char *out_buffer, int eid, int &pay_log_count, int &vip_pay_log_count)
{
	int len;
	int ret;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_GET_PAY_COUNT
	, eid, 1, eid, 1);	// 1 is money_type_crystal

//	DEBUG_PRINT(0, "q_buffer[%s]", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "get_pay_log_count:mysql_errno %d", err);
	}

	// retrieve the data
	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "get_pay_log_count:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row != 1) {
		ret = -7;
		DB_ER_PRINT(ret, "get_pay_log_count: num_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	if (field_count != 2) {
		ret = -17;
		DB_ER_PRINT(ret, "get_pay_log_count:invalid_field_count %d", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!
	if (row == NULL) {
		ret = -13;
		DB_ER_PRINT(ret, "get_pay_log_count:null_row");
		goto cleanup; // cleanup and early exit
	}

	pay_log_count = strtol_safe(row[0], 0);
	vip_pay_log_count = strtol_safe(row[1], 0);

	ret = 0;  // make sure ret is OK (0)
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}

#define SQL_LOG_PAY "INSERT INTO evil_pay VALUES (%ld, NOW(), %d, %d, %d, %d, %d, %d, %d, '%s')"
int add_pay_log(MYSQL **pconn, char *q_buffer, long order_no
, int pay_code, int channel, int price, int game_money_type,
int game_money, int eid, int status, const char* remark)
{
	int len;
	int ret;

	len = sprintf(q_buffer, SQL_LOG_PAY, order_no, pay_code
		, channel, price, game_money_type, game_money, eid
		, status, remark);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		WARN_PRINT(-26, "in_pay:log_pay_info_error");
	}
	return 0;
}

#define SQL_GET_MONTHLY_END_DATE "SELECT UNIX_TIMESTAMP(monthly_end_date) FROM evil_status WHERE eid=%d LIMIT 1"
#define SQL_SAVE_MONTHLY_END_DATE "UPDATE evil_status SET monthly_end_date=FROM_UNIXTIME(%ld) WHERE eid=%d"
int __increase_monthly_date(MYSQL **pconn, char *q_buffer, int eid, time_t &monthly_end_date, int monthly_add_time)
{
	int len;
	int ret;
	int err;

	time_t current_date;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	if (monthly_add_time <= 0)
	{
		ERROR_RETURN(-56, "increase_monthly_date:add_time %d eid %d"
		, monthly_add_time, eid);
	}

	len = sprintf(q_buffer, SQL_GET_MONTHLY_END_DATE, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "increase_monthly_date:query err=%d", mysql_errno(*pconn)); 
	}
	
	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_PRINT(-3, "increase_monthly_date:null_result");
		return -1;
	}

	num_row = mysql_num_rows(result);
	if (num_row != 1) {
		ret = -7;
		DEBUG_PRINT(ret, "increase_monthly_date:num_row[%d]!=1 eid %d"
		, num_row, eid);
		goto cleanup;
	}
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -23;
		ERROR_PRINT(ret, "increase_monthly_date:null_row");
		goto cleanup; // cleanup and early exit
	}

	monthly_end_date = strtolong_safe(row[0], 0);
	mysql_free_result(result);

	current_date = time(NULL);
	if (monthly_end_date < current_date)
	{
		monthly_end_date = current_date;
	}


	len = sprintf(q_buffer, SQL_SAVE_MONTHLY_END_DATE
	, monthly_end_date + monthly_add_time, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-65, "increase_monthly_date:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_NEG_RETURN(-16, "increase_monthly_date:update_error");
	}

	monthly_end_date += monthly_add_time;
	return 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_ADD_GOLD "UPDATE evil_status SET gold = gold+%d where eid = %d"
#define SQL_ADD_CRYSTAL "UPDATE evil_status SET crystal = crystal+%d where eid = %d"
int in_pay(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int n;
	const char *in_ptr;
	char *out_ptr;
	int eid;
	long pay_id;
	int game_money_type;
	int game_money;
	int channel;
	int price;
	int pay_code;
	int first_pay_check;
	int monthly_flag;
	time_t monthly_end_date;
	int monthly_add_time;
	int first_vip_extra_gold;
	int first_vip_extra_crystal;
	int first_vip_extra_card_kind;
	int first_vip_extra_card_list[MAX_FIRST_VIP_CARD_KIND][2];
	int pay_log_count;
	int vip_pay_log_count;
	int pay_status = 0;
	char pay_log_msg[50];
	bzero(pay_log_msg, sizeof(pay_log_msg));
	int is_first_double_pay = 0, is_first_vip_pay = 0;
	int extra_gold = 0, extra_crystal = 0;
	int extra_card_kind = 0;
	int extra_card_list[20][2];
	bzero(extra_card_list, sizeof(extra_card_list));

	ret = sscanf(in_buffer, IN_PAY_SCAN, &pay_id, &eid
	, &game_money_type, &game_money, &channel, &price, &pay_code
	, &first_pay_check, &monthly_flag, &monthly_end_date
	, &monthly_add_time, &first_vip_extra_gold
	, &first_vip_extra_crystal, &first_vip_extra_card_kind, &n);
	if (ret != 14) {
		add_pay_log(pconn, q_buffer, pay_id
			, pay_code, channel, price, game_money_type,
			game_money, eid, -5, "less_input_error");
		DB_ER_RETURN(-5, "in_pay:less_input %d pay_id=%ld", ret, pay_id);
	}
	in_ptr = in_buffer + n;
	DEBUG_PRINT(0, "buffer[%s] vip_data[%s]", in_buffer, in_ptr);
	for (int i = 0; i < first_vip_extra_card_kind; i++)
	{
		ret = sscanf(in_ptr, "%d %d %n", &first_vip_extra_card_list[i][0]
		, &first_vip_extra_card_list[i][1], &n);
		if (ret != 2)
		{
			DB_ER_RETURN(-5, "in_pay:less_input %d pay_id=%ld", ret, pay_id);
		}
		in_ptr += n;
	}

	if (eid <= 0) {
		add_pay_log(pconn, q_buffer, pay_id
			, pay_code, channel, price, game_money_type,
			game_money, eid, -15, "invalid_eid");
		DB_ER_RETURN(-15, "in_pay:invalid_eid %d pay_id=%ld", eid, pay_id);
	}

	if (game_money_type != 0 && game_money_type != 1) {
		add_pay_log(pconn, q_buffer, pay_id
			, pay_code, channel, price, game_money_type,
			game_money, eid, -25, "invalid_game_money_type");
		DB_ER_RETURN(-25, "in_pay:invalid_game_money_type %d pay_id=%ld"
		, game_money_type, pay_id);
	}

	if (game_money <= 0) {
		add_pay_log(pconn, q_buffer, pay_id
			, pay_code, channel, price, game_money_type,
			game_money, eid, -35, "invalid_game_money");
		DB_ER_RETURN(-35, "in_pay:invalid_game_money %d pay_id=%ld", game_money, pay_id);
	}

	pay_status = PAY_STATUS_OK;
	if (game_money_type == 0) {
		len = sprintf(q_buffer, SQL_ADD_GOLD, game_money, eid);
	} 
	if (game_money_type == 1) {

		ret = __get_pay_log_succ_count(pconn, q_buffer, out_buffer, eid
		, pay_log_count, vip_pay_log_count);
		WARN_PRINT(ret, "in_pay:get_first_pay_log_fail");
		// first pay double check
		if (ret == 0)
		{
			if (pay_log_count == 0 && first_pay_check)
			{
				is_first_double_pay = 1;
				extra_crystal += game_money;
//				extra_gold = PAY_FIRST_CHARGE_GOLD;		-- not use
				sprintf(pay_log_msg
				, "First_pay_double:extra_gold[%d] extraCrystal[%d]"
				, extra_gold, extra_crystal);
			}
			if (vip_pay_log_count == 0 && monthly_flag)
			{
				pay_status = PAY_STATUS_FIRST_VIP;
				is_first_vip_pay = 1;
				extra_gold += first_vip_extra_gold;
				extra_crystal += first_vip_extra_crystal;
			}
		}

		len = sprintf(q_buffer, SQL_ADD_CRYSTAL, game_money, eid);
	}
		
	DEBUG_PRINT(0, "%s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		add_pay_log(pconn, q_buffer, pay_id
			, pay_code, channel, price, game_money_type,
			game_money, eid, -55, "query_error");
		DB_ER_RETURN(-55, "in_pay:query err=%d pay_id=%ld", mysql_errno(*pconn), pay_id); 
	}

	ret = mysql_affected_rows(*pconn);
	// max card_count = 9 is normal
	if (ret == 0) {
		add_pay_log(pconn, q_buffer, pay_id
			, pay_code, channel, price, game_money_type,
			game_money, eid, -6, "affected_row_error");
		DB_ER_RETURN(-6, "in_pay:affected_row wrong eid_not_found %d %d pay_id=%ld", eid, ret, pay_id);
	}
	if (ret < 0) {
		add_pay_log(pconn, q_buffer, pay_id
			, pay_code, channel, price, game_money_type,
			game_money, eid, -16, "affected_row_error");
		DB_ER_RETURN(-16, "in_pay:affected_row wrong %d pay_id=%ld", ret, pay_id);
	}

	if (extra_gold > 0 || extra_crystal > 0)
	{
		ret = update_money(pconn, q_buffer, eid, extra_gold, extra_crystal);
		ERROR_PRINT(ret, "in_pay:update_money_fail_first_pay gold[%d] crystal[%d]"
		, extra_gold, extra_crystal);		// through down
	}

	if (is_first_double_pay) {
		// hard code for hero Nishaven[5]
		extra_card_list[extra_card_kind][0] = 5;
		extra_card_list[extra_card_kind][1] = 1;
		extra_card_kind++;
		// hard code for Fireball[71]
		extra_card_list[extra_card_kind][0] = 71;
		extra_card_list[extra_card_kind][1] = 4;
		extra_card_kind++;
	}
	if (is_first_vip_pay) {
		for (int i = 0; i < first_vip_extra_card_kind; i++)
		{
			extra_card_list[extra_card_kind][0] = first_vip_extra_card_list[i][0];
			extra_card_list[extra_card_kind][1] = first_vip_extra_card_list[i][1];
			extra_card_kind++;
		}
	}
	if (extra_card_kind > 0) {
		ret = __update_card_list(pconn, q_buffer, eid
		, extra_card_list, extra_card_kind);
		ERROR_PRINT(ret, "in_pay:update_extra_card_list eid[%d]"
		, eid); // through down
	}

	// TODO: if pay_code is in vip_list, increase player's monthly_end_time
	if (monthly_flag)
	{
		ret = __increase_monthly_date(pconn, q_buffer, eid, monthly_end_date
		, monthly_add_time);
		ERROR_NEG_PRINT(ret
		, "in_pay:increate_monthly_date_fail eid[%d] monthly_add_time[%d]"
		, eid, monthly_add_time);
	}


	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, OUT_PAY_PRINT, pay_id, eid
	, game_money_type, game_money, channel, price
	, extra_gold, extra_crystal
	, monthly_flag, monthly_end_date, extra_card_kind);
	for (int i = 0; i < extra_card_kind; i++)
	{
		out_ptr += sprintf(out_ptr, " %d %d", extra_card_list[i][0]
		, extra_card_list[i][1]);
	}
	
	add_pay_log(pconn, q_buffer, pay_id
		, pay_code, channel, price, game_money_type,
		game_money, eid, pay_status, pay_log_msg);

	ret = 0;
	return ret;
}


#define SQL_GET_COURSE "SELECT eid, course FROM evil_status WHERE eid=%d"

int in_get_course(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid;

	ret = sscanf(in_buffer, IN_GET_COURSE_SCAN, &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "get_course:invalid_input");
	}
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "get_course:invalid_eid %d", eid);
	}

	len = sprintf(q_buffer, SQL_GET_COURSE, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "get_course:mysql_errno %d", err);
	}

	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "get_course:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -13; // null case?
		DB_ER_PRINT(ret, "get_course:negative_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// eid, course
	if (field_count != 2) {
		ret = -17; // null case?
		DB_ER_PRINT(ret, "get_course:invalid_field_count %d", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!
	if (row == NULL) {
		ret = -23;
		DB_ER_PRINT(ret, "get_course:null_row");
		goto cleanup; // cleanup and early exit
	}

	sprintf(out_buffer, OUT_GET_COURSE_PRINT, row[0], row[1]);


	ret = 0;  // make sure ret is OK (0)
cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_SAVE_COURSE "UPDATE evil_status SET course=%d WHERE eid=%d AND %d<=%d"

int in_save_course(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	int course;

	ret = sscanf(in_buffer, IN_SAVE_COURSE_SCAN, &eid, &course);
	if (ret != 2) {
		DB_ER_RETURN(-5, "save_course:invalid_input");
	}
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "save_course:invalid_eid %d", eid);
	}
	if (course < 0 || course > INT32_MAX) {
		DB_ER_RETURN(-25, "save_course:invalid_course %d", course);
	}

	len = sprintf(q_buffer, SQL_SAVE_COURSE, course, eid, course, INT32_MAX);
	// DEBUG_PRINT(0, "q_buffer = %s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		int err = mysql_errno(*pconn);
		DB_ER_RETURN(-55, "save_course:mysql_errno %d", err);
	}

	ret = mysql_affected_rows(*pconn);
	if (ret == 0) {
		// this is normal
		// WARN_PRINT(-6, "save_course:affected_row_0_course_same %d %d", eid, course);
	}
	if (ret < 0) {
		DB_ER_RETURN(-16, "save_course:affected_row_wrong %d %d", eid, course);
	}

	sprintf(out_buffer, OUT_SAVE_COURSE_PRINT, eid, course);
	return 0;
}

#define SQL_LOAD_TWO_DECK 	"SELECT * FROM evil_deck WHERE eid=%d OR eid=%d ORDER BY eid %s LIMIT 2"
// it is actually load_deck
int in_challenge(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int eid1;
	int eid2;
	int eid;
	int slot;
	char * card;
	int count;
	char *ptr;

	ret = sscanf(in_buffer, IN_CHALLENGE_SCAN, &eid1, &eid2);
	if (ret != 2) {
		DB_ER_RETURN(-5, "challenge:invalid_input");
	}
	if (eid1 <= 0) {
		DB_ER_RETURN(-15, "challenge:invalid_eid1");
	}
	if (eid2 <= 0) {
		DB_ER_RETURN(-15, "challenge:invalid_eid2");
	}
	
	// make sure output eid1 is challenger, eid2 is receiver
	if (eid1 > eid2) {
		len = sprintf(q_buffer, SQL_LOAD_TWO_DECK, eid1, eid2, "DESC");
	} else {
		len = sprintf(q_buffer, SQL_LOAD_TWO_DECK, eid1, eid2, "ASC");
	}

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "challenge:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "challenge:query: %s", q_buffer);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "challenge:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		DB_ER_PRINT(ret, "%s %d %d", E_LOAD_DECK_EMPTY_ROW, eid1, eid2);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid) + 1 (slot)
	if (field_count != EVIL_CARD_MAX + 2) {
		ret = -7;
		DB_ER_PRINT(ret, "challenge:field_count %d != card_count+2 %d",
			field_count, EVIL_CARD_MAX+2);
		goto cleanup;
	}

	ptr = out_buffer;
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > 2) {
			DB_ER_RETURN(-65, "challenge:result_count > 2 %d %d", eid1, eid2);
		}

		// what's the implication?
		eid = strtol_safe(row[0], -1);
		// WARN_PRINT(eid-ret, "challenge:eid %d != row[0] %d", eid, ret);
		if (eid1 != eid && eid2 != eid) {
			DB_ER_RETURN(-65, "challenge:result_eid_error %d %d %d"
				, eid, eid1, eid2);
		}

		slot = strtol_safe(row[1], -1);
		if (slot < 0) {
			DB_ER_RETURN(-75, "challenge:result_slot_error %d %d"
				, eid, slot);
		}
		
		if (count == 1) {
			len = sprintf(ptr, "%d ", eid);
		} else {
			len = sprintf(ptr, " %d ", eid);
		}
		card = ptr + len;
		// card[] is zero-based
		for (int i=0; i<EVIL_CARD_MAX; i++) {
			char * data = row[i+2];
			if (NULL==data) {
				card[i] = '0' + 0;
			} else {
				ret = strtol_safe(data, -1);
				if (ret < 0) {
					WARN_PRINT(ret, "challenge:strtol_safe %s card_id=%d", data, i);
					ret = 0;  // manual fix it
				}
				if (ret > 4) {	// limit the num of same card in deck to 4
					WARN_PRINT(ret, "challenge:card[%d]>4 (%d) eid=%d"
					, i, ret, eid);
					ret = 4;
				}
				card[i] = '0' + (char)ret;
			}
		}
		card[EVIL_CARD_MAX] = '\0'; // null terminate it
		ptr = &card[EVIL_CARD_MAX];
	}

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}


// may have list of mission
#define SQL_LOAD_MISSION 	"SELECT eid, mid, n1, status, UNIX_TIMESTAMP(last_update) FROM evil_mission WHERE eid=%d ORDER BY mid DESC"

int in_load_mission(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int count;
	int eid;
	int tmp_eid;
	mission_t mis;
	char *ptr;

	ret = sscanf(in_buffer, IN_LOAD_MISSION_SCAN, &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "load_mission:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "load_mission:invalid_eid");
	}
	
	len = sprintf(q_buffer, SQL_LOAD_MISSION, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "load_mission:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}


	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "load_mission:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -6;
		DB_ER_PRINT(ret, "%s %d", E_LOAD_MISSION, eid);
		goto cleanup;
	}
	// num_row == 0 is normal, fall through

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid)
	if (field_count != 5) {
		ret = -7;
		DB_ER_PRINT(ret, "load_mission:field_count %d != 5",field_count);
		goto cleanup;
	}

	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d", eid, num_row);
	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;

		// what's the implication?
		tmp_eid 		= strtol_safe(row[0], -1);
		mis.mid 		= strtol_safe(row[1], -1);
		mis.n1 			= strtol_safe(row[2], -1);
		mis.status 		= strtol_safe(row[3], -1);
		mis.last_update 	= strtolong_safe(row[4], -1);

		if (tmp_eid != eid) {
			DB_ER_RETURN(-65, "load_mission:result_eid_error %d!=%d"
				, eid, tmp_eid);
		}

		// 7 %d  and 1 %ld  (eid is omitted)
		ptr += sprintf(ptr, OUT_LOAD_MISSION_PRINT
		, mis.mid, mis.n1, mis.status, mis.last_update);
	}

	if (count != num_row) {
		DB_ER_RETURN(-77, "load_mission:count_mismatch eid=%d  %d!=%d"
			, eid, count, num_row);
	}

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}

// peter: mission-fix, delete + insert is too heavy, make it REPLACE
// #define SQL_DELETE_MISSION "DELETE FROM evil_mission WHERE eid=%d"
#define SQL_SAVE_MISSION 	"REPLACE INTO evil_mission VALUES "
// @ref db-init.sql : evil_mission all fields
int in_save_mission(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int count;
	int eid;
	int n;
	mission_t mis;
	const char *ptr;
	char *qptr;

	ret = sscanf(in_buffer, "%d %d %n", &eid, &count, &n);

	if (ret != 2) {
		DB_ER_RETURN(-5, "save_mission:sscanf %d", ret);
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "save_mission:eid %d", eid);
	}
	if (count < 0) {
		DB_ER_RETURN(-25, "save_mission:count %d", count);
	}
	// count = 0 is normal (only delete, no insert)

	// peter: mission-fix do not delete
//	len = sprintf(q_buffer, SQL_DELETE_MISSION, eid);
//	ret = my_query(pconn, q_buffer, len);
//	if (ret != 0) {
//		DB_ER_RETURN(-55, "save_mission:delete_query err=%d"
//		, mysql_errno(*pconn)); 
//		return -55; // safety, should never run
//	}
	// note: no need to get result (delete from)
	// no need to check mysql_affected_rows(), can delete empty (0)

	// ok, we have deleted the mission for eid
	// now: insert the record:  first construct q_buffer contains
	// all values


	if (count == 0) {
		WARN_PRINT(-6, "save_mission:count = 0 eid=%d", eid);
		sprintf(out_buffer, "%d %d", eid, 0);
		return 0;
	}
	ptr = in_buffer + n;
	qptr = q_buffer + sprintf(q_buffer, SQL_SAVE_MISSION);
	for (int i=0; i<count; i++) {
		char sep = ',';	// separator
		if (i==0) sep = ' ' ;

		// mission-fix : only save variable
		ret = sscanf(ptr, IN_SAVE_MISSION_SCAN, &mis.mid
		, &mis.status , &mis.n1 , &mis.last_update, &n);
		if (ret != 4) {
			// we can return, not yet insert
			DB_ER_RETURN(-35, "save_mission:for_loop_sscanf %d", ret);
		}
		ptr += n; // safe for n>=0
		
		
		// mission-fix
		// eid, mid, status, n1, last_update
		qptr += sprintf(qptr, "%c(%d,%d,%d,%d,FROM_UNIXTIME(%ld))"
		, sep, eid, mis.mid, mis.status, mis.n1, mis.last_update);

		// avoid buffer overflow
		if (qptr - q_buffer > DB_BUFFER_MAX - 300) {
			BUG_PRINT(-22, "save_mission:db_buffer overflow eid=%d", eid);
			break;  // save the earlier record
		}
	}

	// DEBUG_PRINT(0, "save_mission:q_buffer %s", q_buffer);

	len = strlen(q_buffer);

	// core logic: INSERT the mission
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-65, "save_mission:insert_query err=%d"
		, mysql_errno(*pconn)); 
		return -65; // safety, should never run
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	// REPLACE can have no affected rows, because all the missions are the same?
	if (ret < 0) {
		ERROR_NEG_RETURN(-6, "save_mission:affected_row wrong %d\n", ret);
	}
	sprintf(out_buffer, "%d %d", eid, ret);

	return 0; // 0 for ok
}



#define SQL_MISSION_REWARD	"UPDATE evil_status SET gold=gold+(%d),crystal=crystal+(%d),exp=exp+(%d),lv=lv+(%d) WHERE eid=%d"
// use db_add_card()

int in_mission_reward(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int n;
	int eid;
	int mid;
	int gold, crystal;
	int power;
	int exp, lv;
	int card_count;
	int card_list[MAX_MISSION_REWARD_CARD][2];
	int piece_count;
	int piece_list[MAX_MISSION_REWARD_PIECE][2];
	const char* in_ptr;
	bzero(card_list, sizeof(card_list));
	bzero(piece_list, sizeof(piece_list));

	in_ptr = in_buffer;
	ret = sscanf(in_ptr, "%d %d %d %d %d %d %d %n", &eid
	, &mid, &exp, &gold, &crystal, &power, &lv, &n);  // all data are offset
	if (ret != 7) {
		DB_ER_RETURN(-15, "in_mission_reward:sscanf %d", ret);
	}
	in_ptr += n;
	ret = sscanf(in_ptr, "%d %n", &card_count, &n);
	if (ret != 1) {
		DB_ER_RETURN(-25, "in_mission_reward:sscanf %d", ret);
	}
	in_ptr += n;
	for (int cdx = 0; cdx < card_count; cdx++) {
		ret = sscanf(in_ptr, "%d %n", &(card_list[cdx][0]), &n);
		if (ret != 1) {
			DB_ER_RETURN(-35, "in_mission_reward:sscanf %d", ret);
		}
		// TODO temp hard code
		card_list[cdx][1] = 1;
		in_ptr += n;
	}
	ret = sscanf(in_ptr, "%d %n", &piece_count, &n);
	if (ret != 1) {
		DB_ER_RETURN(-45, "in_mission_reward:sscanf %d", ret);
	}
	in_ptr += n;
	for (int pdx = 0; pdx < piece_count; pdx++) {
		ret = sscanf(in_ptr, "%d %d %n", &(piece_list[pdx][0])
		, &(piece_list[pdx][1]), &n);
		if (ret != 2) {
			DB_ER_RETURN(-55, "in_mission_reward:sscanf %d", ret);
		}
		in_ptr += n;
	}

	len = sprintf(q_buffer, SQL_MISSION_REWARD, gold, crystal
	, exp, lv, eid);

	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		int err = mysql_errno(*pconn);
		// no need to check 1062, as REPLACE is always replace dup row
		DB_ER_RETURN(-55, "in_mission_reward:mysql_errno %d", err);
	}


	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret != 1) {
		DB_ER_RETURN(-7, "in_mission_reward:affected_row wrong %d", ret);
	}

	ret = update_power(pconn, q_buffer, eid, power);
	if (ret < 0) {
		BUG_PRINT(-17, "in_mission_reward:add_power %d eid=%d", ret, eid);
	}

	// add card
	ret = __update_card_list(pconn, q_buffer, eid, card_list, card_count);
	if (ret < 0) {
		BUG_PRINT(-27, "in_mission_reward:add_card %d eid=%d", ret, eid);
	}
	ret = __update_piece_list(pconn, q_buffer, eid, piece_list, piece_count);
	if (ret < 0) {
		BUG_PRINT(-37, "in_mission_reward:add_piece %d eid=%d", ret, eid);
	}

	sprintf(out_buffer, "%s", in_buffer);

	return 0;
}





#define SQL_INSERT_FIRST_SLOT	"INSERT INTO evil_slot (eid,slot,name) VALUES (%d,1,'%s')"
int insert_first_slot(MYSQL **pconn, char *q_buffer, int eid)
{
	int len;
	int err;
	int ret;

	len = sprintf(q_buffer, SQL_INSERT_FIRST_SLOT, eid, "slot1");

	printf("--- insert_first_slot:query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no need to check 1062, as REPLACE is always replace dup row
		WARN_PRINT(err==1062, "insert_first_slot:err=1026 impossible");
		ERROR_RETURN(-55, "insert_first_slot:mysql_errno %d", err);
	}


	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret != 1) {
		ERROR_RETURN(-7, "insert_first_slot:affected_row wrong %d", ret);
	}

	return 0;
}

#define SQL_LOAD_SLOT_LIST 	"SELECT slot,name FROM evil_slot WHERE eid=%d ORDER BY slot ASC"
int load_slot_list(MYSQL **pconn, char *q_buffer, char *out_buffer, int eid)
{
	int ret;
	int len;
	char *ptr;
	int count;
	count = 0;
	int id;
	const char *name;

	len = sprintf(q_buffer, SQL_LOAD_SLOT_LIST, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "load_slot_list:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "load_slot_list:query: %s", q_buffer);
	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "load_slot_list:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row==0) {
		ret = 0;
		// first 0 is type, second 0 is list count
		sprintf(out_buffer, "%d 0 0", eid);
		WARN_PRINT(ret, "load_slot_list:no_slot eid=%d", eid);
		goto cleanup;
	}
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "load_slot_list:load_slot_list_err eid=%d", eid);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// id, name
	if (field_count != 2) {
		ret = -7;
		ERROR_PRINT(ret, "load_slot_list:field_count %d != 2"
		, field_count);
		goto cleanup;
	}

	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d", eid, num_row);
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_RETURN(-7, "load_slot_list:count_out_bound %d %d"
			, eid, num_row);
		}
		
		id = strtol_safe(row[0], -1);
		if (id <= 0) {
			BUG_RETURN(-6, "load_slot_list:id<=0 %d %d"
			, eid, id);
		}

		name = row[1];
		if (name == NULL || name[0] == '\0') {
			name = "_no_name";
		}

		ptr += sprintf(ptr, " %d %.30s", id, name);
	}
	
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_LOAD_SLOT 	"SELECT * FROM evil_slot WHERE eid=%d AND slot=%d"
int load_slot(MYSQL **pconn, char *q_buffer, char *out_buffer, int eid, int id)
{
	int ret;
	int len;
	const char *name;
	char * card;
	
	len = sprintf(q_buffer, SQL_LOAD_SLOT, eid, id);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "load_slot:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "load_slot:query: %s", q_buffer);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "load_slot:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		// peter: this is normal, since user may disconnect 
		// before job selection, lcard can be empty!
		// DB_ER_PRINT(ret, "load_slot:empty_row eid=%d", eid);
		ERROR_PRINT(ret, "load_slot:no_such_slot eid=%d", eid);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid) + 1 (id) + 1 (name)
	if (field_count != EVIL_CARD_MAX + 3) {
		ret = -7;
		ERROR_PRINT(ret, "load_slot:field_count %d != card_count+1 %d",
			field_count, EVIL_CARD_MAX+1);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	// what's the implication?
	ret = strtol_safe(row[0], -1);
	WARN_PRINT(eid-ret, "load_slot:eid %d != row[0] %d", eid, ret);
	
	ret = strtol_safe(row[1], -1);
	if (ret != id) {
		ERROR_PRINT(-7, "load:slot:id_mismatch %d %d", eid, id);
		goto cleanup;
	}

	name = row[2];
	if (name == NULL || name[0] == '\0') {
		name = "no_name";
	}
	
	len = sprintf(out_buffer, "%d %d %.30s ", eid, id, name);
	card = out_buffer + len;
	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+3];
		if (NULL==data) {
			card[i] = '0' + 0;
		} else {
			ret = strtol_safe(data, -1);
			if (ret < 0) {
				WARN_PRINT(ret, "load_slot:strtol_safe %s card_id=%d", data, i);
				ret = 0;  // manual fix it
			}
			if (ret > 4) {	// limit the num of same card in deck to 4
				WARN_PRINT(ret, "load_slot:card[%d]>4 (%d) eid=%d"
				, i, ret, eid);
				ret = 4;
			}
			card[i] = '0' + (char)ret;
		}
	}
	card[EVIL_CARD_MAX] = '\0'; // null terminate it

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}

int in_slot_list(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;

	ret = sscanf(in_buffer, IN_SLOT_LIST_SCAN, &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "slot_list:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "slot_list:invalid_eid");
	}

	ret = load_slot_list(pconn, q_buffer, out_buffer, eid);
	if (ret < 0) {
		DB_ER_RETURN(-6, "slot_list:err");
	}

	return 0;
}

int in_load_slot(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int id;
	/*
	int slot;
	int n;
	const char * ptr;
	char name[EVIL_ALIAS_MAX + 5];
	*/

	ret = sscanf(in_buffer, IN_LOAD_SLOT_SCAN, &eid, &id);
	if (ret != 2) {
		DB_ER_RETURN(-5, "load_slot:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "load_slot:invalid_eid");
	}
	if (id <= 0) {
		DB_ER_RETURN(-25, "load_slot:invalid_id");
	}

	// printf("load_slot:eid=%d id=%d\n", eid, id);

	ret = load_slot(pconn, q_buffer, out_buffer, eid, id);
	if (ret < 0) {
		DB_ER_RETURN(-16, "load_slot:err");
	}

	/*
	// save slot to deck
	ret = sscanf(out_buffer, "%d %d %s %n", &eid, &slot, name, &n);
	if (ret != 3) {
		DB_ER_RETURN(-7, "load_slot:sscanf_err %d", ret);
	}
	ptr = out_buffer + n; // ptr is card[400]
	ret = save_deck(pconn, q_buffer, eid, slot, ptr);
	if (ret < 0) {
		DB_ER_RETURN(-26, "load_slot:save_deck");
	}
	*/
	return 0;
}

#define SQL_CHECK_SLOT_EXIST	"SELECT * FROM evil_slot WHERE eid=%d AND slot=%d LIMIT 1"

int check_slot_exist(MYSQL **pconn, char *q_buffer, int eid, int id)
{
	int ret;
	int len;
	len = sprintf(q_buffer, SQL_CHECK_SLOT_EXIST, eid, id);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_PRINT(-55, "check_slot_exist:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	DEBUG_PRINT(0, "check_slot_exist:query: %s", q_buffer);

	
	MYSQL_RES *result;
	int num_row;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_PRINT(-3, "check_slot_exist:null_result");
		return -1;
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -1;
		ERROR_PRINT(ret, "check_slot_exist:no_such_slot eid=%d", eid);
		goto cleanup;
	}

	return 0;
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_SAVE_SLOT	"REPLACE INTO evil_slot "
int in_save_slot(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int len;
	int err;
	int ret;
	char *ptr;
	int eid;
	int id;
	int n;
	char name[EVIL_ALIAS_MAX+3];
	// char numstr[30];
	char card[EVIL_CARD_MAX + 5]; // +1 is ok, +5 for safety 
	bzero(card, EVIL_CARD_MAX+5);

	ret = sscanf(in_buffer, IN_SAVE_SLOT_SCAN, &eid, &id, name, &n);
	if (ret != 3) {
		DB_ER_RETURN(-5, "save_slot:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "save_slot:invalid_eid %d", eid);
	}
	if (id <= 0) {
		DB_ER_RETURN(-25, "save_slot:invalid_id %d", id);
	}
	if (name[0] == '\0') {
		DB_ER_RETURN(-35, "save_slot:invalid_name %d", eid);
	}

	/*
	ret = check_slot_exist(pconn, q_buffer, eid, id); 
	if (ret != 0) {
		DB_ER_RETURN(-35, "save_slot:no_such_slot %d %d", eid, id);
	}
	*/
		

	ret = sscanf(in_buffer+n, "%400s", card);

	ptr = q_buffer;
	/*
	ptr = stpcpy(ptr, SQL_SAVE_SLOT);
	sprintf(numstr, "VALUES(%d,%d,'%s'", eid, id, name);
	ptr = stpcpy(ptr, numstr);
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		if (card[i]<'0' || card[i]>'9') {
			DB_ER_RETURN(-32, "save_slot:0-9outbound eid=%d i=%d ascii=%d"
			, eid, i, card[i]);
		}
		*ptr = ','; 	ptr++;	// keep ++ in same line
		*ptr = card[i];	ptr++;
		if (ptr-q_buffer-10 > QUERY_MAX) {
			DB_ER_RETURN(-22, "save_slot:overflow %d i=%d", QUERY_MAX, i);
			return -22;
		}
	}
	*ptr = ')'; ptr++;
	*ptr = '\0';
	*/

	ptr = stpcpy(ptr, SQL_SAVE_SLOT);
	ptr = stpcpy(ptr, "SELECT eid,slot,name");
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		if (card[i]<'0' || card[i]>'9') {
			DB_ER_RETURN(-32, "save_slot:0-9outbound eid=%d i=%d ascii=%d"
			, eid, i, card[i]);
		}
		*ptr = ','; 	ptr++;	// keep ++ in same line
		*ptr = card[i];	ptr++;
		if (ptr-q_buffer-10 > QUERY_MAX) {
			DB_ER_RETURN(-22, "save_slot:overflow %d i=%d", QUERY_MAX, i);
			return -22;
		}
	}
	ptr += sprintf(ptr, " FROM evil_slot WHERE eid=%d AND slot=%d"
	, eid, id);
	*ptr = '\0';

	len = ptr - q_buffer;

	// printf("--- save_slot:query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no need to check 1062, as REPLACE is always replace dup row
		WARN_PRINT(err==1062, "save_slot:err=1026 impossible");
		DB_ER_RETURN(-55, "save_slot:mysql_errno %d", err);
	}


	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		DB_ER_RETURN(-7, "save_slot:affected_row wrong %d\n", ret);
	}

	sprintf(out_buffer, OUT_SAVE_SLOT_PRINT, eid, id, name, card);
	return 0;
}

int save_slot(MYSQL **pconn, char *q_buffer, int eid, int slot, const char *card)
{
	int len;
	int err;
	int ret;
	char *ptr;
	ptr = q_buffer;

	ptr = stpcpy(ptr, SQL_SAVE_SLOT);
	ptr = stpcpy(ptr, "SELECT eid,slot,name");
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		if (card[i]<'0' || card[i]>'9') {
			ERROR_RETURN(-32, "save_slot:0-9outbound eid=%d i=%d ascii=%d"
			, eid, i, card[i]);
		}
		*ptr = ','; 	ptr++;	// keep ++ in same line
		*ptr = card[i];	ptr++;
		if (ptr-q_buffer-10 > QUERY_MAX) {
			ERROR_RETURN(-22, "save_slot:overflow %d i=%d", QUERY_MAX, i);
			return -22;
		}
	}
	ptr += sprintf(ptr, " FROM evil_slot WHERE eid=%d AND slot=%d"
	, eid, slot);
	*ptr = '\0';

	len = ptr - q_buffer;

	// printf("--- save_slot:query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no need to check 1062, as REPLACE is always replace dup row
		WARN_PRINT(err==1062, "save_slot:err=1026 impossible");
		ERROR_RETURN(-55, "save_slot:mysql_errno %d", err);
	}


	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 0 || ret > 2) {
		ERROR_RETURN(-7, "save_slot:affected_row wrong %d\n", ret);
	}

	return 0;
}

#define SQL_RENAME_SLOT	"UPDATE evil_slot SET name='%s' WHERE eid=%d AND slot=%d"
int in_rename_slot(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int len;
	int err;
	int ret;
	int eid;
	int id;
	char name[EVIL_ALIAS_MAX+3];

	ret = sscanf(in_buffer, IN_RENAME_SLOT_SCAN, &eid, &id, name);
	if (ret != 3) {
		DB_ER_RETURN(-5, "rename_slot:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "rename_slot:invalid_eid %d", eid);
	}
	if (id <= 0) {
		DB_ER_RETURN(-25, "rename_slot:invalid_id %d", id);
	}
	if (name[0] == '\0') {
		DB_ER_RETURN(-35, "rename_slot:invalid_name %d", eid);
	}

	len = sprintf(q_buffer, SQL_RENAME_SLOT, name, eid, id);

	// printf("--- rename_slot:query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no need to check 1062, as REPLACE is always replace dup row
		WARN_PRINT(err==1062, "rename_slot:err=1026 impossible");
		DB_ER_RETURN(-55, "rename_slot:mysql_errno %d", err);
	}


	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret == 0) {
		DB_ER_RETURN(-65, "rename_slot:same_name_or_no_slot %d\n", ret);
	}
	if (ret != 1) {
		DB_ER_RETURN(-7, "rename_slot:affected_row wrong %d\n", ret);
	}

	sprintf(out_buffer, OUT_RENAME_SLOT_PRINT, eid, id, name);
	return 0;
}

#define SQL_DELETE_SLOT "DELETE FROM evil_slot WHERE eid=%d AND slot=%d"

int delete_slot(MYSQL **pconn, char *q_buffer, int eid, int id)
{
	int len;
	int ret;
	int err;

	len = sprintf(q_buffer, SQL_DELETE_SLOT, eid, id);

	printf("--- delete_slot:query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "delete_slot:mysql_errno %d  eid=%d", err, eid);
	}

	// delete should work because we use it to revert the insert slot
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-7, "delete_slot:affected_rows %d eid=%d"
		, ret, eid);
	}
	
	return 0;
}

// insert slot to evil_slot, where slot_id is always = max(slot)+1 in the table
// if the slot id is not the max(slot)+1, this SQL will fail
#define SQL_BUY_SLOT	"INSERT INTO evil_slot (eid,slot,name) SELECT eid,%d,'slot%d' FROM evil_slot WHERE eid=%d AND slot=%d AND %d=(SELECT MAX(slot) FROM evil_slot WHERE eid=%d)"

// fix slot1 when the user has empty slot
#define SQL_BUY_SLOT_1	"INSERT INTO evil_slot (eid,slot,name) VALUES (%d,1,'slot1')"

// the gold/crystal here is positive
int in_buy_slot(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int len;
	int err;
	int ret;

	// input
	int eid;
	int flag;
	int id;
	int gold;
	int crystal;
	// char remark[20];

	ret = sscanf(in_buffer, IN_BUY_SLOT_SCAN
	, &eid, &flag, &id, &gold, &crystal);
	if (ret != 5) {
		DB_ER_RETURN(-5, "buy_slot:invalid_input eid=%d", eid);
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "buy_slot:invalid_eid eid=%d", eid);
	}
	if (id <= 0 || id >= MAX_SLOT_NUM) {
		DB_ER_RETURN(-25, "buy_slot:invalid_id %d eid=%d", id, eid);
	}
	if (gold < 0) {
		DB_ER_RETURN(-35, "buy_slot:invalid_gold %d eid=%d", gold, eid);
	}
	if (crystal < 0) {
		DB_ER_RETURN(-45, "buy_slot:invalid_crystal %d eid=%d", crystal, eid);
	}

	// 1, insert slot
	// if insert slot fail, return err
	// SQL_BUY_SLOT	"INSERT INTO evil_slot (eid,id,name) SELECT eid,%d,'slot%d' FROM evil_slot WHERE eid=%d AND id=%d AND %d=(SELECT MAX(id) FROM evil_slot WHERE eid=%d)"

	if (id == 1) {
		// special handling for slot1 because that user may have empty slot
		len = sprintf(q_buffer, SQL_BUY_SLOT_1, eid);
	} else {
		len = sprintf(q_buffer, SQL_BUY_SLOT, id, id, eid, id-1, id-1, eid);
	}

	// printf("--- buy_slot:query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// duplicate: also treat as -6 logic error
		if (err==1062) {
			DB_ER_RETURN(-6, "buy_slot:slot_id_dup_1062 id=%d eid=%d"
			, id, eid);
		}
		DB_ER_RETURN(-55, "buy_slot:mysql_errno %d  eid=%d", err, eid);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		// means slot[id] exist or id > max(slot_id)
		// client send something wrong
		DB_ER_RETURN(-6, "buy_slot:slot_id wrong %d %d eid=%d"
		, id, ret, eid);
	}

	// 2, update money
	// if update money fail, money in memory not sync with db
	// maybe server bug
	ret = update_money(pconn, q_buffer, eid, -gold, -crystal);
	if (ret < 0) {
		// TODO delete the slot
		delete_slot(pconn, q_buffer, eid, id); // error message inside
		DB_ER_RETURN(-7, "buy_slot:update_money_fail ret=%d eid=%d %d %d"
		, ret, eid, gold, crystal);
	}

	sprintf(out_buffer, OUT_BUY_SLOT_PRINT, eid, flag, id, gold, crystal);

	// sprintf(remark, "slot%d", id);
	// ret = record_buy(pconn, q_buffer, eid, SLOT_START_ID + id, 1, gold, crystal, remark);
	// ERROR_PRINT(ret, "buy_slot:record_fail eid=%d, slot_id=%d gold=%d crystal=%d", eid, id, gold, crystal);
	
	return 0;
}




// 
#define SQL_ADD_MATCH	"INSERT INTO evil_match (match_id, title, max_player, start_time, t1, t2, t3, t4, status, round) VALUES (%ld, '%s', %d, FROM_UNIXTIME(%ld), %ld, %ld, %ld, %ld, %d, %d)"

int in_add_match(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int len;
	int err;
	int ret;

	// input
	long match_id;
	char title[105];
	int max_player;
	long start_time;
	long t1, t2, t3, t4;
	const int status = MATCH_STATUS_READY;
	int round;

	ret = sscanf(in_buffer, IN_ADD_MATCH_SCAN
	, &match_id, title, &max_player, &start_time, &t1
	, &t2, &t3, &t4, &round);
	if (ret != 9) {
		DB_ER_RETURN(-5, "add_match:invalid_input %d", ret);
	}
	if (match_id <= 0) {
		DB_ER_RETURN(-15, "add_match:invalid_match_id match_id=%ld", match_id);
	}
	if (max_player <= 0) {
		DB_ER_RETURN(-25, "add_match:invalid_max_player %d", max_player);
	}
	if (start_time <= 0 || start_time < time(NULL)) {
		DB_ER_RETURN(-35, "add_match:invalid_start_time %ld %ld", start_time
		, get_yesterday(time(NULL)));
	}
	if (t1 < 0 || t2 < 0 || t3 < 0 || t4 < 0) {
		DB_ER_RETURN(-45, "add_match:invalid_time t1=%ld t2=%ld t3=%ld t4=%ld", t1, t2, t3, t4);
	}

	// 1, insert evil_match
	// if insert match fail, return err
	len = sprintf(q_buffer, SQL_ADD_MATCH, match_id, title, max_player, start_time, t1, t2, t3, t4, status, round);

	// printf("--- add_match:query=%s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-55, "add_match:mysql_errno %d  match_id=%ld", err, match_id);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		// impossible
		DB_ER_RETURN(-7, "add_match:sql_affected_rows wrong %d  match_id=%ld"
		, ret, match_id);
	}

	sprintf(out_buffer, OUT_ADD_MATCH_PRINT, match_id, title, max_player, start_time, t1, t2, t3, t4, status, round);

	return 0;
}

#define SQL_INSERT_MATCH_PLAYER 	"INSERT INTO evil_match_player VALUES (%ld, %d, %d, %d, %d, %d, %d, %d, %d, %d, '%s')"

int in_match_apply(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int len;
	int err;
	int ret;

	long match_id;
	int eid;
	int round;
	int team_id;
	int win;
	int lose;
	int draw;
	int tid;
	int point;
	int icon;
	char alias[EVIL_ALIAS_MAX + 3];


	ret = sscanf(in_buffer, IN_ADD_MATCH_PLAYER_SCAN
	, &match_id, &eid, &round, &team_id, &win, &lose, &draw, &tid
	, &point, &icon, alias);

	if (ret != 11) {
		DB_ER_RETURN(-5, "in_match_apply:invalid_input %d", ret);
	}

	if (match_id <= 0) {
		DB_ER_RETURN(-15, "in_match_apply:invalid_match_id match_id=%ld", match_id);
	}

	len = sprintf(q_buffer, SQL_INSERT_MATCH_PLAYER
	, match_id, eid, round, team_id, win, lose, draw
	, tid, point, icon, alias);

	// printf("--- in_match_apply:query=%s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "in_match_apply:mysql_errno %d  match_id=%ld", err, match_id);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		// impossible
		ERROR_RETURN(-7, "in_match_apply:sql_affected_rows wrong %d  match_id=%ld"
		, ret, match_id);
	}


	sprintf(out_buffer, OUT_ADD_MATCH_PLAYER_PRINT, match_id, eid, round
	, team_id, win, lose, draw, tid, point, icon, alias);

	ret = 0;
	return ret;
}

#define SQL_INSERT_MATCH_AI	"INSERT INTO evil_match_player (match_id,eid,icon,alias) VALUES "

int in_match_apply_ai(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int err;
	int ret;

	int n;
	long match_id;
	int count;
	int eid;
	int icon;
	char alias[EVIL_ALIAS_MAX + 3];
	char * q_ptr;
	const char * in_ptr;


	in_ptr = in_buffer;
	ret = sscanf(in_ptr, IN_ADD_MATCH_AI_SCAN
	, &match_id, &count, &n);

	if (ret != 2) {
		DB_ER_RETURN(-5, "in_match_apply_ai:invalid_input %d", ret);
	}

	if (match_id <= 0) {
		DB_ER_RETURN(-15, "in_match_apply_ai:invalid_match_id match_id=%ld", match_id);
	}

	if (count <= 0) {
		DB_ER_RETURN(-25, "in_match_apply_ai:invalid_count count=%d", count);
	}

	q_ptr = q_buffer;
	q_ptr += sprintf(q_ptr, SQL_INSERT_MATCH_AI);

	for (int i=0; i<count; i++) {
		in_ptr += n;
		ret = sscanf(in_ptr, "%d %d %s %n", &eid, &icon, alias, &n);

		if (ret != 3) {
			DB_ER_RETURN(-35, "in_match_apply_ai:invalid_ai_info %d", ret);
		}

		q_ptr += sprintf(q_ptr, "(%ld,%d,%d,'%s')", match_id, eid, icon, alias);

		if (i != count-1) {
			q_ptr += sprintf(q_ptr, ",");
		}

	}

	// printf("--- in_match_apply_ai:query=%s\n", q_buffer);

	ret = my_query(pconn, q_buffer, strlen(q_buffer));
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "in_match_apply_ai:mysql_errno %d  match_id=%ld", err, match_id);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != count) {
		// impossible
		ERROR_RETURN(-7, "in_match_apply_ai:sql_affected_rows wrong %d %d match_id=%ld"
		, ret, count, match_id);
	}


	sprintf(out_buffer, OUT_ADD_MATCH_AI_PRINT, match_id, count);

	ret = 0;
	return ret;
}

#define SQL_DELETE_MATCH_PLAYER 	"DELETE FROM evil_match_player WHERE match_id=%ld AND eid=%d"

int in_match_delete(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int len;
	int err;
	int ret;

	long match_id;
	int eid;

	ret = sscanf(in_buffer, IN_DELETE_MATCH_PLAYER_SCAN
	, &match_id, &eid);

	if (ret != 2) {
		DB_ER_RETURN(-5, "in_match_delete:invalid_input %d", ret);
	}

	if (match_id <= 0) {
		DB_ER_RETURN(-15, "in_match_delete:invalid_match_id match_id=%ld", match_id);
	}

	len = sprintf(q_buffer, SQL_DELETE_MATCH_PLAYER
	, match_id, eid);

	// printf("--- in_match_delete:query=%s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "in_match_delete:mysql_errno %d  match_id=%ld", err, match_id);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		// impossible
		ERROR_RETURN(-7, "in_match_delete:sql_affected_rows wrong %d  match_id=%ld"
		, ret, match_id);
	}


	sprintf(out_buffer, OUT_DELETE_MATCH_PLAYER_PRINT, match_id, eid);

	ret = 0;
	return ret;
}

#define SQL_GET_MATCH_PLAYER	"SELECT * FROM evil_match_player where match_id=%ld AND round=%d AND eid=%d"

int get_match_player(MYSQL **pconn, char *q_buffer, long match_id
, int round, int eid, match_player_t *player)
{
	int ret;
	int len;
	len = sprintf(q_buffer, SQL_GET_MATCH_PLAYER, match_id
	, round, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_PRINT(-55, "get_match_player:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "get_match_player:query: %s", q_buffer);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_PRINT(-3, "get_match_player:null_result");
		return -1;
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -1;
		ERROR_PRINT(ret, "get_match_player:no_such_slot eid=%d", eid);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// match_id, eid, round, team_id, win, lose, draw, tid
	if (field_count != 8) {
		ret = -7;
		ERROR_PRINT(ret, "get_match_player:field_count %d != 8", field_count);
		goto cleanup;
	}
	row = mysql_fetch_row(result);
	player->match_id = strtolong_safe(row[0], -1);
	player->eid = strtol_safe(row[1], -1);
	player->round = strtol_safe(row[2], -1);
	player->team_id = strtol_safe(row[3], -1);
	player->win = strtol_safe(row[4], -1);
	player->lose = strtol_safe(row[5], -1);
	player->draw = strtol_safe(row[6], -1);
	player->tid = strtol_safe(row[7], -1);

	DEBUG_PRINT(0, "get_match_player:match_id=%ld eid=%d round=%d team_id=%d win=%d lose=%d draw=%d tid=%d", player->match_id, player->eid
	, player->round, player->team_id, player->win
	, player->lose, player->draw, player->tid);

	return 0;
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_INIT_TEAM	"UPDATE evil_match_player SET "

int __query_init_team(char * query, int query_max, long match_id, int team_id, int eid1, int eid2, int eid3, int eid4)
{
	char *end;
	end = query;  
	end = stpcpy(end, SQL_INIT_TEAM);

	end = stpcpy(end, "round=1,");
	end = stpcpy(end, "team_id=case eid ");
	end += sprintf(end, "when %d then %d ", eid1, team_id);
	end += sprintf(end, "when %d then %d ", eid2, team_id);
	end += sprintf(end, "when %d then %d ", eid3, team_id);
	end += sprintf(end, "when %d then %d ", eid4, team_id);
	end = stpcpy(end, "end, ");
	end = stpcpy(end, "tid=case eid ");
	end += sprintf(end, "when %d then 0 ", eid1);
	end += sprintf(end, "when %d then 1 ", eid2);
	end += sprintf(end, "when %d then 0 ", eid3);
	end += sprintf(end, "when %d then 1 ", eid4);
	end = stpcpy(end, "end ");
	end += sprintf(end, "WHERE eid IN (%d, %d, %d, %d) AND match_id=%ld"
	, eid1, eid2, eid3, eid4, match_id);

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}

#define SQL_MATCH_PLAYER_INIT 	"UPDATE evil_match_player SET round=%d, team_id=%d, win=%d, lose=%d, draw=%d, tid=%d, point=%d WHERE match_id=%ld AND eid=%d"

int init_match_player(MYSQL **pconn, char *q_buffer, long match_id
, int eid, int round, int team_id, int win, int lose, int draw, int tid, int point)
{ 

	int ret;
	int len;
	int err;
	len = sprintf(q_buffer, SQL_MATCH_PLAYER_INIT, round, team_id
	, win, lose, draw, tid, point, match_id, eid);

	// printf("--- init_match_player:query=%s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "init_match_player:mysql_errno %d  match_id=%ld", err, match_id);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		// impossible
		ERROR_RETURN(-7, "init_match_player:sql_affected_rows wrong %d  match_id=%ld"
		, ret, match_id);
	}

	ret = 0;
	return ret;
}

#define SQL_INSERT_FAKE_PLAYER	"INSERT INTO evil_match_player VALUES "

int __query_insert_fake_team_player(char * query, int query_max, long match_id, int team_id)
{
	char *end;
	char * const query_end_max = query + query_max - 300;
	end = query;  
	end = stpcpy(end, SQL_INSERT_FAKE_PLAYER);

	// match_id, eid, round, team_id, win,lose,draw,tid,point,icon,alias
	end += sprintf(end, "(%ld,-201,2,%d,0,0,0,1,0,0,'_'),", match_id, team_id);
	end += sprintf(end, "(%ld,-211,2,%d,0,0,0,1,0,0,'_'),", match_id, team_id);
	end += sprintf(end, "(%ld,-101,2,%d,0,0,0,0,0,0,'_'),", match_id, team_id);
	end += sprintf(end, "(%ld,-111,2,%d,0,0,0,0,0,0,'_'),", match_id, team_id);

	end += sprintf(end, "(%ld,-202,3,%d,0,0,0,0,0,0,'_'),", match_id, team_id);
	end += sprintf(end, "(%ld,-112,3,%d,0,0,0,0,0,0,'_'),", match_id, team_id);
	end += sprintf(end, "(%ld,-212,3,%d,0,0,0,1,0,0,'_'),", match_id, team_id);
	// eid = 0
	end += sprintf(end, "(%ld,0,3,%d,0,0,0,1,0,0,'_')", match_id, team_id);
		
	if (end > query_end_max) {
		ERROR_PRINT(-22, "query_fake_player:overflow");
		return -22;
	}

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}

#define SQL_UPDATE_MATCH	"UPDATE evil_match SET status=%d, round=%d WHERE match_id=%ld"

int update_match(MYSQL **pconn, char *q_buffer, long match_id
, int round, int status)
{ 

	int ret;
	int len;
	int err;
	len = sprintf(q_buffer, SQL_UPDATE_MATCH, status, round, match_id);

	// printf("--- update_match:query=%s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "update_match:mysql_errno %d  match_id=%ld", err, match_id);
	}

	// check affected row
	// load a round start match in db, after do round start function, match may has no change
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1 && ret != 0) {
		// impossible
		ERROR_RETURN(-7, "update_match:sql_affected_rows wrong %d  match_id=%ld"
		, ret, match_id);
	}

	ret = 0;
	return ret;
}

int in_match_team_init(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;

	long match_id;
	int count;
	int n;
	const char * ptr;

	ret = sscanf(in_buffer, IN_MATCH_TEAM_INIT_SCAN
	, &match_id, &count, &n);

	if (ret != 2) {
		DB_ER_RETURN(-5, "in_match_team_init:invalid_input %d", ret);
	}

	ptr = in_buffer + n;
	/*
	// 1.init player by eid
	// 2.insert fake_eid for round2,3
	// 3.update match.status
	*/

	// 1.init player by eid
	int team_num = count / MAX_TEAM_PLAYER;
	for (int i=0; i<team_num; i++) {
		int eid1, eid2, eid3, eid4;
		ret = sscanf(ptr, "%d %d %d %d %n", &eid1, &eid2, &eid3, &eid4, &n);
		if (ret != 4) {
			DB_ER_RETURN(-2, "in_match_team_init:count_mismatch %d %d", i, count);
		}

		len = __query_init_team(q_buffer, QUERY_MAX, match_id, i+1
		, eid1, eid2, eid3, eid4);
		if (len < 0) {
			DB_ER_NEG_RETURN(-25, "in_match_team_init:query_update %d", len);
		}
		// DEBUG_PRINT(0, "q_buffer=%s", q_buffer);
	
		ret = my_query( pconn, q_buffer, len);
		if (ret != 0) {
			err = mysql_errno(*pconn);
			DB_ER_RETURN(-55, "in_match_team_init:mysql_errno %d", err);
		}


		// check affected row
		ret = mysql_affected_rows(*pconn); 
		if (ret != 4) {
			DB_ER_RETURN(-7, "in_match_team_init:affected_row wrong %d\n", ret);
		}
		ptr += n;

	}

	// 2.insert fake_eid for round2,3
	for (int i=0; i<team_num; i++) {
		len = __query_insert_fake_team_player(q_buffer, QUERY_MAX, match_id, i+1);
		if (len < 0) {
			DB_ER_NEG_RETURN(-25, "in_match_team_init:query_insert %d", len);
		}
		// DEBUG_PRINT(0, "q_buffer=%s", q_buffer);
	
		ret = my_query( pconn, q_buffer, len);
		if (ret != 0) {
			err = mysql_errno(*pconn);
			DB_ER_RETURN(-55, "in_match_team_init:mysql_errno %d", err);
		}


		// check affected row
		ret = mysql_affected_rows(*pconn); 
		if (ret != 8) {
			DB_ER_RETURN(-7, "in_match_team_init:affected_row wrong %d\n", ret);
		}

	}

	// 3.update match.status
	ret = update_match(pconn, q_buffer, match_id, 0, MATCH_STATUS_ROUND_END);
	if (ret != 0) {
		DB_ER_RETURN(-16, "in_match_team_init:update_match_fail %d\n", ret);
	}

	return 0;
}



#define SQL_ELI_INSERT_MATCH_PLAYER	"INSERT INTO evil_match_player VALUES "
#define SQL_MATCH_ELI_PLAYER_INIT	"(%ld, %d, %d, 0, 0, 0, 0, %d, 0, (SELECT icon FROM evil_user WHERE eid=%d), (SELECT alias FROM evil_user WHERE eid=%d LIMIT 1))"
#define SQL_MATCH_ELI_AI_INIT	"(%ld, %d, %d, 0, 0, 0, 0, %d, 0, (SELECT icon FROM design.design_ai WHERE id=%d LIMIT 1), (SELECT alias FROM design.design_ai WHERE id=%d LIMIT 1))"
#define SQL_MATCH_ELI_FAKE_PLAYER_INIT	"(%ld, %d, %d, 0, 0, 0, 0, %d, 0, 0, '_')"
/**
 *
 * init_match_eli_init
 * assert max_player for total match is n [min(n) >= 4]
 * then tid for player in elimination match sort as below:
 * round			player_tid
 *	4				(n/2) (n/2)+1 ... n-2 n-1
 *	5				(n/4) (n/4)+1 ... (n/2)-2 (n/2)-1
 *	...				...
 *	4+log2(n/2)-2	4 5 6 7
 *	4+log2(n/2)-1	2 3
 *	4+log2(n/2)		1
 * 
 */
int in_match_eli_init(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;

	long match_id;
	int count;
	int n;
	const char * ptr;
	char * query_ptr;
	int eid;

	ret = sscanf(in_buffer, IN_MATCH_ELI_INIT_SCAN, &match_id, &count, &n);
	// count should be equals max_player/2

	if (ret != 2) {
		DB_ER_RETURN(-5, "in_match_eli_init:invalid_input %d", ret);
	}

	ptr = in_buffer + n;
	/*
	// 1.init eli player by eid at round 4
	// 2.insert fake_player with tid for round 5+
	*/

	// 1.init eli player by eid at round 4
	int start_tid = count * 2;
	int cur_round = 4;
	for (int i = 1; i <= count; i++) {
		ret = sscanf(ptr, "%d %n", &eid, &n);
		if (ret != 1) {
			DB_ER_RETURN(-2, "in_match_eli_init:count_mismatch %d %d", i, count);
		}

		query_ptr = q_buffer;
		query_ptr = stpcpy(query_ptr, SQL_ELI_INSERT_MATCH_PLAYER);
		if (eid > MAX_AI_EID) {
			// eid is player, get alias from evil.evil_user
			query_ptr += sprintf(query_ptr, SQL_MATCH_ELI_PLAYER_INIT, match_id, eid
			, cur_round, start_tid - i, eid, eid);
		} else {
			// eid is ai, get alias from design.design_ai
			query_ptr += sprintf(query_ptr, SQL_MATCH_ELI_AI_INIT, match_id, eid
			, cur_round, start_tid - i, eid, eid);
		}

		len = query_ptr - q_buffer;
		ret = my_query( pconn, q_buffer, len);
		if (ret != 0) {
			err = mysql_errno(*pconn);
			DB_ER_RETURN(-55, "in_match_eli_init:mysql_errno %d", err);
		}
		// check affected row
		ret = mysql_affected_rows(*pconn); 
		if (ret != 1) {
			DB_ER_RETURN(-7, "in_match_eli_init:affected_row wrong %d\n", ret);
		}

		ptr += n;
	}

	// 2.insert fake_player with tid for round 5+
	cur_round++;
	for (int round_start_tid = count / 2; round_start_tid > 0
	; round_start_tid /= 2, cur_round++) {

		for (int cur_tid = round_start_tid; cur_tid < round_start_tid * 2
		; cur_tid++) {
			query_ptr = q_buffer;
			query_ptr = stpcpy(query_ptr, SQL_ELI_INSERT_MATCH_PLAYER);

			query_ptr += sprintf(query_ptr, SQL_MATCH_ELI_FAKE_PLAYER_INIT
			, match_id, -1, cur_round, cur_tid);

			len = query_ptr-q_buffer;
			ret = my_query( pconn, q_buffer, len);
			if (ret != 0) {
				err = mysql_errno(*pconn);
				DB_ER_RETURN(-55, "in_match_eli_init:mysql_errno %d", err);
			}
			// check affected row
			ret = mysql_affected_rows(*pconn); 
			if (ret != 1) {
				DB_ER_RETURN(-7, "in_match_eli_init:affected_row wrong %d\n", ret);
			}

		}

	}
	INFO_PRINT(0, "in_match_eli_init:success");

	return 0;
}

#define SQL_UPDATE_TEAM_PLAYER	"UPDATE evil_match_player SET eid=%d,win=%d,lose=%d,draw=%d,point=%d,icon=%d,alias='%s' WHERE eid=%d AND match_id=%ld AND round=%d AND team_id=%d AND tid=%d"
int update_match_player(MYSQL **pconn, char *q_buffer, long match_id, int eid, int fake_eid, int round, int team_id, int win, int lose, int draw, int tid, int point, int icon, const char * alias)
{
	
	int ret;
	int len;
	int err;
	int origin_eid = (fake_eid != 0) ? fake_eid : eid;
	// printf("-------- origin_eid=%d fake_eid=%d eid=%d\n", origin_eid, fake_eid, eid);
	len = sprintf(q_buffer, SQL_UPDATE_TEAM_PLAYER, eid, win, lose, draw, point, icon, alias, origin_eid, match_id, round, team_id, tid);

	// printf("--- update_match_player:query=%s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "update_match_player:mysql_errno %d  match_id=%ld", err, match_id);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		// impossible
		ERROR_RETURN(-7, "update_match_player:sql_affected_rows wrong %d match_id=%ld"
		, ret, match_id);
	}

	ret = 0;
	return ret;
}

int in_update_match_player(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;

	int eid; 
	int fake_eid; 
	int round; 
	int team_id; 
	int win; 
	int lose; 
	int draw; 
	int tid; 
	int point; 
	int icon;
	char alias[EVIL_ALIAS_MAX + 3];

	long match_id;
	int count;
	int n;
	const char * ptr;

	// INFO_PRINT(0, "update_match_player:in_buffer %s", in_buffer);
	ret = sscanf(in_buffer, IN_UPDATE_MATCH_PLAYER_SCAN
	, &match_id, &count, &n);

	if (ret != 2) {
		DB_ER_RETURN(-5, "in_update_match_player:invalid_input %d", ret);
	}

	ptr = in_buffer + n;

	for (int i=0; i<count; i++) {
		ret = sscanf(ptr, "%d %d %d %d %d %d %d %d %d %d %s %n"
		, &eid, &fake_eid, &round, &team_id
		, &win, &lose, &draw
		, &tid, &point, &icon, alias, &n);
		if (ret != 11) {
			DB_ER_RETURN(-15, "in_update_match_player:invalid_player_data %d", ret);
		}
		ret = 0;
		ret = update_match_player(pconn, q_buffer, match_id, eid
		, fake_eid, round, team_id, win, lose, draw, tid, point, icon, alias);
		// DEBUG_PRINT(0, "-------- update_match_player: %d %d", ret, eid);
		if (ret != 0) {
			DB_ER_RETURN(-6, "in_update_match_player:update_player_fail %d", ret);
		}

		ptr += n;
	}

	sprintf(out_buffer, "0 ok");

	return 0;
}

int in_update_match(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{

	int ret;
	long match_id;
	int round;
	int status;

	ret = sscanf(in_buffer, IN_UPDATE_MATCH_SCAN
	, &match_id, &round, &status);
	
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_update_match:input_error %d", ret);
	}

	if (match_id <= 0) {
		DB_ER_RETURN(-15, "in_update_match:invalid_match_id %ld", match_id);
	}

	if (status != MATCH_STATUS_READY && status != MATCH_STATUS_ROUND_START
	&& status != MATCH_STATUS_ROUND_END && status != MATCH_STATUS_FINISHED
	&& status != MATCH_STATUS_DELETE) {
		DB_ER_RETURN(-6, "in_update_match:invalid_status %d", status);
	}

	ret = update_match(pconn, q_buffer, match_id, round, status);
	if (ret != 0) {
		DB_ER_RETURN(-16, "in_update_match:update_fail %ld %d"
		, match_id, status);
	}

	sprintf(out_buffer, "%d %ld ok", 0, match_id);

	ret = 0;
	return ret;
}


////////////////////////////////////////////////
//	ranking

//#define SQL_UPDATE_PLAYER_RANKING	"UPDATE evil_ranking SET rank=%d WHERE eid=%d"
#define SQL_ADD_PLAYER_RANKING	"INSERT INTO evil_ranking VALUES (%d, (SELECT * FROM (SELECT (IFNULL(MAX(erank), 0)+1) FROM evil_ranking) tmp_tbl))"

int __add_player_ranking(MYSQL **pconn, char *q_buffer, int eid)
{
	int ret;
	int len;
	int err;
	len = sprintf(q_buffer, SQL_ADD_PLAYER_RANKING, eid);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "__add_player_ranking:mysql_errno %d  eid=%d\n"
		, err, eid);
	}

	// check affected row
	// load a round start match in db, after do round start function, match may has no change
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		// impossible
		ERROR_RETURN(-7
		, "__add_player_ranking:sql_affected_rows wrong %d eid=%d\n"
		, ret, eid);
	}

	ret = 0;
	return ret;
}


//#define SQL_RANKING_COUNT_PER_PAGE		500
//#define SQL_INIT_RANKING_USER_EID_LIST	"SELECT eid FROM evil_user WHERE eid >= %d AND alias NOT LIKE '\\_%%' AND EXISTS() AND NOT EXISTS (SELECT eid FROM evil_ranking WHERE eid=evil_user.eid) LIMIT %d"
//#define SQL_INIT_RANKING_USER_EID_LIST	"SELECT eid FROM evil_deck WHERE eid >= %d AND NOT EXISTS (SELECT eid FROM evil_ranking WHERE eid=evil_deck.eid) LIMIT %d"
//#define SQL_ADD_RANKING_LIST	"INSERT INTO evil_ranking VALUES "

//#define SQL_INIT_RANKING_LIST	"INSERT INTO evil_ranking (SELECT ed.eid, ((SELECT COUNT(*) FROM evil_deck where eid <= ed.eid) + (SELECT rank FROM evil_ranking order by rank desc limit 1)) FROM evil_deck ed WHERE NOT EXISTS (SELECT eid FROM evil_ranking WHERE ed.eid=eid) AND ed.eid >= %d)"
// order by eid asc
//#define SQL_INIT_RANKING_LIST	"INSERT INTO evil_ranking (SELECT ed.eid, (SELECT COUNT(*) FROM (SELECT * FROM evil_deck WHERE NOT EXISTS(SELECT eid FROM evil_ranking WHERE eid=evil_deck.eid)) tb1 WHERE eid<=ed.eid) FROM evil_deck ed WHERE NOT EXISTS (SELECT eid FROM evil_ranking WHERE eid=ed.eid) AND ed.eid>=%d)"
//#define SQL_INIT_RANKING_LIST	"INSERT INTO evil_ranking (SELECT ed.eid, ((SELECT COUNT(*) FROM (SELECT * FROM evil_deck WHERE NOT EXISTS(SELECT eid FROM evil_ranking WHERE eid=evil_deck.eid)) tb1 WHERE eid<=ed.eid AND ed.eid>=%d) + (SELECT IFNULL(MAX(rank), 0) FROM evil_ranking)) FROM evil_deck ed WHERE NOT EXISTS (SELECT eid FROM evil_ranking WHERE eid=ed.eid) AND ed.eid>=%d)"
// order by eid desc
// #define SQL_INIT_RANKING_LIST	"INSERT INTO evil_ranking (SELECT ed.eid, (SELECT COUNT(*) FROM (SELECT * FROM evil_deck WHERE NOT EXISTS(SELECT eid FROM evil_ranking WHERE eid=evil_deck.eid)) tb1 WHERE eid>=ed.eid) FROM evil_deck ed WHERE NOT EXISTS (SELECT eid FROM evil_ranking WHERE eid=ed.eid) AND ed.eid>=%d)"
#define SQL_INIT_RANKING_LIST	"INSERT INTO evil_ranking (SELECT ed.eid,@rrr:=(@rrr+1) AS rownum FROM evil_deck AS ed LEFT JOIN evil_ranking AS er ON er.eid=ed.eid LEFT JOIN (SELECT @rrr:=IFNULL(MAX(erank),0) FROM evil_ranking) AS tb1 ON 1=1 WHERE IFNULL(er.eid,0)=0 AND ed.eid>=%d)"

int init_ranking_list(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int start_eid;
	int err;

	ret = sscanf(in_buffer, "%d", &start_eid);
	if (ret <= 0) {
		start_eid = 0;
	}

	len = sprintf(q_buffer, SQL_INIT_RANKING_LIST, start_eid);
//	printf("q_buffer[%s]\n", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "init_ranking_list:mysql_errno %d\n"
		, err);
	}
	ret = mysql_affected_rows(*pconn); 
	if (ret == 0) {
		WARN_PRINT(1, "init_ranking_list: init_none_ranking %d", ret);
	}

	sprintf(out_buffer, "%d", ret);

	return 0;
}

#define SQL_RANKING_INFO	"SELECT er.rank, eu.rank_time FROM evil_ranking er LEFT JOIN evil_user eu ON er.eid=eu.eid WHERE er.eid=%d"
int __get_ranking_info(MYSQL **pconn, char *q_buffer, int eid, int &rank, int &rank_time)
{
	int ret;
	int len;
	int err;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	// get self rank data
	len = sprintf(q_buffer, SQL_RANKING_INFO, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-15, "__get_ranking_info:mysql_errno %d", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "__get_ranking_info:null_result");
	}
	num_row = mysql_num_rows(result);
	if (num_row == 0) {
		ret = -25;
		ERROR_PRINT(ret, "__get_ranking_info:eid_not_exist %d", eid);
		goto cleanup;
	} else if (num_row != 1) {
		ret = -7;
		ERROR_PRINT(ret, "__get_ranking_info:num_row_error %d\n", num_row);
		goto cleanup;
	}
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -17;
		ERROR_PRINT(ret, "__get_ranking_info:row_data_null\n");
		goto cleanup;
	}

	rank = strtol_safe(row[0], -1);
	rank_time = strtol_safe(row[1], -1);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_TOP_RANKING_LIST	"SELECT er.eid, es.lv, er.rank, eu.icon, es.game_count, es.game_win, eu.alias FROM evil_ranking er INNER JOIN evil_user eu ON er.eid=eu.eid LEFT JOIN evil_status es ON es.eid=eu.eid ORDER BY er.rank ASC limit %d"
int in_ranking_list(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{

	int ret;
	int len;
	int err;

	int eid;
	int rank;
	int rank_time;
	int game_count;
	int game_win;
	double rating;

	ret = sscanf(in_buffer, IN_RANKING_LIST_SCAN, &eid);
	if (ret <= 0) {
		DB_ER_RETURN(-5, "in_ranking_list:input_invalid\n");
	}

	ret = __get_ranking_info(pconn, q_buffer, eid, rank, rank_time);
	if (ret != 0) {
		DB_ER_RETURN(-15, "in_ranking_list:get_ranking_info_error\n");
	}

	len = sprintf(q_buffer, SQL_TOP_RANKING_LIST, MAX_RANKING_LIST);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-5, "in_ranking_list:mysql_errno %d\n", err);
	}

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int count;
	int field_count;
	char *ptr;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "in_ranking_list:null_result\n");
	}

	num_row = mysql_num_rows(result);

	count = 0;
	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d %d", rank, rank_time, num_row);
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			ret = -7;
			BUG_PRINT(ret, "in_ranking_list:fetch_row_overflow %d/%d"
			, count, num_row);
			goto cleanup;
		}

		field_count = mysql_field_count(*pconn);
		// eid, level, rank, icon, game_count, game_win, alias
		if (field_count != 7) {
			ret = -17;
			ERROR_PRINT(ret, "in_ranking_list:field_count %d != 7", field_count);
			goto cleanup;
		}
		game_count = strtol_safe(row[4], 0);
		game_win = strtol_safe(row[5], 0);
		rating = (game_count <= 0) ? 1 : (game_win * 1.0f / game_count);
		
		ptr += sprintf(ptr, " %d %d %d %d %lf %s", strtol_safe(row[0], -1)
		, strtol_safe(row[1], -1), strtol_safe(row[2], -1)
		, strtol_safe(row[3], -1), rating, row[6]);
	}

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


int __get_range_list(int *random_list, int cur_v)
{
	const static float RANGE_PERCENT_LIST[] = {
		.9F, .85F, .8F, .75F, .7F
	};
	const int MIN_V = 1;

	// TODO
	int range_count = cur_v - MIN_V;
	if (range_count <= 0) {
		return 0;
	}

	if (range_count <= RANKING_RANDOM_COUNT) {
		// all range_count less than RANKING_RANDOM_COUNT
		// return list from MIN_V to range_count-1
		for (int i = 0; i < range_count; i++) {
			random_list[i] = i + MIN_V;
		}
		return range_count;
	}

	int tmp_v;
	for (int i = 0, j = 0; i < RANKING_RANDOM_COUNT; i++) {
		tmp_v = (int)(cur_v * RANGE_PERCENT_LIST[i]);
		for (j = 0; j < i; j++) {
			// if this value already exists, then value=value-1
			// and continue checking
			if (random_list[j] == tmp_v) {
				tmp_v = tmp_v - 1;
			}
		}
		random_list[i] = tmp_v;
	}

	return RANKING_RANDOM_COUNT;
}

#define SQL_FRONT_RANK_TARGETS	"SELECT er.eid, es.lv, er.rank, eu.icon, es.game_count, es.game_win, eu.alias FROM evil_ranking er INNER JOIN evil_user eu ON er.eid=eu.eid LEFT JOIN evil_status es ON es.eid=eu.eid WHERE er.rank in ("
#define SQL_TAIL_RANK_TARGETS	") ORDER BY er.rank ASC LIMIT 5"

int in_ranking_targets(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;
	int eid;
	int rank;
	int game_count;
	int game_win;
	double rating;
	int rank_time;
	int rank_count;
	int random_rank_list[RANKING_RANDOM_COUNT];

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int count;
	int field_count;
	char *ptr, *out_ptr;

	// 1. get self rank data
	ret = sscanf(in_buffer, IN_RANKING_TARGETS_SCAN, &eid);
	if (ret <= 0) {
		DB_ER_RETURN(-5, "in_ranking_targets:input_invalid\n");
	}

	ret = __get_ranking_info(pconn, q_buffer, eid, rank, rank_time);
	if (ret != 0) {
		DB_ER_RETURN(-15, "in_ranking_targets:get_ranking_info_error\n");
	}

	// 2. get random targets in range
	rank_count = min(rank - 1, RANKING_RANDOM_COUNT);

//	// target rank value in range [min_rank, max_rank]
//	min_rank = max(1, rank - MAX_RANKING_RANDOM_RANGE);
//	max_rank = rank-1;
//	rank_count = min(max_rank - min_rank + 1, RANKING_RANDOM_COUNT);

	if (rank_count <= 0) {
		out_ptr = out_buffer;
		out_ptr += sprintf(out_ptr, "%d %d %d", rank, rank_time, 0);
		return 0;	// normal return
	}

	bzero(random_rank_list, sizeof(random_rank_list));
//	rank_count = get_random_list(random_rank_list, rank_count, min_rank, max_rank);
	rank_count = __get_range_list(random_rank_list, rank);
	
	ptr = q_buffer;
	ptr += sprintf(ptr, SQL_FRONT_RANK_TARGETS);
	for (int i = 0; i < rank_count; i++) {
		if (i > 0) {
			ptr += sprintf(ptr, ",%d", random_rank_list[i]);
		} else {
			ptr += sprintf(ptr, "%d", random_rank_list[i]);
		}
	}
	ptr += sprintf(ptr, SQL_TAIL_RANK_TARGETS);

	len = ptr-q_buffer;
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-35, "in_ranking_targets:mysql_errno %d\n", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-13, "in_ranking_targets:null_result\n");
	}
	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -27;
		DB_ER_PRINT(ret, "in_ranking_targets:num_row_error %d\n", num_row);
		goto cleanup;
	}

	count = 0;
	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, "%d %d %d", rank, rank_time, num_row);
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			ret = -37;
			BUG_PRINT(ret, "in_ranking_targets:fetch_row_overflow %d/%d"
			, count, num_row);
			goto cleanup;
		}

		field_count = mysql_field_count(*pconn);
		// eid, rank, icon, rating, alias
		if (field_count != 7) {
			ret = -47;
			ERROR_PRINT(ret, "in_ranking_targets:field_count %d != 7"
			, field_count);
			goto cleanup;
		}
		game_count = strtol_safe(row[4], 0);
		game_win = strtol_safe(row[5], 0);
		rating = (game_count <= 0) ? 1 : (game_win * 1.0f / game_count);
		
		out_ptr += sprintf(out_ptr, " %d %d %d %d %lf %s", strtol_safe(row[0], -1)
		, strtol_safe(row[1], -1), strtol_safe(row[2], -1)
		, strtol_safe(row[3], -1), rating, row[6]);
	}

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}

bool __check_in_range_list(int rank, int target_rank)
{
	int target_list[RANKING_RANDOM_COUNT];
	int target_count = __get_range_list(target_list, rank);
	for (int i = 0; i < target_count; i++) {
		if (target_list[i] == target_rank) {
			return true;
		}
	}
	return false;
}

#define SQL_GET_TWO_DECK		"SELECT * FROM evil_deck WHERE eid=%d OR eid=%d"
int get_two_deck(MYSQL **pconn, char *q_buffer, char *deck1, char *deck2, int eid1, int eid2)
{
	int ret;
	int err;
	int len;
	int out_eid1, out_eid2;
	
	if (eid1<=0 || eid2<=0) {
		ERROR_RETURN(-15, "get_two_deck:invalid_eid eid1=%d eid2=%d", eid1, eid2);
	}

	if (eid1==eid2) {
		ERROR_RETURN(-25, "get_two_deck:same_eid eid1=%d eid2=%d", eid1, eid2);
	}

	// note: eid2 may be within AI range
	len = sprintf(q_buffer, SQL_GET_TWO_DECK, eid1, eid2);
	ERROR_NEG_RETURN(len, "get_two_deck:sprintf");

	// printf("get_two_deck:query=[%s]\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "get_two_deck:query err %d", mysql_errno(*pconn));
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_two_deck:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid) + 1 (slot)
	if (field_count != EVIL_CARD_MAX + 1 + 1) {
		ret = -7;
		ERROR_PRINT(ret, "get_two_deck:field_count %d != card_count+2 %d",
			field_count, EVIL_CARD_MAX+2);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	// normally 2 rows, for eid1 and eid2, possible 1 row for eid2 is AI
	// 0 is impossible, should be error
	if ((num_row != 2 && eid2 > MAX_AI_EID) || num_row <= 0) {
		ret = -6;	 // logical error
		ERROR_PRINT(ret, "get_two_deck:deck_not_enough row=%d %d %d", num_row, eid1, eid2);
		goto cleanup;
	}


	// ok, we are going to get the first row
	// this is eid1, deck1
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(ret, "get_two_deck:deck_null_row %d %d", eid1, eid2);
		goto cleanup;
	}

	err = row_to_deck(row, &out_eid1, deck1, EVIL_NUM_DECK_MAX);
	if (err < 0) {
		ret = -17;
		ERROR_PRINT(ret, "get_two_deck:row_to_deck %d", err);
		goto cleanup;
	}

	if (eid2<=MAX_AI_EID) {
		ret = 0;
		out_eid2 = eid2;
		strcpy(deck2, "AI");  // must have something, not \0
		// DEBUG_PRINT(eid2, "get_two_deck:ai");
		goto output;
	}

	// eid2, deck2
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -23;
		ERROR_PRINT(ret, "get_two_deck:deck_null_row %d %d", eid1, eid2);
		goto cleanup;
	}

	err = row_to_deck(row, &out_eid2, deck2, EVIL_NUM_DECK_MAX);
	if (err < 0) {
		ret = -27;
		ERROR_PRINT(ret, "get_two_deck:row_to_deck %d", err);
		goto cleanup;
	}

output:
	if (out_eid1!=eid1) {  // assume eid2==out_eid2
		char ptr[EVIL_CARD_MAX+1];
		sprintf(ptr, "%.400s", deck1);
		sprintf(deck1, "%.400s", deck2);
		sprintf(deck2, "%.400s", ptr);
	}
	ret = 0; // ok 

	// DEBUG_PRINT(0, "in_get_two_deck:deck1=[%s] deck2=[%s]", deck1, deck2);

cleanup:
	mysql_free_result(result);
	return ret;
}

// get hero id from deck (assume 400 char)
int __get_hero(const char *deck)
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


typedef struct {
	int eid;
	int hero_id;
	int hp;
	int energy;
	int rank;
	int rank_time;
	int icon;
	char alias[EVIL_ALIAS_MAX+10];
} ranking_data_t;
//#define SQL_RANKING_TARGET		"SELECT er.eid, er.rank, eu.rank_time, eu.icon, eu.alias FROM evil_ranking er INNER JOIN evil_user eu ON er.eid=eu.eid WHERE er.eid in (%d,%d) ORDER BY er.rank ASC"
// #define SQL_RANKING_DATA		"SELECT er.eid, er.rank, eu.rank_time, eu.icon, eu.alias, eh.hero_id, eh.hp, eh.energy FROM evil_ranking er INNER JOIN evil_user eu ON er.eid=eu.eid LEFT JOIN evil_hero eh ON er.eid=eh.eid WHERE (er.eid=%d AND eh.hero_id=%d) OR (er.eid=%d AND eh.hero_id=%d) ORDER BY er.rank ASC"
#define SQL_RANKING_DATA	"SELECT er.eid, er.rank, eu.rank_time, eu.icon, eu.alias, IFNULL(eh.hero_id, 0), IFNULL(eh.hp, 0), IFNULL(eh.energy,0) FROM evil_ranking er INNER JOIN evil_user eu ON er.eid=eu.eid LEFT JOIN evil_hero eh ON er.eid=eh.eid AND eh.hero_id IN (%d,%d) WHERE er.eid IN (%d,%d) ORDER BY er.rank ASC"
int __get_ranking_data(MYSQL **pconn, char *q_buffer, int eid, int target_eid, int hero_id, int target_hero_id, ranking_data_t &data1, ranking_data_t &data2)
{
	int ret;
	int err;
	int len;
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;
	ranking_data_t tmp_data;
	bzero(&tmp_data, sizeof(tmp_data));
	bzero(&data1, sizeof(data1));
	bzero(&data2, sizeof(data2));

	len = sprintf(q_buffer, SQL_RANKING_DATA
	, hero_id, target_hero_id
	, eid, target_eid);
	DEBUG_PRINT(1, "get_ranking_data: q_buffer[%s]", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-15, "get_ranking_data:mysql_errno %d", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_ranking_data:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 2) {
		ret = -6;
		ERROR_PRINT(ret
		, "get_ranking_data:num_row_error row[%d] eid[%d] target_eid[%d]"
		, num_row, eid, target_eid);
		goto cleanup;
	}

	// eid, rank, rank_time, icon, alias, hero_id, hp, energy
	field_count = mysql_field_count(*pconn);
	if (field_count != 8) {
		ret = -7;
		ERROR_PRINT(ret, "get_ranking_data:field_count %d != 8"
		, field_count);
		goto cleanup;
	}

	for (int i = 0; i < num_row; i++) {
		row = mysql_fetch_row(result);
		if (row == NULL) {
			ret = -17;
			ERROR_PRINT(ret, "get_ranking_data:row_data_null");
			goto cleanup;
		}
		tmp_data.eid 		= strtol_safe(row[0], -1);
		tmp_data.rank		= strtol_safe(row[1], -1);
		tmp_data.rank_time	= strtolong_safe(row[2], -1);
		tmp_data.icon		= strtol_safe(row[3], -1);
		strcpy(tmp_data.alias, row[4]);

		// may be 0
		tmp_data.hero_id	= strtol_safe(row[5], -1);
		tmp_data.hp			= strtol_safe(row[6], -1);
		tmp_data.energy		= strtol_safe(row[7], -1);

		if (tmp_data.eid == eid) { 
			if (tmp_data.hero_id == hero_id) {
				data1 = tmp_data;
				continue;
			}
			data1.eid = tmp_data.eid;
			data1.rank = tmp_data.rank;
			data1.rank_time = tmp_data.rank_time;
			data1.icon = tmp_data.icon;
			strcpy(data1.alias, tmp_data.alias);
			data1.hero_id = hero_id;
			continue;
		}

		if (tmp_data.eid == target_eid) {
			if (tmp_data.hero_id == target_hero_id) {
				data2 = tmp_data;
				continue;
			}
			data2.eid = tmp_data.eid;
			data2.rank = tmp_data.rank;
			data2.rank_time = tmp_data.rank_time;
			data2.icon = tmp_data.icon;
			strcpy(data2.alias, tmp_data.alias);
			data2.hero_id = target_hero_id;
			continue;
		}

		ret = -16;
		ERROR_PRINT(ret
		, "get_ranking_data:data error tmp_eid[%d] tmp_rank[%d]"
		, tmp_data.eid, tmp_data.rank);
		goto cleanup;
	}

	DEBUG_PRINT(0, "data1: eid[%d] hero_id[%d] hp[%d] energy[%d] rank[%d] rank_time[%d] icon[%d] alias[%s]", data1.eid, data1.hero_id, data1.hp, data1.energy, data1.rank, data1.rank_time, data1.icon, data1.alias);
	DEBUG_PRINT(0, "data2: eid[%d] hero_id[%d] hp[%d] energy[%d] rank[%d] rank_time[%d] icon[%d] alias[%s]", data2.eid, data2.hero_id, data2.hp, data2.energy, data2.rank, data2.rank_time, data2.icon, data2.alias);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}



int in_check_ranking_target(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;

	ranking_data_t data;
	ranking_data_t target_data;
	int eid;
	int hero_id;
	int target_eid;
	int target_rank;
	int target_hero_id;

	char deck1[EVIL_CARD_MAX+1];
	char deck2[EVIL_CARD_MAX+1];

	bool in_range_list;

	ret = sscanf(in_buffer, IN_CHECK_RANKING_TARGET_PRINT
	, &eid, &target_eid, &target_rank);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_check_ranking_target:input_invalid\n");
	}

	// 1. get deck
	ret = get_two_deck(pconn, q_buffer, deck1, deck2, eid, target_eid);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_check_ranking_target:get_two_deck %d %d", eid, target_eid);
	}

	if (strlen(deck1) != EVIL_CARD_MAX || strlen(deck2) != EVIL_CARD_MAX) {
		DB_ER_RETURN(-16, "in_check_ranking_target:deck_empty %d %d", eid, target_eid);
	}

	// 2. get deck2 hero_id
	hero_id = __get_hero(deck1);
	if (hero_id <= 0 || hero_id > HERO_MAX) {
		DB_ER_RETURN(-26, "in_check_ranking_target:invalid_hero_id %d", hero_id);
	}
	target_hero_id = __get_hero(deck2);
	if (target_hero_id <= 0 || target_hero_id > HERO_MAX) {
		DB_ER_RETURN(-36, "in_check_ranking_target:invalid_hero_id %d", target_hero_id);
	}

	// 3. get ranking data
	ret = __get_ranking_data(pconn, q_buffer, eid, target_eid
	, hero_id, target_hero_id, data, target_data);
	if (ret != 0) {
		DB_ER_RETURN(-46, "in_check_ranking_target:get_ranking_data_fail %d %d"
		, eid, target_eid);
	}

	if ((target_data.eid == target_eid) && (target_data.rank != target_rank)) {
		// rank has changed, need notify client to refresh
		sprintf(out_buffer, "%d rank_has_changed", -1);
		return 0;
	}

	if (data.rank <= target_data.rank) {
		ret = -26;
		DB_ER_RETURN(ret
		, "in_check_ranking_target:target_rank[%d]_is_lower_than_yours[%d]"
		, target_data.rank, data.rank);
	}

	if (data.rank_time <= 0) {
		ret = -36;
		DB_ER_RETURN(ret, "in_check_ranking_target:rank_challenge_time <= 0");
	}

	// check target_rank is in target list
	in_range_list = __check_in_range_list(data.rank, target_data.rank);
	if (!in_range_list) {
		ret = -46;
		DB_ER_RETURN(ret
		, "in_check_ranking_target:target_rank_not_in_target_list eid[%d] rank[%d] target_eid[%d] target_rank[%d]"
		, eid, data.rank, target_eid, target_data.rank);
	}

	sprintf(out_buffer, "0 %d %d %d %d %d %s %s %d %d %d %d %d %d %d %d %.400s %.400s"
	, eid, target_eid, data.rank_time
	, data.icon, target_data.icon, data.alias, target_data.alias
	, data.hero_id, target_data.hero_id, data.hp, target_data.hp
	, data.energy, target_data.energy, data.rank, target_data.rank
	, deck1, deck2);

	ret = 0;
	return ret;
}


#define SQL_RANKING_CHALLENGE		"SELECT er.eid, eu.rank_time, eu.icon, eu.alias FROM evil_ranking er INNER JOIN evil_user eu ON er.eid=eu.eid WHERE er.eid in (%d,%d) ORDER BY er.rank ASC"
int in_ranking_challenge(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;

	int eid;
	int hero_id;
	int target_eid;
	int target_hero_id;
	ranking_data_t data;
	ranking_data_t target_data;
	int resp;

	char deck1[EVIL_CARD_MAX+1];
	char deck2[EVIL_CARD_MAX+1];

	// 1. get self rank data
	ret = sscanf(in_buffer, IN_RANKING_CHALLENGE_SCAN
	, &eid, &target_eid, &resp);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_ranking_challenge:input_invalid\n");
	}

	// 1. get deck
	ret = get_two_deck(pconn, q_buffer, deck1, deck2, eid, target_eid);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_ranking_challenge:get_two_deck %d %d", eid, target_eid);
	}

	if (strlen(deck1) != EVIL_CARD_MAX || strlen(deck2) != EVIL_CARD_MAX) {
		DB_ER_RETURN(-16, "in_ranking_challenge:deck_empty %d %d", eid, target_eid);
	}
	// 2. get deck2 hero_id
	hero_id = __get_hero(deck1);
	if (hero_id <= 0 || hero_id > HERO_MAX) {
		DB_ER_RETURN(-26, "in_ranking_challenge:invalid_hero_id %d", hero_id);
	}
	target_hero_id = __get_hero(deck2);
	if (target_hero_id <= 0 || target_hero_id > HERO_MAX) {
		DB_ER_RETURN(-36, "in_ranking_challenge:invalid_hero_id %d", target_hero_id);
	}
	ret = __get_ranking_data(pconn, q_buffer, eid, target_eid
	, hero_id, target_hero_id, data, target_data);
	if (ret != 0) {
		DB_ER_RETURN(-46, "in_ranking_challenge:get_ranking_data_fail %d %d"
		, eid, target_eid);
	}

	if (target_data.rank_time <= 0) {
		ret = -27;
		DB_ER_RETURN(ret, "in_ranking_challenge:oppo_rank_challenge_time <= 0");
	}

	sprintf(out_buffer, "0 %d %d %d %d %d %d %s %s %d %d %d %d %d %d %d %d %.400s %.400s"
	, eid, target_eid, data.rank_time, resp
	, data.icon, target_data.icon, data.alias, target_data.alias
	, data.hero_id, target_data.hero_id, data.hp, target_data.hp
	, data.energy, target_data.energy, data.rank, target_data.rank
	, deck1, deck2);

	return 0;
}


#define SQL_RANK_INFO		"SELECT er.eid, er.rank, eu.rank_time, eu.icon, eu.alias FROM evil_ranking er INNER JOIN evil_user eu ON er.eid=eu.eid WHERE er.eid in (%d,%d) ORDER BY er.rank ASC"
int __get_rank_info(MYSQL **pconn, char *q_buffer, char *out_buffer
, int eid1, int eid2, int &rank1, int &rank2, int &icon1, int &icon2
, char *alias1, char *alias2)
{
	int ret;
	int len;
	int err;
	int field_count;
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	len = sprintf(q_buffer, SQL_RANK_INFO, eid1, eid2);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-15, "__get_rank_info:mysql_errno %d\n", err);
	}
	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "__get_rank_info:null_result\n");
	}
	num_row = mysql_num_rows(result);
	if (num_row != 2) {
		ret = -6;
		DB_ER_PRINT(ret
		, "__get_rank_info:num_row_error row[%d] eid1[%d] eid2[%d]\n"
		, num_row, eid1, eid2);
		goto cleanup;
	}
	// eid, rank, alias
	field_count = mysql_field_count(*pconn);
	if (field_count != 5) {
		ret = -7;
		DB_ER_PRINT(ret, "__get_rank_info:field_count %d != 5"
		, field_count);
		goto cleanup;
	}
	rank1 = 0, rank2 = 0;
	for (int i = 0; i < num_row; i++) {
		row = mysql_fetch_row(result);
		if (row == NULL) {
			ret = -17;
			DB_ER_PRINT(ret, "__get_rank_info:row_data_null\n");
			goto cleanup;
		}
		int tmp_eid = strtol_safe(row[0], -1);
		if (tmp_eid == eid1) {
			rank1 = strtol_safe(row[1], -1);
			icon1 = strtol_safe(row[2], -1);
			strcpy(alias1, row[3]);
			continue;
		}
		if (tmp_eid == eid2) {
			rank2 = strtol_safe(row[1], -1);
			icon2 = strtol_safe(row[2], -1);
			strcpy(alias2, row[3]);
			continue;
		}
		ret = -27;
		DB_ER_PRINT(ret, "__get_rank_info:row_data_error\n");
		goto cleanup;
	}
	if (rank1 <= 0 || rank2 <= 0) {
		DB_ER_PRINT(ret, "__get_rank_info:rank_data_error eid1[%d] rank1[%d] eid2[%d] rank2[%d]\n", eid1, rank1, eid2, rank2);
		goto cleanup;
	}

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}

//#define SQL_FRONT_UPDATE_RANKING_DATA	"UPDATE evil_ranking SET rank=CASE eid"
//#define SQL_UPDATE_RANKING_DATA_ROW		" WHEN %d THEN %d"
//#define SQL_TAIL_UPDATE_RANKING_DATA	" END WHERE eid IN (%d, %d)"
#define SQL_EXCHANGE_RANKING		"UPDATE evil_ranking AS er1 JOIN evil_ranking er2 ON (er1.eid=%d AND er2.eid=%d) OR (er1.eid=%d AND er2.eid=%d) SET er1.rank=er2.rank,er2.rank=er1.rank"
int __exchange_ranking(MYSQL **pconn, char *q_buffer, char *out_buffer, int eid1, int eid2, int rank1, int rank2)
{
	int ret;
	int len;
	int err;

//	char *ptr;

	bool in_range_list = __check_in_range_list(rank1, rank2);
	if (!in_range_list) {
		WARN_PRINT(1, "__exchange_ranking: rank2[%d]_not_in_range_of_rank1[%d]"
		, rank2, rank1);
	}

//	ptr = q_buffer;
//	ptr += sprintf(ptr, SQL_FRONT_UPDATE_RANKING_DATA);
//	ptr += sprintf(ptr, SQL_UPDATE_RANKING_DATA_ROW, eid1, rank2);
//	ptr += sprintf(ptr, SQL_UPDATE_RANKING_DATA_ROW, eid2, rank1);
//	ptr += sprintf(ptr, SQL_TAIL_UPDATE_RANKING_DATA, eid1, eid2);
//	len = ptr-q_buffer;
	len = sprintf(q_buffer, SQL_EXCHANGE_RANKING, eid1, eid2, eid2, eid1);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-5, "__exchange_ranking:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 2) {
		DB_ER_RETURN(-6, "__exchange_ranking:affected_row wrong %d", ret);
	}

	return 0;
}

//#define SQL_SAVE_RANK_CHALLENGE_RECORD	"INSERT INTO evil_ranking_history VALUES "
//#define SQL_RANK_CHALLENGE_RECORD 		"(%d, %d, %d, %d, %d FROM_UNIXTIME(%ld))"
#define SQL_SAVE_RANK_CHALLENGE_RECORD	"INSERT INTO evil_ranking_history VALUES (%d, %d, %d, %d, %d, FROM_UNIXTIME(%ld))"
int __save_rank_challenge_record(MYSQL **pconn, char *q_buffer, int eid1, int eid2, int rank1, int rank2, bool is_exchange, time_t cur_time)
{
	int ret;
	int len;
	int err;
//	char *ptr;
//
//	int n_rank1 = is_exchange ? rank2 : rank1;
//	int n_rank2 = is_exchange ? rank1 : rank2;
//
//	// save the record, enemy_id < 0 means be challenged
//	ptr = q_buffer;
//	ptr += sprintf(ptr, SQL_SAVE_RANK_CHALLENGE_RECORD);
//	ptr += sprintf(ptr, SQL_RANK_CHALLENGE_RECORD, eid1, eid2
//	, rank1, n_rank1, cur_time);
//	ptr += sprintf(ptr, ",");
//	ptr += sprintf(ptr, SQL_RANK_CHALLENGE_RECORD, eid2, -eid1
//	, rank2, n_rank2, cur_time);

	len = sprintf(q_buffer, SQL_SAVE_RANK_CHALLENGE_RECORD, eid1, eid2
	, rank1, rank2, is_exchange, cur_time);

//	len = ptr - q_buffer;
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DEBUG_PRINT(1, "q_buffer[%s]", q_buffer);
		err = mysql_errno(*pconn);
		WARN_PRINT(-5, "__save_rank_challenge_record:mysql_errno %d", err);
		return -5;
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		err = mysql_errno(*pconn);
		WARN_PRINT(-6, "__save_rank_challenge_record:mysql_errno %d", err);
		return -6;
	}

	return 0;
}

#define SQL_SUB_RANKING_CHALLENGE_COUNT	"UPDATE evil_user SET rank_time=rank_time-1 WHERE eid=%d"
int __auto_sub_rank_challenge_count(MYSQL **pconn, char *q_buffer, int eid1)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_SUB_RANKING_CHALLENGE_COUNT, eid1);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		WARN_PRINT(-5, "__auto_sub_rank_challenge_count:mysql_errno %d", err);
		return -5;
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		err = mysql_errno(*pconn);
		WARN_PRINT(-6, "__auto_sub_rank_challenge_count:mysql_errno %d", err);
		return -6;
	}

	ret = 0;
	return ret;
}
int in_change_ranking_data(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;

	int winner;
	int eid1;
	int rank1;
	int icon1;
	char alias1[EVIL_ALIAS_MAX + 5];
	int eid2;
	int rank2;
	int icon2;
	char alias2[EVIL_ALIAS_MAX + 5];
	bool is_exchange;

	// 1. get rank data
	ret = sscanf(in_buffer, IN_CHANGE_RANKING_DATA_SCAN
	, &eid1, &eid2, &winner);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_change_ranking_data:input_invalid\n");
	}
	if (eid2 < 0) {	eid2 = -eid2; }

	ret = __get_rank_info(pconn, q_buffer, out_buffer, eid1, eid2
	, rank1, rank2, icon1, icon2, alias1, alias2);
	ERROR_RETURN(ret, "in_change_ranking_data:get_rank_info_error");

	WARN_PRINT(rank1 < rank2, "in_change_ranking_data: rank1[%d] > rank2[%d]"
	, rank1, rank2);

	// eid1 is challenger
	// only challenger win the game could change ranking data
	is_exchange = false;
	if (winner == 1 && (rank1 > rank2)) {
		ret = __exchange_ranking(pconn, q_buffer, out_buffer
		, eid1, eid2, rank1, rank2);
		ERROR_RETURN(ret
		, "in_change_ranking_data:update_challenger_rank_fail");
		is_exchange = true;

		ret = __auto_sub_rank_challenge_count(pconn, q_buffer, eid1);
	}

	// save challenge record in ranking_history
	ret = __save_rank_challenge_record(pconn, q_buffer, eid1
	, eid2, rank1, rank2, is_exchange, time(NULL));
	WARN_PRINT(ret, "in_change_ranking_data: save_rank_challenge_record_fail");

	return 0;
}

#define SQL_RANKING_HISTORY		"SELECT eid1, eid2, rank1, rank2, tb1.icon, tb2.icon, tb1.alias, tb2.alias, success, UNIX_TIMESTAMP(time) from evil_ranking_history LEFT JOIN evil_user tb1 ON (eid1=tb1.eid) LEFT JOIN evil_user tb2 ON (eid2=tb2.eid) WHERE eid1=%d or eid2=%d ORDER BY time DESC LIMIT %d"
int in_get_ranking_history(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;

	int eid;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	int eid1;
	int eid2;
	int rank1;
	int rank2;
	int icon1;
	int icon2;
	char alias1[EVIL_ALIAS_MAX + 10], alias2[EVIL_ALIAS_MAX + 10];
	int success;
	time_t rank_time;
	char * out_ptr;

	// 1. get self rank data
	ret = sscanf(in_buffer, IN_RANKING_HISTORY_SCAN, &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "in_get_ranking_history:input_invalid\n");
	}

	len = sprintf(q_buffer, SQL_RANKING_HISTORY, eid, eid
	, RANKING_HISTORY_COUNT);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DEBUG_PRINT(1, "q_buffer[%s]", q_buffer);
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-15, "in_get_ranking_history:mysql_errno %d\n", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "in_get_ranking_history:null_result\n");
	}
	// eid, rank, alias
	field_count = mysql_field_count(*pconn);
	if (field_count != 10) {
		ret = -7;
		DB_ER_PRINT(ret, "in_get_ranking_history:field_count %d != 3"
		, field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, "%d", num_row);
	for (int i = 0; i < num_row; i++) {
		row = mysql_fetch_row(result);
		if (row == NULL) {
			ret = -17;
			DB_ER_PRINT(ret, "in_get_ranking_history:row_data_null\n");
			goto cleanup;
		}
		eid1 = strtol_safe(row[0], -1);
		eid2 = strtol_safe(row[1], -1);
		rank1 = strtol_safe(row[2], -1);
		rank2 = strtol_safe(row[3], -1);
		icon1 = strtol_safe(row[4], -1);
		icon2 = strtol_safe(row[5], -1);
		strcpy(alias1, row[6]);
		strcpy(alias2, row[7]);
		success = strtol_safe(row[8], -1);
		rank_time = strtolong_safe(row[9], -1);
		
		out_ptr += sprintf(out_ptr, " %d %d %d %d %d %d %s %s %d %ld"
		, eid1, eid2, rank1, rank2, icon1, icon2
		, alias1, alias2, success, rank_time);
	}

cleanup:
	mysql_free_result(result);
	return 0;
}

#define SQL_SAVE_RANKING_CHALLENGE	"UPDATE evil_user SET rank_time=%d WHERE eid=%d"
int in_save_ranking_challenge(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;

	int eid;
	int rank_time;

	ret = sscanf(in_buffer, IN_SAVE_RANKING_CHALLENGE_SCAN
	, &eid, &rank_time);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_save_ranking_challenge:input_invalid\n");
	}

	if (eid < 0 || rank_time < 0) {
		DB_ER_RETURN(-15, "in_save_ranking_challenge:input_invalid\n");
	}

	len = sprintf(q_buffer, SQL_SAVE_RANKING_CHALLENGE, rank_time, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-25, "in_save_ranking_challenge:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		DB_ER_RETURN(-6
		, "in_save_ranking_challenge:affected_row wrong %d rank_time=%d eid=%d"
		, ret, rank_time, eid);
	}

	return 0;
}

#define SQL_RESET_RANKING_TIME	"UPDATE evil_user SET rank_time=%d"
int in_reset_ranking_time(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_RESET_RANKING_TIME, MAX_RANKING_CHALLENGE_TIME);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-25, "in_reset_ranking_time:mysql_errno %d\n", err);
	}
	sprintf(out_buffer, "0");

	return 0;
}

long get_unique_time()
{
	static long last_ts = 0;
	long pts;
	struct tm tt;
	time_t now;
	
	now = time(NULL);
	localtime_r(&now, &tt);

	pts = (long)(tt.tm_year-100) * 10000000000000L
	+ (long)(tt.tm_mon+1)		 * 100000000000L
	+ (long)tt.tm_mday 			 * 1000000000L
	+ (long)tt.tm_hour 			 * 10000000L
	+ (long)tt.tm_min 			 * 100000L
	+ (long)tt.tm_sec 			 * 1000L;

	if (pts > last_ts) {
		last_ts = pts;
	} else {
		if ((last_ts % 1000) >= 999) {
			pts = last_ts;
		} else {
			last_ts ++;
			pts = last_ts;
		}
	}
	
	return pts;
}

#define SQL_CHECK_USER_EXIST		"SELECT username,password FROM evil_user WHERE anysdk_uid='%s'"

int in_check_login(MYSQL **pconn, char *q_buffer, const char *in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;


	/*
	username may longer than 30
	so we should add anysdk_uid in evil_user after rank_time
	1.check if uid is exist in evil_user
	2.if exist, return username,password
	3.if not exist, register a new user
	, create a unique username by time: ["#anysdk@"][unique_time]
	*/

	char uid[390];
	long unique_time;
	char username[EVIL_USERNAME_MAX+10];
	char password[EVIL_PASSWORD_MAX+10];
	int channel;
	char esc_uid[800];
	char esc_password[EVIL_PASSWORD_MAX * 2 + 5];
	char in_reg_buffer[DB_BUFFER_MAX+5];

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	ret = sscanf(in_buffer, IN_CHECK_LOGIN_SCAN
	, uid, password, &channel);

	if (ret != 3) {
		DB_ER_RETURN(-5, "in_check_login:input_invalid");
	}

	len = mysql_real_escape_string(*pconn, esc_uid, uid, 
		strlen(uid));
	DB_ER_NEG_RETURN(len, "esc_uid");
	len = mysql_real_escape_string(*pconn, esc_password, password, 
		strlen(password));
	DB_ER_NEG_RETURN(len, "esc_password");

	len = sprintf(q_buffer, SQL_CHECK_USER_EXIST, esc_uid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-25, "in_check_login:mysql_errno %d\n", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "in_check_login:null_result\n");
	}

	// check affected row
	num_row = mysql_num_rows(result);
	if (num_row>1) {
		DB_ER_RETURN(-16, "in_check_login:uid_not_unique %s", uid);
		goto cleanup;
	}

	if (num_row == 0) {
		unique_time = get_unique_time();
		sprintf(username, "#anysdk@%ld", unique_time);
		// DEBUG_PRINT(0, "in_check_login:username=%s", username);
		sprintf(in_reg_buffer, IN_REGISTER_PRINT
		, username, esc_password
		, "localhost", 0, channel, uid);
		ret = in_register(pconn, q_buffer, in_reg_buffer, out_buffer);
		if (ret != 0) {
			DB_ER_RETURN(-6, "register_fail");
		}
		// force write out_buffer
		sprintf(out_buffer, "%d %s %s", 0, username, password);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -7;
		DB_ER_PRINT(ret, "in_check_login:row_data_null\n");
		goto cleanup;
	}
	sprintf(username, "%s", row[0]);
	sprintf(password, "%s", row[1]);
	/*
	if ((strcmp(username, row[0]) == 0)
	&& (strcmp(password, row[1]) != 0)) {
		ret = -17;
		DB_ER_PRINT(ret, "in_check_login:password_error\n");
		goto cleanup;
	}
	*/

	sprintf(out_buffer, "%d %s %s", 0, username, password);
cleanup:
	mysql_free_result(result);
	return ret;
}



//	ranking
///////////////////////////////////////////////

#define SQL_ADD_EVIL_NOTICE	"INSERT INTO evil_message (recv_eid,send_eid,send_alias,time,unread,title,message) VALUES (%d,%d,'%s',%ld,1,'%s','%s')"

int in_add_evil_message(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;

	int eid;
	int send_eid;
	char alias[EVIL_ALIAS_MAX + 5];
	char title[EVIL_MESSAGE_TITLE_MAX + 5];
	char message[EVIL_MESSAGE_MAX + 5];
	char esc_title[EVIL_MESSAGE_TITLE_MAX * 2 + 5];
	char esc_message[EVIL_MESSAGE_MAX * 2 + 5];

	char fmt[50];
	sprintf(fmt, "%%d %%d %%s %%%ds %%%ds", EVIL_MESSAGE_TITLE_MAX, EVIL_MESSAGE_MAX);
	// printf("fmt:%s\n", fmt);

	// ret = sscanf(in_buffer, "%d %d %s %100s %300s", &eid, &send_eid, alias, title, message);
	ret = sscanf(in_buffer, fmt, &eid, &send_eid, alias, title, message);
	if (ret != 5) {
		DB_ER_RETURN(-5, "in_add_evil_message:invalid_input");
	}

	len = mysql_real_escape_string(*pconn, esc_title, title, strlen(title));
	len = mysql_real_escape_string(*pconn, esc_message, message, strlen(message));
	
	len = sprintf(q_buffer, SQL_ADD_EVIL_NOTICE, eid, send_eid, alias, time(NULL), esc_title, esc_message);
	// DEBUG_PRINT(0, "in_add_evil_message:query=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "in_add_evil_message:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-16, "in_add_evil_message:affected_row %d \n", eid);
	}

	sprintf(out_buffer, "%d", 0);

	ret = 0;

	return ret;
		
}


#define SQL_EVIL_NOTICE_TOTAL	"SELECT COUNT(*) FROM evil_message WHERE recv_eid=%d AND unread=1"
#define SQL_LIST_EVIL_NOTICE	"SELECT message_id,send_eid,send_alias,time,unread,title FROM evil_message WHERE recv_eid=%d and unread=1 ORDER BY message_id DESC LIMIT %d,%d"

int in_list_evil_message(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int count;
	char *ptr;

	int eid;
	int total;
	int start_num;
	int page_size;
	char title[EVIL_MESSAGE_TITLE_MAX + 5];

	ret = sscanf(in_buffer, "%d %d %d", &eid, &start_num, &page_size);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_list_evil_message:invalid_input");
	}


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	// 1.get total message
	len = sprintf(q_buffer, SQL_EVIL_NOTICE_TOTAL, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "in_list_evil_message:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "in_list_evil_message:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		sprintf(out_buffer, "%d %d %d 0 0", eid, start_num, page_size);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ERROR_PRINT(-13, "in_list_evil_message:row_null %d", eid);
		sprintf(out_buffer, "%d %d %d 0 0", eid, start_num, page_size);
		goto cleanup;
	}

	total = strtol_safe(row[0], -1);
	// DEBUG_PRINT(0, "in_list_evil_message:total=%d", total);

	if (total <= 0) {
		sprintf(out_buffer, "%d %d %d 0 0", eid, start_num, page_size);
		goto cleanup;
	}
	mysql_free_result(result);

	// 2.get message by start_num, page_size
	len = sprintf(q_buffer, SQL_LIST_EVIL_NOTICE, eid, start_num, page_size);
	// DEBUG_PRINT(0, "in_list_evil_message:list_q_buffer=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "in_list_evil_message:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-23, "in_list_evil_message:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		sprintf(out_buffer, "%d %d %d %d 0", eid, start_num, page_size, total);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// message_id, send_eid, send_alias, time, unread, title
	if (field_count != 6) {
		ret = -7;
		DB_ER_PRINT(ret, "in_list_evil_message:field_count %d != 6", field_count);
		goto cleanup;
	}
		
	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d %d %d %d", eid, start_num, page_size
	, total, num_row);
	count = 0;	
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-7, "in_list_evil_message:fetch_row_overflow %d", count);
			break;
		}

		sprintf(title, "%s", row[5]);
		if (title[0] == '\0') {
			WARN_PRINT(-6, "in_list_evil_message:title_null %d", eid);
			sprintf(title, "-");
		}

		ptr += sprintf(ptr, " %s %s %s %s %s %s", row[0], row[1]
		, row[2], row[3], row[4], title);

	}

	// out_buffer = [eid] [start_num] [page_size] [total] [num_row] [message_info]
	// message info = [message_id] [send_eid] [send_alias] [time] [unread] [title]
	// DEBUG_PRINT(0, "in_list_evil_message:out_buffer=%s", out_buffer);
	ret = 0;  // make sure ret is OK (0)

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_UPDATE_EVIL_NOTICE	"UPDATE evil_message SET unread=0 WHERE message_id=%ld AND recv_eid=%d AND unread=1"
#define SQL_GET_EVIL_NOTICE	"SELECT message_id,recv_eid,send_eid,send_alias,time,unread,title,message FROM evil_message where message_id=%ld AND recv_eid=%d"
int in_read_evil_message(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int err;

	int eid;
	long message_id;

	ret = sscanf(in_buffer, "%d %ld", &eid, &message_id);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_read_evil_message:invalid_input");
	}

	// 1.update message unread flag
	len = sprintf(q_buffer, SQL_UPDATE_EVIL_NOTICE, message_id, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-5, "in_read_evil_message:mysql_errno %d\n", err);
	}


	// 2.get message
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;
	len = sprintf(q_buffer, SQL_GET_EVIL_NOTICE, message_id, eid); 
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-65, "in_read_evil_message:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-23, "in_read_evil_message:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		DB_ER_PRINT(-15, "in_read_evil_message:no_such_message");
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// message, recv_eid, send_eid, send_alias, time, unread, title, message
	if (field_count != 8) {
		ret = -7;
		DB_ER_PRINT(ret, "in_read_evil_message:field_count %d != 8", field_count);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		DB_ER_PRINT(-15, "in_read_evil_message:row_null");
		goto cleanup;
	}

	sprintf(out_buffer, "%d %s %s %s %s %s %s %s %s", eid, row[0], row[1]
	, row[2], row[3], row[4], row[5], row[6], row[7]);

	// DEBUG_PRINT(0, "in_read_evil_message:out_buffer=%s", out_buffer);
	
	ret = 0;

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_ADD_RANK_REWARD_MESSAGE	"INSERT INTO evil_message (recv_eid,send_eid,send_alias,time,unread,title,message) SELECT eid,%d,'%s',%ld,1,'%s','%s' FROM evil_ranking WHERE rank>=%d AND rank<=%d"
#define SQL_ADD_RANK_REWARD_MESSAGE2	"INSERT INTO evil_message (recv_eid,send_eid,send_alias,time,unread,title,message) SELECT eid,%d,'%s',%ld,1,'%s','%s' FROM evil_ranking WHERE rank>=%d"

int add_rank_reward_message(MYSQL **pconn, char *q_buffer, int start, int end, int send_eid, const char *alias, const char * title, const char * message)
{
	int ret;
	int len;

	if (end == 0) {
		len = sprintf(q_buffer, SQL_ADD_RANK_REWARD_MESSAGE2
		, send_eid, alias, time(NULL), title, message, start);
	} else {
		len = sprintf(q_buffer, SQL_ADD_RANK_REWARD_MESSAGE
		, send_eid, alias, time(NULL), title, message, start, end);
	}
	// DEBUG_PRINT(0, "add_rank_reward_message:query=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "add_rank_reward_message:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	ret = 0;

	return ret;
		
}

#define SQL_ADD_RANK_REWARD_MONEY "UPDATE evil_status SET gold=gold+%d, crystal=crystal+%d WHERE eid IN (SELECT eid FROM evil_ranking WHERE rank>=%d AND rank<=%d)"
#define SQL_ADD_RANK_REWARD_MONEY2 "UPDATE evil_status SET gold=gold+%d, crystal=crystal+%d WHERE eid IN (SELECT eid FROM evil_ranking WHERE rank>=%d)"

int add_rank_reward_money(MYSQL **pconn, char *q_buffer, int start, int end, int gold, int crystal)
{
	int ret;
	int len;

	if (end == 0) {
		len = sprintf(q_buffer, SQL_ADD_RANK_REWARD_MONEY2
		, gold, crystal, start);
	} else {
		len = sprintf(q_buffer, SQL_ADD_RANK_REWARD_MONEY
		, gold, crystal, start, end);
	}
	// DEBUG_PRINT(0, "add_rank_reward_money:query=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-5, "add_rank_reward_money:mysql_errno %d\n", mysql_errno(*pconn));
	}

	return 0;
}

int in_rank_reward(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int n;
	const char *ptr;

	int eid; // system eid
	char alias[EVIL_ALIAS_MAX + 5]; // system alias

	int start;
	int end;
	int gold;
	int crystal;
	char title[EVIL_MESSAGE_TITLE_MAX + 5];
	char message[EVIL_MESSAGE_MAX + 5];
	char esc_title[EVIL_MESSAGE_TITLE_MAX * 2 + 5];
	char esc_message[EVIL_MESSAGE_MAX * 2 + 5];

	ret = sscanf(in_buffer, "%d %s %n", &eid, alias, &n);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_rank_reward:invalid_input");
	}

	ptr = in_buffer + n;
	ret = sscanf(ptr, "%d %d %d %d %s %s %n"
	, &start, &end, &gold, &crystal, title, message, &n);
	if (ret != 6) {
		DB_ER_RETURN(-5, "in_rank_reward:invalid_input %d", ret);
	}
	// DEBUG_PRINT(0, "in_rank_reward:start=%d end=%d gold=%d crystal=%d title=%s message=%s", start, end, gold, crystal, title, message);

	mysql_real_escape_string(*pconn, esc_title, title, strlen(title));
	mysql_real_escape_string(*pconn, esc_message, message, strlen(message));

	// 1.update money
	ret = add_rank_reward_money(pconn, q_buffer, start, end, gold, crystal);
	ERROR_PRINT(ret, "in_rank_reward:add_money_error");

	// 2.add message
	ret = add_rank_reward_message(pconn, q_buffer, start, end, eid, alias, esc_title, esc_message);
	ERROR_PRINT(ret, "in_rank_reward:add_message_error");

	sprintf(out_buffer, "%d %d %d", ret, start, end);

	return ret;
}


#define SQL_UNREAD_MESSAGE_COUNT	"SELECT COUNT(*) FROM evil_message WHERE recv_eid=%d AND unread=1"

int get_unread_message_count(MYSQL **pconn, char *q_buffer, int eid)
{
	int ret;
	int len;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	len = sprintf(q_buffer, SQL_UNREAD_MESSAGE_COUNT, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "get_unread_message_count:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ret = -23;
		ERROR_RETURN(-23, "get_unread_message_count:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(-6, "get_unread_message_count:num_row<=0 %d", eid);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(-13, "get_unread_message_count:row_null %d", eid);
		goto cleanup;
	}

	ret = strtol_safe(row[0], -1);
	if (ret < 0) {
		ret = -16;
		ERROR_PRINT(-16, "get_unread_message_count:count_errro %d", eid);
		goto cleanup;
	}

	// DEBUG_PRINT(0, "get_unread_message_count:count=%d", ret);

cleanup:	// make sure ret is setup
	mysql_free_result(result);
	return ret;
}


#define SQL_UPDATE_SIGNALS		"UPDATE evil_status SET signals='%s' WHERE eid=%d"
int __update_signals(MYSQL **pconn, char *q_buffer, int eid, const char *signals)
{
	int ret;
	int len ;
	if (eid <= MAX_AI_EID) {
		return 0; // skip AI eid
	}
	len = sprintf(q_buffer, SQL_UPDATE_SIGNALS, signals, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		return -5;
	}

	ret = mysql_affected_rows(*pconn);
	if (ret != 1 && ret != 0) {
		BUG_PRINT(ret, "update_signals:eid[%d] signals[%s]", eid, signals);
		return -3;  // empty row for status
	}
	return 0;
}


int in_lottery(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;

	int n;
	int eid;
	int type;
	int gold;
	int crystal;
	int times;
	int card_id;
	int card_list[10];
	bzero(card_list, sizeof(card_list));
	char signals[EVIL_SIGNAL_MAX + 1];
	bzero(signals, sizeof(signals));
	const char *in_ptr;
	in_ptr = in_buffer;
	char *out_ptr;

	// DEBUG_PRINT(0, "in_lottery:in_buffer=%s", in_buffer);

	ret = sscanf(in_ptr, "%d %d %d %d %d %n", &eid, &type, &gold, &crystal, &times, &n);
	if (ret != 5) {
		DB_ER_RETURN(-5, "in_lottery:invalid_input %d", ret);
	}

	if ((gold > 0 || crystal > 0)
	|| (gold == 0 && crystal == 0)) {
		DB_ER_RETURN(-15, "in_lottery:invalid_money");
	}

	if (times != 1 && times != 10) {
		DB_ER_RETURN(-25, "in_lottery:invalid_times");
	}

	for (int i=0; i<times; i++) {
		in_ptr += n;
		ret = sscanf(in_ptr, "%d %n", &card_id, &n);
		if (ret != 1) {
			DB_ER_RETURN(-35, "in_lottery:invalid_input");
		} 

		if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
			DB_ER_RETURN(-45, "in_lottery:invalid_card_id");
		}

		card_list[i] = card_id;
	}

	in_ptr += n;
	ret = sscanf(in_ptr, "%30s", signals);
	if (ret != 1) {
		DB_ER_RETURN(-55, "in_lottery:invalid_input[%s][%s]", in_buffer, in_ptr);
	} 

	// 1.update money
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_lottery:update_money_error %d %d", gold, crystal);
	}

	// 2.update card
	for (int i=0; i<times; i++) {
		card_id = card_list[i];
		ret = update_card(pconn, q_buffer, eid, card_id, 1);
		if (ret < 0) {
			DB_ER_RETURN(-16, "in_lottery:update_card_error %d %d", eid, card_id);
		}

		if (ret == 222) {
			// TODO handle card count already == 9, no more card add
			WARN_PRINT(222, "in_lottery:update_card_9 %d", card_id);
		}
	}

	if (times == 1) {
		bool has_changed = false;
		if ((type == 1) && ((signals[SIGNAL_LOTTERY_GOLD_ONE] == '\0')
		|| (signals[SIGNAL_LOTTERY_GOLD_ONE] == '0'))) {
			signals[SIGNAL_LOTTERY_GOLD_ONE] = '1';
			has_changed = true;
		}
		if ((type == 2) && ((signals[SIGNAL_LOTTERY_CRYSTAL_ONE] == '\0')
		|| (signals[SIGNAL_LOTTERY_CRYSTAL_ONE] == '0'))) {
			signals[SIGNAL_LOTTERY_CRYSTAL_ONE] = '1';
			has_changed = true;
		}
		if (has_changed) {
			ret = __update_signals(pconn, q_buffer, eid, signals);
			WARN_PRINT(ret, "in_lottery:update_signals_fail %d %s", eid, signals);
		}
	}

	strncpy(out_buffer, in_buffer, 300);
	out_ptr = out_buffer + (in_ptr - in_buffer);
	sprintf(out_ptr, "%30s", signals);

	// DEBUG_PRINT(0, "in_lottery:out_buffer=%s", out_buffer);
	
	return 0;
}





//////////////////////////////////////////////

//#define SQL_GET_CHANNEL_GIFT	"SELECT * from ((SELECT * FROM evil_channel_gift WHERE key_code='%s') e LEFT JOIN (SELECT id, channel FROM design.design_gift) d ON e.gift_id=d.id) LIMIT 1"
#define SQL_GET_CHANNEL_GIFT	"SELECT gift_id,eid,UNIX_TIMESTAMP(end_time),channel,user_channel from ((SELECT * FROM evil_channel_gift WHERE key_code='%s') e LEFT JOIN (SELECT id, channel FROM design.design_gift) d ON e.gift_id=d.id LEFT JOIN (SELECT channel AS user_channel FROM evil_user WHERE eid=%d) u ON TRUE) LIMIT 1"
int __check_key_code_unused(MYSQL **pconn, char *q_buffer, char *out_buffer, int user_eid, char *key_code, int &gift_id)
{
	int ret;
	int len;
	int err;
	int eid;
	time_t end_time;
	int channel;
	int user_channel;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_GET_CHANNEL_GIFT, key_code, user_eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-5, "in_exchange_gift:mysql_errno %d\n", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ret = -3;
		DB_ER_RETURN(-3, "in_exchange_gift:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		DB_ER_PRINT(-6, "in_exchange_gift:key_code_not_exist");
		goto cleanup;
	}
	
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		DB_ER_PRINT(-13, "in_exchange_gift:row_null");
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// key_code, gift_id, eid, end_time, exchange_time, id, channel, user_channel
	if (field_count != 5) {
		ret = -7;
		DB_ER_PRINT(ret, "in_exchange_gift:field_count %d != 5", field_count);
		goto cleanup;
	}

	gift_id			= strtol_safe(row[0], -1);
	eid				= strtol_safe(row[1], 0);
	end_time		= strtolong_safe(row[2], 0);
	channel			= strtol_safe(row[3], 0);
	user_channel	= strtol_safe(row[4], 0);
	if (eid > 0)
	{
		ret = -16;
		DB_ER_PRINT(ret, "in_exchange_gift:key_code_has_been_used");
		goto cleanup;
	}
	if (end_time > 0 && end_time < time(NULL))
	{
		ret = -26;
		DB_ER_PRINT(ret, "in_exchange_gift:key_code_has_expired %ld", end_time);
		goto cleanup;
	}
	if (channel != 0 && user_channel != channel)
	{
		ret = -36;
		DB_ER_PRINT(ret, "in_exchange_gift:channel_not_fit");
		goto cleanup;
	}

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_CHECK_EXCHANGE_GIFT		"SELECT COUNT(*) FROM evil_channel_gift WHERE eid=%d AND gift_id IN (SELECT id FROM design.design_gift dg WHERE (dg.channel, dg.gtype) IN (SELECT channel, gtype FROM design.design_gift WHERE id=%d))"
int __check_has_exchange_gift(MYSQL **pconn, char *q_buffer, char *out_buffer, int eid, int gift_id)
{
	int ret;
	int len;
	int err;
	int num_row;

	MYSQL_RES *result;
	MYSQL_ROW row;

	len = sprintf(q_buffer, SQL_CHECK_EXCHANGE_GIFT, eid, gift_id);
//	printf("check_has_exchange_gift:q_buffer[%s] len[%d]\n", q_buffer, len);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-5, "in_exchange_gift:mysql_errno %d\n", err);
	}

//	ret = mysql_affected_rows(*pconn);
//	if (ret > 0)	// row > 0, means player has exchange same kind of key_code
//	{
//		ERROR_RETURN(-46, "__check_has_exchange_gift:player_has_exchange");
//	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ret = -3;
		DB_ER_RETURN(-3, "__check_has_exchange_gift:null_result");
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		DB_ER_PRINT(-13, "__check_has_exchange_gift:row_null");
		goto cleanup;
	}

	num_row = strtol_safe(row[0], 0); 
	if (num_row > 0)
	{
		DB_ER_RETURN(-46, "__check_has_exchange_gift:player_has_exchange");
	}


cleanup:
	mysql_free_result(result);
	return 0;
}


#define SQL_GIFT_INFO			"SELECT gold,crystal,ran_team_list FROM design.design_gift WHERE id=(SELECT gift_id FROM evil_channel_gift WHERE key_code='%s' LIMIT 1)"
int __get_design_gift_info(MYSQL **pconn, char *q_buffer, char *key_code, int &gold, int &crystal, char *ran_team_list)
{
	int ret;
	int len;
	int err;
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_GIFT_INFO, key_code);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "get_design_gift_info:mysql_errno %d\n", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ret = -3;
		ERROR_RETURN(-3, "get_design_gift_info:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -7;
		BUG_PRINT(-7, "get_design_gift_info:design_gift_not_exist key_code=%s"
		, key_code);
		goto cleanup;
	}
	
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -17;
		ERROR_PRINT(-17, "get_design_gift_info:row_null");
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// key_code, gift_id, eid, end_time, exchange_time
	if (field_count != 3) {
		ret = -27;
		ERROR_PRINT(ret, "get_design_gift_info:field_count %d != 3", field_count);
		goto cleanup;
	}

	gold	= strtol_safe(row[0], 0);
	crystal = strtol_safe(row[1], 0);
	sprintf(ran_team_list, "%s", row[2]);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_RATE_CARD_TEAM	"SELECT * FROM design.design_rate_card WHERE team_id=%d"
int __random_card_in_team(MYSQL **pconn, char *q_buffer, int team_id, int &card_id)
{
	int ret;
	int len;
	int err;
	int n;

	int weight_list[RANDOM_TEAM_MAX + 1];
	char range_list[RANDOM_TEAM_MAX + 1][1600];
	int count;
	int total_weight = 0;
	int weight_value;
	int team_pos;

	int card_list[EVIL_CARD_MAX];
	int card_count;
	int tmp_card;
	int card_pos;
	char *str_ptr;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_RATE_CARD_TEAM, team_id);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "random_card_in_team:mysql_errno %d\n", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ret = -3;
		ERROR_RETURN(-3, "random_card_in_team:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -7;
		BUG_PRINT(-7, "random_card_in_team:design_random_card_row_0 team_id=%d"
		, team_id);
		goto cleanup;
	}
	
	field_count = mysql_field_count(*pconn);
	// team_id, weight, range_list
	if (field_count != 3) {
		ret = -27;
		ERROR_PRINT(ret, "random_card_in_team:field_count %d != 3", field_count);
		goto cleanup;
	}

	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		weight_list[count] = strtol_safe(row[1], 0);
		sprintf(range_list[count], "%s", row[2]);

		total_weight += weight_list[count];
		count++;
	}
	if (total_weight <= 0)
	{
		ret = -37;
		ERROR_PRINT(ret, "random_card_in_team:total_weight_0 team_id=%d", team_id);
		goto cleanup;
	}

	// random card with rate logic
	// 1. random target team with rate
	weight_value = random() % total_weight;
	for (team_pos = 0; team_pos < count; team_pos++)
	{
		if (weight_value <= weight_list[team_pos])
		{
			break;
		}
		weight_value -= weight_list[team_pos];
	}
	if (team_pos >= count)
	{
		ret = -47;
		ERROR_PRINT(ret, "random_card_in_team:team_pos_outbound team_id=%d"
		, team_id);
		goto cleanup;
	}

	// 2. get random card in range_list
	card_count = 0;
	n = 0;
	str_ptr = range_list[team_pos];
//	printf("range_list[%d]=[%s]\n", team_pos, str_ptr);
	while((ret = sscanf(str_ptr, "%d %n", &tmp_card, &n)) == 1)
	{
		card_list[card_count++] = tmp_card;
		str_ptr += n;
	}

	card_pos = random() % card_count;
	card_id = card_list[card_pos];
//	printf("card_count[%d] card_pos[%d] card_id:[%d]\n"
//	, card_count, card_pos, card_id);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


int __add_gift_to_eid(MYSQL **pconn, char *q_buffer, char *out_buffer, int eid, char *key_code)
{
	int ret;
	int gold;
	int crystal;
	char ran_team_list[1024];
	int team_id;
	int card_id_list[EVIL_CARD_MAX];
	int card_id;
	int card_count;
	int n;
	char *str_ptr;
	char *out_ptr;

	ret = __get_design_gift_info(pconn, q_buffer, key_code, gold
	, crystal, ran_team_list);
	if (ret != 0)
	{
		ERROR_RETURN(-6, "add_gift_to_eid:get_design_gift_info_fail %d key_code=%s"
		, eid, key_code);
	}

	// @Notice actions bellow will not be blocked
	// add gold/crystal to eid
	if (gold > 0 || crystal > 0)
	{
		ret = update_money(pconn, q_buffer, eid, gold, crystal);
		if (ret != 0)
		{
			ERROR_PRINT(-16, "add_gift_to_eid:update_money_error eid=%d gold=%d crystal=%d", eid, gold, crystal);
		}
	}

	// add card to eid
	card_count = 0;
	if (strlen(ran_team_list) > 0)
	{
		n = 0;
		str_ptr = ran_team_list;
		while ((ret = sscanf(str_ptr, "%d %n", &team_id, &n)) == 1)
		{
			ret = __random_card_in_team(pconn, q_buffer, team_id, card_id);
			if (ret != 0)
			{
				ERROR_PRINT(-26
				, "add_gift_to_eid:random_card_to_eid eid=%d team_id=%d"
				, eid, team_id);
				break;
			}
			ret = update_card(pconn, q_buffer, eid, card_id, 1);
			if (ret != 0)
			{
				if (ret == 222)
				{
					WARN_PRINT(ret, "add_gift_to_eid:card_out_bound %d %d"
					, eid, card_id);
				} else {
					ERROR_PRINT(-36
					, "add_gift_to_eid:update_card_fail eid=%d card_id=%d"
					, eid, card_id);
					break;
				}
			}
			card_id_list[card_count++] = card_id;
			str_ptr += n;
		}
	}

	// 
	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, "%d %d %d %d", eid, gold, crystal, card_count);
	for (int i = 0; i < card_count; i++)
	{
		out_ptr += sprintf(out_ptr, " %d", card_id_list[i]);
	}

	return 0;
}

#define SQL_UPDATE_GIFT_KEYCODE		"UPDATE evil_channel_gift SET eid=%d,exchange_time=FROM_UNIXTIME(%ld) WHERE key_code='%s'"
int __set_key_code_used(MYSQL **pconn, char *q_buffer, char *out_buffer, int eid, char *key_code)
{
	// TODO
	int len;
	int err;
	int ret;

	if (eid <= 0) {
		ERROR_NEG_RETURN(-5, "__set_key_code_used:invalid_eid %d", eid);
		return -9;
	}

	if (strlen(key_code) == 0)
	{
		ERROR_RETURN(-15, "__set_key_code_used:key_code_invalid %d %s"
		, eid, key_code);
	}

	len = sprintf(q_buffer, SQL_UPDATE_GIFT_KEYCODE, eid, time(NULL), key_code);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-25, "__set_key_code_used:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_NEG_RETURN(-6, "__set_key_code_used:affected_row wrong %d", ret);
	}
	return 0;
}

int __exchange_gift(MYSQL **pconn, char *q_buffer, char *out_buffer, int eid, char *key_code)
{
	// will not whether check key_code has been used
	// 1. add gift's stuff to eid
	// 2. update evil_channel_gift to set key_code has been used
	int ret;

	ret = __add_gift_to_eid(pconn, q_buffer, out_buffer, eid, key_code);
	ERROR_RETURN(ret, "__exchange_gift:add_gift_to_eid_fail eid=%d key_code=%s"
	, eid, key_code);

	ret = __set_key_code_used(pconn, q_buffer, out_buffer, eid, key_code);
	ERROR_RETURN(ret, "__exchange_gift:set_key_code_used_fail eid=%d key_code=%s"
	, eid, key_code);

	return 0;
}


int in_exchange_gift(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;

	int eid;
	char key_code[35];
	int gift_id;

	ret = sscanf(in_buffer, IN_EXCHANGE_GIFT_SCAN, &eid, key_code);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_exchange_gift:invalid_input");
	}

	// confirm keycode has not used
//	sprintf(q_buffer, SQL_GET_CHANNEL_GIFT, key_code);
	ret = __check_key_code_unused(pconn, q_buffer, out_buffer
	, eid, key_code, gift_id);
	if (ret < 0)
	{
		ERROR_NEG_RETURN(ret, "in_exchange_gift:__check_key_code_unused");
	}

	// confirm eid has not accept other key_code in same gift_type
	ret = __check_has_exchange_gift(pconn, q_buffer, out_buffer, eid, gift_id);
	if (ret < 0)
	{
		ERROR_NEG_RETURN(ret, "in_exchange_gift:__check_has_exchange_gift");
	}

	// exchange gift by key_code
	ret = __exchange_gift(pconn, q_buffer, out_buffer, eid, key_code);
	if (ret != 0)
	{
		DB_ER_RETURN(-56, "in_exchange_gift:exchange_gift_fail eid=%d key_code=%s"
		, eid, key_code);
	}

//	sprintf(out_buffer, "%d %s %s %s %s %s %s %s %s", eid, row[0], row[1]
//	, row[2], row[3], row[4], row[5], row[6], row[7]);
	return 0;
}

//////////////////////////////////////////////

#define SQL_RESET_FIGHT_TIMES	"UPDATE evil_status SET fight_ai_time=%d,fight_gold_time=%d,fight_crystal_time=%d"
int in_reset_fight_times(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_RESET_FIGHT_TIMES, FIGHT_AI_MAX_TIME
	, FIGHT_GOLD_MAX_TIME, FIGHT_CRYSTAL_MAX_TIME);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-25, "in_reset_fight_times:mysql_errno %d\n", err);
	}
	sprintf(out_buffer, "0");
	return 0;
}

int in_fight(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int game_type;
	char tmp_buffer[3000];

	ret = sscanf(in_buffer, IN_FIGHT_LOAD_DECK_SCAN, &eid, &game_type);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_fight:invalid_input ret=%d", ret);
	}
	ret = in_load_deck(pconn, q_buffer, in_buffer, tmp_buffer);
	ERROR_RETURN(ret, "in_fight:in_load_deck_error");

//	printf("in_fight eid[%d] game_type[%d]\n", eid, game_type);
	sprintf(out_buffer, "%d %s", game_type, tmp_buffer);

	return 0;
}
//////////////////////////////////////////////

#define SQL_GATE "SELECT * FROM evil_deck WHERE eid=%d"

int in_gate(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int eid;
	int gate_id;
	int power_offset;
	int ret;
	int err;
	int len;
	int out_eid;
	char deck[EVIL_CARD_MAX+5];
	
	ret = sscanf(in_buffer, "%d %d %d", &eid, &gate_id, &power_offset);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_gate:invalid_input ret=%d", ret);
	}
	if (eid<=0) {
		DB_ER_RETURN(-15, "in_gate:invalid_eid eid=%d", eid);
	}
	if (power_offset > 0 || power_offset + MAX_USER_POWER < 0) {
		DB_ER_RETURN(-25, "in_gate:invalid_power_offset power_offset=%d", power_offset);
	}


	ret = update_power(pconn, q_buffer, eid, power_offset);
	if (ret < 0) {
		DB_ER_RETURN(-6, "in_gate:update_power_fail eid=%d power_offset=%d", eid, power_offset);
	}


	len = sprintf(q_buffer, SQL_GATE, eid);
	DB_ER_NEG_RETURN(len, "in_gate:sprintf");

	// printf("in_gate query: %s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "in_gate:query err %d", mysql_errno(*pconn));
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "in_gate:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid) + 1 (slot)
	if (field_count != EVIL_CARD_MAX + 1 + 1) {
		ret = -7;
		DB_ER_PRINT(ret, "in_gate:field_count %d != card_count+2 %d",
			field_count, EVIL_CARD_MAX+2);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	// normally 2 rows, for eid1 and eid2, possible 1 row for eid2 is AI
	// 0 is impossible, should be error
	if (num_row <= 0) {
		ret = -6;	 // logical error
		DB_ER_PRINT(ret, "in_gate:deck_not_enough row=%d %d", num_row, eid);
		goto cleanup;
	}


	// ok, we are going to get the first row
	// this is eid1, deck1
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		DB_ER_PRINT(ret, "in_gate:deck_null_row %d", eid);
		goto cleanup;
	}

	err = row_to_deck(row, &out_eid, deck, EVIL_NUM_DECK_MAX);
	if (err < 0) {
		ret = -17;
		DB_ER_PRINT(ret, "in_gate:row_to_deck %d", err);
		goto cleanup;
	}


	sprintf(out_buffer, "%d %d %400s", out_eid, gate_id, deck);
	ret = 0; // ok 

	// DEBUG_PRINT(0, "in_gate:out_buffer=%s", out_buffer);

cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_UPDATE_GATE_POS "UPDATE evil_status SET gate_pos=%d WHERE eid=%d and gate_pos<%d"
int in_update_gate_pos(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int err;
	int eid;
	int gate_pos;
	
	ret = sscanf(in_buffer, "%d %d", &eid, &gate_pos);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_update_gate_pos:invalid_input ret=%d", ret);
	}
	if (eid<=0) {
		DB_ER_RETURN(-15, "in_update_gate_pos:invalid_eid eid=%d", eid);
	}
	if (gate_pos <= 0) {
		DB_ER_RETURN(-25, "in_update_gate_pos:invalid_gate_pos=%d", gate_pos);
	}

	len = sprintf(q_buffer, SQL_UPDATE_GATE_POS, gate_pos, eid, gate_pos);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-5, "in_update_gate_pos:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1 && ret != 0) {
		DB_ER_RETURN(-6, "in_update_gate_pos:affected_row wrong %d", ret);
	}

	sprintf(out_buffer, "%d %d", eid, gate_pos);

	ret = 0;
	return ret;
}


#define SQL_GET_STATUS_TOWER "SELECT eid,tower_pos,tower_times,tower_set_time FROM evil_status WHERE eid=%d"
int get_status_tower(MYSQL **pconn, char *q_buffer, int eid, int *tower_pos, int *tower_times, long *tower_set_time)
{
	
	int ret;
	int len;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_GET_STATUS_TOWER, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "get_status_tower:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-23, "get_status_tower:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -7;
		ERROR_PRINT(ret, "get_status_tower:num_row<=0 %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	if (field_count != 4) {
		ret = -7;
		ERROR_PRINT(ret, "get_status_tower:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ERROR_PRINT(-13, "get_status_tower:row_null %d", eid);
		goto cleanup;
	}

	*tower_pos = strtol_safe(row[1], -1);
	if (*tower_pos < 0) {
		ret = -6;
		ERROR_PRINT(-6, "get_status_tower:tower_pos_error %d %d", eid, *tower_pos);
		goto cleanup;
	}
	*tower_times = strtol_safe(row[2], -1);
	if (*tower_times < 0) {
		ret = -16;
		ERROR_PRINT(-16, "get_status_tower:tower_times_error %d %d", eid, *tower_times);
		goto cleanup;
	}
	*tower_set_time = strtolong_safe(row[3], -1);
	if (*tower_set_time < 0) {
		ret = -26;
		ERROR_PRINT(-26, "get_status_tower:tower_set_time_error %d %ld", eid, *tower_set_time);
		goto cleanup;
	}

	DEBUG_PRINT(0, "get_status_tower:pos=%d times=%d set_time=%ld"
	, *tower_pos, *tower_times, *tower_set_time);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;

}

// #define SQL_GET_TOWER_LADDER "SELECT COUNT(*)+1 FROM evil_status WHERE (tower_pos>%d OR (tower_pos=%d AND tower_set_time<%ld)) AND tower_set_time!=0"
#define SQL_GET_TOWER_LADDER "SELECT * FROM (SELECT (SELECT COUNT(*)+1 FROM evil_status t2 WHERE (t1.tower_pos<t2.tower_pos OR (t1.tower_pos=t2.tower_pos AND t2.tower_set_time < t1.tower_set_time) AND t2.tower_set_time!=0)) AS pos, t1.eid, t3.alias, t3.icon, t1.lv, t1.tower_pos FROM evil_status t1 LEFT JOIN evil_user t3 ON t3.eid = t1.eid WHERE tower_set_time!= 0 AND tower_pos>0 ORDER BY t1.tower_pos DESC, t1.tower_set_time ASC) tt WHERE eid=%d"
int get_tower_ladder(MYSQL **pconn, char *q_buffer, int *index, int eid, int tower_pos, long tower_set_time)
{
	
	int ret;
	int len;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_GET_TOWER_LADDER, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "get_tower_ladder:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-23, "get_tower_ladder:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -7;
		ERROR_PRINT(ret, "get_tower_ladder:num_row<=0 %d", num_row);
		goto cleanup;
	}

	if (num_row==0) {
		// no index
		*index = 0;
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	if (field_count != 6) {
		ret = -7;
		ERROR_PRINT(ret, "get_tower_ladder:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ERROR_PRINT(-13, "get_tower_ladder:row_null");
		goto cleanup;
	}

	*index = strtol_safe(row[0], -1);
	if (*index < 0) {
		ret = -6;
		ERROR_PRINT(-6, "get_tower_ladder:index_error %d", *index);
		goto cleanup;
	}

	DEBUG_PRINT(0, "get_tower_ladder:index=%d", *index);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;

}

#define SQL_GET_EVIL_TOWER "SELECT eid,pos,hp_current,hp_offset,res_offset,energy_offset,buff_flag,deck FROM evil_tower WHERE eid=%d"
int get_evil_tower(MYSQL **pconn, char *q_buffer, int eid, int *pos, int *hp_current, int *hp_offset, int *res_offset, int *energy_offset, int *buff_flag, char *deck)
{
	
	int ret;
	int len;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_GET_EVIL_TOWER, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "get_evil_tower:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-23, "get_evil_tower:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row==0) {
		ret = 0;
		deck[0] = '\0';
		INFO_PRINT(0, "get_evil_tower:no_tower %d", eid);
		goto cleanup;
	}

	if (num_row<0) {
		ret = -7;
		ERROR_PRINT(ret, "get_evil_tower:num_row<=0 %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	if (field_count != 8) {
		ret = -7;
		ERROR_PRINT(ret, "get_evil_tower:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ERROR_PRINT(-13, "get_evil_tower:row_null %d", eid);
		goto cleanup;
	}

	*pos = strtol_safe(row[1], -1);
	if (*pos < 0) {
		ret = -6;
		ERROR_PRINT(-6, "get_evil_tower:pos_error %d %d", eid, *pos);
		goto cleanup;
	}

	*hp_current = strtol_safe(row[2], -1);
	if (*hp_current < 0) {
		ret = -6;
		ERROR_PRINT(-6, "get_evil_tower:hp_current_error %d %d", eid, *hp_current);
		goto cleanup;
	}

	*hp_offset = strtol_safe(row[3], 0);
	*res_offset = strtol_safe(row[4], 0);
	*energy_offset = strtol_safe(row[5], 0);

	*buff_flag = strtol_safe(row[6], -1);
	if (*buff_flag < 0) {
		ret = -6;
		ERROR_PRINT(-6, "get_evil_tower:buff_flag_error %d %d", eid, *buff_flag);
		goto cleanup;
	}

	sprintf(deck, "%.400s", row[7]);


	DEBUG_PRINT(0, "get_evil_tower:eid=%d pos=%d hp_current=%d hp_offset=%d res_offset=%d energy_offset=%d buff_flag=%d deck=%s"
	, eid, *pos, *hp_current, *hp_offset, *res_offset, *energy_offset, *buff_flag, deck);

	ret = 1;
cleanup:
	mysql_free_result(result);
	return ret;

}

#define SQL_UPDATE_STATUS_TOWER "UPDATE evil_status SET tower_pos=%d,tower_times=%d,tower_set_time=%ld WHERE eid=%d"
int update_status_tower(MYSQL **pconn, char *q_buffer, int eid, int tower_pos, int tower_times, long tower_set_time)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_UPDATE_STATUS_TOWER, tower_pos, tower_times, tower_set_time, eid);
	DEBUG_PRINT(0, "update_status_tower:q_buffer=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "update_status_tower:mysql_errno %d\n", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-6, "update_status_tower:affected_row wrong %d", ret);
	}

	return 0;
}

#define SQL_DECK "SELECT * FROM evil_deck WHERE eid=%d"
int get_deck(MYSQL **pconn, char *q_buffer, int eid, char *deck)
{
	int ret;
	int err;
	int len;
	int eid_inside;

	len = sprintf(q_buffer, SQL_DECK, eid);

	// printf("get_deck:query: %s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "get_deck:query err %d", mysql_errno(*pconn));
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_deck:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid) + 1 (slot)
	if (field_count != EVIL_CARD_MAX + 1 + 1) {
		ret = -7;
		ERROR_PRINT(ret, "get_deck:field_count %d != card_count+2 %d",
			field_count, EVIL_CARD_MAX+2);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	// normally 2 rows, for eid1 and eid2, possible 1 row for eid2 is AI
	// 0 is impossible, should be error
	if (num_row <= 0) {
		ret = -6;	 // logical error
		ERROR_PRINT(ret, "get_deck:deck_not_enough row=%d %d", num_row, eid);
		goto cleanup;
	}

	// ok, we are going to get the first row
	// this is eid1, deck1
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(ret, "get_deck:deck_null_row %d", eid);
		goto cleanup;
	}

	err = row_to_deck(row, &eid_inside, deck, EVIL_NUM_DECK_MAX);
	if (err < 0) {
		ret = -17;
		ERROR_PRINT(ret, "get_deck:row_to_deck %d", err);
		goto cleanup;
	}

	// DEBUG_PRINT(0, "get_deck:eid=%d deck=%s", eid_inside, deck);

	ret = 0; // ok 

cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_REPLACE_EVIL_TOWER "REPLACE INTO evil_tower VALUES (%d,%d,%d,%d,%d,%d,%d,'%.400s')"
int replace_evil_tower(MYSQL **pconn, char *q_buffer, int eid, int pos, int hp_current, int hp_offset, int res_offset, int energy_offset, int buff_flag, char *deck)
{
	int ret;
	int len;
	int err;

	ret = get_deck(pconn, q_buffer, eid, deck);
	if (ret != 0) {
		ERROR_RETURN(-6, "replace_evil_tower:get_deck_fail %d\n", eid);
	}

	len = sprintf(q_buffer, SQL_REPLACE_EVIL_TOWER, eid, pos, hp_current, hp_offset, res_offset, energy_offset, buff_flag, deck);
	DEBUG_PRINT(0, "replace_evil_tower:q_buffer=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "replace_evil_tower:mysql_errno %d\n", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1 && ret != 2) {
		ERROR_RETURN(-6, "replace_evil_tower:affected_row wrong %d", ret);
	}

	return 0;
}

#define SQL_DELETE_EVIL_TOWER 	"DELETE FROM evil_tower WHERE eid=%d"
int delete_evil_tower(MYSQL **pconn, char *q_buffer, int eid)
{
	int len;
	int err;
	int ret;


	len = sprintf(q_buffer, SQL_DELETE_EVIL_TOWER, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-55, "delete_evil_tower:mysql_errno %d  eid=%d", err, eid);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		// impossible
		ERROR_RETURN(-7, "delete_evil_tower:sql_affected_rows wrong %d  eid=%d"
		, ret, eid);
	}

	ret = 0;
	return ret;
}

int in_tower(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int eid;
	int tower_index;
	int ret;

	int tower_pos;
	int tower_times;
	long tower_set_time;

	int pos = 0;
	int hp_current = 0;
	int hp_offset = 0;
	int res_offset = 0;
	int energy_offset = 0;
	int buff_flag = 0;
	char deck[EVIL_CARD_MAX+1];
	
	ret = sscanf(in_buffer, "%d %d", &eid, &tower_index);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_tower:invalid_input ret=%d", ret);
	}
	if (eid<=0) {
		DB_ER_RETURN(-15, "in_tower:invalid_eid eid=%d", eid);
	}
	if (tower_index <= 0) {
		DB_ER_RETURN(-25, "in_tower:invalid_tower tower=%d", tower_index);
	}

	ret = get_status_tower(pconn, q_buffer, eid, &tower_pos, &tower_times, &tower_set_time);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_tower:get_status_tower_bug eid=%d", eid);
	}

	ret = get_evil_tower(pconn, q_buffer, eid, &pos, &hp_current, &hp_offset, &res_offset, &energy_offset, &buff_flag, deck);
	if (ret != 0 && ret != 1) {
		DB_ER_RETURN(-26, "in_tower:get_evil_tower_bug eid=%d", eid);
	}

	if (tower_times <= 0 && ret == 0) {
		DB_ER_RETURN(-16, "in_tower:no_more_tower_times eid=%d %d", eid, tower_times);
	}

	if (tower_index != pos+1) {
		DB_ER_RETURN(-66, "in_tower:tower_pos_mismatch %d %d", pos, tower_index);
	}

	if (ret == 0) {
		tower_set_time = time(NULL);
		tower_times -= 1;
		// update when use tower_times to start a new round or in win a round
		ret = update_status_tower(pconn, q_buffer, eid, tower_pos, tower_times, tower_set_time);
		if (ret != 0) {
			DB_ER_RETURN(-46, "in_tower:update_status_tower_fail eid=%d", eid);
		}
	}
	// insert a new evil_tower / update buff_flag
	ret = replace_evil_tower(pconn, q_buffer, eid, pos, hp_current, hp_offset, res_offset, energy_offset, buff_flag, deck);
	if (ret != 0) {
		DB_ER_RETURN(-36, "in_tower:replace_tower_fail eid=%d", eid);
	}

	INFO_PRINT(0, "in_tower:eid=%d tower_index=%d tower_pos=%d tower_times=%d tower_set_time=%ld pos=%d hp_current=%d hp_offset=%d res_offset=%d energy_offset=%d buff_flag=%d deck=%s"
	, eid, tower_index, tower_pos, tower_times, tower_set_time
	, pos, hp_current, hp_offset, res_offset, energy_offset, buff_flag, deck);

	sprintf(out_buffer, "%d %d %d %d %d %d %.400s"
	, eid, tower_index, hp_current, hp_offset, res_offset, energy_offset, deck);

	ret = 0; // ok 
	DEBUG_PRINT(0, "in_tower:out_buffer=%s", out_buffer);

	return ret;
}


// update hp_current hp_offset
int in_tower_result(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int eid;
	int tower_index;
	int win_flag;
	int hp_real;
	int ret;

	int tower_pos;
	int tower_times;
	long tower_set_time;

	int pos = 0;
	int hp_current;
	int hp_offset;
	int res_offset;
	int energy_offset;
	int buff_flag;
	char deck[EVIL_CARD_MAX+1];
	
	ret = sscanf(in_buffer, "%d %d %d %d", &eid, &tower_index, &win_flag, &hp_real);
	if (ret != 4) {
		DB_ER_RETURN(-5, "in_tower_result:invalid_input ret=%d", ret);
	}
	if (eid<=0) {
		DB_ER_RETURN(-15, "in_tower_result:invalid_eid eid=%d", eid);
	}
	if (tower_index <= 0) {
		DB_ER_RETURN(-25, "in_tower_result:invalid_tower tower=%d", tower_index);
	}
	if (win_flag != 0 && win_flag != 1) {
		DB_ER_RETURN(-35, "in_tower_result:invalid_win_flag win_flag=%d", win_flag);
	}

	// delete evil_result, return
	if (win_flag == 0) {
		ret = delete_evil_tower(pconn, q_buffer, eid);
		if (ret != 0) {
			DB_ER_RETURN(-6, "in_tower_result:delete_evil_tower_fail %d", eid);
		}
		sprintf(out_buffer, "%d", 0);
		return 0;
	}

	ret = get_evil_tower(pconn, q_buffer, eid, &pos, &hp_current, &hp_offset, &res_offset, &energy_offset, &buff_flag, deck);
	if (ret != 0 && ret != 1) {
		DB_ER_RETURN(-16, "in_tower_result:get_evil_tower_bug eid=%d", eid);
	}

	if (ret == 0) {
		// no evil_tower, may because clean by auto daily reset
		WARN_PRINT(0, "in_tower_result:null_evil_tower %d", eid);
		sprintf(out_buffer, "%d", 0);
		return 0;
	}

	if (pos+1 != tower_index) {
		DB_ER_RETURN(-26, "in_tower_result:invalid_tower_pos %d %d", pos, tower_index);
	}

	pos += 1;
	// if (pos >= 10) {
	if (pos >= 3) {
		hp_offset -= 1;
	}
	// XXX assert hero hp will not bigger than 50, out_tower should handler it, let hero hp >= 1
	if (hp_offset < -50) {
		hp_offset = -50;
	}

	// update buff
	if (pos % 5 == 0) {
		buff_flag = 1;
	}

	// change hp_current to hp_real
	ret = replace_evil_tower(pconn, q_buffer, eid, pos, hp_real, hp_offset, res_offset, energy_offset, buff_flag, deck);
	if (ret != 0) {
		DB_ER_RETURN(-36, "in_tower_result:replace_tower_fail eid=%d", eid);
	}

	ret = get_status_tower(pconn, q_buffer, eid, &tower_pos, &tower_times, &tower_set_time);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_tower_result:get_status_tower_bug eid=%d", eid);
	}

	/*
	if (tower_pos >= pos) {
		// history pos is bigger, do nothing
		sprintf(out_buffer, "%d", 0);
		return 0;
	}
	*/
	tower_pos ++;

	tower_set_time = time(NULL);
	// update when use tower_times to start a new round or in win a round
	ret = update_status_tower(pconn, q_buffer, eid, tower_pos, tower_times, tower_set_time);
	if (ret != 0) {
		DB_ER_RETURN(-46, "in_tower_result:update_status_tower_fail eid=%d", eid);
	}

	sprintf(out_buffer, "%d", 0);
	
	return 0;	
}

int in_tower_buff(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int eid;
	int hp_buff;
	int res_buff;
	int energy_buff;
	int ret;

	int pos = 0;
	int hp_current = 0;
	int hp_offset = 0;
	int res_offset = 0;
	int energy_offset = 0;
	int buff_flag = 0;
	char deck[EVIL_CARD_MAX+1];
	deck[0] = '\0';
	
	ret = sscanf(in_buffer, "%d %d %d %d", &eid, &hp_buff, &res_buff, &energy_buff);
	if (ret != 4) {
		DB_ER_RETURN(-5, "in_tower_buff:invalid_input ret=%d", ret);
	}
	if (eid<=0) {
		DB_ER_RETURN(-15, "in_tower_buff:invalid_eid eid=%d", eid);
	}

	ret = get_evil_tower(pconn, q_buffer, eid, &pos, &hp_current, &hp_offset, &res_offset, &energy_offset, &buff_flag, deck);
	if (ret != 1) {
		DB_ER_RETURN(-26, "in_tower_buff:get_evil_tower_bug eid=%d", eid);
	}

	if (buff_flag != 1) {
		DB_ER_RETURN(-36, "in_tower_buff:no_buff %d %d", eid, pos);
	}

	if (res_buff + res_offset > 5) {
		DB_ER_RETURN(-46, "in_tower_buff:res_buff_max %d %d %d", eid, res_buff, res_offset);
	}

	if (energy_buff + energy_offset > 5) {
		DB_ER_RETURN(-56, "in_tower_buff:energy_buff_max %d %d %d", eid, energy_buff, energy_offset);
	}

	hp_current += hp_buff;
	res_offset += res_buff;
	energy_offset += energy_buff;
	buff_flag = 0;

	ret = replace_evil_tower(pconn, q_buffer, eid, pos, hp_current, hp_offset, res_offset, energy_offset, buff_flag, deck);
	if (ret != 0) {
		DB_ER_RETURN(-36, "in_tower_buff:replace_tower_fail eid=%d", eid);
	}

	sprintf(out_buffer, "%d %d %d %d %d %d %d %.400s"
	, eid, pos, hp_current, hp_offset, res_offset, energy_offset, buff_flag
	, deck);

	return 0;	
}

int in_tower_info(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int eid;
	int ret;

	int tower_pos;
	int tower_times;
	long tower_set_time;

	int pos = 0;
	int hp_current = 0;
	int hp_offset = 0;
	int res_offset = 0;
	int energy_offset = 0;
	int buff_flag = 0;
	char deck[EVIL_CARD_MAX+1];
	deck[0] = '\0';
	
	ret = sscanf(in_buffer, "%d", &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "in_tower_info:invalid_input ret=%d", ret);
	}
	if (eid<=0) {
		DB_ER_RETURN(-15, "in_tower_info:invalid_eid eid=%d", eid);
	}

	ret = get_status_tower(pconn, q_buffer, eid, &tower_pos, &tower_times, &tower_set_time);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_tower_info:get_status_tower_bug eid=%d", eid);
	}

	ret = get_evil_tower(pconn, q_buffer, eid, &pos, &hp_current, &hp_offset, &res_offset, &energy_offset, &buff_flag, deck);
	if (ret != 0 && ret != 1) {
		DB_ER_RETURN(-26, "in_tower_info:get_evil_tower_bug eid=%d", eid);
	}

	sprintf(out_buffer, "%d %d %d %ld %d %d %d %d %d %d %.400s"
	, eid, tower_pos, tower_times, tower_set_time
	, pos, hp_current, hp_offset, res_offset, energy_offset, buff_flag
	, deck);

	return 0;	
}

#define SQL_TOWER_LADDER "SELECT (SELECT COUNT(*)+1 FROM evil_status t2 WHERE (t1.tower_pos<t2.tower_pos OR (t1.tower_pos=t2.tower_pos AND t2.tower_set_time < t1.tower_set_time) AND t2.tower_set_time!=0)) AS pos, t1.eid, t3.alias, t3.icon, t1.lv, t1.tower_pos FROM evil_status t1 LEFT JOIN evil_user t3 ON t3.eid = t1.eid WHERE tower_set_time!= 0 AND tower_pos>0 ORDER BY t1.tower_pos DESC, t1.tower_set_time ASC LIMIT 10"
int in_tower_ladder(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int eid;
	int ret;
	int len;
	int count;
	char *ptr;
	ptr = out_buffer;

	int index;
	int tower_pos;
	int tower_times;
	long tower_set_time;


	ret = sscanf(in_buffer, "%d", &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "in_tower_ladder:invalid_input ret=%d", ret);
	}


	ret = get_status_tower(pconn, q_buffer, eid, &tower_pos, &tower_times, &tower_set_time);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_tower_ladder:get_status_tower_bug eid=%d", eid);
	}

	index = 0;
	ret = get_tower_ladder(pconn, q_buffer, &index, eid, tower_pos, tower_set_time);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_tower_ladder:get_tower_ladder_bug eid=%d", eid);
	}
	DEBUG_PRINT(0, "in_tower_ladder:index=%d, tower_pos=%d, tower_set_time=%ld"
	, index, tower_pos, tower_set_time);
	ptr += sprintf(ptr, "%d %d", eid, index);



	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;
	len = sprintf(q_buffer, SQL_TOWER_LADDER);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "in_tower_ladder:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-23, "in_tower_ladder:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		sprintf(out_buffer, "0");
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	if (field_count != 6) {
		ret = -7;
		DB_ER_PRINT(ret, "in_tower_ladder:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	ptr += sprintf(ptr, " %d", num_row);

	count = 0;	
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-7, "in_tower_ladder:fetch_row_overflow %d", count);
			break;
		}

		ptr += sprintf(ptr, " %s %s %s %s %s %s"
		, row[0], row[1], row[2], row[3], row[4], row[5]);
	}

	ret = 0;
	DEBUG_PRINT(0, "in_tower_ladder:out_buffer=%s", out_buffer);

cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_UPDATE_BATTLE_COIN "UPDATE evil_status SET battle_coin=battle_coin+%d WHERE eid IN (SELECT tt.eid from (SELECT (SELECT COUNT(*)+1 FROM evil_status t2 WHERE (t1.tower_pos<t2.tower_pos OR (t1.tower_pos=t2.tower_pos AND t2.tower_set_time < t1.tower_set_time) AND t2.tower_set_time!=0)) AS pos, t1.eid, t3.alias, t3.icon, t1.lv, t1.tower_pos FROM evil_status t1 LEFT JOIN evil_user t3 ON t3.eid = t1.eid WHERE tower_set_time!= 0 AND tower_pos>0 ORDER BY t1.tower_pos DESC, t1.tower_set_time ASC) tt WHERE tt.pos>=%d AND tt.pos<= %d)"

#define SQL_UPDATE_BATTLE_COIN2 "UPDATE evil_status SET battle_coin=battle_coin+%d WHERE eid IN (SELECT tt.eid from (SELECT (SELECT COUNT(*)+1 FROM evil_status t2 WHERE (t1.tower_pos<t2.tower_pos OR (t1.tower_pos=t2.tower_pos AND t2.tower_set_time < t1.tower_set_time) AND t2.tower_set_time!=0)) AS pos, t1.eid, t3.alias, t3.icon, t1.lv, t1.tower_pos FROM evil_status t1 LEFT JOIN evil_user t3 ON t3.eid = t1.eid WHERE tower_set_time!= 0 AND tower_pos>0 ORDER BY t1.tower_pos DESC, t1.tower_set_time ASC) tt WHERE tt.pos>=%d)"
int update_battle_coin(MYSQL **pconn, char *q_buffer, int start_pos, int end_pos, int battle_coin)
{
	int ret;
	int len;
	int err;

	if (end_pos != 0) {
		len = sprintf(q_buffer, SQL_UPDATE_BATTLE_COIN, battle_coin, start_pos, end_pos);
	} else {
		len = sprintf(q_buffer, SQL_UPDATE_BATTLE_COIN2, battle_coin, start_pos);
	}
	DEBUG_PRINT(0, "update_status_tower:q_buffer=%s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "update_status_tower:mysql_errno %d\n", err);
	}

	return 0;
}

int in_tower_reward(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;

	int start_pos;
	int end_pos;
	int battle_coin;

	ret = sscanf(in_buffer, "%d %d %d"
	, &start_pos, &end_pos, &battle_coin);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_tower_reward:invalid_input %d", ret);
	}

	// update battle_coin
	ret = update_battle_coin(pconn, q_buffer, start_pos, end_pos, battle_coin);
	ERROR_PRINT(ret, "in_tower_reward:add_battle_coin_error");

	ret = 0;	
	sprintf(out_buffer, "%d", ret);

	return ret;
}

#define SQL_GET_SOLO_POS "SELECT eid, solo_pos FROM evil_status WHERE eid=%d"
int get_solo_pos(MYSQL **pconn, char *q_buffer, int eid)
{
	int ret = 0;
	int len;
	int solo_pos;
	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_GET_SOLO_POS, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-6, "get_solo_pos:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ret = -3;
		ERROR_RETURN(-3, "get_solo_pos:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -13;
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	if (field_count != 2) {
		ret = -7;
		ERROR_PRINT(ret, "get_solo_pos:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = 23;
		ERROR_PRINT(-23, "get_solo_pos:row_null %d", eid);
		goto cleanup;
	}

	ret = strtol_safe(row[0], -6);
	if (ret != eid) {
		ERROR_PRINT(-17, "get_solo_pos:eid_mismatch %d %d", ret, eid);
		ret = 17;
		goto cleanup;
	}

	solo_pos = strtol_safe(row[1], -6);
	if (solo_pos <= 0 || solo_pos > MAX_AI_EID) {
		ret = 26;
		BUG_PRINT(-26, "get_solo_pos:solo_pos_bug %d %d", solo_pos, eid);
		goto cleanup;
	}

	ret = solo_pos;

cleanup:
	mysql_free_result(result);
	return ret;
}


// check solo_pos and get deck
int in_solo(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	
	int ret;
	int eid;
	int solo_pos;
	char deck[EVIL_CARD_MAX+1];
	// int status_solo_pos;

	ret = sscanf(in_buffer, "%d %d", &eid, &solo_pos);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_solo:invalid_input ret=%d", ret);
	}

	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_solo:invalid_eid %d", eid);
	}

	if (solo_pos <= 0 || solo_pos > MAX_AI_EID) {
		DB_ER_RETURN(-25, "in_solo:invalid_solo_pos %d", solo_pos);
	}

	/*
	status_solo_pos = get_solo_pos(pconn, q_buffer, eid);
	if (status_solo_pos <= 0) {
		DB_ER_RETURN(-6, "in_solo:status_solo_pos_bug %d %d", status_solo_pos, eid);
	}

	if (status_solo_pos < solo_pos) {
		DB_ER_RETURN(-16, "in_solo:solo_pos_out_bound %d %d", status_solo_pos, solo_pos);
	}
	*/

	ret = get_deck(pconn, q_buffer, eid, deck);
	if (ret != 0) {
		DB_ER_RETURN(-26, "in_solo:get_deck_fail %d", eid);
	}

	sprintf(out_buffer, "%d %d %.400s", eid, solo_pos, deck);
	return 0;
}

// get deck for fight
int in_fight_robot(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	
	int ret;
	int game_type;
	int eid1;
	int eid2;
	char deck[EVIL_CARD_MAX+1];

	ret = sscanf(in_buffer, "%d %d %d", &game_type, &eid1, &eid2);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_fight_robot:invalid_input ret=%d", ret);
	}

	if (eid1 <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_fight_robot:invalid_eid %d", eid1);
	}

	ret = get_deck(pconn, q_buffer, eid1, deck);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_fight_robot:get_deck_fail %d", eid1);
	}

	sprintf(out_buffer, "%d %d %d %.400s", game_type, eid1, eid2, deck);
	return 0;
}


int in_update_signals(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;

	int eid;
	char signals[EVIL_SIGNAL_MAX + 1];
	bzero(signals, sizeof(signals));

	// DEBUG_PRINT(0, "in_update_signals:in_buffer=%s", in_buffer);

	ret = sscanf(in_buffer, "%d %30s", &eid, signals);
	if (ret != 2) {
		DB_ER_RETURN(-55, "in_update_signals:invalid_input");
	} 
	if (eid <= MAX_AI_EID)
	{
		sprintf(out_buffer, "0");
		WARN_PRINT(-5, "in_update_signals:eid<=MAX_AI_EID %d", eid);
		return 0;
	}

	signals[EVIL_SIGNAL_MAX] = '\0';

	if (strlen(signals) != EVIL_SIGNAL_MAX) {
		DB_ER_RETURN(-65, "in_update_signals:invalid_signals");
	}

	ret = __update_signals(pconn, q_buffer, eid, signals);
	if (ret < 0) {
		DB_ER_RETURN(ret, "in_update_signals:update_signals_fail %d %s", eid, signals);
	}

	sprintf(out_buffer, "%d %s", eid, signals);

	// DEBUG_PRINT(0, "in_update_signals:out_buffer=%s", out_buffer);
	
	return 0;
}

/*
// old logic
#define SQL_GET_CHAPTER_DATA "SELECT * FROM evil_chapter WHERE eid=%d AND chapter_id=%d"
int get_chapter_data(MYSQL **pconn, char * q_buffer, int eid, int chapter_id, char *data)
{
	int ret;
	int len;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	data[0] = '\0';

	len = sprintf(q_buffer, SQL_GET_CHAPTER_DATA, eid, chapter_id);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "get_chapter_data:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-23, "get_chapter_data:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -7;
		ERROR_PRINT(ret, "get_chapter_data:num_row<=0 %d", num_row);
		goto cleanup;
	}

	if (num_row==0) {
		// no chapter data, its ok
		ret = 0;
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// eid, chapter_id, data
	if (field_count != 3) {
		ret = -7;
		ERROR_PRINT(ret, "get_chapter_data:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ERROR_PRINT(-13, "get_chapter_data:row_null %d", eid);
		goto cleanup;
	}

	sprintf(data, "%.50s", row[2]);

	// DEBUG_PRINT(0, "get_chapter_data:eid=%d chapter_id=%d data=%s", eid, chapter_id, data);

	ret = 1;
cleanup:
	mysql_free_result(result);
	return ret;
}
*/

/*
// old logic
// consider: VALUES (%d, %d, '%.50s')
#define SQL_INSERT_CHAPTER_DATA "INSERT INTO evil_chapter VALUES (%d,%d,'%s')"
int insert_chapter_data(MYSQL **pconn, char *q_buffer, int eid, int chapter_id, const char * data)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_INSERT_CHAPTER_DATA, eid, chapter_id, data);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "insert_chapter_data:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-6, "insert_chapter_data:affected_row wrong %d", ret);
	}
	
	return 0;
}
*/

/*
// old logic
#define SQL_SAVE_CHAPTER_DATA "UPDATE evil_chapter SET data='%s' WHERE eid=%d AND chapter_id=%d"
int save_chapter_data(MYSQL **pconn, char *q_buffer, int eid, int chapter_id, const char * data)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_SAVE_CHAPTER_DATA, data, eid, chapter_id);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "save_chapter_data:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-6, "save_chapter_data:affected_row wrong %d", ret);
	}
	
	return 0;
}
*/

int in_chapter(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{

	int ret;
	int eid;
	int chapter_id;
	int stage_id;
	int power_offset;
	int solo_id;
	int stage_size;

	// char data[MAX_CHAPTER_STAGE+1];
	char deck[EVIL_CARD_MAX+1];

	ret = sscanf(in_buffer, "%d %d %d %d %d %d", &eid, &chapter_id, &stage_id, &power_offset, &solo_id, &stage_size);
	if (ret != 6) {
		DB_ER_RETURN(-55, "in_chapter:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-65, "in_chapter:invalid_eid");
	}
	if (chapter_id <= 0 || chapter_id >= MAX_CHAPTER) {
		DB_ER_RETURN(-65, "in_chapter:invalid_chapter_id");
	}
	if (stage_size > MAX_CHAPTER_STAGE) {
		BUG_PRINT(-6, "in_chapter:stage_bug %d %d", chapter_id, stage_size);
		DB_ER_RETURN(-75, "in_chapter:invalid_stage_size");
	}

	// DEBUG_PRINT(0, "in_chapter:eid=%d chapter_id=%d stage_id=%d solo_id=%d stage_size=%d", eid, chapter_id, stage_id, solo_id, stage_size);

	ret = update_power(pconn, q_buffer, eid, power_offset);
	if (ret < 0) {
		DB_ER_RETURN(-6, "in_chapter:update_power_fail eid=%d power_offset=%d", eid, power_offset);
	}

	/*
	// old logic, do in nio
	ret = get_chapter_data(pconn, q_buffer, eid, chapter_id, data);
	if (ret != 0 && ret != 1) {
		DB_ER_RETURN(-6, "in_chapter:get_chapter_data_error %d %d", eid, chapter_id);
	}

	if (ret == 0 && chapter_id == 1) {
		// init first chapter data
		memset(data, CHAPTER_DATA_LOCK, sizeof(char)*stage_size); 
		memset(data, CHAPTER_DATA_START, 1);
		data[stage_size] = '\0';
		DEBUG_PRINT(0, "in_chapter:data1=%s", data);
		ret = insert_chapter_data(pconn, q_buffer, eid, chapter_id, data);
		DB_ER_RETURN(ret, "in_chapter:insert_chapter_data_error %d %d", eid, chapter_id);
	}

	if (ret == 0 && chapter_id != 1) {
		// init chapter 
		memset(data, CHAPTER_DATA_LOCK, sizeof(char)*stage_size);
		data[stage_size] = '\0';
		DEBUG_PRINT(0, "in_chapter:data2=%s", data);
	}

	if (data[stage_id-1] == CHAPTER_DATA_LOCK) {
		DB_ER_RETURN(-25, "in_chapter:stage_not_ready %d %d %d", eid, chapter_id, stage_id);
	}
	*/

	ret = get_deck(pconn, q_buffer, eid, deck);
	if (ret != 0) {
		DB_ER_RETURN(-26, "in_chapter:get_deck_fail %d", eid);
	}

	sprintf(out_buffer, "%d %d %d %d %.400s", eid, chapter_id, stage_id, solo_id, deck);
	return 0;
}

/*
// old logic
// note: init_flag not work when data is not empty
int reset_chapter_data(int chapter_id, int stage_size, int init_flag, char *data)
{
	int ret; // ret=0 means no data change, ret=1 means data change

	if (stage_size > MAX_CHAPTER_STAGE) {
		ret = -2;
		BUG_PRINT(ret, "reset_chapter_data:stage_size_out_bound %d", stage_size);
		return ret;
	}

	int len;
	len = strlen(data);

	// empty data
	// 1.if init_flag == 1, fill with [8999...9]
	// 2.if init_flag == 0, fill with [9999...9]
	if (0 == len) {
		memset(data, CHAPTER_DATA_LOCK, sizeof(char)*stage_size);
		if (1 == init_flag) {
			data[0] = CHAPTER_DATA_START;
		}
		data[stage_size] = '\0';
		// DEBUG_PRINT(0, "reset_chapter_data:empty_behind_data=[%s]", data);
		ret = 1;
		return ret;
	}

	// not empty data
	// 1.check if len match stage_size
	// 2.if match, return 0
	// 3.if stage_size < len, set data[stage_size]='\0'
	// 4.if stage_size > len, add '9' at last
	if (stage_size == len) {
		// DEBUG_PRINT(0, "reset_chapter_data:match_data=[%s]", data);
		ret = 0;
		return ret;
	}

	if (stage_size < len) {
		data[stage_size] = '\0';
		// DEBUG_PRINT(0, "reset_chapter_data:over_data=[%s]", data);
		ret = 1;
		return ret;
	}

	if (stage_size > len) {
		int offset = 0;
		offset = stage_size - len;
		memset(data+len, CHAPTER_DATA_LOCK, sizeof(char)*offset);
		data[stage_size] = '\0';
		// DEBUG_PRINT(0, "reset_chapter_data:less_data=[%s]", data);
		ret = 1;
		return ret;
	}

	ret = -6;
	BUG_PRINT(ret, "reset_chapter_data:something_wrong %d %d %d %s", chapter_id, stage_size, init_flag, data);
	return ret;
}
*/

/*
// old logic
int in_chapter_data(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{

	int ret;
	int eid;
	int chapter_id;
	int stage_size;

	char data[MAX_CHAPTER_STAGE+1];
	data[0] = '\0'; // must init data[0]

	ret = sscanf(in_buffer, "%d %d %d", &eid, &chapter_id, &stage_size);
	if (ret != 3) {
		DB_ER_RETURN(-55, "in_chapter_data:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-65, "in_chapter_data:invalid_eid");
	}
	if (chapter_id <= 0 || chapter_id >= MAX_CHAPTER) {
		DB_ER_RETURN(-65, "in_chapter_data:invalid_chapter_id");
	}
	if (stage_size > MAX_CHAPTER_STAGE) {
		DB_ER_RETURN(-66, "in_chapter_data:invalid_stage_size %d %d", chapter_id, stage_size);
	}

	// DEBUG_PRINT(0, "in_chapter_data:eid=%d chapter_id=%d stage_size=%d", eid, chapter_id, stage_size);

	// 1.get player chapter data
	// 2.reset player chapter data to match with stage_size
	// 3.insert OR update OR no change chapter data

	// 1.get player chapter data
	int fill_data = 0;
	fill_data = get_chapter_data(pconn, q_buffer, eid, chapter_id, data);
	if (fill_data != 0 && fill_data != 1) {
		DB_ER_RETURN(-6, "in_chapter_data:get_chapter_data_error %d %d", eid, chapter_id);
	}

	// 2.reset player chapter data to match with stage_size
	int change = 0;
	int init_flag = 0;
	if (chapter_id == 1) {
		init_flag = 1;
	}
	change = reset_chapter_data(chapter_id, stage_size, init_flag, data);
	if (change != 0 && change != 1) {
		DB_ER_RETURN(-16, "in_chapter_data:reset_chapter_data_error %d %d", eid, chapter_id);
	}

	DEBUG_PRINT(0, "in_chapter_data:reset_data=[%s]", data);

	// 3.
	if (fill_data == 0) {
		// data is empty, data must be reset in reset_chapter_data()
		// a. if chapter_id==1, insert into db
		// b. if chapter_id!=1, no need to insert
		if (chapter_id == 1) {
			ret = insert_chapter_data(pconn, q_buffer, eid, chapter_id, data);
			DB_ER_RETURN(ret, "in_chapter_data:insert_chapter_data_error %d %d", eid, chapter_id);
		}
		sprintf(out_buffer, "%d %d %s", eid, chapter_id, data);
		return 0;
	}

	// now, origin data is not empty, should update data in db if data change after reset_chapter_data()
	if (change == 1) {
		// update into db
		ret = save_chapter_data(pconn, q_buffer, eid, chapter_id, data);
		BUG_PRINT(ret, "in_chapter_data:save_chapter_data_bug1 %d %d %s", eid, chapter_id, data);
	}
	sprintf(out_buffer, "%d %d %s", eid, chapter_id, data);
	return 0;

}
*/


typedef struct {
	int id;
	char data[MAX_CHAPTER_STAGE + 1];
} db_chapter_t;

#define SQL_GET_CHAPTER "SELECT * FROM evil_chapter WHERE eid=%d ORDER BY chapter_id"
int get_chapter(MYSQL **pconn, char * q_buffer, int eid, db_chapter_t *chapter_list)
{
	int ret;
	int len;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	int chapter_id;
	db_chapter_t *chapter;

	len = sprintf(q_buffer, SQL_GET_CHAPTER, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "get_chapter:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-23, "get_chapter:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -7;
		ERROR_PRINT(ret, "get_chapter:num_row<=0 %d", num_row);
		goto cleanup;
	}

	if (num_row==0) {
		// no chapter data, its ok
		ret = 0;
		goto cleanup;
	}

	if (num_row > MAX_CHAPTER) {
		ret = -17;
		ERROR_PRINT(ret, "get_chapter:num_row_out_bound %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// eid, chapter_id, data
	if (field_count != 3) {
		ret = -7;
		ERROR_PRINT(ret, "get_chapter:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		chapter_id = strtol_safe(row[1], -6);
		if (chapter_id > MAX_CHAPTER || chapter_id <= 0) {
			ret = 26;
			BUG_PRINT(-26, "get_chapter:chapter_id %d", eid);
			goto cleanup;
		}
		chapter = chapter_list + chapter_id;
		chapter->id = chapter_id;
		sprintf(chapter->data, "%.20s", row[2]);
		if (strlen(chapter->data) == 0) {
			sprintf(chapter->data, "9");
		}

	}

	ret = num_row;
cleanup:
	mysql_free_result(result);
	return ret;
}

int in_get_chapter(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{

	int ret;
	int eid;
	int chapter_id; // only for nio
	int stage_id; // only for nio
	int star; // only for nio
	int size;

	char *ptr;
	db_chapter_t chapter_list[MAX_CHAPTER+1];
	bzero(chapter_list, sizeof(chapter_list));

	ret = sscanf(in_buffer, "%d %d %d %d", &eid, &chapter_id, &stage_id, &star);
	if (ret != 4) {
		DB_ER_RETURN(-55, "in_get_chapter:invalid_input");
	} 

	// get all chapter
	ret = get_chapter(pconn, q_buffer, eid, chapter_list);
	if (ret < 0) {
		DB_ER_RETURN(ret, "in_get_chapter:get_chapter %d", eid);
	}
	size = ret;
	if (size == 0) {
		sprintf(out_buffer, "%d %d %d %d %d", eid, chapter_id, stage_id, star, size);
	}
	// DEBUG_PRINT(0, "in_get_chapter:size=%d", size);

	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d %d %d %d", eid, chapter_id, stage_id, star, size);
	for (int i=1; i<MAX_CHAPTER+1; i++) {
		db_chapter_t *chapter = chapter_list + i;			
		if (chapter->id == 0) { continue; }
		ptr += sprintf(ptr, " %d %.20s", chapter->id, chapter->data);
	}

	// out_buffer = [eid] [chapter_id] [stage_id] [star] [size] [chapter1] [chapter2] ...
	// chapter = [id] [data]

	return 0;
}

#define SQL_REPLACE_CHAPTER_PREFIX "REPLACE INTO evil_chapter VALUES"
// VALUES(%d,%d,'%s')"
int __query_replace_chapter(const char* prefix, char* query, int query_max, int eid, int size, const char *in_buffer)
{
	int ret;
	char *end;
	const char * in;
	int n;
	in = in_buffer;

	if (prefix==NULL) {
		return -13;
	}

	char * const query_end_max = query + query_max - 10;

	end = query;  // make it like recursive
	end = stpcpy(end, prefix);

	int chapter_id;
	char data[MAX_CHAPTER_STAGE+1];
	for (int i=0; i<size; i++) {
		ret = sscanf(in, "%d %20s %n", &chapter_id, data, &n);
		if (ret != 2) {
			ERROR_RETURN(-55, "__query_replace_chapter:invalid_input");
		} 
		in += n;
		// DEBUG_PRINT(0, "__query_replace_chapter:chapter_id=%d data=%s", chapter_id, data);
		end += sprintf(end, "(%d,%d,'%s')", eid, chapter_id, data);

		if (i != size - 1) {
			*end = ','; 
			end++;
		}

		if (end > query_end_max) {
			ERROR_PRINT(-22, "__query_replace_chapter:overflow %d i=%d", query_max, i);
			return -22;
		}
	}
	*end = '\0'; 		//null term but not ++, MUST HAVE!

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}

int in_replace_chapter(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{

	int ret;
	int len;
	int err;
	int eid;
	int size;
	int n;
	const char *in;
	// int chapter_id;
	// char data[MAX_CHAPTER_STAGE+1];

	in = in_buffer;
	ret = sscanf(in, "%d %d %n", &eid, &size, &n);
	if (ret != 2) {
		DB_ER_RETURN(-55, "in_replace_chapter:invalid_input");
	} 
	// DEBUG_PRINT(0, "in_replace_chapter:eid=%d size=%d", eid, size);

	in += n;
		
	len = __query_replace_chapter(SQL_REPLACE_CHAPTER_PREFIX, q_buffer, QUERY_MAX, eid, size, in);
	// DEBUG_PRINT(0, "in_replace_chapter:q_buffer=[%s] len=%d", q_buffer, len);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-5, "in_replace_chapter:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret == 0) {
		DB_ER_RETURN(-6, "in_replace_chapter:affected_row wrong %d", ret);
	}

	sprintf(out_buffer, "%s", in_buffer);
	// out_buffer = [eid] [size] [chapter1] [chapter2] ...
	// chapter = [id] [data]

	return 0;
}

/*
// old logic
int in_chapter_update_data(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{

	int ret;

	int change = 0;
	int now_star;

	int eid;
	int chapter_id;
	int stage_id;
	int in_star;
	int stage_size;
	int next_stage_size;

	int init_flag = 0;

	char data[MAX_CHAPTER_STAGE+1];

	ret = sscanf(in_buffer, "%d %d %d %d %d %d", &eid, &chapter_id, &stage_id, &in_star, &stage_size, &next_stage_size);
	if (ret != 6) {
		DB_ER_RETURN(-55, "in_chapter_update_data:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-65, "in_chapter_update_data:invalid_eid");
	}
	if (chapter_id <= 0 || chapter_id >= MAX_CHAPTER) {
		DB_ER_RETURN(-65, "in_chapter_update_data:invalid_chapter_id");
	}
	if (stage_id <= 0 || stage_id >= MAX_CHAPTER) {
		DB_ER_RETURN(-85, "in_chapter_update_data:invalid_stage_id");
	}
	if (in_star != CHAPTER_DATA_STATUS_STAR_0 
	&& in_star != CHAPTER_DATA_STATUS_STAR_1 
	&& in_star != CHAPTER_DATA_STATUS_STAR_2 
	&& in_star != CHAPTER_DATA_STATUS_STAR_3) {
		DB_ER_RETURN(-95, "in_chapter_update_data:invalid_status");
	}
	if (stage_size > MAX_CHAPTER_STAGE) {
		DB_ER_RETURN(-56, "in_chapter_update_data:invalid_stage_size %d %d", chapter_id, stage_size);
	}
	if (stage_id > stage_size) {
		DB_ER_RETURN(-66, "in_chapter_update_data:stage_id_out_bound %d %d", stage_id, stage_size);
	}

	// DEBUG_PRINT(0, "in_chapter_update_data:eid=%d chapter_id=%d stage_id=%d stage_size=%d next_stage_size%d", eid, chapter_id, stage_id, stage_size, next_stage_size);

	// 1.get now chapter data
	// 2.update chapter data
	// 3.check next chapter data, update it
	int fill_data = 0;
	fill_data = get_chapter_data(pconn, q_buffer, eid, chapter_id, data);
	if (fill_data == 0) {
		BUG_PRINT(-3, "in_chapter_update_data:get_chapter_data_empty %d %d", eid, chapter_id);
		DB_ER_RETURN(-3, "in_chapter_update_data:get_chapter_data_empty %d %d", eid, chapter_id);
	}
	if (fill_data != 1) {
		DB_ER_RETURN(-6, "in_chapter_update_data:get_chapter_data_error %d %d", eid, chapter_id);
	}

	init_flag = 0;
	ret = reset_chapter_data(chapter_id, stage_size, init_flag, data);
	// DEBUG_PRINT(0, "in_chapter_update_data:reset_data=%s", data);


	// 2.update chapter data
	// data is base 0
	now_star = data[stage_id-1] - CHAPTER_DATA_STAR_0;
	// INFO_PRINT(0, "in_chapter_udpate_data:now_star=%d", now_star);
	if (now_star != CHAPTER_DATA_STATUS_STAR_0 
	&& now_star != CHAPTER_DATA_STATUS_STAR_1 
	&& now_star != CHAPTER_DATA_STATUS_STAR_2 
	&& now_star != CHAPTER_DATA_STATUS_STAR_3 
	&& now_star != CHAPTER_DATA_STATUS_START 
	&& now_star != CHAPTER_DATA_STATUS_LOCK) {
		BUG_PRINT(-16, "in_chapter_update_data:invalid_now_star %d", now_star);
		DB_ER_RETURN(-16, "in_chapter_update_data:invalid_stage_play %d %d %d", eid, chapter_id, stage_id);
	}
	if (now_star == CHAPTER_DATA_STATUS_LOCK) {
		DB_ER_RETURN(-26, "in_chapter_update_data:invalid_stage_play %d %d %d", eid, chapter_id, stage_id);
	}

	change = 0;
	// if next stage is lock, unlock it
	if (stage_id < stage_size && data[stage_id] == CHAPTER_DATA_LOCK) {
		data[stage_id] = CHAPTER_DATA_START;
		change = 1;
	}

	// 3->1, early exit
	// 1->1, early exit
	// 0->2, ok
	// 8->0, ok
	// 8->1, ok
	if (now_star < in_star || now_star == CHAPTER_DATA_STATUS_START) {
		data[stage_id-1] = CHAPTER_DATA_STAR_0 + in_star;
		change = 1;
	}

	if (change == 0) {
		// no need to change data, early exit	
		sprintf(out_buffer, "%d %d %d %d %s", eid, chapter_id, stage_id
		, stage_size, data);
		return 0;
	}

	ret = save_chapter_data(pconn, q_buffer, eid, chapter_id, data);
	BUG_PRINT(ret, "in_chapter_update_data:save_chapter_data_bug %d %d %s", eid, chapter_id, data);

	// 3.check next chapter data, update it
	// check if need to init next chapter
	if (stage_id < stage_size) {
		// no need to update next chapter data, early exit	
		sprintf(out_buffer, "%d %d %d %d %s", eid, chapter_id, stage_id
		, stage_size, data);
		return 0;
	}

	if (next_stage_size == 0) {
		// no next chapter, early exit	
		sprintf(out_buffer, "%d %d %d %d %s", eid, chapter_id, stage_id
		, stage_size, data);
		return 0;
	}

	data[0] = '\0';
	int next_chapter_id = chapter_id+1;
	ret = get_chapter_data(pconn, q_buffer, eid, next_chapter_id, data);
	if (ret == 1) {
		// already init next chapter, early exit	
		sprintf(out_buffer, "%d %d %d %d %s", eid, chapter_id, stage_id
		, stage_size, data);
		return 0;
	}
	if (ret != 0) {
		BUG_PRINT(-36, "in_chapter_update_data:get_next_chapter_data_fail %d %d", eid, chapter_id+1);
	}

	init_flag = 1;
	reset_chapter_data(next_chapter_id, next_stage_size, init_flag, data); // init data[0]
	
	DEBUG_PRINT(0, "in_chapter_update_data:next_chapter_data:%s", data);

	ret = insert_chapter_data(pconn, q_buffer, eid, next_chapter_id, data);
	BUG_PRINT(ret, "in_chapter_update_data:insert_chapter_data_bug %d %d %s", eid, next_chapter_id, data);

	// ret = update_chapter_pos(pconn, q_buffer, eid, next_chapter_id);
	// BUG_PRINT(ret, "in_chapter_update_data:update_chapter_pos_bug %d %d %s", eid, next_chapter_id, data);

	// send new chapter_pos to nio
	sprintf(out_buffer, "%d %d %d %d %s", eid, next_chapter_id, stage_id
	, stage_size, data);
	return 0;
}
*/

#define SQL_UPDATE_LV_EXP "UPDATE evil_status SET lv=%d,exp=%d WHERE eid=%d"
int update_lv_exp(MYSQL **pconn, char *q_buffer, int eid, int lv, int exp)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_UPDATE_LV_EXP, lv, exp, eid);

	DEBUG_PRINT(len, "update_lv_exp:g_query = %s\n", q_buffer);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret!=0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-15, "update_lv_exp:mysql_errno %d", err);
		return -15;
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret <= 0) {
		ERROR_NEG_RETURN(-16, "update_lv_exp:update_error");
	}

	if (ret != 1) {
		ERROR_NEG_RETURN(-7, "update_lv_exp:impossible_error %d", ret);
	}

	ret = 0;

	return ret;
}

int in_chapter_reward(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{

	int ret;
	int eid;
	int chapter_id;
	int stage_id;
	int reward;
	int count;
	int ext1; // for reward_exp lv
	int ext2; // for reward_exp exp

	ret = sscanf(in_buffer, "%d %d %d %d %d %d %d", &eid, &chapter_id, &stage_id, &reward, &count, &ext1, &ext2);
	if (ret != 7) {
		DB_ER_RETURN(-55, "in_chapter_reward:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-65, "in_chapter_reward:invalid_eid");
	}

	// DEBUG_PRINT(0, "in_chapter_reward:eid=%d chapter_id=%d stage_id=%d reward=%d count=%d ext1=%d ext2=%d", eid, chapter_id, stage_id, reward, count, ext1, ext2);

	switch (reward) {
	case CHAPTER_REWARD_GOLD:
		ret = update_money(pconn, q_buffer, eid, count, 0);
		if (ret < 0) {
			DB_ER_RETURN(-6, "in_chapter_reward:update_money_gold %d %d %d", eid, reward, count);
		}
		break;
	case CHAPTER_REWARD_CRYSTAL:
		update_money(pconn, q_buffer, eid, 0, count);
		if (ret < 0) {
			DB_ER_RETURN(-6, "in_chapter_reward:update_money_crystal %d %d %d", eid, reward, count);
		}
		break;
	case CHAPTER_REWARD_PIECE:
		// count is card id
		update_piece(pconn, q_buffer, eid, count, 1);
		if (ret < 0) {
			DB_ER_RETURN(-6, "in_chapter_reward:update_piece %d %d %d", eid, reward, count);
		}
		break;
	case CHAPTER_REWARD_CARD:
		// count is card id
		ret = update_card(pconn, q_buffer, eid, count, 1);
		if (ret < 0) {
			DB_ER_RETURN(-6, "in_chapter_reward:update_card %d %d %d", eid, reward, count);
		}
		break;
	case CHAPTER_REWARD_EXP:
		ret = update_lv_exp(pconn, q_buffer, eid, ext1, ext2);
		if (ret < 0) {
			DB_ER_RETURN(-6, "in_chapter_reward:update_lv_exp %d %d %d", eid, ext1, ext2);
		}
		break;
	case CHAPTER_REWARD_POWER:
		break;
	default:
		break;
	}

	sprintf(out_buffer, "%d %d %d %d %d %d %d", eid, chapter_id, stage_id, reward, count, ext1, ext2);
	return 0;
}


#define SQL_GET_CARD "SELECT * FROM evil_card WHERE eid=%d"
int get_card(MYSQL **pconn, char *q_buffer, int eid, char *card)
{
	int ret;
	int err;
	int len;
	int eid_inside;

	len = sprintf(q_buffer, SQL_GET_CARD, eid);

	// printf("get_card:query: %s", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "get_card:query err %d", mysql_errno(*pconn));
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_card:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid)
	if (field_count != EVIL_CARD_MAX + 1) {
		ret = -7;
		ERROR_PRINT(ret, "get_card:field_count %d != card_count+2 %d",
			field_count, EVIL_CARD_MAX+2);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	// normally 2 rows, for eid1 and eid2, possible 1 row for eid2 is AI
	// 0 is impossible, should be error
	if (num_row <= 0) {
		ret = -6;	 // logical error
		ERROR_PRINT(ret, "get_card:deck_not_enough row=%d %d", num_row, eid);
		goto cleanup;
	}

	// ok, we are going to get the first row
	// this is eid1, deck1
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(ret, "get_card:deck_null_row %d", eid);
		goto cleanup;
	}

	err = row_to_card(row, &eid_inside, card, EVIL_NUM_CARD_MAX);
	if (err < 0) {
		ret = -17;
		ERROR_PRINT(ret, "get_card:row_to_deck %d", err);
		goto cleanup;
	}

//	DEBUG_PRINT(0, "get_card:eid=%d deck=%s", eid_inside, card);

	ret = 0; // ok 

cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_GET_MISSION_HERO "SELECT * FROM evil_hero WHERE eid=%d AND hero_id=%d"
int get_mission_hero(MYSQL **pconn, char * q_buffer, int eid, int hero_id, int &hp, int &energy)
{
	int ret;
	int len;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_GET_MISSION_HERO, eid, hero_id);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "get_mission_hero:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-23, "get_mission_hero:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -7;
		ERROR_PRINT(ret, "get_mission_hero:num_row<=0 %d", num_row);
		goto cleanup;
	}

	if (num_row==0) {		// not get hero, or hero_id not exists
		ret = -6;
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// eid, hero_id, hp, energy
	if (field_count != 4) {
		ret = -17;
		ERROR_PRINT(ret, "get_mission_hero:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ERROR_PRINT(-13, "get_mission_hero:row_null %d", eid);
		goto cleanup;
	}

	hp = strtol_safe(row[2], -6);
	if (hp <= 0) {
		ret = 26;
		BUG_PRINT(-26, "get_mission_hero:hp_bug %d %d", eid, hero_id);
		goto cleanup;
	}

	energy = strtol_safe(row[3], -6);
	if (energy <= 0) {
		ret = 26;
		BUG_PRINT(-26, "get_mission_hero:energy_bug %d %d", eid, hero_id);
		goto cleanup;
	}

	DEBUG_PRINT(0, "get_mission_hero:eid=%d hero_id=%d hp=%d energy=%d"
	, eid, hero_id, hp, energy);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_LIST_MISSION_HERO "SELECT * FROM evil_hero WHERE eid=%d ORDER BY hero_id ASC"
int list_mission_hero(MYSQL **pconn, char * q_buffer, int eid, evil_hero_t * hero_list)
{
	int ret;
	int len;

	int hero_id;
	evil_hero_t *hero;
	int count;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_LIST_MISSION_HERO, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "list_mission_hero:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-23, "list_mission_hero:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -7;
		ERROR_PRINT(ret, "list_mission_hero:num_row<=0 %d", num_row);
		goto cleanup;
	}

	if (num_row==0) {
		ret = 0;
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// eid, hero_id, hp, energy
	if (field_count != 4) {
		ret = -7;
		ERROR_PRINT(ret, "list_mission_hero:field_count_mismatch %d", field_count);
		goto cleanup;
	}

	count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		hero_id = strtol_safe(row[1], -6);
		if (hero_id <= 0 || hero_id > HERO_MAX) {
			ret = 26;
			BUG_PRINT(-26, "list_mission_hero:hero_id_bug %d", eid);
			goto cleanup;
		}

		hero = hero_list + hero_id;
		hero->hero_id = hero_id;

		hero->hp = strtol_safe(row[2], -6);
		if (hero->hp <= 0) {
			ret = 26;
			BUG_PRINT(-26, "list_mission_hero:hp_bug %d %d", eid, hero_id);
			goto cleanup;
		}

		hero->energy = strtol_safe(row[3], -6);
		if (hero->energy <= 0) {
			ret = 26;
			BUG_PRINT(-26, "list_mission_hero:energy_bug %d %d", eid, hero_id);
			goto cleanup;
		}
		++count;


//		DEBUG_PRINT(0, "list_mission_hero:eid=%d hero_id=%d hp=%d energy=%d"
//		, eid, hero->hero_id, hero->hp, hero->energy);
	}

	ret = count;
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_SET_MISSION_HERO "UPDATE evil_hero SET hp=%d,energy=%d WHERE eid=%d AND hero_id=%d"
int __set_mission_hero(MYSQL **pconn, char *q_buffer, int eid, int hero_id, int hp, int energy)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_SET_MISSION_HERO, hp, energy, eid, hero_id);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "set_mission_hero:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-6, "set_mission_hero:affected_row wrong %d", ret);
	}
	
	return 0;
}

#define SQL_UPDATE_MISSION_HERO "UPDATE evil_hero SET hp=hp+%d,energy=energy+%d WHERE eid=%d AND hero_id=%d"
int __update_mission_hero(MYSQL **pconn, char *q_buffer, int eid, int hero_id, int add_hp, int add_energy)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_UPDATE_MISSION_HERO, add_hp, add_energy
	, eid, hero_id);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "update_mission_hero:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-6, "update_mission_hero:affected_row wrong %d", ret);
	}
	
	return 0;
}


#define SQL_INSERT_MISSION_HERO "INSERT INTO evil_hero VALUES (%d,%d,%d,%d)"
int insert_mission_hero(MYSQL **pconn, char *q_buffer, int eid, int hero_id, int hp, int energy)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_INSERT_MISSION_HERO, eid, hero_id, hp, energy);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "insert_mission_hero:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-6, "insert_mission_hero:affected_row wrong %d", ret);
	}
	
	return 0;
}

#define SQL_REFRESH_HERO_MISSION	"INSERT INTO evil_hero_mission (SELECT %d,%d,mission_id,%d,0 FROM evil_design.design_hero_mission WHERE hero_id=%d AND (pre=0 OR pre IN (SELECT mission_id FROM evil_hero_mission WHERE eid=%d AND hero_id=%d AND status=3)) AND (%d,hero_id,mission_id) NOT IN (SELECT eid,hero_id,mission_id FROM evil_hero_mission))"
int __refresh_hero_mission(MYSQL **pconn, char *q_buffer, int eid, int hero_id)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_REFRESH_HERO_MISSION, eid, hero_id
	, MISSION_STATUS_READY, hero_id, eid, hero_id, eid);
	
//	DEBUG_PRINT(1, "refresh_hero_mission_sql[%s]", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "refresh_hero_mission:mysql_errno %d", err);
	}

	return 0;
}

int __init_design_hero_list(const char* buffer, evil_hero_t *design_hero_list) {
	int ret;
	int n;
	int hp;
	int energy;
	const char *ptr;

//	DEBUG_PRINT(1, "init_design_hero_list:buffer[%s]", buffer);
	ptr = buffer;
	for (int i=0; i<HERO_MAX; i++) {
		ret = sscanf(ptr, "%d %d %n", &hp, &energy, &n);
		if (ret != 2) {
			ret = -75;
			ERROR_RETURN(ret, "init_design_hero_list:invalid_design_hero_list");
			return ret;
		} 
//		DEBUG_PRINT(0, "pre_set_hero_list[%d]=%d / %d / %d", i+1, i+1, hp, energy);

		ptr += n;
		if (hp == 0 && energy == 0) {
			design_hero_list[i+1].hero_id = 0;
			design_hero_list[i+1].hp = 0;
			design_hero_list[i+1].energy = 0;
			continue;
		}
		design_hero_list[i+1].hero_id = i+1;
		design_hero_list[i+1].hp = hp;
		design_hero_list[i+1].energy = energy;

//		DEBUG_PRINT(0, "design_hero_list[%d]=%d / %d / %d", i+1, i+1, hp, energy);
	}

	return 0;
}

int __init_hero_by_card_list(MYSQL **pconn, char* q_buffer, int eid, evil_hero_t *hero_list, evil_hero_t *design_hero_list) {
	int ret;
	int count;

	char card_list[EVIL_CARD_MAX+1];
	bzero(card_list, sizeof(card_list));
	ret = get_card(pconn, q_buffer, eid, card_list);
	ERROR_RETURN(ret, "init_hero_by_card_list:get_mission_hero");

	ret = list_mission_hero(pconn, q_buffer, eid, hero_list);
	ERROR_NEG_RETURN(ret, "init_hero_by_card_list:list_mission_hero");
	count = ret;

	char c;
	evil_hero_t * hero;
	evil_hero_t * dhero;
	for (int i=0; i<HERO_MAX; i++) {
		c = card_list[i];	
		if (c <= '0') {
			continue;
		}
		hero = hero_list + i + 1; // hero_list base 1	
		if (hero->hero_id == 0) {
			DEBUG_PRINT(0, "init_hero_by_card_list:insert_hero %d", i+1);
			dhero = design_hero_list + i + 1;
			DEBUG_PRINT(0, "init_hero_by_card_list:dhero %d %d %d"
			, dhero->hero_id, dhero->hp, dhero->energy);
			if (dhero->hero_id == 0) {
				ERROR_PRINT(-6
				, "init_hero_by_card_list:no_such_design_hero %d", i+1);
				continue;
			}
			++count;

			// need to init new hero
			ret = insert_mission_hero(pconn, q_buffer, eid, dhero->hero_id
			, dhero->hp, dhero->energy);
			BUG_PRINT(ret
			, "init_hero_by_card_list:insert_mission_hero_fail %d %d"
			, eid, dhero->hero_id);

			hero->hero_id = dhero->hero_id;
			hero->hp = dhero->hp;
			hero->energy = dhero->energy;
		}

		ret = __refresh_hero_mission(pconn, q_buffer, eid, hero->hero_id);
		BUG_PRINT(ret, "init_hero_by_card_list:refresh_hero_mission_fail %d %d"
		, eid, dhero->hero_id);
	}

	return count;
}

int in_get_mission_hero(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int n;
	int eid;
	int count;
	char *out;
	evil_hero_t design_hero_list[HERO_MAX+1];
	evil_hero_t hero_list[HERO_MAX+1];

	ret = sscanf(in_buffer, "%d %n", &eid, &n);
	if (ret != 1) {
		DB_ER_RETURN(-55, "in_get_mission_hero:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-65, "in_get_mission_hero:invalid_eid");
	}

	bzero(design_hero_list, sizeof(design_hero_list));
	ret = __init_design_hero_list(in_buffer + n, design_hero_list);
	DB_ER_RETURN(ret, "in_get_mission_hero:init_design_hero_list_fail");

	bzero(hero_list, sizeof(hero_list));
	ret = __init_hero_by_card_list(pconn, q_buffer, eid, hero_list
	, design_hero_list);
	DB_ER_NEG_RETURN(ret, "in_get_mission_hero:init_hero_by_card_list_fail");

	count = ret;
	evil_hero_t *hero;
	out = out_buffer;
	out += sprintf(out, "%d %d", eid, count);
	for (int i=0;i<HERO_MAX;i++) {
		hero = hero_list+i+1;
		if (hero->hero_id == 0) {
			continue;
		}
		out += sprintf(out, " %d %d %d", hero->hero_id, hero->hp, hero->energy);
	}
	DEBUG_PRINT(0, "in_get_mission_hero:out_buffer=%s", out_buffer);

	return 0;
}

//#define SQL_GET_HERO_MISSION "SELECT e.*,d.p1 FROM evil_hero_mission e LEFT JOIN design.design_hero_mission d ON e.hero_id=d.hero_id AND e.mission_id=d.mission_id WHERE e.eid=%d AND e.hero_id=%d AND e.status<%d ORDER BY e.mission_id ASC"
#define SQL_GET_HERO_MISSION "SELECT * FROM evil_hero_mission WHERE eid=%d AND hero_id=%d ORDER BY mission_id ASC"
int get_hero_mission(MYSQL **pconn, char * q_buffer, int eid, int hero_id, evil_hero_mission_t *mission_list)
{
	int ret;
	int len;

	int mission_id;
	int status;
	int n1;
	evil_hero_mission_t *mission;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;
	int real_row_count;

	len = sprintf(q_buffer, SQL_GET_HERO_MISSION, eid, hero_id);
//	, MISSION_STATUS_FINISH);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "get_hero_mission:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-23, "get_hero_mission:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -7;
		ERROR_PRINT(ret, "get_hero_mission:num_row<=0 %d", num_row);
		goto cleanup;
	}

	if (num_row==0) {
		ret = 0;
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// eid, hero_id, mission_id, status, n1
	if (field_count != 5) {
		ret = -7;
		ERROR_PRINT(ret, "get_hero_mission:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	real_row_count = 0;
	while ( NULL != (row = mysql_fetch_row(result)) ) {

		mission_id = strtol_safe(row[2], -6);
		if (mission_id <= 0) {
			ret = -26;
			BUG_PRINT(-26, "get_hero_mission:mission_id_bug %d %d", eid, hero_id);
			goto cleanup;
		}

		mission = mission_list + mission_id;
		mission->mission_id = mission_id;
		mission->hero_id = hero_id;

		status = strtol_safe(row[3], -6);
		if (status < 0) {
			ret = -36;
			BUG_PRINT(-36, "get_hero_mission:status_bug %d %d", eid, hero_id);
			goto cleanup;
		}
		mission->status = status;

		n1 = strtol_safe(row[4], -6);
		if (n1 < 0) {
			ret = -46;
			BUG_PRINT(-46, "get_hero_mission:n1_bug %d %d", eid, hero_id);
			goto cleanup;
		}
		mission->n1 = n1;

		real_row_count++;

//		DEBUG_PRINT(0
//		, "get_hero_mission:eid=%d hero_id=%d mission_id=%d status=%d n1=%d"
//		, eid, mission->hero_id, mission->mission_id
//		, mission->status, mission->n1);
	}

	ret = real_row_count;
cleanup:
	mysql_free_result(result);
	return ret;
}

int in_get_hero_mission(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int hero_id;
	char *ptr;
	evil_hero_mission_t *mission;

	ret = sscanf(in_buffer, "%d %d", &eid, &hero_id);
	if (ret != 2) {
		DB_ER_RETURN(-55, "in_get_hero_mission:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-65, "in_get_hero_mission:invalid_eid");
	}
	if (hero_id <= 0 || hero_id > HERO_MAX) {
		DB_ER_RETURN(-75, "in_get_hero_mission:invalid_hero_id");
	}

	evil_hero_mission_t mission_list[MAX_HERO_MISSION+1];
	bzero(mission_list, sizeof(mission_list));
	ret = get_hero_mission(pconn, q_buffer, eid, hero_id, mission_list);
	DB_ER_NEG_RETURN(ret, "in_get_hero_mission:get_hero_mission_fail");

	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d %d", eid, hero_id, ret);
	for (int i=0;i<=MAX_HERO_MISSION;i++) {
		mission = mission_list + i;
		if (mission->mission_id == 0) {
			continue;
		}
		ptr += sprintf(ptr, " %d %d %d", mission->mission_id, mission->status, mission->n1);	
	}

//	DEBUG_PRINT(0, "in_get_hero_mission:out_buffer=%s", out_buffer);

	return 0;
}

//#define SQL_SAVE_HERO_MISSION_LIST	"UPDATE evil_hero_mission SET n1=%d,status=%d WHERE eid=%d AND hero_id=%d AND mission_id=%d"
#define SQL_SAVE_HERO_MISSION_LIST_START	"INSERT INTO evil_hero_mission VALUES "
#define SQL_SAVE_HERO_MISSION_LIST_ITEM		"(%d,%d,%d,%d,%d)"
#define SQL_SAVE_HERO_MISSION_LIST_END		" ON DUPLICATE KEY UPDATE status=VALUES(status),n1=VALUES(n1)"
int __save_hero_mission_list(MYSQL **pconn, char *q_buffer, int eid, int hero_id, evil_hero_mission_t *update_mlist, int ucount)
{
	int ret;
	int len;
	char *qptr;
	int count;

	count = 0;
	qptr = q_buffer;
	len = sprintf(qptr, SQL_SAVE_HERO_MISSION_LIST_START);
	qptr += len;
	for (int i = 0; i <= MAX_HERO_MISSION; i++) {
		evil_hero_mission_t *mission = update_mlist + i;
		if (mission->mission_id == 0) {
			continue;
		}
		if (count > 0) {
			len = sprintf(qptr, ",");
			qptr += len;
		}
		len = sprintf(qptr, SQL_SAVE_HERO_MISSION_LIST_ITEM, eid, hero_id
		, mission->mission_id, mission->status, mission->n1);
		qptr += len;
		count++;
	}
	DEBUG_PRINT(1, "save_hero_mission_buffer[%s]", q_buffer);
	len = sprintf(qptr, SQL_SAVE_HERO_MISSION_LIST_END);
	qptr += len;
	
	len = qptr - q_buffer;
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-5, "save_hero_mission_list:query ret=%d", ret);
	}

	return 0;
}

int in_update_hero_mission(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int hero_id;
	int count;
	int n;
	int mid;
	int status;
	int n1;
	const char *in_ptr;
	int mid_list[MAX_HERO_MISSION+1];
	evil_hero_mission_t mission_list[MAX_HERO_MISSION+1];
	bzero(mid_list, sizeof(mid_list));
	bzero(mission_list, sizeof(mission_list));

	// DEBUG_PRINT(1, "in_buffer[%s]", in_buffer);
	in_ptr = in_buffer;
	ret = sscanf(in_ptr, "%d %d %d %n", &eid, &hero_id, &count, &n);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_update_hero_mission:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_update_hero_mission:invalid_eid");
	}
	if (hero_id <= 0 || hero_id > HERO_MAX) {
		DB_ER_RETURN(-25, "in_update_hero_mission:invalid_hero_id");
	}
	if (count == 0) {
		WARN_PRINT(-35, "in_update_hero_mission:mission_count_0");
		sprintf(out_buffer, "0");
		return 0;
	}
	in_ptr += n;
	for (int i = 0; i < count; i++) {
		ret = sscanf(in_ptr, "%d %d %d %n", &mid, &status, &n1, &n);
		if (ret != 3) {
			DB_ER_RETURN(-45, "in_update_hero_mission:invalid_input");
		}
		in_ptr += n;
		mission_list[mid].mission_id = mid;
		mission_list[mid].status = status;
		mission_list[mid].n1 = n1;
	}

	// update to databases
	ret = __save_hero_mission_list(pconn, q_buffer, eid, hero_id
	, mission_list, count);
	DB_ER_RETURN(ret, "in_update_hero_mission:save_hero_mission_list_fail %d %d"
	, eid, hero_id);

	sprintf(out_buffer, "0");
	return 0;
}

int in_load_hero_data(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int n;
	int hero_count;
	int mis_count;
	char *out_ptr, *hero_ptr;
	char hero_buffer[DB_BUFFER_MAX+1];
	evil_hero_t *hero;
	evil_hero_t design_hero_list[HERO_MAX+1];
	evil_hero_t hero_list[HERO_MAX+1];
	evil_hero_mission_t *mission;
	evil_hero_mission_t mission_list[MAX_HERO_MISSION+1];

	ret = sscanf(in_buffer, IN_LOAD_HERO_DATA_SCAN, &eid, &n);
	if (ret != 1) {
		DB_ER_RETURN(-5, "in_load_hero_data:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_load_hero_data:invalid_eid");
	}

	bzero(design_hero_list, sizeof(design_hero_list));
	ret = __init_design_hero_list(in_buffer + n, design_hero_list);
	DB_ER_RETURN(ret, "in_get_mission_hero:init_design_hero_list_fail");

	bzero(hero_list, sizeof(hero_list));
	ret = __init_hero_by_card_list(pconn, q_buffer, eid, hero_list
	, design_hero_list);
	DB_ER_NEG_RETURN(ret, "in_get_mission_hero:init_hero_by_card_list_fail");

	hero_count = ret;
	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, "%d %d", eid, hero_count);
	for (int i = 1; i <= HERO_MAX; i++) {
		hero = hero_list+i;
		if (hero->hero_id == 0) {
			continue;
		}

		bzero(mission_list, sizeof(mission_list));
		ret = get_hero_mission(pconn, q_buffer, eid, hero->hero_id, mission_list);
		ERROR_NEG_RETURN(ret, "in_load_hero_data:get_hero_mission_fail");

		mis_count = ret;
		hero_ptr = hero_buffer;
		hero_ptr += sprintf(hero_ptr, OUT_LOAD_HERO_DATA_PRINT, hero->hero_id
		, hero->hp, hero->energy, mis_count);
		for (int mi = 0; mi < MAX_HERO_MISSION; mi++) {
			mission = mission_list+mi;
			if (mission->mission_id == 0) {
				continue;
			}
			hero_ptr += sprintf(hero_ptr, OUT_LOAD_HERO_MISSION_PRINT
			, mission->mission_id, mission->status, mission->n1);
		}
		
		out_ptr += sprintf(out_ptr, "%s", hero_buffer);
	}

	return 0;
}


#define SQL_SUBMIT_HERO_MISSION "UPDATE evil_hero_mission SET status=%d WHERE eid=%d AND hero_id=%d and mission_id=%d AND status=%d"
int in_submit_hero_mission(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	int hero_id;
	int mission_id;
	int reward_type;
	int reward_count;
	int hp, add_hp;
	int energy, add_energy;
//	char tmp_buffer[DB_BUFFER_MAX+1];

	ret = sscanf(in_buffer, "%d %d %d %d %d", &eid, &hero_id, &mission_id
	, &reward_type, &reward_count);
	if (ret != 5) {
		DB_ER_RETURN(-5, "in_submit_hero_mission:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_submit_hero_mission:invalid_eid");
	}
	if (hero_id <= 0 || hero_id > HERO_MAX) {
		DB_ER_RETURN(-25, "in_submit_hero_mission:invalid_hero_id");
	}
	if (mission_id <= 0 || mission_id > MAX_HERO_MISSION) {
		DB_ER_RETURN(-35, "in_submit_hero_mission:invalid_mission_id");
	}
	if (reward_type != 1 && reward_type != 2) {
		DB_ER_RETURN(-7, "in_submit_hero_mission:reward_type_error %d"
		, reward_type);
	}

	len = sprintf(q_buffer, SQL_SUBMIT_HERO_MISSION, MISSION_STATUS_FINISH
	, eid, hero_id, mission_id, MISSION_STATUS_OK);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-45, "in_submit_hero_mission:query ret=%d", ret);
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret == 0) {
		DB_ER_RETURN(-6
		, "in_submit_hero_mission:mission_id_not_exist %d or mission_unfinished"
		, mission_id);
	}
	if (ret != 1) {
		ERROR_RETURN(-55, "in_submit_hero_mission:query err=%d"
		, mysql_errno(*pconn)); 
	}

	add_hp = 0;
	add_energy = 0;
	if (reward_type == 1) {
		add_hp = reward_count;
	} else {
		add_energy = reward_count;
	}
	ret = __update_mission_hero(pconn, q_buffer, eid, hero_id
	, add_hp, add_energy);
	ERROR_RETURN(ret, "in_submit_hero_mission:add_hero_mission_reward_fail");

	ret = __refresh_hero_mission(pconn, q_buffer, eid, hero_id);
	BUG_PRINT(ret, "in_submit_hero_mission:refresh_hero_mission_fail %d %d %d"
	, eid, hero_id, mission_id);

	ret = get_mission_hero(pconn, q_buffer, eid, hero_id, hp, energy);
	ERROR_RETURN(ret, "in_submit_hero_mission:get_mission_hero_fail %d %d"
	, eid, hero_id);

//	sprintf(tmp_buffer, "%d", eid);
//	ret = in_load_hero_data(pconn, q_buffer, tmp_buffer, tmp_buffer);
//	ERROR_RETURN(ret, "in_submit_hero_mission:in_load_hero_data_fail %d %d"
//	, eid, hero_id);

//	sprintf(out_buffer, "%d %d %d %s", eid, hero_id, mission_id, tmp_buffer);
	sprintf(out_buffer, "%d %d %d %d %d", eid, hero_id, mission_id, hp, energy);
	return 0;
}



#define SQL_GET_HERO_SLOT "SELECT * FROM evil_hero_slot WHERE eid=%d and hero_id=%d and slot_id=%d"
int __get_hero_slot(MYSQL **pconn, char *q_buffer, char *slot, int eid, int hero_id, int slot_id)
{
	int ret;
	int err;
	int len;
	int eid_inside;
	int hero_id_inside;
	int slot_id_inside;

	slot[0] = '\0';
	len = sprintf(q_buffer, SQL_GET_HERO_SLOT, eid, hero_id, slot_id);

	// printf("get_hero_slot:query: %s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "get_hero_slot:query err %d", mysql_errno(*pconn));
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_hero_slot:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid) + 1 (hero_id) + 1 (slot_id)
	if (field_count != EVIL_CARD_MAX + 3) {
		ret = -7;
		ERROR_PRINT(ret, "get_hero_slot:field_count %d != card_count+3 %d",
			field_count, EVIL_CARD_MAX+3);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	// normally 2 rows, for eid1 and eid2, possible 1 row for eid2 is AI
	// 0 is impossible, should be error
	if (num_row <= 0) {
		ret = -6;	 // logical error
		ERROR_PRINT(ret, "get_hero_slot:deck_not_enough row=%d %d", num_row, eid);
		goto cleanup;
	}

	// ok, we are going to get the first row
	// this is eid1, deck1
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(ret, "get_hero_slot:deck_null_row %d", eid);
		goto cleanup;
	}

//	err = row_to_deck(row, &eid_inside, deck, EVIL_NUM_DECK_MAX);
	err = row_to_hero_slot(row, &eid_inside, &hero_id_inside
	, &slot_id_inside, slot, EVIL_NUM_DECK_MAX);
	if (err < 0) {
		ret = -17;
		ERROR_PRINT(ret, "get_hero_slot:row_to_hero_slot %d", err);
		goto cleanup;
	}

	// DEBUG_PRINT(0, "get_hero_slot:eid=%d slot=%s", eid_inside, slot);
	if (eid_inside != eid || hero_id_inside != hero_id
	|| slot_id_inside != slot_id) {
		ret = -27;
		ERROR_PRINT(ret, "get_hero_slot:slot_mismatch %d/%d %d/%d %d/%d"
		, eid_inside, eid, hero_id_inside, hero_id, slot_id_inside, slot_id);
		goto cleanup;
	}

	ret = 0; // ok 

cleanup:
	mysql_free_result(result);
	return ret;
}


int in_get_hero_slot(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int hero_id;
	int slot_id;
	char slot_buffer[EVIL_CARD_MAX+1];

	ret = sscanf(in_buffer, "%d %d %d", &eid, &hero_id, &slot_id);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_get_hero_slot:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_get_hero_slot:invalid_eid");
	}
	if (hero_id <= 0 || hero_id > HERO_MAX) {
		DB_ER_RETURN(-25, "in_get_hero_slot:invalid_hero_id");
	}
	if (slot_id <= 0 || slot_id > EVIL_HERO_SLOT_MAX) {
		DB_ER_RETURN(-35, "in_get_hero_slot:invalid_slot_id");
	}
	ret = __get_hero_slot(pconn, q_buffer, slot_buffer, eid, hero_id, slot_id);
	DB_ER_RETURN(ret, "in_get_hero_slot:get_hero_slot_fail");

	sprintf(out_buffer, "%d %d %d %s", eid, hero_id, slot_id, slot_buffer);

	// out_buffer = [eid] [hero_id] [slot_id] [slot]

	return 0;
}

static int __query_hero_slot(const char* prefix, char* query, int query_max, 
int eid, int hero_id, int slot_id, const char *card, int card_count)
{
	// @see mysql-ref-51.pdf : p.2907   for mysql_escape_string
	char *end;
	char numstr[20];

	if (card_count < 0) {
		return -2;
	}
	if (card==NULL) {
		return -3;
	}
	if (prefix==NULL) {
		return -13;
	}
	// not enough for base 
	if (query_max < card_count * 5) { 
		ERROR_RETURN(-12, "query_hero_slot:query_max_too_small %d", query_max);
	}

	// why -10 ? -- Each card equals ",1" use two bytes
	// Last ) and '\0' use two bytes.
	char * const query_end_max = query + query_max - 10;


	// (to, from from_len)
	// mysql_escape_string( )

	end = query;  // make it like recursive
	end = stpcpy(end, prefix);
	////////// 
	//for (int i=1; i<=card_count; i++) {
	//	sprintf(numstr, ",c%d", i);
	//	end = stpcpy(end, numstr);
	//}
	//////////

	// remove ) before VALUES
	sprintf(numstr, " SET ");
	end = stpcpy(end, numstr);

	// TODO check card_count
	for (int i=0; i<card_count; i++) {
		if (card[i]<'0' || card[i]>'9') {
			ERROR_RETURN(-32, "query_card:0-9 outbound eid=%d i=%d ascii=%d"
			, eid, i, card[i]);
		}
		if (i > 0) {
			*end = ',';	end++;
		}
		end += sprintf(end, "c%d=%d", i+1, card[i]-'0');
//		*end = ','; 	end++;	// keep ++ in same line
//		*end = card[i];	end++;
		if (end > query_end_max) {
			ERROR_PRINT(-22, "query_card:overflow %d i=%d", query_max, i);
			return -22;
		}
	}
	// close )
	end += sprintf(end, " WHERE eid=%d AND hero_id=%d AND slot_id=%d", eid
	, hero_id, slot_id);
	*end = '\0'; 		//null term but not ++, MUST HAVE!

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}

#define SQL_UPDATE_HERO_SLOT "UPDATE evil_hero_slot"
int __update_hero_slot(MYSQL **pconn, char *q_buffer, char *slot, int eid, int hero_id, int slot_id)
{
	int ret;
	int err;
	int len;

	len = __query_hero_slot(SQL_UPDATE_HERO_SLOT, q_buffer, QUERY_MAX
	, eid, hero_id, slot_id, slot, EVIL_CARD_MAX);
	if (len < 0) {
		ERROR_NEG_RETURN(-25, "update_hero_slot:card %d", len);
	}
	// printf("--- update_hero_slot: query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		if (err==1062) {
			ERROR_RETURN(-6, "update_hero_slot:already_choose_job");
		}
		ERROR_RETURN(-55, "update_hero_slot:mysql_errno %d", err);
	}


	// check affected row
	ret = mysql_affected_rows(*pconn); // update can be 0 or 1
	if (ret < 0 || ret > 1) {
		ERROR_RETURN(-7, "update_hero_slot:affected_row wrong %d\n", ret);
	}

	return 0;
}

static int __query_hero_deck(const char* prefix, char* query, int query_max, 
int eid, int hero_id, int slot_id, const char *card, int card_count)
{
	// @see mysql-ref-51.pdf : p.2907   for mysql_escape_string
	char *end;
	char numstr[20];

	if (card_count < 0) {
		return -2;
	}
	if (card==NULL) {
		return -3;
	}
	if (prefix==NULL) {
		return -13;
	}
	// not enough for base 
	if (query_max < card_count * 5) { 
		ERROR_RETURN(-12, "query_hero_deck:query_max_too_small %d", query_max);
	}

	// why -10 ? -- Each card equals ",1" use two bytes
	// Last ) and '\0' use two bytes.
	char * const query_end_max = query + query_max - 10;


	// (to, from from_len)
	// mysql_escape_string( )

	end = query;  // make it like recursive
	end = stpcpy(end, prefix);
	////////// 
	//for (int i=1; i<=card_count; i++) {
	//	sprintf(numstr, ",c%d", i);
	//	end = stpcpy(end, numstr);
	//}
	//////////

	// remove ) before VALUES
	sprintf(numstr, " SET ");
	end = stpcpy(end, numstr);

	// TODO check card_count
	for (int i=0; i<card_count; i++) {
		if (card[i]<'0' || card[i]>'9') {
			ERROR_RETURN(-32, "query_hero_deck:0-9 outbound eid=%d i=%d ascii=%d"
			, eid, i, card[i]);
		}
		if (i > 0) {
			*end = ',';	end++;
		}
		end += sprintf(end, "c%d=%d", i+1, card[i]-'0');
//		*end = ','; 	end++;	// keep ++ in same line
//		*end = card[i];	end++;
		if (end > query_end_max) {
			ERROR_PRINT(-22, "query_hero_deck:overflow %d i=%d", query_max, i);
			return -22;
		}
	}
	// close )
	end += sprintf(end, " WHERE eid=%d AND slot=%d AND c%d=1", eid
	, slot_id, hero_id);
	*end = '\0'; 		//null term but not ++, MUST HAVE!

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}

#define SQL_UPDATE_HERO_DECK "UPDATE evil_deck"
int __update_hero_deck(MYSQL **pconn, char *q_buffer, char *slot, int eid, int hero_id, int slot_id)
{
	int ret;
	int err;
	int len;

	len = __query_hero_deck(SQL_UPDATE_HERO_DECK, q_buffer, QUERY_MAX
	, eid, hero_id, slot_id, slot, EVIL_CARD_MAX);
	if (len < 0) {
		ERROR_NEG_RETURN(-25, "update_hero_deck:card %d", len);
	}
	// printf("--- update_hero_deck: query=%s\n", q_buffer);

	// if (1) return -10;
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		if (err==1062) {
			ERROR_RETURN(-6, "update_hero_deck:already_choose_job");
		}
		ERROR_RETURN(-55, "update_hero_deck:mysql_errno %d", err);
	}


	// check affected row
	ret = mysql_affected_rows(*pconn); // update can be 0 or 1
	if (ret < 0 || ret > 1) {
		ERROR_RETURN(-7, "update_hero_deck:affected_row wrong %d\n", ret);
	}

	return ret;
}

int in_update_hero_slot(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int hero_id;
	int slot_id;
	char slot_buffer[EVIL_CARD_MAX+1];

	ret = sscanf(in_buffer, "%d %d %d %400s", &eid, &hero_id
	, &slot_id, slot_buffer);
	if (ret != 4) {
		DB_ER_RETURN(-5, "in_update_hero_slot:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_update_hero_slot:invalid_eid");
	}
	if (hero_id <= 0 || hero_id > HERO_MAX) {
		DB_ER_RETURN(-25, "in_update_hero_slot:invalid_hero_id");
	}
	if (slot_id <= 0 || slot_id > EVIL_HERO_SLOT_MAX) {
		DB_ER_RETURN(-35, "in_update_hero_slot:invalid_slot_id");
	}
	if (strlen(slot_buffer) != EVIL_CARD_MAX) {
		DB_ER_RETURN(-45, "in_update_hero_slot:invalid_slot");
	}
	ret = __update_hero_slot(pconn, q_buffer, slot_buffer, eid, hero_id, slot_id);
	DB_ER_RETURN(ret, "in_update_hero_slot:update_hero_slot_fail");

	ret = __update_hero_deck(pconn, q_buffer, slot_buffer, eid, hero_id, slot_id);
	DB_ER_NEG_RETURN(ret, "in_update_hero_slot:update_hero_deck_fail");

	sprintf(out_buffer, "%d %d %d %d %.400s", eid, hero_id, slot_id, ret
	, slot_buffer);

	// out_buffer = [eid] [hero_id] [slot_id] [update_flag] [slot_buffer]

	return 0;
}

#define SQL_INSERT_HERO_SLOT "INSERT INTO evil_hero_slot (eid, hero_id, slot_id, c%d) VALUES (%d, %d, (SELECT * FROM (SELECT IFNULL(MAX(slot_id), 0)+1 AS count FROM evil_hero_slot WHERE eid=%d AND hero_id=%d LIMIT 1) tmp WHERE count <= %d), 1)"
int __insert_hero_slot(MYSQL **pconn, char *q_buffer, int eid, int hero_id)
{
	int ret;
	int err;
	int len;

	len = sprintf(q_buffer, SQL_INSERT_HERO_SLOT, hero_id, eid, hero_id, eid
	, hero_id, EVIL_HERO_SLOT_MAX);
	if (len < 0) {
		ERROR_NEG_RETURN(-25, "insert_hero_slot:card %d", len);
	}
	// printf("--- insert_hero_slot: query=%s\n", q_buffer);
	
	ret = my_query( pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		if (err==1062) {
			ERROR_RETURN(-6, "insert_hero_slot:already_choose_job");
		}
		ERROR_RETURN(-55, "insert_hero_slot:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn);
	if (ret != 1) {
		ERROR_RETURN(-7, "insert_hero_slot:affected_row wrong %d\n", ret);
	}
	// DEBUG_PRINT(ret, "insert_hero_slot:ret=%d", ret);

	return 0;
}


#define SQL_GET_MAX_HERO_SLOT_ID "SELECT MAX(slot_id) FROM evil_hero_slot WHERE eid=%d and hero_id=%d"
int __get_max_hero_slot_id(MYSQL **pconn, char *q_buffer, int eid, int hero_id)
{
	int ret;
	int len;
	int slot_id;

	len = sprintf(q_buffer, SQL_GET_MAX_HERO_SLOT_ID, eid, hero_id);

	// printf("get_max_hero_slot_id:query: %s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "get_max_hero_slot_id:query err %d", mysql_errno(*pconn));
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_max_hero_slot_id:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// MAX(slot_id)
	if (field_count != 1) {
		ret = -7;
		ERROR_PRINT(ret, "get_max_hero_slot_id:field_count %d != 1", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	// normally 2 rows, for eid1 and eid2, possible 1 row for eid2 is AI
	// 0 is impossible, should be error
	if (num_row <= 0) {
		ret = -6;	 // logical error
		ERROR_PRINT(ret, "get_max_hero_slot_id:deck_not_enough row=%d %d", num_row, eid);
		goto cleanup;
	}

	// ok, we are going to get the first row
	// this is eid1, deck1
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(ret, "get_max_hero_slot_id:null_row %d", eid);
		goto cleanup;
	}

	slot_id = strtol_safe(row[0], -1);
	if (slot_id <= 0) {
		slot_id = -66;
		ERROR_PRINT(ret, "get_max_hero_slot_id:max_slot_id_error %d %d", eid, hero_id);
		goto cleanup;
	}
	// DEBUG_PRINT(0, "get_max_hero_slot_id:slot_id = %d", slot_id);

	ret = slot_id; // ok 


cleanup:
	mysql_free_result(result);
	return ret;
}

int in_insert_hero_slot(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int hero_id;
	int slot_id;

	ret = sscanf(in_buffer, "%d %d", &eid, &hero_id);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_insert_hero_slot:invalid_input");
	} 
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "in_insert_hero_slot:invalid_eid");
	}
	if (hero_id <= 0 || hero_id > HERO_MAX) {
		DB_ER_RETURN(-25, "in_insert_hero_slot:invalid_hero_id");
	}

	ret = __insert_hero_slot(pconn, q_buffer, eid, hero_id);
	DB_ER_RETURN(ret, "in_insert_hero_slot:insert_fail %d %d", eid, hero_id);

	slot_id = __get_max_hero_slot_id(pconn, q_buffer, eid, hero_id);
	DB_ER_NEG_RETURN(slot_id, "in_insert_hero_slot:get_max_hero_slot_id_fail %d %d", eid, hero_id);

	sprintf(out_buffer, "%d %d %d", eid, hero_id, slot_id);

	// out_buffer = [eid] [hero_id] [slot_id]

	return 0;
}

#define SQL_LIST_HERO_SLOT	"SELECT eid,hero_id,slot_id,c1+c2+c3+c4+c5+c6+c7+c8+c9+c10+c11+c12+c13+c14+c15+c16+c17+c18+c19+c20+c21+c22+c23+c24+c25+c26+c27+c28+c29+c30+c31+c32+c33+c34+c35+c36+c37+c38+c39+c40+c41+c42+c43+c44+c45+c46+c47+c48+c49+c50+c51+c52+c53+c54+c55+c56+c57+c58+c59+c60+c61+c62+c63+c64+c65+c66+c67+c68+c69+c70+c71+c72+c73+c74+c75+c76+c77+c78+c79+c80+c81+c82+c83+c84+c85+c86+c87+c88+c89+c90+c91+c92+c93+c94+c95+c96+c97+c98+c99+c100+c101+c102+c103+c104+c105+c106+c107+c108+c109+c110+c111+c112+c113+c114+c115+c116+c117+c118+c119+c120+c121+c122+c123+c124+c125+c126+c127+c128+c129+c130+c131+c132+c133+c134+c135+c136+c137+c138+c139+c140+c141+c142+c143+c144+c145+c146+c147+c148+c149+c150+c151+c152+c153+c154+c155+c156+c157+c158+c159+c160+c161+c162+c163+c164+c165+c166+c167+c168+c169+c170+c171+c172+c173+c174+c175+c176+c177+c178+c179+c180+c181+c182+c183+c184+c185+c186+c187+c188+c189+c190+c191+c192+c193+c194+c195+c196+c197+c198+c199+c200+c201+c202+c203+c204+c205+c206+c207+c208+c209+c210+c211+c212+c213+c214+c215+c216+c217+c218+c219+c220+c221+c222+c223+c224+c225+c226+c227+c228+c229+c230+c231+c232+c233+c234+c235+c236+c237+c238+c239+c240+c241+c242+c243+c244+c245+c246+c247+c248+c249+c250+c251+c252+c253+c254+c255+c256+c257+c258+c259+c260+c261+c262+c263+c264+c265+c266+c267+c268+c269+c270+c271+c272+c273+c274+c275+c276+c277+c278+c279+c280+c281+c282+c283+c284+c285+c286+c287+c288+c289+c290+c291+c292+c293+c294+c295+c296+c297+c298+c299+c300+c301+c302+c303+c304+c305+c306+c307+c308+c309+c310+c311+c312+c313+c314+c315+c316+c317+c318+c319+c320+c321+c322+c323+c324+c325+c326+c327+c328+c329+c330+c331+c332+c333+c334+c335+c336+c337+c338+c339+c340+c341+c342+c343+c344+c345+c346+c347+c348+c349+c350+c351+c352+c353+c354+c355+c356+c357+c358+c359+c360+c361+c362+c363+c364+c365+c366+c367+c368+c369+c370+c371+c372+c373+c374+c375+c376+c377+c378+c379+c380+c381+c382+c383+c384+c385+c386+c387+c388+c389+c390+c391+c392+c393+c394+c395+c396+c397+c398+c399+c400 AS count FROM evil_hero_slot WHERE eid=%d AND hero_id=%d ORDER BY slot_id ASC"
int in_list_hero_slot(MYSQL **pconn, char *q_buffer, const char *in_buffer, char* out_buffer)
{
	int ret;
//	int err;
	int len;
	int count;
	int eid;
	int hero_id;
	char *ptr;

	ret = sscanf(in_buffer, "%d %d", &eid, &hero_id);
	if (ret != 2) {
		DB_ER_RETURN(-5, "list_hero_slot:less_input %d", ret);
	}
	
	len = sprintf(q_buffer, SQL_LIST_HERO_SLOT, eid, hero_id);

	// printf("list_hero_slot:query: %s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "list_hero_slot:query err %d", mysql_errno(*pconn));
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "list_hero_slot:null_result");
	}

	field_count = mysql_field_count(*pconn);
	// 1 (eid) + 1 (hero_id) + 1 (slot_id) + 1 (count)
	if (field_count != 4) {
		ret = -7;
		DB_ER_PRINT(ret, "list_hero_slot:field_count %d != 4",
			field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	// normally 2 rows, for eid1 and eid2, possible 1 row for eid2 is AI
	// 0 is impossible, should be error
	if (num_row < 0) {
		ret = -6;	 // logical error
		DB_ER_PRINT(ret, "list_hero_slot:deck_not_enough row=%d %d", num_row, eid);
		goto cleanup;
	}
	if (num_row == 0) {
		sprintf(out_buffer, "%d %d %d", eid, hero_id, num_row);
		ret = 0;
		goto cleanup;
	}

	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d %d", eid, hero_id, num_row);
	count = 0;	
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		// DEBUG_PRINT(1, "list_hero_slot: count[%s]", row[3]);
		count++;
		if (count > num_row) {
			BUG_PRINT(-7, "list_hero_slot:fetch_row_overflow %d", count);
			break;
		}

		ptr += sprintf(ptr, " %s %s", row[2], row[3]);
	}

	// out_buffer = [eid] [hero_id] [row_num] [slot_info1] [slot_info2] ...
	// slot_info = [slot_id] [total_card_count]

	ret = 0; // ok 
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_GET_HERO_SLOT_CARD_COUNT	"SELECT c1+c2+c3+c4+c5+c6+c7+c8+c9+c10+c11+c12+c13+c14+c15+c16+c17+c18+c19+c20+c21+c22+c23+c24+c25+c26+c27+c28+c29+c30+c31+c32+c33+c34+c35+c36+c37+c38+c39+c40+c41+c42+c43+c44+c45+c46+c47+c48+c49+c50+c51+c52+c53+c54+c55+c56+c57+c58+c59+c60+c61+c62+c63+c64+c65+c66+c67+c68+c69+c70+c71+c72+c73+c74+c75+c76+c77+c78+c79+c80+c81+c82+c83+c84+c85+c86+c87+c88+c89+c90+c91+c92+c93+c94+c95+c96+c97+c98+c99+c100+c101+c102+c103+c104+c105+c106+c107+c108+c109+c110+c111+c112+c113+c114+c115+c116+c117+c118+c119+c120+c121+c122+c123+c124+c125+c126+c127+c128+c129+c130+c131+c132+c133+c134+c135+c136+c137+c138+c139+c140+c141+c142+c143+c144+c145+c146+c147+c148+c149+c150+c151+c152+c153+c154+c155+c156+c157+c158+c159+c160+c161+c162+c163+c164+c165+c166+c167+c168+c169+c170+c171+c172+c173+c174+c175+c176+c177+c178+c179+c180+c181+c182+c183+c184+c185+c186+c187+c188+c189+c190+c191+c192+c193+c194+c195+c196+c197+c198+c199+c200+c201+c202+c203+c204+c205+c206+c207+c208+c209+c210+c211+c212+c213+c214+c215+c216+c217+c218+c219+c220+c221+c222+c223+c224+c225+c226+c227+c228+c229+c230+c231+c232+c233+c234+c235+c236+c237+c238+c239+c240+c241+c242+c243+c244+c245+c246+c247+c248+c249+c250+c251+c252+c253+c254+c255+c256+c257+c258+c259+c260+c261+c262+c263+c264+c265+c266+c267+c268+c269+c270+c271+c272+c273+c274+c275+c276+c277+c278+c279+c280+c281+c282+c283+c284+c285+c286+c287+c288+c289+c290+c291+c292+c293+c294+c295+c296+c297+c298+c299+c300+c301+c302+c303+c304+c305+c306+c307+c308+c309+c310+c311+c312+c313+c314+c315+c316+c317+c318+c319+c320+c321+c322+c323+c324+c325+c326+c327+c328+c329+c330+c331+c332+c333+c334+c335+c336+c337+c338+c339+c340+c341+c342+c343+c344+c345+c346+c347+c348+c349+c350+c351+c352+c353+c354+c355+c356+c357+c358+c359+c360+c361+c362+c363+c364+c365+c366+c367+c368+c369+c370+c371+c372+c373+c374+c375+c376+c377+c378+c379+c380+c381+c382+c383+c384+c385+c386+c387+c388+c389+c390+c391+c392+c393+c394+c395+c396+c397+c398+c399+c400 AS count FROM evil_hero_slot WHERE eid=%d AND hero_id=%d AND slot_id=%d LIMIT 1"
int __get_hero_slot_card_count(MYSQL **pconn, char *q_buffer, int eid, int hero_id, int slot_id)
{
	int ret;
	int len;
	int count = 0;

	len = sprintf(q_buffer, SQL_GET_HERO_SLOT_CARD_COUNT, eid, hero_id, slot_id);

	// printf("get_max_hero_slot_id:query: %s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "get_hero_slot_card_count:query err %d", mysql_errno(*pconn));
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_hero_slot_card_count:null_result");
	}

	field_count = mysql_field_count(*pconn);
	if (field_count != 1) {
		ret = -7;
		ERROR_PRINT(ret, "get_hero_slot_card_count:field_count %d != 1", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;	 // logical error
		ERROR_PRINT(ret, "get_hero_slot_card_count:no_row row=%d %d", num_row, eid);
		goto cleanup;
	}

	// ok, we are going to get the first row
	// this is eid1, deck1
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(ret, "get_hero_slot_card_count:null_row %d", eid);
		goto cleanup;
	}

	count = strtol_safe(row[0], -1);
	// DEBUG_PRINT(0, "get_hero_slot_card_count:slot_id = %d", slot_id);

	ret = count; // ok 


cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_HERO_SLOT_TO_DECK	"REPLACE INTO evil_deck (SELECT eid,slot_id,c1,c2,c3,c4,c5,c6,c7,c8,c9,c10,c11,c12,c13,c14,c15,c16,c17,c18,c19,c20,c21,c22,c23,c24,c25,c26,c27,c28,c29,c30,c31,c32,c33,c34,c35,c36,c37,c38,c39,c40,c41,c42,c43,c44,c45,c46,c47,c48,c49,c50,c51,c52,c53,c54,c55,c56,c57,c58,c59,c60,c61,c62,c63,c64,c65,c66,c67,c68,c69,c70,c71,c72,c73,c74,c75,c76,c77,c78,c79,c80,c81,c82,c83,c84,c85,c86,c87,c88,c89,c90,c91,c92,c93,c94,c95,c96,c97,c98,c99,c100,c101,c102,c103,c104,c105,c106,c107,c108,c109,c110,c111,c112,c113,c114,c115,c116,c117,c118,c119,c120,c121,c122,c123,c124,c125,c126,c127,c128,c129,c130,c131,c132,c133,c134,c135,c136,c137,c138,c139,c140,c141,c142,c143,c144,c145,c146,c147,c148,c149,c150,c151,c152,c153,c154,c155,c156,c157,c158,c159,c160,c161,c162,c163,c164,c165,c166,c167,c168,c169,c170,c171,c172,c173,c174,c175,c176,c177,c178,c179,c180,c181,c182,c183,c184,c185,c186,c187,c188,c189,c190,c191,c192,c193,c194,c195,c196,c197,c198,c199,c200,c201,c202,c203,c204,c205,c206,c207,c208,c209,c210,c211,c212,c213,c214,c215,c216,c217,c218,c219,c220,c221,c222,c223,c224,c225,c226,c227,c228,c229,c230,c231,c232,c233,c234,c235,c236,c237,c238,c239,c240,c241,c242,c243,c244,c245,c246,c247,c248,c249,c250,c251,c252,c253,c254,c255,c256,c257,c258,c259,c260,c261,c262,c263,c264,c265,c266,c267,c268,c269,c270,c271,c272,c273,c274,c275,c276,c277,c278,c279,c280,c281,c282,c283,c284,c285,c286,c287,c288,c289,c290,c291,c292,c293,c294,c295,c296,c297,c298,c299,c300,c301,c302,c303,c304,c305,c306,c307,c308,c309,c310,c311,c312,c313,c314,c315,c316,c317,c318,c319,c320,c321,c322,c323,c324,c325,c326,c327,c328,c329,c330,c331,c332,c333,c334,c335,c336,c337,c338,c339,c340,c341,c342,c343,c344,c345,c346,c347,c348,c349,c350,c351,c352,c353,c354,c355,c356,c357,c358,c359,c360,c361,c362,c363,c364,c365,c366,c367,c368,c369,c370,c371,c372,c373,c374,c375,c376,c377,c378,c379,c380,c381,c382,c383,c384,c385,c386,c387,c388,c389,c390,c391,c392,c393,c394,c395,c396,c397,c398,c399,c400 FROM evil_hero_slot WHERE eid=%d AND hero_id=%d AND slot_id=%d LIMIT 1)"
int in_choose_hero_slot(MYSQL **pconn, char *q_buffer, const char *in_buffer, char* out_buffer)
{
	int ret;
	int len;
	int eid;
	int hero_id;
	int slot_id;
	int card_count;
	char deck[EVIL_CARD_MAX+1];
	char recommand_slot[EVIL_CARD_MAX+1];

	ret = sscanf(in_buffer, "%d %d %d %400s", &eid, &hero_id, &slot_id, recommand_slot);
	if (ret != 3 && ret != 4) {
		DB_ER_RETURN(-5, "in_choose_hero_slot:less_input %d", ret);
	}
	if (slot_id == HERO_SLOT_RECOMMAND && ret == 3) {
		DB_ER_RETURN(-7, "in_choose_hero_slot:input_error %d", ret);
	}
	
	if (slot_id == HERO_SLOT_RECOMMAND) {
		ret = save_deck(pconn, q_buffer, eid, slot_id, recommand_slot);
		DB_ER_RETURN(ret, "in_choose_hero_slot:save_deck_fail %d %d"
		, eid, slot_id);
		sprintf(out_buffer, "%d %d %d", eid, hero_id, slot_id);
		return 0;
	}

	// TODO should check slot count first
	card_count = __get_hero_slot_card_count(pconn, q_buffer, eid, hero_id, slot_id);
	DEBUG_PRINT(0, "in_choose_hero_slot:card_count=%d", card_count);
	if (card_count < 0) {
		ret = -6;
		DB_ER_RETURN(ret, "in_choose_hero_slot:get_card_count_fail %d %d %d"
		, eid, hero_id, slot_id);
	}
	if (card_count < EVIL_DECK_CARD_MIN) {
		ret = -6;
		DB_ER_RETURN(ret, "in_choose_hero_slot:%s", E_CHECK_DECK_LESS_MIN);
	}

	len = sprintf(q_buffer, SQL_HERO_SLOT_TO_DECK, eid, hero_id, slot_id);

	// printf("in_choose_hero_slot:query: %s\n", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "in_choose_hero_slot:query err %d", mysql_errno(*pconn));
	}

	ret = mysql_affected_rows(*pconn);
	if (ret != 1 && ret != 2) {
		ERROR_RETURN(-6, "in_choose_hero_slot:hero_id or slot_id error %d %d"
		, hero_id, slot_id);
	}
	// DEBUG_PRINT(ret, "in_choose_hero_slot:ret=%d", ret);

	ret = get_deck(pconn, q_buffer, eid, deck);
	if (ret != 0) {
		DB_ER_RETURN(-26, "in_choose_hero_slot:get_deck_fail %d", eid);
	}

	sprintf(out_buffer, "%d %d %d %.400s", eid, hero_id, slot_id, deck);

	// out_buffer = [eid] [hero_id] [slot_id] [deck]

	ret = 0; // ok 
	return ret;
}


#define SQL_LOAD_HERO_DECK	"SELECT * FROM evil_deck WHERE eid=%d"
int in_load_hero_deck(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	int slot = 0;
	char * card;

	ret = sscanf(in_buffer, "%d", &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "load_hero_deck:invalid_input");
	}
	if (eid <= 0) {
		DB_ER_RETURN(-15, "load_hero_deck:invalid_eid");
	}
	
	len = sprintf(q_buffer, SQL_LOAD_HERO_DECK, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "load_hero_deck:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "load_hero_deck:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		// peter: this is normal, since user may disconnect 
		// before job selection, lcard can be empty!
		// DB_ER_PRINT(ret, "load_deck:empty_row eid=%d", eid);
		DB_ER_PRINT(ret, "%s eid=%d", E_LOAD_DECK_EMPTY_ROW, eid);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid) + 1 (slot)
	if (field_count != EVIL_CARD_MAX + 1  + 1) {
		ret = -7;
		DB_ER_PRINT(ret, "load_deck:field_count %d != card_count+2 %d",
			field_count, EVIL_CARD_MAX+2);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	// what's the implication?
	ret = strtol_safe(row[0], -1);
	WARN_PRINT(eid-ret, "load_hero_deck:eid %d != row[0] %d", eid, ret);

	// slot
	slot = strtol_safe(row[1], -1);
	if (slot < 0) {
		ret = -6;
		DB_ER_PRINT(ret, "load_hero_deck:slot(%d) < 0 eid=%d", slot, eid);
		goto cleanup;
	}
	if (slot == 0) {
		WARN_PRINT(-3, "load_hero_deck:slot=0  eid=%d", eid);
//		slot = 1;
		// continue with slot=1
	}
	
	len = sprintf(out_buffer, "%d %d ", eid, slot);
	card = out_buffer + len;
	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+2];
		if (NULL==data) {
			card[i] = '0' + 0;
		} else {
			ret = strtol_safe(data, -1);
			if (ret < 0) {
				WARN_PRINT(ret, "load_hero_deck:strtol_safe %s card_id=%d"
				, data, i);
				ret = 0;  // manual fix it
			}
			if (ret > 4) {	// limit the num of same card in deck to 4
				WARN_PRINT(ret, "load_hero_deck:card[%d]>4 (%d) eid=%d"
				, i, ret, eid);
				ret = 4;
			}
			card[i] = '0' + (char)ret;
		}
	}
	card[EVIL_CARD_MAX] = '\0'; // null terminate it

	// out_buffer = [eid] [slot_id] [card400]

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}


//=====================================================
// daily login

#define SQL_INSERT_DAILY_LOGIN	"INSERT INTO evil_daily_login (eid) VALUES(%d)"
int __insert_daily_login(MYSQL **pconn, char *q_buffer, int eid)
{
	int len;
	int err;
	int ret;

	if (eid <= 0) {
		ERROR_NEG_RETURN(-5, "insert_daily_login:invalid_eid %d", eid);
		return -9;
	}

	// global access: using g_query
	len = sprintf(q_buffer, SQL_INSERT_DAILY_LOGIN, eid);
	ret = my_query(pconn, q_buffer, len);

	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_NEG_RETURN(-55, "insert_daily_login:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_NEG_RETURN(-6, "insert_daily_login:affected_row wrong %d\n", ret);
	}
	return 0 ;
}


//#define SQL_GET_DAILY_LOGIN	"SELECT *,UNIX_TIMESTAMP(last_reward_date) FROM evil_daily_login WHERE eid=%d"
#define SQL_GET_DAILY_LOGIN	"SELECT *,IFNULL(DATEDIFF(NOW(),last_reward_date),1) FROM evil_daily_login WHERE eid=%d"
int in_get_daily_login(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int eid;
	int login_day;
	int diff_day;
	int has_get_reward;
	time_t last_reward_date;
	char *ptr;
	int day[MAX_DAILY_LOGIN+1];
	bzero(day, sizeof(day));

	ret = sscanf(in_buffer, "%d", &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "get_daily_login:invalid_input");
	}
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "get_daily_login:invalid_eid");
	}
	
	len = sprintf(q_buffer, SQL_GET_DAILY_LOGIN, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-55, "get_daily_Login:query err=%d", mysql_errno(*pconn)); 
		return -55; // safety, should never run
	}

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "get_daily_Login:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -7;
		// peter: this is normal, since user may disconnect 
		// before job selection, lcard can be empty!
		// DB_ER_PRINT(ret, "load_deck:empty_row eid=%d", eid);
		DB_ER_PRINT(ret, "get_daily_login:no_daily_login_data eid=%d", eid);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	// card_count + 1 (eid) + 1 (slot)
	if (field_count != 11) {
		ret = -17;
		DB_ER_PRINT(ret, "get_daily_Login:field_count %d", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	ret = strtol_safe(row[0], -1);
	login_day = strtol_safe(row[1], -1);
	last_reward_date = sqltime_to_time(row[2], -1);
//	last_reward_date = strtolong_safe(row[10], -1);
	diff_day = strtol_safe(row[10], -1);
	if (login_day < 0) {
		ret = -6;
		DB_ER_PRINT(ret, "get_daily_Login:login_day_error %d", login_day);
		goto cleanup;
	}
	if (last_reward_date < 0) {
		ret = -16;
		DB_ER_PRINT(ret, "get_daily_Login:last_reward_date %ld", last_reward_date);
		goto cleanup;
	}
	if (diff_day < 0) {
		ret = -26;
		DB_ER_PRINT(ret, "get_daily_Login:diff_day_error %d", diff_day);
		goto cleanup;
	}
	for (int i = 0; i < MAX_DAILY_LOGIN; i++) {
		day[i] = strtol_safe(row[3 + i], -1);
		if (day[i] < 0) {
			DB_ER_PRINT(ret, "get_daily_Login:day_error %d", day[i]);
			goto cleanup;
		}
	}

	if (diff_day > 1) {
		login_day = 0;	// set login_day to default(0)
	}
	has_get_reward = (diff_day<=0);
//	if (has_get_reward == 0) {
//		login_day++;	// has_get_reward, login_day means can get next reward day
//	}

	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d %d", eid, login_day, has_get_reward);
	for (int i = 0; i < MAX_DAILY_LOGIN; i++) {
		ptr += sprintf(ptr, " %d", day[i]);
	}
	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


// @_diff ORDER IS IMPORTANT!!!!
#define SQL_SET_DAILY_LOGIN_REWARD	"UPDATE evil_daily_login SET login_day=IF(@_diff=1,login_day+1, 1),last_reward_date=NOW(),day%d=day%d+1 WHERE eid=%d AND (((@_diff:=IFNULL(DATEDIFF(NOW(),last_reward_date),1))=1 AND login_day+1=%d) OR (@_diff>1 AND %d=1))"
int in_get_daily_reward(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	int ret;
	int len;
	int n;
	int eid;
	int log_day;
	int reward_day;
	int gold;
	int crystal;
	const char *in_ptr;
	int card_count;
	int piece_count;
	int cards[MAX_DAILY_REWARD_CARD+1][2];
	int pieces[MAX_DAILY_REWARD_PIECE+1][2];
	bzero(cards, sizeof(cards));
	bzero(pieces, sizeof(pieces));

	DEBUG_PRINT(0, "in_buffer[%s]", in_buffer);
	ret = sscanf(in_buffer, "%d %d %d %d %d %n", &eid, &log_day, &gold, &crystal
	, &card_count, &n);
	if (ret != 5) {
		DB_ER_RETURN(-5, "get_daily_reward:invalid_input");
	}
	if (eid <= MAX_AI_EID) {
		DB_ER_RETURN(-15, "get_daily_reward:invalid_eid");
	}
	if (gold < 0 || crystal < 0) {
		BUG_PRINT(-25, "get_daily_reward:error_gold[%d]_crystal[%d]"
		, gold, crystal);
	}

	in_ptr = in_buffer + n;
	for (int i = 0; i < card_count; i++) {
		ret = sscanf(in_ptr, "%d %n", &(cards[i][0]), &n);
		if (ret != 1) {
			DB_ER_RETURN(-35, "get_daily_reward:get_card_id_null %d %d"
			, card_count, i);
		}
		// TODO temp hard code
		cards[i][1] = 1;
		in_ptr += n;
	}

	ret = sscanf(in_ptr, "%d %n", &piece_count, &n);
	if (ret != 1) {
		DB_ER_RETURN(-45, "get_daily_reward:get_piece_count_null");
	}

	in_ptr += n;
	for (int i = 0; i < piece_count; i++) {
		ret = sscanf(in_ptr, "%d %d %n", &(pieces[i][0]), &(pieces[i][1]), &n);
		if (ret != 2) {
			DB_ER_RETURN(-55, "get_daily_reward:get_piece_id_null %d %d"
			, piece_count, i);
		}
		in_ptr += n;
	}
	
	if (log_day > MAX_DAILY_LOGIN)	{	reward_day = MAX_DAILY_LOGIN;	}
	else if (log_day <= 0)			{	reward_day = 1;					}
	else							{	reward_day = log_day;			}

	len = sprintf(q_buffer, SQL_SET_DAILY_LOGIN_REWARD, reward_day
	, reward_day, eid, log_day, log_day);
//	DEBUG_PRINT(1, "q_buffer[%s]", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		DB_ER_RETURN(-65, "get_daily_reward:query err=%d", mysql_errno(*pconn)); 
		return -65; // safety, should never run
	}
	ret = mysql_affected_rows(*pconn);
	if (ret != 1) {
		DB_ER_NEG_RETURN(-6, "get_daily_reward:affected_row wrong %d", ret);
	}

	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	if (ret != 0) {
		ERROR_PRINT(-16, "get_daily_reward:update_money_err %d %d %d %d"
		, ret, eid, gold, crystal);	// let it though down
	}
	ret = __update_card_list(pconn, q_buffer, eid, cards, card_count);
	if (ret != 0) {
		char tmp_buffer[1000];
		char *tmp_ptr = tmp_buffer;
		for (int i = 0; i < card_count; i++) {
			tmp_ptr += sprintf(tmp_ptr, "%d ", cards[i][0]);
		}
		ERROR_PRINT(-26, "get_daily_reward:update_card_list_error %d %d %d [%s]"
		, ret, eid, card_count, tmp_buffer);	// let it though down
	}

	ret = __update_piece_list(pconn, q_buffer, eid, pieces, piece_count);
	if (ret != 0) {
		char tmp_buffer[1000];
		char *tmp_ptr = tmp_buffer;
		for (int i = 0; i < piece_count; i++) {
			tmp_ptr += sprintf(tmp_ptr, "%d[%d] ", pieces[i][0]
			, pieces[i][1]);
		}
		ERROR_PRINT(-36, "get_daily_reward:update_piece_list_error %d %d %d [%s]"
		, ret, eid, piece_count, tmp_buffer);	// let it though down
	}

	sprintf(out_buffer, "%s", in_buffer);

	return 0;
}

// daily login
//=====================================================



//=====================================================
// piece_shop


#define SQL_GET_PIECE_SHOP	"SELECT * FROM evil_piece_shop WHERE eid=%d LIMIT 1"
int __get_piece_shop(MYSQL **pconn, char *q_buffer, int eid, evil_piece_shop_t &shop)
{
	int ret;
	int len;

	if (eid <= MAX_AI_EID)
	{
		ERROR_RETURN(-15, "get_piece_shop:eid_out_bound %d", eid);
	}

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	len = sprintf(q_buffer, SQL_GET_PIECE_SHOP, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-25, "get_piece_shop:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-23, "get_piece_shop:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<0) {
		ret = -35;
		ERROR_RETURN(-35, "get_piece_shop:mysql_result_error %d", num_row);
		goto cleanup;
	}

	if (num_row==0) {
		ret = 0;
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	if (field_count != 16) {
		ret = -7;
		ERROR_PRINT(ret, "get_piece_shop:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(-13, "get_piece_shop:row_null %d", eid);
		goto cleanup;
	}

	shop.last_time = strtolong_safe(row[1], -1);
	if (shop.last_time < 0) {
		ret = -45;
		ERROR_PRINT(ret, "get_piece_shop:last_time %d", eid);
		goto cleanup;
	}
	shop.refresh_times = strtol_safe(row[2], -1);
	if (shop.refresh_times < 0) {
		ret = -55;
		ERROR_PRINT(ret, "get_piece_shop:refresh_times %d", eid);
		goto cleanup;
	}
	shop.show_times = strtol_safe(row[3], -1);
	if (shop.show_times < 0) {
		ret = -65;
		ERROR_PRINT(ret, "get_piece_shop:show_times %d", eid);
		goto cleanup;
	}

	for (int i = 0; i < MAX_PIECE_SHOP_SLOT; i++)
	{
		shop.pid_list[i] = strtol_safe(row[4+i*2], -1);
		shop.buy_flag_list[i] = strtol_safe(row[5+i*2], -1);
		if (shop.pid_list[i] < 0) {
			ret = -75;
			ERROR_PRINT(ret, "get_piece_shop:pid_list[%d] %d", i, eid);
			goto cleanup;
		}
		if (shop.buy_flag_list[i] != 0 && shop.buy_flag_list[i] != 1) {
			ret = -85;
			ERROR_PRINT(ret, "get_piece_shop:buy_flag_list[%d] %d", i, eid);
			goto cleanup;
		}
	}

cleanup:
	mysql_free_result(result);
	return ret;
}

int in_get_piece_shop(MYSQL **pconn, char *q_buffer, const char *in_buffer, char* out_buffer)
{
	int ret;
	int eid;
	evil_piece_shop_t shop;
	bzero(&shop, sizeof(shop));

	ret = sscanf(in_buffer, "%d", &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "in_get_piece_shop:invalid_input");
	}
	if (eid <= MAX_AI_EID)
	{
		DB_ER_RETURN(-15, "in_get_piece_shop:eid_out_bound %d", eid);
	}

	ret = __get_piece_shop(pconn, q_buffer, eid, shop);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_get_piece_shop:get_piece_shop %d", eid);
	}

	sprintf(out_buffer, "%d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, eid
	, shop.last_time, shop.refresh_times, shop.show_times
	, shop.pid_list[0], shop.buy_flag_list[0]
	, shop.pid_list[1], shop.buy_flag_list[1]
	, shop.pid_list[2], shop.buy_flag_list[2]
	, shop.pid_list[3], shop.buy_flag_list[3]
	, shop.pid_list[4], shop.buy_flag_list[4]
	, shop.pid_list[5], shop.buy_flag_list[5]);

	ret = 0;
	return ret;
}


#define SQL_REPLACE_PIECE_SHOP	"REPLACE INTO evil_piece_shop VALUES(%d,%ld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d)"
int __update_piece_shop(MYSQL **pconn, char *q_buffer, int eid, evil_piece_shop_t &shop)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_REPLACE_PIECE_SHOP, eid
	, shop.last_time, shop.refresh_times, shop.show_times
	, shop.pid_list[0], shop.buy_flag_list[0]
	, shop.pid_list[1], shop.buy_flag_list[1]
	, shop.pid_list[2], shop.buy_flag_list[2]
	, shop.pid_list[3], shop.buy_flag_list[3]
	, shop.pid_list[4], shop.buy_flag_list[4]
	, shop.pid_list[5], shop.buy_flag_list[5]);
	// DEBUG_PRINT(1, "SQL[%s]", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "update_piece_shop:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		ERROR_NEG_RETURN(-6, "update_piece_shop:affected_row wrong %d\n", ret);
	}

	return 0;
}

int in_update_piece_shop(MYSQL **pconn, char *q_buffer, const char *in_buffer, char* out_buffer)
{
	int ret;
	int eid;
	evil_piece_shop_t shop;
	bzero(&shop, sizeof(shop));

	ret = sscanf(in_buffer, "%d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, &eid, &shop.last_time, &shop.refresh_times, &shop.show_times
	, &shop.pid_list[0], &shop.buy_flag_list[0]
	, &shop.pid_list[1], &shop.buy_flag_list[1]
	, &shop.pid_list[2], &shop.buy_flag_list[2]
	, &shop.pid_list[3], &shop.buy_flag_list[3]
	, &shop.pid_list[4], &shop.buy_flag_list[4]
	, &shop.pid_list[5], &shop.buy_flag_list[5]
	);
	if (ret != 16) {
		DB_ER_RETURN(-5, "update_piece_shop:invalid_input");
	}
	if (eid <= MAX_AI_EID)
	{
		DB_ER_RETURN(-15, "update_piece_shop:eid_out_bound %d", eid);
	}

	ret = __update_piece_shop(pconn, q_buffer, eid, shop);
	if (ret != 0) {
		DB_ER_RETURN(-6, "update_piece_shop:update_fail %d", eid);
	}

	sprintf(out_buffer, "%s", in_buffer);
	return 0;
}

int in_refresh_piece_shop(MYSQL **pconn, char *q_buffer, const char *in_buffer, char* out_buffer)
{
	int ret;
	int eid;
	int gold;
	evil_piece_shop_t shop;
	bzero(&shop, sizeof(shop));

	ret = sscanf(in_buffer, "%d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, &eid, &shop.last_time, &shop.refresh_times, &shop.show_times
	, &shop.pid_list[0], &shop.buy_flag_list[0]
	, &shop.pid_list[1], &shop.buy_flag_list[1]
	, &shop.pid_list[2], &shop.buy_flag_list[2]
	, &shop.pid_list[3], &shop.buy_flag_list[3]
	, &shop.pid_list[4], &shop.buy_flag_list[4]
	, &shop.pid_list[5], &shop.buy_flag_list[5]
	, &gold
	);
	if (ret != 17) {
		DB_ER_RETURN(-5, "in_refresh_piece_shop:invalid_input");
	}
	if (eid <= MAX_AI_EID)
	{
		DB_ER_RETURN(-15, "in_refresh_piece_shop:eid_out_bound %d", eid);
	}

	ret = update_money(pconn, q_buffer, eid, gold, 0, EVIL_BUY_REFRESH_PSHOP, 1, "refresh_pshop");
	if (ret != 0) {
		DB_ER_RETURN(-16, "in_refresh_piece_shop:update_money_fail %d", eid);
	}

	ret = __update_piece_shop(pconn, q_buffer, eid, shop);
	if (ret != 0) {
		DB_ER_RETURN(-26, "in_refresh_piece_shop:update_fail %d", eid);
	}

	// ret = record_buy(pconn, q_buffer, eid, EVIL_BUY_REFRESH_PSHOP, 1, gold, 0, "refresh_pshop");

	sprintf(out_buffer, "%s", in_buffer);
	return 0;
}

#define SQL_UPDATE_PIECE_BUY_FLAG	"UPDATE evil_piece_shop SET buy_flag%d=%d WHERE eid=%d"
int __update_piece_buy_flag(MYSQL **pconn, char* q_buffer, int eid, int pos)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_UPDATE_PIECE_BUY_FLAG, pos, 1, eid);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "update_piece_buy_flag:mysql_errno %d", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); // replace can be 1 or 2
	if (ret != 1) {
		ERROR_NEG_RETURN(-6, "update_piece_buy_flag:affected_row wrong %d\n", ret);
	}

	return 0;
}

int in_piece_buy(MYSQL **pconn, char *q_buffer, const char *in_buffer, char* out_buffer)
{
	int ret;
	int eid;
	int pos;
	int card_id;
	int count;
	int gold;
	int crystal;

	ret = sscanf(in_buffer, "%d %d %d %d %d %d"
	, &eid, &pos, &card_id, &count, &gold, &crystal);
	if (ret != 6) {
		DB_ER_RETURN(-5, "in_piece_buy:invalid_input");
	}
	if (eid <= MAX_AI_EID)
	{
		DB_ER_RETURN(-15, "in_piece_buy:eid_out_bound %d", eid);
	}

	ret = __update_piece_buy_flag(pconn, q_buffer, eid, pos);
	if (ret != 0) {
		DB_ER_RETURN(-36, "in_piece_buy:update_piece_buy_flag_fail %d", eid);
	}

	ret = update_money(pconn, q_buffer, eid, gold, crystal, card_id+EVIL_BUY_PIECE, count, "buy_piece");
	if (ret != 0) {
		DB_ER_RETURN(-16, "in_piece_buy:update_money_fail %d", eid);
	}

	ret = update_piece(pconn, q_buffer, eid, card_id, count);
	if (ret != 0) {
		DB_ER_RETURN(-26, "in_piece_buy:update_piece_fail %d", eid);
	}

	// ret = record_buy(pconn, q_buffer, eid, card_id+EVIL_BUY_PIECE, count, gold, crystal, "buy_piece");


	sprintf(out_buffer, "%s", in_buffer);
	return 0;
}


// piece_shop
//=====================================================

#define	SQL_ADMIN_REGISTER	"INSERT INTO evil_user VALUES (NULL, '%s', '%s', '%s', NOW(), NOW(), %d, 0, 0, 0, 0, '_')"
int __admin_register(MYSQL **pconn, char *q_buffer, const char *username, const char *password, const char * alias, int icon)
{
	int ret;
	int err;
	int len;
	char esc_username[EVIL_USERNAME_MAX * 2 + 5];
	char esc_password[EVIL_PASSWORD_MAX * 2 + 5];
	char esc_alias[EVIL_ALIAS_MAX * 2 + 5]; 

	len = mysql_real_escape_string(*pconn, esc_username, username, strlen(username));
	ERROR_NEG_RETURN(len, "esc_username");
	len = mysql_real_escape_string(*pconn, esc_password, password, strlen(password));
	ERROR_NEG_RETURN(len, "esc_password");
	len = mysql_real_escape_string(*pconn, esc_alias, alias, strlen(alias));
	ERROR_NEG_RETURN(len, "esc_alias");

	len = sprintf(q_buffer, SQL_ADMIN_REGISTER, esc_username, esc_password
	, esc_alias, icon);
	// DEBUG_PRINT(0, "admin_register:q_buffer=%s", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_PRINT(-55, "admin_register: username=%s password=%s alias=%s query=%s"
		, username, password, alias, q_buffer);
		ERROR_RETURN(-55, "admin_register:mysql_errno %d", err);
		return -55;
	}

	ret = (int)mysql_insert_id(*pconn);
	// INFO_PRINT(0, "admin_register:eid=%d", ret);
	if (ret <= 0) {
		ERROR_RETURN(-7, "admin_register:mysql_insert_id %d", ret);
	}

	evil_user_t user;
	bzero(&user, sizeof(user));
	user.eid = ret;  // from above: mysql_insert_id()
	init_user_status(&user);
	int level = abs(random()) % 20 + 10;
	user.lv = level;
	ret = save_status(pconn, q_buffer, &user);
	// DEBUG_PRINT(0, "admin_register:save_status");

	return user.eid;
}

int in_admin_add_robot(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;

	int eid;
	int hero_id;

	int icon;
	int hp;
	int energy;
	char username[EVIL_USERNAME_MAX+1];
	char password[EVIL_PASSWORD_MAX+1];
	char alias[EVIL_ALIAS_MAX+1];
	char deck[EVIL_CARD_MAX+1];

	ret = sscanf(in_buffer, "%s %s %s %d %d %d %400s"
	, username, password, alias, &icon, &hp, &energy, deck);
	if (ret != 7) {
		DB_ER_RETURN(-5, "in_admin_add_robot:invalid_input");
	}

	hero_id = __get_hero(deck);
	if (hero_id <= 0 || hero_id > HERO_MAX) {
		DB_ER_RETURN(-16, "in_admin_add_robot:invalid_hero_id %d", hero_id);
	}

	// note:
	// 1.do register
	// 2.save card
	// 3.add evil_hero

	// return eid
	ret = __admin_register(pconn, q_buffer, username, password, alias, icon);
	if (ret <= 0) {
		DB_ER_RETURN(-5, "in_admin_add_robot:register_fail %d", ret);
	}

	eid = ret;
	ret = __save_card(pconn, q_buffer, eid, deck);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_admin_add_robot:save_card_fail %d [%s]", eid, deck);
	}

	ret = insert_mission_hero(pconn, q_buffer, eid, hero_id, hp, energy);
	if (ret != 0) {
		DB_ER_RETURN(-26, "in_admin_add_robot:insert_hero_fail %d %d", eid, hero_id);
	}

	sprintf(out_buffer, "0");

	return 0;
}


////////////////////////////////////////////////
//	arena


// #define SQL_ADD_PLAYER_ARENA	"INSERT INTO evil_arena VALUES (%d, (SELECT * FROM (SELECT (IFNULL(MAX(rank), 0)+1) FROM evil_arena) tmp_tbl))"
#define SQL_ADD_PLAYER_ARENA	"INSERT INTO evil_arena VALUES (%d, (SELECT (IFNULL(MAX(t1.erank), 0)+1) as r FROM evil_arena t1));"

int __add_player_arena(MYSQL **pconn, char *q_buffer, int eid)
{
	int ret;
	int len;
	int err;
	len = sprintf(q_buffer, SQL_ADD_PLAYER_ARENA, eid);
	
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "__add_player_arena:mysql_errno %d  eid=%d\n"
		, err, eid);
	}

	// check affected row
	// load a round start match in db, after do round start function, match may has no change
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		// impossible
		ERROR_RETURN(-7
		, "__add_player_arena:sql_affected_rows wrong %d eid=%d\n"
		, ret, eid);
	}

	ret = 0;
	return ret;
}


// #define SQL_INIT_ARENA_LIST	"INSERT INTO evil_arena (SELECT ed.eid, ((SELECT COUNT(*) FROM (SELECT * FROM evil_deck WHERE NOT EXISTS(SELECT eid FROM evil_arena WHERE eid=evil_deck.eid)) tb1 WHERE eid<=ed.eid) + (SELECT IFNULL(MAX(rank), 0) FROM evil_arena)) FROM evil_deck ed WHERE NOT EXISTS (SELECT eid FROM evil_arena WHERE eid=ed.eid) AND ed.eid>=%d)"
//#define SQL_INIT_ARENA_LIST	"INSERT INTO evil_arena (SELECT ed.eid, ((SELECT COUNT(*) FROM evil_deck tb1 WHERE tb1.eid NOT IN (SELECT eid FROM evil_arena WHERE eid=tb1.eid) AND tb1.eid<=ed.eid AND tb1.eid>=%d) + (SELECT IFNULL(MAX(rank), 0) FROM evil_arena)) FROM evil_deck ed WHERE NOT EXISTS (SELECT eid FROM evil_arena WHERE eid=ed.eid) AND ed.eid>=%d)"
//#define SQL_INIT_ARENA_LIST	"INSERT INTO evil_arena (SELECT ed.eid, ((SELECT COUNT(*) FROM (SELECT * FROM evil_deck WHERE NOT EXISTS(SELECT eid FROM evil_arena WHERE eid=evil_deck.eid)) tb1 WHERE eid<=ed.eid AND ed.eid>=%d) + (SELECT IFNULL(MAX(rank), 0) FROM evil_arena)) FROM evil_deck ed WHERE NOT EXISTS (SELECT eid FROM evil_arena WHERE eid=ed.eid) AND ed.eid>=%d)"
#define SQL_INIT_ARENA_LIST	"INSERT INTO evil_arena (SELECT ed.eid,@rrr:=(@rrr+1) AS rownum FROM evil_deck AS ed LEFT JOIN evil_arena AS ea ON ea.eid=ed.eid LEFT JOIN (SELECT @rrr:=IFNULL(MAX(erank),0) FROM evil_arena) AS tb1 ON 1=1 WHERE IFNULL(ea.eid,0)=0 AND ed.eid>=%d)"

int in_init_arena(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int start_eid;
	int err;

	ret = sscanf(in_buffer, "%d", &start_eid);
	if (ret <= 0) {
		start_eid = 0;
	}

	len = sprintf(q_buffer, SQL_INIT_ARENA_LIST, start_eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "in_init_arena:mysql_errno %d\n"
		, err);
	}
	ret = mysql_affected_rows(*pconn); 
	if (ret == 0) {
		WARN_PRINT(1, "in_init_arena: init_none_arena %d", ret);
	}

	sprintf(out_buffer, "%d", ret);
	return 0;
}

// 1.get arena top 50
// 2.get arena target list
// 3.exchange arena pos, TODO nio should use special dbio thread to do this
// 4.get two arena player deck and data

#define SQL_ARENA_INFO	"SELECT rank,arena_times,arena_last_reward_time FROM evil_arena LEFT JOIN evil_status ON evil_arena.eid = evil_status.eid WHERE evil_arena.eid=%d"
int __get_arena_info(MYSQL **pconn, char *q_buffer, int eid, int &rank, int &arena_times, time_t &arena_last_reward_time)
{
	int ret;
	int len;
	int err;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	len = sprintf(q_buffer, SQL_ARENA_INFO, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-15, "__get_arena_info:mysql_errno %d", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "__get_arena_info:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row == 0) {
		ret = -25;
		ERROR_PRINT(ret, "__get_arena_info:eid_not_exist %d", eid);
		goto cleanup;
	} else if (num_row != 1) {
		ret = -7;
		ERROR_PRINT(ret, "__get_arena_info:num_row_error %d\n", num_row);
		goto cleanup;
	}
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -17;
		ERROR_PRINT(ret, "__get_arena_info:row_data_null\n");
		goto cleanup;
	}

	rank = strtol_safe(row[0], -1);
	arena_times = strtol_safe(row[1], -1);
	arena_last_reward_time = strtol_safe(row[2], -1);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_ARENA_TOP_LIST	"SELECT rank, ea.eid, icon, lv, game_count, game_win, alias FROM evil_arena ea INNER JOIN evil_user eu ON ea.eid=eu.eid LEFT JOIN evil_status es ON es.eid=eu.eid ORDER BY ea.rank ASC limit %d"
int in_arena_top_list(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{

	int ret;
	int len;
	int err;

	int eid;
	int rank;
	int arena_times;
	time_t arena_last_reward_time;
	int game_count;
	int game_win;
	double win_rate;

	ret = sscanf(in_buffer, "%d", &eid);
	if (ret != 1) {
		DB_ER_RETURN(-5, "in_arena_top_list:input_invalid\n");
	}

	ret = __get_arena_info(pconn, q_buffer, eid, rank
	, arena_times, arena_last_reward_time);
	if (ret != 0) {
		ERROR_PRINT(-6, "in_arena_top_list:get_ranking_info_error\n");
		rank = 0;
	}
	// DEBUG_PRINT(0, "in_arena_top_list:eid=%d rank=%d", eid, rank);

	len = sprintf(q_buffer, SQL_ARENA_TOP_LIST, MAX_ARENA_TOP_LIST);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-15, "in_arena_top_list:mysql_errno %d\n", err);
	}

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int count;
	int field_count;
	char *ptr;

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-3, "in_arena_top_list:null_result\n");
	}

	num_row = mysql_num_rows(result);

	count = 0;
	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d", rank, num_row);
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			ret = -7;
			BUG_PRINT(ret, "in_arena_top_list:fetch_row_overflow %d/%d"
			, count, num_row);
			goto cleanup;
		}

		field_count = mysql_field_count(*pconn);
		// rank, eid, icon, lv, game_count, game_win, alias
		if (field_count != 7) {
			ret = -17;
			ERROR_PRINT(ret, "in_arena_top_list:field_count %d != 7", field_count);
			goto cleanup;
		}

		game_count = strtol_safe(row[4], 0);
		game_win = strtol_safe(row[5], 0);
		win_rate = (game_count <= 0) ? 1 : (game_win * 1.0f / game_count);
		
		ptr += sprintf(ptr, " %d %d %d %d %lf %s", strtol_safe(row[0], -1)
		, strtol_safe(row[1], -1), strtol_safe(row[2], -1)
		, strtol_safe(row[3], -1), win_rate, row[6]);
	}

	// out_buffer = [size] [arena_info1] [arena_info2] ...
	// arena_info = [rank] [eid] [icon] [lv] [win_rate(double)] [alias]

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


int __get_arena_target_list(int *rank_list, int rank)
{
	const static float RANGE_PERCENT_LIST[] = {
		.9F, .85F, .8F, .75F, .7F
	};

	int rank_count = min((rank - 1), MAX_ARENA_TARGET);
	if (rank_count <= 0) {
		return 0;
	}

	// rank form 2 ~ 20
	int tmp_rank = rank - 1;
	if (rank <= ARENA_GET_TARGET_BY_ORDER_RANK) {
		for (int i = 0; i < rank_count; i++) {
			rank_list[i] = tmp_rank--;
		}
		return rank_count;
	}

	// rank > 20
	for (int i = 0; i < MAX_ARENA_TARGET; i++) {
		tmp_rank = (int)(rank * RANGE_PERCENT_LIST[i]);
		if (i > 0 && rank_list[i-1] == tmp_rank) {
			tmp_rank--;
		}
		rank_list[i] = tmp_rank;
	}

	return rank_count;
}

int __is_arena_target(int rank_challenger, int rank_receiver)
{
	int target_list[MAX_ARENA_TARGET];
	int target_count = __get_arena_target_list(target_list, rank_challenger);
	for (int i = 0; i < target_count; i++) {
		if (target_list[i] == rank_receiver) {
			return 1;
		}
	}
	return 0;
}

int __check_arena_reward(time_t last_arena_time, time_t *offset)
{

	int flag_reward = 0;
	*offset = 0;
	time_t now = time(NULL);
	time_t yesterday = get_yesterday(now);
	time_t HALF_DAY = 12*60*60;
	time_t ONE_DAY = 24*60*60;
	time_t noon = yesterday + HALF_DAY;
	time_t end = yesterday + ONE_DAY;

	// 1.
	if (last_arena_time <= yesterday) {
		flag_reward = 1;
		*offset = 0;
		return flag_reward;
	}

	// 2.
	if (last_arena_time > yesterday && last_arena_time < noon && now < noon) {
		flag_reward = 0;
		*offset = noon - now;
		return flag_reward;
	}

	// 3.
	if (last_arena_time > yesterday && last_arena_time < noon && now >= noon) {
		flag_reward = 1;
		*offset = 0;
		return flag_reward;
	}

	// 4.
	if (last_arena_time >= noon) {
		flag_reward = 0;
		*offset = end - now;
		return flag_reward;
	}

	return flag_reward;
}

#define SQL_ARENA_TARGET_FRONT	"SELECT rank, ea.eid, icon, lv, game_count, game_win, alias FROM evil_arena ea INNER JOIN evil_user eu ON ea.eid=eu.eid LEFT JOIN evil_status es ON es.eid=eu.eid WHERE rank in ("
#define SQL_ARENA_TARGET_TAIL	") ORDER BY rank ASC LIMIT 5"
int in_arena_target(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;

	int eid;
	int rank;
	int arena_times;
	time_t arena_last_reward_time;
	time_t reward_time_offset;
	int has_reward;

	int game_count;
	int game_win;
	double win_rate;

	int target_count;
	int target_rank_list[MAX_ARENA_TARGET];
	bzero(target_rank_list, sizeof(target_rank_list));

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int count;
	int field_count;
	char *ptr, *out_ptr;

	ret = sscanf(in_buffer, "%d", &eid);
	if (ret <= 0) {
		DB_ER_RETURN(-5, "in_arena_target:input_invalid\n");
	}

	// 1. get self rank data
	ret = __get_arena_info(pconn, q_buffer, eid, rank
	, arena_times, arena_last_reward_time);
	if (ret != 0) {
		DB_ER_RETURN(-15, "in_arena_target:get_ranking_info_error\n");
	}
	has_reward = __check_arena_reward(arena_last_reward_time, &reward_time_offset);

	// 2. get target list by rank
	target_count = min(rank - 1, MAX_ARENA_TARGET);
	if (target_count <= 0) {
		// player is no.1, no target
		out_ptr = out_buffer;
		out_ptr += sprintf(out_ptr, "%d %d %ld %d %d", rank, has_reward, reward_time_offset, arena_times, 0);
		return 0;	// normal return
	}

	target_count = __get_arena_target_list(target_rank_list, rank);
	
	ptr = q_buffer;
	ptr += sprintf(ptr, SQL_ARENA_TARGET_FRONT);
	for (int i = 0; i < target_count; i++) {
		if (i > 0) {
			ptr += sprintf(ptr, ",%d", target_rank_list[i]);
		} else {
			ptr += sprintf(ptr, "%d", target_rank_list[i]);
		}
	}
	ptr += sprintf(ptr, SQL_ARENA_TARGET_TAIL);

	len = ptr-q_buffer;
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-35, "in_arena_target:mysql_errno %d\n", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-13, "in_arena_target:null_result\n");
	}
	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -27;
		DB_ER_PRINT(ret, "in_arena_target:num_row_error %d\n", num_row);
		goto cleanup;
	}

	count = 0;
	out_ptr = out_buffer;
	out_ptr += sprintf(out_ptr, "%d %d %ld %d %d", rank, has_reward, reward_time_offset
	, arena_times, num_row);
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			ret = -37;
			BUG_PRINT(ret, "in_arena_target:fetch_row_overflow %d/%d"
			, count, num_row);
			goto cleanup;
		}

		field_count = mysql_field_count(*pconn);
		// rank, eid, icon, lv, game_count, game_win, alias
		if (field_count != 7) {
			ret = -47;
			ERROR_PRINT(ret, "in_arena_target:field_count %d != 7"
			, field_count);
			goto cleanup;
		}
		game_count = strtol_safe(row[4], 0);
		game_win = strtol_safe(row[5], 0);
		win_rate = (game_count <= 0) ? 1 : (game_win * 1.0f / game_count);
		
		out_ptr += sprintf(out_ptr, " %d %d %d %d %lf %s", strtol_safe(row[0], -1)
		, strtol_safe(row[1], -1), strtol_safe(row[2], -1)
		, strtol_safe(row[3], -1), win_rate, row[6]);
	}

	// out_buffer = [my_rank] [has_reward] [reward_time_offset] [arena_times] [size] [arena_info1] [arena_info2] ...
	// arena_info = [rank] [eid] [icon] [lv] [win_rate(double)] [alias]

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_ARENA_RANK	"SELECT eid, rank FROM evil_arena WHERE eid IN (%d,%d)"
int __get_arena_rank(MYSQL **pconn, char *q_buffer, int eid1, int eid2, int &rank1, int &rank2)
{
	int ret;
	int len;
	int err;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	len = sprintf(q_buffer, SQL_ARENA_RANK, eid1, eid2);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-15, "__get_arena_rank:mysql_errno %d", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "__get_arena_rank:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row != 2) {
		ret = -25;
		ERROR_PRINT(ret, "__get_arena_rank:num_row_error %d %d %d", num_row, eid1, eid2);
		goto cleanup;
	}

	for (int i=0; i<num_row; i++) {
		row = mysql_fetch_row(result);
		if (row == NULL) {
			ret = -17;
			ERROR_PRINT(ret, "__get_arena_rank:row_data_null");
			goto cleanup;
		}
		int out_eid = strtol_safe(row[0], -1);
		int out_rank = strtol_safe(row[1], -1);
		if (out_eid <= 0 || (out_eid != eid1 && out_eid != eid2)) {
			ret = -66;
			ERROR_PRINT(ret, "__get_arena_rank:out_eid %d", out_eid);
			goto cleanup;
		}
		if (out_rank <= 0) {
			ret = -76;
			ERROR_PRINT(ret, "__get_arena_rank:out_rank %d", out_eid);
			goto cleanup;
		}
		
		if (out_eid == eid1) {
			rank1 = out_rank;
		} else {
			rank2 = out_rank;
		}
	}

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}

// "update evil_arena t1 join evil_arena t2 on (t1.eid=550 and t2.eid=547) or (t1.eid=547 and t2.eid=550) set t1.rank=t2.rank, t2.rank=t1.rank"
// #define SQL_EXCHANGE_ARENA	"UPDATE evil_arena SET rank=CASE eid WHEN %d THEN (SELECT rank FROM (SELECT eid, rank FROM evil_arena) AS t1 WHERE t1.eid=%d) WHEN %d THEN (SELECT rank FROM (SELECT eid,rank FROM evil_arena) as t1 WHERE t1.eid=%d) END WHERE eid IN (%d,%d)"
//#define SQL_EXCHANGE_ARENA	"UPDATE evil_arena SET rank=CASE eid WHEN %d THEN (SELECT t1.rank FROM (SELECT tmp.* FROM evil_arena tmp) AS t1 WHERE t1.eid=%d) WHEN %d THEN (SELECT t1.rank FROM (SELECT tmp.* FROM evil_arena tmp) AS t1 WHERE t1.eid=%d) END WHERE eid IN (%d,%d)"
#define SQL_EXCHANGE_ARENA		"UPDATE evil_arena AS ea1 JOIN evil_arena ea2 ON (ea1.eid=%d AND ea2.eid=%d) OR (ea1.eid=%d AND ea2.eid=%d) SET ea1.rank=ea2.rank,ea2.rank=ea1.rank"
int __exchange_arena_rank(MYSQL **pconn, char *q_buffer, int eid1, int eid2)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_EXCHANGE_ARENA, eid1, eid2, eid2, eid1);

	DEBUG_PRINT(0, "__exchange_arena_rank:q_buffer=[%s]", q_buffer);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "__exchange_arena_rank:mysql_errno %d\n", err);
	}

	ret = mysql_affected_rows(*pconn); 
	if (ret != 2) {
		ERROR_RETURN(-6, "__exchange_arena_rank:affected_row wrong %d", ret);
	}

	return 0;
}

int in_exchange_arena_rank(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;

	int eid_challenger;
	int eid_receiver;
	int rank_challenger;
	int rank_receiver;

	// 1. get rank data
	ret = sscanf(in_buffer, "%d %d", &eid_challenger, &eid_receiver);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_exchange_arena_rank:input_invalid");
	}

	eid_challenger = max(eid_challenger, -eid_challenger);
	eid_receiver = max(eid_receiver, -eid_receiver);

	ret = __get_arena_rank(pconn, q_buffer, eid_challenger, eid_receiver, rank_challenger, rank_receiver);
	DB_ER_RETURN(ret, "in_exchange_arena_rank:get_rank_error");

	if (rank_challenger <= 0 || rank_receiver <= 0) {
		DB_ER_RETURN(-15, "in_exchange_arena_rank:rank_error %d %d", rank_challenger, rank_receiver);
	}

	if (rank_challenger == rank_receiver) {
		DB_ER_RETURN(-7, "in_exchange_arena_rank:rank_same %d %d", rank_challenger, rank_receiver);
	}

	if (rank_challenger < rank_receiver) {
		// no need to exchange rank
		WARN_PRINT(-6, "in_exchange_arena_rank:rank_receiver_low %d %d", rank_challenger, rank_receiver);
		ret = 0;
		sprintf(out_buffer, "0");
		return ret;
	}

	ret = __exchange_arena_rank(pconn, q_buffer, eid_challenger, eid_receiver);
	DB_ER_RETURN(ret, "in_exchange_arena_rank:exchange_fail %d %d", eid_challenger, eid_receiver);

	sprintf(out_buffer, "0 %d %d %d %d", eid_challenger, eid_receiver, rank_receiver, rank_challenger);
	// out_buffer = [0] [eid_challenger] [eid_receiver] [rank_receiver] [rank_challenger]

	return 0;
}


typedef struct {
	int eid;
	int hero_id;
	int hp;
	int energy;
	int rank;
	int arena_times;
	int icon;
	char alias[EVIL_ALIAS_MAX+10];
} arena_data_t;

#define SQL_ARENA_DATA	"SELECT ea.eid, rank, arena_times, icon, alias, IFNULL(hero_id, 0), IFNULL(hp, 0), IFNULL(energy,0) FROM evil_arena ea INNER JOIN evil_user eu ON ea.eid=eu.eid LEFT JOIN evil_status es ON ea.eid=es.eid LEFT JOIN evil_hero eh ON ea.eid=eh.eid AND eh.hero_id IN (%d,%d) WHERE ea.eid IN (%d,%d) ORDER BY ea.rank ASC"
int __get_arena_data(MYSQL **pconn, char *q_buffer, int eid, int target_eid, int hero_id, int target_hero_id, arena_data_t &data1, arena_data_t &data2)
{
	int ret;
	int err;
	int len;
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;
	arena_data_t tmp_data;
	bzero(&tmp_data, sizeof(tmp_data));
	bzero(&data1, sizeof(data1));
	bzero(&data2, sizeof(data2));

	len = sprintf(q_buffer, SQL_ARENA_DATA
	, hero_id, target_hero_id, eid, target_eid);
	DEBUG_PRINT(0, "get_arena_data: q_buffer[%s]", q_buffer);

	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-15, "get_arena_data:mysql_errno %d", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "get_arena_data:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 2) {
		ret = -6;
		ERROR_PRINT(ret
		, "get_arena_data:num_row_error row[%d] eid[%d] target_eid[%d]"
		, num_row, eid, target_eid);
		goto cleanup;
	}

	// eid, rank, arena_times, icon, alias, hero_id, hp, energy
	field_count = mysql_field_count(*pconn);
	if (field_count != 8) {
		ret = -7;
		ERROR_PRINT(ret, "get_arena_data:field_count %d != 8"
		, field_count);
		goto cleanup;
	}

	for (int i = 0; i < num_row; i++) {
		row = mysql_fetch_row(result);
		if (row == NULL) {
			ret = -17;
			ERROR_PRINT(ret, "get_arena_data:row_data_null");
			goto cleanup;
		}
		tmp_data.eid 				= strtol_safe(row[0], -1);
		tmp_data.rank				= strtol_safe(row[1], -1);
		tmp_data.arena_times		= strtol_safe(row[2], -1);
		tmp_data.icon				= strtol_safe(row[3], -1);
		strcpy(tmp_data.alias, row[4]);
		// may be 0
		tmp_data.hero_id			= strtol_safe(row[5], -1);
		tmp_data.hp					= strtol_safe(row[6], -1);
		tmp_data.energy				= strtol_safe(row[7], -1);

		if (tmp_data.eid == eid) { 
			if (tmp_data.hero_id == hero_id) {
				data1 = tmp_data;
				continue;
			}
			data1.eid = tmp_data.eid;
			data1.rank = tmp_data.rank;
			data1.arena_times = tmp_data.arena_times;
			data1.icon = tmp_data.icon;
			strcpy(data1.alias, tmp_data.alias);
			data1.hero_id = hero_id;
			continue;
		}

		if (tmp_data.eid == target_eid) {
			if (tmp_data.hero_id == target_hero_id) {
				data2 = tmp_data;
				continue;
			}
			data2.eid = tmp_data.eid;
			data2.rank = tmp_data.rank;
			data2.arena_times = tmp_data.arena_times;
			data2.icon = tmp_data.icon;
			strcpy(data2.alias, tmp_data.alias);
			data2.hero_id = target_hero_id;
			continue;
		}

		ret = -16;
		ERROR_PRINT(ret
		, "get_arena_data:data error tmp_eid[%d] tmp_rank[%d]"
		, tmp_data.eid, tmp_data.rank);
		goto cleanup;
	}

	DEBUG_PRINT(0, "data1: eid[%d] hero_id[%d] hp[%d] energy[%d] rank[%d] arena_times[%d] icon[%d] alias[%s]", data1.eid, data1.hero_id, data1.hp, data1.energy, data1.rank, data1.arena_times, data1.icon, data1.alias);
	DEBUG_PRINT(0, "data2: eid[%d] hero_id[%d] hp[%d] energy[%d] rank[%d] arena_times[%d] icon[%d] alias[%s]", data2.eid, data2.hero_id, data2.hp, data2.energy, data2.rank, data1.arena_times, data2.icon, data2.alias);

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}

int in_arena_game(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;

	arena_data_t data_challenger;
	arena_data_t data_receiver;
	int eid_challenger;
	int hero_id_challenger;
	int eid_receiver;
	int hero_id_receiver;

	char deck1[EVIL_CARD_MAX+1];
	char deck2[EVIL_CARD_MAX+1];

	ret = sscanf(in_buffer, "%d %d", &eid_challenger, &eid_receiver);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_arena_game:input_invalid\n");
	}

	// 1. get deck
	ret = get_two_deck(pconn, q_buffer, deck1, deck2, eid_challenger, eid_receiver);
	if (ret != 0) {
		DB_ER_RETURN(-6, "in_arena_game:get_two_deck %d %d", eid_challenger, eid_receiver);
	}

	if (strlen(deck1) != EVIL_CARD_MAX || strlen(deck2) != EVIL_CARD_MAX) {
		DB_ER_RETURN(-15, "in_arena_game:deck_empty %d %d", eid_challenger, eid_receiver);
	}

	// 2. get deck2 hero_id
	hero_id_challenger = __get_hero(deck1);
	if (hero_id_challenger <= 0 || hero_id_challenger > HERO_MAX) {
		DB_ER_RETURN(-25, "in_arena_game:invalid_hero_id_challenger %d", hero_id_challenger);
	}
	hero_id_receiver = __get_hero(deck2);
	if (hero_id_receiver <= 0 || hero_id_receiver > HERO_MAX) {
		DB_ER_RETURN(-35, "in_arena_game:invalid_hero_id_receiver %d", hero_id_receiver);
	}

	// 3. get arena data
	ret = __get_arena_data(pconn, q_buffer, eid_challenger, eid_receiver
	, hero_id_challenger, hero_id_receiver, data_challenger, data_receiver);
	if (ret != 0) {
		DB_ER_RETURN(-26, "in_arena_game:get_arena_data_fail %d %d"
		, eid_challenger, eid_receiver);
	}

	if (data_challenger.arena_times <= 0) {
		ret = -36;
		DB_ER_RETURN(ret, "in_arena_game:arena_times <= 0 %d", data_challenger.arena_times);
	}

	// check target_rank is in target list
	int is_target = __is_arena_target(data_challenger.rank, data_receiver.rank);
	if (!is_target) {
		ret = -66;
		DB_ER_RETURN(ret, "in_arena_game:not_target %d %d"
		, data_challenger.rank, data_receiver.rank);
	}

	sprintf(out_buffer, "0 %d %d %d %d %s %s %d %d %d %d %d %d %d %d %.400s %.400s"
	, eid_challenger, eid_receiver, data_challenger.icon, data_receiver.icon
	, data_challenger.alias, data_receiver.alias
	, data_challenger.hero_id, data_receiver.hero_id
	, data_challenger.hp, data_receiver.hp
	, data_challenger.energy, data_receiver.energy
	, data_challenger.rank, data_receiver.rank
	, deck1, deck2);

	ret = 0;
	return ret;
}

#define SQL_UPDATE_ARENA_TIMES	"UPDATE evil_status SET arena_times=arena_times+(%d), gold=gold+(%d), crystal=crystal+(%d) WHERE eid=%d AND arena_times>=-(%d) AND gold>=-(%d) AND crystal>=-(%d)"
int in_update_arena_times(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;
	int eid;
	int offset;
	int gold;
	int crystal;

	ret = sscanf(in_buffer, "%d %d %d %d", &eid, &offset, &gold, &crystal);
	if (ret != 4) {
		DB_ER_RETURN(-5, "in_update_arena_times:input_invalid\n");
	}


	len = sprintf(q_buffer, SQL_UPDATE_ARENA_TIMES, offset, gold, crystal, eid, offset, gold, crystal);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-16, "in_update_arena_times:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		DB_ER_RETURN(-26, "in_update_arena_times:affected_row wrong %d", ret);
	}

	// because not use update_money(), so have to call record_buy() here
	if (gold != 0 || crystal != 0) {
		ret = record_buy(pconn, q_buffer, eid, EVIL_BUY_ARENA_TIMES, offset, gold, crystal, "buy_arena_times");
	}

	sprintf(out_buffer, "%d %d %d %d", eid, offset, gold, crystal);
	// out_buffer = [eid] [offset] [gold] [crystal]

	return 0;
}


#define SQL_GET_ARENA_RANK	"SELECT ea.eid,ea.rank,es.arena_last_reward_time FROM evil_arena ea LEFT JOIN evil_status es ON ea.eid=es.eid WHERE ea.eid=%d LIMIT 1"
int __get_arena_rank_time(MYSQL **pconn, char *q_buffer, int eid, int &rank, time_t &last_reward_time)
{
	int ret;
	int len;
	int err;

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;

	len = sprintf(q_buffer, SQL_GET_ARENA_RANK, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-15, "__get_arena_rank_time:mysql_errno %d", err);
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		ERROR_RETURN(-3, "__get_arena_rank_time:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row != 1) {
		ret = -25;
		ERROR_PRINT(ret, "__get_arena_rank_time:num_row_error %d %d", num_row, eid);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -17;
		ERROR_PRINT(ret, "__get_arena_rank_time:row_data_null");
		goto cleanup;
	}
//	eid = strtol_safe(row[0], -1);
	rank = strtol_safe(row[1], -1);
	last_reward_time = strtolong_safe(row[2], -1);
	if (rank <= 0) {
		ret = -7;
		ERROR_PRINT(ret, "__get_arena_rank_time:out_rank[%d] %d", rank, eid);
		goto cleanup;
	}
	if (last_reward_time < 0) {
		ret = -17;
		ERROR_PRINT(ret, "__get_arena_rank_time:out_last_reward_time %d", eid);
		goto cleanup;
	}
		
	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_GET_ARENA_REWARD	"UPDATE evil_status SET arena_last_reward_time=(%ld),gold=gold+(%d),crystal=crystal+(%d) WHERE eid=(%d)"
int in_get_arena_reward(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;
	int n;
	int eid;
	int rank;
	time_t last_reward_time;
	time_t now = time(NULL);
	time_t reward_time_offset;
	int has_reward;
	int reward_count;
	design_arena_reward_t reward_list[MAX_ARENA_REWARD];
	bzero(reward_list, sizeof(reward_list));
	design_arena_reward_t *target_reward;
	const char *in_ptr;

	ret = sscanf(in_buffer, "%d %d %n", &eid, &reward_count, &n);
	if (ret != 2) {
		DB_ER_RETURN(-5, "in_get_arena_reward:input_invalid");
	}
	in_ptr = in_buffer + n;
	for (int i = 0; i < reward_count; i++)
	{
		ret = sscanf(in_ptr, "%d %d %d %d %n", &reward_list[i].start
		, &reward_list[i].end, &reward_list[i].gold, &reward_list[i].crystal
		, &n);
		if (ret != 4) {
			DB_ER_RETURN(-15, "in_get_arena_reward:reward_data_invalid");
		}
		in_ptr += n;
	}

	ret = __get_arena_rank_time(pconn, q_buffer, eid, rank, last_reward_time);
	if (ret != 0) {
		DB_ER_RETURN(-25, "in_get_arena_reward:arena_rank_invalid");
	}

	has_reward = __check_arena_reward(last_reward_time, &reward_time_offset);
	if (!has_reward) {
		DB_ER_RETURN(-6, "in_get_arena_reward:no_reward");
	}

	DEBUG_PRINT(0, "in_get_arena_reward:rank=%d has_reward=%d", rank, has_reward);

	target_reward = NULL;
	for (int i = 0; i < reward_count; i++)
	{
		DEBUG_PRINT(0, "in_get_arena_reward:start=%d end=%d", reward_list[i].start, reward_list[i].end);
		if ((rank >= reward_list[i].start && rank <= reward_list[i].end) 
		|| (rank >= reward_list[i].start && reward_list[i].end == 0)) {
			target_reward = reward_list + i;
			break;
		}
	}

	if (target_reward == NULL) {
		DB_ER_RETURN(-35, "in_get_arena_reward:reward_data_error");
	}

	len = sprintf(q_buffer, SQL_GET_ARENA_REWARD, now, target_reward->gold
	, target_reward->crystal, eid);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-45, "in_get_arena_reward:mysql_errno %d\n", err);
	}

	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		DB_ER_RETURN(-16, "in_get_arena_reward:already_rewarded %d", eid);
	}

	sprintf(out_buffer, "%d %d %d %d", eid, rank, target_reward->gold
	, target_reward->crystal);
	// out_buffer = [eid] [rank] [gold] [crystal]

	return 0;
}

#define SQL_RESET_ARENA_TIMES	"UPDATE evil_status SET arena_times=%d"
int in_reset_arena_times(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int len;
	int err;

	len = sprintf(q_buffer, SQL_RESET_ARENA_TIMES, ARENA_TIMES_STD);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		DB_ER_RETURN(-25, "in_reset_arena_times:mysql_errno %d\n", err);
	}
	sprintf(out_buffer, "0");

	return 0;
}

//	arena
////////////////////////////////////////////////

int in_money_exchange(MYSQL **pconn, char *q_buffer, const char * in_buffer, char *out_buffer)
{
	int ret;
	int eid;
	int gold;
	int crystal;

	ret = sscanf(in_buffer, "%d %d %d", &eid, &gold, &crystal);
	if (ret != 3) {
		DB_ER_RETURN(-5, "in_money_exchange:input_invalid\n");
	}

	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	DB_ER_RETURN(ret, "in_money_exchange:update_money_fail %d %d", gold, crystal);

	sprintf(out_buffer, "%d %d %d", eid, gold, crystal);
	// out_buffer = [eid] [gold] [crystal]

	return 0;
}


// not yet implement
int in_notyet(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	sprintf(out_buffer, "-10 not_yet_implement");
	return -10;
}


int in_notyet_select(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	/*
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;
	len = sprintf(q_buffer, SQL, eid, start_num, page_size);
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		ERROR_RETURN(-65, "in_notyet_select:query err=%d", mysql_errno(*pconn)); 
	}

	result = mysql_store_result(*pconn);
	if (result==NULL) {
		DB_ER_RETURN(-23, "in_notyet_select:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		sprintf(out_buffer, "%d %d %d 0 0", eid, start_num, page_size);
		goto cleanup;
	}

	field_count = mysql_field_count(*pconn);
	if (field_count != 4) {
		ret = -7;
		DB_ER_PRINT(ret, "in_notyet_select:field_count_mismatch %d", field_count);
		goto cleanup;
	}
		
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ERROR_PRINT(-13, "in_notyet_select:row_null %d", eid);
		sprintf(out_buffer, "%d %d %d 0 0", eid, start_num, page_size);
		goto cleanup;
	}
	ptr = out_buffer;
	ptr += sprintf(ptr, "%d %d %d %d %d", eid, start_num, page_size
	, total, num_row);
	count = 0;	
	while ( NULL != (row = mysql_fetch_row(result)) ) {
		count++;
		if (count > num_row) {
			BUG_PRINT(-7, "in_notyet_select:fetch_row_overflow %d", count);
			break;
		}

		ptr += sprintf(ptr, " %s %s %s", row[0], row[1], row[2]);
	}

cleanup:
	mysql_free_result(result);
	return ret;
	*/

	
	sprintf(out_buffer, "-10 not_yet_implement");
	return -10;
}


int in_notyet_update(MYSQL **pconn, char *q_buffer, const char * in_buffer
, char *out_buffer)
{
	/*
	ret = my_query(pconn, q_buffer, len);
	if (ret != 0) {
		err = mysql_errno(*pconn);
		ERROR_RETURN(-5, "in_notyet_update:mysql_errno %d\n", err);
	}
	// check affected row
	ret = mysql_affected_rows(*pconn); 
	if (ret != 1) {
		ERROR_RETURN(-6, "in_notyet_update:affected_row wrong %d", ret);
	}

	*/
	
	sprintf(out_buffer, "-10 not_yet_implement");
	return -10;
}

// TODO: all in function, please do not move away from IN END
// OK: in_game  : load 2 decks (for eid>30) and return
// OK: in_win	: win / lose / draw  (async, no return)
// OK in_save_replay : save the game to replay
// NOT YET in_list_replay : list the replay base on eid
// NOT YET in_load_replay : load game replay (base on gameid or eid?)
// OK in_save_debug  : save debug info
// OK in_load_debug  : load debug info


//////////////////////////////////////////////////
//////////////////// IN END ] ////////////////////
//////////////////////////////////////////////////

typedef int (*in_worker_t) (MYSQL **, char*, const char *, char *); 


in_worker_t in_list[] = {
	in_test				// 0
,	in_register			// 1
,	in_login			// 2
,	in_load_card		// 3 
,	in_load_deck		// 4
///////////////////////////////
,	in_save_card		// 5
,	in_save_deck		// 6
,	in_alias			// 7
,	in_game				// 8
,	in_win				// 9
///////////////////////////////
,	in_save_replay		// 10
,	in_list_replay		// 11
,	in_load_replay		// 12
, 	in_save_debug		// 13
,	in_load_debug		// 14
///////////////////////////////
,	in_quick			// 15
,	in_status			// 16
,	in_buy_card			// 17
,	in_sell_card		// 18
,	in_cccard			// 19
///////////////////////////////
,	in_load_batch		// 20
,	in_save_batch		// 21
,	in_pick				// 22
,	in_add_exchange_piece	// 23
,	in_buy_exchange_piece	// 24
///////////////////////////////
, 	in_list_exchange	// 25
,	in_reset_exchange	// 26
,	in_list_guild		// 27	list guild
,	in_create_guild		// 28
,	in_delete_guild		// 29
///////////////////////////////
,	in_guild_apply		// 30
,	in_guild_pos		// 31
,	in_guild_quit		// 32
,	in_guild_lmember	// 33   list member inside guild
,	in_guild_deposit	// 34   deposit to guild gold
///////////////////////////////
,	in_guild_bonus		// 35
,	in_list_deposit		// 36		// list evil_guild_deposit
,	in_create_ladder	// 37		// create ladder
,	in_get_ladder		// 38		// get ladder
,	in_update_profile	// 39		// update user icon, sex, signature
///////////////////////////////
,	in_friend_add		// 40
,	in_friend_list		// 41
,	in_friend_sta		// 42
,	in_friend_search	// 43
,	in_guild			// 44
///////////////////////////////
,	in_deposit			// 45
,	in_glv				// 46
,	in_glevelup			// 47
,	in_load_piece		// 48
,	in_pick_piece		// 49
///////////////////////////////
,	in_merge_piece		// 50
,	in_pay				// 51
,	in_get_course		// 52
,	in_save_course		// 53
,	in_challenge		// 54	
///////////////////////////////
,	in_load_mission		// 55
,	in_save_mission		// 56
,	in_load_slot		// 57
,	in_save_slot		// 58
,	in_rename_slot		// 59
///////////////////////////////
,	in_buy_slot			// 60
,	in_mission_reward	// 61
,	in_slot_list		// 62
,	in_add_match		// 63
,	in_match_apply		// 64
//////////////////////////////
,	in_match_delete				// 65
,	in_match_team_init			// 66
,	in_update_match_player		// 67
,	in_match_eli_init			// 68
,	in_match_apply_ai			// 69
//////////////////////////////
,	in_update_match				// 70
,	in_friend_del				// 71
,	init_ranking_list			// 72
,	in_ranking_list				// 73
,	in_ranking_targets			// 74
//////////////////////////////
,	in_check_ranking_target		// 75
,	in_change_ranking_data		// 76
,	in_get_ranking_history		// 77
,	in_save_ranking_challenge	// 78
,	in_reset_ranking_time		// 79
//////////////////////////////
,	in_check_login				// 80
,	in_list_evil_message		// 81
,	in_read_evil_message		// 82
,	in_rank_reward				// 83
,	in_ranking_challenge		// 84
//////////////////////////////
,	in_add_evil_message			// 85
,	in_lottery					// 86
,	in_exchange_gift			// 87
,	in_gate						// 88
,	in_reset_fight_times		// 89
//////////////////////////////
,	in_update_gate_pos			// 90
,	in_fight					// 91
,	in_tower					// 92
,	in_tower_result				// 93
,	in_tower_info				// 94
//////////////////////////////
,	in_tower_buff				// 95
,	in_tower_ladder				// 96
,	in_tower_reward				// 97
,	in_solo						// 98
,	in_notyet					// 99
//////////////////////////////
,	in_fight_robot				// 100
,	in_update_signals			// 101
,	in_chapter					// 102
,	in_get_chapter				// 103
,	in_replace_chapter			// 104
//////////////////////////////
,	in_chapter_reward			// 105
,	in_load_hero_data			// 106
,	in_submit_hero_mission		// 107
,	in_update_hero_mission		// 108
,	in_load_card_piece			// 109
//////////////////////////////
,	in_load_hero_deck			// 110
,	in_list_hero_slot			// 111
,	in_get_hero_slot			// 112
,	in_insert_hero_slot			// 113
,	in_update_hero_slot			// 114
//////////////////////////////
,	in_choose_hero_slot			// 115
,	in_get_daily_login			// 116
,	in_get_daily_reward			// 117
,	in_update_piece_shop		// 118
,	in_get_piece_shop			// 119
//////////////////////////////
,	in_refresh_piece_shop		// 120
,	in_piece_buy				// 121
,	in_admin_add_robot			// 122
,	in_init_arena				// 123
,	in_arena_top_list			// 124
//////////////////////////////
,	in_arena_target				// 125
,	in_exchange_arena_rank		// 126
,	in_arena_game				// 127
,	in_update_arena_times		// 128
,	in_get_arena_reward			// 129
//////////////////////////////
,	in_money_exchange			// 130
,	in_reset_arena_times		// 131
,	in_guild_lsearch			// 132

};

const int MAX_WORKER = sizeof(in_list) / sizeof(in_worker_t);


// this is for testing
int dbin_once(const char *in_buffer, char *out_buffer)
{
	int cid, dbtype, n;
	int ret = -77;
	MYSQL *conn;
	char q_buffer[QUERY_MAX+1];

	conn = my_open();
	FATAL_EXIT(conn==NULL, "dbin_once:my_open");
	printf("dbin_once:  READ %s", in_buffer);
	ret = sscanf(in_buffer, "%d %d %n", &cid, &dbtype, &n);
	if (ret < 2) {
		// basic err:  cid=-1, dbtype=-1, code=-negative  END with:\n 
		sprintf(out_buffer, "-1 -1 -5 dbin_once:sscanf %d\n", ret);
		ret = -5; goto cleanup;
	}
	// printf("dbio in: cid=%d dbtype=%d  in_buffer+n=%s\n", cid, dbtype, in_buffer+n);
	if (cid == -9) { ret = -9; goto cleanup; }  // -9 is quit signal
	if (dbtype<0 || dbtype >= MAX_WORKER) {
		sprintf(out_buffer, "%d %d -88 dbin_once:invalid_dbtype\n", cid, dbtype);
		ret = -15;  goto cleanup; // invalid_dbtype
	}

	// core logic
	ret = in_list[dbtype](&conn, q_buffer, in_buffer+n, out_buffer);
	// printf("dbio out: cid=%d dbtype=%d  out_buffer=%s\n", cid, dbtype, out_buffer);

cleanup:
	my_close(conn);
	return ret;
}


static int char_to_short(unsigned char *num)
{
	return num[0] | (num[1] << 8);
}

// macro it ?
void short_to_char(unsigned char *num, int value)
{
	num[0] = value & 0xff;
	num[1] = (value >> 8) & 0xff;
}

// core logic:  dbio
// pthread entry
// open 
void * dbio(void * init_ptr)
{
	dbio_init_t * init = (dbio_init_t*)init_ptr;
	int fd = init->fd;
//	int max_trans = init->max_trans;
	// char ** db_buffer = init->db_buffer;
	int ret, n;
	int cid, dbtype;
	int buffer_index;
	MYSQL * conn;
	char out_buffer[DB_BUFFER_MAX + 1];
	char q_buffer[QUERY_MAX + 1]; // this is large!

//	INFO_PRINT(0, "dbio START %d", fd);

	mysql_thread_init();

	conn = my_open();
	if (conn == NULL) {
		BUG_PRINT(-3, "dbio:null_conn %d", fd);
		goto cleanup;
	}

	// new share memory style
	unsigned char num[2];
	buffer_index = 0;
	char *ptr;
	// printf("--- dbio:new_share_memory\n");
	while ( read(fd, num, 2) == 2 )  {
		buffer_index = char_to_short(num); // num[0] | (num[1] << 8);
		if (buffer_index==9999) {
			printf("dbio:quit_signal 9999, bye\n");
			break;
		}
		if (buffer_index < 0 || buffer_index >= DB_TRANS_MAX) {
			ERROR_PRINT(-2, "dbio:buffer_index_outbound %d", buffer_index);
		}
		ptr = init->db_buffer[buffer_index];
		// printf("dbio:db_buffer[%d]=%s", buffer_index, ptr);  // no \n is need

		cid = -1; dbtype = -1;
		ret = sscanf(ptr, "%d %d %n", &cid, &dbtype, &n);
		if (dbtype < 0 || dbtype>= MAX_WORKER) {
			BUG_PRINT(-105, "dbio:invalid_dbtype %d sscanf=%d", dbtype, ret);
			sprintf(ptr, "%d %d -105 dbio:invalid_dbtype", cid, dbtype);
			write(fd, num, 2); // signal back to main
			continue; // must signal by write() before next loop
		}

		// normal core logic
		// assert(ptr == init->db_buffer[buffer_index]);
		ret = in_list[dbtype](&conn, q_buffer, ptr+n, out_buffer);
		sprintf(ptr, "%d %d %s", cid, dbtype, out_buffer);
		ret = write(fd, num, 2); // signal back to main (no need to check, read will do)
		BUG_PRINT(2-ret, "dbio:write_2_bug");
	}

cleanup:
	INFO_PRINT(0, "dbio END %d", fd);
	if (conn != NULL) { 
		my_close(conn);
	}
	mysql_thread_end();
	close(fd);
	return NULL;
}


int dbio_init_server_socket(const char *path)
{ 
	int ret;
	int fd;
	struct sockaddr_un addr;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	FATAL_NEG_EXIT(fd, "dbio_init_server_socket:socket");
	INFO_PRINT(0, "dbio_init_server_socket:fd=%d", fd);

	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, path);
	unlink(path);	// lets clean up

	ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	FATAL_NEG_EXIT(ret, "dbio_init_server_socket:bind %s", path);

	ret = listen(fd, 10);  // we may make it larger if more threads
	FATAL_NEG_EXIT(ret, "dbio_init_server_socket:listen");

	return fd;
}

int dbio_init_client_socket(const char *path)
{
	int ret;
	int fd;
	struct sockaddr_un addr;

	fd  = socket(PF_UNIX, SOCK_STREAM, 0); 
	FATAL_NEG_EXIT(fd, "dbio_init_client_socket:fd=%d", fd);

	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, path);
	ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
	FATAL_NEG_EXIT(ret, "dbio_init_client_socket:fd=%d", fd);
	return fd;
}


//////////////////////////////////////////////////////
//////////////////// TEST START [ ////////////////////
//////////////////////////////////////////////////////

#ifdef TTT	

int test_reconn(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("please turn OFF and ON mysql (sleep now)\n");
	sleep(15);
	return 0;
}

int test0_register(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_register\n");
	int ret;
	char in_buffer[DB_BUFFER_MAX+1];
	// char out_buffer[DB_BUFFER_MAX+1];
	char *out_buffer = in_buffer; 
	int eid;
	char username[EVIL_USERNAME_MAX + 1];
	char password[EVIL_PASSWORD_MAX + 1];
	char alias[EVIL_ALIAS_MAX + 1];
	char gname[EVIL_ALIAS_MAX + 1];
	int lv, gold, crystal, res1, res2, glevel;
	int count, win, lose, draw, run, icon, exp, sex, course;
	char signature[EVIL_SIGNATURE_MAX + 1];
	double rating;
	int gate_pos;


	sprintf(in_buffer, "missing_pass_ip_alias");
	printf("----- note: the following error is NORMAL\n");
	ret = in_register(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -5) {
		ERROR_RETURN(ret, "missing_pass_alias test fail %d", ret);
	}

	sprintf(in_buffer, "_invalid_username pass 127.0.0.1:1111 0 0 whatever_alias");
	printf("----- note: the following error is NORMAL\n");
	ret = in_register(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -15) {
		ERROR_RETURN(ret, "_invalid_username test fail %d", ret);
	}

	sprintf(in_buffer, "valid_username pass 127.0.0.1:2222 0 0 _invalid_alias");
	printf("----- note: the following error is NORMAL\n");
	ret = in_register(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -15) {
		ERROR_RETURN(ret, "_invalid_alias test fail %d", ret);
	}

	sprintf(in_buffer, "user_without_alias pass 127.0.0.1:3333 0 0");
	ret = in_register(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "user_without_alias test fail %d", ret);
	if (ret == -6) {
		WARN_PRINT(ret, "duplicate:  please run: my < db-init.sql [%s]"
		, in_buffer );
	}
	ERROR_RETURN(ret, "user_without_alias fail in=%s  out=%s"
		, in_buffer, out_buffer);
	ret = sscanf(out_buffer, OUT_REGISTER_SCAN, &eid, username, password, alias
		, &lv, &rating, &gold, &crystal, &res1, &res2 
		, &count, &win, &lose, &draw, &run, &icon, &exp
		, &sex, &course, signature, gname, &glevel, &gate_pos
		);
	ERROR_RETURN(ret - 23, "user_without_alias sscanf %d", ret);
	if (strcmp(username, alias+1)!=0) {
		ERROR_RETURN(-7, "user_without_alias:  username=%s alias=%s"
		, username, alias);
	}
	


// check:
// SELECT * FROM evil_user LEFT JOIN evil_status ON evil_user.eid=evil_status.eid WHERE evil_user.username = 'normal_register';

	// special_user insert:  should be successful (need db-init.sql)
	sprintf(in_buffer, "normal_register pass 127.0.0.1:4444 normal_alias");
	ret = in_register(pconn, q_buffer, in_buffer, out_buffer);
	if (ret == -6) {
		WARN_PRINT(ret, "duplicate: please run: my < db-init.sql [%s]"
		, in_buffer);
	}
	ERROR_RETURN(ret, "test_register:normal_register fail");
	ret = sscanf(out_buffer, OUT_REGISTER_SCAN, &eid, username, password, alias
	, &lv, &rating, &gold, &crystal, &res1, &res2
	, &count, &win, &lose, &draw, &run, &icon, &exp, &sex, &course, signature
	, gname, &glevel, &gate_pos);
	ERROR_RETURN(ret - 23, "test_register:normal sscanf");
	printf("reg success: eid=%d username=%s password=%s alias=%s lv=%d rating=%lf gold=%d crystal=%d res1=%d res2=%d count=%d win=%d lose=%d draw=%d run=%d icon=%d exp=%d gname=%s sex=%d course=%d signature=%s glevel=%d gate_pos=%d\n"
	, eid, username, password, alias, lv, rating, gold, crystal, res1, res2
	, count, win, lose, draw, run, icon, exp, gname, sex, course, signature, glevel, gate_pos);

	return 0;
}

int test1_login(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_login\n");
	int ret;
	char in_buffer[DB_BUFFER_MAX+1];
	// char out_buffer[DB_BUFFER_MAX+1];
	char *out_buffer = in_buffer; 
	int eid;
	char username[EVIL_USERNAME_MAX + 1];
	char password[EVIL_PASSWORD_MAX + 1];
	char alias[EVIL_ALIAS_MAX + 1];
	int lv, gold, crystal, res1, res2;
	int count, win, lose, draw, run, icon, exp, sex, course;
	int fight_ai_time, fight_gold_time, fight_crystal_time;
	char signals[EVIL_SIGNAL_MAX + 1];
	char signature[EVIL_SIGNATURE_MAX + 1];
	double rating;
	int code;
	char msg[100];
	char gname[EVIL_ALIAS_MAX+1];
	int glevel;
	char card[EVIL_CARD_MAX+2]; // must at least +1
	int gate_pos;
	int chapter_pos;
	time_t monthly_end_date;
	int has_get_reward;
	int has_card;
	evil_user_t user;

	// password error
	sprintf(in_buffer, "x y 127.0.0.1:5678 1 1");
	ret = in_login(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -6) {
		ERROR_RETURN(-11, "test_login:wrong_pass fail %d %s", ret, out_buffer);
	}
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	if (ret != 2) {
		ERROR_RETURN(-12, "test_login:wrong_pass sscanf %d %s", ret, out_buffer);
	}
	if (code != -6) {
		ERROR_RETURN(-13, "test_login:wrong_pass code %d %s", code, out_buffer);
	}
	printf("wrong_pass out_buffer: %s\n", out_buffer);


	
	// success case: x x 
	sprintf(in_buffer, "x x 127.0.0.1:5678 1 1");
	ret = in_login(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_login:login_x fail");
	ret = sscanf(out_buffer, OUT_LOGIN_SCAN, &eid, username, password
	, alias , &lv, &rating, &gold, &crystal, &res1, &res2
	, &count, &win, &lose, &draw, &run, &icon, &exp, &sex, &course
	, &fight_ai_time, &fight_gold_time, &fight_crystal_time
	, signals, &monthly_end_date, signature , gname, &glevel, card
	, &gate_pos, &chapter_pos, &has_get_reward, &has_card);
	ERROR_RETURN(ret - 32, "test_login:login_x sscanf %d", ret);
	printf("login success: eid=%d username=%s password=%s alias=%s lv=%d rating=%lf gold=%d crystal=%d res1=%d res2=%d count=%d win=%d lose=%d draw=%d run=%d  icon=%d exp=%d gname=%s sex=%d course=%d fight_ai_time=%d, fight_gold_time=%d, fight_crystal_time=%d monthly_end_date=%ld signature=%s glevel=%d card=[%s] gate_pos=%d chapter_pos=%d has_get_reward=%d has_card=%d\n"
	, eid, username, password, alias, lv, rating, gold, crystal, res1, res2
	, count, win, lose, draw, run, icon, exp, gname, sex, course
	, fight_ai_time, fight_gold_time, fight_crystal_time
	, monthly_end_date, signature, glevel, card, gate_pos, chapter_pos
	, has_get_reward, has_card);

	sprintf(in_buffer, "user_without_alias pass 127.0.0.1:3333 0 0");
	ret = in_login(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_login:login user_without_alias fail");
	DEBUG_PRINT(0, "test_login:user_without_alias out_buffer=%s", out_buffer);
	ret = sscanf(out_buffer, OUT_LOGIN_SCAN, &eid, username, password
	, alias , &lv, &rating, &gold, &crystal, &res1, &res2
	, &count, &win, &lose, &draw, &run, &icon, &exp, &sex, &course
	, &fight_ai_time, &fight_gold_time, &fight_crystal_time
	, signals, &monthly_end_date, signature , gname, &glevel, card
	, &gate_pos, &chapter_pos, &has_get_reward, &has_card);
	ERROR_RETURN(ret - 32, "test_login:login user_without_alias sscanf %d", ret);
	printf("login user_without_alias success: eid=%d username=%s password=%s alias=%s lv=%d rating=%lf gold=%d crystal=%d res1=%d res2=%d count=%d win=%d lose=%d draw=%d run=%d  icon=%d exp=%d gname=%s sex=%d course=%d fight_ai_time=%d fight_gold_time=%d fight_crystal_time=%d monthly_end_date=%ld signature=%s glevel=%d card=[%s] gate_pos=%d chapter_pos=%d has_get_reward=%d has_card=%d\n"
	, eid, username, password, alias, lv, rating, gold, crystal, res1, res2
	, count, win, lose, draw, run, icon, exp, gname, sex, course
	, fight_ai_time, fight_gold_time, fight_crystal_time
	, monthly_end_date, signature, glevel, card, gate_pos, chapter_pos
	, has_get_reward, has_card);

	// test in_status
	eid = 550;
	ret = get_status(pconn, q_buffer, &user, eid);
	ERROR_RETURN(ret, "test_login:get_status");
	printf("get_status success: eid=%d lv=%d rating=%lf gold=%d crystal=%d gid=%d gpos=%d count=%d win=%d lose=%d draw=%d run=%d course=%d  card=[%s]\n"
	, user.eid, user.lv, user.rating, user.gold, user.crystal
	, user.gid, user.gpos, user.game_count, user.game_win, user.game_lose
	, user.game_draw, user.game_run, user.course
	, card);

	return 0;
}


int test2_load_card(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_load_card\n");
	int eid;
	int ret;
	int len;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char card[DB_BUFFER_MAX+1];
	int code;
	char msg[100];

	// load_nishaven: load the standard deck of (5)nishaven
	eid = 505;
	sprintf(in_buffer, IN_LOAD_CARD_PRINT, eid);
	ret = in_load_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_card:load_nishaven_error %s", out_buffer);
	ret = sscanf(out_buffer, OUT_LOAD_CARD_SCAN, &code, card);
	ERROR_RETURN(ret-2, "test_load_card:load_nishaven sscanf %d", ret);
	ERROR_RETURN(code-eid, "test_load_card:eid_mismatch %d %d", eid, code);
	printf("card [%s]\n", card);
	len = strlen(card);
	ERROR_RETURN(len - 400, "test_load_card:card_len_error %d", len);


	// load_invalid_eid 
	eid = -9;  // <= 0
	sprintf(in_buffer, IN_LOAD_CARD_PRINT, eid);
	printf("------ following error is NORMAL\n");
	ret = in_load_card(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -15) {  // -15 is invalid eid
		ERROR_RETURN(-21, "test_load_card:invalid_eid %d", ret);
	}
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(ret-2, "test_load_card:invalid_eid sscanf %d", ret);
	ERROR_RETURN(code-(-15), "test_load_card:invalid_eid out_buffer %s", out_buffer);
	printf("invalid_eid: %d %s\n", code ,msg);

	// no_input
	sprintf(in_buffer, "%s", "");
	printf("------ following error is NORMAL\n");
	ret = in_load_card(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -5) {  // -5 is invalid input (empty)
		ERROR_RETURN(-31, "test_load_card:no_input %d", ret);
	}
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(ret-2, "test_load_card:no_input sscanf %d", ret);
	ERROR_RETURN(code-(-5), "test_load_card:no_input out_buffer %s", out_buffer);
	printf("no_input: %d %s\n", code ,msg);

	
	// not_found  (any eid)
	eid = 9999;
	sprintf(in_buffer, "%d", eid);
	printf("------ following error is NORMAL\n");
	ret = in_load_card(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -6) {  // -6 is logic error, cannot find card of eid
		ERROR_RETURN(-41, "test_load_card:not_found %d", ret);
	}
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(ret-2, "test_load_card:not_found sscanf %d", ret);
	ERROR_RETURN(code-(-6), "test_load_card:not_found out_buffer %s", out_buffer);
	printf("not_found: %d %s\n", code ,msg);

	return 0;
}

int test3_save_card(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_save_card\n");
	int eid;
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	//char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	//char card[DB_BUFFER_MAX+1];
	int code;
	char msg[100];
	
	// init ranking_list before save_card
	// for save_card could change add new ranking record
	ret = init_ranking_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_save_card: init_ranking_list fail");

	// nocard test 
	eid = 553; // noalias user, also no card : must be in db-init.sql
	sprintf(in_buffer, "%d %.400s", eid, "0000100000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001");
	ret = in_save_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_save_card:nocard test fail");
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(ret - 2, "test_save_card:nocard sscanf %d  out=%s", ret, out_buffer);
	ERROR_RETURN(code, "test_save_card:nocard code err %s", out_buffer);


	// already : already_choose_job
	eid = 540; // peter has card, so it should be error
	sprintf(in_buffer, "%d %.400s", eid, "0000100000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001");
	printf("---- Follow error is Normal ----\n");
	ret = in_save_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_save_card:already ret=%d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(2-ret, "test_save_card:already sscanf ret=%d out=%s", ret, out_buffer);
	ERROR_RETURN(-6-code, "test_save_card:already code out=%s", out_buffer);
	printf("--- test_save_card:already: %s\n", out_buffer);


	// invalid_input (-5)
	sprintf(in_buffer, "invalid_input");
	printf("---- Follow error is Normal ----\n");
	ret = in_save_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-5-ret, "test_save_card:invalid_input %d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(2-ret, "test_save_card:invalid_input sscanf %d out=%s", ret, out_buffer);
	ERROR_RETURN(-5-code, "test_save_card:invalid_input code %d out=%s", code, out_buffer);
	printf("--- test_save_card:invalid_input: %s\n", out_buffer);
	

	// invalid_eid (-15)
	sprintf(in_buffer, "-1 invalid_eid");
	ret = in_save_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-15-ret, "test_save_card:invalid_input %d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(2-ret, "test_save_card:invalid_input sscanf %d out=%s", ret, out_buffer);
	ERROR_RETURN(-15-code, "test_save_card:invalid_input code %d out=%s", code, out_buffer);
	printf("--- test_save_card:invalid_input: %s\n", out_buffer);
	

	// invalid_card (-25)
	sprintf(in_buffer, "58 1000000");
	ret = in_save_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-25-ret, "test_save_card:invalid_card %d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(2-ret, "test_save_card:invalid_card sscanf %d out=%s", ret, out_buffer);
	ERROR_RETURN(-25-code, "test_save_card:invalid_card code %d out=%s", code, out_buffer);
	printf("--- test_save_card:invalid_card: %s\n", out_buffer);

	
	return 0;
}


int test4_query_card_list(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int len;
//	int ret;
	int eid;
	char card[EVIL_CARD_MAX+1];

	eid = 1005;
	len = sprintf(card, "%.400s", "0000100000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001");

	printf("card len = %d\n", len);


	len = __query_card_list("INSERT INTO evil_card", q_buffer, QUERY_MAX,
		eid, card, EVIL_CARD_MAX);

	ERROR_RETURN(len - (int)strlen(q_buffer), "test_query_card_list:len %d", len);

	printf("query: %s\n", q_buffer);

	return 0;
}

int test5_load_deck(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_load_deck\n");
	int eid;
	int slot;
	int ret;
	int len;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char name[EVIL_ALIAS_MAX +5];
	char card[DB_BUFFER_MAX+1];
	int code;
	char msg[100];

	// load_nishaven: load the standard deck of (5)nishaven
	eid = 505;
	sprintf(in_buffer, IN_LOAD_DECK_PRINT, eid);
	ret = in_load_deck(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_deck:load_nishaven_error %s", out_buffer);
	ret = sscanf(out_buffer, OUT_LOAD_DECK_SCAN, &code, &slot, name, card);
	ERROR_RETURN(ret-4, "test_load_deck:load_nishaven sscanf %d", ret);
	ERROR_RETURN(code-eid, "test_load_deck:eid_mismatch %d %d", eid, code);
	printf("card [%s]\n", card);
	len = strlen(card);
	ERROR_RETURN(len - 400, "test_load_deck:card_len_error %d", len);


	// load_invalid_eid 
	eid = -9;  // <= 0
	sprintf(in_buffer, IN_LOAD_CARD_PRINT, eid);
	printf("------ following error is NORMAL\n");
	ret = in_load_deck(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -15) {  // -15 is invalid eid
		ERROR_RETURN(-21, "test_load_deck:invalid_eid %d", ret);
	}
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(ret-2, "test_load_deck:invalid_eid sscanf %d", ret);
	ERROR_RETURN(code-(-15), "test_load_deck:invalid_eid out_buffer %s", out_buffer);
	printf("invalid_eid: %d %s\n", code ,msg);

	// no_input
	sprintf(in_buffer, "%s", "");
	printf("------ following error is NORMAL\n");
	ret = in_load_deck(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -5) {  // -5 is invalid input (empty)
		ERROR_RETURN(-31, "test_load_deck:no_input %d", ret);
	}
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(ret-2, "test_load_deck:no_input sscanf %d", ret);
	ERROR_RETURN(code-(-5), "test_load_deck:no_input out_buffer %s", out_buffer);
	printf("no_input: %d %s\n", code ,msg);

	
	// not_found  (any eid)
	eid = 9999;
	sprintf(in_buffer, "%d", eid);
	printf("------ following error is NORMAL\n");
	ret = in_load_deck(pconn, q_buffer, in_buffer, out_buffer);
	if (ret != -6) {  // -6 is logic error, cannot find card of eid
		ERROR_RETURN(-41, "test_load_deck:not_found %d", ret);
	}
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(ret-2, "test_load_deck:not_found sscanf %d", ret);
	ERROR_RETURN(-6 - code, "test_load_deck:not_found out_buffer %s", out_buffer);
	printf("not_found: %d %s\n", code ,msg);

	return 0;
}

int test6_save_deck(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_save_deck\n");
	int eid;
	int slot;
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	//char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	//char card[DB_BUFFER_MAX+1];
	int code;
	char msg[100];
	char card[EVIL_CARD_MAX+5];

	// normal test
	eid = 550; // normal user : must be in db-init.sql
	slot = 1;
	sprintf(in_buffer, IN_SAVE_DECK_PRINT, eid, slot, "0000100000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
	ret = in_save_deck(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_save_deck:normal test fail");
	ret = sscanf(out_buffer, OUT_SAVE_DECK_SCAN, &eid, &slot, card);
	ERROR_RETURN(ret - 3, "test_save_deck:normal sscanf %d  out=%s", ret, out_buffer);
	ERROR_NEG_RETURN(eid, "test_save_deck:normal  out=%s", out_buffer);
	printf("-- out deck : %d [%s]\n", eid, card);

	eid = 553; // invalid card count 'a'
	slot = 1;
	// TODO check duplicate hero ?
	// nio has checked duplicate hero
	sprintf(in_buffer, IN_SAVE_DECK_PRINT, eid, slot, "0000a00000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
	printf("----- 2 errors are normal\n");
	ret = in_save_deck(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-25-ret, "test_save_deck:invalid_count test fail out=%s"
	, out_buffer);
	ret = sscanf(out_buffer, "%d %80[^\n]", &code, msg);
	ERROR_RETURN(2-ret, "test_save_deck:invalid_count sscanf %d", ret);
	ERROR_RETURN(-25-code, "test_save_deck:invalid_count test fail2 out=%s"
	, out_buffer);
	printf("code %d  msg=[%s]\n", code, msg);
	
	// TODO invalid input, eid, duplicate(yes)
	return 0;
}

int test7_alias(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int eid;
	int ret;
	char alias[EVIL_ALIAS_MAX+5];
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	//char card[DB_BUFFER_MAX+1];
	int code;
	char msg[100];


	// duplicate test  (must before normal noalias test)
	// must have a record: 553, 'noalias', '1', '_noalias' 
	// and 'normal' alias is used by 550
	eid = 553;  strcpy(alias, "normal");
	sprintf(in_buffer, IN_ALIAS_PRINT, eid, alias);  // order is important
	printf("--------- this error is normal for testcase\n");
	ret = in_alias(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_alias:duplicate test fail out=%s", out_buffer);
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(2-ret, "test_alias:duplicate sscanf ret=%d out=%s", ret, out_buffer);
	ERROR_RETURN(-6-code, "test_alias:duplicate code %s", out_buffer);


	// normal case: noalias
	// must have a record: 53, 'noalias', '1', '_noalias'
	eid = 553;  strcpy(alias, "yesalias");
	sprintf(in_buffer, IN_ALIAS_PRINT, eid, alias);
	ret = in_alias(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_alias:noalias test fail");
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(ret - 2, "test_alias:noalias sscanf %d  out=%s", ret, out_buffer);
	ERROR_NEG_RETURN(code, "test_alias:noalias code err %s", out_buffer);

	
	eid = 552; strcpy(alias, "\b\\'%%_*?^@()`~\"\a/-!\b@#$%%\t+=][.,\"");
	printf("------ alias : [%s]\n", alias);
	sprintf(in_buffer, IN_ALIAS_PRINT, eid, alias);
	ret = in_alias(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_alias:special_str test fail");
	ret = sscanf(out_buffer, "%d %80s", &code, msg);

	// 550 is normal user with alias
	eid = 550;  strcpy(alias, "verynewalias");
	sprintf(in_buffer, IN_ALIAS_PRINT, eid, alias);
	printf("--------- normal error for testcase\n");
	ret = in_alias(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-16-ret, "test_alias:already test fail out=%s", out_buffer);
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(2-ret, "test_alias:already sscanf ret=%d out=%s", ret, out_buffer);
	ERROR_RETURN(-16-code, "test_alias:already code %s", out_buffer);
	printf("test_alias:already out=%s\n", out_buffer);

	return 0;
}


int test8_game(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_game\n");
	int ret;
	int eid1, eid2;
	int out_eid1, out_eid2;
	char deck1[EVIL_CARD_MAX + 5];
	char deck2[EVIL_CARD_MAX + 5];
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int code = -1;
	char msg[100];

	strcpy(out_buffer, "bug");
	
	eid1 = 545;   eid2 = 546;
	sprintf(in_buffer, IN_GAME_PRINT, eid1, eid2);
	ret = in_game(pconn, q_buffer, in_buffer, out_buffer);

	ERROR_RETURN(ret, "test_game:normal ret=%d  out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, OUT_GAME_SCAN, &out_eid1, deck1, &out_eid2, deck2);
	ERROR_RETURN(4-ret, "test_game:normal sscanf ret=%d  out=%s", ret, out_buffer);

	printf("test_game:normal out=%s\n", out_buffer);
	printf("test_game:normal out_eid1=%d  out_eid2=%d  strlen(deck1)=%zd  strlen(deck2)=%zd\n", out_eid1, out_eid2, strlen(deck1), strlen(deck2));


	// eid1_not_found 
	eid1 = 555;   eid2 = 545;
	sprintf(in_buffer, IN_GAME_PRINT, eid1, eid2);
	printf("------- normal error\n");
	ret = in_game(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_game:eid1_not_found ret=%d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(2-ret, "test_game:eid1_not_found sscanf %d out=%s", ret, out_buffer);
	ERROR_RETURN(-6-code, "test_game:eid1_not_found code out=%s", out_buffer);


	// both_not_found   555=nocard, 556=no_user(no_card)
	eid1 = 555;   eid2 = 556;
	sprintf(in_buffer, IN_GAME_PRINT, eid1, eid2);
	printf("------- normal error\n");
	ret = in_game(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_game:both_not_found ret=%d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %80s", &code, msg);
	ERROR_RETURN(2-ret, "test_game:both_not_found sscanf %d out=%s", ret, out_buffer);
	ERROR_RETURN(-6-code, "test_game:both_not_found code out=%s", out_buffer);

	// test AI case
	eid1=550;  eid2=1; // AI warrior
	sprintf(in_buffer, IN_GAME_PRINT, eid1, eid2);
	ret = in_game(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_game:ai ret=%d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, OUT_GAME_SCAN, &out_eid1, deck1, &out_eid2, deck2);
	ERROR_RETURN(4-ret, "test_game:ai sscanf %d out=%s", ret, out_buffer);
	ret = strcmp(deck2, "AI");
	ERROR_RETURN(ret, "test_game:ai deck2 not AI %s", deck2);
	printf("test_game:ai  out_eid1=%d  deck1=[%s]\n", out_eid1, deck1);
	printf("test_game:ai  out_eid2=%d  deck2=[%s]\n", out_eid2, deck2);

	return 0;
}


int test9_win(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_win\n");
	int ret;
	int eid1, eid2;
	int gold1, gold2, crystal1, crystal2;
	int exp1, exp2, lv1, lv2;
	int card_id1, card_id2;
	int ai_times1, ai_times2;
	int gold_times1, gold_times2;
	int crystal_times1, crystal_times2;
	double rating ;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int code = -1;
	char msg[100];

	ai_times1 = ai_times2 = 0;
	gold_times1 = gold_times2 = 0;
	crystal_times1 = crystal_times2 = 0;
// #define IN_WIN_PRINT	"%d %lf %d %d %d %d %d %d" // winner,rating,eid1,eid2,gold1, crystal1,gold2,crystal2
	// test 50 win 51(winner=1)  or  51 win 50 (winner=2)
	eid1=550; eid2=551; gold1=6; crystal1=3; gold2=1; crystal2=2; 
	exp1=100; lv1=0; exp2=40; lv2=0; rating=10.0;
	card_id1 = 0; card_id2 = 0;
	sprintf(in_buffer, IN_WIN_PRINT, 1, rating, eid1, eid2, gold1, crystal1
	, gold2, crystal2, exp1, lv1, exp2, lv2, card_id1, card_id2
	, ai_times1, ai_times2, gold_times1, gold_times2
	, crystal_times1, crystal_times2);
	ret = in_win(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_win:normal ret=%d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %s", &code, msg);
	ERROR_RETURN(2-ret, "test_win:normal sscanf ret=%d out=%s", ret, out_buffer);


	// test 52, 53 draw  (winner=9)    rating auto change to 0.0
	eid1=545; eid2=546; gold1=1; crystal1=2; gold2=3; crystal2=4; 
	exp1=700; lv1=1; exp2=700; lv2=0; rating=5.0;
	card_id1 = 23; card_id2 = 24;
	sprintf(in_buffer, IN_WIN_PRINT, 9, rating, eid1, eid2, gold1, crystal1
	, gold2, crystal2, exp1, lv1, exp2, lv2, card_id1, card_id2
	, ai_times1, ai_times2, gold_times1, gold_times2
	, crystal_times1, crystal_times2);
	ret = in_win(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_win:draw ret=%d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %s", &code, msg);
	ERROR_RETURN(2-ret, "test_win:draw sscanf ret=%d out=%s", ret, out_buffer);

	// test negative gold 
	eid1=545; eid2=546; gold1=-1; crystal1=2; gold2=3; crystal2=4; 
	exp1=700; lv1=1; exp2=700; lv2=0; rating=5.0;
	card_id1 = 23; card_id2 = 24;
	sprintf(in_buffer, IN_WIN_PRINT, 9, rating, eid1, eid2, gold1, crystal1
	, gold2, crystal2, exp1, lv1, exp2, lv2, card_id1, card_id2
	, ai_times1, ai_times2, gold_times1, gold_times2
	, crystal_times1, crystal_times2);
	ret = in_win(pconn, q_buffer, in_buffer, out_buffer);
//	ERROR_RETURN(-15-ret, "test_win:neg_gold ret=%d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %s", &code, msg);
	ERROR_RETURN(2-ret, "test_win:draw sscanf ret=%d out=%s", ret, out_buffer);
	return 0;
}

int test10_save_replay(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_save_replay\n");
	int ret;
	long game_id;
	int game_type;
	int winner;
	int star;
	int seed;
	int start_side;
	int ver;
	int eid1, eid2;
	int lv1, lv2;
	int icon1, icon2;
	char alias1[EVIL_ALIAS_MAX+1];
	char alias2[EVIL_ALIAS_MAX+1];
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int code = -1;
	char msg[100];
	char deck1[EVIL_CARD_MAX+5];
	char deck2[EVIL_CARD_MAX+5];
	char param[REPLAY_PARAM_MAX+5];
	char cmd[EVIL_CMD_MAX * 2+5];  // prepare for overflow

// game_id, winner, seed, ver, eid1, eid2, deck1[400], deck2[400], cmd[2000]

	game_id = 150715182701001L;
	game_type = GAME_CHAPTER;
	winner = 1;
	star = 3;
	seed = 234;
	start_side = 1;
	ver = 3397;
	eid1=501; eid2=502; 
	lv1 = 9; lv2 = 10; 
	icon1=0; icon2=0;
	sprintf(alias1, "w>a?r_-*1~^");
	sprintf(alias2, "w'a@r\"2");

	// @see nio.cpp game_param_string()  logic.lua game_param_split()
	// param = count(7), game_flag, type_list, ai_max_ally, hp1, hp2, energy1, energy2
	sprintf(param, GAME_PARAM_PRINT, 1001, "000001111111", 0, 15, 12, 99, 99);
	strcpy(deck1, "1 1000000000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
	strcpy(deck2, "12 1 23 24 28 37 65 26 22 26 22 26 22");
	strcpy(cmd, "s 1201;n;s 2201;n;fold");
	// "%ld %d %d %d %d %d %d %d %d %d %d %d %.30s %.30s %.400s %.400s %.200s %.4000s"
	sprintf(in_buffer, IN_SAVE_REPLAY_PRINT
	, game_id, game_type, winner, star, seed
	, start_side, ver, eid1, eid2
	, lv1, lv2, icon1, icon2, alias1
	, alias2, deck1, deck2, param, cmd);

	ret = in_save_replay(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_save_replay:normal ret=%d out=%s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %s", &code, msg);
	ERROR_RETURN(2-ret, "test_save_replay:normal sscanf ret=%d out=%s", ret, out_buffer);

	printf("--- test_save_replay: out=%s\n", out_buffer);
	
	return 0;
}

int test11_save_debug(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_save_debug\n");
	int ret;
	int eid;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int code = -1;
	char msg[100];
	char filename[100 + 5];
	char content[4000 + 5];

	
	eid = 551; strcpy(filename, "bug123.txt");  strcpy(content, "bug_content");
	sprintf(in_buffer, IN_SAVE_DEBUG_PRINT, eid, filename, content);
	ret = in_save_debug(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_save_debug:normal %d %s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %s", &code, msg);
	ERROR_RETURN(2-ret, "test_save_debug:normal sscanf %d %s", ret, out_buffer);
	ERROR_RETURN(code, "test_save_debug:code %d %s", code, out_buffer);

	return 0;
}

int test12_load_debug(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_load_debug\n");
	char filename[100];
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
//	int code = -1;
//	char msg[100];
	int ret;
	int eid;
	char content[4000+5];

	strcpy(filename, "bug-test.txt"); // must have in db-init.sql
	sprintf(in_buffer, IN_LOAD_DEBUG_PRINT, filename);
	ret = in_load_debug(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_debug:normal %d %s", ret, out_buffer);
	ret = sscanf(out_buffer, OUT_LOAD_DEBUG_SCAN, &eid, filename, content);
	ERROR_RETURN(3-ret, "test_load_debug:normal sscanf %d %s", ret, out_buffer);
	printf("-- test_load_debug: eid=%d filename=%s content=%s\n"
	, eid, filename, content);

	return 0;
}

int test13_buy_card(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_buy_card\n");
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int ret;
	int eid;
	int card_id;
	int card_type;
	int money_type; // 0=gold, 1=crystal
	int buy_count;
	int gold;
	int crystal;

	// buy card
	eid = 547;
	card_id = 23;
	card_type = 0;
	money_type = 0;
	buy_count = 1;
	gold = -100;
	crystal = 0;

	sprintf(in_buffer, IN_BUY_CARD_PRINT, eid, card_id, card_type
	, money_type, buy_count, gold, crystal);
	ret = in_buy_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_buy_card:normal %d", ret);
	printf("test13_buy_card:out_buffer=%s\n", out_buffer);


	// buy piece
	eid = 547;
	card_id = 23;
	card_type = 1;
	money_type = 0;
	buy_count = 1;
	gold = -100;
	crystal = 0;

	sprintf(in_buffer, IN_BUY_CARD_PRINT, eid, card_id, card_type
	, money_type, buy_count, gold, crystal);
	ret = in_buy_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_buy_card:normal %d", ret);
	printf("test13_buy_card:out_buffer=%s\n", out_buffer);

	return 0;
}


int test14_sell_card(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_buy_card\n");
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int ret;
	int eid;
	int card_id;
	int card_type;
	int money_type; // 0=gold, 1=crystal
	int sell_count;
	int gold_sell;
	int crystal_sell;

	// sell card not enough
	eid = 547;
	card_id = 23;
	card_type = 0;
	money_type = 0;
	sell_count = 2;
	gold_sell = 100;
	crystal_sell = 0;

	sprintf(in_buffer, IN_SELL_CARD_PRINT, eid, card_id, card_type
	, money_type, sell_count, gold_sell, crystal_sell);
	ret = in_sell_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-62 - ret, "test_sell_card:sell_card_normal %d", ret);
	DEBUG_PRINT(0, "test14_sell_card:sell_card_out_buffer=%s", out_buffer);

	// sell card
	eid = 547;
	card_id = 23;
	card_type = 0;
	money_type = 0;
	sell_count = 1;
	gold_sell = 100;
	crystal_sell = 0;

	sprintf(in_buffer, IN_SELL_CARD_PRINT, eid, card_id, card_type
	, money_type, sell_count, gold_sell, crystal_sell);
	ret = in_sell_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_sell_card:sell_card_normal %d", ret);
	DEBUG_PRINT(0, "test14_sell_card:sell_card_out_buffer=%s", out_buffer);

	// sell piece
	eid = 547;
	card_id = 23;
	card_type = 1;
	money_type = 0;
	sell_count = 1;
	gold_sell = 100;
	crystal_sell = 0;

	sprintf(in_buffer, IN_SELL_CARD_PRINT, eid, card_id, card_type
	, money_type, sell_count, gold_sell, crystal_sell);
	ret = in_sell_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_sell_card:sell_piece_normal %d", ret);
	DEBUG_PRINT(0, "test14_sell_card:sell_piece_out_buffer=%s", out_buffer);

	return 0;
}



int test15_cccard(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	printf("test_cccard\n");
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int code = -1;
	char msg[100];
	int ret;
	int eid, card_id, count;

	
	eid = 550;  card_id = 22;  count = 4; // was 2 : db-init.sql
	sprintf(in_buffer, IN_CCCARD_PRINT, eid, card_id, count);
	ret = in_cccard(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_cccard:normal %d %s", ret, out_buffer);
	ret = sscanf(out_buffer, "%d %80[^\n]", &code, msg);
	ERROR_RETURN(2-ret, "test_ccard:normal sscanf %s", out_buffer);
	printf("--- cccard out=%s\n", out_buffer);
	
	// TODO check error for outbound: card_id, count

	return 0;
}

int test16_load_batch(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) {
	printf("test load_batch()\n");

	int ret;
	int code;
	char msg[100];
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int ptype;

	eid = 547;
	ptype = 0;

	sprintf(in_buffer, "%d %d", eid, ptype);
	ret = in_load_batch(pconn, q_buffer, in_buffer, out_buffer);
	DEBUG_PRINT(0, "test_load_batch:out=%s\n", out_buffer);

	ret = sscanf(out_buffer, "%d %80[^\n]", &code, msg);
	ERROR_RETURN(2-ret, "test_load_batch:sscanf %d %s", ret, out_buffer);
	if (code < 0) {
		WARN_PRINT(code, "test_load_batch:msg=%s", msg);
		return 0;
	}
	if (code == 0) {
		DEBUG_PRINT(code, "test_load_batch:msg=%s", msg);
		return 0;
	}
	DEBUG_PRINT(code, "test_load_batch:msg=%s", msg);

	return 0;


}

int test17_save_batch(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) {
	int ret;
	int eid ;
	int ptype;
	int card_list[MAX_LOC] = { 21, 22, 23, 24, 25, 26};
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;


	eid = 550;   ptype = 0;
	sprintf(in_buffer, IN_SAVE_BATCH_PRINT, eid, ptype, card_list[0], card_list[1], card_list[2]
	, card_list[3], card_list[4], card_list[5], 0, 0);
	ret = in_save_batch(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(ret, "test_save_batch:normal1 %s", out_buffer);
	printf("test_save_batch 550 0: %s\n", out_buffer);

	eid = 550;   ptype = 1;	card_list[0]=99;
	sprintf(in_buffer, IN_SAVE_BATCH_PRINT, eid, ptype, card_list[0], card_list[1], card_list[2]
	, card_list[3], card_list[4], card_list[5], 0, 0);
	ret = in_save_batch(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(ret, "test_save_batch:normal2 %s", out_buffer);
	printf("test_save_batch 550 1: %s\n", out_buffer);

	eid = 550;   ptype = 0;	card_list[0]=88;
	sprintf(in_buffer, IN_SAVE_BATCH_PRINT, eid, ptype, card_list[0], card_list[1], card_list[2]
	, card_list[3], card_list[4], card_list[5], 0, 0);
	ret = in_save_batch(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(ret, "test_save_batch:normal3 %s", out_buffer);
	printf("test_save_batch 550 0(again): %s\n", out_buffer);

	return 0;
}

int test18_update_money(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) {
	int ret;
	int eid;
	int gold;
	int crystal;

	eid = 550;
	gold = -5000;
	crystal = 0;
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	ERROR_RETURN(ret+16, "test_update_money:not_enough_gold");

	gold = 0;
	crystal = -1000;
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	ERROR_RETURN(ret+16, "test_update_money:not_enough_crystal");

	gold = -1;
	crystal = -1;
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	ERROR_RETURN(ret, "test_update_money:normal1");

	gold = -99;
	crystal = -99;
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	ERROR_RETURN(ret, "test_update_money:normal2");

	gold = 99;
	crystal = 99;
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	ERROR_RETURN(ret, "test_update_money:normal3");

	gold = 1;
	crystal = 1;
	ret = update_money(pconn, q_buffer, eid, gold, crystal);
	ERROR_RETURN(ret, "test_update_money:normal4");

	return ret;
}

int test19_pick(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) {
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid;
	int batch_type;
	int loc;
	int card_id;
	int gold;
	int crystal;

	eid = 547;   
	batch_type = 0;
	loc = 0;
	card_id = 24;
	gold = -20;
	crystal = 0;
	sprintf(in_buffer, IN_PICK_PRINT, eid, batch_type, loc, card_id 
	, gold, crystal);
	ret = in_pick(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(ret, "test_pick:normal %s", out_buffer);
	DEBUG_PRINT(ret, "test_pick:%s\n", out_buffer);
	ret = 0;


	return ret;
}

int test20_add_exchange(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) {
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid;
	int card_id;
	int gold, crystal;
	int count;
	char name[35];

	// normal case
	eid = 547;
	card_id = 30; // puwen, must have 2
	count = 2;
	gold = 200;  crystal = 0;
	// strcpy(name, "狂战士 【人类】");
	strcpy(name, "无双剑姬 c【人类】");
	ret = sprintf(in_buffer, IN_ADD_EXCHANGE_PRINT, eid, card_id, count, gold
	, crystal, name);
	ret = in_add_exchange_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(ret, "test_add_exchange:normal %s", out_buffer);
	DEBUG_PRINT(ret, "test_add_exchange:%s\n", out_buffer);

	/*
	// failure case
	eid = 550;
	card_id = 190; // no card case (voice of winter, not in warrior deck)
	gold = 5000;  crystal = 0;
	// strcpy(name, "狂战士 【人类】");
	strcpy(name, "冰晶节杖【法师】【牧师】");
	ret = sprintf(in_buffer, IN_ADD_EXCHANGE_PRINT, eid, card_id, gold
	, crystal, name);
	printf("------ normal error:\n");
	ret = in_add_exchange(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(-6-ret, "test_add_exchange:no_card %s", out_buffer);
	DEBUG_PRINT(ret, "test_add_exchange:no_card %s\n", out_buffer);

	// normal case
	eid = 550;
	card_id = 30; // puwen, must have 2
	gold = 200;  crystal = 0;
	// strcpy(name, "狂战士 【人类】");
	strcpy(name, "无双剑姬 c【人类】");
	ret = sprintf(in_buffer, IN_ADD_EXCHANGE_PRINT, eid, card_id, gold
	, crystal, name);
	ret = in_add_exchange(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(ret, "test_add_exchange:normal %s", out_buffer);
	DEBUG_PRINT(ret, "test_add_exchange:%s\n", out_buffer);
	*/

	return ret;
}


int test21_list_exchange(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int start_id, page_size;
	char name[35];


	start_id = 0;  page_size = 5;
	strcpy(name, ""); // "狂战士 【人类】"
	ret = sprintf(in_buffer, IN_LIST_EXCHANGE_PRINT, start_id, page_size, name);
	ret = in_list_exchange(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(ret, "test_list_exchange:normal %s", out_buffer);
	DEBUG_PRINT(ret, "test_list_exchange:%s\n", out_buffer);

	return ret;
}

int test22_buy_exchange(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid;
	long xcid;
	int count;

	/*
	eid = 540;  // peter(540) buy normal(550)
	xcid = 14022415003366;  // @see db-init.sql c26 (not now() record)
	// update card count of c26=9
	ret = update_card_count(pconn, q_buffer, eid, 26, 9);
	ERROR_NEG_RETURN(ret, "test_buy_xc:update_card_count");
	sprintf(in_buffer, IN_BUY_EXCHANGE_PRINT, eid, xcid);
	printf("------- normal error\n");
	ret = in_buy_exchange(pconn, q_buffer, in_buffer, out_buffer);
	// -2 is normal
	ERROR_RETURN(-2-ret, "test_buy_xc:card_count_overflow");
	*/
	
	eid = 547;  // peter(540) buy normal(550)
	xcid = 14022415003366;  // @see db-init.sql (not now() record)
	count = 3;
	sprintf(in_buffer, IN_BUY_EXCHANGE_PRINT, eid, xcid, count);
	ret = in_buy_exchange_piece(pconn, q_buffer, in_buffer, out_buffer);
	return ret;
}


int test23_reset_exchange(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	long ts;
	// non-thread-safe!  all exchange related DB must be in one-thread
	ts = ptimestamp();
	if ((ts % 100) >= 99) {
		ERROR_RETURN(-2, "test_reset_exchange:too_many_exchange");
	}
	
	sprintf(in_buffer, IN_RESET_EXCHANGE_PRINT, ts);
	ret = in_reset_exchange(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_reset_exchange:normal");


	return 0;
}





// TODO swap test25, test26  because test25 assign gid to noguild
int test24_check_create_guild(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) {
	int ret;
	int eid;
	char gname[EVIL_ALIAS_MAX + 5];

	// case 1: noguild, gname ok
	eid = 556;  strcpy(gname, "lowguild");
	ret = check_create_guild(pconn, q_buffer, eid, gname);
	ERROR_RETURN(ret, "test_check_create_guild:normal error");
	

	// case 2: normal has guild, gname oK
	eid = 550; strcpy(gname, "lowguild");
	printf("----- following error is normal\n");
	ret = check_create_guild(pconn, q_buffer, eid, gname);
	ERROR_RETURN(-6 - ret, "test_check_create_guild:has_guild error");


	// case 3: noguild,  gname duplicate
	eid = 556; strcpy(gname, "normalguild");
	printf("----- following error is normal\n");
	ret = check_create_guild(pconn, q_buffer, eid, gname);
	ERROR_RETURN(-22 - ret, "test_check_create_guild:duplicate_gname error");

	return 0;
}

int test25_update_status_guild(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) {
	int ret;
	int eid, gid, gpos;
	int check_gid;
	int check_pos;

	// update 'noalias2' user (552) to be the master of his guild
	gid = 552;  eid = 552;  gpos=GUILD_POS_MASTER;  check_gid = 0; check_pos=0;
	ret = update_status_guild(pconn, q_buffer, gid, gpos, eid
	, check_gid, check_pos);
	ERROR_RETURN(ret, "test_update_status_guild error");

	gid = 0;  eid = 552;  gpos=GUILD_POS_MASTER;  check_gid = 552; check_pos=GUILD_POS_MASTER;
	ret = update_status_guild(pconn, q_buffer, gid, gpos, eid
	, check_gid, check_pos);
	ERROR_RETURN(-25-ret, "test_update_status_guild leave guild fail");

	gid = 0;  eid = 552;  gpos=0;  check_gid = 552; check_pos=GUILD_POS_MASTER;
	ret = update_status_guild(pconn, q_buffer, gid, gpos, eid
	, check_gid, check_pos);
	ERROR_RETURN(ret, "test_update_status_guild leave guild ok");
	
	return 0;
}


int test26_create_guild(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int gold;
	int crystal;
	char gname[30];

	// case 1: x money not enough
	eid = 547; gold = 999999; crystal = 0;   sprintf(gname, "newguild");
	sprintf(in_buffer, IN_CREATE_GUILD_PRINT, eid, gold, crystal, gname);
	printf("----- following error is normal\n");
	ret = in_create_guild(pconn, q_buffer, in_buffer, out_buffer);
	printf("test26:out_buffer=%s\n", out_buffer);
	ERROR_RETURN(-2-ret, "test_create_guild");

	// case 1: normal,  x create a guild called gogogo
	eid = 547; gold = 100; crystal = 0;    sprintf(gname, "gogogo");
	sprintf(in_buffer, IN_CREATE_GUILD_PRINT, eid, gold, crystal, gname);
	ret = in_create_guild(pconn, q_buffer, in_buffer, out_buffer);
	printf("test26:out_buffer=%s\n", out_buffer);
	ERROR_RETURN(ret, "test_create_guild");

	// case 2: duplicate gname,  y has no guild
	eid = 548; gold = 100; crystal = 0;   sprintf(gname, "gogogo");
	sprintf(in_buffer, IN_CREATE_GUILD_PRINT, eid, gold, crystal, gname);
	printf("----- following error is normal\n");
	ret = in_create_guild(pconn, q_buffer, in_buffer, out_buffer);
	printf("test26:out_buffer=%s\n", out_buffer);
	ERROR_RETURN(-22-ret, "test_create_guild");

	// case 3: x has guild, gname OK
	eid = 547; gold = 100; crystal = 0;   sprintf(gname, "newguild");
	sprintf(in_buffer, IN_CREATE_GUILD_PRINT, eid, gold, crystal, gname);
	printf("----- following error is normal\n");
	ret = in_create_guild(pconn, q_buffer, in_buffer, out_buffer);
	printf("test26:out_buffer=%s\n", out_buffer);
	ERROR_RETURN(-6-ret, "test_create_guild");

	return 0;
}


int test27_list_gmember(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int gid;
	int flag;
	int start_id;
	int page_size;


	// case 1 : normal list all (flag=0)(gid = 550)
	gid = 550; flag = 0; start_id = 0; page_size = 3;
	sprintf(in_buffer, IN_LIST_GMEMBER_PRINT, flag, start_id, page_size, gid);
	ret = in_guild_lmember(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:all");
	printf("out_buffer : %s\n", out_buffer);

	gid = 550; flag = 0; start_id = 3; page_size = 5;
	sprintf(in_buffer, IN_LIST_GMEMBER_PRINT, flag, start_id, page_size, gid);
	ret = in_guild_lmember(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:all");
	printf("out_buffer : %s\n", out_buffer);

	gid = 550; flag = 1; start_id = 0; page_size = 10;  // list member without apply
	sprintf(in_buffer, IN_LIST_GMEMBER_PRINT, flag, start_id, page_size, gid);
	ret = in_guild_lmember(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:member_without_apply");
	printf("out_buffer : %s\n", out_buffer);

	gid = 550; flag = 9; start_id = 0; page_size = 10; // list apply only
	sprintf(in_buffer, IN_LIST_GMEMBER_PRINT, flag, start_id, page_size, gid);
	ret = in_guild_lmember(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:apply");
	printf("out_buffer : %s\n", out_buffer);

	// case 2:  noguild
	gid = 556; flag = 0; start_id = 0; page_size = 10;
	sprintf(in_buffer, IN_LIST_GMEMBER_PRINT, flag, start_id, page_size, gid);
	printf("----------- following error is normal\n");
	ret = in_guild_lmember(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:noguild");
	printf("out_buffer : %s\n", out_buffer);

	return 0;
}


int test28_list_guild(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int start_id, page_size;
	char key[EVIL_ALIAS_MAX + 5];

	
	// case 1 : normal
	start_id = 0;   page_size = 10; key[0] = 0;
	sprintf(in_buffer, IN_LIST_GUILD_PRINT, start_id, page_size, key);
	ret = in_list_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_guild:normal");
	printf("out_buffer=%s\n", out_buffer);

	// case 2 : start_id negative < 0
	start_id = -1;   page_size = 10; key[0] = 0;
	sprintf(in_buffer, IN_LIST_GUILD_PRINT, start_id, page_size, key);
	printf("----------- following error is normal\n");
	ret = in_list_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-15-ret, "test_list_guild:invalid_start_id");
	printf("out_buffer=%s\n", out_buffer);

	// case 3 : page_size = 0
	start_id = 1;   page_size = 0; key[0] = 0;
	sprintf(in_buffer, IN_LIST_GUILD_PRINT, start_id, page_size, key);
	printf("----------- following error is normal\n");
	ret = in_list_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-25-ret, "test_list_guild:invalid_page_size");
	printf("out_buffer=%s\n", out_buffer);

	// case 4 : start_id = 10  page_size = 10  (empty output)
	start_id = 10;   page_size = 10;  key[0] = 0;
	sprintf(in_buffer, IN_LIST_GUILD_PRINT, start_id, page_size, key);
	ret = in_list_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_guild:empty");
	printf("out_buffer=%s\n", out_buffer);

	// case 5 : start_id = 1  page_size = 1  (one record)
	start_id = 1;   page_size = 1;  key[0] = 0;
	sprintf(in_buffer, IN_LIST_GUILD_PRINT, start_id, page_size, key);
	ret = in_list_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_guild:one_one");
	printf("out_buffer=%s\n", out_buffer);
	return 0;
}


int test29_guild_apply(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	// input
	int eid, gid;

	// case 1 : normal  nocard(555) join normalguild(550)
	eid = 555;  gid = 550;
	sprintf(in_buffer, IN_GUILD_APPLY_PRINT, gid, eid);
	ret = in_guild_apply(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_guild_apply:normal %d %d", gid, eid);
	printf("out: %s\n", out_buffer);


	// case 2:  guild_not_exist (-3)    noguild join noguild
	eid = 556; gid = 556;
	sprintf(in_buffer, IN_GUILD_APPLY_PRINT, gid, eid);
	ret = in_guild_apply(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-3-ret, "test_guild_apply:guild_not_exist %d %d", gid, eid);
	printf("out: %s\n", out_buffer);

	// case 3:  already_has_guild (-6)   normal1 join normalguild
	eid = 551; gid = 550;  
	sprintf(in_buffer, IN_GUILD_APPLY_PRINT, gid, eid);
	ret = in_guild_apply(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_guild_apply:already_has_guild %d %d", gid, eid);
	printf("out: %s\n", out_buffer);

	return 0;
}

int test30_guild_member_change(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	// input
	int gid, offset, member_max;


	gid = 546;  offset = 1;  member_max = 20;// www(546) has a guild with 1 member
	ret = guild_member_change(pconn, q_buffer, gid, offset, member_max);
	ERROR_RETURN(ret, "test_guild_member_change +1");

	gid = 546;  offset = -1; member_max = 20;
	ret = guild_member_change(pconn, q_buffer, gid, offset, member_max);
	ERROR_RETURN(ret, "test_guild_member_change -1");

	return 0;
}


int test31_guild_approve(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	// input
	int gid, eid, member_max;

	// case 1 : normal approve
	eid = 557;  gid=550; member_max = 20;// apply  become normal member in normalguild
	ret = guild_approve(pconn, q_buffer, gid, eid, GUILD_POS_SENIOR, member_max);
	ERROR_RETURN(ret, "test_guild_approve:normal");

	// case 2:   approve again
	eid = 557;  gid=550; member_max = 20;// apply  already become GUILD_POS_MEMBER
	ret = guild_approve(pconn, q_buffer, gid, eid, GUILD_POS_MEMBER, member_max);
	ERROR_RETURN(-6-ret, "test_guild_approve:again");


	// case 3:  guild_not_exist  (same as MAX_MEMBER reached?)
	eid = 558;  gid = 554; member_max = 20;// no such guild
	ret = guild_approve(pconn, q_buffer, gid, eid, GUILD_POS_MEMBER, member_max);
	ERROR_RETURN(-2-ret, "test_guild_approve:no_such_guild");

	return 0;
}

// note: guild_pos use guild_approve, guild_promote
int test32_guild_pos(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	// input:
	int eid, pos, gid, member_max;
	
	// case 1:  mismatch gid (558 apply2 on 550, but gid=545)
	eid = 558;    pos = GUILD_POS_MEMBER;  gid = 545;	member_max = 15;
	sprintf(in_buffer, IN_GUILD_POS_PRINT, eid, pos, gid, member_max);
	ret = in_guild_pos(pconn, q_buffer, in_buffer , out_buffer);
	ERROR_RETURN(-6-ret, "test_guild_pos:mismatch_gid");
	printf("out %s\n", out_buffer);

	// case 2:  normal  558 apply2 on 550
	eid = 558;    pos = GUILD_POS_MEMBER;  gid = 550;	member_max = 15;
	sprintf(in_buffer, IN_GUILD_POS_PRINT, eid, pos, gid, member_max);
	ret = in_guild_pos(pconn, q_buffer, in_buffer , out_buffer);
	ERROR_RETURN(ret, "test_guild_pos:normal");
	printf("out %s\n", out_buffer);

	// case 3:  promote  558 apply2 on 550
	eid = 558;    pos = GUILD_POS_SENIOR;  gid = 550;	member_max = 15;
	sprintf(in_buffer, IN_GUILD_POS_PRINT, eid, pos, gid, member_max);
	ret = in_guild_pos(pconn, q_buffer, in_buffer , out_buffer);
	ERROR_RETURN(ret, "test_guild_pos:promote");
	printf("out %s\n", out_buffer);

	// case 3:  promote  558 apply2 on 550
	eid = 558;    pos = GUILD_POS_APPLY;  gid = 550;	member_max = 15;
	sprintf(in_buffer, IN_GUILD_POS_PRINT, eid, pos, gid, member_max);
	ret = in_guild_pos(pconn, q_buffer, in_buffer , out_buffer);
	ERROR_RETURN(-25-ret, "test_guild_pos:invalid_pos");
	printf("out %s\n", out_buffer);


	return 0;
}

int test33_guild_quit(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	// input:
	int eid;
	int gid;
	int member_max;

	// e2(532) member of (lll)546 guild glll
	eid = 532; gid = 546; member_max = 20;
	sprintf(in_buffer, IN_GUILD_QUIT_PRINT, eid, gid, member_max);
	ret = in_guild_quit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_guild_quit:normal");
	printf("out %s\n", out_buffer);

	// apply(557) member
	eid = 557; gid = 550; member_max = 20;
	sprintf(in_buffer, IN_GUILD_QUIT_PRINT, eid, gid, member_max);
	ret = in_guild_quit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_guild_quit:apply");
	printf("out %s\n", out_buffer);

	// normal(550) master
	eid = 550; gid = 550; member_max = 20;
	sprintf(in_buffer, IN_GUILD_QUIT_PRINT, eid, gid, member_max);
	printf("--------------- follow error id normal\n");
	ret = in_guild_quit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-9-ret, "test_guild_quit:master");
	printf("out %s\n", out_buffer);

	// nosuch_user(554) 
	eid = 554; gid = 550; member_max = 20;
	sprintf(in_buffer, IN_GUILD_QUIT_PRINT, eid, gid, member_max);
	printf("--------------- follow error id normal\n");
	ret = in_guild_quit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-55-ret, "test_guild_quit:no_such_user");
	printf("out %s\n", out_buffer);

	// no_guild(556) 
	eid = 556; gid = 0; member_max = 20;
	sprintf(in_buffer, IN_GUILD_QUIT_PRINT, eid, gid, member_max);
	printf("--------------- follow error id normal\n");
	ret = in_guild_quit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-25-ret, "test_guild_quit:no_guild");
	printf("out %s\n", out_buffer);

	return 0;
}


// order is important, after this:  glll
int test34_guild_delete(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	// input:
	int gid;

	gid = 546;
	sprintf(in_buffer, IN_DELETE_GUILD_PRINT, gid);
	ret = in_delete_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_guild_delete:normal");
	printf("out %s\n", out_buffer);

	gid = 546;  // again
	sprintf(in_buffer, IN_DELETE_GUILD_PRINT, gid);
	ret = in_delete_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-3-ret, "test_guild_delete:again");
	printf("out %s\n", out_buffer);

	return 0;
}

int test35_get_guild_gold(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	// input:
	int gid;
	int gold;
	int crystal;

	gid = 550;
	ret = get_guild_gold(pconn, q_buffer, gid);
	ERROR_NEG_RETURN(ret, "test_guild_gold:error");
	printf("ret %d\n", ret);
	
	gid = 545;
	ret = get_guild_gold(pconn, q_buffer, gid);
	ERROR_NEG_RETURN(ret, "test_guild_gold:error");
	printf("ret %d\n", ret);

	gid = 545; gold = -20000; crystal = 5;
	ret = update_guild_money(pconn, q_buffer, gid, gold, crystal);
	ERROR_NEG_RETURN(-6-ret, "test_update_guild_money:error");
	printf("ret %d\n", ret);

	gid = 545; gold = 20; crystal = 5;
	ret = update_guild_money(pconn, q_buffer, gid, gold, crystal);
	ERROR_NEG_RETURN(ret, "test_update_guild_money:normal");
	printf("ret %d\n", ret);

	return 0;
}

int test36_guild_deposit(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	// input:
	int gid;
	int eid;
	int gold;

	eid=551; gid=550; gold=100;
	sprintf(in_buffer, IN_GUILD_DEPOSIT_PRINT, eid, gid, gold); 
	ret = in_guild_deposit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(ret, "test_guild_deposit:normal1");
	printf("out %s\n", out_buffer);

	eid=545; gid=545; gold=30;
	sprintf(in_buffer, IN_GUILD_DEPOSIT_PRINT, eid, gid, gold); 
	ret = in_guild_deposit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_NEG_RETURN(ret, "test_guild_deposit:gwww");
	printf("out %s\n", out_buffer);

	return 0;
}


int test37_insert_guild_share(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	// input:
	int gid, eid;
	double gshare;
	time_t last_bonus;
	
	gid = 545;  eid=531; gshare = 0.1;  last_bonus = time(NULL);
	ret = insert_guild_share(pconn, q_buffer, gid, eid
, gshare, last_bonus);
	ERROR_NEG_RETURN(ret, "test_insert_guild_share:normal e1");
	printf("ret = %d\n", ret);

	gid = 545;  eid=532; gshare = 0.2;  last_bonus = 0;
	ret = insert_guild_share(pconn, q_buffer, gid, eid
, gshare, last_bonus);
	ERROR_NEG_RETURN(ret, "test_insert_guild_share:normal e1");
	printf("ret = %d\n", ret);
	return 0;
}


// select unix_timestamp(date_format(now(), '%Y-%m-%d 00:00:00'));
// 2014-03-12 00:00:00 = unix 1394553600
int test38_get_guild_share(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	// input:
	int eid;
	share_t share;

	bzero(&share, sizeof(share));

	eid = 551;  // master (0.8)
	ret = get_guild_share(pconn, q_buffer, eid, &share);
	ERROR_RETURN(ret, "test_get_guild_share:normal");
	printf("share: eid=%d  gid=%d  gshare=%lf  last_bonus=%ld\n"
	, share.eid, share.gid, share.gshare, share.last_bonus);

	return 0;
}


int test39_update_last_bonus(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	// input:
	int eid;
	time_t last_bonus;

	eid = 545;  last_bonus = time(NULL);
	ret = update_last_bonus(pconn, q_buffer, eid, last_bonus);
	ERROR_RETURN(ret, "test_update_last_bonus:normal");

	return 0;
}

int test40_guild_bonus(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	// input:
	int eid;
	double rate;
	rate = 0.05;
	int get_flag;

	// note:  normal1 already get bonus
	eid=551; // normal1
	get_flag = 1;
	sprintf(in_buffer, IN_GUILD_BONUS_PRINT, eid, rate, get_flag);
	ret = in_guild_bonus(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_guild_bonus:normal1_already_get_bonus");
	printf("out %s\n", out_buffer);

	eid=559; // member (got yesterday)
	get_flag = 1;
	sprintf(in_buffer, IN_GUILD_BONUS_PRINT, eid, rate, get_flag);
	ret = in_guild_bonus(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_guild_bonus:member");
	printf("out %s\n", out_buffer);


	eid = 551; // normal1  get_flag=0
	get_flag = 0; // read only
	sprintf(in_buffer, IN_GUILD_BONUS_PRINT, eid, rate, get_flag);
	ret = in_guild_bonus(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_guild_bonus:get_flag=0");
	printf("out %s\n", out_buffer);

	return 0;
}

int test41_update_last_login(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	//input
	int eid;
	time_t last_login;
	int platform;

	eid=550; last_login=time(NULL); platform = 0;
	ret = update_last_login(pconn, q_buffer, eid, last_login, platform);
	ERROR_RETURN(ret, "test_update_last_login:normal");

	eid=550; last_login=0;
	ret = update_last_login(pconn, q_buffer, eid, last_login, platform);
	ERROR_RETURN(-15-ret, "test_update_last_login:error");

	return 0;
}

int test42_save_guild_deposit(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	// input
	int eid;
	int gid;
	int gold;
	int crystal;

	eid = 550; gid = 550; gold = 300; crystal = 0;
	ret = save_guild_deposit(pconn, q_buffer, eid, gid, gold, crystal);
	ERROR_RETURN(ret, "test_save_guild_deposit:normal");

	return 0;
}


int test43_list_deposit(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	// input
	int start_id, page_size, gid;

	start_id = 0; page_size = 3; gid = 550;
	sprintf(in_buffer, IN_LIST_DEPOSIT_PRINT, gid, start_id, page_size);
	ret = in_list_deposit(pconn, q_buffer, in_buffer,out_buffer);
	ERROR_RETURN(ret, "test_list_deposit:normal");
	printf("out = %s\n", out_buffer);

	start_id = 3; page_size = 3; gid = 550;
	sprintf(in_buffer, IN_LIST_DEPOSIT_PRINT, gid, start_id, page_size);
	ret = in_list_deposit(pconn, q_buffer, in_buffer,out_buffer);
	ERROR_RETURN(ret, "test_list_deposit:normal_page2");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test44_create_ladder(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	ret = in_create_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_ladder:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test45_get_ladder(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int ladder_type;
	int eid;
	int gid;

	ladder_type = LADDER_RATING; eid = 547; gid = 0;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_rating:normal");
	printf("rating_out = %s\n", out_buffer);

	ladder_type = LADDER_LEVEL; eid = 547; gid = 0;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_level:normal");
	printf("level_out = %s\n", out_buffer);

	ladder_type = LADDER_GUILD; eid = 547; gid = 550;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_guild:normal");
	printf("guild_out = %s\n", out_buffer);

	ladder_type = LADDER_COLLECTION; eid = 547; gid = 0;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_collection:normal");
	printf("collection_out = %s\n", out_buffer);

	ladder_type = LADDER_GOLD; eid = 547; gid = 0;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_gold:normal");
	printf("gold_out = %s\n", out_buffer);

	/////

	ladder_type = LADDER_RATING; eid = 99999; gid = 0;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_rating:normal");
	printf("rating_out = %s\n", out_buffer);

	ladder_type = LADDER_LEVEL; eid = 99999; gid = 0;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_level:normal");
	printf("level_out = %s\n", out_buffer);

	ladder_type = LADDER_GUILD; eid = 99999; gid = 99999;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_guild:normal");
	printf("guild_out = %s\n", out_buffer);

	ladder_type = LADDER_COLLECTION; eid = 99999; gid = 0;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_collection:normal");
	printf("collection_out = %s\n", out_buffer);

	ladder_type = LADDER_GOLD; eid = 99999; gid = 0;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_gold:normal");
	printf("gold_out = %s\n", out_buffer);

	// chapter
	ladder_type = LADDER_CHAPTER; eid = 547; gid = 0;
	sprintf(in_buffer, IN_GET_LADDER_PRINT, ladder_type, eid, gid);
	ret = in_get_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_ladder_gold:normal");
	printf("chapter_out = %s\n", out_buffer);

	return 0;
}

int test46_list_replay(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;

	eid = 501;
	sprintf(in_buffer, IN_LIST_REPLAY_PRINT, eid);
	ret = in_list_replay(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_replay:normal");
	INFO_PRINT(0, "test46:out_buffer=[%s]", out_buffer);

	return 0;
}

int test47_load_replay(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	long gameid;

	gameid = 150715182701001L;
	sprintf(in_buffer, IN_LOAD_REPLAY_PRINT, gameid);
	ret = in_load_replay(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_replay:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test48_update_profile(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int icon;
	int sex;
	char signature[EVIL_SIGNATURE_MAX+1];

	eid = 547;
	icon = 12;
	sex = 0;
	sprintf(signature, "%.300s", "myprecious!");
	sprintf(in_buffer, IN_UPDATE_PROFILE_PRINT, eid, icon, sex, signature);
	ret = in_update_profile(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_save_profile:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test49_friend_add(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid1;
	int eid2;

	eid1=547; eid2=548;
	sprintf(in_buffer, IN_FRIEND_ADD_PRINT, eid1, eid2);
	ret = in_friend_add(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_add_friend:normal");
	printf("out = %s\n", out_buffer);

	eid1=549; eid2=547;
	sprintf(in_buffer, IN_FRIEND_ADD_PRINT, eid1, eid2);
	ret = in_friend_add(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_add_friend:normal");
	printf("out = %s\n", out_buffer);

	eid1=547; eid2=550;
	sprintf(in_buffer, IN_FRIEND_ADD_PRINT, eid1, eid2);
	ret = in_friend_add(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_add_friend:normal");
	printf("out = %s\n", out_buffer);

	eid1=547; eid2=551;
	sprintf(in_buffer, IN_FRIEND_ADD_PRINT, eid1, eid2);
	ret = in_friend_add(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_add_friend:normal");
	printf("out = %s\n", out_buffer);

	eid1=547; eid2=547;
	sprintf(in_buffer, IN_FRIEND_ADD_PRINT, eid1, eid2);
	ret = in_friend_add(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-15-ret, "test_add_friend:same_eid");
	printf("out = %s\n", out_buffer);

	eid1=547; eid2=548;
	sprintf(in_buffer, IN_FRIEND_ADD_PRINT, eid1, eid2);
	ret = in_friend_add(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_add_friend:already_friend");
	printf("out = %s\n", out_buffer);

	eid1=548; eid2=547;
	sprintf(in_buffer, IN_FRIEND_ADD_PRINT, eid1, eid2);
	ret = in_friend_add(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_add_friend:already_friend");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test50_friend_list(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int start_num;
	int page_size;
	char alias[EVIL_ALIAS_MAX+1];

	eid=547;
	start_num=0;
	page_size=10;
	sprintf(in_buffer, IN_FRIEND_LIST_PRINT, eid, start_num, page_size, "");
	ret = in_friend_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_list:normal");
	printf("out = %s\n", out_buffer);

	eid=547;
	start_num=0;
	page_size=2;
	sprintf(in_buffer, IN_FRIEND_LIST_PRINT, eid, start_num, page_size, "");
	ret = in_friend_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_list:normal");
	printf("out = %s\n", out_buffer);

	eid=547;
	start_num=2;
	page_size=2;
	sprintf(in_buffer, IN_FRIEND_LIST_PRINT, eid, start_num, page_size, "");
	ret = in_friend_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_list:normal");
	printf("out = %s\n", out_buffer);

	eid=547;
	start_num=4;
	page_size=2;
	sprintf(in_buffer, IN_FRIEND_LIST_PRINT, eid, start_num, page_size, "");
	ret = in_friend_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_list:normal");
	printf("out = %s\n", out_buffer);

	eid=555;
	start_num=2;
	page_size=2;
	sprintf(in_buffer, IN_FRIEND_LIST_PRINT, eid, start_num, page_size, "");
	ret = in_friend_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_list:normal");
	printf("out = %s\n", out_buffer);

	eid=547;
	start_num=0;
	page_size=10;
	sprintf(alias, "%s", "y");
	sprintf(in_buffer, IN_FRIEND_LIST_PRINT, eid, start_num, page_size, alias);
	ret = in_friend_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_list:normal");
	printf("out = %s\n", out_buffer);

	eid=547;
	start_num=0;
	page_size=10;
	sprintf(alias, "%s", "normal");
	sprintf(in_buffer, IN_FRIEND_LIST_PRINT, eid, start_num, page_size, alias);
	ret = in_friend_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_list:normal");
	printf("out = %s\n", out_buffer);

	eid=547;
	start_num=0;
	page_size=10;
	sprintf(alias, "%s", "550");
	sprintf(in_buffer, IN_FRIEND_LIST_PRINT, eid, start_num, page_size, alias);
	ret = in_friend_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_list:normal");
	printf("out = %s\n", out_buffer);


	return 0;
}

int test51_friend_sta(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int my_eid;
	int eid;

	my_eid=550; eid=547; 
	sprintf(in_buffer, IN_FRIEND_STA_PRINT, my_eid, eid);
	ret = in_friend_sta(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_sta:normal");
	printf("out = %s\n", out_buffer);

	my_eid=550; eid=548;
	sprintf(in_buffer, IN_FRIEND_STA_PRINT, my_eid, eid);
	ret = in_friend_sta(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_sta:normal");
	printf("out = %s\n", out_buffer);

	my_eid=547; eid=548;
	sprintf(in_buffer, IN_FRIEND_STA_PRINT, my_eid, eid);
	ret = in_friend_sta(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_sta:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test52_friend_search(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char alias[EVIL_ALIAS_MAX+1];

	sprintf(alias, "app");
	sprintf(in_buffer, IN_FRIEND_SEARCH_PRINT, alias);
	ret = in_friend_search(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_search:normal");
	printf("out = %s\n", out_buffer);

	sprintf(alias, "xyz");
	sprintf(in_buffer, IN_FRIEND_SEARCH_PRINT, alias);
	ret = in_friend_search(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_friend_search:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}


int test53_guild(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int gid = 550;

	
	gid = 550;
	sprintf(in_buffer, IN_GUILD_PRINT, gid, "   ");
	ret = in_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_guild:normal");
	printf("out = %s\n", out_buffer);

	gid = 888;
	sprintf(in_buffer, IN_GUILD_PRINT, gid, "");
	ret = in_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-13-ret, "test_guild:not_found");
	printf("out = %s\n", out_buffer);

	gid = 550;
	sprintf(in_buffer, IN_GUILD_PRINT, gid, "no news is good news 1234567890|1234567890|1234567890|1234567890|1234567890|1234567890|1234567890|abcdefgxyz|1234567890|");
	ret = in_guild(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_guild:not_found");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test54_deposit(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int gid;

	eid=550; gid=550;
	sprintf(in_buffer, IN_DEPOSIT_PRINT, eid, gid);
	ret = in_deposit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_deposit:normal");
	printf("out = %s\n", out_buffer);

	eid=551; gid=550;
	sprintf(in_buffer, IN_DEPOSIT_PRINT, eid, gid);
	ret = in_deposit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_deposit:normal");
	printf("out = %s\n", out_buffer);

	eid=547; gid=547;
	sprintf(in_buffer, IN_DEPOSIT_PRINT, eid, gid);
	ret = in_deposit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_deposit:normal");
	printf("out = %s\n", out_buffer);

	eid=888; gid=550;
	sprintf(in_buffer, IN_DEPOSIT_PRINT, eid, gid);
	ret = in_deposit(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_deposit:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test55_glv(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int gid;

	gid=550;
	sprintf(in_buffer, IN_GLV_PRINT, gid);
	ret = in_glv(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_glv:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test56_glevelup(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int gid, glevel, gold;

	gid=550;  glevel=2;  gold=1000;
	sprintf(in_buffer, IN_GLEVELUP_PRINT, gid, glevel, gold);
	ret = in_glevelup(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_glevelup:normal");
	printf("out = %s\n", out_buffer);

	gid=550;  glevel=3;  gold=10000;
	sprintf(in_buffer, IN_GLEVELUP_PRINT, gid, glevel, gold);
	printf("----- following error is normal\n");
	ret = in_glevelup(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-65-ret, "test_glevelup:not_enough_money");
	printf("out = %s\n", out_buffer);

	gid=550;  glevel=4;  gold=1000;
	sprintf(in_buffer, IN_GLEVELUP_PRINT, gid, glevel, gold);
	printf("----- following error is normal\n");
	ret = in_glevelup(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-65-ret, "test_glevelup:level_mismatch");
	printf("out = %s\n", out_buffer);
	return 0;
}

int test57_save_piece(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid;

	/*
	eid = 547;
	ret = save_piece(pconn, q_buffer, eid);
	ERROR_RETURN(ret, "test_save_piece:normal");
	printf("save_piece ok\n");
	*/

	eid = 548;
	ret = save_piece(pconn, q_buffer, eid);
	ERROR_RETURN(ret, "test_save_piece:normal");
	printf("save_piece ok\n");

	return 0;
}

int test58_load_piece(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;

	eid = 547;
	sprintf(in_buffer, IN_LOAD_PIECE_PRINT, eid);
	ret = in_load_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 548;
	sprintf(in_buffer, IN_LOAD_PIECE_PRINT, eid);
	ret = in_load_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_piece:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test59_update_piece(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int card_id;
	int num;

	eid = 547; card_id = 5000; num = 10;
	sprintf(in_buffer, IN_UPDATE_PIECE_PRINT, eid, card_id, num);
	ret = in_update_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-25-ret, "test_update_piece:error");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 10; num = 100;
	sprintf(in_buffer, IN_UPDATE_PIECE_PRINT, eid, card_id, num);
	ret = in_update_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-35-ret, "test_update_piece:error");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 4; num = 10;
	sprintf(in_buffer, IN_UPDATE_PIECE_PRINT, eid, card_id, num);
	ret = in_update_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_update_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 5; num = 7;
	sprintf(in_buffer, IN_UPDATE_PIECE_PRINT, eid, card_id, num);
	ret = in_update_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_update_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 21; num = 30;
	sprintf(in_buffer, IN_UPDATE_PIECE_PRINT, eid, card_id, num);
	ret = in_update_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_update_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 399; num = 18;
	sprintf(in_buffer, IN_UPDATE_PIECE_PRINT, eid, card_id, num);
	ret = in_update_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_update_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 400; num = 9;
	sprintf(in_buffer, IN_UPDATE_PIECE_PRINT, eid, card_id, num);
	ret = in_update_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_update_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 400; num = -4;
	sprintf(in_buffer, IN_UPDATE_PIECE_PRINT, eid, card_id, num);
	ret = in_update_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_update_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547;
	sprintf(in_buffer, IN_LOAD_PIECE_PRINT, eid);
	ret = in_load_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_piece:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test60_merge_piece(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int card_id;
	int count;
	int gold;
	int crystal;



	eid = 547; card_id = 4; count = 95; gold = 0; crystal = 0;
	sprintf(in_buffer, IN_MERGE_PIECE_PRINT, eid, card_id, count, gold, crystal);
	ret = in_merge_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-2-ret, "test_merge_piece:error_not_enough_piece");
	printf("out = %s\n", out_buffer);

	// test c21=9
	eid = 547; card_id = 21; count = 5; gold = 0; crystal = 0;
	ret = update_card(pconn, q_buffer, eid, card_id, 9);
	sprintf(in_buffer, IN_MERGE_PIECE_PRINT, eid, card_id, count, gold, crystal);
	ret = in_merge_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-12-ret, "test_merge_piece:error_card_out_bound");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 4; count = 5; gold = -100000; crystal = -100000;
	sprintf(in_buffer, IN_MERGE_PIECE_PRINT, eid, card_id, count, gold, crystal);
	ret = in_merge_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-22-ret, "test_merge_piece:error_not_enough_money");
	printf("out = %s\n", out_buffer);


	eid = 547; card_id = 4; count = 5; gold = -10; crystal = 0;
	sprintf(in_buffer, IN_MERGE_PIECE_PRINT, eid, card_id, count, gold, crystal);
	ret = in_merge_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_merge_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 30; count = 5; gold = 0; crystal = 0;
	sprintf(in_buffer, IN_MERGE_PIECE_PRINT, eid, card_id, count, gold, crystal);
	ret = in_merge_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_merge_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547; card_id = 30; count = 5; gold = 0; crystal = 0;
	sprintf(in_buffer, IN_MERGE_PIECE_PRINT, eid, card_id, count, gold, crystal);
	ret = in_merge_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-2-ret, "test_merge_piece:count_not_enough_error");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test61_pick_piece(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	// eid, pick_type, loc, card_id, count, gold(-), crystal(-)
	int eid;
	int pick_type;
	int loc;
	int card_id;
	int count;
	int gold;
	int crystal;

	eid = 547; pick_type = 0; loc = 0; card_id = 6; 
	count = 20; gold = -500000; crystal = 0;
	sprintf(in_buffer, IN_PICK_PIECE_PRINT, eid, pick_type, loc, card_id
	, count, gold, crystal);
	ret = in_pick_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-16-ret, "test_pick_piece:error_not_enough_money");
	printf("out = %s\n", out_buffer);

	eid = 547; pick_type = 0; loc = 0; card_id = 6; 
	count = 20; gold = -50; crystal = 0;
	sprintf(in_buffer, IN_PICK_PIECE_PRINT, eid, pick_type, loc, card_id
	, count, gold, crystal);
	ret = in_pick_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_pick_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547; pick_type = 0; loc = 0; card_id = 6; 
	count = 99; gold = -50; crystal = 0;
	sprintf(in_buffer, IN_PICK_PIECE_PRINT, eid, pick_type, loc, card_id
	, count, gold, crystal);
	ret = in_pick_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_pick_piece:normal");
	printf("out = %s\n", out_buffer);

	eid = 547; pick_type = 0; loc = 0; card_id = 6; 
	count = 10; gold = -50; crystal = 0;
	sprintf(in_buffer, IN_PICK_PIECE_PRINT, eid, pick_type, loc, card_id
	, count, gold, crystal);
	ret = in_pick_piece(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_pick_piece:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test62_check_friend(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid1;
	int eid2;

	eid1 = 547; eid2 = 548;
	ret = check_friend(pconn, q_buffer, eid1, eid2);
	DEBUG_PRINT(0, "check_friend:ret1 = %d", ret);

	eid1 = 549; eid2 = 548;
	ret = check_friend(pconn, q_buffer, eid1, eid2);
	DEBUG_PRINT(0, "check_friend:ret2 = %d", ret);

	return 0;
}

int test63_add_money(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char *in_ptr;
	long pay_id;
	int eid;
	int money;
	int money_type;
	int channel;
	int price;
	int pay_code;
	int first_pay_check = 1;
	int monthly_flag = 0;
	time_t monthly_end_date = 0;
	int monthly_inc_date = 60 * 60 * 24 * 30;
	int first_vip_extra_gold;
	int first_vip_extra_crystal;
	int first_vip_extra_card_count;
	int first_vip_extra_card_list[MAX_FIRST_VIP_CARD_KIND][2];
	char tmp_buffer[100];
	char *tmp_ptr;
	first_vip_extra_gold = 300; first_vip_extra_crystal = 50;
	first_vip_extra_card_count = 2;
	first_vip_extra_card_list[0][0] = 36;
	first_vip_extra_card_list[0][1] = 1;
	first_vip_extra_card_list[1][0] = 39;
	first_vip_extra_card_list[1][1] = 2;
	tmp_ptr = tmp_buffer;
	tmp_ptr += sprintf(tmp_ptr, "%d", first_vip_extra_card_count);
	for (int i = 0; i < first_vip_extra_card_count; i++)
	{
		tmp_ptr += sprintf(tmp_ptr, " %d %d", first_vip_extra_card_list[i][0]
		, first_vip_extra_card_list[i][1]);
	}

	pay_id = 111111111111; eid = 547; money_type = 0; money = 3000;
	channel = 1; price = 100; pay_code = 0;
	in_ptr = in_buffer;
	in_ptr += sprintf(in_ptr, IN_PAY_PRINT, pay_id, eid, money_type, money
	, channel, price, pay_code, first_pay_check, monthly_flag
	, monthly_end_date, monthly_inc_date, first_vip_extra_gold
	, first_vip_extra_crystal, tmp_buffer);
	ret = in_pay(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_add_money:normal");
	printf("out = %s\n", out_buffer);

	pay_id = 222222222222; eid = 547; money_type = 1; money = 3000;
	channel = 1; price = 100;
	in_ptr = in_buffer;
	in_ptr += sprintf(in_ptr, IN_PAY_PRINT, pay_id, eid, money_type, money
	, channel, price, pay_code, first_pay_check, monthly_flag
	, monthly_end_date, monthly_inc_date, first_vip_extra_gold
	, first_vip_extra_crystal, tmp_buffer);
	for (int i = 0; i < first_vip_extra_card_count; i++)
	{
		in_ptr += sprintf(in_ptr, " %d %d", first_vip_extra_card_list[i][0]
		, first_vip_extra_card_list[i][1]);
	}
	ret = in_pay(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_add_money:normal");
	printf("out = %s\n", out_buffer);

	pay_id = 333333333333; eid = 547; money_type = 3; money = 3000;
	channel = 1; price = 100;
	in_ptr = in_buffer;
	in_ptr += sprintf(in_ptr, IN_PAY_PRINT, pay_id, eid, money_type, money
	, channel, price, pay_code, first_pay_check, monthly_flag
	, monthly_end_date, monthly_inc_date, first_vip_extra_gold
	, first_vip_extra_crystal, tmp_buffer);
	for (int i = 0; i < first_vip_extra_card_count; i++)
	{
		in_ptr += sprintf(in_ptr, " %d %d", first_vip_extra_card_list[i][0]
		, first_vip_extra_card_list[i][1]);
	}
	ret = in_pay(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-25-ret, "test_add_money:error_money_type");
	printf("out = %s\n", out_buffer);

	pay_id = 444444444444; eid = 333; money_type = 0; money = 3000;
	channel = 1; price = 100;
	in_ptr = in_buffer;
	in_ptr += sprintf(in_ptr, IN_PAY_PRINT, pay_id, eid, money_type, money
	, channel, price, pay_code, first_pay_check, monthly_flag
	, monthly_end_date, monthly_inc_date, first_vip_extra_gold
	, first_vip_extra_crystal, tmp_buffer);
	for (int i = 0; i < first_vip_extra_card_count; i++)
	{
		in_ptr += sprintf(in_ptr, " %d %d", first_vip_extra_card_list[i][0]
		, first_vip_extra_card_list[i][1]);
	}
	ret = in_pay(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_add_money:error_eid");
	printf("out = %s\n", out_buffer);


	pay_id = 555555555555; eid = 547; money_type = 1; money = 1000; first_pay_check=1;
	channel = 1; price = 30; pay_code = 0; monthly_flag = 1; monthly_end_date = time(NULL) + 60 * 60;
	in_ptr = in_buffer;
	in_ptr += sprintf(in_ptr, IN_PAY_PRINT, pay_id, eid, money_type, money
	, channel, price, pay_code, first_pay_check, monthly_flag
	, monthly_end_date, monthly_inc_date, first_vip_extra_gold
	, first_vip_extra_crystal, tmp_buffer);
	for (int i = 0; i < first_vip_extra_card_count; i++)
	{
		in_ptr += sprintf(in_ptr, " %d %d", first_vip_extra_card_list[i][0]
		, first_vip_extra_card_list[i][1]);
	}
	ret = in_pay(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_add_money:normal");
	printf("out = %s\n", out_buffer);

	pay_id = 666666666666; eid = 547; money_type = 1; money = 2000; first_pay_check=1;
	channel = 1; price = 30; pay_code = 0; monthly_flag = 1; monthly_end_date = time(NULL) + 60 * 60;
	in_ptr = in_buffer;
	in_ptr += sprintf(in_ptr, IN_PAY_PRINT, pay_id, eid, money_type, money
	, channel, price, pay_code, first_pay_check, monthly_flag
	, monthly_end_date, monthly_inc_date, first_vip_extra_gold
	, first_vip_extra_crystal, tmp_buffer);
	for (int i = 0; i < first_vip_extra_card_count; i++)
	{
		in_ptr += sprintf(in_ptr, " %d %d", first_vip_extra_card_list[i][0]
		, first_vip_extra_card_list[i][1]);
	}
	ret = in_pay(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_add_money:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}

int test64_card_all_count(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid;
	int card_id;
	int card_count = 0;
	int deck_count = 0;

	eid = 547; card_id = 26;
	ret = get_card_all_count(pconn, q_buffer, eid, card_id, &card_count, &deck_count);
	ERROR_RETURN(ret, "test_card_all_count:normal");
	printf("test_card_all_count:card_count = %d deck_count = %d\n", card_count
	, deck_count);

	return 0;
}

// this test, should set c21 card_sell_count > card_all_count - card_deck_count
int test65_sell_card(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int ret;
	int eid;
	int card_id;
	int card_type;
	int money_type; // 0=gold, 1=crystal
	int sell_count;
	int gold_sell;
	int crystal_sell;

	// sell card
	eid = 547;
	card_id = 21;
	card_type = 0;
	money_type = 0;
	sell_count = 3;
	gold_sell = 100;
	crystal_sell = 0;

	sprintf(in_buffer, IN_SELL_CARD_PRINT, eid, card_id, card_type
	, money_type, sell_count, gold_sell, crystal_sell);
	ret = in_sell_card(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_sell_card:sell_card_normal %d", ret);
	DEBUG_PRINT(0, "test65_sell_card:sell_card_out_buffer=%s", out_buffer);

	return 0;
}

int test66_get_course(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int ret;
	int eid;

	eid = 547; 
	sprintf(in_buffer, IN_GET_COURSE_PRINT, eid);
	ret = in_get_course(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_get_course:normal %d", ret);
	DEBUG_PRINT(0, "test65_get_course:out_buffer=%s", out_buffer);

	return 0;
}

int test67_save_course(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int ret;
	int eid;
	int course;

	eid = 547; course = 3;
	sprintf(in_buffer, IN_SAVE_COURSE_PRINT, eid, course);
	ret = in_save_course(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_save_course:normal %d", ret);
	DEBUG_PRINT(0, "test65_save_course:out_buffer=%s", out_buffer);

	return 0;
}

int test68_challenge(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int eid1;
	int eid2;
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid1 = 547; eid2 = 548;
	sprintf(in_buffer, IN_CHALLENGE_PRINT, eid1, eid2);
	ret = in_challenge(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_challenge:error %s", out_buffer);
	printf("test68:out_buffer = %s \n", out_buffer);

	return 0;
}

int test69_load_mission(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int eid;
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 547;  // 548 has 2 rows in evil_mission
	sprintf(in_buffer, IN_LOAD_MISSION_PRINT, eid);
	ret = in_load_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_mission:error %s", out_buffer);
	printf("test69:out_buffer = %s \n", out_buffer);

	return 0;
}


int test70_save_mission(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int eid;
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char *ptr;
	mission_t mis;
	mission_t mlist[MAX_MISSION];
	int mis_count;

	// mission-fix :  4 fields
	// mid, status, n1, last_update(%ld)
//	mlist.clear();
	bzero(mlist, sizeof(mlist));
	mis_count = 0;

	{
		mission_t tmp = {1, 3, 1, 1413888424};
		mis = tmp;
	}
	mlist[mis.mid] = mis;
	mis_count++;
	{
		mission_t tmp = {2, 2, 1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;
	mis_count++;
	{
		// mid, status, n1, last_update(%ld)
		mission_t tmp = {3, 3, 1, 1413888425};
		mis = tmp; // warning but ok
	}
	mlist[mis.mid] = mis;
	mis_count++;

	eid = 549;  // 549 has rubbish records in db-init.sql

	ptr = in_buffer + sprintf(in_buffer, "%d %d", eid, mis_count);
	for (int i = 0; i < MAX_MISSION; i++) {
		mission_t & mis = mlist[i];
		if (mis.mid == 0) continue;
		ptr += sprintf(ptr, IN_SAVE_MISSION_PRINT, mis.mid
		, mis.status, mis.n1, mis.last_update);
	}

//	ptr = in_buffer + sprintf(in_buffer, "%d %zu", eid, mlist.size());
//	for (MISSION_MAP::iterator it=mlist.begin(); it!=mlist.end(); it++) {
//		mission_t & mis = (*it).second;
//		ptr += sprintf(ptr, IN_SAVE_MISSION_PRINT, mis.mid
//		, mis.status, mis.n1, mis.last_update);
//	}

	ret = in_save_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_mission:error %s", out_buffer);
	printf("test70:out_buffer = %s \n", out_buffer);
	return 0;
}

int test71_insert_first_slot(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int eid;
	int ret;

	eid = 549; 
	ret = insert_first_slot(pconn, q_buffer, eid);
	ERROR_RETURN(ret, "test_load_slot:error");
	printf("test71:ret = %d \n", ret);
	return 0;
}
int test72_load_slot(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int eid;
	int id;
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 547; id = 1;
	sprintf(in_buffer, IN_LOAD_SLOT_PRINT, eid, id);
	ret = in_load_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_load_slot:error %s", out_buffer);
	printf("test72:out_buffer = %s \n", out_buffer);

	return 0;
}

int test73_save_slot(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int eid;
	int id;
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 547; id = 1; 
	sprintf(in_buffer, IN_SAVE_SLOT_PRINT, eid, id, "slot1", "1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001");
	ret = in_save_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_save_slot:error %s", out_buffer);
	printf("test73:out_buffer = %s \n", out_buffer);
	return 0;
}

int test74_rename_slot(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int eid;
	int id;
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 547; id = 1; 
	sprintf(in_buffer, IN_RENAME_SLOT_PRINT, eid, id, "slot");
	ret = in_rename_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_rename_slot:error %s", out_buffer);
	printf("test74:out_buffer = %s \n", out_buffer);
	return 0;
}

int test75_buy_slot(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int eid;
	int id;
	int flag;
	int gold;
	int crystal;

	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	// fail case:  547 already has slot 1, so this will return -6
	eid = 547; id = 1; flag = 1; gold = 100; crystal = 0;
	sprintf(in_buffer, IN_BUY_SLOT_PRINT, eid, flag, id, gold, crystal);
	printf(">>>>>> following error is normal\n");
	ret = in_buy_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_buy_slot:fail1 %s", out_buffer);
	printf("test75:out_buffer = %s \n", out_buffer);

	// fail case:  eid=547  slot 3 is always fail, only 2 is ok
	eid = 547; id = 3; flag = 1; gold = 100; crystal = 0;
	sprintf(in_buffer, IN_BUY_SLOT_PRINT, eid, flag, id, gold, crystal);
	printf(">>>>>> following error is normal\n");
	ret = in_buy_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_buy_slot:fail2 %s", out_buffer);
	printf("test75:out_buffer = %s \n", out_buffer);

	// fail case:  eid=547  slot 2 ok, but not enough gold
	eid = 547; id = 2; flag = 1; gold = 1000000; crystal = 0;
	sprintf(in_buffer, IN_BUY_SLOT_PRINT, eid, flag, id, gold, crystal);
	printf(">>>>>> following error is normal\n");
	ret = in_buy_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-7-ret, "test_buy_slot:gold_fail %s", out_buffer);
	printf("test75:out_buffer = %s \n", out_buffer);

	// normal case:  eid=547  slot 2 
	eid = 547; id = 2; flag = 1; gold = 100; crystal = 0;
	sprintf(in_buffer, IN_BUY_SLOT_PRINT, eid, flag, id, gold, crystal);
	ret = in_buy_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_buy_slot:normal %s", out_buffer);
	printf("test75:out_buffer = %s \n", out_buffer);

	// 548 has no slot, so id=0 is error
	eid = 548; id = 0; flag = 1;  gold = 10; crystal = 0;
	sprintf(in_buffer, IN_BUY_SLOT_PRINT, eid, flag, id, gold, crystal);
	ret = in_buy_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-25-ret, "test_buy_slot:fail_y_slot0 %s", out_buffer);

	// 548 has no slot, so id=2 is error
	eid = 548; id = 2; flag = 1;  gold = 10; crystal = 0;
	sprintf(in_buffer, IN_BUY_SLOT_PRINT, eid, flag, id, gold, crystal);
	ret = in_buy_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_buy_slot:fail_y_slot2 %s", out_buffer);

	// 548 has no slot, so id=1 is ok
	eid = 548; id = 1; flag = 1;  gold = 10; crystal = 0;
	sprintf(in_buffer, IN_BUY_SLOT_PRINT, eid, flag, id, gold, crystal);
	ret = in_buy_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_buy_slot:normal_y_slot1 %s", out_buffer);

	return 0;
}

int test76_mission_reward(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	
	int eid;
	int mid;
	int gold;
	int crystal;
	int power;
	int exp;
	int lv;
	int card_count;
	int card_list[MAX_MISSION_REWARD_CARD];
	int piece_count;
	int piece_list[MAX_MISSION_REWARD_PIECE][2];
	char *tmp_ptr;
	char tmp_buffer[1000];

	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 547; mid = 2; exp=1000; 
	gold = 500; crystal = 600; lv = 1;
	power = 12;
	card_count = 1; card_list[0] = 22;
	piece_count = 2;
	piece_list[0][0] = 21; piece_list[0][1] = 3;
	piece_list[1][0] = 36; piece_list[1][1] = 4;
	tmp_ptr = tmp_buffer;
	tmp_ptr += sprintf(tmp_ptr, "%d", card_count);
	for (int cdx = 0; cdx < card_count; cdx++) {
		tmp_ptr += sprintf(tmp_ptr, " %d", card_list[cdx]);
	}
	tmp_ptr += sprintf(tmp_ptr, " %d", piece_count);
	for (int pdx = 0; pdx < piece_count; pdx++) {
		tmp_ptr += sprintf(tmp_ptr, " %d %d", piece_list[pdx][0]
		, piece_list[pdx][1]);
	}
	sprintf(in_buffer, IN_MISSION_REWARD_PRINT, eid, mid, exp
	, gold, crystal, power, lv, tmp_buffer);
	ret = in_mission_reward(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_mission_reward:normal %s", out_buffer);
	printf("test76:out_buffer = %s \n", out_buffer);
	return 0;
}

int test77_insert_login(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int ret;
	int eid;
	char ip[EVIL_ADDRESS_MAX + 1];
	char remark[105];

	eid = 547; 
	sprintf(ip, "255.255.255.255:65511");
	sprintf(remark, "67");
	ret = insert_login(pconn, q_buffer, eid, ip, remark);
	ERROR_RETURN(ret, "test_insert_login:normal");
	return 0;
}
	
int test78_only_save_slot(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int ret;
	int eid;
	int slot;
	char card[EVIL_CARD_MAX + 5];

	eid = 547; slot = 2;
	sprintf(card, "%.400s", "1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000021");
	ret = save_slot(pconn, q_buffer, eid, slot, card);
	ERROR_RETURN(ret, "test78_save_slot:normal");

	return 0;
}
	
int test79_only_save_deck(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int ret;
	int eid;
	int slot;
	char card[EVIL_CARD_MAX + 5];

	eid = 547; slot = 2;
	sprintf(card, "%.400s", "1000000000000000000002200321030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000045");
	ret = save_deck(pconn, q_buffer, eid, slot, card);
	ERROR_RETURN(ret, "test78_save_deck:normal");

	return 0;
}

int test80_slot_list(MYSQL **pconn, char *q_buffer, int argc, char *argv[]) 
{
	int eid;
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	// char out_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 547; 
	sprintf(in_buffer, IN_SLOT_LIST_PRINT, eid);
	ret = in_slot_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_slot_list:error %s", out_buffer);
	printf("test80:out_buffer = %s \n", out_buffer);

	return 0;
}


int test81_record_buy(MYSQL **pconn, char *q_buffer, int argc, char *argv[]){
	int ret;
	int eid;
	int card_id;
	int buy_count;
	int gold;
	int crystal;

	// buy card
	eid = 547;
	card_id = 23;
	buy_count = 3;
	gold = -100 * buy_count;
	crystal = -30 * buy_count;

	ret = record_buy(pconn, q_buffer, eid, card_id, buy_count, gold, crystal, "test");
	ERROR_RETURN(ret, "test_record_buy:normal %d", ret);

	return 0;
}


int test82_add_pay_log(MYSQL **pconn, char *q_buffer, int argc, char *argv[]){
	int ret;
	long order_no;
	int pay_code;
	int channel;
	int price;
	int game_money_type;
	int game_money;
	int eid;
	int status;

	// buy card
	eid = 547;
	order_no = 1929461038L;
	pay_code = 41;
	channel = 1;
	price = 100;
	game_money_type = 1;
	game_money = 1000;
	status = 0;

	ret = add_pay_log(pconn, q_buffer, order_no
		, pay_code, channel, price, game_money_type,
		game_money, eid, status, "test_add_pay_log");
	ERROR_RETURN(ret, "test_add_pay_log:normal %d", ret);

	return 0;
}



int test83_add_match(MYSQL **pconn, char *q_buffer, int argc, char *argv[]){
	int ret;
	long match_id;
	char title[105] = "test";
	int max_player;
	long start_time;
	long t1, t2, t3, t4;
	int round;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	// test for not enough input
	match_id = 1111, max_player = 8, start_time = time(NULL) - 500;
	sprintf(in_buffer, "%ld %.100s %d %ld", match_id, title, max_player, start_time);
	ret = in_add_match(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-5 - ret, "test_add_match_error %d", ret);

	// test for start time error
	t1 = 60 * 60 * 20; t2 = 60 * 60 * 22; t3 = 0; t4 = 0; round = -1;
	sprintf(in_buffer, IN_ADD_MATCH_PRINT, match_id, title, max_player
	, start_time, t1, t2, t3, t4, round);
	ret = in_add_match(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-35 - ret, "test_add_match_error %d", ret);

	// test for normal case
	start_time = time(NULL) + 1;
	sprintf(in_buffer, IN_ADD_MATCH_PRINT, match_id, title, max_player
	, start_time, t1, t2, t3, t4, round);
	ret = in_add_match(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_add_match_normal %d", ret);

	return 0;
}

int test84_add_match_player(MYSQL **pconn, char *q_buffer, int argc, char *argv[]){
	int ret;
	long match_id;
	int round;
	int team_id;
	int eid;
	int win;
	int lose;
	int draw;
	int tid;
	int point;
	int icon;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	match_id = 1111; round = 0; team_id=0;
	eid = 1; win=0; lose=0; draw=0; tid = 0; point = 0; icon = 3;
	sprintf(in_buffer, IN_ADD_MATCH_PLAYER_PRINT, match_id
	, eid, round, team_id, win, lose, draw, tid, point, icon, "masha");

	ret = in_match_apply(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "add_match_player %d", ret);
	DEBUG_PRINT(0, "add_match_player:out_buffer=%s", out_buffer);
	return 0;
}

int test85_delete_match_player(MYSQL **pconn, char *q_buffer, int argc, char *argv[]){
	int ret;
	long match_id;
	int eid;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	match_id = 1111; 
	eid = 1; 

	sprintf(in_buffer, IN_DELETE_MATCH_PLAYER_PRINT, match_id, eid);

	ret = in_match_delete(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "delete_match_player %d", ret);
	DEBUG_PRINT(0, "delete_match_player:out_buffer=%s", out_buffer);

	return 0;
}


int test86_match_team_init(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	long match_id;
	int round;
	int team_id;
	int eid;
	int win;
	int lose;
	int draw;
	int tid;
	int point;
	int icon;
	int count;
	char in_buffer[DB_BUFFER_MAX+5];
	char * ptr;
	char *out_buffer = in_buffer;

	count = 8;
	for (int i=1; i<=count; i++) {
		match_id = 1111; round = 0; team_id=0;
		eid = i; win=0; lose=0; draw=0; tid = 0; point = 0; icon = i;
		char alias[EVIL_ALIAS_MAX + 1];
		sprintf(alias, "alias_%d", i);
		sprintf(in_buffer, IN_ADD_MATCH_PLAYER_PRINT, match_id
		, eid, round, team_id, win, lose, draw, tid, point, icon, alias);

		ret = in_match_apply(pconn, q_buffer, in_buffer, out_buffer);
		ERROR_RETURN(ret, "add_match_player %d", ret);
		DEBUG_PRINT(0, "add_match_player:out_buffer=%s", out_buffer);
	}
	// match_apply end

	match_id = 1111; 
	ptr = in_buffer;
	ptr += sprintf(ptr, "%ld %d", match_id, count);
	for (int i=1; i<=count; i++) {
		ptr += sprintf(ptr, " %d", i);
	}

	DEBUG_PRINT(0, "in_buffer=%s", in_buffer);
	
	ret = in_match_team_init(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "match_team_init %d", ret);

	return 0;
}

int test87_update_match_player(MYSQL **pconn, char *q_buffer, int argc, char *argv[]){
	int ret;
	long match_id;
	int round;
	int team_id;
	int eid;
	int fake_eid;
	int win;
	int lose;
	int draw;
	int tid;
	int point;
	int icon;
	int count;
	const char * alias = "masha";
	char in_buffer[DB_BUFFER_MAX+5];
	char * ptr;
	char *out_buffer = in_buffer;

	match_id = 1111; 
	ptr = in_buffer;
	count = 2;
	ptr += sprintf(ptr, "%ld %d", match_id, count);

	eid = 1; fake_eid = 0; round = 1; team_id = 1;
	win = 1; lose = 1; draw = 1;
	tid = 0; point = 5; icon = 2;
	ptr += sprintf(ptr, " %d %d %d %d %d %d %d %d %d %d %s"
	, eid, fake_eid, round, team_id, win, lose, draw, tid, point, icon, alias);

	eid = 1; fake_eid = -201; round = 2; team_id = 1;
	win = 2; lose = 1; draw = 0;
	tid = 1; point = 6; icon = 2;
	ptr += sprintf(ptr, " %d %d %d %d %d %d %d %d %d %d %s"
	, eid, fake_eid, round, team_id, win, lose, draw, tid, point, icon, alias);

	DEBUG_PRINT(0, "in_buffer=%s", in_buffer);
	
	ret = in_update_match_player(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "update_match_player: %d", ret);

	return 0;
}

int test88_match_eli_init(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	long match_id;
	int max_player;
	char in_buffer[DB_BUFFER_MAX+5];
	char * ptr;
	char *out_buffer = in_buffer;

	// test for start time error
	match_id = 2121;
	max_player = 8;

	int win_eid_list[] = { 428, 481, 426, 547 };
	ptr = in_buffer;
	ptr += sprintf(ptr, "%ld %d", match_id, max_player / 2);
	for (int i = 0; i < max_player / 2; i++) {
		ptr += sprintf(ptr, " %d", win_eid_list[i]);
	}
	ret = in_match_eli_init(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "match_eli_init:error_in_eli_init");

	return 0;
}

int test89_add_match_ai(MYSQL **pconn, char *q_buffer, int argc, char *argv[]){
	int ret;
	long match_id;
	int count;
	int eid;
	int icon;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char * ptr;
	ptr = in_buffer;

	match_id = 2222; 
	count = 8;
	ptr += sprintf(ptr, "%ld %d", match_id, count);
	for (int i=1; i<=count; i++) {
		eid = i;
		icon = i;
		char alias[EVIL_ALIAS_MAX + 1];
		sprintf(alias, "alias_%d", i);
		ptr += sprintf(ptr, " %d %d %s", eid, icon, alias);
	}

	ret = in_match_apply_ai(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "add_match_ai %d", ret);
	DEBUG_PRINT(0, "add_match_ai:out_buffer=%s", out_buffer);
	return 0;
}


int test90_update_match(MYSQL **pconn, char *q_buffer, int argc, char *argv[]){
	int ret;
	long match_id;
	int round;
	int status;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	match_id = 1111; round = 1; status = MATCH_STATUS_DELETE;
	sprintf(in_buffer, IN_UPDATE_MATCH_PRINT, match_id, round, status);

	ret = in_update_match(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "update_match: %d", ret);
	DEBUG_PRINT(0, "update_match:out_buffer=%s", out_buffer);
	return 0;
}

int test91_friend_del(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid1;
	int eid2;

	eid1=550; eid2=551;
	sprintf(in_buffer, IN_FRIEND_ADD_PRINT, eid1, eid2);
	ret = in_friend_add(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_del_friend:normal");
	printf("out = %s\n", out_buffer);

	eid1=552; eid2=551;
	sprintf(in_buffer, IN_FRIEND_ADD_PRINT, eid1, eid2);
	ret = in_friend_add(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_del_friend:normal");
	printf("out = %s\n", out_buffer);


	eid1=550; eid2=550;
	sprintf(in_buffer, IN_FRIEND_DEL_PRINT, eid1, eid2);
	ret = in_friend_del(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-15 - ret, "test_del_friend:same_eid");
	printf("out = %s\n", out_buffer);

	eid1=550; eid2=552;
	sprintf(in_buffer, IN_FRIEND_DEL_PRINT, eid1, eid2);
	ret = in_friend_del(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6 - ret, "test_del_friend:not_friend");
	printf("out = %s\n", out_buffer);

	eid1=551; eid2=550;
	sprintf(in_buffer, IN_FRIEND_DEL_PRINT, eid1, eid2);
	ret = in_friend_del(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_del_friend:normal");
	printf("out = %s\n", out_buffer);

	return 0;
}


int test92_init_ranking_list(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	bzero(in_buffer, sizeof(in_buffer));
//	sprintf(in_buffer, "%d", ROBOT_START_EID);
	ret = init_ranking_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_init_ranking_list: init_ranking_list fail");

	DEBUG_PRINT(1, "test_init_ranking_list: out_buffer[%s]", out_buffer);

	return 0;
}

int test93_ranking_list(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char *ptr;
	int n;
	int rank_count;
	int level;
	int rank;
	int rank_time;
	int eid;
	int icon;
	double rating;
	char alias[EVIL_ALIAS_MAX + 10];

	/*
	printf(">>>>>> following error is normal\n");
	ret = in_ranking_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-5-ret, "test_ranking_list: in_ranking_list error");
	*/

	eid = 550;
	sprintf(in_buffer, "%d", eid);
	ret = in_ranking_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_ranking_list: in_ranking_list error");

	ptr = out_buffer;
	ret = sscanf(ptr, "%d %d %d %n", &rank, &rank_time, &rank_count, &n);
	ERROR_RETURN(ret-3, "test_ranking_list: out_buffer error %s", out_buffer);

	for (int i = 0; i < rank_count; i++) {
		ptr += n;
		ret = sscanf(ptr, "%d %d %d %d %lf %s %n", &eid, &level, &rank, &icon
		, &rating, alias, &n);
		ERROR_RETURN(ret-6
		, "test_ranking_list: sscanf count != 6\nout_buffer:%s\nptr:%s"
		, out_buffer, ptr);

		INFO_PRINT(0
		, "rank[%d] eid[%d] level[%d] icon[%d], rating[%lf] alias[%s]"
		, rank, eid, level, icon, rating, alias);
	}

	return 0;
}


int test94_ranking_targets(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char *ptr;
	int n;
	int eid;
	int rank;
	int rank_time;
	int rank_count;
	int target_level;
	int target_rank;
	int target_eid;
	int target_icon;
	double target_rating;
	char target_alias[EVIL_ALIAS_MAX + 10];

	ret = init_ranking_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_ranking_targets: init_ranking_list fail");

	// no eid input
	bzero(in_buffer, sizeof(in_buffer));
	printf(">>>>>> following error is normal\n");
	ret = in_ranking_targets(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-5-ret, "test_ranking_targets: eid_not_exist");

	eid = 400;	// not exist
	sprintf(in_buffer, "%d", eid);
	printf(">>>>>> following error is normal\n");
	ret = in_ranking_targets(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-15-ret, "test_ranking_targets: eid_not_exist");

	printf(">>>>>> normal test\n");
	eid = 501;	// rank 1
	sprintf(in_buffer, "%d", eid);
	ret = in_ranking_targets(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_ranking_targets: in_ranking_targets error");
	ptr = out_buffer;
	ret = sscanf(ptr, "%d %d %n", &rank, &rank_count, &n);
	DEBUG_PRINT(1, "test_ranking_targets eid=[%d] rank[%d] rank_count[%d]"
	, eid, rank, rank_count);
	ERROR_RETURN(ret-2, "test_ranking_list: out_buffer error %s", out_buffer);
	ERROR_RETURN(rank-1, "test_ranking_targets: eid=[%d] rank=[%d]", eid, rank);
	DEBUG_PRINT(rank_count
	, "test_ranking_targets: eid=[%d] rank=[%d] rank_count=[%d]"
	, eid, rank, rank_count);

	/*
	eid = 506;	// rank 4
	sprintf(in_buffer, "%d", eid);
	ret = in_ranking_targets(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_ranking_targets: in_ranking_targets error");
	ptr = out_buffer;
	ret = sscanf(ptr, "%d %d %n", &rank, &rank_count, &n);
	DEBUG_PRINT(1, "test_ranking_targets eid=[%d] rank[%d] rank_count[%d]"
	, eid, rank, rank_count);
	ERROR_RETURN(ret-2, "test_ranking_list: out_buffer error %s", out_buffer);
	ERROR_RETURN(rank-4, "test_ranking_targets: eid=[%d] rank=[%d]", eid, rank);
	ERROR_RETURN(rank_count-3
	, "test_ranking_targets: eid=[%d] rank=[%d] rank_count=[%d]"
	, eid, rank, rank_count);
	for (int i = 0; i < rank_count; i++) {
		ptr += n;
		ret = sscanf(ptr, "%d %d %s %n", &target_eid, &target_rank
		, target_alias, &n);
		ERROR_RETURN(ret-3
		, "test_ranking_list: sscanf count != 3\nout_buffer:%s\nptr:%s"
		, out_buffer, ptr);

		INFO_PRINT(0, "eid[%d] rank[%d] alias[%s]"
		, target_eid, target_rank, target_alias);
	}
	*/

	eid = 571;
	sprintf(in_buffer, "%d", eid);
	ret = in_ranking_targets(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_ranking_targets: in_ranking_targets error");
	ptr = out_buffer;
	ret = sscanf(ptr, "%d %d %d %n", &rank, &rank_time, &rank_count, &n);
	DEBUG_PRINT(1, "test_ranking_targets eid=[%d] rank[%d] rank_count[%d]"
	, eid, rank, rank_count);
	ERROR_RETURN(ret-3, "test_ranking_list: out_buffer error %s", out_buffer);
	for (int i = 0; i < rank_count; i++) {
		ptr += n;
		ret = sscanf(ptr, "%d %d %d %d %lf %s %n", &target_eid
		, &target_level, &target_rank, &target_icon, &target_rating
		, target_alias, &n);
		ERROR_RETURN(ret-6
		, "test_ranking_list: sscanf count != 6\nout_buffer:%s\nptr:%s"
		, out_buffer, ptr);

		INFO_PRINT(0
		, "eid[%d] level[%d] rank[%d] icon[%d] rating[%lf] alias[%s]"
		, target_eid, target_level, target_rank, target_icon
		, target_rating, target_alias);
	}

	return 0;
}

int test95_check_ranking_target(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int target_rank;
	int target_eid;

	ret = init_ranking_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_check_ranking_target: init_ranking_list fail");

	// no eid input
	bzero(in_buffer, sizeof(in_buffer));
	printf(">>>>>> following error is normal\n");
	ret = in_check_ranking_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-5-ret, "test_check_ranking_target: input_invalid");

	// target_eid_not_exist
	eid = 571, target_eid = 400, target_rank = 1;
	sprintf(in_buffer, "%d %d %d", eid, target_eid, target_rank);
	printf(">>>>>> following error is normal\n");
	ret = in_check_ranking_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-16-ret, "test_check_ranking_target: eid_not_exist");

	// need refresh list
	eid = 571, target_eid = 547, target_rank = 18;
	sprintf(in_buffer, "%d %d %d", eid, target_eid, target_rank);
	ret = in_check_ranking_target(pconn, q_buffer, in_buffer, out_buffer);
	DEBUG_PRINT(ret, "test_check_ranking_target: out_buffer[%s]"
	, out_buffer);

	printf(">>>>>> following error is normal\n");
	// target not in range
	eid = 571, target_eid = 542, target_rank = 12;
	sprintf(in_buffer, "%d %d %d", eid, target_eid, target_rank);
	ret = in_check_ranking_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-46-ret, "test_check_ranking_target: not_in_range");


	// normal test
	eid = 571, target_eid = 547, target_rank = 17;
	sprintf(in_buffer, "%d %d %d", eid, target_eid, target_rank);
	ret = in_check_ranking_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_check_ranking_target: normal_test");
	DEBUG_PRINT(ret, "test_check_ranking_target: out_buffer[%s]"
	, out_buffer);

	return 0;
}

int test96_change_ranking_data(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid1, eid2;
	int winner;

	ret = init_ranking_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_check_ranking_target: init_ranking_list fail");

	// no eid input
	bzero(in_buffer, sizeof(in_buffer));
	printf(">>>>>> following error is normal\n");
	ret = in_change_ranking_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-5-ret, "test_change_ranking_data: input_invalid");

	// target_eid_not_exist
	eid1 = 549, eid2 = 400, winner = 2;
	sprintf(in_buffer, "%d %d %d", eid1, eid2, winner);
	printf(">>>>>> following error is normal\n");
	ret = in_change_ranking_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test_change_ranking_data: eid_not_exist");

	// win challenge lower rank eid
	eid1 = 549, eid2 = 550, winner = 1;
	sprintf(in_buffer, "%d %d %d", eid1, eid2, winner);
	ret = in_change_ranking_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_change_ranking_data: normal_test");

	// lose challenge
	eid1 = 549, eid2 = 545, winner = 2;
	sprintf(in_buffer, "%d %d %d", eid1, eid2, winner);
	ret = in_change_ranking_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_change_ranking_data: normal_test");

	// draw challenge
	eid1 = 545, eid2 = 549, winner = 9;
	sprintf(in_buffer, "%d %d %d", eid1, eid2, winner);
	ret = in_change_ranking_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_change_ranking_data: normal_test");

	// win challenge
	eid1 = 549, eid2 = 545, winner = 1;
	sprintf(in_buffer, "%d %d %d", eid1, eid2, winner);
	ret = in_change_ranking_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_change_ranking_data: normal_test");

	return 0;
}


int test97_get_ranking_history(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;

	int num_count;
	int n;

	char *out_ptr;
	int eid1, eid2;
	int rank1, rank2;
	int icon1, icon2;
	char alias1[EVIL_ALIAS_MAX + 10];
	char alias2[EVIL_ALIAS_MAX + 10];
	int success;
	time_t cur_time;

	struct tm *ptm;

	// win challenge
	eid = 549;
	sprintf(in_buffer, "%d", eid);
	ret = in_get_ranking_history(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_change_ranking_data: normal_test");
	DEBUG_PRINT(1, "ranking_history[%s]", out_buffer);
	out_ptr = out_buffer;
	sscanf(out_ptr, "%d %n", &num_count, &n);
	for (int i = 0; i < num_count; i++) {
		out_ptr += n;
		sscanf(out_ptr, "%d %d %d %d %d %d %s %s %d %ld %n"
		, &eid1, &eid2, &rank1, &rank2, &icon1, &icon2
		, alias1, alias2, &success, &cur_time, &n);
		ptm = localtime(&cur_time);
		DEBUG_PRINT(1
		, "eid1[%d] eid2[%d] rank1[%d] rank2[%d] icon1[%d] icon2[%d] alias1[%s] alis2[%s] success[%d] time[%s]"
		, eid1, eid2, rank1, rank2, icon1, icon2, alias1, alias2
		, success, asctime(ptm)); 
	}


	return 0;
}

int test98_get_range_list(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int range_count;
	int rank;
	
	int range_list[RANKING_RANDOM_COUNT];

	rank = 1;
	range_count = __get_range_list(range_list, rank);
	DEBUG_PRINT(1, "rank = %d", rank);
	ERROR_RETURN(range_count, "test_get_range_list: rank=%d", rank);

	rank = 3;
	range_count = __get_range_list(range_list, rank);
	DEBUG_PRINT(1, "rank = %d", rank);
	ERROR_RETURN(range_count-2, "test_get_range_list: rank=%d", rank);
	for (int i = 0; i < range_count; i++) {
		DEBUG_PRINT(1, "test_get_range_list[%d] = [%d]", i, range_list[i]);
	}

	rank = 6;
	range_count = __get_range_list(range_list, rank);
	DEBUG_PRINT(1, "rank = %d", rank);
	ERROR_RETURN(range_count-5, "test_get_range_list: rank=%d", rank);
	for (int i = 0; i < range_count; i++) {
		DEBUG_PRINT(1, "test_get_range_list[%d] = [%d]", i, range_list[i]);
	}
	
	rank = 7;
	range_count = __get_range_list(range_list, rank);
	DEBUG_PRINT(1, "rank = %d", rank);
	ERROR_RETURN(range_count-5, "test_get_range_list: rank=%d", rank);
	for (int i = 0; i < range_count; i++) {
		DEBUG_PRINT(1, "test_get_range_list[%d] = [%d]", i, range_list[i]);
	}

	rank = 200;
	range_count = __get_range_list(range_list, rank);
	DEBUG_PRINT(1, "rank = %d", rank);
	ERROR_RETURN(range_count-5, "test_get_range_list: rank=%d", rank);
	for (int i = 0; i < range_count; i++) {
		DEBUG_PRINT(1, "test_get_range_list[%d] = [%d]", i, range_list[i]);
	}

	rank = 2015;
	range_count = __get_range_list(range_list, rank);
	DEBUG_PRINT(1, "rank = %d", rank);
	ERROR_RETURN(range_count-5, "test_get_range_list: rank=%d", rank);
	for (int i = 0; i < range_count; i++) {
		DEBUG_PRINT(1, "test_get_range_list[%d] = [%d]", i, range_list[i]);
	}

	return 0;
}

int test99_save_ranking_challenge(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid;
	int rank_time;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	/*
	printf(">>>>>> following error is normal\n");
	ret = in_save_ranking_challenge(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-5-ret, "save_ranking_challenge: input_error");
	*/

	eid = 545; rank_time = -1;
	sprintf(in_buffer, IN_SAVE_RANKING_CHALLENGE_PRINT, eid, rank_time);
	printf(">>>>>> following error is normal\n");
	ret = in_save_ranking_challenge(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-15-ret, "save_ranking_challenge: eid=%d rank=%d"
	, eid, rank_time);

	eid = 400; rank_time = 1;
	sprintf(in_buffer, IN_SAVE_RANKING_CHALLENGE_PRINT, eid, rank_time);
	printf(">>>>>> following error is normal\n");
	ret = in_save_ranking_challenge(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "save_ranking_challenge: eid=%d rank=%d"
	, eid, rank_time);

	eid = 549; rank_time = 1;
	sprintf(in_buffer, IN_SAVE_RANKING_CHALLENGE_PRINT, eid, rank_time);
	ret = in_save_ranking_challenge(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "save_ranking_challenge: eid=%d rank=%d"
	, eid, rank_time);

	return 0;
}

int test100_add_evil_message(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int recv_eid;
	int send_eid;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	recv_eid = 547;
	send_eid = 547;

	sprintf(in_buffer, "%d %d %s %s %s", recv_eid, send_eid, "me", "hello", "qwerty123");
	ret = in_add_evil_message(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test100_add_evil_message");


	return 0;
}

int test101_list_evil_message(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid;
	int start_num;
	int page_size;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 547;
	start_num = 0;
	page_size = 5;

	sprintf(in_buffer, "%d %d %d", eid, start_num, page_size);
	ret = in_list_evil_message(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test101_list_evil_message1");
	DEBUG_PRINT(0, "test101_list_evil_message:out_buffer=%s", out_buffer);

	eid = 547;
	start_num = 5;
	page_size = 5;

	sprintf(in_buffer, "%d %d %d", eid, start_num, page_size);
	ret = in_list_evil_message(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test101_list_evil_message2");
	DEBUG_PRINT(0, "test101_list_evil_message:out_buffer=%s", out_buffer);

	return 0;
}

int test102_read_evil_message(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid;
	long message_id;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 547;
	message_id = 1;

	sprintf(in_buffer, "%d %ld", eid, message_id);
	ret = in_read_evil_message(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test102_read_evil_message");

	DEBUG_PRINT(0, "test102:out_buffer=%s", out_buffer);

	return 0;
}

int test103_add_rank_reward_message(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;

	int start;
	int end;
	int eid;
	eid = 0;

	start = 0;
	end = 5;
	ret = add_rank_reward_message(pconn, q_buffer, start, end, eid, "系统", "恭喜你获得奖金", "哈哈哈哈啊哈获得奖金");
	ERROR_RETURN(ret, "test103_add_rank_reward_message");

	start = 5;
	end = 0;
	ret = add_rank_reward_message(pconn, q_buffer, start, end, eid, "系统", "可惜你没有获得奖金", "没有获得奖金");
	ERROR_RETURN(ret, "test103_add_rank_reward_message");

	return 0;
}

int test104_get_unread_message_count(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid;

	eid = 547;
	ret = get_unread_message_count(pconn, q_buffer, eid);
	ERROR_NEG_RETURN(ret, "test104_get_unread_message_count");

	eid = 1001;
	ret = get_unread_message_count(pconn, q_buffer, eid);
	ERROR_NEG_RETURN(ret, "test104_get_unread_message_count");

	return 0;
}

int test105_rank_reward(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid = 0;
	char alias[EVIL_ALIAS_MAX] = "system";

	int start1 = 1;
	int end1 = 5;
	int gold1 = 0;
	int crystal1 = 100;
	char title1[EVIL_MESSAGE_TITLE_MAX] = "title111";
	char message1[EVIL_MESSAGE_MAX] = "message111";

	int start2 = 6;
	int end2 = 0;
	int gold2 = 200;
	int crystal2 = 0;
	char title2[EVIL_MESSAGE_TITLE_MAX] = "title222";
	char message2[EVIL_MESSAGE_MAX] = "message222";

	char * ptr;
	ptr = in_buffer;
	ptr += sprintf(ptr, "%d %s", eid, alias);
	ptr += sprintf(ptr, " %d %d %d %d %s %s", start1, end1, gold1, crystal1, title1, message1);
	ret = in_rank_reward(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test105_rank_reward1");
	DEBUG_PRINT(0, "test105_rank_reward:out_buffer1=%s", out_buffer);

	ptr = in_buffer;
	ptr += sprintf(ptr, "%d %s", eid, alias);
	ptr += sprintf(ptr, " %d %d %d %d %s %s", start2, end2, gold2, crystal2, title2, message2);
	ret = in_rank_reward(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test105_rank_reward2");
	DEBUG_PRINT(0, "test105_rank_reward:out_buffer2=%s", out_buffer);

	return 0;
}

int test106_ranking_challenge(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int target_eid;
	int resp;

	ret = init_ranking_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_ranking_challenge: init_ranking_list fail");

	// no eid input
	bzero(in_buffer, sizeof(in_buffer));
	printf(">>>>>> following error is normal\n");
	ret = in_ranking_challenge(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-5-ret, "test_ranking_challenge: input_invalid");

	// target_eid_not_exist
	eid = 571, target_eid = 400, resp = 0;
	sprintf(in_buffer, "%d %d %d", eid, target_eid, resp);
	printf(">>>>>> following error is normal\n");
	ret = in_ranking_challenge(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-16-ret, "test_ranking_challenge: eid_not_exist");

	// normal test
	eid = 571, target_eid = 547;
	sprintf(in_buffer, "%d %d %d", eid, target_eid, resp);
	ret = in_ranking_challenge(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_ranking_challenge: normal_test");
	DEBUG_PRINT(ret, "test_ranking_challenge: out_buffer[%s]"
	, out_buffer);

	return 0;
}

int test107_check_login(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	char uid[EVIL_UID_MAX+5];
	char password[EVIL_PASSWORD_MAX+5];
	int channel;

	sprintf(uid, "qwerty123");
	sprintf(password, "ANYSDKPWD");
	channel = 234;

	sprintf(in_buffer, IN_CHECK_LOGIN_PRINT, uid, password, channel);
	ret = in_check_login(pconn, q_buffer, in_buffer, out_buffer);
	DEBUG_PRINT(0, "test107_check_login:out_buffer=%s", out_buffer);
	ERROR_RETURN(ret, "test107_check_login_error");

	/*
	// uid not unique
	sprintf(uid, "_");
	sprintf(password, "ANYSDKPWD");
	channel = 234;

	sprintf(in_buffer, IN_CHECK_LOGIN_PRINT, uid, password, channel);
	ret = in_check_login(pconn, q_buffer, in_buffer, out_buffer);
	DEBUG_PRINT(0, "test107_check_login:out_buffer=%s", out_buffer);
	ERROR_RETURN(ret, "test107_check_login_error");
	*/

	return 0;
}

int test108_lottery(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char * in_ptr;
	int len;

	int eid;
	int type;
	int gold;
	int crystal;
	int times;
	int card_list[10];
	char signals[31];

	/*
	eid = 547;
	gold = -100;
	crystal = 0;
	times = 1;
	bzero(card_list, sizeof(card_list));
	in_ptr = in_buffer;
	len = sprintf(in_ptr, "%d %d %d %d", eid, gold, crystal, times);
	sprintf(in_ptr + len, " %d", 21);

	DEBUG_PRINT(0, "test108_lottery:in_buffer=%s", in_buffer);

	ret = in_lottery(pconn, q_buffer, in_buffer, out_buffer);
	DEBUG_PRINT(0, "test108_lottery:out_buffer=%s", out_buffer);
	ERROR_RETURN(ret, "test108_lottery_error");
	*/

	eid = 547;
	type = 1;
	gold = -100;
	crystal = 0;
	times = 10;
	bzero(card_list, sizeof(card_list));
	memset(signals, '0', EVIL_SIGNAL_MAX);
	in_ptr = in_buffer;
	len = sprintf(in_ptr, "%d %d %d %d %d", eid, type, gold, crystal, times);
	len += sprintf(in_ptr + len, " %d %d %d %d %d %d %d %d %d %d"
	, 21, 22, 23, 24, 25, 26, 27, 28, 29, 31);
	sprintf(in_ptr + len, " %30s", signals);

	DEBUG_PRINT(0, "test108_lottery:in_buffer=%s", in_buffer);

	ret = in_lottery(pconn, q_buffer, in_buffer, out_buffer);
	DEBUG_PRINT(0, "test108_lottery:out_buffer=%s", out_buffer);
	ERROR_RETURN(ret, "test108_lottery_error");
	
	return 0;
}


int test109_exchange_gift(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid;
	char key_code[105];

	// it will effect eid(547) get gift card
//	int rand_skip_count = time(NULL) % 100;
//	for (int i = 0; i < rand_skip_count; i++) {
//		random();
//	}

	eid = 547;
	sprintf(key_code, "456");
	sprintf(in_buffer, "%d %s", eid, key_code);
	printf(">>>>>> following error is normal\n");
	ret = in_exchange_gift(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-16-ret, "test109_key_code_has_been_used");


	eid = 547;
	sprintf(key_code, "233");
	sprintf(in_buffer, "%d %s", eid, key_code);
	printf(">>>>>> following error is normal\n");
	ret = in_exchange_gift(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-26-ret, "test109_key_code_expired");

	eid = 547;
	sprintf(key_code, "123");
	sprintf(in_buffer, "%d %s", eid, key_code);
	printf(">>>>>> following error is normal\n");
	ret = in_exchange_gift(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-36-ret, "test109_channel_not_fit");
	
	eid = 551;
	sprintf(key_code, "123");
	sprintf(in_buffer, "%d %s", eid, key_code);
	printf(">>>>>> following error is normal\n");
	ret = in_exchange_gift(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-46-ret, "test109_user_has_exchange_gift");

	
	eid = 547;
	sprintf(key_code, "346");
	sprintf(in_buffer, "%d %s", eid, key_code);
	printf("normal test\n");
	ret = in_exchange_gift(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test109_normal_test");


	return 0;
}


int test110_gate(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid = 547;
	int gate_id = 1;
	int power_offset = 0;
	sprintf(in_buffer, "%d %d %d", eid, gate_id, power_offset);
	ret = in_gate(pconn, q_buffer, in_buffer, out_buffer);
	DEBUG_PRINT(0, "test110_gate:out_buffer=%s", out_buffer);
	ERROR_RETURN(ret, "test110_gate_error");

	eid = 547;
	gate_id = 1;
	power_offset = -10;
	sprintf(in_buffer, "%d %d %d", eid, gate_id, power_offset);
	ret = in_gate(pconn, q_buffer, in_buffer, out_buffer);
	DEBUG_PRINT(0, "test110_gate:out_buffer=%s", out_buffer);
	ERROR_RETURN(ret, "test110_gate_error");
	
	return 0;
}

int test111_power(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;

	int eid = 547;
	int power_offset = 0;
	INFO_PRINT(0, "test111:updaet_power1");
	ret = update_power(pconn, q_buffer, eid, power_offset);
	ERROR_NEG_RETURN(ret, "test111:update_power1");
	DEBUG_PRINT(0, "test111:power1=%d", ret);

	eid = 547;
	power_offset = -6;
	INFO_PRINT(0, "test111:updaet_power2");
	ret = update_power(pconn, q_buffer, eid, power_offset);
	ERROR_NEG_RETURN(ret, "test111:update_power2");
	DEBUG_PRINT(0, "test111:power2=%d", ret);

	eid = 547;
	power_offset = 1000;
	INFO_PRINT(0, "test111:updaet_power3");
	ret = update_power(pconn, q_buffer, eid, power_offset);
	ERROR_NEG_RETURN(ret, "test111:update_power3");
	DEBUG_PRINT(0, "test111:power3=%d", ret);

	eid = 547;
	power_offset = -1000;
	INFO_PRINT(0, "test111:updaet_power4");
	ret = update_power(pconn, q_buffer, eid, power_offset);
	ERROR_NEG_RETURN(ret, "test111:update_power4");
	DEBUG_PRINT(0, "test111:power4=%d", ret);

	eid = 547;
	power_offset = -90;
	INFO_PRINT(0, "test111:updaet_power5");
	ret = update_power(pconn, q_buffer, eid, power_offset);
	ERROR_NEG_RETURN(-2-ret, "test111:update_power5");
	DEBUG_PRINT(0, "test111:power5=%d", ret);

	eid = 547;
	power_offset = 10;
	INFO_PRINT(0, "test111:updaet_power6");
	ret = update_power(pconn, q_buffer, eid, power_offset);
	ERROR_NEG_RETURN(ret, "test111:update_power6");
	DEBUG_PRINT(0, "test111:power6=%d", ret);

	return 0;
}

int test112_update_gate_pos(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int gate_pos;

	eid = 547;
	gate_pos = 2;
	sprintf(in_buffer, "%d %d", eid, gate_pos);
	ret = in_update_gate_pos(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test112:update_gate_pos");
	DEBUG_PRINT(0, "test112:out_buffer=%s", out_buffer);

	eid = 547;
	gate_pos = 1;
	sprintf(in_buffer, "%d %d", eid, gate_pos);
	ret = in_update_gate_pos(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test112:update_gate_pos");
	DEBUG_PRINT(0, "test112:out_buffer=%s", out_buffer);

	return 0;
}


int test113_tower(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int tower_index;
	int win_flag;
	int hero_hp;

	int hp_buff;
	int res_buff;
	int energy_buff;

	eid = 547;
	tower_index = 1;
	sprintf(in_buffer, "%d %d", eid, tower_index);
	ret = in_tower(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower");
	DEBUG_PRINT(0, "test113:in_tower out_buffer=%s", out_buffer);


	eid = 547;
	tower_index = 1;
	win_flag = 1;
	hero_hp = 25;
	sprintf(in_buffer, "%d %d %d %d", eid, tower_index, win_flag, hero_hp);
	ret = in_tower_result(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_result");
	DEBUG_PRINT(0, "test113:in_tower_result out_buffer=%s", out_buffer);

	eid = 547;
	tower_index = 2;
	win_flag = 1;
	hero_hp = 22;
	sprintf(in_buffer, "%d %d %d %d", eid, tower_index, win_flag, hero_hp);
	ret = in_tower_result(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_result");
	DEBUG_PRINT(0, "test113:in_tower_result out_buffer=%s", out_buffer);

	eid = 547;
	tower_index = 3;
	win_flag = 1;
	hero_hp = 21;
	sprintf(in_buffer, "%d %d %d %d", eid, tower_index, win_flag, hero_hp);
	ret = in_tower_result(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_result");
	DEBUG_PRINT(0, "test113:in_tower_result out_buffer=%s", out_buffer);

	eid = 547;
	tower_index = 4;
	win_flag = 1;
	hero_hp = 20;
	sprintf(in_buffer, "%d %d %d %d", eid, tower_index, win_flag, hero_hp);
	ret = in_tower_result(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_result");
	DEBUG_PRINT(0, "test113:in_tower_result out_buffer=%s", out_buffer);

	eid = 547;
	tower_index = 5;
	win_flag = 1;
	hero_hp = 20;
	sprintf(in_buffer, "%d %d %d %d", eid, tower_index, win_flag, hero_hp);
	ret = in_tower_result(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_result");
	DEBUG_PRINT(0, "test113:in_tower_result out_buffer=%s", out_buffer);

	eid = 547;
	hp_buff = 10;
	res_buff = 0;
	energy_buff = 0;
	sprintf(in_buffer, "%d %d %d %d", eid, hp_buff, res_buff, energy_buff);
	ret = in_tower_buff(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_buff");
	DEBUG_PRINT(0, "test113:in_tower_buff out_buffer=%s", out_buffer);


	eid = 547;
	tower_index = 6;
	win_flag = 0;
	hero_hp = 20;
	sprintf(in_buffer, "%d %d %d %d", eid, tower_index, win_flag, hero_hp);
	ret = in_tower_result(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_result");
	DEBUG_PRINT(0, "test113:in_tower_result out_buffer=%s", out_buffer);


	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_tower_info(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_info");
	INFO_PRINT(0, "test113:in_tower_info out_buffer1=%s", out_buffer);

	eid = 547;
	tower_index = 1;
	sprintf(in_buffer, "%d %d", eid, tower_index);
	ret = in_tower(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower");
	DEBUG_PRINT(0, "test113:in_tower out_buffer=%s", out_buffer);

	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_tower_info(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_info");
	INFO_PRINT(0, "test113:in_tower_info out_buffer2=%s", out_buffer);


	eid = 547;
	tower_index = 1;
	win_flag = 1;
	hero_hp = 25;
	sprintf(in_buffer, "%d %d %d %d", eid, tower_index, win_flag, hero_hp);
	ret = in_tower_result(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_result");
	DEBUG_PRINT(0, "test113:in_tower_result out_buffer=%s", out_buffer);


	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_tower_info(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test113:in_tower_info");
	INFO_PRINT(0, "test113:in_tower_info out_buffer3=%s", out_buffer);

	return 0;
}

int test114_tower_ladder(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;

	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_tower_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test114:in_tower_ladder");
	INFO_PRINT(0, "test114:in_tower_ladder out_buffer=%s", out_buffer);

	eid = 545;
	sprintf(in_buffer, "%d", eid);
	ret = in_tower_ladder(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test114:in_tower_ladder");
	INFO_PRINT(0, "test114:in_tower_ladder out_buffer=%s", out_buffer);

	return 0;
}

int test115_tower_reward(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int start_pos;
	int end_pos;
	int battle_coin;

	start_pos = 1;
	end_pos = 1;
	battle_coin = 100;
	sprintf(in_buffer, "%d %d %d", start_pos, end_pos, battle_coin);
	ret = in_tower_reward(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test115:in_tower_reward");
	INFO_PRINT(0, "test115:in_tower_reward out_buffer=%s", out_buffer);

	start_pos = 2;
	end_pos = 3;
	battle_coin = 50;
	sprintf(in_buffer, "%d %d %d", start_pos, end_pos, battle_coin);
	ret = in_tower_reward(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test115:in_tower_reward");
	INFO_PRINT(0, "test115:in_tower_reward out_buffer=%s", out_buffer);

	start_pos = 4;
	end_pos = 0;
	battle_coin = 10;
	sprintf(in_buffer, "%d %d %d", start_pos, end_pos, battle_coin);
	ret = in_tower_reward(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test115:in_tower_reward");
	INFO_PRINT(0, "test115:in_tower_reward out_buffer=%s", out_buffer);

	return 0;
}

int test116_solo(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid;
	int solo_pos;

	eid = 547;
	solo_pos = 1;
	sprintf(in_buffer, "%d %d", eid, solo_pos);
	ret = in_solo(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test116:in_solo");
	INFO_PRINT(0, "test116:out_buffer=%s", out_buffer);

	return 0;
}

int test117_empty(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	return 0;
}

int test118_fight_robot(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int game_type;
	int eid1;
	int eid2;

	eid1=547; eid2=1; game_type=9;
	sprintf(in_buffer, "%d %d %d", game_type, eid1, eid2);
	ret = in_fight_robot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test118:in_fight_robot");
	INFO_PRINT(0, "test118:out_buffer=%s", out_buffer);

	return 0;
}

int test119_update_signals(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char signals[31];
	memset(signals, '0', EVIL_SIGNAL_MAX);

	eid=547;
	signals[10] = '1';
	sprintf(in_buffer, "%d %s", eid, signals);
	ret = in_update_signals(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test119:in_update_signals");
	INFO_PRINT(0, "test119:out_buffer=%s", out_buffer);

	return 0;
}

int test120_chapter(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int chapter_id;
	int stage_id;
	int power_offset;
	int solo_id;
	int stage_size;

	/*
	eid = 547;
	chapter_id = 1;
	char data[MAX_CHAPTER_STAGE+1];
	data[0] = '\0';
	
	ret = get_chapter_data(pconn, q_buffer, eid, chapter_id, data);
	ERROR_RETURN(ret, "test120:get_chapter_data_error");
	INFO_PRINT(0, "test120:data=%s", data);

	sprintf(data, "0999999999999999");
	ret = insert_chapter_data(pconn, q_buffer, eid, chapter_id, data);
	ERROR_RETURN(ret, "test120:insert_chapter_data_error");
	
	sprintf(data, "1099999999999999");
	ret = save_chapter_data(pconn, q_buffer, eid, chapter_id, data);
	ERROR_RETURN(ret, "test120:save_chapter_data_error");
	*/

	eid=547;
	chapter_id=1;
	stage_id=1;
	power_offset = -6;
	solo_id=2;
	stage_size = 10;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, power_offset, solo_id, stage_size);
	ret = in_chapter(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test120:in_chapter_error");
	INFO_PRINT(0, "test120:in_chapter_out_buffer=%s", out_buffer);

	return 0;
}

int test121_chapter_data(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	/*
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int chapter_id;
	int stage_size;

	eid=548;
	chapter_id=1;
	stage_size = 10;
	sprintf(in_buffer, "%d %d %d", eid, chapter_id, stage_size);
	ret = in_chapter_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test121:in_chapter_data_error");
	INFO_PRINT(0, "test121:in_chapter_data_out_buffer1=%s", out_buffer);

	eid=548;
	chapter_id=1;
	stage_size = 20;
	sprintf(in_buffer, "%d %d %d", eid, chapter_id, stage_size);
	ret = in_chapter_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test121:in_chapter_data_error");
	INFO_PRINT(0, "test121:in_chapter_data_out_buffer2=%s", out_buffer);

	eid=548;
	chapter_id=1;
	stage_size = 5;
	sprintf(in_buffer, "%d %d %d", eid, chapter_id, stage_size);
	ret = in_chapter_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test121:in_chapter_data_error");
	INFO_PRINT(0, "test121:in_chapter_data_out_buffer3=%s", out_buffer);
	*/

	return 0;
}

int test122_chapter_update_data(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	/*
	int ret;
	ret = 0;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int chapter_id;
	int stage_id;
	int star;
	int stage_size;
	int next_stage_size;

	eid=549;
	chapter_id=1;
	stage_size = 5;
	sprintf(in_buffer, "%d %d %d", eid, chapter_id, stage_size);
	ret = in_chapter_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_data_error");
	INFO_PRINT(0, "test122:in_chapter_data_out_buffer1=%s", out_buffer);

	// stage 1
	eid=549;
	chapter_id=1;
	stage_id = 1;
	star = 2;
	stage_size = 5;
	next_stage_size = 5;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// stage 1
	eid=549;
	chapter_id=1;
	stage_id = 1;
	star = 1;
	stage_size = 5;
	next_stage_size = 5;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// stage 2
	eid=549;
	chapter_id=1;
	stage_id = 2;
	star = 1;
	stage_size = 5;
	next_stage_size = 5;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// stage 3
	eid=549;
	chapter_id=1;
	stage_id = 3;
	star = 1;
	stage_size = 5;
	next_stage_size = 5;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// stage 4
	eid=549;
	chapter_id=1;
	stage_id = 4;
	star = 1;
	stage_size = 5;
	next_stage_size = 5;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// stage 2
	eid=549;
	chapter_id=1;
	stage_id = 2;
	star = 3;
	stage_size = 5;
	next_stage_size = 5;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// stage 5
	eid=549;
	chapter_id=1;
	stage_id = 5;
	star = 2;
	stage_size = 5;
	next_stage_size = 5;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 2, error
	eid=549;
	chapter_id=2;
	stage_id = 2;
	star = 2;
	stage_size = 6;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-26-ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 3 stage 1, error
	eid=549;
	chapter_id=3;
	stage_id = 1;
	star = 2;
	stage_size = 5;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-3-ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 1
	eid=549;
	chapter_id=2;
	stage_id = 1;
	star = 2;
	stage_size = 6;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 2
	eid=549;
	chapter_id=2;
	stage_id = 2;
	star = 1;
	stage_size = 6;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 3
	eid=549;
	chapter_id=2;
	stage_id = 3;
	star = 3;
	stage_size = 6;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 4
	eid=549;
	chapter_id=2;
	stage_id = 4;
	star = 1;
	stage_size = 6;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 5
	eid=549;
	chapter_id=2;
	stage_id = 5;
	star = 1;
	stage_size = 6;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 4
	eid=549;
	chapter_id=2;
	stage_id = 4;
	star = 2;
	stage_size = 6;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 5
	eid=549;
	chapter_id=2;
	stage_id = 5;
	star = 3;
	stage_size = 6;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 6
	eid=549;
	chapter_id=2;
	stage_id = 6;
	star = 3;
	stage_size = 6;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);

	// chapter 2 stage 6, update stage_size=7
	eid=549;
	chapter_id=2;
	stage_id = 6;
	star = 2;
	stage_size = 7;
	next_stage_size = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, chapter_id, stage_id, star, stage_size, next_stage_size);
	ret = in_chapter_update_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test122:in_chapter_update_data_error");
	INFO_PRINT(0, "test122:in_chapter_update_data_out_buffer2=%s", out_buffer);
	*/

	return 0;
}

int test123_hero_mission(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	// char in_buffer[DB_BUFFER_MAX+5];
	// char *out_buffer = in_buffer;

	int eid;
	int hero_id;
	int hp;
	int energy;
	char card_list[EVIL_CARD_MAX+1];
	bzero(card_list, sizeof(card_list));
	evil_hero_t hero_list[HERO_MAX+1];
	bzero(hero_list, sizeof(hero_list));

	eid=547;
	hero_id=1;
	ret = get_mission_hero(pconn, q_buffer, eid, hero_id, hp, energy);
	ERROR_RETURN(ret, "test123:get_mission_hero");

	eid=547;
	ret = get_card(pconn, q_buffer, eid, card_list);
	ERROR_RETURN(ret, "test123:get_mission_hero");

	eid=547;
	hero_id=1;
	hp=11;
	energy=5;
	ret = __set_mission_hero(pconn, q_buffer, eid, hero_id, hp, energy);
	ERROR_RETURN(ret, "test123:set_mission_hero");

	eid=547;
	hero_id=2;
	hp=10;
	energy=5;
	ret = insert_mission_hero(pconn, q_buffer, eid, hero_id, hp, energy);
	ERROR_RETURN(ret, "test123:insert_mission_hero");

	eid=547;
	ret = list_mission_hero(pconn, q_buffer, eid, hero_list);
	ERROR_NEG_RETURN(ret, "test123:list_mission_hero");

	eid=547;
	hero_id=1;
	evil_hero_mission_t mission_list[MAX_HERO_MISSION+1];
	ret = get_hero_mission(pconn, q_buffer, eid, hero_id, mission_list);
	ERROR_NEG_RETURN(ret, "test123:get_mission_hero");

	return 0;
}

int test124_mission_hero(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char *ptr;

	int eid;
	eid = 548;
	ptr = in_buffer;
	ptr += sprintf(ptr, "%d", eid);
	for (int i=0;i<HERO_MAX;i++) {
		ptr += sprintf(ptr, " %d %d", 10, 5);
	}
	ret = in_get_mission_hero(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test124:in_get_mission_hero");
	DEBUG_PRINT(0, "test124:in_get_mission_hero out_buffer=%s", out_buffer);
	

	return 0;
}

int test125_hero_mission(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid;
	int hero_id;
	
	eid = 547;
	hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_get_hero_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test125:in_get_hero_mission");
	DEBUG_PRINT(0, "test125:in_get_hero_mission out_buffer=%s", out_buffer);
	

	return 0;
}

int test126_update_hero_mission(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int hero_id;
	int mission_id;
	int status;
	int n1;

	eid = 547; hero_id = 1; mission_id = 3; status = 1; n1 = 1;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, hero_id, 1, mission_id
	, status, n1);
	ret = in_update_hero_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test126_update_hero_mission_not_exists");

	eid = 547; hero_id = 1; mission_id = 2; status = 2; n1 = 1;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, hero_id, 1, mission_id
	, status, n1);
	ret = in_update_hero_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test126_update_hero_mission:no_mission_changed");

	// normal test
	eid = 547; hero_id = 1; mission_id = 1; status = 3; n1 = 1;
	sprintf(in_buffer, "%d %d %d %d %d %d", eid, hero_id, 1, mission_id
	, status, n1);
	ret = in_update_hero_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test126_update_hero_mission %d %d %d", eid
	, hero_id, mission_id);
	DEBUG_PRINT(1, "out_buffer[%s]", out_buffer);

	return 0;
}

int test127_submit_hero_mission(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid;
	int hero_id;
	int mission_id;
	int reward_type;
	int reward_count;
	
	eid = 547; hero_id = 1; mission_id = 1;
	reward_type = 1; reward_count = 1;
	sprintf(in_buffer, "%d %d %d %d %d", eid, hero_id, mission_id
	, reward_type, reward_count);
	printf(">>>>>>>>>>>> Error below is normal >>>>>>>>>>>>\n");
	ret = in_submit_hero_mission(pconn, q_buffer, in_buffer, out_buffer);
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	ERROR_RETURN(-6-ret, "test127:in_submit_hero_mission");
	
	eid = 547; hero_id = 1; mission_id = 2;
	reward_type = 1; reward_count = 1;
	sprintf(in_buffer, "%d %d %d %d %d", eid, hero_id, mission_id
	, reward_type, reward_count);
	ret = in_submit_hero_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test127:in_submit_hero_mission");
	DEBUG_PRINT(1, "out_buffer[%s]", out_buffer);
	
	eid = 547; hero_id = 2; mission_id = 1;
	reward_type = 1; reward_count = 1;
	sprintf(in_buffer, "%d %d %d %d %d", eid, hero_id, mission_id
	, reward_type, reward_count);
	ret = in_submit_hero_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test127:in_submit_hero_mission");

	return 0;
}
	
int test128_refresh_hero_mission(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char *ptr;

	int eid;
	int hero_id;
	int card_id;

	eid = 547; hero_id = 3;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_get_hero_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test128:get_hero_mission %d %d"
	, eid, hero_id);
	DEBUG_PRINT(0, "test128:out_buffer[%s]", out_buffer);
	
	// Add hero(2) to 547
	eid = 547; card_id = 2;
	ret = db_add_card(pconn, q_buffer, eid, card_id);
	ERROR_RETURN(ret, "test128:add_card_fail %d %d"
	, eid, card_id);
	// Add hero(3) to 547
	eid = 547; card_id = 3;
	ret = db_add_card(pconn, q_buffer, eid, card_id);
	ERROR_RETURN(ret, "test128:add_card_fail %d %d"
	, eid, card_id);

	eid = 547;
	ptr = in_buffer;
	ptr += sprintf(ptr, "%d", eid);
	for (int i=0;i<HERO_MAX;i++) {
		ptr += sprintf(ptr, " %d %d", 10, 5);
	}
	ret = in_get_mission_hero(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test128:in_get_mission_hero_fail");
	DEBUG_PRINT(0, "out_buffer[%s]", out_buffer);

	eid = 547; hero_id = 3;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_get_hero_mission(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test128:get_hero_mission %d %d"
	, eid, hero_id);
	DEBUG_PRINT(0, "test128:out_buffer[%s]", out_buffer);

	return 0;
}

int test129_load_hero_data(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char *ptr;
	int eid;

	eid = 547;
	ptr = in_buffer;
	ptr += sprintf(ptr, "%d", eid);
	for (int i=0;i<HERO_MAX;i++) {
		ptr += sprintf(ptr, " %d %d", 10, 5);
	}
	ret = in_load_hero_data(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test129:load_hero_data_out_buffer[%s]", out_buffer);

	return 0;
}

int test130_reset_chapter_data(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	/*
	int ret;

	int chapter_id;
	int stage_size;
	int init_flag;
	char data[MAX_CHAPTER_STAGE + 1];
	int data_len = 0;

	// test 1, empty first chapter data
	chapter_id = 1;
	stage_size = 10;
	init_flag = 1;
	data[0] = '\0';
	DEBUG_PRINT(0, "test130:empty_first_data [%s]", data);
	ret = reset_chapter_data(chapter_id, stage_size, init_flag, data);
	DEBUG_PRINT(0, "test130:ret=%d data=[%s]", ret, data);

	// test 2, empty behind chapter data
	chapter_id = 2;
	stage_size = 10;
	init_flag = 0;
	data[0] = '\0';
	DEBUG_PRINT(0, "test130:empty_behind_data [%s]", data);
	ret = reset_chapter_data(chapter_id, stage_size, init_flag, data);
	DEBUG_PRINT(0, "test130:ret=%d data=[%s]", ret, data);

	// test 3, match data
	chapter_id = 2;
	stage_size = 10;
	init_flag = 0;
	data_len = stage_size;
	memset(data, CHAPTER_DATA_LOCK, data_len);
	data[0] = CHAPTER_DATA_START;
	data[data_len] = '\0';
	DEBUG_PRINT(0, "test130:match_data [%s]", data);
	ret = reset_chapter_data(chapter_id, stage_size, init_flag, data);
	DEBUG_PRINT(0, "test130:ret=%d data=[%s]", ret, data);

	// test 4, over data
	chapter_id = 2;
	stage_size = 10;
	init_flag = 0;
	data_len = stage_size+2;
	memset(data, CHAPTER_DATA_LOCK, data_len);
	data[0] = CHAPTER_DATA_START;
	data[data_len] = '\0';
	DEBUG_PRINT(0, "test130:over_data [%s]", data);
	ret = reset_chapter_data(chapter_id, stage_size, init_flag, data);
	DEBUG_PRINT(0, "test130:ret=%d data=[%s]", ret, data);

	// test 5, less data
	chapter_id = 2;
	stage_size = 10;
	init_flag = 0;
	data_len = 1;
	memset(data, CHAPTER_DATA_START, data_len);
	data[data_len] = '\0';
	DEBUG_PRINT(0, "test130:less_data [%s]", data);
	ret = reset_chapter_data(chapter_id, stage_size, init_flag, data);
	DEBUG_PRINT(0, "test130:ret=%d data=[%s]", ret, data);
	*/
	


	return 0;
}


int test131_hero_slot(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid;
	int hero_id;
	int slot_id;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	// char slot[EVIL_CARD_MAX+1];
//	const char *SLOT = "0000100000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001";
	const char *SLOT = "1000000000000000000000320033333333330000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_load_hero_deck(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_load_hero_deck:normal_test");
	INFO_PRINT(0, "test131_load_hero_deck:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_list_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_list_hero_slot:normal_test");
	INFO_PRINT(1, "test131_list_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1, slot_id = 1; 
	sprintf(in_buffer, "%d %d %d %.400s", eid, hero_id, slot_id
	, SLOT);
	ret = in_update_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_update_hero_slot: normal_test");
	INFO_PRINT(0, "test131_update_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_list_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_list_hero_slot:normal_test");
	INFO_PRINT(1, "test131_list_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1, slot_id = 1;
	sprintf(in_buffer, "%d %d %d", eid, hero_id, slot_id);
	ret = in_get_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_test_get_hero_slot: normal_test");
	INFO_PRINT(1, "test131_get_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1; slot_id = 1;
	sprintf(in_buffer, "%d %d %d", eid, hero_id, slot_id);
	ret = in_choose_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_choose_hero_slot: normal_test");
	INFO_PRINT(1, "test131_choose_hero_slot:out_buffer[%s]", out_buffer);

	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_load_hero_deck(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_load_hero_deck:normal_test");
	INFO_PRINT(0, "test131_load_hero_deck:out_buffer=[%s]", out_buffer);

	printf("----------------------------\n");

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_list_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_list_hero_slot:normal_test");
	INFO_PRINT(1, "test131_list_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1, slot_id = 2;
	sprintf(in_buffer, "%d %d %d", eid, hero_id, slot_id);
	ret = in_get_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_test_get_hero_slot: normal_test");
	INFO_PRINT(1, "test131_get_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1, slot_id = 2; 
	sprintf(in_buffer, "%d %d %d %.400s", eid, hero_id, slot_id
	, SLOT);
	ret = in_update_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_update_hero_slot: normal_test");
	INFO_PRINT(0, "test131_update_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1; slot_id = 2;
	sprintf(in_buffer, "%d %d %d", eid, hero_id, slot_id);
	ret = in_choose_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_choose_hero_slot: normal_test");
	INFO_PRINT(1, "test131_choose_hero_slot:out_buffer[%s]", out_buffer);

	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_load_hero_deck(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_load_hero_deck:normal_test");
	INFO_PRINT(0, "test131_load_hero_deck:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1, slot_id = 2; 
	sprintf(in_buffer, "%d %d %d %.400s", eid, hero_id, slot_id
	, SLOT);
	ret = in_update_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_update_hero_slot: normal_test");
	INFO_PRINT(0, "test131_update_hero_slot:out_buffer=[%s]", out_buffer);

	// before choose updated slot
	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_load_hero_deck(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_load_hero_deck:normal_test");
	INFO_PRINT(0, "test131_load_hero_deck:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_list_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_list_hero_slot:normal_test");
	INFO_PRINT(1, "test131_list_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1; slot_id = 2;
	sprintf(in_buffer, "%d %d %d", eid, hero_id, slot_id);
	ret = in_choose_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_choose_hero_slot: normal_test");
	INFO_PRINT(1, "test131_choose_hero_slot:out_buffer[%s]", out_buffer);

	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_load_hero_deck(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_load_hero_deck:normal_test");
	INFO_PRINT(0, "test131_load_hero_deck:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_list_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_list_hero_slot:normal_test");
	INFO_PRINT(1, "test131_list_hero_slot:out_buffer=[%s]", out_buffer);

	printf("----------------------------\n");

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_list_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_list_hero_slot:normal_test");
	INFO_PRINT(1, "test131_list_hero_slot:out_buffer=[%s]", out_buffer);

	// 10 times
	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_insert_hero_slot: normal_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_list_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_list_hero_slot:normal_test");
	INFO_PRINT(1, "test131_list_hero_slot:out_buffer=[%s]", out_buffer);

	// 11 times
	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_insert_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-55-ret, "test131_insert_hero_slot:normal_error_test");
	INFO_PRINT(0, "test131_insert_hero_slot:out_buffer=[%s]", out_buffer);

	eid = 547, hero_id = 1;
	sprintf(in_buffer, "%d %d", eid, hero_id);
	ret = in_list_hero_slot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test131_list_hero_slot:normal_test");
	INFO_PRINT(1, "test131_list_hero_slot:out_buffer=[%s]", out_buffer);

	return 0;
}


int test_nscan(char *in)
{
	char *out;
	int ret;

	for (int i=0; i<100; i++) { // max 100
		printf("in=[%s]   ", in);
		ret = nscan2(in, &out);
		ERROR_NEG_RETURN(ret, "test_nscan");
		in += ret;
		printf("ret=%d  in+ret=[%s]  out=[%s]\n", ret, in, out);
		if (ret == 0) {
			break;
		}
	}
	printf("---------------------\n\n");
	return ret;
}

int test_nscan2(char *in)
{
	char *out;
	int ret;

	for (int i=0; i<100; i++) { // max 100
		printf("in=[%s]   ", in);
		ret = nscan3(in, &out);
		ERROR_NEG_RETURN(ret, "test_nscan2");
		in += ret;
		printf("ret=%d  in+ret=[%s]  out=[%s]\n", ret, in, out);
		if (ret == 0) {
			break;
		}
	}
	printf("---------------------\n\n");
	return ret;
}

int test132_nscan(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	char in[200];
	strcpy(in, "3 a b c 2 x y");

	char out[200];
	char *ptr;
	ptr = in;
	int ret;

	ret = nscan_copy(ptr, out);
	ERROR_NEG_RETURN(ret, "test132:nscan");

	DEBUG_PRINT(0, "out=[%s]", out);
	ptr += ret;
	DEBUG_PRINT(0, "ptr=[%s]", ptr);

	ret = nscan_copy(ptr, out);
	ERROR_NEG_RETURN(ret, "test132:nscan");

	DEBUG_PRINT(0, "out=[%s]", out);
	ptr += ret;
	DEBUG_PRINT(0, "ptr=[%s]", ptr);


	printf("---------------------\n\n");

	// case 1
	strcpy(in, "3 aa   b c23 3 x y z  ");
	ret = test_nscan(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 1");

	// case 2:
	strcpy(in, "3 aa   b c23 3 x y z");
	ret = test_nscan(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 2");

	strcpy(in, "0  0");
	ret = test_nscan(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 3");

	strcpy(in, "0  0  ");
	ret = test_nscan(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 4");

	strcpy(in, "0 0");
	ret = test_nscan(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 5");

	printf("---- following 2 error is normal\n");
	strcpy(in, "3 a b");
	ret = test_nscan(in);
	ERROR_RETURN(ret != -12, "test132:nscan 6");

	printf("---- following 2 error is normal\n");
	strcpy(in, "3 a b ");
	ret = test_nscan(in);
	ERROR_RETURN(ret != -12, "test132:nscan 7");

	strcpy(in, "0 3 a5b 6bx 8c8 0");
	ret = test_nscan(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 8");

	strcpy(in, " 0 3 aaa bbb ccc 2 xx yy   ");
	ret = test_nscan(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 9");

	strcpy(in, " 3 aaa bbb ccc xx yy   ");
	ret = test_nscan(in);
	ERROR_RETURN(ret != -5, "test132:nscan 10");

	printf("\n----------------TEST nscan3()-----------------------\n\n");

	// case 1
	strcpy(in, "3 aa   b c23 3 x y z  ");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 1");

	// case 2:
	strcpy(in, "3 aa   b c23 3 x y z");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 2");

	strcpy(in, "0  0");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 3");

	strcpy(in, "0  0  ");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 4");

	strcpy(in, "0 0");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 5");

	printf("---- following 2 error is normal\n");
	strcpy(in, "3 a b");
	ret = test_nscan2(in);
	ERROR_RETURN(ret != -25, "test132:nscan 6");

	printf("---- following 2 error is normal\n");
	strcpy(in, "3 a b ");
	ret = test_nscan2(in);
	ERROR_RETURN(ret != -35, "test132:nscan 7");

	strcpy(in, "0 3 a5b 6bx 8c8 0");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 8");

	strcpy(in, " 0 3 aaa bbb ccc 2 xx yy   ");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 9");

	strcpy(in, " 3 aaa bbb ccc xx yy   ");
	ret = test_nscan2(in);
	ERROR_RETURN(ret != -5, "test132:nscan 10");

	strcpy(in, " 3 aaa bbb ccc 2 xx yy   ");
	strcpy(in, " 3 aaa bbb ccc");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 11");

	strcpy(in, " 0 1 a");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 12");

	strcpy(in, " 0 1 a   ");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 13");

	strcpy(in, "0");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 14");

	strcpy(in, "1 a");
	ret = test_nscan2(in);
	ERROR_NEG_RETURN(ret, "test132:nscan 14");

	
	return 0;
}

	
int test133_daily_login(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{		
	int ret;
	int eid;
//	time_t last_login;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 552;
	ret = __insert_daily_login(pconn, q_buffer, eid);
	ERROR_RETURN(ret, "test133_daily_login:insert_normal_test");

	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_get_daily_login(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test133_daily_login:get_normal_test");
	DEBUG_PRINT(0, "get_daily_login: out_buffer[%s]", out_buffer);

//	eid = 547;
//	last_login = time(NULL);
//	ret = __update_daily_login(pconn, q_buffer, eid, last_login);
//	ERROR_RETURN(ret, "test133_daily_login:update_normal_test");

	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_get_daily_login(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test133_daily_login:get_normal_test");
	DEBUG_PRINT(0, "get_daily_login: out_buffer[%s]", out_buffer);

	return 0;
}

int test134_get_daily_reward(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	int eid;
	int log_day;
	int gold;
	int crystal;
	int card_count;
	int piece_count;
	int cards[MAX_DAILY_REWARD_CARD+1];
	int pieces[MAX_DAILY_REWARD_PIECE+1][2];
	char *in_ptr;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	eid = 547; log_day = 1; gold = 300; crystal = 5;
	card_count = 1; piece_count = 2;
	cards[0] = 36;
	pieces[0][0] = 3; pieces[0][1] = 1;
	pieces[1][0] = 22; pieces[1][1] = 3;
	in_ptr = in_buffer;
	in_ptr += sprintf(in_ptr, "%d %d %d %d %d", eid, log_day
	, gold, crystal, card_count);
	for (int i = 0; i < card_count; i++) {
		in_ptr += sprintf(in_ptr, " %d", cards[i]);
	}
	in_ptr += sprintf(in_ptr, " %d", piece_count);
	for (int i = 0; i < piece_count; i++) {
		in_ptr += sprintf(in_ptr, " %d %d", pieces[i][0], pieces[i][1]);
	}
	ret = in_get_daily_reward(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test134_daily_reward:reward_normal_test");
	DEBUG_PRINT(0, "out_buffer[%s]", out_buffer);


	return 0;
}

int test135_piece_shop(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int pos;
	int card_id;
	int count;
	int gold;
	int crystal;
	int cost;
	evil_piece_shop_t shop;
	bzero(&shop, sizeof(shop));

	eid = 547;
	ret = __get_piece_shop(pconn, q_buffer, eid, shop);
	DEBUG_PRINT(1
	, "test135:get_piece_shop last_time[%ld] refresh_times[%d] show_times[%d]"
	, shop.last_time, shop.refresh_times, shop.show_times);
	printf("shop_pid:buy_flag ");
	for (int i = 0; i < MAX_PIECE_SHOP_SLOT; i++)
	{
		printf(" [%d:%d]", shop.pid_list[i], shop.buy_flag_list[i]);
	}
	printf("\n");

	sprintf(in_buffer, "%d", eid);
	ret = in_get_piece_shop(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test135_piece_shop:get_piece_shop");
	INFO_PRINT(0, "test135:get_piece_shop out_buffer=[%s]", out_buffer);

	eid = 547;
	shop.last_time = 6789; shop.refresh_times = 2; shop.show_times = 3;
	shop.pid_list[0] = 6; shop.buy_flag_list[0] = 0;
	shop.pid_list[1] = 5; shop.buy_flag_list[1] = 1;
	shop.pid_list[2] = 4; shop.buy_flag_list[2] = 0;
	shop.pid_list[3] = 3; shop.buy_flag_list[3] = 1;
	shop.pid_list[4] = 2; shop.buy_flag_list[4] = 0;
	shop.pid_list[5] = 1; shop.buy_flag_list[5] = 1;
	sprintf(in_buffer, "%d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, eid, shop.last_time, shop.refresh_times, shop.show_times
	, shop.pid_list[0], shop.buy_flag_list[0]
	, shop.pid_list[1], shop.buy_flag_list[1]
	, shop.pid_list[2], shop.buy_flag_list[2]
	, shop.pid_list[3], shop.buy_flag_list[3]
	, shop.pid_list[4], shop.buy_flag_list[4]
	, shop.pid_list[5], shop.buy_flag_list[5]
	);
	ret = in_update_piece_shop(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test135_piece_shop:update_piece_shop");

	eid = 547;
	shop.last_time = 9988; shop.refresh_times = 3; shop.show_times = 4;
	shop.pid_list[0] = 3; shop.buy_flag_list[0] = 0;
	shop.pid_list[1] = 2; shop.buy_flag_list[1] = 0;
	shop.pid_list[2] = 8; shop.buy_flag_list[2] = 1;
	shop.pid_list[3] = 1; shop.buy_flag_list[3] = 0;
	shop.pid_list[4] = 6; shop.buy_flag_list[4] = 1;
	shop.pid_list[5] = 9; shop.buy_flag_list[5] = 0;
	cost = -30;
	sprintf(in_buffer, "%d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d"
	, eid, shop.last_time, shop.refresh_times, shop.show_times
	, shop.pid_list[0], shop.buy_flag_list[0]
	, shop.pid_list[1], shop.buy_flag_list[1]
	, shop.pid_list[2], shop.buy_flag_list[2]
	, shop.pid_list[3], shop.buy_flag_list[3]
	, shop.pid_list[4], shop.buy_flag_list[4]
	, shop.pid_list[5], shop.buy_flag_list[5]
	, cost
	);
	ret = in_refresh_piece_shop(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test135_piece_shop:refresh_piece_shop");
	DEBUG_PRINT(1, "test135_out_buffer[%s]", out_buffer);


	eid = 547; pos = 1; card_id = 21; count = 8; gold = -300; crystal = 0;
	sprintf(in_buffer, "%d %d %d %d %d %d"
	, eid, pos, card_id, count, gold, crystal);
	ret = in_piece_buy(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test135_piece_shop:piece_buy_error");
	DEBUG_PRINT(1, "test135_out_buffer[%s]", out_buffer);

	return 0;
}


int test136_admin_add_robot(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	char username[EVIL_USERNAME_MAX+1];
	char password[EVIL_PASSWORD_MAX+1];
	int icon;
	int hp;
	int energy;
	char alias[EVIL_ALIAS_MAX+1];
	char deck[EVIL_CARD_MAX+1];

	strcpy(username, "robot_user");
	strcpy(password, "2015");
	strcpy(alias, "robot_alias");
	icon = 3;
	hp = 30;
	energy = 0;
	sprintf(deck, "%.400s", "0000100000000000000002200321030000000000000000000000000000000000000000221212002200000000000000000000000000000000000000000000000000111110000000000000002002200000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001");

	sprintf(in_buffer, "%s %s %s %d %d %d %.400s"
	, username, password, alias, icon, hp, energy, deck);

	ret = in_admin_add_robot(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test136_admin_add_robot:add_robot_error");


	return 0;
}

int test137_admin_init_arena(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int start_eid;

	start_eid = 549;
	sprintf(in_buffer, "%d", start_eid);
	ret = in_init_arena(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test137_init_arena:error");

	start_eid = 0;
	sprintf(in_buffer, "%d", start_eid);
	ret = in_init_arena(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test137_init_arena:error");

	return 0;
}

int test138_arena_top_list(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;

	eid = 547;
	sprintf(in_buffer, "%d", eid);
	ret = in_arena_top_list(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test138_arena_top_list:error");
	INFO_PRINT(0, "test138:arena_top_list_out_buffer=[%s]", out_buffer);

	return 0;
}

int test139_get_arena_target_list(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{

	int target_count;
	int rank;
	int target_rank_list[RANKING_RANDOM_COUNT];
	bzero(target_rank_list, sizeof(target_rank_list));

	rank = 1;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 2;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 4;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 5;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 6;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 7;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 18;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 19;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 20;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 21;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 22;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);

	rank = 30;
	target_count = __get_arena_target_list(target_rank_list, rank);
	DEBUG_PRINT(0, "test139:rank=%d target_count=%d target_rank_list=[%d][%d][%d][%d][%d]"
	, rank, target_count, target_rank_list[0], target_rank_list[1], target_rank_list[2]
	, target_rank_list[3], target_rank_list[4]);


	return 0;
}

int test140_list_arena_target(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;

	sprintf(in_buffer, "%d", 0);
	ret = in_init_arena(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test140_list_arena_target:init_error");

	eid = 549;
	sprintf(in_buffer, "%d", eid);
	ret = in_arena_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test140_list_arena_target:error");
	INFO_PRINT(0, "test140:out_buffer=[%s]", out_buffer);

	eid = 550;
	sprintf(in_buffer, "%d", eid);
	ret = in_arena_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test140_list_arena_target:error");
	INFO_PRINT(0, "test140:out_buffer=[%s]", out_buffer);

	eid = 572;
	sprintf(in_buffer, "%d", eid);
	ret = in_arena_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test140_list_arena_target:error");
	INFO_PRINT(0, "test140:out_buffer=[%s]", out_buffer);

	eid = 573;
	sprintf(in_buffer, "%d", eid);
	ret = in_arena_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test140_list_arena_target:error");
	INFO_PRINT(0, "test140:out_buffer=[%s]", out_buffer);

	eid = 505;
	sprintf(in_buffer, "%d", eid);
	ret = in_arena_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test140_list_arena_target:error");
	INFO_PRINT(0, "test140:out_buffer=[%s]", out_buffer);

	eid = 511;
	sprintf(in_buffer, "%d", eid);
	ret = in_arena_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test140_list_arena_target:error");
	INFO_PRINT(0, "test140:out_buffer=[%s]", out_buffer);

	eid = 515;
	sprintf(in_buffer, "%d", eid);
	ret = in_arena_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test140_list_arena_target:error");
	INFO_PRINT(0, "test140:out_buffer=[%s]", out_buffer);

	eid = 548;
	sprintf(in_buffer, "%d", eid);
	ret = in_arena_target(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test140_list_arena_target:error");
	INFO_PRINT(0, "test140:out_buffer=[%s]", out_buffer);


	return 0;
}

int test141_exchange_arena_rank(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid_challenger;
	int eid_receiver;
	int challenger_rank;
	int receiver_rank;

	eid_challenger = 547;
	eid_receiver = 550;
	ret = __get_arena_rank(pconn, q_buffer, eid_challenger, eid_receiver
	, challenger_rank, receiver_rank);
	DEBUG_PRINT(0, "challenger[%d][%d] receiver[%d][%d]"
	, eid_challenger, challenger_rank, eid_receiver, receiver_rank);
	sprintf(in_buffer, "%d %d", eid_challenger, eid_receiver);
	ret = in_exchange_arena_rank(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test141_exchange_arena_rank:error");
	INFO_PRINT(0, "test141:out_buffer=[%s]", out_buffer);

	ret = __get_arena_rank(pconn, q_buffer, eid_challenger, eid_receiver
	, challenger_rank, receiver_rank);
	DEBUG_PRINT(0, "challenger[%d][%d] receiver[%d][%d]"
	, eid_challenger, challenger_rank, eid_receiver, receiver_rank);


	eid_challenger = 547;
	eid_receiver = 550;
//	sprintf(in_buffer, "%d %d", eid_challenger, eid_receiver);
	sprintf(in_buffer, "%d %d", eid_receiver, eid_challenger);
	ret = in_exchange_arena_rank(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test141_exchange_arena_rank:error");
	INFO_PRINT(0, "test141:out_buffer=[%s]", out_buffer);

	ret = __get_arena_rank(pconn, q_buffer, eid_challenger, eid_receiver
	, challenger_rank, receiver_rank);
	DEBUG_PRINT(0, "challenger[%d][%d] receiver[%d][%d]"
	, eid_challenger, challenger_rank, eid_receiver, receiver_rank);

	return 0;
}

int test142_arena_game(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	int eid_challenger;
	int eid_receiver;

	eid_challenger = 508;
	eid_receiver = 506;
	sprintf(in_buffer, "%d %d", eid_challenger, eid_receiver);
	ret = in_arena_game(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test142_arena_game:error");
	INFO_PRINT(0, "test142:out_buffer=[%s]", out_buffer);

	eid_challenger = 506;
	eid_receiver = 508;
	sprintf(in_buffer, "%d %d", eid_challenger, eid_receiver);
	ret = in_arena_game(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-66-ret, "test142_arena_game:normal_not_target_error");
	INFO_PRINT(0, "test142:out_buffer=[%s]", out_buffer);

	return 0;
}

int test143_update_arena_times(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int offset;
	int gold;
	int crystal;

	eid = 547;
	offset = -1;
	gold = 0;
	crystal = 0;
	sprintf(in_buffer, "%d %d %d %d", eid, offset, gold, crystal);
	ret = in_update_arena_times(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test143_update_arena_times:normal");
	INFO_PRINT(0, "test143:out_buffer=[%s]", out_buffer);

	eid = 547;
	offset = 5;
	gold = -1000;
	crystal = 0;
	sprintf(in_buffer, "%d %d %d %d", eid, offset, gold, crystal);
	ret = in_update_arena_times(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test143_update_arena_times:normal");
	INFO_PRINT(0, "test143:out_buffer=[%s]", out_buffer);

	eid = 547;
	offset = -15;
	gold = 0;
	crystal = 0;
	sprintf(in_buffer, "%d %d %d %d", eid, offset, gold, crystal);
	ret = in_update_arena_times(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-26-ret, "test143_update_arena_times:normal_arena_times_error");
	INFO_PRINT(0, "test143:out_buffer=[%s]", out_buffer);

	return 0;
}

int test144_get_arena_reward(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	char *in_ptr;
	int eid;
	int reward_count;
	design_arena_reward_t reward_list[MAX_ARENA_REWARD] = {
		{ 1, 1, 1, 350, 0, "1", "1_desc" }
	,	{ 2, 2, 2, 300, 0, "2", "2_desc" }
	,	{ 3, 3, 3, 250, 0, "3", "3_desc" }
	,	{ 4, 4, 4, 200, 0, "4", "4_desc" }
	,	{ 5, 5, 5, 150, 0, "5", "5_desc" }
	,	{ 6, 6, 10, 100, 0, "6-10", "6-10_desc" }
	,	{ 7, 11, 100, 60, 0, "11-100", "11-100_desc" }
	,	{ 8, 101, 300, 50, 0, "101-300", "101-300_desc" }
	,	{ 9, 301, 500, 40, 0, "301-500", "301-500_desc" }
	,	{ 10, 501, 750, 30, 0, "501-750", "501-750_desc" }
	,	{ 11, 751, 1000, 20, 0, "750-1000", "750-1000_desc" }
	,	{ 12, 1001, 0, 10, 0, "1001-", "1001_desc" }
	};

	sprintf(in_buffer, "%d", 0);
	ret = in_init_arena(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test144_in_init_arena");

	eid = 547; reward_count = 12;
	in_ptr = in_buffer;
	in_ptr += sprintf(in_ptr, "%d %d", eid, reward_count);
	for (int i = 0; i < reward_count; i++) {
		in_ptr += sprintf(in_ptr, " %d %d %d %d", reward_list[i].start
		, reward_list[i].end, reward_list[i].gold, reward_list[i].crystal);
	}
	ret = in_get_arena_reward(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test144_get_arena_reward:normal");
	INFO_PRINT(0, "test144:out_buffer=[%s]", out_buffer);

	eid = 547; reward_count = 12;
	in_ptr = in_buffer;
	in_ptr += sprintf(in_ptr, "%d %d", eid, reward_count);
	for (int i = 0; i < reward_count; i++) {
		in_ptr += sprintf(in_ptr, " %d %d %d %d", reward_list[i].start
		, reward_list[i].end, reward_list[i].gold, reward_list[i].crystal);
	}
	printf(">>>>>>>> error below is normal >>>>>>>>\n");
	ret = in_get_arena_reward(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(-6-ret, "test144_get_arena_reward:has_reward");


	return 0;
}

int test145_get_chapter(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int chapter_id;
	int stage_id;
	int star;
	db_chapter_t chapter_list[MAX_CHAPTER+1];
	bzero(chapter_list, sizeof(chapter_list));

	eid = 549;
	ret = get_chapter(pconn, q_buffer, eid, chapter_list);
	ERROR_NEG_RETURN(ret, "test145_get_chapter:normal");

	for (int i=0; i<MAX_CHAPTER+1; i++) {
		db_chapter_t *chapter = chapter_list + i;
		if (chapter->id == 0) {continue;}
		DEBUG_PRINT(0, "test145:get_chapter chapter_id=%d data=%s", chapter->id, chapter->data);
	}

	eid = 549;
	chapter_id = 1;
	stage_id = 2;
	star = 3;
	sprintf(in_buffer, "%d %d %d %d", eid, chapter_id, stage_id, star);
	ret = in_get_chapter(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test145:normal");
	INFO_PRINT(0, "test145:out_buffer=[%s]", out_buffer);

	eid = 547;
	chapter_id = 1;
	stage_id = 3;
	star = 0;
	sprintf(in_buffer, "%d %d %d %d", eid, chapter_id, stage_id, star);
	ret = in_get_chapter(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test145:normal");
	INFO_PRINT(0, "test145:out_buffer=[%s]", out_buffer);

	return 0;
}

int test146_replace_chapter(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int size;

	eid = 547;
	size = 2;
	sprintf(in_buffer, "%d %d %d %s %d %s", eid, size, 1, "0123123", 2, "2389999");
	ret = in_replace_chapter(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test146:normal");
	INFO_PRINT(0, "test146:out_buffer=[%s]", out_buffer);

	eid = 548;
	size = 1;
	sprintf(in_buffer, "%d %d %d %s", eid, size, 1, "0123189");
	ret = in_replace_chapter(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test146:normal");
	INFO_PRINT(0, "test146:out_buffer=[%s]", out_buffer);

	return 0;
}


int test147_money_exchange(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int eid;
	int gold;
	int crystal;

	eid = 547;
	gold = 100;
	crystal = -10;
	sprintf(in_buffer, "%d %d %d", eid, gold, crystal);
	ret = in_money_exchange(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test147:normal");
	INFO_PRINT(0, "test147:out_buffer=[%s]", out_buffer);

	return 0;
}

int test148_reset_arena_times(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;

	ret = in_reset_arena_times(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test148:normal");
	INFO_PRINT(0, "test148:out_buffer=[%s]", out_buffer);

	return 0;
}

int test149_list_gsearch(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	int gid;
	int flag;
	int start_id;
	int page_size;
	char search[101];


	// case 1 : normal list all (flag=0)(gid = 550)
	gid = 550; flag = 0; start_id = 0; page_size = 3;
	sprintf(search, "%s", "no");
	sprintf(in_buffer, IN_LIST_GSEARCH_PRINT, flag, start_id, page_size
	, search, gid);
	ret = in_guild_lsearch(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:all");
	printf("out_buffer : %s\n", out_buffer);

	gid = 550; flag = 0; start_id = 3; page_size = 5;
	sprintf(search, "%d", 5);
	sprintf(in_buffer, IN_LIST_GSEARCH_PRINT, flag, start_id, page_size
	, search, gid);
	ret = in_guild_lsearch(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:all");
	printf("out_buffer : %s\n", out_buffer);

	gid = 550; flag = 1; start_id = 0; page_size = 10;  // list member without apply
	sprintf(search, "%d", 5);
	sprintf(in_buffer, IN_LIST_GSEARCH_PRINT, flag, start_id, page_size
	, search, gid);
	ret = in_guild_lsearch(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:member_without_apply");
	printf("out_buffer : %s\n", out_buffer);

	gid = 550; flag = 9; start_id = 0; page_size = 10; // list apply only
	sprintf(search, "%s", "no");
	sprintf(in_buffer, IN_LIST_GSEARCH_PRINT, flag, start_id, page_size
	, search, gid);
	ret = in_guild_lsearch(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:apply");
	printf("out_buffer : %s\n", out_buffer);

	// case 2:  noguild
	gid = 556; flag = 0; start_id = 0; page_size = 10;
	sprintf(search, "%s", "no");
	sprintf(in_buffer, IN_LIST_GSEARCH_PRINT, flag, start_id, page_size
	, search, gid);
	printf("----------- following error is normal\n");
	ret = in_guild_lsearch(pconn, q_buffer, in_buffer, out_buffer);
	ERROR_RETURN(ret, "test_list_gmember:noguild");
	printf("out_buffer : %s\n", out_buffer);

	return 0;
}


int test_notyet(MYSQL **pconn, char *q_buffer, int argc, char *argv[])
{
	/*
	int ret;
	char in_buffer[DB_BUFFER_MAX+5];
	char *out_buffer = in_buffer;
	*/

	return -10;
}

///////////////////// GENERAL TESTCASE FUNCTION //////////////////

typedef int (*testcase_t) (MYSQL **, char*, int, char*[]); //  testcase_t;

// test_reconn
testcase_t test_list[] = {
	test0_register	// test_register
,	test1_login	// test_login
, 	test2_load_card	// test_load_card
,	test3_save_card	// test_save_card
,	test4_query_card_list
,	test5_load_deck // test_load_deck
,	test6_save_deck
,	test7_alias
,	test8_game
,	test9_win
,	test10_save_replay
,	test11_save_debug
,	test12_load_debug
,	test13_buy_card
,	test14_sell_card
,	test15_cccard
,	test16_load_batch
,	test17_save_batch
,	test18_update_money
,	test19_pick
,	test20_add_exchange
, 	test21_list_exchange
, 	test22_buy_exchange
,	test23_reset_exchange
,	test24_check_create_guild
,	test25_update_status_guild
,	test26_create_guild
,	test27_list_gmember
,	test28_list_guild
,	test29_guild_apply
,	test30_guild_member_change
,	test31_guild_approve
,	test32_guild_pos
,	test33_guild_quit
,	test34_guild_delete
,	test35_get_guild_gold
,	test36_guild_deposit
,	test37_insert_guild_share
,	test38_get_guild_share
,	test39_update_last_bonus
,	test40_guild_bonus
,	test41_update_last_login
,	test42_save_guild_deposit
,	test43_list_deposit
,	test44_create_ladder
,	test45_get_ladder
,	test46_list_replay
,	test47_load_replay
,	test48_update_profile
,	test49_friend_add
,	test50_friend_list
,	test51_friend_sta
,	test52_friend_search
,	test53_guild
,	test54_deposit
,	test55_glv
,	test56_glevelup
,	test57_save_piece
,	test58_load_piece
,	test59_update_piece
,	test60_merge_piece
,	test61_pick_piece
,	test62_check_friend
,	test63_add_money
,	test64_card_all_count
,	test65_sell_card
,	test66_get_course
,	test67_save_course
,	test68_challenge
,	test69_load_mission
,	test70_save_mission
,	test71_insert_first_slot
,	test72_load_slot
,	test73_save_slot
,	test74_rename_slot
,	test75_buy_slot
,	test76_mission_reward
,	test77_insert_login
,	test78_only_save_slot
,	test79_only_save_deck
,	test80_slot_list
,	test81_record_buy
,	test82_add_pay_log
,	test83_add_match
,	test84_add_match_player
,	test85_delete_match_player
,	test86_match_team_init
,	test87_update_match_player
,	test88_match_eli_init
,	test89_add_match_ai
,	test90_update_match
,	test91_friend_del
,	test92_init_ranking_list
,	test93_ranking_list
,	test94_ranking_targets
,	test95_check_ranking_target
,	test96_change_ranking_data
,	test97_get_ranking_history
,	test98_get_range_list
,	test99_save_ranking_challenge
,	test100_add_evil_message
,	test101_list_evil_message
,	test102_read_evil_message
,	test103_add_rank_reward_message
,	test104_get_unread_message_count
,	test105_rank_reward
,	test106_ranking_challenge
,	test107_check_login
,	test108_lottery
,	test109_exchange_gift
,	test110_gate
,	test111_power
,	test112_update_gate_pos
,	test113_tower
,	test114_tower_ladder
,	test115_tower_reward
,	test116_solo
,	test117_empty
,	test118_fight_robot
,	test119_update_signals
,	test120_chapter
,	test121_chapter_data
,	test122_chapter_update_data
,	test123_hero_mission
,	test124_mission_hero
,	test125_hero_mission
,	test126_update_hero_mission
,	test127_submit_hero_mission
,	test128_refresh_hero_mission
,	test129_load_hero_data
,	test130_reset_chapter_data
,	test131_hero_slot
,	test132_nscan
,	test133_daily_login
,	test134_get_daily_reward
,	test135_piece_shop
,	test136_admin_add_robot
,	test137_admin_init_arena
,	test138_arena_top_list
,	test139_get_arena_target_list
,	test140_list_arena_target
,	test141_exchange_arena_rank
,	test142_arena_game
,	test143_update_arena_times
,	test144_get_arena_reward
,	test145_get_chapter
,	test146_replace_chapter
,	test147_money_exchange
,	test148_reset_arena_times
,	test149_list_gsearch
};


int test_selector(MYSQL **pconn, char * q_buffer, int argc, char *argv[])
{
	int testmax = sizeof(test_list) / sizeof(test_list[0]);
	int testcase = testmax - 1 ; // TESTCASE_MAX-1;
	int ret;

	if (argc > 1) {
		testcase = atoi(argv[1]);
	}

	printf("RUN test%d:\n", testcase);
	if (testcase < 0 || testcase >= testmax) {
		printf("ERR invalid testcase %d\n", testcase);
		return -2;
	}
	ret = test_list[testcase](pconn, q_buffer, argc, argv);
	
	printf("RET %d\n", ret);
	if (ret != 0 ) {
		printf("XXXXXXXXX BUG ret!=0: %d\n", ret);
	}
	
	return ret;
}


int testcase_main(int argc, char *argv[])
{
	int ret;
	MYSQL * conn;
	char q_buffer[QUERY_MAX+1];
	// char in_buffer[DB_BUFFER_MAX + 1];
	// char out_buffer[DB_BUFFER_MAX + 1];

	// make init is called in : non-multi-thread environment
	ret = mysql_library_init(0, NULL, NULL); // argc, argv, char**group
	ret = mysql_thread_init(); // test in single thread environment
	FATAL_EXIT(ret, "main:mysql_library_init");

	conn = my_open();
	FATAL_EXIT(conn==NULL, "dbio:mysql_open:null");


//	ret = in_register(&conn, q_buffer, "special_user pass", out_buffer);
//	printf("ret = %d   out_buffer=%s\n", ret, out_buffer);

	if (argc > 1 && strcmp("all", argv[1])==0) {
		int testmax = sizeof(test_list) / sizeof(test_list[0]);
		int error_count = 0;
		for (int i=0; i<testmax; i++) {
			char str[10];
			char *alist[2] = {str, str};
			sprintf(str, "%d", i);
			ret = test_selector(&conn, q_buffer, argc, alist); // changed argv
			if (ret != 0) { error_count++; break;}
		}
		printf("TEST ALL SUMMARY: error_count=%d\n", error_count);
	} else {
		test_selector(&conn, q_buffer, argc, argv);
	}

//	test0(&conn, q_buffer, argc, argv);

	// mysql_library_end();
	mysql_thread_end();
	return 0;
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

// put it on static
dbio_init_t console_dbio;

#define CONSOLE_PATH 	"/tmp/evil_test.sock"

// console-based testing
int console_main(int argc, char *argv[])
{
	int ret;
	int master_fd;
	// main_fd, db_fd are pairs, duplex
	int main_fd;	// for main(this)
	int db_fd;		// for dbio() thread
	struct sockaddr_un unix_addr;
	pthread_t dbio_thread;
	socklen_t addr_len;


	ret = mysql_library_init(0, NULL, NULL); // argc, argv, char**group
	FATAL_EXIT(ret, "main:mysql_library_init");

	printf("----- console_main START\n");
	printf("Type in command [cid dbtype in_buffer]  [-9 0] to quit e.g.:\n");
	printf("0 0 anything   				  -  for testing\n");
	printf("0 1 new_user pass newalias    - for register a new_user\n");
	printf("0 2 www www                   - for login as www\n");
	printf("------------------------\n");

	if (DB_TRANS_MAX > 10) {
		WARN_PRINT(-1, "note: set evil.h : DB_TRANS_MAX to smaller for testing");
	}


	master_fd = dbio_init_server_socket(CONSOLE_PATH);

	main_fd = dbio_init_client_socket(CONSOLE_PATH);
	db_fd = accept(master_fd, (struct sockaddr*)&unix_addr, &addr_len);

	console_dbio.fd = db_fd;  // core logic

	ret = pthread_create(&dbio_thread, NULL, dbio, (void *)&console_dbio);
	FATAL_EXIT(ret, "pthread_create(dbio)");


	ret = set_nonblock(main_fd);

	fd_set fdset;  // the select set  must FD_ZERO every while start

	int buffer_index = 0;  // 0 to DB_TRANS_MAX-1
	int len;
	char buffer[DB_BUFFER_MAX+1];
	char *ptr; // temp pointer to db_buffer[buffer_index]
	unsigned char num[2];  // num buffer send via main_fd / db_fd
	int running = 1;
	while ( running ) {
		FD_ZERO(&fdset);
		FD_SET(1, &fdset);
		FD_SET(main_fd, &fdset);
		ret = select(main_fd+1, &fdset, NULL, NULL, 0);
		// printf("select ret %d\n", ret);
		if (ret < 0) { break; }
		if (FD_ISSET(1, &fdset)) {
			len = read(1, buffer, DB_BUFFER_MAX);
			if (len < 0) {
				printf("ERROR read %d  err %d\n", len, errno);
				continue;
			}
			buffer[len] = 0;
			if (len <= 1) continue;  // avoid enter only command
			printf("console_main:stdin=%s", buffer);
			if (buffer[0] == 'q') {
				// send 9999 (quit signal) to dbio() - this is optional
				short_to_char(num, 9999);  // @see dbio:quit_signal
				write(main_fd, num, 2);  // no need to check error
				close(main_fd); // core logic: close, and the other end dead
				break;
			}
			// TODO write to dbio() via main_fd

			ptr = console_dbio.db_buffer[buffer_index]; // make an alias
			// this is buffer overflow case
			if (ptr[0] != 0)  {
				ERROR_PRINT(-2, "db_buffer overflow\n");
				continue;
			}
			// implicit: [buffer_index][0] is null, good
			strcpy(ptr, buffer);
			num[0] = buffer_index & 0xff;  num[1] = (buffer_index >> 8) & 0xff;
			buffer_index++;  if (buffer_index >= DB_TRANS_MAX) buffer_index=0;
			ret = write(main_fd, num, 2); // hard code len=2 bytes
			FATAL_EXIT(2-ret, "write_num_main_fd");
		}

		// TODO check FD_ISSET(main_fd, &fdset) : 
		// if set, read 2 bytes

		if (FD_ISSET(main_fd, &fdset)) {
			if (2!=(ret=read(main_fd, num, 2))) {
				ERROR_PRINT(-5, "read_main_fd size!=2 %d", ret);
				break; // cannot help, fatal
			}
			buffer_index = char_to_short(num);
			if (buffer_index < 0 || buffer_index >= DB_TRANS_MAX) {
				ERROR_PRINT(-2, "console_main:buffer_index_outbound %d"
				, buffer_index);
				continue;  // cannot help, fatal
			}
			// ok, good, buffer
			ptr = console_dbio.db_buffer[buffer_index];
			// we will clear ptr[0] = 0 later, so this error is not fatal
			if (ptr[0] == 0) {
				ERROR_PRINT(-3, "console_main:receive_buffer_null %d"
				, buffer_index);
				continue;
			}
			printf("console_main:receive=%s\n", ptr);
			// this is where the main logic should handle 'ptr'
			ptr[0] = 0; // clean up, core logic!
		}
	}


	// select ( stdin, main_fd )
	
	// dbio((void*)&console_dbio);  // new way to init dbio()

	mysql_library_end();   
	return 0;
}


int main(int argc, char *argv[])
{
	if (argc > 1 && strcmp("con", argv[1])==0) {
		return console_main(argc, argv);
	} else {
		return testcase_main(argc, argv);
	}
}

#endif	// TTT

//////////////////////////////////////////////////////
//////////////////// TEST END ] //////////////////////
//////////////////////////////////////////////////////
