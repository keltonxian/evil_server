/**
 * db_conn.cpp
 * using MYSQL C API, base on  c99 standard (-std=c99 )
 * compile:
 * gcc -std=c99 -I /usr/local/include/mysql  db_conn.c libmysqlclient.a -lz
 *
 * ref: mysql-ref-51.pdf (mydoc/book)   p.2853
 *
 * function naming convention
 * query_*  = generate query string, no execution
 * db_* = database execution
 * 
 * ---------- testcase -----------
 * db [n] [args]
 * - run testcase [n] with arguments [args]
 * e.g. 
 * db 0 
 * - execute:  int test0(2, {"db", "0"})
 * db 5 20
 * - execute:  int test5(3, {"db", "5", "20"}
 * - eid=20 (default is eid=11)
 * 
 * db all
 * - run all testcase
 * NOTE: before run all testcase, use "my < db-init.sql" to initialize database
 * NOTE2: db all | less       search for XXX BUG
 *
 * @see TODO-server.txt for latest protocol
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <sys/time.h>

//  /usr/local/include/mysql
#include "mysql.h"

// evil related
#include "fatal.h"
#include "evil.h"


// gcc -I /usr/local/include/mysql -L. db_conn.c -lmysqlclient -lz


//////////  CONSTANT START //////////
// mysql -u[USER] -p[PASSWORD] -h[HOST] [DATABASE]
// note: -P3306  (use default, do not change!)
// #define MYSQL_HOST	"192.168.1.30"
#define MYSQL_HOST	"127.0.0.1"
#define MYSQL_PORT	3306
#define	MYSQL_USER	"evil"
#define MYSQL_PASSWORD	"1"
#define MYSQL_DATABASE	"evil_base"

// one card max 4 bytes ",NULL" + 100 overhead
#define QUERY_MAX (EVIL_CARD_MAX*5 + 100)

//////////  CONSTANT END //////////



//////////  GLOBAL START //////////

MYSQL * g_conn = NULL;
char g_query[QUERY_MAX+1];

//////////  GLOBAL END //////////


////////// UTILITY START /////////

// return the digit count for an input number [num]
// e.g. num=1  ret=1,    num=10 ret=2,  num=256 ret=3
// maximum is 5
// negative num is treated as math.abs(num)
int digit_count(int num)
{
	num = abs(num);
	int result = (int)floor(log10((double)num));
	// printf("result = %d\n", result);

	return result + 1;
}

//  we only need integer range!
// usage: 
// strtol_safe("123abc", -1) => 123
// strtol_safe("abc", -1) => -1
// strtok_safe("56.9", -1) => 56
static int strtol_safe(char *str, int default_value) {
    char *end;
    long value;
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

////////// UTILITY END /////////


/////// DB INIT START //////////

// access to g_conn
int db_init()
{
	int ret;
	printf("INFO db_init: mysql client version: %s\n", 
		mysql_get_client_info());
	// ret = mysql_library_init(0, NULL, NULL); // argc, argv, char**group
	ret = mysql_thread_init();
	FATAL_EXIT(ret, "db_init library_init");

	//  ----------------------
	g_conn = mysql_init(NULL);  // NULL ?
	FATAL_EXIT(g_conn==NULL, "db_init:mysql_init");


	// ref: setup utf8 connection
	// http://stackoverflow.com/questions/8112153/process-utf-8-data-from-mysql-in-c-and-give-result-back
	mysql_options(g_conn, MYSQL_SET_CHARSET_NAME, "utf8");   // was "utf8"
	// peter: SET NAMES is optional:
//    mysql_options(g_conn, MYSQL_INIT_COMMAND, "SET NAMES utf8");  // was "utf8"
	MYSQL * ret_conn;
	ret_conn = mysql_real_connect(g_conn, MYSQL_HOST, MYSQL_USER, 
		MYSQL_PASSWORD, MYSQL_DATABASE, MYSQL_PORT, NULL, 0);
	if (ret_conn == NULL) {
		ERROR_NEG_RETURN(-55, "db_init:mysql_real_connect %s", MYSQL_HOST);
	}
	// FATAL_EXIT(ret_conn==NULL, "db_init:mysql_real_connect");

	
	MY_CHARSET_INFO charset_info;
	mysql_get_character_set_info(g_conn, &charset_info);
	// pls check:  charset=utf8_general_ci  collate=utf8  num=33
	DEBUG_PRINT(0, "mysql charset=%s collate=%s num=%d"
	, charset_info.name, charset_info.csname, charset_info.number);
	return 0;
}

// access to g_conn
int db_clean()
{
	if (NULL==g_conn) {
		ERROR_PRINT(-3, "db_clean:g_conn=null");
		return -3;
	}
	mysql_close(g_conn);
	g_conn = NULL;
	//mysql_library_end();   
	mysql_thread_end();
	return 0;
}

int db_reconn()
{
	int myerr = mysql_errno(g_conn);
	WARN_PRINT(-5, "db_reconn errno %d", myerr);
	db_clean();
	int ret = db_init();
	return ret;
}


// safe means it can do auto-reconnect
int safe_mysql_real_query(const char *q, int len)
{
	int ret;
	int count = 0;
again:
	BUG_PRINT(-888, "still_using_db_conn: q=%s", q);
	ret = mysql_real_query(g_conn, q, len);
	if (ret == 0) {
		return 0;
	}
	// implicit: ret != 0 : means error
	int myerr = mysql_errno(g_conn);
	if (myerr==2013 || myerr==2006) {
		// sleep depends on count
		WARN_PRINT(-1, "db_conn:safe_mysql_real_query:retry %d", count);
		sleep(count);
		db_reconn();
		count++;
		if (count >= 3) {
			return ret;
		}
		goto again;
	}
	return ret;
}

int safe_mysql_query(const char *q)
{
	return safe_mysql_real_query(q, strlen(q));
}

////////// DB INIT END


////////// DB FUNCTION START /////////

#define QUERY_STATUS 	"SELECT * FROM evil_status WHERE eid=%d"
// input: user->eid 
// output: user->lv, rating, user->gold, crystal, gid, gpos
//         game_count, game_win, game_lose, game_draw, game_run
int db_load_status(evil_user_t* user)
{
	int ret;
	int len;
	int eid;

	eid = user->eid;
	if (eid <= 0) {
		ERROR_PRINT(-9, "not login");
		return -9;
	}
	len = sprintf(g_query, QUERY_STATUS, eid);
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		int err = mysql_errno(g_conn);
		ERROR_NEG_RETURN(-55, "db_load_status mysql_errno %d", err);
		return -55;
	}

	// retrieve the data
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_load_status null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "db_load_status empty row %d", num_row);
		goto cleanup;
	}

	// TODO check mysql_field_count
	field_count = mysql_field_count(g_conn);
	// row[0]=eid,  [1]=lv, [2]=rating, [3]=gold, [4]=crystal, 
	// [5]=gid, [6]=gpos
	// [7]=game_count, [8]=win, [9]=lose, [10]=draw, [11]=run  (TOTAL=12)
	if (field_count != 12 ) {
		ret = -7;
		ERROR_PRINT(ret, "db_load_status field_count %d != 10", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	// assert row[0] == eid
	ret = strtol_safe(row[0], -1);
	if (ret != eid) {  // this is really impossible
		ret = -17;
		ERROR_PRINT(eid-ret, "db_load_status eid %d != row[0] %d", eid, ret);
		goto cleanup;
	}
	
	// row[0]=eid,  [1]=lv, [2]=rating, [3]=gold, [4]=crystal, 
	// [5]=gid, [6]=gpos
	// [7]=game_count, [8]=win, [9]=lose, [10]=draw, [11]=run  (TOTAL=12)
	user->lv 		= strtol_safe(row[1], 0);
	// user->rating 	= strtol_safe(row[2], 0);  // TODO double
	user->rating = str_double_safe(row[2], 1000.99); // 1000 is init score
	user->gold 		= strtol_safe(row[3], 0);
	user->crystal 	= strtol_safe(row[4], 0);
	user->gid 	= strtol_safe(row[5], 0);
	user->gpos 	= strtol_safe(row[6], 0);
	//
	user->game_count= strtol_safe(row[7], 0);
	user->game_win  = strtol_safe(row[8], 0);
	user->game_lose = strtol_safe(row[9], 0);
	user->game_draw = strtol_safe(row[10], 0);
	user->game_run  = strtol_safe(row[11], 0);

	// ok, we are good return 0, using cleanup routing
	ret = 0;  

cleanup:
	mysql_free_result(result);
	return ret;
}

// eid,lv,rating
#define REPLACE_STATUS 	"REPLACE INTO evil_status VALUES (%d,%d,%lf,%d,%d,%d,%d,%d,%d,%d,%d,%d)"

int db_save_status(evil_user_t* user)
{
	int len;
	int err;
	int ret;

	if (user==NULL) {
		ERROR_NEG_RETURN(-3, "db_save_status null user");
		return -3;
	}
	if (user->eid <= 0) {
		ERROR_NEG_RETURN(-9, "db_save_status invalid eid %d", user->eid);
		return -9;
	}

	// global access: using g_query
	len = sprintf(g_query, REPLACE_STATUS, user->eid
	, user->lv, user->rating , user->gold, user->crystal
	, user->gid, user->gpos
	, user->game_count, user->game_win
	, user->game_lose, user->game_draw, user->game_run);

	DEBUG_PRINT(0, "db_save_status: [%s]", g_query);

	ret = safe_mysql_real_query( g_query, len);

	if (ret != 0) {
		err = mysql_errno(g_conn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "db_save_status mysql_errno %d", err);
		return -55;
	}

	// check affected row
	ret = mysql_affected_rows(g_conn); // replace can be 1 or 2
	DEBUG_PRINT(ret, "db_save_status affected_row");
	if (ret < 1 || ret > 2) {
		ERROR_NEG_RETURN(-6, "affected_row wrong %d\n", ret);
	}
	return 0 ;
}

// construct the query for insert/replace to evil_card/evil_deck
// @param query is the output param
// @param card card[0] = c1,  card[1]=c2, ..., card[399]=c400etc
// @param card_count  number of card (card[card_count-1] is valid!)
// @param prefix = "REPLACE INTO evil_card ", or INSERT, or evil_deck
// return length of query
int query_card_list(const char* prefix, char* query, int query_max, 
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
	if (query_max < 50) { 
		return -12;
	}
	assert(card_count >= 0);
	assert(card!=NULL);

	char * const query_end_max = query + query_max - 50;


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
		if (card[i]==0) {
			// end = stpcpy(end, ",NULL"); // null may save space!
			end = stpcpy(end, ",0"); // make it 0 first
		} else {
			sprintf(numstr, ",%d", (int) card[i]);
			end = stpcpy(end, numstr);
		}
		if (end > query_end_max) {
			DEBUG_PRINT(i, "query overflow max %d", query_max);
			return -22;
		}
	}
	// close )
	end = stpcpy(end, ")");

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}

// query=output param, query_max is the max len of query (must >=50)
// or use maxlen = *(int *)query
// 
int query_insert_card(char * query, int query_max, int eid, 
const char *card, int card_count)
{
	static const char * prefix = "INSERT INTO evil_card ";
	return query_card_list(prefix, query, query_max, eid, card, card_count);
}

int query_replace_card(char * query, int query_max, int eid, 
const char *card, int card_count)
{
	static const char * prefix = "REPLACE INTO evil_card ";
	return query_card_list(prefix, query, query_max, eid, card, card_count);
}



/**
 * use g_conn (must have init'ed!)
 * use g_query (non-thread-safe!)
 *
 * XXX note: consider to use INSERT instead of REPLACE, since
 * db_save_card only do once!
 */
