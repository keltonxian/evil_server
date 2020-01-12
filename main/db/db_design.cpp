#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <sys/time.h>

#include "mysql.h"

#include "fatal.h"
#include "evil.h"

#define MYSQL_HOST	"127.0.0.1"
#define MYSQL_PORT	3306
#define MYSQL_USER	"evil"
#define MYSQL_PASSWORD	"1" // "1"
#define MYSQL_DATABASE	"evil_design"

#define QUERY_MAX (EVIL_CARD_MAX*5 + 100)


//#define BUG_NEG_CLEANUP(code, ...)     do { if ((code) < 0) { ret=code; BUG_PRINT(__VA_ARGS__);  goto cleanup; } } while(0)
#define BUG_CLEANUP(code, ret_val, ...)     do { if ((code)) { ret = ret_val; __PRINT_ERRNO_TIME((code), BUG_TAG, __VA_ARGS__); goto cleanup; } } while(0)


//////////  GLOBAL START //////////
MYSQL * g_db_conn = NULL;
char g_sql_query[QUERY_MAX+1];
//////////  GLOBAL END //////////

////////// UTILITY START /////////

// return the digit count for an input number [num]
// e.g. num=1  ret=1,    num=10 ret=2,  num=256 ret=3
// maximum is 5
// negative num is treated as math.abs(num)
int design_digit_count(int num)
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
/*

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
*/

////////// UTILITY END /////////


/////// DB INIT START //////////

// access to g_db_conn
int db_design_init()
{
	int ret;
	INFO_PRINT(0, "db_design_init: mysql : %s"
	, mysql_get_client_info());
	ret = mysql_library_init(0, NULL, NULL); // argc, argv, char**group
	ret = mysql_thread_init(); 
	FATAL_EXIT(ret, "db_design_init library_init");

	//  ----------------------
	g_db_conn = mysql_init(NULL);  // NULL ?
	// FATAL_EXIT(g_db_conn==NULL, "db_design_init:mysql_init");
	if (g_db_conn == NULL) {
		BUG_RETURN(-6, "db_design_init:g_db_conn==NULL");
	}


	// ref: setup utf8 connection
	// http://stackoverflow.com/questions/8112153/process-utf-8-data-from-mysql-in-c-and-give-result-back
	mysql_options(g_db_conn, MYSQL_SET_CHARSET_NAME, "utf8");   // was "utf8"
	// peter: SET NAMES is optional:
//    mysql_options(g_db_conn, MYSQL_INIT_COMMAND, "SET NAMES utf8");  // was "utf8"
	MYSQL * ret_conn;
	ret_conn = mysql_real_connect(g_db_conn, MYSQL_HOST, MYSQL_USER, 
		MYSQL_PASSWORD, MYSQL_DATABASE, MYSQL_PORT, NULL, 0);
	if (ret_conn == NULL) {
		ERROR_NEG_RETURN(-55, "db_design_init:mysql_real_connect %s", MYSQL_HOST);
	}
	// FATAL_EXIT(ret_conn==NULL, "db_design_init:mysql_real_connect");

	
	MY_CHARSET_INFO charset_info;
	mysql_get_character_set_info(g_db_conn, &charset_info);
	// pls check:  charset=utf8_general_ci  collate=utf8  num=33
//	DEBUG_PRINT(0, "mysql charset=%s collate=%s num=%d"
//	, charset_info.name, charset_info.csname, charset_info.number);
	return 0;
}

// access to g_db_conn
int db_design_clean()
{
	if (NULL==g_db_conn) {
		ERROR_PRINT(-3, "db_design_clean:g_db_conn=null");
		return -3;
	}
	mysql_close(g_db_conn);
	g_db_conn = NULL;
	// mysql_library_end();   
	mysql_thread_end();   
	return 0;
}

int db_design_reconn()
{
	int myerr = mysql_errno(g_db_conn);
	WARN_PRINT(-5, "db_design_reconn errno %d", myerr);
	db_design_clean();
	int ret = db_design_init();
	return ret;
}


// safe means it can do auto-reconnect
int design_safe_mysql_real_query(const char *q, int len)
{
	int ret;
	int count = 0;
again:
	ret = mysql_real_query(g_db_conn, q, len);
	if (ret == 0) {
		return 0;
	}
	// implicit: ret != 0 : means error
	int myerr = mysql_errno(g_db_conn);
	if (myerr==2013 || myerr==2006) {
		// sleep depends on count
		WARN_PRINT(-1, "db_conn:design_safe_mysql_real_query:retry %d", count);
		sleep(count);
		db_design_reconn();
		count++;
		if (count >= 3) {
			return ret;
		}
		goto again;
	}
	return ret;
}

int design_safe_mysql_query(const char *q)
{
	return design_safe_mysql_real_query(q, strlen(q));
}

////////// DB FUNCTION START /////////
const char * DESIGN_LOAD_SHOP = 
"SELECT * FROM design_shop";

int db_design_load_shop(shop_t * shop_list)
{
	int ret;
	int len;

	int ccc;
	int card_buy_gold;
	int card_sell_gold;
	int card_buy_crystal;
	int card_sell_crystal;
	int piece_buy_gold;
	int piece_sell_gold;
	int piece_buy_crystal;
	int piece_sell_crystal;


	// DEBUG_PRINT(0, "%s\n", DESIGN_LOAD_SHOP);

	len = strlen(DESIGN_LOAD_SHOP);

	ret = design_safe_mysql_real_query( DESIGN_LOAD_SHOP, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_design_load_shop:design_safe_mysql_real_query %d", ret); 
		return -55; // safety, should never run
	}

	// DEBUG_PRINT(0, "db_design_load_shop:query: %s", g_sql_query);

	
	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_design_load_shop:null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = -6;
		ERROR_PRINT(ret, "db_design_load_shop:empty row %d", num_row);
		goto cleanup;
	}

	// DEBUG_PRINT(0, "db_design_load_shop:num_row=%d", num_row);
	// ret = sprintf(info, "%d ", num_row);

	field_count = mysql_field_count(g_db_conn);
	// cid, card_buy_gold, card_sell_gold, card_buy_crystal, card_sell_crystal
	// piece_buy_gold, piece_sell_gold, piece_buy_crystal, piece_sell_crystal
	if (field_count != 9) { 
		ret = -7;
		ERROR_PRINT(ret, "db_design_load_shop:field_count %d != 9", field_count);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {

		ccc = strtol_safe(row[0], -5);
		if (ccc < 0 || ccc > EVIL_CARD_MAX) {
			ERROR_PRINT(-2, "load_shop:ccc out bound %d", ccc);
			continue;
		}

		card_buy_gold = strtol_safe(row[1], -5);
		if (card_buy_gold < 0) {
			ERROR_PRINT(-12, "db_design_load_shop:card_buy_gold %d %d"
			, ccc, card_buy_gold);
			continue;
		}

		card_sell_gold = strtol_safe(row[2], -5);
		if (card_sell_gold < 0) {
			ERROR_PRINT(-12, "db_design_load_shop:card_sell_gold %d %d"
			, ccc, card_sell_gold);
			continue;
		}

		card_buy_crystal = strtol_safe(row[3], -5);
		if (card_buy_crystal < 0) {
			ERROR_PRINT(-12, "db_design_load_shop:card_buy_crystal %d %d"
			, ccc, card_buy_crystal);
			continue;
		}

		card_sell_crystal = strtol_safe(row[4], -5);
		if (card_sell_crystal < 0) {
			ERROR_PRINT(-12, "db_design_load_shop:card_sell_crystal %d %d"
			, ccc, card_sell_crystal);
			continue;
		}

		piece_buy_gold = strtol_safe(row[5], -5);
		if (piece_buy_gold < 0) {
			ERROR_PRINT(-12, "db_design_load_shop:piece_buy_gold %d %d"
			, ccc, piece_buy_gold);
			continue;
		}

		piece_sell_gold = strtol_safe(row[6], -5);
		if (piece_sell_gold < 0) {
			ERROR_PRINT(-12, "db_design_load_shop:piece_sell_gold %d %d"
			, ccc, piece_sell_gold);
			continue;
		}

		piece_buy_crystal = strtol_safe(row[7], -5);
		if (piece_buy_crystal < 0) {
			ERROR_PRINT(-12, "db_design_load_shop:piece_buy_crystal %d %d"
			, ccc, piece_buy_crystal);
			continue;
		}

		piece_sell_crystal = strtol_safe(row[8], -5);
		if (piece_sell_crystal < 0) {
			ERROR_PRINT(-12, "db_design_load_shop:piece_sell_crystal %d %d"
			, ccc, piece_sell_crystal);
			continue;
		}

		shop_list[ccc].card_id = ccc;
		shop_list[ccc].card_buy_gold = -card_buy_gold;
		shop_list[ccc].card_sell_gold = card_sell_gold;
		shop_list[ccc].card_buy_crystal = -card_buy_crystal;
		shop_list[ccc].card_sell_crystal = card_sell_crystal;
		shop_list[ccc].piece_buy_gold = -piece_buy_gold;
		shop_list[ccc].piece_sell_gold = piece_sell_gold;
		shop_list[ccc].piece_buy_crystal = -piece_buy_crystal;
		shop_list[ccc].piece_sell_crystal = piece_sell_crystal;

	}

	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;

}

#define SQL_AI	"SELECT * FROM design_ai ORDER BY id ASC"

