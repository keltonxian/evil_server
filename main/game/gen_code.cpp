/**
 * gen_code.cpp
 *
 * gen_code [key_count] [gift_id] [expire_time] [key_len] [prefix]
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
#define MYSQL_PASSWORD	"New2xin" //"1"
#define MYSQL_DATABASE	"evil"

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




static time_t get_expire_time(time_t tt, int day)
{
	time_t yesterday;
	struct tm timestruct;

	// second become m_day
	localtime_r(&tt, &timestruct);

	// clear hour:min:second to 0
	timestruct.tm_sec = 0;
	timestruct.tm_min = 0;
	timestruct.tm_hour = 0;

	timestruct.tm_mday = timestruct.tm_mday + day;

	yesterday = mktime(&timestruct);
	yesterday --;

	localtime_r(&yesterday, &timestruct);
//	printf("yesterday %ld %s\n", yesterday, asctime(&timestruct));

	return yesterday;
}

//////////////////////////////////////////////////
//////////////////// DB END ] ////////////////////
//////////////////////////////////////////////////


#define STR_DIGIT_ELE	"0123456789"
#define STR_UPPER_ELE	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define STR_LOWER_ELE	"abcdefghijklmnopqrstuvwxyz"
#define STR_DIGIT_UPPER_ELE		"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define STR_DIGIT_LOWER_ELE		"0123456789abcdefghijklmnopqrstuvwxyz"
#define STR_CHAR_ELE		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
#define STR_ALL_ELE		"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
const char* __get_code_element_str(int char_type)
{
	switch (char_type)
	{
		case 0: { return STR_ALL_ELE; }
		case 1: { return STR_DIGIT_LOWER_ELE; }
		case 2: { return STR_DIGIT_UPPER_ELE; }
		case 3: { return STR_DIGIT_ELE; }
		case 4: { return STR_CHAR_ELE; }
		case 5: { return STR_LOWER_ELE; }
		case 6: { return STR_UPPER_ELE; }
	}
	return "";
}

int __generate_code(char *key_code, char *prefix, int key_len, const char* STR_ELE)
{
	int pre_len;
	sprintf(key_code, prefix);

	pre_len = strlen(key_code);
	static int STR_ELE_LEN = strlen(STR_ELE);
	for (int i = pre_len; i < key_len; i++)
	{
		key_code[i] = STR_ELE[rand() % STR_ELE_LEN];
	}

	return 0;
}


#define SQL_INSERT_GEN_CODE_TIME	"INSERT INTO evil_channel_gift VALUES('%s', %d, 0, FROM_UNIXTIME(%ld), 0)"
#define SQL_INSERT_GEN_CODE			"INSERT INTO evil_channel_gift VALUES('%s', %d, 0, 0, 0)"
int __add_key_code(MYSQL **pconn, char *q_buffer, int gift_id, int key_count, int day, char *prefix, int key_len, int char_type)
{
	int ret;
	int err;
	int len;

	time_t expire_time;
	char key_code[35];
	const char* STR_ELE = __get_code_element_str(char_type);

	expire_time = 0;
	bzero(key_code, sizeof(key_code));

	if (day > 0)
	{
		expire_time = get_expire_time(time(NULL), day);
//		printf("day[%d] expire_time[%ld] yesterday[%ld]\n"
//		, day, expire_time, get_expire_time(time(NULL), 0));
	}

//	for (int i = 0; i < key_count; i++)
	while(key_count > 0)
	{
		ret = __generate_code(key_code, prefix, key_len, STR_ELE);
		ERROR_RETURN(ret, "add_key_code:generate_code_fail");

		if (expire_time > 0)
		{
			len = sprintf(q_buffer, SQL_INSERT_GEN_CODE_TIME
			, key_code, gift_id, expire_time);
		} else {
			len = sprintf(q_buffer, SQL_INSERT_GEN_CODE, key_code
			, gift_id);
		}
		ret = my_query(pconn, q_buffer, len);
		if (ret != 0) {
			err = mysql_errno(*pconn);
			ERROR_RETURN(-5, "add_key_code:mysql_errno %d", err);
		}

		ret = mysql_affected_rows(*pconn);
		if (ret <= 0) {
			WARN_PRINT(-6, "add_key_code:affected_row_wrong %d", ret);
			continue;
//			ERROR_RETURN(-6, "add_key_code:affected_row_wrong %d", ret);
		}
		key_count--;
	}

	return 0;
}

///////////////////// GENERAL TESTCASE FUNCTION //////////////////

void show_help()
{
//				#define ERROR_TAG	"\x1B[31mERROR "
//				#define INFO_TAG	"\x1B[34mINFO "
//				#define END_TAG		"\x1B[0m\n"	// clean up color code
	printf("Usage: gen_code [gift_id] [key_count] [day] [key_len] [prefix] [char_type]\n");
	printf("\tNotice: table \x1B[31mdesign.design_gift\x1B[0m and \x1b[31mdesign.design_rate_card\x1b[0m should be settled before using.\n");
	printf("\tgift_id is needed, others could be use default value.\n");
	printf("Example:\n");
	printf("\t1. gen_code 1\n");
	printf("\t   \x1B[34mIt will generate 100 key_code in \x1B[0m\x1B[31mevil_channel_gift\x1B[0m\x1B[34m. It's the same effects as\x1B[0m\n");
	printf("\t   gen_code 1 100 0 10 0\n\n");
	printf("\t2. gen_code 5 30 100 12 17kp 1\n");
	printf("\t   \x1B[34mIt will generate 30 key_code with gift_id=5, 100 day limited, 12 length, started with 17kp, combined with digit and lower characters.\x1B[0m\n\n");
	printf("\tchar_type:\n");
	printf("\t   \x1B[34m0: digit, upper characters and lower characters\x1B[0m\n");
	printf("\t   \x1B[34m1: digit and lower characters\x1B[0m\n");
	printf("\t   \x1B[34m2: dight and upper characters\x1B[0m\n");
	printf("\t   \x1B[34m3: dight\x1B[0m\n");
	printf("\t   \x1B[34m4: upper characters and lower characters\x1B[0m\n");
	printf("\t   \x1B[34m5: lower characters\x1B[0m\n");
	printf("\t   \x1B[34m6: upper characters\x1B[0m\n");
}

int main(int argc, char *argv[])
{
	int ret;
	MYSQL * conn;
	char q_buffer[QUERY_MAX+1];

	int gift_id;
	int key_count;
	int day;
	int key_len;
	char prefix[10];
	int char_type;
	gift_id		= 0;
	key_count	= 100;
	day			= 0;
	key_len		= 10;
	bzero(prefix, sizeof(prefix));
	char_type	= 0;


	switch (argc) {
		case 7:
			char_type	= strtol_safe(argv[6], 0);
			if (char_type < 0 || char_type > 6)
			{
				ERROR_PRINT(-5, "gen_code:invalid_char_type[%d]", char_type);
				show_help();
				return 0;
			}
		case 6:
			sprintf(prefix, argv[5]);
		case 5:
			key_len		= strtol_safe(argv[4], 10);
		case 4:
			day			= strtol_safe(argv[3], 0);
		case 3:
			key_count = strtol_safe(argv[2], 100);
		case 2:
			gift_id		= strtol_safe(argv[1], 0);

			if (gift_id > 0)
			{
				break;
			}
			ERROR_PRINT(-5, "gen_code:invalid_gift_id_value[%d]", gift_id);
		case 1:
		default:
			show_help();
			return 0;
	}

	srand(time(NULL));
	// input: gen_code gift_id [key_count] [expire_time] [key_len] [prefix]

	// make init is called in : non-multi-thread environment
	ret = mysql_library_init(0, NULL, NULL); // argc, argv, char**group
	ret = mysql_thread_init(); // test in single thread environment
	FATAL_EXIT(ret, "gen_code:mysql_library_init");

	conn = my_open();
	FATAL_EXIT(conn==NULL, "gen_code:mysql_open:null");

	ret = __add_key_code(&conn, q_buffer, gift_id, key_count, day
	, prefix, key_len, char_type);
	ERROR_RETURN(ret, "gen_code:generate_code_fail");

	// mysql_library_end();
	mysql_thread_end();
	return 0;
}