int db_save_card(int eid, const char *card) // card_count
{
	int len;
	int err;
	int ret;

	assert(g_conn != NULL);
	// peter: save is only once!  use insert
	len = query_insert_card(g_query, QUERY_MAX, eid, card, EVIL_CARD_MAX);
	ERROR_NEG_RETURN(len, "db_save_card query");
	
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		err = mysql_errno(g_conn);
		// DEBUG_PRINT(ret, "db_save_card mysql_errno %d", err);
		if (err==1062) {
			return -22;  // duplicated (-2), already choose job
		}
		ERROR_NEG_RETURN(-55, "db_save_card mysql_errno %d", err);
		return -55;
	}


	// check affected row
	ret = mysql_affected_rows(g_conn); // replace can be 1 or 2
	DEBUG_PRINT(ret, "db_save_card affected_row");
	if (ret < 1 || ret > 2) {
		ERROR_NEG_RETURN(-6, "affected_row wrong %d\n", ret);
	}

	return 0;
}


#define QUERY_SELECT_CARD 	"SELECT * FROM evil_card WHERE eid=%d"
/**
 *
 * input:  char *card,   [0] to [399] means number of cards for card1 to card400
 * e.g. card[0] = 3  means we have 3 x card1
 *      card[21] = 2  means we have 2 x card22
 * return 
 */
int db_load_card(int eid, char *card) // card_count==EVIL_CARD_MAX
{
	int ret;
	int len;
	
	len = sprintf(g_query, QUERY_SELECT_CARD, eid);

	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_load_card safe_mysql_real_query"); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "db_load_card query: %s", g_query);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_load_card null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "db_load_card empty row=%d eid=%d", num_row, eid);
		goto cleanup;
	}
	// TODO check mysql_field_count

	field_count = mysql_field_count(g_conn);
	// card_count + 1 (eid)
	if (field_count != EVIL_CARD_MAX + 1) {
		ret = -7;
		ERROR_PRINT(ret, "db_load_card field_count %d != card_count+1 %d",
			field_count, EVIL_CARD_MAX+1);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	// assert row[0] == eid
	ret = strtol_safe(row[0], -1);
	ERROR_PRINT(eid-ret, "eid %d != row[0] %d", eid, ret);
	
	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+1];
		if (NULL==data) {
			card[i] = 0;
		} else {
			ret = strtol_safe(data, -1);
			if (ret < 0) {
				ERROR_PRINT(ret, "db_load_card strtol_safe %s", data);
				goto cleanup;
			}
			card[i] = (char)ret;
		}
	}

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}


//////// deck start //////////
/**
 * @param query 	output param
 * @return >=0  length of query
 */
int replace_deck(char *query, int query_max, int eid, 
const char * card, int card_count) {
	static const char * prefix = "REPLACE INTO evil_deck ";
	return query_card_list(prefix, query, query_max, eid, card, card_count);
}

/**
 * use g_conn (must have init'ed!)
 * use g_query (non-thread-safe!)
 *
 */
int db_save_deck(int eid, const char *card)
{
	int len;
	int ret;

	assert(g_conn != NULL);
	len = replace_deck(g_query, QUERY_MAX, eid, card, EVIL_CARD_MAX);
	ERROR_NEG_RETURN(len, "db_save_deck query");
	
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		int err = mysql_errno(g_conn);
		ERROR_RETURN(-55, "db_save_deck mysql_errno %d", err);
		return -55;
	}

	// check affected row
	ret = mysql_affected_rows(g_conn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		ERROR_NEG_RETURN(-6, "affected_row wrong %d\n", ret);
	}

	return 0;
}


#define QUERY_SELECT_DECK 	"SELECT * FROM evil_deck WHERE eid=%d"
/**
 *
 * @param:  char *card,   [0] to [399] means number of cards for card1 to card400
 * e.g. card[0] = 3  means we have 3 x card1
 *      card[21] = 2  means we have 2 x card22
 *
 */
int db_load_deck(int eid, char *card)
{
	int ret;
	int len;
	
	len = sprintf(g_query, QUERY_SELECT_DECK, eid);

	ret = safe_mysql_real_query( g_query, len);
	ERROR_RETURN(ret, "db_load_deck safe_mysql_real_query");
	// implicit: ret==0 OK

	// DEBUG_PRINT(0, "db_load_deck query: %s", g_query);

	
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_load_deck null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "db_load_deck empty row %d", num_row);
		goto cleanup;
	}
	// TODO check mysql_field_count

	field_count = mysql_field_count(g_conn);
	// card_count + 1 (eid)
	if (field_count != EVIL_CARD_MAX + 1) {
		ret = -7;
		ERROR_PRINT(ret, "db_load_deck field_count %d != card_count+1 %d",
			field_count, EVIL_CARD_MAX+1);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	// assert row[0] == eid
	ret = strtol_safe(row[0], -1);
	ERROR_PRINT(eid-ret, "eid %d != row[0] %d", eid, ret);
	
	// card[] is zero-based
	for (int i=0; i<EVIL_CARD_MAX; i++) {
		char * data = row[i+1];
		char * c = card + i;
		if (NULL==data) {
			*c = 0;
			continue;
		}

		ret = strtol_safe(data, -1);
		if (ret < 0) {
			ERROR_PRINT(ret, "db_load_deck strtol_safe %s", data);
			goto cleanup;
		}
		*c = (char)ret;
	}

	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	if (result != NULL) mysql_free_result(result);
	return ret;
}

#define QUERY_CCCARD	"UPDATE evil_card SET c%d=%d WHERE eid=%d"
int db_cccard(int eid, int cid, int cnum) {
	int ret;
	int len;

	if (cnum < 0 || cnum >9) {
		ERROR_RETURN(-2, "db_cccard:cnum out of bound %d", cnum);
	}

	len = sprintf(g_query, QUERY_CCCARD, cid, cnum, eid);
	printf("g_query = %s\n", g_query);
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_cccard:safe_mysql_real_query");
		return -55;
	}

	ret = mysql_affected_rows(g_conn); // must be 1 for success
	if (ret == 0) {
		ERROR_NEG_RETURN(-16, "db_cccard:update_error");
	}

	// shall we check ret < 0 ?
	if (ret != 1) {
		ERROR_NEG_RETURN(-17, "db_cccard:update_impossible");
	}

	return ret;

}



/**
 * new user save to 
 * @param user	output param, pointer should be allocated by caller
 * @param username  must be unique relative to existing evil_user.username
 * 				    username_valid() == 0
 * @param password  password_valid() == 0
 * @return 0 for ok, which is the eid is stored in evil_user_t * user
 * 
 * this may have 2 queries, require non-multi-thread execution to
 * guarantee correctness (alias check)
 */
int db_register_user(evil_user_t * user, const char *username, 
const char *password, const char *in_alias) {
	int ret;
	if (user==NULL || username==NULL || password==NULL || in_alias==NULL) {
		ret = -33;
		BUG_PRINT(ret, "db_register user|username|password|alias null");
		return ret;
	}
	// escape use more characters, assume double!
	int err;
	int len;
	int len_username = strlen(username);
	int len_password = strlen(password);
	int len_alias;
	char esc_username[EVIL_USERNAME_MAX * 2 + 1];
	char esc_password[EVIL_PASSWORD_MAX * 2 + 1];
	char alias[EVIL_ALIAS_MAX + 1]; 
	char esc_alias[EVIL_ALIAS_MAX * 2 + 1]; 
	// TODO alias ? set_alias
	// const char *qq = "INSERT INTO evil_user (username,password,alias,last_login) " " VALUES ('%s','%s','%s',NOW())";
	const char *qq = "INSERT INTO evil_user VALUES (NULL, '%s', '%s', '%s', NOW())";

	// _ means visitor
	if (in_alias == NULL || in_alias[0]=='\0') {
		sprintf(alias, "_%.29s", username);  // _username means visitor
	} else {
		sprintf(alias, "%.30s", in_alias);
	}
	len_alias = strlen(alias);  // if 0, means not yet setup alias

	len = mysql_real_escape_string(g_conn, esc_username, username, 
		len_username);
	ERROR_NEG_RETURN(len, "esc_username");
	len = mysql_real_escape_string(g_conn, esc_password, password, 
		len_password);
	ERROR_NEG_RETURN(len, "esc_password");
	len = mysql_real_escape_string(g_conn, esc_alias, alias, 
		len_alias);
	ERROR_NEG_RETURN(len, "esc_alias");
	printf("esc_alias=%s  in_alias=%s  alias=%s\n", esc_alias, in_alias, alias);

	len = sprintf(g_query, qq, esc_username, esc_password, esc_alias);
	ret = safe_mysql_real_query( g_query, len);
	// this return is usually duplicated username
	if (ret!=0) {
		err = mysql_errno(g_conn);
		// -6 is username duplicate error
		if (err==1062) {  // duplicate entry
			return -6;  // username and/or alias duplicate
		} else {
			ERROR_PRINT(-55, "register_user esc_username=%s esc_password=%s esc_alias=%s query=%s", esc_username, esc_password, esc_alias, g_query);
			ERROR_RETURN(-55, "register_user mysql_errno %d", err);
			return -55;
		}
	}

	ret = (int)mysql_insert_id(g_conn);
	if (ret >= 0) {
		user->eid = ret;
		strcpy(user->username, username);
		strcpy(user->password, password);
		strcpy(user->alias, alias);
		// TODO last_login
	} 
	
	return ret;
}