// caller should ensure ai_list has MAX_AI_EID+1 capacity
// consider: pass the MYSQL *conn here, like dbio ?
int db_design_load_ai(ai_t * ai_list)
{
	int ret;
	int len;
	int id;
	int icon;
	int lv;
	int rating_flag;
	int pid;
	int win_gold;
	int win_exp;

	ai_t *pai;

	// clear the list first
	for (int i=0; i<=MAX_AI_EID; i++) {
		ai_list[i].id = 0;  // means it is empty
	}
	// note: it may have error during the reload, clear it is not the best
	// but anyway, we have to use this simple way to implement

	len = strlen(SQL_AI);
	ret = design_safe_mysql_real_query( SQL_AI, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_ai:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// id, icon, lv, rating, rating_flag, pid, win_gold, win_exp, alias, deck
	if (field_count != 10) {
		ret = -7;
		ERROR_PRINT(ret, "load_ai:field_count %d != 9", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_ai:empty_row %d", num_row);
		goto cleanup;
	}

	// loop all rows! : row[0]=id,  row[1]=deck (sprintf %.400s)
	while (NULL != (row = mysql_fetch_row(result))) {
		id = strtol_safe(row[0], -1);
		if (id <= 0 || id > MAX_AI_EID) {
			ret = -5;
			BUG_PRINT(-5, "load_ai:id_outbound %d", id);
			goto cleanup;
		}
		pai = ai_list + id;
		pai->id = id; // this is rather duplicate

		icon = strtol_safe(row[1], -1);
		if (icon < 0 || icon > EVIL_ICON_MAX) {
			ret = -15;
			BUG_PRINT(-15, "load_ai:icon_outbound %d", icon);
			goto cleanup;
		}
		pai->icon = icon;

		lv = strtol_safe(row[2], -1);
		if (lv < 0) {
			ret = -25;
			BUG_PRINT(-25, "load_ai:lv_outbound %d", lv);
			goto cleanup;
		}
		pai->lv = lv;

		sscanf(row[3], "%lf", &(pai->rating));
		if (pai->rating < 0) {
			ret = -35;
			BUG_PRINT(-35, "load_ai:lv_outbound %lf", pai->rating);
			goto cleanup;
		}

		rating_flag = strtol_safe(row[4], -1);
		if (rating_flag != 0 && rating_flag != 1) {
			ret = -45;
			BUG_PRINT(-45, "load_ai:rating_flag_outbound %d", rating_flag);
			goto cleanup;
		}
		pai->rating_flag = rating_flag;

		pid = strtol_safe(row[5], -1);
		if (pid < 0) {
			ret = -55;
			BUG_PRINT(-55, "load_ai:lv_outbound %d", pid);
			goto cleanup;
		}
		pai->pid = pid;

		win_gold = strtol_safe(row[6], -1);
		if (win_gold < 0) {
			ret = -65;
			BUG_PRINT(-65, "load_ai:win_gold_outbound %d", win_gold);
			goto cleanup;
		}
		pai->win_gold = win_gold;

		win_exp = strtol_safe(row[7], -1);
		if (win_exp < 0) {
			ret = -75;
			BUG_PRINT(-75, "load_ai:win_exp_outbound %d", win_exp);
			goto cleanup;
		}
		pai->win_exp = win_exp;

		sprintf(pai->alias, "%.30s", row[8]);	

		len = sprintf(pai->deck, "%.400s", row[9]);
		if (len != EVIL_CARD_MAX) {
			ret = -2;
			BUG_PRINT(-2, "load_ai:deck_len %d id=%d", len, id);
			goto cleanup;
			pai->id = 0; // clean it!
		}
	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


// first show all ptype=0, loc from 0 to 5 in this order, then ptype=1:
#define SQL_PICK	"SELECT * FROM design_pick ORDER BY ptype,loc ASC"

int db_design_load_pick(pick_t *pick_list)
{
	int ret;
	int len;
	int value;
	int error_count;
	pick_t *pick;

	// pick_list[0] = gold_pick
	// pick_list[1] = crystal_pick
	// clear the list first
	for (int i=0; i<MAX_PICK; i++) {
		bzero(pick_list+i, sizeof(pick_t));
	}

	len = strlen(SQL_PICK);
	ret = design_safe_mysql_real_query( SQL_PICK, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_pick:null_result");
	}

	// make sure num_rows = MAX_PICK * MAX_LOC (2 * 6 = 12)
	num_row = mysql_num_rows(result);
	if (num_row != MAX_PICK*MAX_LOC) {
		ret = -6;
		ERROR_PRINT(ret, "load_pick:num_row %d != %d", num_row, MAX_PICK*MAX_LOC);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// only id and deck
	if (field_count != 8) {
		ret = -7;
		ERROR_PRINT(ret, "load_pick:field_count %d != 8", field_count);
		goto cleanup;
	}


	// TODO need to fetch all rows in cleanup, else BUGGY ???
	error_count = 0;
	for (int p=0; p<MAX_PICK; p++) {
		pick = pick_list + p;  // assume ptype in order
		for (int loc=0; loc<MAX_LOC; loc++) {
			row = mysql_fetch_row(result);
			if (row==NULL) {
				ret = -13;
				ERROR_PRINT(ret, "load_pick:null_row pick=%d loc=%d", p, loc);
				goto cleanup;
			}
			// row[0]=ptype, [1]=loc, [2]=rate, [3]=star1, [4]=star2, [5]=star3
			// [6]=star4, [7]=star5

			// pick index check
			value = strtol_safe(row[0], -1);
			if (value != p) {
				ret = -16;  // logic error
				ERROR_PRINT(ret, "load_pick:pick_mismatch p=%d row[0]=%s", p, row[0]);
				goto cleanup;
			}

			// loc index check
			value = strtol_safe(row[1], -1);
			if (value != loc) {
				ret = -26;  // logic error
				ERROR_PRINT(ret, "load_pick:loc_mismatch loc=%d row[1]=%s", loc, row[1]);
				goto cleanup;
			}

			value = strtol_safe(row[2], -2);
			error_count += (value < 0) ? 1 : 0;
			ERROR_NEG_PRINT(value, "load_pick:rate_invalid [%d][%d] rate=%s"
			, p, loc, row[2]);
			pick->pick_rate[loc] = value;
			pick->batch_rate[loc][0] = strtol_safe(row[3], -3); //  star1
			pick->batch_rate[loc][1] = strtol_safe(row[4], -4); //  star1
			pick->batch_rate[loc][2] = strtol_safe(row[5], -5); //  star1
			pick->batch_rate[loc][3] = strtol_safe(row[6], -6); //  star1
			pick->batch_rate[loc][4] = strtol_safe(row[7], -7); //  star1
			for (int star=0; star<MAX_STAR; star++) {
				value = pick->batch_rate[loc][star];
				error_count += (value < 0) ? 1 : 0;
				ERROR_NEG_PRINT(value, "load_pick:star_invalid [%d][%d] star=%d v=%d"
				, p, loc, star, value);
			}
			
		} // for loc
	} // for p (pick index)

	ret = 0;
	if (error_count > 0) {
		ret = -36;
		ERROR_NEG_PRINT(ret, "load_pick:data_invalid error_count=%d", error_count);
	}
	
cleanup:
	// TODO what if we still have some result left ?  need to fetch ?
	mysql_free_result(result);
	return ret;
}


#define SQL_CONSTANT	"SELECT * FROM design_constant"

// consider: pass the MYSQL *conn here, like dbio ?
int db_design_load_constant(constant_t * cons)
{
	int ret;
	int len;
	int n;
	char *rptr;


	len = strlen(SQL_CONSTANT);
	ret = design_safe_mysql_real_query( SQL_CONSTANT, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_constant:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_constant:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// batch_refresh_gold, pick_gold
	// , batch_refresh_crystal, pick_crystal, max_timeout
	// , guild_bonus_rate
	// , create_guild_gold, create_guild_crystal
	// , lottery_one_gold, lottery_one_crystal
	// , lottery_ten_gold, lottery_ten_crystal
	// , first_pay_double, vip_increase_date
	// , fight_ai_win_gold, fight_ai_cost_gold, fight_ai_draw_gold
	// , fight_ai_fold_gold, fight_ai_win_exp, fight_ai_lose_exp
	// , fihgt_ai_draw_exp, fight_ai_fold_exp
	// , fight_pvp_win_gold, fight_pvp_cost_gold, fight_pvp_draw_gold
	// , fight_pvp_fold_gold, fight_pvp_gold_win_exp, fight_pvp_gold_lose_exp
	// , fihgt_pvp_gold_draw_exp, fight_pvp_gold_fold_exp
	// , fight_pvp_win_crystal, fight_pvp_cost_crystal
	// , fight_pvp_draw_crystal, fight_pvp_fold_crystal
	// , fight_pvp_crystal_win_exp, fight_pvp_crystal_lose_exp
	// , fihgt_pvp_crystal_draw_exp, fight_pvp_crystal_fold_exp
	// , fight_ai_free_win_gold, fight_ai_cost_gold, fight_ai_draw_gold
	// , fight_ai_free_fold_gold, fight_ai_win_exp, fight_ai_lose_exp
	// , fihgt_ai_free_draw_exp, fight_ai_fold_exp
	// , fight_pvp_free_win_gold, fight_pvp_cost_gold, fight_pvp_draw_gold
	// , fight_pvp_free_fold_gold, fight_pvp_win_exp, fight_pvp_lose_exp
	// , fihgt_pvp_free_draw_exp, fight_pvp_fold_exp, pshop_reset_interval
	// , pshop_refresh_gold, pshop_max_refresh_gold, exchange_crystal_gold
	// , first_vip_extra_gold, first_vip_extra_crystal, first_vip_extra_cards
	// , quick_robot_flag
	// , quick_win_exp, quick_lose_exp, quick_draw_exp, quick_fold_exp
	if (field_count != 66) {
		ret = -7;
		ERROR_PRINT(ret, "load_constant:field_count %d", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);
//	cons->win_quick_gold = strtol_safe(row[0], -1);
//	BUG_NEG_RETURN(cons->win_quick_gold, "load_constant:win_quick_gold_null");

//	cons->win_solo_gold = strtol_safe(row[1], -1);
//	BUG_NEG_RETURN(cons->win_solo_gold, "load_constant:win_solo_gold_null");

	cons->batch_refresh_gold = strtol_safe(row[0], -1);
	BUG_CLEANUP(cons->batch_refresh_gold < 0, -5, "load_constant:batch_refresh_gold_null");

	cons->pick_gold = strtol_safe(row[1], -1);
	BUG_CLEANUP(cons->pick_gold < 0, -5, "load_constant:pick_gold_null");

	cons->batch_refresh_crystal = strtol_safe(row[2], -1);
	BUG_CLEANUP(cons->batch_refresh_crystal < 0, -5, "load_constant:batch_refresh_crystal_null");

	cons->pick_crystal = strtol_safe(row[3], -1);
	BUG_CLEANUP(cons->pick_crystal < 0, -5, "load_constant:pick_crystal_null");

	cons->max_timeout = strtol_safe(row[4], 59); // force-next
	BUG_CLEANUP(cons->max_timeout < 0, -5, "load_constant:max_timeout");

	sscanf(row[5], "%lf", &(cons->guild_bonus_rate));

	cons->create_guild_gold = strtol_safe(row[6], 0); 
	BUG_CLEANUP(cons->create_guild_gold < 0, -5, "load_constant:create_guild_gold");

	cons->create_guild_crystal = strtol_safe(row[7], 0); 
	BUG_CLEANUP(cons->create_guild_crystal < 0, -5
	, "load_constant:create_guild_crystal");

	cons->lottery_one_gold = strtol_safe(row[8], 0); 
	BUG_CLEANUP(cons->lottery_one_gold < 0, -5, "load_constant:lottery_one_gold");

	cons->lottery_one_crystal = strtol_safe(row[9], 0); 
	BUG_CLEANUP(cons->lottery_one_crystal < 0, -5, "load_constant:lottery_one_crystal");

	cons->lottery_ten_gold = strtol_safe(row[10], 0); 
	BUG_CLEANUP(cons->lottery_ten_gold < 0, -5, "load_constant:lottery_ten_gold");

	cons->lottery_ten_crystal = strtol_safe(row[11], 0); 
	BUG_CLEANUP(cons->lottery_ten_crystal < 0, -5, "load_constant:lottery_ten_crystal");

	cons->first_pay_double = strtol_safe(row[12], 0);
	BUG_CLEANUP(cons->first_pay_double < 0, -5, "load_constant:first_pay_double");

	cons->monthly_increase_date = strtol_safe(row[13], 0);
	BUG_CLEANUP(cons->monthly_increase_date < 0, -5, "load_constant:monthly_increase_date");


	cons->win_gold[GAME_SOLO_GOLD] = strtol_safe(row[14], 0); 
	BUG_CLEANUP(cons->win_gold[GAME_SOLO_GOLD] < 0, -5, "load_constant:fight_ai_win_gold");

	cons->cost_gold[GAME_SOLO_GOLD] = strtol_safe(row[15], 0); 
	BUG_CLEANUP(cons->cost_gold[GAME_SOLO_GOLD] < 0, -5, "load_constant:fight_ai_cost_gold");

	cons->draw_gold[GAME_SOLO_GOLD] = strtol_safe(row[16], 0); 
	BUG_CLEANUP(cons->draw_gold[GAME_SOLO_GOLD] < 0, -5, "load_constant:fight_ai_draw_gold");

	cons->fold_gold[GAME_SOLO_GOLD] = strtol_safe(row[17], 0); 
	BUG_CLEANUP(cons->fold_gold[GAME_SOLO_GOLD] < 0, -5, "load_constant:fight_ai_fold_gold");

	cons->win_exp[GAME_SOLO_GOLD] = strtol_safe(row[18], 0); 
	BUG_CLEANUP(cons->win_exp[GAME_SOLO_GOLD] < 0, -5, "load_constant:fight_ai_win_exp");

	cons->lose_exp[GAME_SOLO_GOLD] = strtol_safe(row[19], 0); 
	BUG_CLEANUP(cons->lose_exp[GAME_SOLO_GOLD] < 0, -5, "load_constant:fight_ai_lose_exp");

	cons->draw_exp[GAME_SOLO_GOLD] = strtol_safe(row[20], 0); 
	BUG_CLEANUP(cons->draw_exp[GAME_SOLO_GOLD] < 0, -5, "load_constant:fight_ai_draw_exp");

	cons->fold_exp[GAME_SOLO_GOLD] = strtol_safe(row[21], 0); 
	BUG_CLEANUP(cons->fold_exp[GAME_SOLO_GOLD] < 0, -5, "load_constant:fight_ai_fold_exp");


	cons->win_gold[GAME_VS_GOLD] = strtol_safe(row[22], 0); 
	BUG_CLEANUP(cons->win_gold[GAME_VS_GOLD] < 0, -5, "load_constant:fight_pvp_win_gold");

	cons->cost_gold[GAME_VS_GOLD] = strtol_safe(row[23], 0); 
	BUG_CLEANUP(cons->cost_gold[GAME_VS_GOLD] < 0, -5, "load_constant:fight_pvp_cost_gold");

	cons->draw_gold[GAME_VS_GOLD] = strtol_safe(row[24], 0); 
	BUG_CLEANUP(cons->draw_gold[GAME_VS_GOLD] < 0, -5, "load_constant:fight_pvp_draw_gold");

	cons->fold_gold[GAME_VS_GOLD] = strtol_safe(row[25], 0); 
	BUG_CLEANUP(cons->fold_gold[GAME_VS_GOLD] < 0, -5, "load_constant:fight_pvp_fold_gold");

	cons->win_exp[GAME_VS_GOLD] = strtol_safe(row[26], 0); 
	BUG_CLEANUP(cons->win_exp[GAME_VS_GOLD] < 0, -5, "load_constant:fight_pvp_win_exp");

	cons->lose_exp[GAME_VS_GOLD] = strtol_safe(row[27], 0); 
	BUG_CLEANUP(cons->lose_exp[GAME_VS_GOLD] < 0, -5, "load_constant:fight_pvp_lose_exp");

	cons->draw_exp[GAME_VS_GOLD] = strtol_safe(row[28], 0); 
	BUG_CLEANUP(cons->draw_exp[GAME_VS_GOLD] < 0, -5, "load_constant:fight_pvp_draw_exp");

	cons->fold_exp[GAME_VS_GOLD] = strtol_safe(row[29], 0); 
	BUG_CLEANUP(cons->fold_exp[GAME_VS_GOLD] < 0, -5, "load_constant:fight_pvp_fold_exp");


	cons->win_crystal[GAME_VS_CRYSTAL] = strtol_safe(row[30], 0); 
	BUG_CLEANUP(cons->win_crystal[GAME_VS_CRYSTAL] < 0, -5, "load_constant:fight_pvp_win_crystal");

	cons->cost_crystal[GAME_VS_CRYSTAL] = strtol_safe(row[31], 0); 
	BUG_CLEANUP(cons->cost_crystal[GAME_VS_CRYSTAL] < 0, -5, "load_constant:fight_pvp_cost_crystal");

	cons->draw_crystal[GAME_VS_CRYSTAL] = strtol_safe(row[32], 0); 
	BUG_CLEANUP(cons->draw_crystal[GAME_VS_CRYSTAL] < 0, -5, "load_constant:fight_pvp_draw_crystal");

	cons->fold_crystal[GAME_VS_CRYSTAL] = strtol_safe(row[33], 0); 
	BUG_CLEANUP(cons->fold_crystal[GAME_VS_CRYSTAL] < 0, -5, "load_constant:fight_pvp_fold_crystal");

	cons->win_exp[GAME_VS_CRYSTAL] = strtol_safe(row[34], 0); 
	BUG_CLEANUP(cons->win_exp[GAME_VS_CRYSTAL] < 0, -5, "load_constant:fight_pvp_win_exp");

	cons->lose_exp[GAME_VS_CRYSTAL] = strtol_safe(row[35], 0); 
	BUG_CLEANUP(cons->lose_exp[GAME_VS_CRYSTAL] < 0, -5, "load_constant:fight_pvp_lose_exp");

	cons->draw_exp[GAME_VS_CRYSTAL] = strtol_safe(row[36], 0); 
	BUG_CLEANUP(cons->draw_exp[GAME_VS_CRYSTAL] < 0, -5, "load_constant:fight_pvp_draw_exp");

	cons->fold_exp[GAME_VS_CRYSTAL] = strtol_safe(row[37], 0); 
	BUG_CLEANUP(cons->fold_exp[GAME_VS_CRYSTAL] < 0, -5, "load_constant:fight_pvp_fold_exp");


	cons->win_gold[GAME_SOLO_FREE] = strtol_safe(row[38], 0);
	BUG_CLEANUP(cons->win_gold[GAME_SOLO_FREE] < 0, -5, "load_constant:fight_pvp_win_gold");

	cons->lose_gold[GAME_SOLO_FREE] = strtol_safe(row[39], 0);
	BUG_CLEANUP(cons->lose_gold[GAME_SOLO_FREE] < 0, -5, "load_constant:fight_pvp_lose_gold");

	cons->draw_gold[GAME_SOLO_FREE] = strtol_safe(row[40], 0);
	BUG_CLEANUP(cons->draw_gold[GAME_SOLO_FREE] < 0, -5, "load_constant:fight_pvp_draw_gold");

	cons->fold_gold[GAME_SOLO_FREE] = strtol_safe(row[41], 0);
	BUG_CLEANUP(cons->fold_gold[GAME_SOLO_FREE] < 0, -5, "load_constant:fight_ai_free_fold_gold");

	cons->win_exp[GAME_SOLO_FREE] = strtol_safe(row[42], 0);
	BUG_CLEANUP(cons->win_exp[GAME_SOLO_FREE] < 0, -5, "load_constant:fight_ai_free_win_exp");

	cons->lose_exp[GAME_SOLO_FREE] = strtol_safe(row[43], 0);
	BUG_CLEANUP(cons->lose_exp[GAME_SOLO_FREE] < 0, -5, "load_constant:fight_ai_free_lose_exp");

	cons->draw_exp[GAME_SOLO_FREE] = strtol_safe(row[44], 0);
	BUG_CLEANUP(cons->draw_exp[GAME_SOLO_FREE] < 0, -5, "load_constant:fight_ai_free_draw_exp");

	cons->fold_exp[GAME_SOLO_FREE] = strtol_safe(row[45], 0);
	BUG_CLEANUP(cons->fold_exp[GAME_SOLO_FREE] < 0, -5, "load_constant:fight_ai_free_fold_exp");


	cons->win_gold[GAME_VS_FREE] = strtol_safe(row[46], 0);
	BUG_CLEANUP(cons->win_gold[GAME_VS_FREE] < 0, -5, "load_constant:fight_player_free_win_gold");

	cons->lose_gold[GAME_VS_FREE] = strtol_safe(row[47], 0);
	BUG_CLEANUP(cons->lose_gold[GAME_VS_FREE] < 0, -5, "load_constant:fight_player_free_lose_gold");

	cons->draw_gold[GAME_VS_FREE] = strtol_safe(row[48], 0);
	BUG_CLEANUP(cons->draw_gold[GAME_VS_FREE] < 0, -5, "load_constant:fight_player_free_draw_gold");

	cons->fold_gold[GAME_VS_FREE] = strtol_safe(row[49], 0);
	BUG_CLEANUP(cons->fold_gold[GAME_VS_FREE] < 0, -5, "load_constant:fight_player_free_fold_gold");

	cons->win_exp[GAME_VS_FREE] = strtol_safe(row[50], 0);
	BUG_CLEANUP(cons->win_exp[GAME_VS_FREE] < 0, -5, "load_constant:fight_player_free_win_exp");

	cons->lose_exp[GAME_VS_FREE] = strtol_safe(row[51], 0);
	BUG_CLEANUP(cons->lose_exp[GAME_VS_FREE] < 0, -5, "load_constant:fight_player_free_lose_exp");

	cons->draw_exp[GAME_VS_FREE] = strtol_safe(row[52], 0);
	BUG_CLEANUP(cons->draw_exp[GAME_VS_FREE] < 0, -5, "load_constant:fight_player_free_draw_exp");

	cons->fold_exp[GAME_VS_FREE] = strtol_safe(row[53], 0);
	BUG_CLEANUP(cons->fold_exp[GAME_VS_FREE] < 0, -5, "load_constant:fight_player_free_fold_exp");

	cons->pshop_reset_interval = strtol_safe(row[54], 0);
	BUG_CLEANUP(cons->pshop_reset_interval < 10, -5, "load_constant:pshop_reset_interval_bug %d", cons->pshop_reset_interval);

	cons->pshop_refresh_gold = strtol_safe(row[55], 0);
	BUG_CLEANUP(cons->pshop_refresh_gold < 0, -5, "load_constant:pshop_refresh_gold");

	cons->pshop_max_refresh_gold = strtol_safe(row[56], 0);
	BUG_CLEANUP(cons->pshop_max_refresh_gold < 0, -5, "load_constant:pshop_max_refresh_gold");

	cons->exchange_crystal_gold = strtol_safe(row[57], 0);
	BUG_CLEANUP(cons->exchange_crystal_gold < 0, -5, "load_constant:exchange_crystal_gold");

	cons->first_vip_extra_gold = strtol_safe(row[58], 0);
	BUG_CLEANUP(cons->first_vip_extra_gold < 0, -5, "load_constant:first_vip_extra_gold");

	cons->first_vip_extra_crystal = strtol_safe(row[59], 0);
	BUG_CLEANUP(cons->first_vip_extra_crystal < 0, -5, "load_constant:first_vip_extra_crystal");

	rptr = row[60];
	cons->first_vip_extra_card_kind = 0;
	for (int i = 0; i < MAX_FIRST_VIP_CARD_KIND; i++)
	{
		ret = sscanf(rptr, "%d %d %n", &(cons->first_vip_extra_card_list[i][0])
		, &(cons->first_vip_extra_card_list[i][1]), &n);
		if (ret != 2) {
			break;
		}
		if (cons->first_vip_extra_card_list[i][0] <= 0
		|| cons->first_vip_extra_card_list[i][0] > EVIL_CARD_MAX
		|| cons->first_vip_extra_card_list[i][1] <= 0)
		{
			BUG_CLEANUP(1, -5
			, "load_constant:first_vip_extra_card_list %d[%d]"
			, cons->first_vip_extra_card_list[i][0]
			, cons->first_vip_extra_card_list[i][1]);
		}
		cons->first_vip_extra_card_kind++;
		rptr += n;
	}

	cons->quick_robot_flag = strtol_safe(row[61], 0);
	BUG_CLEANUP(cons->quick_robot_flag < 0, -5, "load_constant:quick_robot_flag");

	cons->win_exp[GAME_QUICK] = strtol_safe(row[62], 0);
	BUG_CLEANUP(cons->win_exp[GAME_QUICK] < 0, -5, "load_constant:quick_win_exp");

	cons->lose_exp[GAME_QUICK] = strtol_safe(row[63], 0);
	BUG_CLEANUP(cons->lose_exp[GAME_QUICK] < 0, -5, "load_constant:quick_lose_exp");

	cons->draw_exp[GAME_QUICK] = strtol_safe(row[64], 0);
	BUG_CLEANUP(cons->draw_exp[GAME_QUICK] < 0, -5, "load_constant:quick_draw_exp");

	cons->fold_exp[GAME_QUICK] = strtol_safe(row[65], 0);
	BUG_CLEANUP(cons->fold_exp[GAME_QUICK] < 0, -5, "load_constant:quick_fold_exp");

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_EXP	"SELECT * FROM design_exp ORDER BY lv ASC"

//int db_design_load_exp(char *ptr)
int db_design_load_exp(int *exp_list)
{
	int ret;
	int len;
	int lv;
	int exp;
	int count;


	len = strlen(SQL_EXP);
	ret = design_safe_mysql_real_query( SQL_EXP, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_exp:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_exp:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// lv, exp
	if (field_count != 2) {
		ret = -7;
		ERROR_PRINT(ret, "load_exp:field_count %d != 2", field_count);
		goto cleanup;
	}

	count = 0;
//	ptr += sprintf(ptr, "%d", num_row);
	while(NULL != (row = mysql_fetch_row(result))) {
		count++;

		if (count > MAX_LEVEL) {
			ret = -16;
			BUG_PRINT(-16, "load_exp:max_level_out_bound %d %d"
			, count, MAX_LEVEL);
			goto cleanup;
		}

		lv = strtol_safe(row[0], -1);
		if (lv < 0) {
			ret = -5;
			BUG_PRINT(lv, "load_exp:lv_null");
			goto cleanup;
		}

		exp = strtol_safe(row[1], -1);
		if (exp < 0) {
			ret = -5;
			BUG_PRINT(exp, "load_exp:exp_null");
			goto cleanup;
		}

		if (lv != count) {
			ret = -16;
			BUG_PRINT(-16, "load_exp:lv_missing %d %d ", lv, count);
			goto cleanup;
		}

//		ret = sprintf(ptr, " %d", exp);
//		ptr += ret;
		exp_list[count] = exp;
	}

	ret = count;	// ret is max level
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_LOAD_NOTICE	"SELECT UNIX_TIMESTAMP(create_date), title, notice FROM design_notice WHERE create_date <= NOW() ORDER BY create_date DESC"

// consider: pass the MYSQL *conn here, like dbio ?
int db_design_load_notice(notice_t * notice_list)
{
	int ret;
	int len;
	char title_fmt[15];
	char notice_fmt[15];
	char title[MAX_TITLE_SIZE+1];
	char note[MAX_NOTICE_SIZE+1];

	notice_t *pnotice;
	int count = 0;

	len = strlen(SQL_LOAD_NOTICE);
	ret = design_safe_mysql_real_query( SQL_LOAD_NOTICE, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_notice:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;
		WARN_PRINT(ret, "load_notice:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// create_date, title, note
	if (field_count != 3) {
		ret = -7;
		ERROR_PRINT(ret, "load_notice:field_count %d", field_count);
		goto cleanup;
	}

	sprintf(title_fmt, "%%.%ds", MAX_TITLE_SIZE);
	sprintf(notice_fmt, "%%.%ds", MAX_NOTICE_SIZE);
	
	while ( NULL != (row = mysql_fetch_row(result)) ) {

		bzero(title, MAX_TITLE_SIZE+1);
		bzero(note, MAX_NOTICE_SIZE+1);

		if (row[1] == NULL) {
			WARN_PRINT(-15, "load_notice:no_title");
			continue;
		}
		sprintf(title, title_fmt, row[1]);
		if (title[0] == '\0') {
			WARN_PRINT(-25, "load_notice:no_title");
			continue;
		}


		if (row[2] == NULL) {
			WARN_PRINT(-35, "load_notice:no_notice");
			continue;
		}
		sprintf(note, notice_fmt, row[2]);
		if (note[0] == '\0') {
			WARN_PRINT(-45, "load_notice:no_notice");
			continue;
		}

		count++;
		pnotice = notice_list + count;

		for ( size_t i = 0; i < strlen(note); i++) {
			if (note[i] == '\n' || note[i] == '\r') {
				note[i] = '^';
			}
			if (note[i] == '\0') {
				break;
			}
		}

		strcpy(pnotice->title, title);
		strcpy(pnotice->note, note);



		// printf("load_notice:title=%s, note=%s\n"
		// , pnotice->title, pnotice->note);

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_LOAD_STANDARD_DECK	"SELECT * FROM design_std_deck"

// consider: pass the MYSQL *conn here, like dbio ?
int db_design_load_std_deck(std_deck_t *sdeck_list)
{
	int ret;
	int len;
	int id;
	std_deck_t *pdeck;

	len = strlen(SQL_LOAD_STANDARD_DECK);
	ret = design_safe_mysql_real_query( SQL_LOAD_STANDARD_DECK, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_std_card:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// id, deck
	if (field_count != 2) {
		ret = -7;
		ERROR_PRINT(ret, "load_std_card:field_count %d != 2", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;
		WARN_PRINT(ret, "load_std_card:empty_row %d", num_row);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {
		id = strtol_safe(row[0], -1);
		BUG_NEG_RETURN(id, "load_std_deck:id_null");
		pdeck = sdeck_list + id;
		pdeck->id = id;
		sprintf(pdeck->deck, "%.400s", row[1]);
	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}



#define SQL_DESIGN_GUILD "SELECT * FROM design_guild ORDER BY lv ASC"

int db_design_load_guild(design_guild_t * design_guild_list)
{
	int ret;
	int len;
	int lv;
	int consume_gold;
	int levelup_gold;
	int member_max;
	int count;

	design_guild_t *pguild;


	len = strlen(SQL_DESIGN_GUILD);
	ret = design_safe_mysql_real_query( SQL_DESIGN_GUILD, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_guild:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_design_guild:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// lv, gold, levelup_gold, member_max
	if (field_count != 4) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_guild:field_count %d ", field_count);
		goto cleanup;
	}

	count = 0;
	while(NULL != (row = mysql_fetch_row(result))) {
		count++;

		lv = strtol_safe(row[0], -1);
		BUG_NEG_RETURN(lv, "load_design_guild:lv_null");
		pguild = design_guild_list + lv;
		pguild->lv = lv;

		consume_gold = strtol_safe(row[1], -1);
		BUG_NEG_RETURN(consume_gold, "load_design_guild:gold_null");
		pguild->consume_gold = consume_gold;

		levelup_gold = strtol_safe(row[2], -1);
		BUG_NEG_RETURN(levelup_gold, "load_design_guild:levelup_gold_null");
		pguild->levelup_gold = levelup_gold;

		member_max = strtol_safe(row[3], -1);
		BUG_NEG_RETURN(member_max, "load_design_guild:member_max_null");
		pguild->member_max = member_max;

		if (lv != count) {
			BUG_NEG_RETURN(-16, "load_design_guild:lv_missing %d %d ", lv, count);
		}

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}



#define SQL_DESIGN_MERGE "SELECT * FROM design_merge ORDER BY card_id ASC"

int db_design_load_merge(design_merge_t * design_merge_list)
{
	int ret;
	int len;
	int card_id;
	int count;
	int gold;
	int crystal;
	int row_count;

	design_merge_t *pmerge;

	len = strlen(SQL_DESIGN_MERGE);
	ret = design_safe_mysql_real_query( SQL_DESIGN_MERGE, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_merge:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_merge:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// card_id, count, gold, crystal
	if (field_count != 4) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_merge:field_count %d ", field_count);
		goto cleanup;
	}

	row_count = 0;
	while(NULL != (row = mysql_fetch_row(result))) {
		row_count++;

		card_id = strtol_safe(row[0], -1);
		if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
			BUG_RETURN(-5, "load_design_merge:card_id_bug %d", card_id);
		}
		pmerge = design_merge_list + card_id;
		pmerge->card_id = card_id;

		count = strtol_safe(row[1], -1);
		if (count < 0 || count > EVIL_NUM_PIECE_MAX) {
			BUG_RETURN(-15, "load_design_merge:count_bug %d", count);
		}
		pmerge->count = count;

		gold = strtol_safe(row[2], -1);
		if (gold < 0) {
			BUG_RETURN(-25, "load_design_merge:gold_bug %d", gold);
		}
		pmerge->gold = gold;

		crystal = strtol_safe(row[3], -1);
		if (crystal < 0) {
			BUG_RETURN(-35, "load_design_merge:crystal_bug %d", crystal);
		}
		pmerge->crystal = crystal;
	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_DESIGN_PAY "SELECT * FROM design_pay ORDER BY pay_code ASC"

int db_design_load_pay(design_pay_t * design_pay_list)
{
	int ret;
	int len;
	int count;

	int pay_id;
	int pay_code;
	int channel;
	int price;
	int money_type;
	int money;

	char title_fmt[15];
	char description_fmt[15];

	design_pay_t *ppay;

	len = strlen(SQL_DESIGN_PAY);
	ret = design_safe_mysql_real_query( SQL_DESIGN_PAY, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_pay:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_pay:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// pay_id, pay_code, channel, price, money_type, money, title, description
	if (field_count != 8) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_pay:field_count %d ", field_count);
		goto cleanup;
	}

	sprintf(title_fmt, "%%.%ds", MAX_PAY_DESCRIPTION_SIZE);
	sprintf(description_fmt, "%%.%ds", MAX_PAY_DESCRIPTION_SIZE);
	count = 0;
	while(NULL != (row = mysql_fetch_row(result))) {
		count++;

		pay_id = strtol_safe(row[0], -1);
		if (pay_id >= MAX_PAY_NUM || pay_id <= 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_pay:pay_id_bug %d", pay_id);
			goto cleanup;
		}
		ppay = design_pay_list + pay_id;
		ppay->pay_id = pay_id;
	
		pay_code = strtol_safe(row[1], -1);
		if (pay_code <= 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_pay:pay_code_bug %d", pay_code);
			goto cleanup;
		}
		ppay->pay_code = pay_code;

		channel = strtol_safe(row[2], -1);
		if (channel <= 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_pay:channel %d", channel);
			goto cleanup;
		}
		ppay->channel = channel;

		price = strtol_safe(row[3], -1);
		if (price < 0) {
			ret = -35;
			BUG_PRINT(-35, "load_ai:price %d", price);
			goto cleanup;
		}
		ppay->price = price;

		money_type = strtol_safe(row[4], -1);
		if (money_type != 0 && money_type != 1) {
			ret = -15;
			BUG_PRINT(-15, "load_design_pay:money_type %d", money_type);
			goto cleanup;
		}
		ppay->money_type = money_type;

		money = strtol_safe(row[5], -1);
		if (money <= 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_pay:money %d", money);
			goto cleanup;
		}
		ppay->money = money;

		sprintf(ppay->title, description_fmt, row[6]);
		if (ppay->title[0] == '\0') {
			WARN_PRINT(-45, "load_design_pay:no_title");
			sprintf(ppay->title, "-");
		}

		sprintf(ppay->description, description_fmt, row[7]);
		if (ppay->description[0] == '\0') {
			WARN_PRINT(-45, "load_design_pay:no_description");
			sprintf(ppay->description, "-");
		}

		// DEBUG_PRINT(0, "load_pay:pay_id=%d pay_code=%d channel=%d price=%d money_type=%d money=%d title=%s description=%s"
		// , ppay->pay_id, ppay->pay_code, ppay->channel, ppay->price, ppay->money_type, ppay->money, ppay->title, ppay->description);
	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_DESIGN_VERSION	"SELECT id, game_version, client_version FROM design_version WHERE id = 1 LIMIT 1"
int db_design_load_version(design_version_t * pversion)
{
	int ret;
	int len;

	int id;
	int game_version;
	int client_version;

	len = strlen(SQL_DESIGN_VERSION);
	ret = design_safe_mysql_real_query( SQL_DESIGN_VERSION, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_version:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_version:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// id, game_version, client_version
	if (field_count != 3) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_version:field_count %d ", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);

	id = strtol_safe(row[0], -1);
	if (id != 1) {
		ret = -5;
		BUG_PRINT(-5, "load_design_version:id %d", id);
		goto cleanup;
	}
	pversion->id = id;

	game_version = strtol_safe(row[1], -1);
	if (game_version < 0) {
		ret = -15;
		BUG_PRINT(game_version, "load_design_version:game_version %d", game_version);
		goto cleanup;
	}
	pversion->game_version = game_version;

	client_version = strtol_safe(row[2], -1);
	if (client_version < 0) {
		ret = -25;
		BUG_PRINT(client_version, "load_design_version:client_version %d", client_version);
		goto cleanup;
	}
	pversion->client_version = client_version;

	// DEBUG_PRINT(0, "load_design_version:id=%d game_version=%d client_version=%d", pversion->id, pversion->game_version, pversion->client_version);

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_DESIGN_WEBSITE "SELECT device_id, website FROM design_website ORDER BY device_id ASC"

int db_design_load_website(design_website_t * design_website_list)
{
	int ret;
	int len;

	int device_id;

	design_website_t *pwebsite;

	len = strlen(SQL_DESIGN_WEBSITE);
	ret = design_safe_mysql_real_query( SQL_DESIGN_WEBSITE, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_website:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_website:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// device_id, website
	if (field_count != 2) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_website:field_count %d ", field_count);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {

		device_id = strtol_safe(row[0], -1);
		if (device_id < 0 || device_id >= MAX_WEBSITE_NUM) {
			ret = -5;
			BUG_PRINT(-5, "load_design_website:device_id %d", device_id);
			goto cleanup;
		}
		pwebsite = design_website_list + device_id;
		pwebsite->device_id = device_id;

		sprintf(pwebsite->website, "%.300s", row[1]);
		if (pwebsite->website[0] == '\0') {
			WARN_PRINT(-45, "load_design_website:no_website");
			sprintf(pwebsite->website, "_");
		}

		// DEBUG_PRINT(0, "load_design_website:device_id=%d website=%s"
		// , pwebsite->device_id, pwebsite->website);

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_DESIGN_FIGHT_SCHEDULE	"SELECT game_type,time_to_sec(open_time),time_to_sec(close_time) FROM design_fight_schedule ORDER BY game_type ASC"
int db_design_load_fight_schedule(design_fight_schedule_t * design_fight_schedule_list, int &max_fight_schedule)
{
	int ret;
	int len;

	int game_type;
	time_t tmp_time;
	design_fight_schedule_t *fight_schedule;

	len = strlen(SQL_DESIGN_FIGHT_SCHEDULE);
	ret = design_safe_mysql_real_query( SQL_DESIGN_FIGHT_SCHEDULE, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_fight_schedule:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_fight_schedule:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// device_id, website
	if (field_count != 3) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_fight_schedule:field_count %d ", field_count);
		goto cleanup;
	}

	max_fight_schedule = 0;
	while(NULL != (row = mysql_fetch_row(result))) {

		game_type = strtol_safe(row[0], -1);
//		if (game_type != GAME_SOLO_GOLD || game_type != GAME_VS_GOLD
//		|| game_type != GAME_VS_CRYSTAL || game_type != GAME_SOLO_FREE
//		|| game_type != GAME_VS_FREE)
		if (game_type < 8 || game_type > 12) {
			ret = -5;
			BUG_PRINT(-5, "load_design_fight_schedule:game_type %d", game_type);
			goto cleanup;
		}

		fight_schedule = design_fight_schedule_list + max_fight_schedule;
		fight_schedule->game_type = game_type;

		tmp_time = strtolong_safe(row[1], -1);
		if (tmp_time < 0 || tmp_time > 24 * 60 * 60) {
			ret = -15;
			BUG_PRINT(-15, "load_design_fight_schedule:open_time %ld", tmp_time);
			goto cleanup;
		}
		fight_schedule->open_time = tmp_time;

		tmp_time = strtolong_safe(row[2], -1);
		if (tmp_time < 0 || tmp_time > 24 * 60 * 60) {
			ret = -25;
			BUG_PRINT(-25, "load_design_fight_schedule:close_time %ld", tmp_time);
			goto cleanup;
		}
		fight_schedule->close_time = tmp_time;

		max_fight_schedule++;
	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define DESIGN_LOAD_MONTHLY "SELECT * FROM design_monthly WHERE pay_code IN (SELECT pay_code FROM design_pay)"

int db_design_load_monthly(int *pay_code_list, int &max_count)
{
	int ret;
	int len;
	int pay_code;

	max_count = 0;

	len = strlen(DESIGN_LOAD_MONTHLY);

	ret = design_safe_mysql_real_query( DESIGN_LOAD_MONTHLY, len);
	if (ret != 0) {
		ERROR_RETURN(-55, "db_design_load_monthly:design_safe_mysql_real_query %d"
		, ret); 
		return -55; // safety, should never run
	}

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result==NULL) {
		ERROR_RETURN(-3, "db_design_load_monthly:null result");
	}

	num_row = mysql_num_rows(result);
	if (num_row<=0) {
		ret = 0;	// normal return
		WARN_PRINT(ret, "db_design_load_monthly:empty row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// pay_code
	if (field_count != 1) { 
		ret = -7;
		ERROR_PRINT(ret, "db_design_load_monthly:field_count %d != 1"
		, field_count);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {

		pay_code = strtol_safe(row[0], -5);
		if (pay_code < 0) {
			ERROR_PRINT(-2, "load_monthly:pay_code out bound %d", pay_code);
			continue;
		}
		pay_code_list[max_count++] = pay_code;

	}

	ret = 0;

cleanup:
	mysql_free_result(result);
	return ret;

}





// tested in sandbox/str.c
// character to character replace
// [from] : character array end with \0, should be same length as [to]
// Usage:
// str = "Win AI 3 time\nDo it everyday";
// from=" \n\r";  // both \r and \n convert to ^
// to  ="_^^";
// str_replace(sr, from, to)
int str_replace(char * str, const char *from, const char *to)
{
	size_t toklen;
	size_t len;
	// no null check on str, from, to ?
	toklen = strlen(from);
	if (toklen != strlen(to)) {
		ERROR_RETURN(-2, "str_replace:from to len not match %s", str);
	}
	len = strlen(str);
	for (size_t i=0; i<len; i++) {
		char ch = str[i];
		for (size_t t=0; t<toklen; t++) {
			if (ch == from[t]) {
				str[i] = to[t];
				break; // only one
			}
		}
	}

	return 0;
}

// peter: 2014-10-26 update the design.design_mission (+reset_time)
#define SQL_DESIGN_MISSION "SELECT * FROM design_mission ORDER BY mid DESC"
int db_design_load_mission(design_mission_t *mission_list)
{
	int ret;
	int len;
	int n;
	char *tptr;
	design_mission_t mis;

	len = strlen(SQL_DESIGN_MISSION);
	ret = design_safe_mysql_real_query( SQL_DESIGN_MISSION, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_mission:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_mission:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// mid, pre, lv, hero, daily, guild_lv, mtype, p1, p2, p3
	// reward_exp, reward_gold, reward_crystal
	// reward_power, reward_card, reward_piece
	// mtext	
	if (field_count != 18) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_mission:field_count %d ", field_count);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {

		bzero(&mis, sizeof(mis));
		mis.mid = strtol_safe(row[0], -1);
		if (mis.mid <= 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_mission:mid %d", mis.mid);
			goto cleanup;
		}
		if (mis.mid >= MAX_MISSION) {
			ret = -2;
			BUG_PRINT(-2, "load_design_mission:mid_out_of_bound %d", mis.mid);
			goto cleanup;
		}

		mis.pre 	= strtol_safe(row[1], -1);
		mis.lv 		= strtol_safe(row[2], -1);
		mis.hero 	= strtol_safe(row[3], -1);
		mis.daily 	= strtol_safe(row[4], -1);
		mis.guild_lv 	= strtol_safe(row[5], -1);
		mis.mtype 	= strtol_safe(row[6], -1);
		mis.p1 		= strtol_safe(row[7], -1);
		mis.p2 		= strtol_safe(row[8], -1);
		mis.p3 		= strtol_safe(row[9], -1);

		mis.reward_exp 		= strtol_safe(row[10], -1);
		mis.reward_gold		= strtol_safe(row[11], -1);
		mis.reward_crystal	= strtol_safe(row[12], -1);
		mis.reward_power	= strtol_safe(row[13], -1);

		tptr = row[14];
		for (int cdx = 0; cdx < MAX_MISSION_REWARD_CARD; cdx++) {
			ret = sscanf(tptr, "%d %n", &(mis.reward_card[cdx]), &n);
			if (ret != 1 || mis.reward_card[cdx] == 0) {
				break;
			}
			if (mis.reward_card[cdx] < 0 || mis.reward_card[cdx] > EVIL_CARD_MAX)
			{
				ret = -mis.mid;
				BUG_PRINT(ret, "load_design_mission:reward_card_out_of_bound %d"
				, mis.reward_card[cdx]);
				goto cleanup;
			}
			mis.card_count++;
			tptr += n;
		}
		
		tptr = row[15];
		for (int pdx = 0; pdx < MAX_MISSION_REWARD_PIECE; pdx++) {
			ret = sscanf(tptr, "%d %d %n", &(mis.reward_piece[pdx][0])
			, &(mis.reward_piece[pdx][1]), &n);
			if (ret != 2) {
				break;
			}
			if (mis.reward_piece[pdx][0] <= 0
			|| mis.reward_piece[pdx][0] > EVIL_CARD_MAX)
			{
				ret = -mis.mid;
				BUG_PRINT(ret, "load_design_mission:reward_piece_out_of_bound %d"
				, mis.reward_piece[pdx][0]);
				goto cleanup;
			}
			if (mis.reward_piece[pdx][1] <= 0
			|| mis.reward_piece[pdx][1] > EVIL_NUM_PIECE_MAX)
			{
				ret = -mis.mid;
				BUG_PRINT(ret
				, "load_design_mission:reward_piece_count_out_of_bound %d"
				, mis.reward_piece[pdx][1]);
				goto cleanup;
			}
			mis.piece_count++;
			tptr += n;
		}
		
		sprintf(mis.reset_time, "%.5s", row[16]+11); // 0000-00-00 00:00:00
		sprintf(mis.mtext, "%.300s", row[17]);
		str_replace(mis.mtext, " \r\n", "_^^");

		if (mis.daily != 0 && mis.daily != 1) {
			ret = -55;
			BUG_PRINT(-55, "load_design_mission:daily_error mid=%d", mis.mid);
			goto cleanup;
		}
		if (mis.guild_lv < 0) {
			ret = -65;
			BUG_PRINT(-65, "load_design_mission:guild_lv_error mid=%d", mis.mid);
			goto cleanup;
		}
		if (mis.mtype <= 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_mission:mtype<=0 mid=%d", mis.mid);
			goto cleanup;
		}
		if (mis.p1 <= 0) {
			ret = -25;
			BUG_PRINT(-25, "load_design_mission:p1<=0 mid=%d", mis.mid);
			goto cleanup;
		}

		if (mis.reset_time[0] == '\0') {
			ret = -35;
			BUG_PRINT(-35, "load_design_mission:reset_time=null mid=%d", mis.mid);
			goto cleanup;
		}

		if (mis.mtext[0] == '\0') {
			WARN_PRINT(-45, "load_design_mission:no_mtext %d", mis.mid);
			sprintf(mis.mtext, "_");
		}

		// mission_list.push_back(mis);
		mission_list[mis.mid] = mis;

		// DEBUG_PRINT(0, "load_design_mission:mid=%d pre=%d lv=%d hero=%d daily=%d mtype=%d p1=%d p2=%d p3=%d reward_card=%d exp=%d gold=%d crystal=%d mtext=%s", mis.mid, mis.pre, mis.lv, mis.hero, mis.daily
		// , mis.mtype, mis.p1, mis.p2, mis.p3
		// , mis.reward_card, mis.reward_exp, mis.reward_gold
		// , mis.reward_crystal, mis.mtext);

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}



#define SQL_DESIGN_SLOT "SELECT * FROM design_slot ORDER BY id ASC"
int db_design_load_slot(design_slot_t * design_slot_list)
{
	int ret;
	int len;
	
	int id;
	design_slot_t * pslot;

	len = strlen(SQL_DESIGN_SLOT);
	ret = design_safe_mysql_real_query( SQL_DESIGN_SLOT, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_slot:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_slot:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// id, gold, crystal
	if (field_count != 3) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_slot:field_count %d ", field_count);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {

		id = strtol_safe(row[0], -1);
		if (id <= 0 || id >= MAX_SLOT_NUM) {
			ret = -5;
			BUG_PRINT(-5, "load_design_slot:id %d", id);
			goto cleanup;
		}

		pslot = design_slot_list + id;
		pslot->id = id;
		pslot->gold = strtol_safe(row[1], -1);
		if (pslot->gold < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_slot:gold %d", pslot->gold);
			goto cleanup;
		}
		pslot->crystal = strtol_safe(row[2], -1);
		if (pslot->crystal < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_slot:crystal %d", pslot->crystal);
			goto cleanup;
		}

		// DEBUG_PRINT(0, "load_design_slot:id=%d gold=%d crystal=%d"
		// , pslot->id, pslot->gold, pslot->crystal);

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_DESIGN_LOAD_MATCH "SELECT match_id, title, max_player, UNIX_TIMESTAMP(start_time), t1, t2, t3, t4, status, round FROM evil_base.evil_match WHERE status=1 OR status=2 ORDER BY match_id DESC LIMIT 1"
int db_design_load_match(match_t * match)
{
	int ret;
	int len;
	
	len = strlen(SQL_DESIGN_LOAD_MATCH);
	ret = design_safe_mysql_real_query( SQL_DESIGN_LOAD_MATCH, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_match:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		WARN_PRINT(-6, "load_match:num_row<0 %d", num_row);
		goto cleanup;
	}
	if (num_row == 0) {
		ret = 0;
		DEBUG_PRINT(ret, "load_match:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	if (field_count != 10) {
		ret = -7;
		ERROR_PRINT(ret, "load_match:field_count %d ", field_count);
		goto cleanup;
	}

	row = mysql_fetch_row(result);

	match->match_id = strtolong_safe(row[0], -5);
	sprintf(match->title, "%s", row[1]);
	match->max_player = strtol_safe(row[2], -5);
	match->max_team = match->max_player / MAX_TEAM_PLAYER;
	match->start_time = strtolong_safe(row[3], -5);
	match->round_time_list[0] = strtol_safe(row[4], -5);
	match->round_time_list[1] = strtol_safe(row[5], -5);
	match->round_time_list[2] = strtol_safe(row[6], -5);
	match->round_time_list[3] = strtol_safe(row[7], -5);
	match->status = strtol_safe(row[8], -5);
	match->round = strtol_safe(row[9], -5);

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_DESIGN_LOAD_MATCH_PLAYER "SELECT * FROM evil_base.evil_match_player WHERE match_id = %ld ORDER BY round ASC, team_id ASC, tid DESC"
int db_design_load_match_player(match_t * match)
{
	int ret;
	int len;
	int num_team = 0;
	int num_eli = 0;
	int round = 0;

	char query[300];
	sprintf(query, SQL_DESIGN_LOAD_MATCH_PLAYER, match->match_id);
	
	len = strlen(query);
	ret = design_safe_mysql_real_query( query, len);

	printf("query=%s\n", query);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_match_player:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_match_player:negative_row %d", num_row);
		goto cleanup;
	}
	if (num_row == 0) {
		ret = 0;
		INFO_PRINT(ret, "load_match_player:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// match_id, eid, round, team_id, win, lose, draw, tid, point, icon, alias
	if (field_count != 11) {
		ret = -7;
		ERROR_PRINT(ret, "load_match_player:field_count %d ", field_count);
		goto cleanup;
	}

	match_player_t * player;
	while(NULL != (row = mysql_fetch_row(result))) {

		round = strtol_safe(row[2], -1);
		if (round <= MAX_TEAM_ROUND) {
			player = match->player_list + num_team;
			num_team ++;
		} else {
			player = match->e_player_list + num_eli;
			num_eli ++;
		}

		player->match_id = strtolong_safe(row[0], -1);
		player->eid = strtol_safe(row[1], -1);
		player->round = strtol_safe(row[2], -1);
		player->team_id = strtol_safe(row[3], -1);
		player->win = strtol_safe(row[4], -1);
		player->lose = strtol_safe(row[5], -1);
		player->draw = strtol_safe(row[6], -1);
		player->tid = strtol_safe(row[7], -1);
		player->point = strtol_safe(row[8], -1);
		player->icon = strtol_safe(row[9], -1);
		sprintf(player->alias, "%s", row[10]);

		// DEBUG_PRINT(0, "load_match_player:%ld %d %d %d %d %d %d %d %d %s", player->match_id, player->eid, player->round, player->team_id, player->win, player->lose, player->draw, player->tid, player->point, player->alias);

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_DESIGN_RANK_REWARD "SELECT id,start_num,end_num,gold,crystal,title,message FROM design_rank_reward ORDER BY id ASC"
int db_design_load_rank_reward(design_rank_reward_t * rank_reward_list)
{
	int ret;
	int len;
	int num;
	
	int id;

	design_rank_reward_t * preward;

	len = strlen(SQL_DESIGN_RANK_REWARD);
	ret = design_safe_mysql_real_query( SQL_DESIGN_RANK_REWARD, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_rank_reward:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_rank_reward:neg_row %d", num_row);
		goto cleanup;
	}
	if (num_row == 0) {
		ret = 0;
		WARN_PRINT(-3, "load_design_rank_reward:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// id, start_num, end_num, gold, crystal, title, message
	if (field_count != 7) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_rank_reward:field_count %d ", field_count);
		goto cleanup;
	}

	num = 0;
	while(NULL != (row = mysql_fetch_row(result))) {

		id = strtol_safe(row[0], -1);
		if (id <= 0 || id >= MAX_RANK_REWARD) {
			ret = -5;
			BUG_PRINT(-5, "load_design_rank_reward:id_out_bound %d", id);
			goto cleanup;
		}

		if (++num != id) {
			ret = -15;
			BUG_PRINT(-15, "load_design_rank_reward:id_mismatch %d %d", id, num);
			goto cleanup;
		}

		preward = rank_reward_list + id;
		preward->id = id;

		preward->start = strtol_safe(row[1], -1);
		if (preward->start < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_rank_reward:start %d", preward->id);
			goto cleanup;
		}

		preward->end = strtol_safe(row[2], -1);
		if (preward->end < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_rank_reward:end %d", preward->id);
			goto cleanup;
		}

		preward->gold = strtol_safe(row[3], -1);
		if (preward->gold < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_rank_reward:gold %d", preward->id);
			goto cleanup;
		}

		preward->crystal = strtol_safe(row[4], -1);
		if (preward->crystal < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_rank_reward:crystal %d", preward->id);
			goto cleanup;
		}

		sprintf(preward->title, "%s", row[5]);
		if (preward->title[0] == '\0') {
			ret = -5;
			BUG_PRINT(-5, "load_design_rank_reward:title_null %d", preward->id);
			goto cleanup;
		}

		sprintf(preward->message, "%s", row[6]);
		if (preward->message[0] == '\0') {
			ret = -5;
			BUG_PRINT(-5, "load_design_rank_reward:message_null %d", preward->id);
			goto cleanup;
		}
		

		/*
		DEBUG_PRINT(0, "load_design_rank_reward:id=%d start=%d end=%d gold=%d crystal=%d title=%s message=%s"
		, preward->id, preward->start, preward->end, preward->gold, preward->crystal, preward->title, preward->message);
		*/

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_DESIGN_RANK_TIME "SELECT id,time FROM design_rank_time ORDER BY id ASC"
int db_design_load_rank_time(design_rank_time_t * rank_time_list)
{
	int ret;
	int len;
	
	int id;
	int num;

	design_rank_time_t * ptime;

	len = strlen(SQL_DESIGN_RANK_TIME);
	ret = design_safe_mysql_real_query( SQL_DESIGN_RANK_TIME, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_rank_time:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_rank_time:neg_row %d", num_row);
		goto cleanup;
	}
	if (num_row == 0) {
		ret = 0;
		WARN_PRINT(-3, "load_design_rank_time:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// id, time
	if (field_count != 2) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_rank_time:field_count %d ", field_count);
		goto cleanup;
	}

	num = 0;
	while(NULL != (row = mysql_fetch_row(result))) {

		id = strtol_safe(row[0], -1);
		if (id <= 0 || id >= MAX_RANK_TIME) {
			ret = -5;
			BUG_PRINT(-5, "load_design_rank_time:id_out_bound %d", id);
			goto cleanup;
		}

		if (++num != id) {
			ret = -15;
			BUG_PRINT(-15, "load_design_rank_time:id_mismatch %d %d", id, num);
			goto cleanup;
		}

		ptime = rank_time_list + id;
		ptime->id = id;

		ptime->time = strtol_safe(row[1], -1);
		if (ptime->time < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_rank_time:time %d", ptime->time);
			goto cleanup;
		}

		/*
		DEBUG_PRINT(0, "load_design_rank_time:id=%d time%d"
		, ptime->id, ptime->time);
		*/

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_DESIGN_ARENA_REWARD "SELECT * FROM design_arena_reward ORDER BY id ASC"
int db_design_load_arena_reward(design_arena_reward_t * arena_reward_list, int &reward_count)
{
	int ret;
	int len;
	int num;
	
	int id;

	design_arena_reward_t * preward;

	len = strlen(SQL_DESIGN_ARENA_REWARD);
	ret = design_safe_mysql_real_query( SQL_DESIGN_ARENA_REWARD, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_arena_reward:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_arena_reward:neg_row %d", num_row);
		goto cleanup;
	}
	if (num_row == 0) {
		ret = 0;
		WARN_PRINT(-3, "load_design_arena_reward:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// id, start_num, end_num, gold, crystal, title, message
	if (field_count != 7) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_arena_reward:field_count %d ", field_count);
		goto cleanup;
	}

	num = 0;
	reward_count = 0;
	while(NULL != (row = mysql_fetch_row(result))) {

		id = strtol_safe(row[0], -1);
		if (id <= 0 || id >= MAX_ARENA_REWARD) {
			ret = -5;
			BUG_PRINT(-5, "load_design_arena_reward:id_out_bound %d", id);
			goto cleanup;
		}

		if (++num != id) {
			ret = -15;
			BUG_PRINT(-15, "load_design_arena_reward:id_mismatch %d %d", id, num);
			goto cleanup;
		}

		preward = arena_reward_list + id;
		preward->id = id;

		preward->start = strtol_safe(row[1], -1);
		if (preward->start < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_arena_reward:start %d", preward->id);
			goto cleanup;
		}

		preward->end = strtol_safe(row[2], -1);
		if (preward->end < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_arena_reward:end %d", preward->id);
			goto cleanup;
		}

		preward->gold = strtol_safe(row[3], -1);
		if (preward->gold < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_arena_reward:gold %d", preward->id);
			goto cleanup;
		}

		preward->crystal = strtol_safe(row[4], -1);
		if (preward->crystal < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_arena_reward:crystal %d", preward->id);
			goto cleanup;
		}

		sprintf(preward->title, "%s", row[5]);
		if (preward->title[0] == '\0') {
			ret = -5;
			BUG_PRINT(-5, "load_design_arena_reward:title_null %d", preward->id);
			goto cleanup;
		}

		sprintf(preward->message, "%s", row[6]);
		if (preward->message[0] == '\0') {
			ret = -5;
			BUG_PRINT(-5, "load_design_arena_reward:message_null %d", preward->id);
			goto cleanup;
		}
		
		reward_count++;

		/*
		DEBUG_PRINT(0, "load_design_arena_reward:id=%d start=%d end=%d gold=%d crystal=%d title=%s message=%s"
		, preward->id, preward->start, preward->end, preward->gold, preward->crystal, preward->title, preward->message);
		*/

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_DESIGN_LOTTERY	"SELECT team, money_type, weight, cards FROM design_lottery WHERE team=%d AND money_type=%d ORDER BY money_type ASC, weight ASC"
int load_lottery_list(char * q_buffer, design_lottery_t *lottery, int team, int money_type)
{
	int ret;
	int len;
	int n;

	int num;
	int weight;
	int now_weight;
	char cards[2000];  // 4 * 400
	bzero(cards, sizeof(cards));
	char *ptr;


	design_lottery_single_t *lottery_list = NULL;
	int *lottery_list_size = NULL;
	int *lottery_list_max_weight = NULL;
	design_lottery_single_t *lottery_single;
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
		ERROR_RETURN(-3, "load_lottery_list:lottery_null");
	}

	len = sprintf(q_buffer, SQL_DESIGN_LOTTERY, team, money_type);

	ret = design_safe_mysql_real_query( q_buffer, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_lottery_list:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_lottery_list:neg_row %d", num_row);
		goto cleanup;
	}

	if (num_row == 0) {
		ret = -15;
		ERROR_PRINT(-3, "load_lottery_list:empty_row %d", num_row);
		goto cleanup;
	}

	if (num_row > 10) {
		ret = -2;
		ERROR_PRINT(-2, "load_lottery_list:row_out_bound %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// team, money_type, weight, cards
	if (field_count != 4) {
		ret = -7;
		ERROR_PRINT(ret, "load_lottery_list:field_count %d ", field_count);
		goto cleanup;
	}

	num = 0;
	now_weight = 1;
	*lottery_list_max_weight = 0;
	while(NULL != (row = mysql_fetch_row(result))) {

		team = strtol_safe(row[0], -1);
		if (team != 1 && team != 2 && team != 3) {
			ret = -5;
			BUG_PRINT(-5, "load_lottery_list:team_out_mismatch %d", team);
			goto cleanup;
		}

		money_type = strtol_safe(row[1], -1);
		if (money_type != 1 && money_type != 2) {
			ret = -5;
			BUG_PRINT(-5, "load_lottery_list:money_type_mismatch %d", money_type);
			goto cleanup;
		}

		weight = strtol_safe(row[2], -1);
		if (weight <= 0) {
			ret = -5;
			BUG_PRINT(-5, "load_lottery_list:weight_invalid %d", weight);
			goto cleanup;
		}

		strncpy(cards, row[3], 2000);

		// DEBUG_PRINT(0, "load_lottery_list:team=%d money_type=%d weight=%d cards=%s"
		// , team, money_type, weight, cards);

		lottery_single = lottery_list + num;
		lottery_single->weight_start = now_weight;
		now_weight += weight;
		lottery_single->weight_end = now_weight;
		lottery_single->size = 0;


		n = 0;
		ptr = cards;
		while (sscanf(ptr, "%d %n", &card_id, &n) == 1) {
			if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
				break;
			}
			// DEBUG_PRINT(0, "load_lottery_list:sscanf card_id=%d", card_id);
			if (lottery_single->size > EVIL_CARD_MAX) {
				ERROR_PRINT(-22, "load_lottery_list:size_out_bound %d", lottery_single->size);
			}

			lottery_single->cards[lottery_single->size] = card_id;
			lottery_single->size++;

			ptr += n;
			card_id = 0;
		}

		// DEBUG PRINT
		/*
		DEBUG_PRINT(0, "load_lottery_list:lottery_single weight_start=%d weight_end=%d size=%d", lottery_single->weight_start, lottery_single->weight_end, lottery_single->size);

		for (int i=0; i<lottery_single->size; i++) {
			DEBUG_PRINT(0, "	card_id=%d", lottery_single->cards[i]);
		}
		*/
		//

		if (lottery_single->size == 0) {
			ERROR_PRINT(-5, "load_lottery_list:size_zero %d", lottery_single->size);
		}

		*lottery_list_max_weight += weight;

		num++;
		if (num >= MAX_LOTTERY_LIST) {
			ERROR_RETURN(-12, "load_lottery_list:num_out_bound %d", num);
		}

	}

	*lottery_list_size = num;

	// DEBUG_PRINT(0, "load_lottery_list:lottery_size=%d lottery_max_weight=%d\n"
	// , *lottery_list_size, *lottery_list_max_weight);

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;

}


int db_design_load_lottery(design_lottery_t *lottery)
{
	int ret;
	
	ret = 0;
	char q_buffer[500];
	ret = load_lottery_list(q_buffer, lottery, 1, 1);
	if (ret != 0) {
		ERROR_RETURN(-6, "db_design_load_lottery:load_single_error");
	}

	ret = load_lottery_list(q_buffer, lottery, 1, 2);
	if (ret != 0) {
		ERROR_RETURN(-6, "db_design_load_lottery:load_single_error");
	}

	ret = load_lottery_list(q_buffer, lottery, 2, 1);
	if (ret != 0) {
		ERROR_RETURN(-6, "db_design_load_lottery:load_single_error");
	}

	ret = load_lottery_list(q_buffer, lottery, 2, 2);
	if (ret != 0) {
		ERROR_RETURN(-6, "db_design_load_lottery:load_single_error");
	}

	ret = load_lottery_list(q_buffer, lottery, 3, 1);
	if (ret != 0) {
		ERROR_RETURN(-6, "db_design_load_lottery:load_single_error");
	}

	ret = load_lottery_list(q_buffer, lottery, 3, 2);
	if (ret != 0) {
		ERROR_RETURN(-6, "db_design_load_lottery:load_single_error");
	}

	return 0;
}


#define SQL_DESIGN_LOAD_GATE "SELECT gate_id, title, gold, crystal, exp, focus_card, power, gate_info FROM design_gate ORDER BY gate_id ASC"
int db_design_load_gate(design_gate_t * gate_list)
{
	int ret;
	int len;
	int gate_id;
	design_gate_t * gate;

	int index;
	int n;
	char *ptr;
	int round;
	int card_id;

	len = strlen(SQL_DESIGN_LOAD_GATE);
	ret = design_safe_mysql_real_query( SQL_DESIGN_LOAD_GATE, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_gate:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_gate:neg_row %d", num_row);
		goto cleanup;
	}

	if (num_row == 0) {
		ret = 0;
		WARN_PRINT(-3, "load_design_gate:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// gate_id, name, gold, crystal, exp focus_card, power, gate_info
	if (field_count != 8) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_gate:field_count %d ", field_count);
		goto cleanup;
	}

	index = 0;
	while(NULL != (row = mysql_fetch_row(result))) {

		gate_id = strtol_safe(row[0], -1);
		if (gate_id <= 0 || gate_id >= MAX_GATE_LIST){
			ret = -5;
			BUG_PRINT(-5, "load_design_gate:gate_id_out_bound %d", gate_id);
			goto cleanup;
		}

		index++;
		if (gate_id != index) {
			ret = -55;
			BUG_PRINT(-55, "load_design_gate:gate_id_miss %d", gate_id);
			goto cleanup;
		}

		gate = gate_list + gate_id;

		strncpy(gate->title, row[1], MAX_GATE_TITLE);
		if (gate->title[0] == '\0') {
			ret = -15;
			BUG_PRINT(-15, "load_design_gate:title_error %d", gate_id);
			goto cleanup;
		}

		gate->gold = strtol_safe(row[2], -1);
		if (gate->gold < 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_gate:gold_error %d", gate_id);
			goto cleanup;
		}

		gate->crystal = strtol_safe(row[3], -1);
		if (gate->crystal < 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_gate:crystal_error %d", gate_id);
			goto cleanup;
		}

		gate->exp = strtol_safe(row[4], -1);
		if (gate->exp < 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_gate:exp_error %d", gate_id);
			goto cleanup;
		}

		gate->focus_card = strtol_safe(row[5], -1);
		if (gate->focus_card < 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_gate:focus_card_error %d", gate_id);
			goto cleanup;
		}

		gate->power = strtol_safe(row[6], -1);
		if (gate->power < 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_gate:power_error %d", gate_id);
			goto cleanup;
		}

		strncpy(gate->gate_info, row[7], EVIL_CARD_MAX);
		if (gate->gate_info[0] == '\0') {
			ret = -15;
			BUG_PRINT(-15, "load_design_gate:gate_info_error %d", gate_id);
			goto cleanup;
		}

		gate->size = 0;
		n = 0;
		ptr = gate->gate_info;
		for (int i=0; i<50; i++) {
			ptr += n;	
			ret = sscanf(ptr, "%d %d %n", &round, &card_id, &n);
			if (ret != 2) {
				break;
			}
			gate->size++;
		}

		// DEBUG_PRINT(0, "load_design_gate:gate_id=%d title=%s gold=%d crystal=%d exp=%d focus_card=%d power=%d size=%d gate_info=%s"
		// , gate_id, gate->title, gate->gold, gate->crystal, gate->exp, gate->focus_card, gate->power, gate->size, gate->gate_info);

	}
	
	ret = 0;

cleanup:
	mysql_free_result(result);
	return 0;
}


#define SQL_DESIGN_LOAD_GATE_MSG "SELECT gate_id, round, card_id, msg FROM design_gate_msg ORDER BY gate_id ASC, round ASC"
int db_design_load_gate_msg(design_gate_msg_t * msg_list)
{
	int ret;
	int len;
	int gate_id;
	design_gate_msg_t * gate;

	int round;
	int card_id;
	char msg[1000];

	len = strlen(SQL_DESIGN_LOAD_GATE_MSG);
	ret = design_safe_mysql_real_query( SQL_DESIGN_LOAD_GATE_MSG, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_gate_msg:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_gate_msg:neg_row %d", num_row);
		goto cleanup;
	}

	if (num_row == 0) {
		ret = 0;
		WARN_PRINT(-3, "load_design_gate_msg:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// gate_id, round, card_id, msg
	if (field_count != 4) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_gate_msg:field_count %d ", field_count);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {

		gate_id = strtol_safe(row[0], -1);
		if (gate_id <= 0 || gate_id >= MAX_GATE_LIST){
			ret = -5;
			BUG_PRINT(-5, "load_design_gate_msg:gate_id_out_bound %d", gate_id);
			goto cleanup;
		}

		gate = msg_list + gate_id;

		gate->size ++;

		round = strtol_safe(row[1], -1);
		if (round < 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_gate_msg:round_error %d", gate_id);
			goto cleanup;
		}

		card_id = strtol_safe(row[2], -1);
		if (card_id < 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_gate_msg:card_id_error %d", gate_id);
			goto cleanup;
		}

		sprintf(msg, "%d %d %s ", round, card_id, row[3]);

		strcat(gate->msg, msg);

		// DEBUG_PRINT(0, "load_design_gate_msg:gate_id=%d size=%d msg=%s"
		// , gate_id, gate->size, gate->msg);

	}
	
	ret = 0;

cleanup:
	mysql_free_result(result);
	return 0;
}


#define SQL_DESIGN_TOWER_REWARD "SELECT id,start_num,end_num,battle_coin FROM design_tower_reward ORDER BY id ASC"
int db_design_load_tower_reward(design_tower_reward_t * tower_reward_list)
{
	int ret;
	int len;
	int num;
	
	int id;

	design_tower_reward_t * preward;

	len = strlen(SQL_DESIGN_TOWER_REWARD);
	ret = design_safe_mysql_real_query( SQL_DESIGN_TOWER_REWARD, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_tower_reward:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_tower_reward:neg_row %d", num_row);
		goto cleanup;
	}
	if (num_row == 0) {
		ret = 0;
		WARN_PRINT(-3, "load_design_tower_reward:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// id, start_num, end_num, battle_coin
	if (field_count != 4) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_tower_reward:field_count %d ", field_count);
		goto cleanup;
	}

	num = 0;
	while(NULL != (row = mysql_fetch_row(result))) {

		id = strtol_safe(row[0], -1);
		if (id <= 0 || id >= MAX_TOWER_REWARD) {
			ret = -5;
			BUG_PRINT(-5, "load_design_tower_reward:id_out_bound %d", id);
			goto cleanup;
		}

		if (++num != id) {
			ret = -15;
			BUG_PRINT(-15, "load_design_tower_reward:id_mismatch %d %d", id, num);
			goto cleanup;
		}

		preward = tower_reward_list + id;
		preward->id = id;

		preward->start = strtol_safe(row[1], -1);
		if (preward->start < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_tower_reward:start %d", preward->id);
			goto cleanup;
		}

		preward->end = strtol_safe(row[2], -1);
		if (preward->end < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_tower_reward:end %d", preward->id);
			goto cleanup;
		}

		preward->battle_coin = strtol_safe(row[3], -1);
		if (preward->battle_coin < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_tower_reward:battle_coin %d", preward->id);
			goto cleanup;
		}

		// DEBUG_PRINT(0, "load_design_tower_reward:id=%d start=%d end=%d battle_coin=%d"
		// , preward->id, preward->start, preward->end, preward->battle_coin);

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_DESIGN_PIECE_SHOP	"SELECT * FROM design_piece_shop ORDER BY id ASC"
int db_design_load_piece_shop(design_piece_shop_t *shop_list, int *weight_max)
{
	int ret;
	int len;

	int hard_show_count[MAX_PIECE_SHOP_HARD_SHOW+1];
	bzero(hard_show_count, sizeof(hard_show_count));

	design_piece_shop_t item;

	len = strlen(SQL_DESIGN_PIECE_SHOP);
	ret = design_safe_mysql_real_query(SQL_DESIGN_PIECE_SHOP, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_piece_shop:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_piece_shop:neg_row %d", num_row);
		goto cleanup;
	}

	if (num_row == 0) {
		ret = -15;
		ERROR_PRINT(-3, "load_piece_shop:empty_row %d", num_row);
		goto cleanup;
	}

	if (num_row > MAX_PIECE_SHOP_LIST) {
		ret = -2;
		ERROR_PRINT(-2, "load_piece_shop:row_out_bound %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// id, card_id, count, gold, crystal, weight, hard_show
	if (field_count != 7) {
		ret = -7;
		ERROR_PRINT(ret, "load_piece_shop:field_count %d ", field_count);
		goto cleanup;
	}

	*weight_max = 0;
	while(NULL != (row = mysql_fetch_row(result))) {

		bzero(&item, sizeof(item));
		item.id = strtol_safe(row[0], -1);
		if (item.id <= 0 || item.id >= MAX_PIECE_SHOP_LIST) {
			ret = -5;
			BUG_PRINT(-5, "load_piece_shop:id_out_bound %d", item.id);
			goto cleanup;
		}
		item.card_id = strtol_safe(row[1], -1);
		if (item.card_id <= 0 || item.card_id > EVIL_CARD_MAX)
		{
			ret = -item.id;
			BUG_PRINT(ret, "load_piece_shop:card_id_out_bound");
			goto cleanup;
		}

		item.count = strtol_safe(row[2], -1);
		if (item.count <= 0) {
			ret = -item.id;
			BUG_PRINT(ret, "load_piece_shop:count_invalid %d", item.count);
			goto cleanup;
		}

		item.gold = strtol_safe(row[3], -1);
		if (item.gold < 0) {
			ret = -item.id;
			BUG_PRINT(ret, "load_piece_shop:gold_invalid %d", item.gold);
			goto cleanup;
		}

		item.crystal = strtol_safe(row[4], -1);
		if (item.crystal < 0) {
			ret = -item.id;
			BUG_PRINT(ret, "load_piece_shop:crystal_invalid %d", item.crystal);
			goto cleanup;
		}

		if ((item.gold > 0 && item.crystal > 0)
		|| (item.gold == 0 && item.crystal == 0)) {
			ret = -item.id;
			BUG_PRINT(ret, "load_piece_shop:money_invalid %d %d", item.gold, item.crystal);
			goto cleanup;
		}

		item.weight = strtol_safe(row[5], -1);
		if (item.weight <= 0) {
			ret = -item.id;
			BUG_PRINT(ret, "load_piece_shop:weight_invalid %d", item.weight);
			goto cleanup;
		}
		(*weight_max) += item.weight;

		item.hard_show = strtol_safe(row[6], -1);
		if (item.hard_show < 0 || item.hard_show > MAX_PIECE_SHOP_HARD_SHOW) {
			ret = -5;
			BUG_PRINT(-5, "load_piece_shop:hard_show_invalid %d", item.hard_show);
			goto cleanup;
		}
		hard_show_count[item.hard_show]++;

		shop_list[item.id] = item;

	}

	for (int i=1; i<MAX_PIECE_SHOP_HARD_SHOW; i++) {
		if (hard_show_count[i] != 0 && hard_show_count[i] != 6) {
			ret = -i;
			BUG_PRINT(ret, "load_piece_shop:hard_show_miss %d", i);
			goto cleanup;
		}
	}


//	*weight_max = now_weight;

	// DEBUG_PRINT(0, "load_piece_shop:weight_max=%d", *weight_max);

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;

}

// input: 22 23 32 
// output: 3
// maximum 100,  if we encounter non-digit, stop
int db_design_count_card(const char *deck)
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

#define SQL_SOLO	"SELECT * FROM design_solo ORDER BY id ASC"
int db_design_load_solo(solo_t *solo_list)
{
	int ret;
	int len;

	int id;

	int hero_id;
	int my_hero;
	solo_t *solo;

	len = strlen(SQL_SOLO);
	ret = design_safe_mysql_real_query( SQL_SOLO, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-93, "load_solo:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// id, hero_id, alias, solo_type, ai_max_ally, hp2, deck, type_list
	// , my_hero, hp1, my_deck, start_side
	// , target1, p11, p12, target2, p21, p22, target3, p31, p32
	if (field_count != 21) {
		ret = -97;
		ERROR_PRINT(ret, "load_solo:field_count_mismatch %d", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -96;
		ERROR_PRINT(ret, "load_solo:empty_row %d", num_row);
		goto cleanup;
	}

	while (NULL != (row = mysql_fetch_row(result))) {
		id = strtol_safe(row[0], -1);
		if (id <= 0 || id > MAX_AI_EID) {
			ret = -95;
			BUG_PRINT(ret, "load_solo:id_outbound %d", id);
			goto cleanup;
		}
		solo = solo_list + id;
		solo->id = id;

		hero_id = strtol_safe(row[1], -1);
		if (hero_id < 0 || hero_id > HERO_MAX) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:hero_id_outbound %d", hero_id);
			goto cleanup;
		}

		sprintf(solo->alias, "%.30s", row[2]);	

		solo->solo_type = strtol_safe(row[3], -1);
		if (solo->solo_type < 0) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:solo_type_outbound %d", solo->solo_type);
			goto cleanup;
		}

		solo->ai_max_ally = strtol_safe(row[4], -1);
		if (solo->ai_max_ally < 0) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:max_ally_outbound %d", solo->ai_max_ally);
			goto cleanup;
		}

		solo->hp2 = strtol_safe(row[5], -1);
		if (solo->hp2 < 0) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:hp2_outbound %d", solo->hp2);
			goto cleanup;
		}

		// solo->deck = [hero_id card_id1 card_id2 ... card_idn]
		len = sprintf(solo->deck2, "%d %.400s", hero_id, row[6]);
		if (len == 0) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:deck_len %d id=%d", len, id);
			goto cleanup;
		}
		// order is important, -1 for hero
		int count = db_design_count_card(solo->deck2)-1;

		len = sprintf(solo->type_list, "%.100s", row[7]);
		if (len != count && (solo->solo_type & 1)) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:type_list_len %d %d id=%d", len, count, id);
			goto cleanup;
		}

		// check type_list is '0'-'9'
		for (int i=0; i<len; i++) {
			if (0==isdigit(solo->type_list[i])) {
				ret = -id;
				BUG_PRINT(ret, "load_solo:type_list_not_digit %d", id);
				goto cleanup;
			}
		}

		if (len == 0) {
			len = sprintf(solo->type_list, "0");
		}

		my_hero = strtol_safe(row[8], -1);
		if (my_hero < 0 || my_hero > HERO_MAX) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:my_hero_out_bound %d", my_hero);
			goto cleanup;
		}

		solo->hp1 = strtol_safe(row[9], -1);
		if (solo->hp1 < 0 || solo->hp1 > 99) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:hp1_out_bound hero=%d hp=%d", my_hero, solo->hp1);
			goto cleanup;
		}

		// solo->my_deck = [hero_id card_id1 card_id2 ... card_idn]
		len = sprintf(solo->deck1, "%d %.400s", my_hero, row[10]);
		if (my_hero!=0 && len <= 3) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:my_deck_len %d my_hero=%d id=%d", len, my_hero, id);
			solo->id = 0; // clean it!
			goto cleanup;
		}

		solo->start_side = strtol_safe(row[11], -1);
		if (solo->start_side != 0 && solo->start_side != 1
		&& solo->start_side != 2) {
			ret = -id;
			BUG_PRINT(ret, "load_solo:start_side_error %d", solo->start_side);
			goto cleanup;
		}

		// target and point
		for (int i=0; i<MAX_SOLO_TARGET; i++) {
			solo->target_list[i].target = strtol_safe(row[12+i*3], -1);
			if (solo->target_list[i].target < 0) {
				ret = -5;
				BUG_PRINT(-5, "load_chapter:target_error %d", solo->id);
				goto cleanup;
			}
			solo->target_list[i].p1 = strtol_safe(row[13+i*3], -1);
			solo->target_list[i].p2 = strtol_safe(row[14+i*3], -1);
		}

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_ROBOT	"SELECT * FROM design_robot ORDER BY rtype ASC, id ASC"
int db_design_load_robot(design_robot_t *robot_list, design_robot_t *fakeman_list)
{
	int ret;
	int len;

	int id;
	int rtype;
	int hero_id;

	int robot_count = 0;
	int fakeman_count = 0;

	design_robot_t *robot;

	len = strlen(SQL_ROBOT);
	ret = design_safe_mysql_real_query( SQL_ROBOT, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_robot:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// id, rtype, icon, lv, rating, hero_id, hp, energy, alias, deck
	if (field_count != 10) {
		ret = -7;
		ERROR_PRINT(ret, "load_robot:field_count_mismatch %d", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_robot:empty_row %d", num_row);
		goto cleanup;
	}

	while (NULL != (row = mysql_fetch_row(result))) {
		id = strtol_safe(row[0], -1);
		if (id <= 0 || id > MAX_AI_EID) {
			ret = -5;
			BUG_PRINT(-5, "load_robot:id_outbound %d", id);
			goto cleanup;
		}

		rtype = strtol_safe(row[1], -1);
		if (rtype != ROBOT_TYPE_NORMAL && rtype != ROBOT_TYPE_FAKEMAN) {
			ret = -id;
			BUG_PRINT(ret, "load_robot:rtype_error %d", rtype);
			goto cleanup;
		}

		if (rtype == ROBOT_TYPE_NORMAL) {
			robot = robot_list + id;
			robot_count++;
			if (id != robot_count) {
				ret = -id;
				BUG_PRINT(ret, "load_robot:robot_id_miss %d", id);
				goto cleanup;
			}
		} else {
			robot = fakeman_list + id;
			fakeman_count++;
			if (id != fakeman_count) {
				ret = -id;
				BUG_PRINT(ret, "load_robot:fakeman_id_miss %d", id);
				goto cleanup;
			}
		}
		robot->id = id;
		robot->rtype = rtype;

		robot->icon = strtol_safe(row[2], -1);
		if (robot->icon < 0 || robot->icon > EVIL_ICON_MAX) {
			ret = -id;
			BUG_PRINT(ret, "load_robot:icon_outbound %d", robot->icon);
			goto cleanup;
		}

		robot->lv = strtol_safe(row[3], -1);
		if (robot->lv < 0) {
			ret = -id;
			BUG_PRINT(ret, "load_robot:lv_outbound %d", robot->lv);
			goto cleanup;
		}

		sscanf(row[4], "%lf", &(robot->rating));
		if (robot->rating < 0) {
			ret = -id;
			BUG_PRINT(ret, "load_robot:rating %lf", robot->rating);
			goto cleanup;
		}

		hero_id = strtol_safe(row[5], -1);
		if (hero_id <= 0 || hero_id > HERO_MAX) {
			ret = -id;
			BUG_PRINT(ret, "load_robot:hero_id_outbound %d", hero_id);
			goto cleanup;
		}

		robot->hp = strtol_safe(row[6], -1);
		if (robot->hp < 0) {
			ret = -id;
			BUG_PRINT(ret, "load_robot:hp_negative %d", robot->hp);
			goto cleanup;
		}

		robot->energy = strtol_safe(row[7], -1);
		if (robot->energy < 0) {
			ret = -id;
			BUG_PRINT(ret, "load_robot:energy_negative %d", robot->energy);
			goto cleanup;
		}

		sprintf(robot->alias, "%.30s", row[8]);	

		len = sprintf(robot->deck, "%d %.400s", hero_id, row[9]);
		if (len == 0) {
			ret = -id;
			BUG_PRINT(ret, "load_robot:deck_len %d id=%d", len, id);
			goto cleanup;
		}

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_CHAPTER	"SELECT * FROM design_chapter ORDER BY chapter_id ASC, stage_id ASC"
int db_design_load_chapter(design_chapter_t *chapter_list)
{
	int ret;
	int len;
	int n;
	int count;
	int chapter_count;
	int stage_count;
	char *ptr;

	int chapter_id;
	int stage_id;
	// char chapter_name[210];
	// char stage_name[210];
	char solo_list[30];
	char tips_list[30];

//	int stage_size;
	int all_weight;

	design_chapter_t *pchapter;
	design_chapter_stage_t *pstage;

	len = strlen(SQL_CHAPTER);
	ret = design_safe_mysql_real_query( SQL_CHAPTER, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_chapter:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// chapter_id, stage_id, chapter_name, stage_name, stage_msg
	// , solo_list, exp, power, tips_list
	// , r1, r1_c, r1_w, r2, r2_c, r2_w, r3, r3_c, r3_w
	if (field_count != 21) {
		ret = -7;
		ERROR_PRINT(ret, "load_chapter:field_count_mismatch %d", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_chapter:row_negative %d", num_row);
		goto cleanup;
	}
	if (num_row == 0) {
		ret = 0;
		DEBUG_PRINT(0, "load_chapter:empty_row %d", num_row);
		goto cleanup;
	}

//	stage_size = 0;
	while (NULL != (row = mysql_fetch_row(result))) {
		// chapter_id
		chapter_id = strtol_safe(row[0], -1);
		if (chapter_id <= 0 || chapter_id >= MAX_CHAPTER) {
			ret = -5;
			BUG_PRINT(-5, "load_chapter:chapter_id_outbound %d", chapter_id);
			goto cleanup;
		}

		pchapter = chapter_list + chapter_id;
		pchapter->chapter_id = chapter_id;

		// stage_id
		stage_id = strtol_safe(row[1], -1);
		if (stage_id <= 0 || stage_id >= MAX_CHAPTER_STAGE) {
			ret = -5;
			BUG_PRINT(-5, "load_chapter:stage_id_error %d", stage_id);
			goto cleanup;
		}

		pstage = pchapter->stage_list + stage_id;
		pstage->stage_id = stage_id;

		// chapter_name
		if (pchapter->name[0] == '\0') {
			sprintf(pchapter->name, "%.210s", row[2]);	
		}

		// stage_name
		sprintf(pstage->name, "%.210s", row[3]);	
		// stage_msg
		sprintf(pstage->stage_msg, "%.800s", row[4]);	

		// solo_list
		sprintf(solo_list, "%.30s", row[5]);	
		count = 0;
		n = 0;
		ptr = solo_list;
		for (int i=0; i<5; i++) {
			ret = sscanf(ptr, "%d %n", &pstage->solo_list[i], &n);
			if (ret != 1 || (pstage->solo_list[i] == 0)) {
				break;
			}
			++count;
			ptr += n;
		}
		if (count == 0) {
			ret = -5;
			BUG_PRINT(-5, "load_chapter:solo_list_zero %d %d", chapter_id, stage_id);
			goto cleanup;
		}
		pstage->solo_size = count;

		// exp
		pstage->exp = strtol_safe(row[6], -1);
		if (pstage->exp < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_chapter:exp_error %d", pstage->stage_id);
			goto cleanup;
		}

		// power
		pstage->power = strtol_safe(row[7], -1);
		if (pstage->power < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_chapter:power_error %d", pstage->stage_id);
			goto cleanup;
		}

		// tips_list
		sprintf(tips_list, "%.30s", row[8]);	
		count = 0;
		n = 0;
		ptr = tips_list;
		for (int i=0; i<5; i++) {
			ret = sscanf(ptr, "%d %n", &pstage->tips_list[i], &n);
			if (ret != 1 || (pstage->tips_list[i] == 0)) {
				break;
			}
			++count;
			ptr += n;
		}
		pstage->tips_size = count;
		if (pstage->tips_list[0] == 0) {
			// no tips list
			pstage->tips_size = 0;
		}

		// reward, count, weight
		all_weight = 0;
		for (int i=0; i<MAX_CHAPTER_REWARD; i++) {
			pstage->reward_list[i].reward = strtol_safe(row[9+i*3], -1);
			if (pstage->reward_list[i].reward < 0) {
				ret = -5;
				BUG_PRINT(-5, "load_chapter:reward_error %d %d %d"
				, chapter_id, pstage->stage_id, pstage->reward_list[i].reward);
				goto cleanup;
			}
			pstage->reward_list[i].count = strtol_safe(row[10+i*3], -1);
			pstage->reward_list[i].weight_start = all_weight+1;
			pstage->reward_list[i].weight_end = all_weight + strtol_safe(row[11+i*3], -1);
			all_weight = pstage->reward_list[i].weight_end;

			if (pstage->reward_list[i].reward == 0 && 
			pstage->reward_list[i].weight_end >= pstage->reward_list[i].weight_start) {
				ret = -5;
				BUG_PRINT(ret, "load_chapter:reward_error %d %d %d"
				, chapter_id, pstage->stage_id, pstage->reward_list[i].reward);
				goto cleanup;
			}
		}
		// set stage reward_weight_max
		pstage->reward_weight_max = all_weight;

	}

	// check stage miss, set chapter size
	chapter_count = 0;
	for (int i=0; i<MAX_CHAPTER; i++) {
		pchapter = chapter_list + i;
		if (pchapter->chapter_id == 0) {continue;};
		++chapter_count;
		if (chapter_count != pchapter->chapter_id) {
			// miss chapter
			ret = -pchapter->chapter_id;
			BUG_PRINT(ret, "load_chapter:chapter_miss %d", pchapter->chapter_id);
			goto cleanup;
		}

		stage_count = 0;
		for (int j=0; j<MAX_CHAPTER_STAGE; j++) {
			pstage = pchapter->stage_list + j;
			if (pstage->stage_id == 0) {continue;};
			++stage_count;
			if (stage_count != pstage->stage_id) {
				ret = -pchapter->chapter_id;
				BUG_PRINT(ret, "load_chapter:stage_miss %d %d %d", pchapter->chapter_id, stage_count, pstage->stage_id);
				goto cleanup;
			}
		}
		pchapter->stage_size = stage_count;
	}


	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_HERO_MISSION	"SELECT * FROM design_hero_mission ORDER BY hero_id ASC, mission_id ASC"
int db_design_load_hero_mission(design_mission_hero_t *hero_list)
{
	int ret;
	int len;
	
	int hero_id;
	int mission_id;

	design_mission_hero_t * hero;
	design_hero_mission_t mission;

	len = strlen(SQL_HERO_MISSION);
	ret = design_safe_mysql_real_query( SQL_HERO_MISSION, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_hero_mission:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// hero_id, mission_id, pre, reward_type, reward_count, mtype, p1, p2, p3, msg
	if (field_count != 10) {
		ret = -7;
		ERROR_PRINT(ret, "load_hero_mission:field_count_mismatch %d", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_hero_mission:row_negative %d", num_row);
		goto cleanup;
	}
	if (num_row == 0) {
		ret = 0;
		DEBUG_PRINT(0, "load_hero_mission:empty_row %d", num_row);
		goto cleanup;
	}

	while (NULL != (row = mysql_fetch_row(result))) {
		hero_id = strtol_safe(row[0], -1);
		if (hero_id <= 0 || hero_id >= HERO_MAX) {
			ret = -5;
			BUG_PRINT(-5, "load_hero_mission:hero_id_outbound %d", hero_id);
			goto cleanup;
		}
		hero = hero_list + hero_id;
		hero->hero_id = hero_id;

		bzero(&mission, sizeof(mission));
		mission_id = strtol_safe(row[1], -1);
		if (mission_id <= 0 || mission_id >= MAX_HERO_MISSION) {
			ret = -5;
			BUG_PRINT(-5, "load_hero_mission:mission_id_outbound %d %d", hero_id, mission_id);
			goto cleanup;
		}
		mission.mission_id = mission_id;


		mission.pre = strtol_safe(row[2], -1);
		if (mission.pre < 0 || mission.pre >= MAX_HERO_MISSION) {
			ret = -5;
			BUG_PRINT(-5, "load_hero_mission:pre_outbound %d %d", hero_id, mission_id);
			goto cleanup;
		}

		mission.reward_type = strtol_safe(row[3], -1);
		if (mission.reward_type < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_hero_mission:reward_type_outbound %d %d", hero_id, mission_id);
			goto cleanup;
		}

		mission.reward_count = strtol_safe(row[4], -1);
		if (mission.reward_count < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_hero_mission:reward_count_outbound %d %d", hero_id, mission_id);
			goto cleanup;
		}

		mission.mtype = strtol_safe(row[5], -1);
		if (mission.mtype < 0) {
			ret = -5;
			BUG_PRINT(-5, "load_hero_mission:mtype_outbound %d %d", hero_id, mission_id);
			goto cleanup;
		}

		mission.p1 = strtol_safe(row[6], -1);
		mission.p2 = strtol_safe(row[7], -1);
		mission.p3 = strtol_safe(row[8], -1);


		sprintf(mission.msg, "%s", row[9]);
		if (strlen(mission.msg) == 0) {
			sprintf(mission.msg, "%s", "_");
		}
		
		hero->mission_list[mission.mission_id] = mission;
		++hero->mission_size;

	//	DEBUG_PRINT(0, "load_hero_mission:hero_id=%d mission_id=%d pre=%d reward_type=%d reward_count=%d mtype=%d p1=%d p2=%d p3=%d msg=%s"
	//	, hero_id, mission->mission_id, mission->pre, mission->reward_type, mission->reward_count, mission->mtype, mission->p1, mission->p2, mission->p3, mission->msg);

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_LOAD_HERO	"SELECT * FROM design_hero ORDER BY hero_id ASC"
int db_design_load_hero(design_hero_t *hero_list)
{
	int ret;
	int len;
	
	int hero_id;

	design_hero_t * hero;

	len = strlen(SQL_LOAD_HERO);
	ret = design_safe_mysql_real_query( SQL_LOAD_HERO, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_hero:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// hero_id, hp, energy
	if (field_count != 3) {
		ret = -7;
		ERROR_PRINT(ret, "load_hero:field_count_mismatch %d", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_hero:row_negative %d", num_row);
		goto cleanup;
	}
	if (num_row == 0) {
		ret = 0;
		DEBUG_PRINT(0, "load_hero:empty_row %d", num_row);
		goto cleanup;
	}

	while (NULL != (row = mysql_fetch_row(result))) {
		hero_id = strtol_safe(row[0], -1);
		if (hero_id <= 0 || hero_id >= HERO_MAX) {
			ret = -5;
			BUG_PRINT(-5, "load_hero:hero_id_outbound %d", hero_id);
			goto cleanup;
		}
		hero = hero_list + hero_id;
		hero->hero_id = hero_id;

		hero->hp = strtol_safe(row[1], -1);
		if (hero->hp <= 0) {
			ret = -5;
			BUG_PRINT(-5, "load_hero:hp_outbound %d", hero_id);
			goto cleanup;
		}

		hero->energy = strtol_safe(row[2], -1);
		if (hero->energy <= 0) {
			ret = -5;
			BUG_PRINT(-5, "load_hero:energy_outbound %d", hero_id);
			goto cleanup;
		}
		
	//	DEBUG_PRINT(0, "load_hero:hero_id=%d hp=%d energy=%d"
	//	, hero->hero_id, hero->hp, hero->energy);

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_DESIGN_CARD_CHAPTER	"SELECT * FROM design_card_chapter ORDER BY card_id ASC, chapter_id ASC, stage_id ASC"
int db_design_load_card_chapter(design_card_chapter_t * card_chapter_list)
{
	int ret;
	int len;
	int card_id;
	design_card_chapter_t *card_chapter;
	design_piece_chapter_t *chapter;

	len = strlen(SQL_DESIGN_CARD_CHAPTER);
	ret = design_safe_mysql_real_query( SQL_DESIGN_CARD_CHAPTER, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_card_chapter:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_card_chapter:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// card_id, chapter_name, chapter_id, stage_id
	if (field_count != 4) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_card_chapter:field_count %d ", field_count);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {

		card_id = strtol_safe(row[0], -1);
		if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
			ret = -5;
			BUG_PRINT(-5, "load_design_card_chapter:card_id %d", card_id);
			goto cleanup;
		}
		card_chapter = card_chapter_list + card_id;
		card_chapter->card_id = card_id;

		chapter = card_chapter->chapter_list + card_chapter->count;
		// peter: BUGBUG %.50s will be overflow, @see design_piece_chapter_t
		// name[30]
		sprintf(chapter->name, "%.50s", row[1]);	
		chapter->chapter_id = strtol_safe(row[2], -1);
		chapter->stage_id = strtol_safe(row[3], -1);
		card_chapter->count++;
	}

	ret = 0;
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_LOAD_HERO_SLOT	"SELECT * FROM design_hero_slot"
// consider: pass the MYSQL *conn here, like dbio ?
int db_design_load_hero_slot(design_hero_slot_t *hero_slot_list)
{
	int ret;
	int len;
	int id;
	design_hero_slot_t *slot;

	len = strlen(SQL_LOAD_HERO_SLOT);
	ret = design_safe_mysql_real_query( SQL_LOAD_HERO_SLOT, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_hero_slot:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// id, deck
	if (field_count != 2) {
		ret = -7;
		ERROR_PRINT(ret, "load_hero_slot:field_count %d != 2", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -6;
		WARN_PRINT(ret, "load_hero_slot:empty_row %d", num_row);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {
		id = strtol_safe(row[0], -1);
		if (id <= 0) {
			ret = -3;
			BUG_PRINT(ret, "load_hero_slot:id_null %d", id);
			goto cleanup;
		}
		slot = hero_slot_list + id;
		slot->id = id;
		sprintf(slot->deck, "%.400s", row[1]);
		if (strlen(slot->deck) != EVIL_CARD_MAX) {
			ret = -id;
			BUG_PRINT(ret, "load_hero_slot:deck_len_error");
			goto cleanup;
		}

		for (int i=0; i< EVIL_CARD_MAX; i++) {
			if (slot->deck[i] < '0' || slot->deck[i] > '9') {
				ret = -id;
				BUG_PRINT(ret, "load_hero_slot:deck_error %d", i);
				goto cleanup;
			}
		}

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_DESIGN_ACHIEVEMENT "SELECT * FROM design_achievement ORDER BY aid DESC"
int db_design_load_achievement(design_achi_t *achi_list)
{
	int ret;
	int len;
	const char* ptr;
	int n;
	int count;

	design_achi_t dach;

	len = strlen(SQL_DESIGN_ACHIEVEMENT);
	ret = design_safe_mysql_real_query( SQL_DESIGN_ACHIEVEMENT, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_design_achievement:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_design_achievement:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// aid, name, msg
	// pre, atype, p1, p2, p3
	// gold, crystal, card, piece
	if (field_count != 12) {
		ret = -7;
		ERROR_PRINT(ret, "load_design_achievement:field_count %d ", field_count);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {
		bzero(&dach, sizeof(dach));

		dach.aid = strtol_safe(row[0], -1);
		if (dach.aid <= 0) {
			ret = -5;
			BUG_PRINT(-5, "load_design_achievement:aid %d", dach.aid);
			goto cleanup;
		}
		if (dach.aid >= MAX_ACHIEVEMENT) {
			ret = -2;
			BUG_PRINT(-2, "load_design_achievement:aid_out_of_bound %d", dach.aid);
			goto cleanup;
		}

		sprintf(dach.name, "%s", row[1]);
		sprintf(dach.msg, "%s", row[2]);
		dach.pre 	= strtol_safe(row[3], -1);
		dach.atype	= strtol_safe(row[4], -1);
		dach.p1 	= strtol_safe(row[5], -1);
		dach.p2 	= strtol_safe(row[6], -1);
		sprintf(dach.p3, "%s", row[7]);
		dach.reward_gold	= strtol_safe(row[8], -1);
		dach.reward_crystal	= strtol_safe(row[9], -1);

		ptr = row[10];
		if (strlen(ptr) > 0) {
			for (int cdx = 0; cdx < MAX_ACHIEVEMENT_REWARD_CARD; cdx++) {
				count = sscanf(ptr, "%d %n", &(dach.reward_card[cdx]), &n);
				if (count == 0) {
					break;
				}
				ptr += n;
			}
		}
		ptr = row[11];
		if (strlen(ptr) > 0) {
			for (int cdx = 0; cdx < MAX_ACHIEVEMENT_REWARD_PIECE; cdx++) {
				count = sscanf(ptr, "%d %n", &(dach.reward_piece[cdx]), &n);
				if (count == 0) {
					break;
				}
				ptr += n;
			}
		}
//		DEBUG_PRINT(0, "row[10] %s row[11] %s", row[10], row[11]);
		if (dach.atype <= 0) {
			ret = -15;
			BUG_PRINT(-15, "load_design_achievement:atype<=0 aid=%d", dach.aid);
			goto cleanup;
		}

		if (dach.atype == ACHIEVEMENT_CARD_LIST) {
			for (int pdx = 0; pdx < EVIL_CARD_MAX; pdx++) 
			{
				if (dach.p3[pdx] < '0' || dach.p3[pdx] > '9') {
					BUG_PRINT(-25, "load_design_achievement:p3[%d]=%c aid=%d"
					, pdx, dach.p3[pdx], dach.aid);
					goto cleanup;
				}
				if (dach.p3[pdx] - '0' > 0) {
					dach.p1++;
				}
			}
		}

		achi_list[dach.aid] = dach;

		// DEBUG_PRINT(0, "load_design_achievement:mid=%d pre=%d lv=%d hero=%d daily=%d mtype=%d p1=%d p2=%d p3=%d reward_card=%d exp=%d gold=%d crystal=%d mtext=%s", mis.mid, mis.pre, mis.lv, mis.hero, mis.daily
		// , mis.mtype, mis.p1, mis.p2, mis.p3
		// , mis.reward_card, mis.reward_exp, mis.reward_gold
		// , mis.reward_crystal, mis.mtext);

	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}

#define SQL_DESIGN_DAILY_LOGIN "SELECT * FROM design_daily_login ORDER BY day DESC"
int db_design_load_daily_login(design_daily_login_t *daily_login_list)
{
	int ret;
	int len;
	int n;
	int day;
	int piece;
	int p_count;
	int log_time;
	const char* ptr;
	int tmp_count;

	design_daily_login_t *daily_login;

	len = strlen(SQL_DESIGN_DAILY_LOGIN);
	ret = design_safe_mysql_real_query( SQL_DESIGN_DAILY_LOGIN, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_daily_login:null_result");
	}

	num_row = mysql_num_rows(result);
	if (num_row <= 0) {
		ret = -5;
		ERROR_PRINT(ret, "load_daily_login:empty_row %d", num_row);
		goto cleanup;
	}

	field_count = mysql_field_count(g_db_conn);
	// day, log_time, gold, crystal
	// cards, pieces
	if (field_count != 6) {
		ret = -7;
		ERROR_PRINT(ret, "load_daily_login:field_count %d ", field_count);
		goto cleanup;
	}

	while(NULL != (row = mysql_fetch_row(result))) {

		day = strtol_safe(row[0], -1);
		if (day <= 0) {
			ret = -5;
			BUG_PRINT(-5, "load_daily_login:day %d", day);
			goto cleanup;
		}
		if (day > MAX_DAILY_LOGIN) {
			ret = -2;
			BUG_PRINT(-2, "load_daily_login:day_out_of_bound %d", day);
			goto cleanup;
		}

		daily_login = daily_login_list + day;
		daily_login->day = day;

		log_time = strtol_safe(row[1], -1);
		if (log_time <= 0 || log_time >= MAX_DAILY_REWARD) {
			ret = -15;
			BUG_PRINT(-15, "load_daily_login:log_time_out_bound %d", log_time);
			goto cleanup;
		}
		design_daily_reward_t &reward = daily_login->daily_reward[log_time];
		reward.log_time = log_time;
		reward.gold		= strtol_safe(row[2], 0);
		reward.crystal	= strtol_safe(row[3], 0);

		tmp_count = 0;
		ptr = row[4];
		for (int cdx = 0; cdx < MAX_DAILY_REWARD_CARD; cdx++)
		{
			ret = sscanf(ptr, "%d %n", &(reward.cards[cdx]), &n);
			if (ret != 1 || (reward.cards[cdx] == 0)) {
				break;
			}
			tmp_count++;
			ptr += n;
		}
		reward.card_count = tmp_count;

		tmp_count = 0;
		ptr = row[5];
		for (int cdx = 0; cdx < MAX_DAILY_REWARD_CARD; cdx++)
		{
			ret = sscanf(ptr, "%d %d %n", &piece, &p_count, &n);
			if (ret != 2) {
				break;
			}
			reward.pieces[cdx][0] = piece;
			reward.pieces[cdx][1] = p_count;
			tmp_count++;
			ptr += n;
		}
		reward.piece_count = tmp_count;
	}

	for (int i = 1; i <= MAX_DAILY_LOGIN; i++)
	{
		daily_login = daily_login_list + i;
		BUG_CLEANUP(daily_login->day == 0, -i, "load_daily_login_fail %d", i);
		if (daily_login->daily_reward[1].log_time == 0) {
			BUG_CLEANUP(1, -i, "load_daily_login_reward_1_empty");
		}
	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_QUICK_REWARD	"SELECT * FROM design_quick_reward ORDER BY win_flag ASC"
int db_design_load_quick_reward(design_quick_reward_list_t *reward_list)
{
	int ret;
	int len;

	int flag;
	int weight;
	int * total;
	int win_pos = 0;
	int lose_pos = 0;
	int win_total_weight = 0;
	int lose_total_weight = 0;

	design_quick_reward_t *reward;

	len = strlen(SQL_QUICK_REWARD);
	ret = design_safe_mysql_real_query( SQL_QUICK_REWARD, len);


	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_quick_reward:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// win_flag, reward_type, count, weight
	if (field_count != 4) {
		ret = -7;
		ERROR_PRINT(ret, "load_quick_reward:field_count_mismatch %d", field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_quick_reward:row_negative %d", num_row);
		goto cleanup;
	}

	if (num_row != 8) {
		ret = -16;
		ERROR_PRINT(ret, "load_quick_reward:row_num_mismatch %d", num_row);
		goto cleanup;
	}

	while (NULL != (row = mysql_fetch_row(result))) {

		flag = strtol_safe(row[0], -1);
		if (flag != 1 && flag != 2) {
			ret = -15;
			BUG_PRINT(-15, "load_quick_reward:win_flag %d", flag);
			goto cleanup;
		}

		if (flag == 1) {
			reward = &reward_list[0].reward_array[win_pos++];		
			total = &win_total_weight;
		} else {
			reward = &reward_list[1].reward_array[lose_pos++];
			total = &lose_total_weight;
		}
		
		reward->type = strtol_safe(row[1], -1);
		if (reward->type != QUICK_REWARD_TYPE_GOLD 
		&& reward->type != QUICK_REWARD_TYPE_CRYSTAL) {
			ret = -15;
			BUG_PRINT(-15, "load_quick_reward:type %d", reward->type);
			goto cleanup;
		}
		
		reward->count = strtol_safe(row[2], -1);
		if (reward->count <= 0) {
			ret = -15;
			BUG_PRINT(-15, "load_quick_reward:count %d", reward->count);
			goto cleanup;
		}
		
		weight = strtol_safe(row[3], -1);
		if (weight <= 0) {
			ret = -15;
			BUG_PRINT(-15, "load_quick_reward:weight %d", weight);
			goto cleanup;
		}

		reward->weight_start = *total+1;
		*total += weight;
		reward->weight_end = *total;

	}

	if (win_pos != MAX_QUICK_REWARD || lose_pos != MAX_QUICK_REWARD) {
		ret = -15;
		BUG_PRINT(-15, "load_quick_reward:pos_mismatch %d %d", win_pos, lose_pos);
		goto cleanup;
	}

	reward_list[0].max_weight = win_total_weight;
	reward_list[1].max_weight = lose_total_weight;

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}


#define SQL_CHAPTER_DIALOG	"SELECT * FROM design_chapter_dialog ORDER BY chapter ASC, stage ASC"
int db_design_load_chapter_dialog(design_chapter_dialog_t *dialog_list)
{
	int ret;
	int n;
	int len;
	int chapter;
	int stage;
	char* dlg_ptr;
	char tmp_buffer[1000];

	len = strlen(SQL_CHAPTER_DIALOG);
	ret = design_safe_mysql_real_query( SQL_CHAPTER_DIALOG, len);

	MYSQL_RES *result;
	MYSQL_ROW row;
	int num_row;
	int field_count;

	result = mysql_store_result(g_db_conn);
	if (result == NULL) {
		ERROR_RETURN(-3, "load_chapter_dialog:null_result");
	}

	field_count = mysql_field_count(g_db_conn);
	// chapter, stage, dialog
	if (field_count != 3) {
		ret = -7;
		ERROR_PRINT(ret, "load_chapter_dialog:field_count_mismatch %d"
		, field_count);
		goto cleanup;
	}

	num_row = mysql_num_rows(result);
	if (num_row < 0) {
		ret = -6;
		ERROR_PRINT(ret, "load_chapter_dialog:row_negative %d", num_row);
		goto cleanup;
	}

	while (NULL != (row = mysql_fetch_row(result))) {

		chapter = strtol_safe(row[0], -1);
		if (chapter <= 0 || chapter > MAX_CHAPTER) {
			ret = -15;
			BUG_PRINT(-15, "load_chapter_dialog:chapter %d", chapter);
			goto cleanup;
		}

		stage = strtol_safe(row[1], -1);
		if (stage <= 0 || stage > MAX_CHAPTER_STAGE) {
			ret = -25;
			BUG_PRINT(-25, "load_chapter_dialog:stage %d", stage);
			goto cleanup;
		}
		
		design_stage_dialog_t &dlg = dialog_list[chapter].stage_dialog_list[stage];
		sprintf(dlg.dialog, "%s", row[2]);
		dlg_ptr = row[2];
		while ((ret = sscanf(dlg_ptr, "%s%n", tmp_buffer, &n)) == 1) {
			dlg.dialog_count++;
			dlg_ptr += n;
		}
	}

	ret = 0;
	
cleanup:
	mysql_free_result(result);
	return ret;
}




////////// DB FUNCTION END /////////



////////// DB TEST START /////////

#ifdef TTT

// db_design_load_shop
int design_test0_shop(int argc, char * argv[]) {
	int ret;

	shop_t shop_list[EVIL_CARD_MAX+1];
	bzero(shop_list, sizeof(shop_list));

	ret = db_design_load_shop(shop_list);
	ERROR_PRINT(ret, "design_test0:db_design_load_shop_error");

	shop_t c;
	for (int i=1;i<=EVIL_CARD_MAX;i++) {

		c = shop_list[i];	
		if (c.card_id == 0) {
			continue;
		}
		DEBUG_PRINT(0, "design_test0:ccc=%d, card_buy_gold=%d, card_sell_gold=%d, card_buy_crystal=%d, card_sell_crystal=%d, piece_buy_gold=%d, piece_sell_gold=%d, piece_buy_crystal=%d, piece_sell_crystal=%d"
		, c.card_id
		, c.card_buy_gold, c.card_sell_gold
		, c.card_buy_crystal, c.card_sell_crystal
		, c.piece_buy_gold, c.piece_sell_gold
		, c.piece_buy_crystal, c.piece_sell_crystal
		);
		
	}

	return ret;
}

int design_test1_ai(int argc, char * argv[]) {
	int ret;
	char * deck;
	ai_t ai_list[MAX_AI_EID+1]; // +1 for 1-base array
	ai_t *pai;

	printf("in_design_test1_ai\n");

	bzero(ai_list, sizeof(ai_list));  // safety

	ret = db_design_load_ai(ai_list);
	ERROR_RETURN(ret, "test_load_ai_error");

	for (int i=0; i<=MAX_AI_EID; i++) {
		if (ai_list[i].id == 0) continue;
		pai = &ai_list[i];
		deck = ai_list[i].deck;

		printf("id=%d, icon=%d, lv=%d, rating=%lf, rating_flag=%d, pid=%d, win_gold=%d, win_exp=%d, alias=%s \n"
		, pai->id, pai->icon, pai->lv, pai->rating, pai->rating_flag
		, pai->pid, pai->win_gold, pai->win_exp, pai->alias);

		printf("id=%d   deck_len=%zd  c1-c20=[%.20s]\n", ai_list[i].id
		, strlen(deck), deck);
		printf("  c22(human)=%c  c41(shadow)=%c\n"
		, deck[22-1], deck[41-1]);

		printf("  c61(war)=%c  c71(mage)=%c  c91(pri)=%c\n"
		, deck[61-1], deck[71-1], deck[91-1]);
	}
	return 0;
}


int design_test2_pick(int argc, char * argv[]) {
	int ret;
	pick_t pick_list[MAX_PICK];
	pick_t *pick;
	ret = db_design_load_pick(pick_list);
	ERROR_NEG_RETURN(ret, "test_load_pick_error");

	// header
	printf("ptype\tloc\trate\tstar1\tstar2\tstar3\tstar4\tstar5\n");
	printf("---------------------------------\n");
	for (int p=0; p<MAX_PICK; p++) {
		pick = pick_list + p;
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
	return 0;
}

int design_test3_constant(int argc, char * argv[])
{
	int ret;
	constant_t cons;
	char tmp_buffer[100];
	char *tptr;
	ret = db_design_load_constant(&cons);
	ERROR_NEG_RETURN(ret, "test_load_constant_error");

	tptr = tmp_buffer;
	tptr += sprintf(tptr, "[");
	for (int i = 0; i < cons.first_vip_extra_card_kind; i++)
	{
		tptr += sprintf(tptr, "%d(%d) "
		, cons.first_vip_extra_card_list[i][0]
		, cons.first_vip_extra_card_list[i][1]);
	}
	tptr += sprintf(tptr, "]");

	INFO_PRINT(0, "test_load_constant: batch_refresh_gold=%d, pick_gold=%d, batch_refresh_crystal=%d, pick_crystal=%d, max_timeout=%d, guild_bonus_rate=%lf, create_guild_gold=%d, create_guild_crystal=%d, lottery_one_gold=%d lottery_one_crystal=%d lottery_ten_gold=%d lottery_ten_crystal=%d pshop_reset_interval=%d pshop_refresh_gold=%d pshop_max_refresh_gold=%d exchange_crystal_gold=%d first_vip_extra_gold=%d first_vip_extra_crystal=%d first_vip_extra_card_count=%d first_vip_extra_card_list=%s quick_robot_flag=%d quick_win_exp=%d quick_lose_exp=%d quick_draw_exp=%d quick_fold_exp=%d"
	, cons.batch_refresh_gold
	, cons.pick_gold, cons.batch_refresh_crystal, cons.pick_crystal
	, cons.max_timeout
	, cons.guild_bonus_rate
	, cons.create_guild_gold, cons.create_guild_crystal
	, cons.lottery_one_gold, cons.lottery_one_crystal
	, cons.lottery_ten_gold, cons.lottery_ten_crystal
	, cons.pshop_reset_interval, cons.pshop_refresh_gold
	, cons.pshop_max_refresh_gold, cons.exchange_crystal_gold
	, cons.first_vip_extra_gold , cons.first_vip_extra_crystal
	, cons.first_vip_extra_card_kind, tmp_buffer
	, cons.quick_robot_flag
	, cons.win_exp[GAME_QUICK], cons.lose_exp[GAME_QUICK]
	, cons.draw_exp[GAME_QUICK], cons.fold_exp[GAME_QUICK]
	);

	return 0;
}

int design_test4_load_notice(int argc, char * argv[]) {
	int ret;
	notice_t notice_list[MAX_NOTICE];
	bzero(notice_list, sizeof(notice_list));
	ret = db_design_load_notice(notice_list);
	ERROR_NEG_RETURN(ret, "test_load_constant_error");

	return 0;
}

int design_test5_load_exp(int argc, char * argv[]) {
	int ret;
	int exp_list[MAX_LEVEL + 1];
	bzero(exp_list, sizeof(exp_list));
	ret = db_design_load_exp(exp_list);
	ERROR_NEG_RETURN(ret, "test_load_exp_error");
	printf("test5_load_exp:exp_list\n");
	for (int i = 0; i < MAX_LEVEL + 1; i++) {
		DEBUG_PRINT(0, " level %d exp %d", i, exp_list[i]);
	}

	return 0;
}

int design_test6_load_std_deck(int argc, char * argv[]) {
	int ret;
	std_deck_t deck_list[HERO_MAX+1]; // +1 for 1-base array
	bzero(deck_list, sizeof(deck_list));
	ret = db_design_load_std_deck(deck_list);
	ERROR_NEG_RETURN(ret, "test_load_std_deck_error");

	std_deck_t std_deck;
	for (int i=0; i<=HERO_MAX; i++) {
		if (deck_list[i].id == 0) continue;
		std_deck = deck_list[i];

		printf("deck_list[%d], id=%d deck=%s \n", i, std_deck.id, std_deck.deck);
	}

	return 0;
}

int design_test7_load_design_guild(int argc, char * argv[]) {
	int ret;
	design_guild_t *pguild;
	design_guild_t design_guild_list[100];
	bzero(design_guild_list, sizeof(design_guild_list));
	ret = db_design_load_guild(design_guild_list);
	ERROR_NEG_RETURN(ret, "test_load_design_guild_error");

	for (int i=0; i<100; i++) {
		if (design_guild_list[i].lv == 0) continue;
		pguild = &design_guild_list[i];

		printf("lv=%d, consume_gold=%d, levelup_gold=%d, member_max=%d\n"
		, pguild->lv, pguild->consume_gold, pguild->levelup_gold
		, pguild->member_max);

	}

	return 0;
}


int design_test8_load_design_merge(int argc, char * argv[]) {
	int ret;
	design_merge_t *pmerge;
	design_merge_t design_merge_list[EVIL_CARD_MAX+1];
	bzero(design_merge_list, sizeof(design_merge_list));
	ret = db_design_load_merge(design_merge_list);
	ERROR_NEG_RETURN(ret, "test_load_design_merge_error");

	for (int i=1; i<=EVIL_CARD_MAX; i++) {
		if (design_merge_list[i].card_id == 0) continue;
		pmerge = &design_merge_list[i];

		printf("card_id=%d, count=%d\n"
		, pmerge->card_id, pmerge->count);

	}

	return 0;
}

int design_test9_load_design_pay(int argc, char * argv[]) {
	int ret;
	design_pay_t design_pay_list[MAX_PAY_NUM]; // XXX
	bzero(design_pay_list, sizeof(design_pay_list));
	ret = db_design_load_pay(design_pay_list);
	ERROR_NEG_RETURN(ret, "test_load_design_pay_error");


	return 0;
}

int design_test10_load_design_version(int argc, char * argv[]) {
	int ret;
	design_version_t version;
	bzero(&version, sizeof(design_version_t));
	ret = db_design_load_version(&version);
	ERROR_NEG_RETURN(ret, "test_load_design_version_error");

	return 0;
}

int design_test11_load_design_website(int argc, char * argv[]) {
	int ret;
	design_website_t website[50];
	bzero(&website, sizeof(design_website_t));
	ret = db_design_load_website(website);
	ERROR_NEG_RETURN(ret, "test_load_design_website_error");

	return 0;
}

int design_test12_load_design_mission(int argc, char * argv[]) {
	int ret;
	int count;
	char *tmp_ptr;
	char tmp_buffer[DB_BUFFER_MAX+1];

	design_mission_t mission_list[MAX_MISSION];
	bzero(mission_list, sizeof(mission_list));
	ret = db_design_load_mission(mission_list);
	ERROR_NEG_RETURN(ret, "test_load_design_mission_error");

//	ERROR_RETURN(mission_list.size!=3, "test_load_design_mission:size!=2 %zu", mission_list.size());
	count = 0;
	for (int i = 0; i < MAX_MISSION; i++) {
		design_mission_t &mission = mission_list[i];
		if (mission.mid == 0) {
			continue;
		}
		count ++;

		tmp_ptr = tmp_buffer;
		tmp_ptr += sprintf(tmp_ptr, "card_count[%d] card[", mission.card_count);
		for (int cdx = 0; cdx < mission.card_count; cdx++) {
			tmp_ptr += sprintf(tmp_ptr, "%d ", mission.reward_card[cdx]);
		}
		tmp_ptr += sprintf(tmp_ptr, "] piece_count[%d] piece["
		, mission.piece_count);
		for (int pdx = 0; pdx < mission.piece_count; pdx++) {
			tmp_ptr += sprintf(tmp_ptr, "%d:%d "
			, mission.reward_piece[pdx][0], mission.reward_piece[pdx][1]);
		}
		tmp_ptr += sprintf(tmp_ptr, "]");
		DEBUG_PRINT(0, "mission id=%d pre=%d lv=%d hero=%d daily=%d guild_lv=%d mtype=%d p1=%d p2=%d p3=%d exp=%d gold=%d crystal=%d power=%d time=%s text=%s"
		, mission.mid, mission.pre, mission.lv, mission.hero
		, mission.daily, mission.guild_lv
		, mission.mtype, mission.p1, mission.p2, mission.p3
		, mission.reward_exp
		, mission.reward_gold, mission.reward_crystal
		, mission.reward_power
		, mission.reset_time, mission.mtext);
		INFO_PRINT(0, "\t%s", tmp_buffer);
	}
	DEBUG_PRINT(0, "test12:mission_count=%d", count);

	return 0;
}

int design_test13_load_design_slot(int argc, char * argv[]) {
	int ret;
	design_slot_t slot_list[MAX_SLOT_NUM];
	bzero(&slot_list, sizeof(design_slot_t));
	ret = db_design_load_slot(slot_list);
	ERROR_NEG_RETURN(ret, "test_load_design_slot_error");

	return 0;
}

int design_test14_load_match(int argc, char * argv[]) 
{
	int ret;
	match_t match;
	bzero(&match, sizeof(match_t));
	ret = db_design_load_match(&match);
	ERROR_NEG_RETURN(ret, "test_load_design_match_player_error");
	DEBUG_PRINT(0, "test14:match_id=%ld title=%s max_player=%d max_team=%d start_time=%ld t1=%ld t2=%ld t3=%ld t4=%ld status=%d round=%d", match.match_id, match.title, match.max_player, match.max_team, match.start_time, match.round_time_list[0], match.round_time_list[1], match.round_time_list[2], match.round_time_list[3], match.status, match.round);

	return 0;
}

int design_test15_load_match_player(int argc, char * argv[]) 
{
	int ret;
	match_t match;
	bzero(&match, sizeof(match));
	match.match_id = 1111;
	ret = db_design_load_match_player(&match);
	ERROR_NEG_RETURN(ret, "test_load_design_match_player_error");

	DEBUG_PRINT(0, "test15:print player_list -----------");
	int num = sizeof(match.player_list) / sizeof(match_player_t);
	for (int i=0; i<num; i++) {
		match_player_t &player = match.player_list[i];
		if (player.eid == 0) {
			continue;
		}
		DEBUG_PRINT(0, "team_player:%ld %d %d %d %d %d %d %d %d %d %s"
		, player.match_id, player.eid, player.round, player.team_id, player.win, player.lose, player.draw, player.tid, player.point, player.icon, player.alias);
	}
	DEBUG_PRINT(0, "test15:print player_list -----------");

	DEBUG_PRINT(0, "test15:print e_player_list -----------");
	num = sizeof(match.e_player_list) / sizeof(match_player_t);
	for (int i=0; i<num; i++) {
		match_player_t &player = match.e_player_list[i];
		if (player.eid == 0) {
			continue;
		}
		DEBUG_PRINT(0, "eli_player:%ld %d %d %d %d %d %d %d %d %d %s"
		, player.match_id, player.eid, player.round, player.team_id, player.win, player.lose, player.draw, player.tid, player.point, player.icon, player.alias);
	}
	DEBUG_PRINT(0, "test15:print e_player_list -----------");


	return 0;
}

int design_test16_load_design_rank_reward(int argc, char * argv[]) {
	int ret;
	design_rank_reward_t reward_list[MAX_RANK_REWARD];
	bzero(&reward_list, sizeof(design_rank_reward_t));
	ret = db_design_load_rank_reward(reward_list);
	ERROR_NEG_RETURN(ret, "test_load_design_rank_reward_error");

	return 0;
}

int design_test17_load_design_rank_time(int argc, char * argv[]) {
	int ret;
	design_rank_time_t time_list[MAX_RANK_TIME];
	bzero(&time_list, sizeof(time_list));
	ret = db_design_load_rank_time(time_list);
	ERROR_NEG_RETURN(ret, "test_load_design_rank_time_error");

	for (int i=0; i<MAX_RANK_TIME; i++) {
		design_rank_time_t & time = time_list[i];
		if (time.id == 0) {
			continue;
		}
		DEBUG_PRINT(0, "test17:design_rank_time id=%d time%d"
		, time.id, time.time);
	}

	return 0;
}

int design_test18_load_design_lottery(int argc, char * argv[]) {
	int ret;
	design_lottery_t lottery_info;
	ret = db_design_load_lottery(&lottery_info);
	ERROR_NEG_RETURN(ret, "test18:load_design_lottery_error");
	return 0;
}

int design_test19_load_design_gate(int argc, char * argv[]) {
	int ret;
	// int design_gate_size;
	design_gate_t design_gate_list[MAX_GATE_LIST];
	ret = db_design_load_gate(design_gate_list);
	ERROR_NEG_RETURN(ret, "test19:load_design_gate_error");
	return 0;
}

int design_test20_load_design_fight_schedule(int argc, char * argv[]) {
	int ret;
	design_fight_schedule_t fight_schedule_list[FIGHT_SCHEDULE_MAX];
	int max_fight_schedule;
	ret = db_design_load_fight_schedule(fight_schedule_list, max_fight_schedule);
	ERROR_NEG_RETURN(ret, "test19:load_design_fight_schedule_error");
	for (int i = 0; i < max_fight_schedule; i++)
	{
		design_fight_schedule_t *schedule = fight_schedule_list + i;
		DEBUG_PRINT(1, "game_type[%d] open_time[%ld] close_time[%ld]"
		, schedule->game_type, schedule->open_time, schedule->close_time);
	}

	return 0;
}


int design_test21_load_design_gate_msg(int argc, char * argv[]) {
	int ret;
	// int design_gate_size;
	design_gate_msg_t msg_list[MAX_GATE_LIST];
	bzero(msg_list, sizeof(msg_list));
	ret = db_design_load_gate_msg(msg_list);
	ERROR_NEG_RETURN(ret, "test21:load_design_gate_msg_error");
	return 0;
}


int design_test22_load_design_tower_reward(int argc, char * argv[]) {
	int ret;
	design_tower_reward_t reward_list[MAX_RANK_REWARD];
	bzero(&reward_list, sizeof(design_tower_reward_t));
	ret = db_design_load_tower_reward(reward_list);
	ERROR_NEG_RETURN(ret, "test_load_design_tower_reward_error");

	return 0;
}


int design_test23_load_design_piece_shop(int argc, char * argv[]) {
	int ret;
	int weight_max;
	design_piece_shop_t shop_list[MAX_PIECE_SHOP_LIST];
	bzero(shop_list, sizeof(shop_list));
	ret = db_design_load_piece_shop(shop_list, &weight_max);
	ERROR_NEG_RETURN(ret, "test_load_design_piece_shop_error");

	DEBUG_PRINT(1, "piece_shop_list -- weight_max[%d]", weight_max);
	for (int i = 0; i < MAX_PIECE_SHOP_LIST; i++)
	{
		design_piece_shop_t &item = shop_list[i];
		if (item.id == 0)
		{
			continue;
		}
		DEBUG_PRINT(1, "piece_shop_item: id[%d] card_id[%d] count[%d] gold[%d] crystal[%d] weight[%d] hard_show[%d]"
		, item.id, item.card_id, item.count, item.gold, item.crystal
		, item.weight, item.hard_show);
	}

	return 0;
}

int design_test24_load_solo(int argc, char * argv[]) {
	int ret;
	solo_t solo_list[MAX_AI_EID+1];
	bzero(solo_list, sizeof(solo_list));
	ret = db_design_load_solo(solo_list);
	ERROR_NEG_RETURN(ret, "test_load_design_solo");

	for (int i=0; i<MAX_AI_EID+1; i++) {
		solo_t *solo = solo_list + i;
		if (solo->id == 0) { continue;}
		DEBUG_PRINT(0, "load_solo:id=%d alias=%s solo_type=%d ai_max_ally=%d hp2=%d deck2=[%s] type_list=[%s] hp1=%d deck1=[%s] start_side=%d"
		, solo->id, solo->alias, solo->solo_type, solo->ai_max_ally, solo->hp2, solo->deck2, solo->type_list, solo->hp1, solo->deck1, solo->start_side);

		printf("target_list:");
		for (int j=0; j<MAX_SOLO_TARGET; j++) {
			solo_target_t *t = solo->target_list + j;
			printf("\t[%d %d %d]", t->target, t->p1, t->p2);
		}
		printf("\n");

	}
	return 0;
}

int design_test25_load_robot(int argc, char * argv[]) {
	int ret;
	design_robot_t robot_list[MAX_AI_EID];
	bzero(robot_list, sizeof(robot_list));
	design_robot_t fakeman_list[MAX_AI_EID];
	bzero(fakeman_list, sizeof(fakeman_list));
	ret = db_design_load_robot(robot_list, fakeman_list);
	ERROR_NEG_RETURN(ret, "test_load_design_robot");

	design_robot_t * robot;
	for (int i=0; i<MAX_AI_EID; i++) {
		robot = robot_list + i;
		if (robot->id == 0) { continue;}
		INFO_PRINT(0, "robot[%d]:id=%d rtype=%d icon=%d lv=%d rating=%lf hp=%d energy=%d alias=%s deck=[%s]"
		, i, robot->id, robot->rtype, robot->icon, robot->lv, robot->rating, robot->hp, robot->energy, robot->alias, robot->deck);

	}
	for (int i=0; i<MAX_AI_EID; i++) {
		robot = fakeman_list + i;
		if (robot->id == 0) { continue;}
		INFO_PRINT(0, "robot[%d]:id=%d rtype=%d icon=%d lv=%d rating=%lf hp=%d energy=%d alias=%s deck=[%s]"
		, i, robot->id, robot->rtype, robot->icon, robot->lv, robot->rating, robot->hp, robot->energy, robot->alias, robot->deck);

	}

	return 0;
}

int design_test26_load_chapter(int argc, char * argv[]) {
	int ret;
	design_chapter_t chapter_list[MAX_CHAPTER];
	bzero(chapter_list, sizeof(chapter_list));
	ret = db_design_load_chapter(chapter_list);
	ERROR_NEG_RETURN(ret, "test_load_design_chapter");
	
	design_chapter_t *pchapter;
	design_chapter_stage_t * pstage;

	// DEBUG PRINT
	for (int i=0; i<MAX_CHAPTER; i++) {
		pchapter = chapter_list + i;
		if (pchapter->chapter_id == 0) {continue;};
		printf("chapter_name=%s stage_size=%d\n", pchapter->name, pchapter->stage_size);
		for (int j=0; j<MAX_CHAPTER_STAGE; j++) {
			pstage = pchapter->stage_list + j;
			if (pstage->stage_id == 0) {continue;};
			printf("stage_name=%s", pstage->name);
			printf(" stage_msg=%s", pstage->stage_msg);
			printf(" solo_size=%d", pstage->solo_size);
			printf(" solo_list=[");
			for (int l=0; l<pstage->solo_size; l++) {
				printf(" %d", pstage->solo_list[l]);
			}
			printf("]");
			printf(" exp=%d", pstage->exp);
			printf(" power=%d", pstage->power);
			printf(" tips_size=%d", pstage->tips_size);
			printf(" tips_list=[");
			for (int l=0; l<pstage->tips_size; l++) {
				printf(" %d", pstage->tips_list[l]);
			}
			printf("]");
			printf(" reward_weight_max=%d", pstage->reward_weight_max);
			printf(" reward_list=");
			for (int l=0; l<MAX_CHAPTER_REWARD; l++) {
				printf(" [%d %d %d %d]", pstage->reward_list[l].reward, pstage->reward_list[l].count, pstage->reward_list[l].weight_start, pstage->reward_list[l].weight_end);
			}
			printf("\n");
		}
	}

	return 0;
}

int design_test27_load_hero_mission(int argc, char * argv[]) {
	int ret;
	design_mission_hero_t hero_list[HERO_MAX];
	bzero(hero_list, sizeof(hero_list));
	ret = db_design_load_hero_mission(hero_list);
	ERROR_NEG_RETURN(ret, "test_load_design_hero_mission");

	return 0;
}

int design_test28_load_hero(int argc, char * argv[]) {
	int ret;
	design_hero_t hero_list[HERO_MAX];
	bzero(hero_list, sizeof(hero_list));
	ret = db_design_load_hero(hero_list);
	ERROR_NEG_RETURN(ret, "test_load_design_hero");

	return 0;
}

int design_test29_load_hero_slot(int argc, char * argv[]) {
	int ret;
	design_hero_slot_t hero_slot_list[HERO_MAX+1]; // +1 for 1-base array
	bzero(hero_slot_list, sizeof(hero_slot_list));
	ret = db_design_load_hero_slot(hero_slot_list);
	ERROR_NEG_RETURN(ret, "test_load_hero_slot_error");

	for (int i=0; i<=HERO_MAX; i++) {
		design_hero_slot_t &hero_slot = hero_slot_list[i];
		if (hero_slot.id == 0) continue;

		printf("hero_slot_list[%d], id=%d deck=%s \n", i
		, hero_slot.id, hero_slot.deck);
	}

	return 0;
}

int design_test30_load_design_achievement(int argc, char * argv[]) {
	int ret;
	int count;

	design_achi_t achi_list[MAX_ACHIEVEMENT];
	bzero(achi_list, sizeof(achi_list));
	ret = db_design_load_achievement(achi_list);
	ERROR_NEG_RETURN(ret, "test_load_design_achievement_error");

	count = 0;
	for (int i = 0; i < MAX_ACHIEVEMENT; i++) {
		design_achi_t &achi = achi_list[i];
		if (achi.aid == 0) {
			continue;
		}
		count ++;
	// aid, name, msg
	// pre, atype, p1, p2, p3
	// gold, crystal, card, piece
		DEBUG_PRINT(0, "achievement aid=%d name=%s msg=%s pre=%d atype=%d p1=%d p2=%d p3=%s gold=%d crystal=%d"
		, achi.aid, achi.name, achi.msg
		, achi.pre, achi.atype, achi.p1, achi.p2, achi.p3
		, achi.reward_gold, achi.reward_crystal);
		printf("reward_card_list = {");
		for (int cdx = 0; cdx < MAX_ACHIEVEMENT_REWARD_CARD; cdx++) {
			if (achi.reward_card[cdx] == 0) {
				continue;
			}
			printf("%d ", achi.reward_card[cdx]);
		}
		printf("}\n");
		printf("reward_piece_list = {");
		for (int cdx = 0; cdx < MAX_ACHIEVEMENT_REWARD_PIECE; cdx++) {
			if (achi.reward_piece[cdx] == 0) {
				continue;
			}
			printf("%d ", achi.reward_piece[cdx]);
		}
		printf("}\n");
	}
	DEBUG_PRINT(0, "test30:achievement_count=%d", count);

	return 0;
}

int design_test31_load_design_daily_login(int argc, char * argv[]) {
	int ret;
	int count;
	char tmp_buffer[2000];
	char *ptr;

	design_daily_login_t daily_login_list[MAX_DAILY_LOGIN+1];
	bzero(daily_login_list, sizeof(daily_login_list));
	ret = db_design_load_daily_login(daily_login_list);
	ERROR_NEG_RETURN(ret, "test_load_design_daily_login_error");

	count = 0;
	for (int i = 0; i <= MAX_DAILY_LOGIN; i++) {
		design_daily_login_t &daily_login = daily_login_list[i];
		if (daily_login.day == 0) {
			continue;
		}
		count ++;

		// day daily_reward[3]
		// daily_reward = log_time gold crystal card[10] piece[10][2]
		DEBUG_PRINT(0, "daily_login day[%d]", daily_login.day);
		for (int rdx = 0; rdx < MAX_DAILY_REWARD; rdx++)
		{
			design_daily_reward_t &daily_reward = daily_login.daily_reward[rdx];
			DEBUG_PRINT(0, "\tlog_time[%d] gold[%d] crystal[%d]"
			, daily_reward.log_time, daily_reward.gold, daily_reward.crystal);

			ptr = tmp_buffer;
			ptr += sprintf(ptr, "\tcards: ");
			for (int cdx = 0; cdx < MAX_DAILY_REWARD_CARD; cdx++)
			{
				if (daily_reward.cards[cdx] == 0) {
					break;
				}
				ptr += sprintf(ptr, "%d ", daily_reward.cards[cdx]);
			}
			DEBUG_PRINT(0, "%s", tmp_buffer);

			ptr = tmp_buffer;
			ptr += sprintf(ptr, "\tpieces: ");
			for (int pdx = 0; pdx < MAX_DAILY_REWARD_PIECE; pdx++)
			{
				if (daily_reward.pieces[pdx][0] == 0) {
					break;
				}
				ptr += sprintf(ptr, "%d[%d] ", daily_reward.pieces[pdx][0]
				, daily_reward.pieces[pdx][1]);
			}
			DEBUG_PRINT(0, "%s", tmp_buffer);

		}
	}
	DEBUG_PRINT(0, "test31:daily_login_count=%d", count);

	return 0;
}


int design_test32_load_design_arena_reward(int argc, char * argv[]) {
	int ret;
	int reward_count;
	design_arena_reward_t reward_list[MAX_ARENA_REWARD];
	bzero(&reward_list, sizeof(reward_list));
	ret = db_design_load_arena_reward(reward_list, reward_count);
	ERROR_NEG_RETURN(ret, "test_load_design_arena_reward_error");
	for (int i = 0; i < reward_count; i++) {
		design_arena_reward_t &reward = reward_list[i];
		DEBUG_PRINT(0, "load_design_arena_reward:id=%d start=%d end=%d gold=%d crystal=%d title=%s message=%s"
		, reward.id, reward.start, reward.end, reward.gold, reward.crystal
		, reward.title, reward.message);
	}

	return 0;
}

int design_test33_load_design_quick_reward(int argc, char * argv[]) {
	int ret;

	design_quick_reward_list_t reward_list[2];
	bzero(&reward_list, sizeof(reward_list));

	ret = db_design_load_quick_reward(reward_list);
	ERROR_NEG_RETURN(ret, "test_load_design_quick_reward_error");
	for (int i = 0; i < 2; i++) {
		design_quick_reward_list_t &reward = reward_list[i];
		DEBUG_PRINT(0, "load_design_quick_reward:max_weight=%d", reward.max_weight);
		for (int j=0; j<MAX_QUICK_REWARD; j++) {
			design_quick_reward_t &r = reward.reward_array[j];
			DEBUG_PRINT(0, "type=%d count=%d weight_start=%d weight_end=%d"
			, r.type, r.count, r.weight_start, r.weight_end);
		}
	}

	return 0;
}


int design_test34_load_design_chapter_dialog(int argc, char * argv[]) {
	int ret;

	design_chapter_dialog_t dialog_list[MAX_CHAPTER+1];
	bzero(&dialog_list, sizeof(dialog_list));

	ret = db_design_load_chapter_dialog(dialog_list);
	ERROR_NEG_RETURN(ret, "test_load_design_chapter_dialog_error");
	for (int i = 0; i <= MAX_CHAPTER; i++) {
		for (int j = 0; j <= MAX_CHAPTER_STAGE; j++) {
			design_stage_dialog_t &dialog = dialog_list[i].stage_dialog_list[j];
			if (dialog.dialog[0] == '\0') {
				continue;
			}
			DEBUG_PRINT(0
			, "load_design_chapter_dialog:chapter[%d] stage[%d] dialog[%s] count[%d]"
			, i, j, dialog.dialog, dialog.dialog_count);
		}
	}

	return 0;
}



/////////////////////////////////////////////////
typedef int (*testcase_t) (int, char*[]); //  testcase_t;

testcase_t design_test_list[] = {
	design_test0_shop // db_design_load_shop
, 	design_test1_ai
, 	design_test2_pick
, 	design_test3_constant
,	design_test4_load_notice
,	design_test5_load_exp
,	design_test6_load_std_deck
,	design_test7_load_design_guild
,	design_test8_load_design_merge
,	design_test9_load_design_pay
,	design_test10_load_design_version
,	design_test11_load_design_website
,	design_test12_load_design_mission
,	design_test13_load_design_slot
,	design_test14_load_match
,	design_test15_load_match_player
,	design_test16_load_design_rank_reward
,	design_test17_load_design_rank_time
,	design_test18_load_design_lottery
,	design_test19_load_design_gate
,	design_test20_load_design_fight_schedule
,	design_test21_load_design_gate_msg
,	design_test22_load_design_tower_reward
,	design_test23_load_design_piece_shop
,	design_test24_load_solo
,	design_test25_load_robot
,	design_test26_load_chapter
,	design_test27_load_hero_mission
,	design_test28_load_hero
,	design_test29_load_hero_slot
,	design_test30_load_design_achievement
,	design_test31_load_design_daily_login
,	design_test32_load_design_arena_reward
,	design_test33_load_design_quick_reward
,	design_test34_load_design_chapter_dialog
};


int test_selector(int argc, char *argv[])
{
	int testmax = sizeof(design_test_list) / sizeof(design_test_list[0]);
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
	ret = design_test_list[testcase](argc, argv);
	
	printf("RET %d\n", ret);
	if (ret < 0) {
		printf("XXXXXXXXX BUG ret < 0: %d\n", ret);
	}
	
	return ret;
}


////////// DB TEST END /////////

int main(int argc, char *argv[])
{
	int ret;
	// char str[100];

	ret = mysql_library_init(0, NULL, NULL); // argc, argv, char**group
	db_design_init();  // @see db_design_clean()
	assert(NULL!=g_db_conn);

	// test case after mysql conn is initialized !
	if (argc > 1 && strcmp("all", argv[1])==0) {
		// loop all
		int testmax = sizeof(design_test_list) / sizeof(design_test_list[0]);
		int error_count = 0;
		for (int i=0; i<testmax; i++) {
			printf("RUN test%d:\n", i);
			ret = design_test_list[i](argc, argv);
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
	db_design_clean();
	return ret;
}

#endif  //TTT