// TODO check no _
int db_alias(int eid, const char *alias) {
	int ret;
	if (alias==NULL) {
		ret = -33;
		BUG_PRINT(ret, "db_alias alias null");
		return ret;
	}
	// escape use more characters, assume double!
	int err;
	int len;
	int len_alias = strlen(alias);  
	char esc_alias[EVIL_ALIAS_MAX * 2 + 1]; 
	// TODO alias ? set_alias

	if (eid <= 0) {
		return -19;  // eid invalid is access denied
	}
	
	// only allow alias update, if old alias = _xxx   e.g. _$(username)
	// @see db_register_user
	const char *qq 
	= "UPDATE evil_user SET alias='%s' WHERE eid=%d AND alias LIKE '\\_%%'" ;

	len = mysql_real_escape_string(g_conn, esc_alias, alias, 
		len_alias);
	ERROR_NEG_RETURN(len, "esc_alias");

	len = sprintf(g_query, qq, esc_alias, eid);
	printf("db_alias : query = %s\n", g_query);
	ret = safe_mysql_real_query( g_query, len);
	// this return is usually duplicated username
	if (ret!=0) {
		err = mysql_errno(g_conn);
		// -6 is username duplicate error
		if (err==1062) {  // duplicate entry
			return -26;  // possible when alias is duplicated
		} else {
			ERROR_RETURN(-55, "alias_mysql_errno %d  query=%s", err, g_query);
			return -55;
		}
	}

	// note: if the alias is already set, this will be zero (0)
	ret = mysql_affected_rows(g_conn); // must be 1 for success
	if (ret == 0) {
		ERROR_NEG_RETURN(-16, "alias_already_set");
	}

	// shall we check ret < 0 ?
	if (ret != 1) {
		ERROR_NEG_RETURN(-17, "alias_impossible");
	}

	// finally  (assume ret == 1)
	return 0;
}


/*
 * @param user	output param, pointer should be allocated by caller
 * @param username  must be unique relative to existing evil_user.username
 * 				    username_valid() == 0
 * @param password  password_valid() == 0
 *
 * logic:  if success, fill up evil_user_t * user
 * eid, username, password, alias, last_login(later)
 */
int db_login(evil_user_t * user, char *username, char *password) {
	int ret;
	int len;
	int len_username = strlen(username);
	int len_password = strlen(password);
	char esc_username[EVIL_USERNAME_MAX * 2 + 1];
	char esc_password[EVIL_PASSWORD_MAX * 2 + 1];

	assert(g_conn!=NULL);

	// this is impossible, since caller should have evil_user_t alloc'ed
	if (user==NULL || username==NULL || password==NULL) {
		ret = -33;
		BUG_PRINT(ret, "db_login evil_user_t|username|password null");
		return ret;
	}
	
	len = mysql_real_escape_string(g_conn, esc_username, username, 
		len_username);
	ERROR_NEG_RETURN(len, "esc_username");

	len = mysql_real_escape_string(g_conn, esc_password, password, 
		len_password);
	ERROR_NEG_RETURN(len, "esc_password");

	len = sprintf(g_query, "SELECT eid, password, alias, last_login FROM evil_user WHERE username='%s' LIMIT 5", esc_username);
	// printf("DEBUG login query: [%s]\n", g_query);
	ret = safe_mysql_real_query( g_query, len);
	if (0 != ret) {
		ERROR_RETURN(-55, "db_login mysql_errno %d", mysql_errno(g_conn));
	}

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(g_conn);

	if (result == NULL) {
		printf("---- error result\n");
		ERROR_PRINT(-3, "db_login null result");
		return -3; // early exit
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;	// normal user not found
		DEBUG_PRINT(ret, "db_login user not found");
		goto cleanup;
	}
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(ret, "db_login null row");
		goto cleanup; // cleanup and early exit
	}

	// implicit: row != NULL
	// row[0], row[1], row[2], row[3] = eid, password, alias, last_login
	if (0 != strcmp(password, row[1])) {
		ret = -16; // normal password mismatch
		DEBUG_PRINT(ret, "db_login password mismatch user %s", username);
		goto cleanup;
	}
	
	ret = strtol_safe(row[0], -7);
	if (ret<0) {
		BUG_PRINT(ret, "db_login strtol_safe %s", row[0]);
		goto cleanup;
	}

	// OK finally we are done, setup user structure
	user->eid = ret;
	if (NULL!=row[2]) {
		// this is normal
		strcpy(user->alias, row[2]);
	} else {
		strcpy(user->alias, "");
	}
	strcpy(user->username, username);
	strcpy(user->password, password);
	// TODO later: user->last_login = strtodatetime(row[3]);

cleanup:
	mysql_free_result(result);
	return ret;
}


const char * UPDATE_WIN = "UPDATE evil_status SET game_count=game_count+1, game_win=game_win+1,rating=rating+%lf WHERE eid = %d";

const char * UPDATE_LOSE = "UPDATE evil_status SET game_count=game_count+1, game_lose=game_lose+1,rating=rating-%lf WHERE eid = %d";

// assume: elo_diff must be positive
int db_win(int win_eid, int lose_eid, double elo_diff)
{
	int ret;
	assert(g_conn!=NULL);

	if (elo_diff < 0) {
		BUG_PRINT(-7, "db_win_neg_elo_diff");
	}

	int len;
	// 1. do winner first
	if (win_eid > 20) {
		len = sprintf(g_query, UPDATE_WIN, win_eid, elo_diff);
		DEBUG_PRINT(0, "WIN SQL: %s", g_query);
		ret = safe_mysql_real_query( g_query, len);
		if (0 != ret) {
			ERROR_RETURN(-55, "db_win_winner mysql_errno %d"
			, mysql_errno(g_conn));
		}

		ret = mysql_affected_rows(g_conn); // replace can be 1 or 2
		// this is logical error
		if (1 != ret) {
			ERROR_RETURN(-56, "db_win_affected_row %d mysql_errno %d"
			, ret, mysql_errno(g_conn));
		}
	}

	// 2. do loser
	if (lose_eid > 20) {
		len = sprintf(g_query, UPDATE_LOSE, lose_eid, elo_diff);
		DEBUG_PRINT(0, "LOSE SQL: %s", g_query);
		ret = safe_mysql_real_query( g_query, len);
		if (0 != ret) {
			ERROR_RETURN(-65, "db_win_loser mysql_errno %d"
			, mysql_errno(g_conn));
		}

		ret = mysql_affected_rows(g_conn); // replace can be 1 or 2
		// this is logical error
		if (1 != ret) {
			ERROR_RETURN(-66, "db_win_lose_affected_row %d mysql_errno %d"
			, ret, mysql_errno(g_conn));
		}
	}

	return 0;
}


const char * UPDATE_DRAW = "UPDATE evil_status SET game_count=game_count+1, game_draw=game_draw+1 WHERE eid = %d";

int db_draw(int eid0, int eid1)
{
	int ret;
	assert(g_conn!=NULL);

	int len;
	if (eid0 > 20) { 
		len = sprintf(g_query, UPDATE_DRAW, eid0);
		DEBUG_PRINT(0, "DRAW SQL: %s", g_query);
		ret = safe_mysql_real_query( g_query, len);
		if (0 != ret) {
			ERROR_RETURN(-55, "db_draw0 mysql_errno %d"
			, mysql_errno(g_conn));
		}

		ret = mysql_affected_rows(g_conn); // replace can be 1 or 2
		// this is logical error
		if (1 != ret) {
			ERROR_RETURN(-56, "db_draw0_affected_row %d mysql_errno %d"
			, ret, mysql_errno(g_conn));
		}
	}

	if (eid1 > 20) { 
		len = sprintf(g_query, UPDATE_DRAW, eid1);
		DEBUG_PRINT(0, "DRAW SQL: %s", g_query);
		ret = safe_mysql_real_query( g_query, len);
		if (0 != ret) {
			ERROR_RETURN(-65, "db_draw1 mysql_errno %d"
			, mysql_errno(g_conn));
		}

		ret = mysql_affected_rows(g_conn); // replace can be 1 or 2
		// this is logical error
		if (1 != ret) {
			ERROR_RETURN(-66, "db_draw1_affected_row %d mysql_errno %d"
			, ret, mysql_errno(g_conn));
		}
	}

	return 0;
}


const char * INSERT_REPLAY = 
"INSERT INTO evil_replay VALUES (%ld, %d, %d, %d, %d, %d, '%s', '%s', '%s')";

// gameid must be unique
int db_save_replay(long gameid, int win, int seed, int ver, int eid1, int eid2
, char *deck1, char *deck2, char *cmd)
{
	int len_deck1 = strlen(deck1);
	int len_deck2 = strlen(deck2);
	int len_cmd = strlen(cmd);

	char esc_deck1[len_deck1 * 2 + 1];
	char esc_deck2[len_deck2 * 2 + 1];
	char esc_cmd[len_cmd * 2 + 1];
	int len;
	int ret;

	len = mysql_real_escape_string(g_conn, esc_deck1, deck1, len_deck1);
	ERROR_NEG_RETURN(len, "replay_esc_deck1");
	len = mysql_real_escape_string(g_conn, esc_deck2, deck2, len_deck2);
	ERROR_NEG_RETURN(len, "replay_esc_deck2");
	len = mysql_real_escape_string(g_conn, esc_cmd, cmd, len_cmd);
	ERROR_NEG_RETURN(len, "replay_esc_cmd");

	len = sprintf(g_query, INSERT_REPLAY, gameid, win, seed, ver, eid1, eid2
	, esc_deck1, esc_deck2, esc_cmd);

	DEBUG_PRINT(0, "INSERT_REPLAY:(%d) [%s]\n", len, g_query);

	ret = safe_mysql_real_query( g_query, len);

	if (ret != 0) {
		int err = mysql_errno(g_conn);
		// no 1062 because we are using REPLACE
		ERROR_NEG_RETURN(-55, "db_save_replay mysql_errno %d", err);
		return -55;
	}

	// check affected row
	ret = mysql_affected_rows(g_conn); // replace can be 1 or 2
	DEBUG_PRINT(ret, "db_save_replay_affected_row");
	if (ret != 1) {
		ERROR_NEG_RETURN(-6, "replay_affected_row %d %ld\n", ret, gameid);
	}
	return 0;
}

int db_load_replay(const char *alias1, const char *alias2, game_t * pgame)
{
	int ret;
	int len;
	// input escape 
	int len_alias1 = strlen(alias1);
	int len_alias2 = strlen(alias2);
	char esc_alias1[EVIL_ALIAS_MAX * 2 + 1];
	char esc_alias2[EVIL_ALIAS_MAX * 2 + 1];
	// output: pgame  (by pass-in param)

	len = mysql_real_escape_string(g_conn, esc_alias1, alias1, len_alias1);
	ERROR_NEG_RETURN(len, "db_load_replay_esc_alias1");
	len = mysql_real_escape_string(g_conn, esc_alias2, alias2, len_alias2);
	ERROR_NEG_RETURN(len, "db_load_replay_esc_alias2");
	
//	SELECT eid1, eid2, seed FROM evil_replay LEFT JOIN evil_user AS user1 ON evil_replay.eid1=user1.eid LEFT JOIN evil_user AS user2 ON evil_replay.eid2=user2.eid WHERE (user1.alias = 'x' AND user2.alias='y') OR (user1.alias = 'y' AND user2.alias='x');

	len = sprintf(g_query, "SELECT gameid, win, seed, ver, eid1, eid2, deck1, deck2, cmd FROM evil_replay LEFT JOIN evil_user AS user1 ON evil_replay.eid1=user1.eid LEFT JOIN evil_user AS user2 ON evil_replay.eid2=user2.eid WHERE (user1.alias = '%s' AND user2.alias='%s') OR (user1.alias = '%s' AND user2.alias='%s') ORDER BY gameid DESC LIMIT 1"
		, esc_alias1, esc_alias2, esc_alias2, esc_alias1);
	
	ERROR_NEG_RETURN(len, "db_load_replay_g_query");

	DEBUG_PRINT(0, "%s\n", g_query);


	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_load_replay safe_mysql_real_query %d", ret); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "db_load_replay query: %s", g_query);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_load_replay null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "db_load_replay empty row %d", num_row);
		goto cleanup;
	}
	// TODO check mysql_field_count

	field_count = mysql_field_count(g_conn);
	// gameid, win, seed, ver, eid1, eid2, deck1, deck2, cmd 
	if (field_count != 9) { 
		ret = -7;
		ERROR_PRINT(ret, "db_load_replay field_count %d != 9", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);  // only the first row is needed!

	// [0]=gameid, [1]=win, [2]=seed, [3]=ver, [4]=eid1, [5]=eid2, 
	// [6]=deck1, [7]=deck2, [9]=cmd 
	// printf("--- gameid row[0] = [%s]\n", row[0]);
	pgame->gameid = strtolong_safe(row[0], -1);
	if (pgame->gameid < 0) {
		ERROR_NEG_PRINT(-55, "db_load_replay_negative_gameid %ld"
		, pgame->gameid);
	}

	pgame->win = strtol_safe(row[1], -65);
	ERROR_NEG_PRINT(pgame->win, "db_load_replay_negative_win");

	pgame->seed = strtol_safe(row[2], -75);
	ERROR_NEG_PRINT(pgame->seed, "db_load_replay_negative_seed");

	pgame->ver = strtol_safe(row[3], -85);
	ERROR_NEG_PRINT(pgame->ver, "db_load_replay_negative_ver");

	pgame->eid1 = strtol_safe(row[4], -95);
	ERROR_NEG_PRINT(pgame->ver, "db_load_replay_negative_eid1");
	pgame->eid2 = strtol_safe(row[5], -105);
	ERROR_NEG_PRINT(pgame->ver, "db_load_replay_negative_eid2");

	strcpy(pgame->deck[0], row[6]);
	strcpy(pgame->deck[1], row[7]);
	strcpy(pgame->cmd, row[8]);

	
	// ok, we are good, set ret = 0
	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;
}


const char * LOAD_SHOP = 
// "SELECT * FROM evil_shop WHERE cid >= %d AND cid < %d";
"SELECT * FROM evil_shop WHERE cid >= %d LIMIT %d";

int db_load_shop(int cid, int size, char *info)
{
	int ret;
	int len;

	int ccc;
	int gold_buy;
	int gold_sell;
	int crystal_buy;
	int crystal_sell;

	len = sprintf(g_query, LOAD_SHOP, cid, size);

	ERROR_NEG_RETURN(len, "db_load_shop_g_query");

	DEBUG_PRINT(0, "%s\n", g_query);

	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_load_shop safe_mysql_real_query %d", ret); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "db_load_shop query: %s", g_query);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_load_shop null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "db_load_shop empty row %d", num_row);
		goto cleanup;
	}

	// DEBUG_PRINT(0, "db_load_shop:num_row=%d", num_row);
	ret = sprintf(info, "%d ", num_row);

	field_count = mysql_field_count(g_conn);
	// cid, gold_buy, gold_sell, crystal_buy, crystal_sell
	if (field_count != 5) { 
		ret = -7;
		ERROR_PRINT(ret, "db_load_shop field_count %d != 9", field_count);
		goto cleanup;
	}

	char tbuff[100];
	for (int i=0;i<num_row;i++) {
	// while(NULL != row) {
	row = mysql_fetch_row(result);  

	ccc = strtol_safe(row[0], -5);
	ERROR_NEG_PRINT(ccc, "db_load_shop negative_ccc");
	gold_buy = strtol_safe(row[1], -5);
	ERROR_NEG_PRINT(gold_buy, "db_load_shop negative_gb");
	gold_sell = strtol_safe(row[2], -5);
	ERROR_NEG_PRINT(gold_sell, "db_load_shop negative_gs");
	crystal_buy = strtol_safe(row[3], -5);
	ERROR_NEG_PRINT(crystal_buy, "db_load_shop negative_cb");
	crystal_sell = strtol_safe(row[4], -5);
	ERROR_NEG_PRINT(crystal_sell, "db_load_shop negative_cs");

	// DEBUG_PRINT(0, "db_load_shop:ccc=%d, gold_buy=%d, gold_sell=%d, crystal=%d, crystal=%d", ccc, gold_buy, gold_sell, crystal_buy, crystal_sell);

	ret = sprintf(tbuff, "%d %d %d %d %d ", ccc, gold_buy, gold_sell, crystal_buy, crystal_sell);
	strcat(info, tbuff);

	}

	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;

}


const char * GET_CARD_COUNT = 
"SELECT eid, c%d FROM evil_card WHERE eid=%d";

const char * GET_EUSER_MONEY = 
"SELECT eid, gold, crystal FROM evil_status WHERE eid=%d";

const char * GET_CARD_PRICE = 
"SELECT * FROM evil_shop WHERE cid=%d";

const char * UPDATE_MONEY =
"UPDATE evil_status SET gold=%d, crystal=%d WHERE eid=%d";

const char * UPDATE_CARD_COUNT =
"UPDATE evil_card SET c%d=%d WHERE eid=%d";

int db_get_card_count(int eid, int cid, int * cc)
{
	int ret;
	int len;
	int card_count;
	char * data;

	len = sprintf(g_query, GET_CARD_COUNT, cid, eid);

	ERROR_NEG_RETURN(len, "db_get_card_count:g_query");

	DEBUG_PRINT(0, "%s\n", g_query);

	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_get_card_count:safe_mysql_real_query %d", ret); 
		return -55; // safety, should never run
	}

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_get_card_count null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "db_get_card_count:empty row %d", num_row);
		goto cleanup;
	}

	DEBUG_PRINT(0, "num_row=%d", num_row);

	field_count = mysql_field_count(g_conn);
	// eid, card_count
	if (field_count != 2) { 
		ret = -7;
		ERROR_PRINT(ret, "db_get_card_count field_count %d != 9", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);

	int eee;
	// if eee != eid....
	eee = strtol_safe(row[0], -5);
	ERROR_NEG_PRINT(eee, "db_get_card_count:negative_eee");

	
	data = row[1];
	if (NULL == data) {
		DEBUG_PRINT(0, "db_get_card_count:data==NULL");
		card_count = 0;
	} else {
		card_count = strtol_safe(row[1], -5);
	}	
	ERROR_NEG_PRINT(card_count, "db_get_card_count:negative_card_count");

	*cc = card_count;

	DEBUG_PRINT(0, "db_get_card_count:eee=%d, card_count=%d", eee, card_count);
	
	ret = 0;


cleanup:
	mysql_free_result(result);
	return ret;
}

int db_get_euser_money(int eid, evil_user_t * euser)
{
	int ret;
	int len;

	len = sprintf(g_query, GET_EUSER_MONEY, eid);

	ERROR_NEG_RETURN(len, "db_get_euser_money:g_query");

	DEBUG_PRINT(0, "%s\n", g_query);

	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_get_euser_money:safe_mysql_real_query %d", ret); 
		return -55; // safety, should never run
	}

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_get_euser_money:null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "db_get_euser_money:empty row %d", num_row);
		goto cleanup;
	}

	DEBUG_PRINT(0, "num_row=%d", num_row);

	field_count = mysql_field_count(g_conn);
	// eid, gold, crystal
	if (field_count != 3) { 
		ret = -7;
		ERROR_PRINT(ret, "db_get_euser_money:field_count %d != 9", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);

	int eee;
	int ggg;
	int ccc;
	// if eee != eid....
	eee = strtol_safe(row[0], -5);
	ERROR_NEG_PRINT(eee, "db_get_euser_money:negative_eee");

	ggg = strtol_safe(row[1], -5);
	ERROR_NEG_PRINT(ggg, "db_get_euser_money:negative_gold");

	ccc = strtol_safe(row[2], -5);
	ERROR_NEG_PRINT(ccc, "db_get_euser_money:negative_crystal");


	DEBUG_PRINT(0, "db_get_euser_money:eee=%d, gold=%d, crystal=%d", eee, ggg, ccc);
	
	euser->gold = ggg;
	euser->crystal = ccc;
	ret = 0;


cleanup:
	mysql_free_result(result);
	return ret;
}


int db_get_card_price(int cid, int * gold_buy, int *gold_sell, int *crystal_buy, int *crystal_sell)
{
	int ret;
	int len;

	len = sprintf(g_query, GET_CARD_PRICE, cid);

	ERROR_NEG_RETURN(len, "db_get_card_price:g_query");

	DEBUG_PRINT(0, "%s\n", g_query);

	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_get_card_price:safe_mysql_real_query %d", ret); 
		return -55; // safety, should never run
	}

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_get_card_price:null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "db_get_card_price:empty row %d", num_row);
		goto cleanup;
	}

	DEBUG_PRINT(0, "num_row=%d", num_row);

	field_count = mysql_field_count(g_conn);
	// cid, gold_buy, gold_sell, crystal_buy, crystal_sell
	if (field_count != 5) { 
		ret = -7;
		ERROR_PRINT(ret, "db_get_card_price:field_count %d != 9", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);

	int ccc;
	int g_buy;
	int g_sell;
	int c_buy;
	int c_sell;
	ccc = strtol_safe(row[0], -5);
	ERROR_NEG_PRINT(ccc, "db_get_card_price:negative_ccc");

	g_buy = strtol_safe(row[1], -5);
	ERROR_NEG_PRINT(g_buy, "db_get_card_price:negative_gold_buy");

	g_sell = strtol_safe(row[2], -5);
	ERROR_NEG_PRINT(g_sell, "db_get_card_price:negative_gold_sell");

	c_buy = strtol_safe(row[3], -5);
	ERROR_NEG_PRINT(c_buy, "db_get_card_price:negative_crystal_buy");

	c_sell = strtol_safe(row[4], -5);
	ERROR_NEG_PRINT(c_buy, "db_get_card_price:negative_crystal_sell");

	DEBUG_PRINT(0, "db_get_card_price:ccc=%d, gold_buy=%d, gold_sell=%d, crystal_buy=%d, crystal_sell=%d", ccc, g_buy, g_sell, c_buy, c_sell);

	*gold_buy = g_buy;
	*gold_sell = g_sell;
	*crystal_buy = c_buy;
	*crystal_sell = c_sell;
	
	ret = 0;


cleanup:
	mysql_free_result(result);
	return ret;
}

int db_update_euser_money(int eid, int gold, int crystal) {
	int ret;
	int len;


	len = sprintf(g_query, UPDATE_MONEY, gold, crystal, eid);
	printf("g_query = %s\n", g_query);
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_update_euser_money:safe_mysql_real_query");
		return -55;
	}

	ret = mysql_affected_rows(g_conn); // must be 1 for success
	if (ret == 0) {
		ERROR_NEG_RETURN(-16, "db_update_euser_money:update_error");
	}

	// shall we check ret < 0 ?
	if (ret != 1) {
		ERROR_NEG_RETURN(-17, "db_update_euser_money:update_impossible");
	}

	return ret;

}

int db_update_card_count(int eid, int cid, int cnum) {
	int ret;
	int len;


	if (cnum < 0 || cnum >9) {
		ERROR_RETURN(-2, "db_update_card_count:cnum out of bound %d", cnum);
	}

	len = sprintf(g_query, UPDATE_CARD_COUNT, cid, cnum, eid);
	printf("g_query = %s\n", g_query);
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_update_card_count:safe_mysql_real_query");
		return -55;
	}

	ret = mysql_affected_rows(g_conn); // must be 1 for success
	if (ret == 0) {
		ERROR_NEG_RETURN(-16, "db_update_card_count:update_error");
	}

	// shall we check ret < 0 ?
	if (ret != 1) {
		ERROR_NEG_RETURN(-17, "db_update_card_count:update_impossible");
	}

	return ret;

}


//"select t1.eid, t1.gold, t1.crystal, t2.c23 from evil_status t1, evil_card t2 where t1.eid = 51 and t2.eid = 51"
const char * UPDATE_BUY_CARD =
"UPDATE evil_card t1, evil_status t2 set t1.c%d=t1.c%d+1, t2.gold=t2.gold+(%d), t2.crystal=t2.crystal+(%d) where t1.eid=%d and t2.eid=%d";

int db_buy_card(int eid, int cid, int gold, int crystal) {
	int ret;
	int len;


	len = sprintf(g_query, UPDATE_BUY_CARD, cid, cid, gold, crystal, eid, eid);
	DEBUG_PRINT(len, "db_buy_card:g_query = %s\n", g_query);
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_buy_card:safe_mysql_real_query");
		return -55;
	}

	ret = mysql_affected_rows(g_conn); // must be 1 for success
	if (ret == 0) {
		ERROR_NEG_RETURN(-16, "db_buy_card:update_error");
	}

	// shall we check ret < 0 ?
	if (ret != 2) {
		ERROR_NEG_RETURN(-17, "db_buy_card:update_impossible");
	}

	return ret;

}


const char * UPDATE_SELL_CARD =
"UPDATE evil_card t1, evil_status t2 set t1.c%d=t1.c%d-1, t2.gold=t2.gold+(%d), t2.crystal=t2.crystal+(%d) where t1.eid=%d and t2.eid=%d";

int db_sell_card(int eid, int cid, int gold, int crystal) {
	int ret;
	int len;


	len = sprintf(g_query, UPDATE_SELL_CARD, cid, cid, gold, crystal, eid, eid);
	DEBUG_PRINT(len, "db_sell_card:g_query = %s\n", g_query);
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_sell_card:safe_mysql_real_query");
		return -55;
	}

	ret = mysql_affected_rows(g_conn); // must be 1 for success
	if (ret == 0) {
		ERROR_NEG_RETURN(-16, "db_sell_card:update_error");
	}

	// shall we check ret < 0 ?
	if (ret != 2) {
		ERROR_NEG_RETURN(-17, "db_sell_card:update_impossible");
	}

	return ret;

}



// input: filename 
// output: content
// return the latest ? (bugtime is the newest)
// @return eid 
int db_load_debug(const char * filename, char *content) {
	// SELECT eid, content FROM evil_debug WHERE filename = '%s' ORDER BY bugtime DESC LIMIT 1

	int ret;
	int len;
	char esc_filename[300];

	len = mysql_real_escape_string(g_conn, esc_filename
		, filename , strlen(filename));

	len = sprintf(g_query, "SELECT eid, content FROM evil_debug WHERE "
		"filename = '%s' ORDER BY bugtime DESC LIMIT 1", esc_filename);

	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		int err = mysql_errno(g_conn);
		ERROR_RETURN(-55, "db_load_debug mysql_errno %d", err);
		return -55;
	}

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	result = mysql_store_result(g_conn);

	if (result == NULL) {
		ERROR_PRINT(-3, "db_load_debug null result");
		return -3; // early exit
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;	// normal user not found
		DEBUG_PRINT(ret, "db_load_debug filename not found %s", filename);
		goto cleanup;
	}
	row = mysql_fetch_row(result);
	if (row == NULL) {
		ret = -13;
		ERROR_PRINT(ret, "db_load_debug null row");
		goto cleanup; // cleanup and early exit
	}
	// implicit: row != NULL

	// row[0], row[1] = eid, content
	ret = strtol_safe(row[0], -77); // normally 

	// copy the content as output parameter
	strncpy(content, row[1], 3000);
	content[3000] = 0; // safety


cleanup:
	mysql_free_result(result);
	return ret;
}

int db_save_debug(int eid, const char * filename, const char *content)
{
	int len;
	int ret;
	char esc_filename[200];
	char esc_content[8000]; // a bit too large

	assert(g_conn != NULL);

	len = mysql_real_escape_string(g_conn, esc_filename
		, filename , strlen(filename));
	ERROR_NEG_RETURN(len, "db_save_debug:esc_filename");

	len = mysql_real_escape_string(g_conn, esc_content
		, content, strlen(content));
	ERROR_NEG_RETURN(len, "db_save_debug:esc_content");

	len = sprintf(g_query, "INSERT INTO evil_debug VALUES (NULL, %d, NOW(), '%s', '%s')"
		, eid, esc_filename, esc_content);
	ERROR_NEG_RETURN(len, "db_save_deck query");
	DEBUG_PRINT(len, "save_debug: %s", g_query);

	
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		int err = mysql_errno(g_conn);
		ERROR_RETURN(-55, "db_save_debug mysql_errno %d", err);
		return -55;
	}

	// check affected row
	ret = mysql_affected_rows(g_conn); // replace can be 1 or 2
	if (ret < 1 || ret > 2) {
		ERROR_NEG_RETURN(-6, "db_save_debug affected_row wrong %d\n", ret);
	}

	return 0;
}

/////// DB FUNCTION END ////////




///////////////////////////////////////////
/////////////  邪恶的分割线 ///////////////
///////////////////////////////////////////
///////////////////////////////////////////
////////// production code ABOVE //////////
////////// testing code BELOW    //////////
///////////////////////////////////////////
///////////////////////////////////////////

#ifdef TTT

// print_card for debug
void print_card(const char * card, int card_count)
{
	int first_flag = 0;
	// printf("card : ");
	for (int i=0; i<card_count; i++) {
		if (0==card[i]) continue;
		if (first_flag) printf(", "); else first_flag = 1;
		printf("%dx%d", i+1, (int)card[i]);
	}
	printf("\n");
}

////////// testcase start /////////

/**
 * this is mainly for testing, flood the evil_card table
 */
int insert_mass_card(char * query, int eid_start, int eid_end, 
	char *card, int card_count)
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
	assert(card_count >= 0);
	assert(card!=NULL);

	// (to, from from_len)
	// mysql_escape_string( )

	end = query;  // make it like recursive
	end = stpcpy(end, "INSERT INTO evil_card (eid");
	// open (
	////////// 
	for (int i=1; i<=card_count; i++) {
		sprintf(numstr, ",c%d", i);
		end = stpcpy(end, numstr);
	}
	//////////

	// close )
	end = stpcpy(end, ") VALUES ");

	for (int eid=eid_start; eid<=eid_end; eid++) {
		
		if (eid == eid_start) {
			sprintf(numstr, "(%d", eid);  
		} else {
			sprintf(numstr, ",(%d", eid);  
		}
		// open (
		end = stpcpy(end, numstr);

		// TODO check card_count
		for (int i=1; i<=card_count; i++) {
			if (card[i]==0) {
				sprintf(numstr, ",NULL"); // check whether it save space?
			} else {
				sprintf(numstr, ",%d", (int)card[i]);
			}
			end = stpcpy(end, numstr);
		}

		// close )
		end = stpcpy(end, ")"); 
	}

	int len = end - query;
	// printf("query(%d) [%s]\n", len, query);
	return len;
}


// standard deck max, mainly for testing
#define STD_DECK_MAX	40
int set_card(char *card, int card_count, int hero_id) {

	int deck_zhanna[STD_DECK_MAX] = { // id = 8
         // 5 id a row
    22, 22, 23, 23, 26, // dirk x 2, sandra x 2, puwen x 3
    26, 26, 27, 27, 28, // birgitte x 2, kurt x 1, 
    30, 30, 30, 91, 91, // blake x 3, healingtouch(91) x 2
    92, 92, 94, 94, 95, // innerstr(92)x2, icestorm(94)x2, focus prayer x 2
    95, 96, 97, 97, 99, // resurrection(96), holyshield(97)x2, smite(99)x2
    99, 100, 131, 132, 133, // book of curses(100), c_o_night, retreat, armor
    134, 135, 151, 151, 154, // special, campfire, rain(151)x2, extra(154)x2
    154, 155, 155, 173, 173, //bazaar(155)x2, plate armor(172)x2
	};

	int *deck;

	assert(card_count >= STD_DECK_MAX);
	bzero(card, card_count+1);
	card[hero_id-1] = 1; // one hero  // base zero, so -1


	// hard code priest deck
	deck = deck_zhanna;  // TODO switch hero_id
	for (int i=0; i<STD_DECK_MAX; i++) {
		if (deck[i] < 1 || deck[i] > card_count)  {
			ERROR_NEG_RETURN(-2, "deck[%d] overflow %d", i, deck[i]);
		}
		card[deck[i] - 1] ++;
	}

	return 0;
}

#define TEST_QUERY_MAX	30000000

static char test_query[TEST_QUERY_MAX];
int test0(int argc, char *argv[]) {
	int ret;
	ret = safe_mysql_query("SELECT eid, username FROM evil_user LIMIT 5");
	if (0 != ret) {
		ERROR_PRINT(-55, "main mysql_errno %d", mysql_errno(g_conn));
		return -55;
	}
	MYSQL_RES *result;
	result = mysql_store_result(g_conn);

	if (result == NULL) {
		printf("---- error result\n");
		ERROR_PRINT(-3, "mysql_store_result");
		return -3; // early exit
	}

	MYSQL_ROW row;
	int row_id = 0;
	while ( (row = mysql_fetch_row(result)) ) {
		row_id ++;
		printf("Row  %d : eid %s  username %s\n", row_id, row[0], row[1]);
	}
	mysql_free_result(result);  // clean up, must have

	return 0;
}

int test1(int argc, char *argv[]) {
			char card[100];
			int len;
			for (int i=0; i<=20; i++) card[i] = i % 5;
			len = query_card_list("INSERT INTO evil_card ", g_query, 
				QUERY_MAX, 10, card, 20);
			printf("query(%d)  = [%s]\n", len, g_query);
			return 0;
}

int test2(int argc, char *argv[]) {
			printf("query_insert_card:\n");
			char card[100];
			int card_count = 3;
			int len;
			card[0] = 99;
			card[1] = 0;
			card[2] = 4;
			card[3] = 2;
			len = query_insert_card(test_query, 100, 60, card, card_count);
			ERROR_NEG_RETURN(len, "query_insert_card short");
			printf("query(%d) = [ %s ]\n", len, test_query);
			return 0;
}

// DANAGEROUS testcase 
int test3(int argc, char *argv[]) {
	printf("insert_mass_card:\n");
	char *end = NULL;
	char card[EVIL_CARD_MAX+1];
	int card_count = EVIL_CARD_MAX;
	int eid_start = 30;
	int eid_end = 35;
	long value;
	int len;
	int ret;
	if (argc >= 3) {
		value = strtol(argv[2], &end, 10);
		if (end>argv[2]) eid_start = (int) value;
	}
	if (argc >= 4) {
		value = strtol(argv[3], &end, 10);
		if (end>argv[3]) eid_end = (int) value;
	}
	printf("eid_start %d   eid_end %d\n", eid_start, eid_end);
	ERROR_RETURN(eid_end < eid_start, "eid_end %d < eid_start %d", 
		eid_start, eid_end);
	const int max = TEST_QUERY_MAX / 2000; // 210;
	ERROR_RETURN(eid_end - eid_start > max, 
		"eid range overflow %d max %d", eid_end-eid_start, max); 
	for (int i=1; i<=EVIL_CARD_MAX; i++) {
		card[i] = i % 10;
		if ((i % 10)==0) card[i] = 0;
	}
	len = insert_mass_card(test_query, eid_start, eid_end, 
		card, card_count);
	printf("eid range %d   len %d   len/range %d\n",
		eid_end-eid_start, len, len / (eid_end-eid_start));
	// printf("query(%d) = [ %s ]\n", len, query);
	ret = safe_mysql_real_query( test_query, len);
	ERROR_RETURN(ret, "real_query");
	return 0;
}

int test4(int argc, char *argv[]) {
	int len;
	printf("query_replace_card:\n");
	char card[EVIL_CARD_MAX+1];
	int card_count = EVIL_CARD_MAX;
	card[0] = 0;
	for (int i=1; i<=EVIL_CARD_MAX; i++) {
		card[i] = i % 4;
	}
	len = query_replace_card(test_query, 4000, 100, card, card_count);
	ERROR_NEG_RETURN(len, "query_replace_card");
	printf("query(%d) = [ %s ]\n", len, test_query);
	return 0;
}

int test5(int argc, char *argv[]) {
	int ret;
	int eid = 40;  // 10 is peter (only 40 - 49 has card)
	if (argc > 2) eid = strtol_safe(argv[2], eid);
	printf("test:db_load_card [eid] %d:\n", eid);
	char card[EVIL_CARD_MAX+1];
	int card_count = EVIL_CARD_MAX;

	ret = db_load_card(eid, card);
	ERROR_NEG_RETURN(ret, "test:db_load_card");
	printf("load card: ");
	print_card(card, card_count);
	return 0;
}

int test6(int argc, char *argv[]) {
	int eid = 25; // x5 no card
	int ret;
	if (argc > 2) eid = strtol_safe(argv[2], eid);
	printf("db_save_card eid=%d\n", eid);
	char card[EVIL_CARD_MAX+1];
	int card_count = EVIL_CARD_MAX;
	bzero(card, EVIL_CARD_MAX+1);

	set_card(card, card_count, 8);  // 8=zhanna priest
	printf("save card: ");
	print_card(card, card_count);

	ret = db_save_card(eid, card);
	if (ret == -22) {
		printf("NOTE:  test:db_save_card(eid=%d, card) can be called once only!!  do db-init.sql again\n", eid);
	}
	ERROR_NEG_RETURN(ret, "db_save_card");
	return ret;
}



int test7(int argc, char *argv[]) {
	int ret;
	int eid = 15;  // testuser15
	// precondition:  c22 = 8
	printf("db_save_deck:\n");
	char card[EVIL_CARD_MAX+1];
	int card_count = EVIL_CARD_MAX;
	bzero(card, EVIL_CARD_MAX+1);

	set_card(card, card_count, 8);
	card[22] = 0;  // actually this is 23
	printf("deck: ");
	print_card(card, card_count);

	ret = db_save_deck(eid, card); // core logic
	ERROR_NEG_RETURN(ret, "db_save_deck");
	return 0;
}

// test load deck
int test8(int argc, char *argv[]) {
	int ret;
	int eid = 15; // testuser15

	// normally, we will save the deck and card in register
	printf("db_load_deck:\n");  // note: empty row ret -6
	char card[EVIL_CARD_MAX+1];
	int card_count = EVIL_CARD_MAX;

	ret = db_load_deck(eid, card);  // core logic
	ERROR_NEG_RETURN(ret, "db_load_deck");
	printf("deck: ");
	print_card(card, card_count);
	return 0;
}

// test db_register_user
int test9(int argc, char *argv[]) {
	// register
	// usage: db 9 [username] [alias]
	int ret;
	char username[EVIL_USERNAME_MAX + 1] = "t9";
	char password[EVIL_PASSWORD_MAX + 1] = "pass";
	char alias[EVIL_ALIAS_MAX + 1] = ""; // "a9";
	if (argc > 3) {
		strcpy(username, argv[2]);
		strcpy(alias, argv[3]);
	}
	printf("db_register_user  username [%s]  alias [%s]:\n"
		, username, alias);
	evil_user_t	user;

	ret = db_register_user(&user, username, password, alias); 
	ERROR_NEG_RETURN(ret, "test case 9 db_register_user");
	printf("OK eid %d\n", ret);
	return ret;
}

int test10(int argc, char *argv[]) {
	int ret;
	// db_login
	char username[EVIL_USERNAME_MAX + 1] = "NNn"; // this is valid init
	char password[EVIL_PASSWORD_MAX + 1] = "ppp";
	evil_user_t user;
	if (argc > 3) {
		strcpy(username, argv[2]);
		strcpy(password, argv[3]);
	}
	printf("db_login  username [%s]  password [%s]\n"
		, username, password);
	
	ret = db_login(&user, username, password);
	ERROR_NEG_RETURN(ret, "db_login");
	printf("OK eid %d  alias %s\n", user.eid, user.alias);
	return ret;
}

int test11(int argc, char *argv[]) {
			// sscanf(str, "%d %d%n", ...)
			int ret;
			int total, from_id, n;
			char str[] = "5 1 9 88 7 66 555";
			char *ptr;
			ret = sscanf(str, "%d %d %n", &total, &from_id, &n);
			printf("ret=%d   total=%d  from_id=%d  n=%d\n", ret, total, from_id, n);
			printf("REST: [%s]\n", str + n);

			ptr = str + n;
			int card_list[total]; // shall we do malloc ?
			bzero(card_list, sizeof(int)*total);
			for (int i=0; i<total; i++) {
				int id = 1;
				ret = sscanf(ptr, "%d %n", &id, &n);
				if (ret != 1) {
					printf("BUG sscanf ret %d\n", ret);
					break;
				}
				card_list[i] = id;
				ptr += n;
			}

			printf("card_list :");
			for (int i=0; i<total; i++) {
				printf(" %d", card_list[i]);
			}
			printf(" END\n");
			return ret;
}

int test12(int argc, char *argv[]) {
			// db_load_status
			int ret;
			int eid = 45; // www
			if (argc > 2) {
				eid = strtol_safe(argv[2], eid);
			}
			evil_user_t user;
			user.eid = eid;

			ret = db_load_status(&user);
			printf("User: eid=%d " " lv=%d rating=%.2lf" 
				"  gold=%d crystal=%d\n" 
				// " gid=%d gpos=%d\n" 
				, user.eid
				, user.lv, user.rating
				, user.gold, user.crystal
				// , user.gid, user.gpos
				);

			printf("Game: count=%d  win=%d  lose=%d  draw=%d  run=%d\n"
			, user.game_count, user.game_win, user.game_lose, user.game_draw
			, user.game_run);
			return ret;
}

int test13(int argc, char *argv[]) {
			int ret;
			int eid = 20;
			if (argc > 2) {
				eid = strtol_safe(argv[2], eid);
			}
			evil_user_t user;
			user.eid = eid;
			user.lv = 5; user.rating = 1012.5; user.gold = 999;	user.crystal = 50; 	
			user.gid = 1;	user.gpos = 2;
			user.game_count = 33; user.game_win = 20; user.game_lose=10; 
			user.game_draw = 2; user.game_run = 1;  // total match game_count
			// TODO user.gold = 999;
			printf("User: eid=%d  gold=%d  crystal=%d  lv=%d  rating=%lf\n"
			,	user.eid, user.gold, user.crystal, user.lv, user.rating );

			printf("Game: count=%d  win=%d  lose=%d  draw=%d  run=%d\n"
			, user.game_count, user.game_win, user.game_lose, user.game_draw
			, user.game_run);

			ret = db_save_status(&user);
			printf("db_save_status ret: %d\n", ret);
			
			return ret;
}

// test db_alias()     
int test14(int argc, char *argv[]) {
			int ret;
			int eid = 50;  // default to nnn @see db-init.sql
			char alias[EVIL_ALIAS_MAX + 1];
			strcpy(alias, "def_alias");
			if (argc > 2) {
				eid = strtol_safe(argv[2], eid);
			}
			if (argc > 3) {
				strcpy(alias, argv[3]); // beware  too long
			}
			printf("test14 update alias to [%s] for eid=%d\n", alias, eid);

			ret = db_alias(eid, alias);
			printf("db_alias ret: %d\n", ret);
			return ret;
}

// test db_win(int win_eid, int lose_eid)
int test15(int argc, char *argv[]) {
	int ret = 0;
	int win_eid = 15;  // default to nnn @see db-init.sql
	int lose_eid = 16;
	if (argc > 2) {
		win_eid = strtol_safe(argv[2], win_eid);
	}
	if (argc > 3) {
		lose_eid = strtol_safe(argv[3], lose_eid);
	}

	ret = db_win(win_eid, lose_eid, 10.0);
	ERROR_RETURN(ret, "db_win_error");
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

// test db_save_replay
int test16(int argc, char *argv[]) {
	int ret = 0;
	long gameid;  // init below
	int win = 2;   // eid1 win
	int ver = 5; // fake version
	int eid1 = 47;  // 47='x',  48='y', @see test21 for db_load_replay
	int eid2 = 48;
	int seed = 900;
	char deck1[401], deck2[401];
	char cmd[2000];

	strcpy(deck1, "0100000000000000001000004000000000000000000000000000000000000000000000200000000000000000000000000010000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");
	strcpy(deck2, "0000000080000000000100004000000000000000000000000000000000000000000000200000000000000000000000000010000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");

	strcpy(cmd, "sac 1206;ab 1201;at 1301 2101");

	gameid = get_usec();
	ret = db_save_replay(gameid, win, seed, ver, eid1, eid2 
	, deck1, deck2, cmd);

	ERROR_PRINT(ret, "test16_db_save_replay");

	return 0;
}




// 
// ref: ccUTF8.cpp  (cocos2dx/support/ccUTF8.cpp)
#define UTF8_GET(Result, Chars, Count, Mask, Len)    \
(Result) = (Chars)[0] & (Mask);            \
for ((Count) = 1; (Count) < (Len); ++(Count))        \
{                            \
if (((Chars)[(Count)] & 0xc0) != 0x80)        \
{                        \
(Result) = -1;                \
break;                    \
}                        \
(Result) <<= 6;                    \
(Result) |= ((Chars)[(Count)] & 0x3f);        \
}

// Code from GLIB gutf8.c starts here.   peter: also from ccUTF8.cpp
#define UTF8_COMPUTE(Char, Mask, Len)        \
if (Char < 128)                \
{                        \
Len = 1;                    \
Mask = 0x7f;                \
}                        \
else if ((Char & 0xe0) == 0xc0)        \
{                        \
Len = 2;                    \
Mask = 0x1f;                \
}                        \
else if ((Char & 0xf0) == 0xe0)        \
{                        \
Len = 3;                    \
Mask = 0x0f;                \
}                        \
else if ((Char & 0xf8) == 0xf0)        \
{                        \
Len = 4;                    \
Mask = 0x07;                \
}                        \
else if ((Char & 0xfc) == 0xf8)        \
{                        \
Len = 5;                    \
Mask = 0x03;                \
}                        \
else if ((Char & 0xfe) == 0xfc)        \
{                        \
Len = 6;                    \
Mask = 0x01;                \
}                        \
else                        \
Len = -1;


/*
 * g_utf8_get_char:
 * @p: a pointer to Unicode character encoded as UTF-8
 *
 * Converts a sequence of bytes encoded as UTF-8 to a Unicode character.
 * If @p does not point to a valid UTF-8 encoded character, results are
 * undefined. If you are not sure that the bytes are complete
 * valid Unicode characters, you should use g_utf8_get_char_validated()
 * instead.
 *
 * Return value: the resulting character
 **/
static unsigned int
cc_utf8_get_char (const char * p)
{
    int i, mask = 0, len;
    unsigned int result;
    unsigned char c = (unsigned char) *p;
    
    UTF8_COMPUTE (c, mask, len);
    if (len == -1)
        return (unsigned int) - 1;
    UTF8_GET (result, p, i, mask, len);
    
    return result;
}


int test17(int argc, char *argv[])
{
	int ret;
	int len;
	// TODO read evil_user where eid=11 (masha)
	//cc_utf8_get_char (const char * p)

	// assume eid=11 is masha, with alias in chinese

	len = sprintf(g_query, "SELECT eid, alias FROM evil_user WHERE eid=11 LIMIT 1");

	ret = safe_mysql_real_query( g_query, len);

	if (ret != 0) {
		int err = mysql_errno(g_conn);
		ERROR_NEG_RETURN(-55, "test17 safe_mysql_real_query error mysql_errno %d"
		, err);
	}

	MYSQL_RES * result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "test17 null result");
	}
	// must have 1 row, 2 fields: eid, alias
	num_row = mysql_num_rows(result);
	if (num_row != 1) {
		ERROR_PRINT(-7, "test17 num_row != 1: %d", num_row);
		goto cleanup;
	}
	assert(num_row == 1);
	field_count = mysql_field_count(g_conn);
	if (field_count != 2) {
		ERROR_PRINT(-17, "test17 field_count != 2: %d", field_count);
		goto cleanup;
	}
	assert(field_count == 2);

	// row[0]=eid, [1]=alias
	row = mysql_fetch_row(result);

	len = (int)strlen(row[1]);
	printf("masha alias hex (len=%d) :  ", len);
	for (int i=0; i<len; i++) {
		printf(" [%x]", (unsigned char) row[1][i]);
	}
	printf("\n");


	unsigned int uni;
	uni = cc_utf8_get_char(row[1]); // get the first char
	printf("First unicode: %x\n", uni);  // this is 9A6C = 马 (U4E00.pdf)
	// in UTF-8:  E9 A9 AC = 马

	// convertion from utf-8 to unicode:
	// if (((Chars)[(Count)] & 0xc0) != 0x80)   : BUG
	// Result = Chars[0] & Mask   // chinese len=3 mask=0x0f
	// for (i=1; i<=len; i++) {
	// 	(Result) <<= 6;                    
	// 	(Result) |= ((Chars)[(Count)] & 0x3f);  
	// }

	// 
	// Example: 
	// input: utf-8 : E9 A9 AC = 马
	// expect output:  0x9A6C = 马  (ref: U4E00.pdf)
	// 
	// result = E9 & 0x0f = 0x9     ( char[0] )
	// 
	// check: char[1](0xA9) & 0xc0 != 0x80 ?   = 0x80
	// result <<= 6  : result = 0x240
	// result |= char[1] (0xA9) & 0x3f : result = 0x240 | 0x29 = 0x269
	// 
	// check: char[2](0xAC) & 0xc0 != 0x80 ?   = 0x80
	// result <<= 6  : result = 0x9A40
	// result |= char[2] (0xAC) & 0x3f : result = 0x9A40 | 0x2c = 0x9A6C
	//
	// YES correct


cleanup:
	mysql_free_result(result);
	return ret;
}


// test writing 你好吗  into evil_user : eid=12(kelton):alias, 13(win):esc_alias
// E4 BD A0   E5 A5 BD   E5 90 97
int test18(int argc, char *argv[]) {
	int ret;
	int len;
	char alias[EVIL_ALIAS_MAX + 1] = {
		0xE4, 0xBD, 0xA0 
	,	0xE5, 0xA5, 0xBD
	,	0xE5, 0x90, 0x97
	, 	0x0	// null-terminator
	};


	const char *fmt = "UPDATE evil_user SET alias='%s' WHERE eid=%d";
	len = sprintf(g_query, fmt, alias, 42); // kelton
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		int err = mysql_errno(g_conn);
		ERROR_RETURN(-55, "test18 safe_mysql_real_query 111 %d", err);
	}
	printf("DEBUG: updated kelton alias to chinese\n");



	int len_alias = strlen(alias);  
	printf("DEBUG len_alias=%d\n", len_alias); // 9 ?
	char esc_alias[EVIL_ALIAS_MAX * 2 + 1]; 
	len = mysql_real_escape_string(g_conn, esc_alias, alias, len_alias);
	ERROR_NEG_RETURN(len, "esc_alias");
	len = sprintf(g_query, fmt, esc_alias, 13); // win
	ret = safe_mysql_real_query( g_query, len);
	if (ret != 0) {
		int err = mysql_errno(g_conn);
		ERROR_RETURN(-55, "test18 safe_mysql_real_query 222 %d", err);
	}
	printf("DEBUG: updated win alias to chinese(use escape sequence)\n");

	return ret;
}

int test19(int argc, char *argv[]) {
// int db_save_debug(int eid, const char * filename, const char *content)

	int ret;
	int eid = 43;
	const char * filename = "game_12345.txt";
	char content[4000];
	time_t now;

	now = time(NULL);
	sprintf(content, "bug 123;time is %ld", now);
	ret = db_save_debug(eid, filename, content);

	ERROR_RETURN(ret, "test19 db_save_debug error");
	return ret;
}

int test20(int argc, char *argv[]) {
// int db_load_debug(const char * filename, char *content) 

	int ret;
	const char * filename = "game_12345.txt";
	char content[4000];

	ret = db_load_debug(filename, content);
	ERROR_NEG_RETURN(ret, "test19 db_load_debug error");

	printf("db_load_debug ret=%d  content=%s\n", ret, content);

	return ret;
}

// db 21 [alias1] [alias2]
// alias1 = argv[2] or default to 'x'
// alias2 = argv[3] or default to 'y'
// @see test16 for sample data from x(47), y(48)
int test21(int argc, char *argv[]) {
// int db_load_replay(const char *alias1, const char *alias2, game_t * pgame)
	int ret;
	char alias1[EVIL_ALIAS_MAX + 1];
	char alias2[EVIL_ALIAS_MAX + 1];
	game_t game;
	strcpy(alias1, "x");
	strcpy(alias2, "y");

	if (argc >= 3) {
		strcpy(alias1, argv[2]);
	}
	if (argc >= 4) {
		strcpy(alias2, argv[3]);
	}
	printf("db_load_replay alias1=[%s]  alias2=[%s]\n", alias1, alias2);
	ret = db_load_replay(alias1, alias2, &game);

	ERROR_NEG_RETURN(ret, "test21_db_load_replay");

	printf("game:  gameid=%ld eid1=%d  eid2=%d  seed=%d   cmd=%s\n"
		, game.gameid, game.eid1, game.eid2, game.seed, game.cmd);
	printf("deck1: %s\n", game.deck[0]);
	printf("deck2: %s\n", game.deck[1]);
	return 0;
}



int test22(int argc, char *argv[]) {
	int ret = 0;
	int eid = 0;
	int cid = 0;
	int cnum = 0;

	printf("test22 argc=%d\n", argc);
	if (argc < 5) {
		printf("BUG test16 argc!=4");
		ret = -1;
		return ret;
	}
	eid = atoi(argv[2]);
	cid = atoi(argv[3]);
	cnum = atoi(argv[4]);
	printf("db_cccard eid=%d, cid=%d, cnum=%d\n", eid, cid, cnum);
	ret = db_cccard(eid, cid, cnum);

	ERROR_NEG_RETURN(ret, "db_cccard");
	printf("OK db_cccard!\n");
	return ret;
}


// test copy evil_deck 
int test23(int argc, char *argv[]) {
	int eid1 = 43; // from (src)
	int eid2 = 50; // to (target)
	int len;
	int ret;
	// TODO :

	if (argc > 2) {
		eid1 = atoi(argv[2]);
	}
	if (argc > 3) {
		eid2 = atoi(argv[3]);
	}
	if (eid1==0 || eid2==0) {
		ERROR_RETURN(-5, "invalid eid: eid1(src)=%d  eid2(target)=%d", eid1, eid2);
	}

	sprintf(g_query, "UPDATE evil_deck as t INNER JOIN evil_deck as s ON s.eid=%d AND t.eid=%d SET ", eid1, eid2);
	// 	t.c1=s.c1, t.c22=s.c22 
	// FROM evil_deck as s 
	for (int i=1; i<=400; i++) {
		char tmp[100];
		char sep = (i==1) ? ' ' : ',' ;
		sprintf(tmp, "%ct.c%d=s.c%d", sep, i, i);
		strcat(g_query, tmp);
	}
	len = strlen(g_query);
	printf("g_query: %s\n", g_query);


	ret = safe_mysql_real_query(g_query, len);
	ERROR_NEG_RETURN(ret, "test23:deck_copy_error");
	printf("deck_copy: src_eid=%d target_eid=%d ret=%d\n", eid1, eid2, ret);


	sprintf(g_query, "UPDATE evil_card as t INNER JOIN evil_card as s ON s.eid=%d AND t.eid=%d SET ", eid1, eid2);
	// 	t.c1=s.c1, t.c22=s.c22 
	// FROM evil_deck as s 
	for (int i=1; i<=400; i++) {
		char tmp[100];
		char sep = (i==1) ? ' ' : ',' ;
		sprintf(tmp, "%ct.c%d=s.c%d", sep, i, i);
		strcat(g_query, tmp);
	}
	len = strlen(g_query);

	ret = safe_mysql_real_query(g_query, len);
	ERROR_NEG_RETURN(ret, "test23:card_copy_error");
	printf("card_copy: %d\n", ret);

	return ret;
}

// db_draw
int test24(int argc, char * argv[]) {
	int eid1 = 45; 
	int eid2 = 46; 
	int ret;

	ret = db_draw(eid1, eid2);
	ERROR_PRINT(ret, "test24:db_draw_error");

	return ret;
}

// db_load_shop
int test25(int argc, char * argv[]) {
	int cid = 20; 
	int size = 10; 
	int ret;

	char info[1000];
	char *ptr;
	ptr = info;
	bzero(info, 1000);
	info[1000 - 1] = '\0';
	info[1000 - 2] = '\0';


	ret = db_load_shop(cid, size, info);
	ERROR_PRINT(ret, "test25:db_load_shop_error");

	DEBUG_PRINT(0, "test25:info=%s", info);
	int n;
	int num;
	ret = sscanf(ptr, "%d, %n", &num, &n);
	DEBUG_PRINT(0, "test25:num=%d", num);

	int ccc;
	int gold_buy;
	int gold_sell;
	int crystal_buy;
	int crystal_sell;
	for (int i=0;i<num;i++) {
		ptr += n;
		ret = sscanf(ptr, "%d %d %d %d %d %n"
			, &ccc, &gold_buy, &gold_sell, &crystal_buy, &crystal_sell, &n);
		DEBUG_PRINT(0, "test25:ccc=%d, gold_buy=%d, gold_sell=%d, crystal=%d, crystal=%d", ccc, gold_buy, gold_sell, crystal_buy, crystal_sell);
		
	}

	return ret;
}

// buy
int test26(int argc, char * argv[]) {
	int ret;
	int eid = 51;
	int cid = 23; 
	int shop_type; // 0=buy, 1=sell
	shop_type = 0;

	int buy_type; // 0=gold, 1=crystal
	buy_type = 0;

	int sell_type; // 0=gold, 1=crystal
	sell_type = 0;
	int buy_num = 1;
	int sell_num = 1;

	int card_count;
	evil_user_t euser;

	ret = db_get_card_count(eid, cid, &card_count);
	ERROR_RETURN(ret, "test26:db_get_card_count_error");
	DEBUG_PRINT(0, "test26:card_count=%d", card_count);

	if (shop_type == 0 && card_count + buy_num > 9) {
		ERROR_RETURN(1, "test26:card_count_buy_out_bound");
	}

	if (shop_type == 1 && card_count - sell_num < 0) {
		ERROR_RETURN(1, "test26:card_count_sell_out_bound");
	}
		
	///////

	ret = db_get_euser_money(eid, &euser);
	ERROR_RETURN(ret, "test26:db_get_euser_money_error");
	DEBUG_PRINT(0, "test26:gold=%d, crystal=%d", euser.gold, euser.crystal);

	//////

	int gold_buy;
	int gold_sell;
	int crystal_buy;
	int crystal_sell;
	ret = db_get_card_price(cid, &gold_buy, &gold_sell, &crystal_buy, &crystal_sell);
	ERROR_PRINT(ret, "test26:db_get_card_price_error");
	DEBUG_PRINT(0, "test26:gold_buy=%d, gold_sell=%d, crystal_buy=%d, crystal_sell=%d", gold_buy, gold_sell, crystal_buy, crystal_sell);

	if (shop_type == 0 && buy_type == 0 && euser.gold < gold_buy * buy_num) {
		ERROR_RETURN(1, "test26:gold_not_enough");
	}

	if (shop_type == 0 && buy_type == 1 && euser.crystal < crystal_buy * buy_num) {
		ERROR_RETURN(1, "test26:crystal_not_enough");
	}
	

	if (shop_type == 0) {
		if (buy_type == 0) {
			euser.gold -= gold_buy * buy_num;
		} else {
			euser.crystal -= crystal_buy * buy_num;
		}
	} else {
		if (sell_type == 1) {
			euser.gold += gold_sell * sell_num;
		} else {
			euser.crystal += crystal_sell * sell_num;
		}
	}

	if (euser.gold < 0 || euser.crystal < 0) {
		ERROR_RETURN(1, "test26:money_not_enough");
	}
		
	//////////////////////////////////

	ret = db_update_euser_money(eid, euser.gold, euser.crystal);
	ERROR_NEG_RETURN(ret, "test26:db_update_euser_money_error");

	if (shop_type == 0) {
		card_count += buy_num;
	} else {
		card_count -= sell_num;
	}

	if (card_count < 0 || card_count > 9) {
		ERROR_RETURN(1, "test26:card_count_out_bound");
	}

	ret = db_update_card_count(eid, cid, card_count);
	ERROR_NEG_RETURN(ret, "test26:db_update_card_count_error");

	return ret;
}


int test27(int argc, char * argv[]) {
	int ret;
	int cid;
	int eid;

	eid = 51;
	cid = 23;

	int buy_type; //0=gold, 1=crystal
	int sell_type; //0=gold, 1=crystal
	int gold_buy;
	int gold_sell;
	int crystal_buy;
	int crystal_sell;

	buy_type = 0;
	sell_type = 0;
	gold_buy = 0;
	crystal_buy = 0;
	gold_sell = 0;
	crystal_sell = 0;

	ret = db_get_card_price(cid, &gold_buy, &gold_sell, &crystal_buy, &crystal_sell);
	ERROR_PRINT(ret, "test27:db_get_card_price_error");
	DEBUG_PRINT(0, "test27:gold_buy=%d, gold_sell=%d, crystal_buy=%d, crystal_sell=%d", gold_buy, gold_sell, crystal_buy, crystal_sell);
	gold_buy = -gold_buy;
	crystal_buy = -crystal_buy;

	if (buy_type == 0) {
		crystal_buy = 0;
	} else {
		gold_buy = 0;
	}

	ret = db_buy_card(eid, cid, gold_buy, crystal_buy);
	ERROR_NEG_RETURN(ret, "test27:db_buy_card_error");


	/*
	if (sell_type == 0) {
		crystal_sell = 0;
	} else {
		gold_sell = 0;
	}

	ret = db_sell_card(eid, cid, gold_sell, crystal_sell);
	ERROR_NEG_RETURN(ret, "test27:db_sell_card_error");
	*/

	return ret;

}

typedef int (*testcase_t) (int, char*[]); //  testcase_t;

testcase_t test_list[] = {
	test0
,	test1
,	test2
, 	test3
, 	test4
, 	test5
, 	test6
,	test7
,	test8
, 	test9
,	test10
,	test11
,	test12
,	test13
,	test14	// db_alias
,	test15	// db_win
,	test16	// db_save_replay
, 	test17	// test db chinese
,	test18	// test write chinese to db: evil_user
,	test19	// db_save_debug
,	test20	// db_load_debug
,	test21	// db_load_replay
,	test22	// db_cccard
,	test23	// copy deck
,	test24	// db_draw
,	test25	// db_load_shop
,	test26	// db_buy
,	test27	// db_buy
};


int test_selector(int argc, char *argv[])
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
	ret = test_list[testcase](argc, argv);
	
	printf("RET %d\n", ret);
	if (ret < 0) {
		printf("XXXXXXXXX BUG ret < 0: %d\n", ret);
	}
	
	return ret;
}


////////// testcase end //////////


int main(int argc, char *argv[])
{
	int ret;
	// char str[100];

	db_init();  // @see db_clean()
	assert(NULL!=g_conn);

	// test case after mysql conn is initialized !
	if (argc > 1 && strcmp("all", argv[1])==0) {
		// loop all
		int testmax = sizeof(test_list) / sizeof(test_list[0]);
		int error_count = 0;
		for (int i=0; i<testmax; i++) {
			printf("RUN test%d:\n", i);
			ret = test_list[i](argc, argv);
			if (ret < 0)  {
				printf("XXXXXXXXXX BUG:  ret %d\n", ret);
				error_count ++;
			} else {
				printf("RET %d\n", ret);
			}
		}
		printf("SUMMARY:  error_count = %d\n", error_count);
		goto cleanup;
	}

	ret = test_selector(argc, argv);
	goto cleanup;

	// finally close connection and end the library (free)
cleanup:
	db_clean();
//	mysql_close(g_conn);
//	mysql_library_end();
	return ret;
}

#endif	// TTT
