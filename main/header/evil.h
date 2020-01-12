
#ifndef __EVIL_H__
#define __EVIL_H__

#include <time.h>
#include <iostream>
//#include <vector>
//#include <map>

using namespace std;

///////// CONSTANT START //////////
// @see db-init.sql
// declare: char username[USERNAME_MAX+1]  escape: [USERNAME_MAX*2 + 1]
#define EVIL_USERNAME_MAX	30
#define EVIL_PASSWORD_MAX	30
#define EVIL_ALIAS_MAX		30
#define EVIL_ADDRESS_MAX	30

#define EVIL_SIGNAL_MAX		30

#define EVIL_UID_MAX		400

#define EVIL_SIGNATURE_MAX	300
#define EVIL_ICON_MAX		100

// declare: char card[CARD_MAX+1]   loop: for(i=1; i<=CARD_MAX; i++)
#define EVIL_CARD_MAX	400
#define EVIL_CMD_MAX	4000	//@see TABLE evil_replay.cmd; IN_SAVE_REPLAY_PRINT
#define EVIL_PLAYER_MAX	2	// in one game, max number of players

// max number of card with same id
#define EVIL_NUM_CARD_MAX	9
#define EVIL_NUM_DECK_MAX	4
#define EVIL_NUM_PIECE_MAX	99
#define EVIL_DECK_CARD_MIN	10

#define EVIL_HERO_SLOT_MAX	10
#define HERO_SLOT_RECOMMAND	99

// evil_notice
#define EVIL_MESSAGE_TITLE_MAX	100
#define EVIL_MESSAGE_MAX			300

// replay command max (10 * 200)

#define MAX_LEVEL	100 // to init g_design->exp_list, real max level is g_design->max_level

#define HERO_MAX	20

#define MAX_AI_EID		499 	// max hero id for AI
#define ROBOT_START_EID	570

#define MAX_NOTICE 		10
#define MAX_TITLE_SIZE	310 //40
#define MAX_NOTICE_SIZE	1510


#define MAX_PAY_NUM	100
#define MAX_PAY_DESCRIPTION_SIZE	1510

#define MAX_WEBSITE_NUM	50

#define MAX_LOCAL_NAME 2000
#define NAME_SIZE		14

#define MAX_SLOT_NUM	100
#define SLOT_START_ID	3000 // For evil_buy:record

#define MAX_RANK_REWARD	15
#define MAX_RANK_TIME	10

#define MAX_LOTTERY_LIST	10

#define MAX_GATE_LIST	200
#define MAX_GATE_TITLE	305
#define GATE_MSG_SIZE	2000
#define MAX_USER_POWER	90
#define POWER_RECOVER_SEC	300

#define MAX_TOWER_REWARD	15

#define MAX_PIECE_SHOP_LIST			300
#define MAX_PIECE_SHOP_SLOT			6
#define MAX_PIECE_SHOP_HARD_SHOW	10


#define DB_BUFFER_MAX	(EVIL_CARD_MAX*2 + 100 + EVIL_CMD_MAX) // around 4900
// max transaction can be queued
#define DB_TRANS_MAX	1000

#define REPLAY_PARAM_MAX	200

#define MAX_PICK	2	// gold and crystal
#define MAX_LOC		6	// how many cards display at one batch
#define MAX_STAR	5	

// guild related:
#define GUILD_POS_NONE		0  // no guild
#define GUILD_POS_MASTER	1
#define GUILD_POS_SENIOR	2
#define GUILD_POS_MEMBER	3
#define GUILD_POS_APPLY		9

#define MAX_GUILD_MEMBER	100 // TODO check buffer size

#define MAX_LADDER	10
#define LADDER_RATING		0
#define LADDER_LEVEL		1
#define LADDER_GUILD		2
#define LADDER_COLLECTION	3
#define LADDER_GOLD			4
#define LADDER_CHAPTER		5


#define RANDOM_TEAM_MAX		20
#define MAX_GIFT			20
#define GIFT_ITEM_MAX		10
//#define MAX_RANDOM_TEAM		100

#define FIGHT_AUTO_ROBOT_TIME	50 // 50 second

#define FIGHT_SCHEDULE_MAX		20

#define FIGHT_AI_MAX_TIME		6
#define FIGHT_GOLD_MAX_TIME		3
#define FIGHT_CRYSTAL_MAX_TIME	3

#define FIGHT_STATUS_OPEN		1
#define FIGHT_STATUS_CLOSE		0

#define FIGHT_FOLD_HERO_HP		5


#define FIGHT_SIGNAL_AI_EID		1

#define SOLO_TYPE_NORMAL		0 // random deck
#define SOLO_TYPE_SPECIAL		1 // order deck

#define MAX_SOLO_TARGET				3

#define MAX_CHAPTER						20 // base 1
#define MAX_CHAPTER_STAGE				20 // base 1
// #define MAX_CHAPTER_TARGET				3
#define MAX_CHAPTER_AI					3
#define MAX_CHAPTER_REWARD				4
#define MAX_CHAPTER_TIPS				3
#define CHAPTER_DATA_STAR_0					'0'
#define CHAPTER_DATA_START					'8'
#define CHAPTER_DATA_LOCK					'9'

#define CHAPTER_DATA_STATUS_STAR_0				0
#define CHAPTER_DATA_STATUS_STAR_1				1
#define CHAPTER_DATA_STATUS_STAR_2				2
#define CHAPTER_DATA_STATUS_STAR_3				3
#define CHAPTER_DATA_STATUS_START				8
#define CHAPTER_DATA_STATUS_LOCK				9

#define CHAPTER_REWARD_GOLD					1
#define CHAPTER_REWARD_CRYSTAL				2
#define CHAPTER_REWARD_PIECE				3
#define CHAPTER_REWARD_CARD					4
#define CHAPTER_REWARD_EXP					5
#define CHAPTER_REWARD_POWER				6


#define CHAPTER_TARGET_POINT_NULL					99
#define CHAPTER_TARGET_P2_TYPE_SAME					0
#define CHAPTER_TARGET_P2_TYPE_LESS					1
#define CHAPTER_TARGET_P2_TYPE_MORE					2

///////////////////////////////////////////
// SOLO TARGET
#define CHAPTER_TARGET_MY_HERO_HP					1
#define CHAPTER_TARGET_ROUND						2
#define CHAPTER_TARGET_WIN							3
#define CHAPTER_TARGET_MY_HAND_ALLY					4 // hand to ally counter
#define CHAPTER_TARGET_MY_HAND_SUPPORT				5 // hand to support counter
///////////////////////////////////////////
#define CHAPTER_TARGET_MY_HAND_ABILITY				6 // hand play ability counter
#define CHAPTER_TARGET_MY_CARD						7
#define CHAPTER_TARGET_OPPO_ALLY					8 // end game ally counter
#define CHAPTER_TARGET_OPPO_SUPPORT					9 // end game support counter
#define CHAPTER_TARGET_OPPO_ABILITY					10 // hand play ability counter
///////////////////////////////////////////
#define CHAPTER_TARGET_OPPO_CARD					11
#define CHAPTER_TARGET_MY_GRAVE						12 // end game my grave counter
#define CHAPTER_TARGET_OPPO_GRAVE					13 // end game oppo grave counter
#define CHAPTER_TARGET_MY_GRAVE_ALLY				14 // end game my grave ally counter
#define CHAPTER_TARGET_OPPO_GRAVE_ALLY				15 // end game oppo grave ally counter
///////////////////////////////////////////
#define CHAPTER_TARGET_MY_CARD_GRAVE                16 // end game my grave p1 card counter
#define CHAPTER_TARGET_OPPO_CARD_GRAVE              17 // end game oppo grave p1 card counter
#define CHAPTER_TARGET_MY_HERO 		                18 // my hero p1 card counter
#define CHAPTER_TARGET_OPPO_HERO                    19 // oppo hero p1 card counter

#define MAX_HERO_MISSION				50
#define HERO_MISSION_TYPE_CHAPTER		1


#define MAX_CARD_CHAPTER				5

#define MAX_QUICK_REWARD				4
#define QUICK_REWARD_TYPE_GOLD			1
#define QUICK_REWARD_TYPE_CRYSTAL		2
#define QUICK_AUTO_ROBOT_TIME			60 // 60 second
#define QUICK_DECK_CARD_MIN				40
#define QUICK_BROADCAST_MSG				"玩家于决斗之城中向世界发起挑战"


///////////////////////////////////////////
// room game type, for win_game_income()
#define GAME_SOLO		1
#define GAME_QUICK		2
#define GAME_ROOM		3
#define GAME_CHALLENGE	4
#define GAME_MATCH		5 // TODO update win_game_income() for match game

#define GAME_RANK		6
#define GAME_GATE		7
#define GAME_SOLO_GOLD	8
#define GAME_VS_GOLD	9
#define GAME_VS_CRYSTAL	10

#define GAME_SOLO_FREE	11
#define GAME_VS_FREE	12
#define GAME_TOWER		13
#define GAME_SOLO_PLUS	14
#define GAME_CHAPTER	15

#define GAME_ARENA		16

#define MAX_ROOM_TYPE	20
///////////////////////////////////////////


///// SIGNAL START /////

#define SIGNAL_FIGHT_AI				1
#define SIGNAL_FIGHT_AI_FREE		2
#define SIGNAL_LOTTERY_GOLD_ONE		3
#define SIGNAL_LOTTERY_CRYSTAL_ONE	4


///// SIGNAL END /////

#define PAY_FIRST_CHARGE_GOLD		20000
#define MAX_PAY_MONTHLY				30
#define MAX_FIRST_VIP_CARD_KIND		10
#define PAY_STATUS_OK				0
#define PAY_STATUS_FIRST_VIP		1

#define ROBOT_TYPE_NORMAL		0
#define ROBOT_TYPE_FAKEMAN		1

#define EVIL_BUY_CARD					0
#define EVIL_BUY_PIECE					1000 // in_piece_buy()
#define EVIL_BUY_REFRESH_PSHOP			2001 // in_refresh_piece_shop()
#define EVIL_BUY_CREATE_GUILD			2002 // in_create_guild()
#define EVIL_BUY_GUILD_DEPOSIT			2003 // in_guild_deposit()
#define EVIL_BUY_ARENA_TIMES			2004 // in_update_arena_times()


///////// CONSTANT END //////////

///////// ERROR MESSAGE START //////////
#define E_MONEY_NOT_ENOUGH	"E0001:金钱不足"
// register
#define E_REGISTER_LESS_INPUT	"E0101:输入错误"
#define E_REGISTER_INVALID_NAME	"E0102:非法用户名"
#define E_REGISTER_DUPLICATE_USERNAME 	"E0103:账户名已被注册"
#define E_REGISTER_SAVE_STATUS 	"E0104:保存信息失败"
#define E_REGISTER_SAVE_PIECE 	"E0105:保存碎片失败"
// login
#define E_LOGIN_LESS_INPUT	"E0201:输入错误"
#define E_LOGIN_NULL_RESULT	"E0202:没有记录"
#define E_LOGIN_INCORRECT	"E0203:账号或密码错误"
// load_card
#define E_LOAD_CARD_EMPTY_ROW	"E0301:读取卡牌错误"
// load_deck
#define E_LOAD_DECK_EMPTY_ROW	"E0401:读取上阵卡牌错误"
// save_card
#define I_SAVE_CARD_OK	"I0501:保存卡牌成功"
// save_deck
#define E_SAVE_DECK_COUNT_LESS	"E0601:上阵卡牌数量需要大于30张"
// alias
#define E_ALIAS_DUPLICATE	"E0701:名称重复了"
// game
#define E_GAME_NULL_ROOM	"E0801:没有房间"
#define E_GAME_ONLY_MASTER_START	"E0802:只有房主能开始游戏"
#define E_GAME_LESS_PLAYER	"E0803:玩家数量不足"
#define E_GAME_OPPO_OFFLINE	"E0804:对手离线"
#define E_GAME_ALREADY_START	"E0805:游戏已经开始"
#define E_GAME_INIT_ERROR	"E0806:游戏初始化失败"
// buy_card
#define E_BUY_CARD_COUNT_OUT_BOUND	"E1701:已拥有卡牌数量达到最大值"
#define E_BUY_CARD_FAIL	"E1702:购买卡牌失败"
// buy_piece
#define E_BUY_PIECE_COUNT_OUT_BOUND	"E1703:已拥有碎片数量达到最大值"
#define E_BUY_PIECE_FAIL	"E1704:购买碎片失败"
// sell_card
#define E_SELL_CARD_COUNT_OUT_BOUND	"E1801:没有空闲卡牌可以出售"
#define E_SELL_CARD_FAIL	"E1802:出售卡牌失败"
#define E_SELL_CARD_DECK	"E1803:不能出售手牌"
// sell_piece
#define E_SELL_PIECE_COUNT_OUT_BOUND	"E1803:没有碎片可以出售"
#define E_SELL_PIECE_FAIL	"E1804:出售碎片失败"
// load_batch
#define E_LOAD_BATCH_NO_BATCH	"E2001:没有可抽取卡牌"
// pick
#define E_PICK_MONEY_NOT_ENOUGH	"E2201:金钱不足"
// add_exchange
#define E_ADD_EXCHANGE_PIECE_NOT_ENOUGH	"E2301:没有足够的碎片可交易"
// buy_exchange
#define E_BUY_EXCHANGE_CANNOT_BUY_MYSELF	"E2401:不能购入自己出售的碎片"
#define E_BUY_EXCHANGE_BUY_TOO_MUCH	"E2402:购买碎片超过出售碎片数量"
#define E_BUY_EXCHANGE_PIECE_OUT_BOUND	"E2403:没有足够空间存放购买的碎片"
#define E_BUY_EXCHANGE_MONEY_NOT_ENOUGH	"E2404:金钱不足"
// create_guild
#define E_CREATE_GUILD_EID_NOT_FOUND "E2801:找不到id"
#define E_CREATE_GUILD_ALREADY_HAS_GUILD "E2802:已存在公会"
#define E_CREATE_GUILD_DUPLICATE_GUILD_NAME "E2803:已存在的公会名称"
#define E_CREATE_GUILD_OTHER_ERROR "E2804:其它错误"
#define E_CREATE_GUILD_MONEY_NOT_ENOUGH "E2804:金钱不足"
// guild_apply
#define E_GUILD_APPLY_GUILD_NOT_EXIST	"E3001:公会不存在"
#define E_GUILD_APPLY_ALREADY_HAS_GUILD	"E3002:已经加入一个公会,不能再申请加入公会"
// guild_pos
#define E_GUILD_POS_MASTER_CANNOT_CHANGE_POS	"E3101:会长不能更改位置"
#define E_GUILD_POS_TOTAL_MEMBER_OVERFLOW	"E3102:公会人数超过最大值"
#define E_GUILD_POS_OTHER_ERROR	"E3103:其它错误"
// guild_quit
#define E_GUILD_QUIT_NOT_IN_GUILD	"E3201:不在公会中"
#define E_GUILD_QUIT_MASTER_CANNOT_QUIT	"E3202:会长不能退出公会"
// guild_del
#define E_GUILD_DEL_GUILD_NOT_EXIST	"E2901:公会不存在"
// guild_deposit
#define E_GUILD_DEPOSIT_MONEY_NOT_ENOUGH	"E3401:金钱不足"
// guild_bonus
#define E_GUILD_BONUS_ALREADY_GET	"E3501:已经获得过奖励"
// guild
#define E_GUILD_NOT_FOUND	"E4401:公会不存在"
// glv
#define E_GLV_GUILD_NOT_FOUND	"E4601:公会不存在"
// update_profile
#define E_UPDATE_PROFILE_INVALID_ICON	"E3901:非法头像"
#define E_UPDATE_PROFILE_INVALID_SEX	"E3901:非法性别"
// add_friend
#define E_ADD_FRIEND_ALREADY_FRIEND 	"E4001:已经是好友"
// friend_sta
#define E_FRIEND_STA_SAME_EID	"E4201:不能查看自己"
// guild_level_up
#define E_GLEVELUP_MAX_LEVEL "E4701:公会等级已到达最高级"
// load_piece
#define E_LOAD_PIECE_EMPTY_ROW	"E4801:找不到碎片"
// merge_piece
#define E_MERGE_PIECE_PIECE_NOT_ENOUGH		"E5001:碎片不足"
#define E_MERGE_PIECE_CARD_OUT_BOUND		"E5002:没有足够空间存放卡牌"
#define E_MERGE_PIECE_MONEY_NOT_ENOUGH		"E5003:金钱不足"
#define E_MERGE_PIECE_CRYSTAL_NOT_ENOUGH	"E5004:水晶不足"
// pick_piece
#define E_PICK_PIECE_MONEY_NOT_ENOUGH	"E4901:金钱不足"
#define E_PICK_PIECE_UPDATE_PIECE_FAIL	"E4902:更新碎片失败"
// del_friend
#define E_DEL_FRIEND_NOT_FRIEND		"E7101:不是好友"


// normal err, not db
// cmd_room
#define E_CMD_ROOM_NULL_ROOM	"EN0001:没有房间"
#define E_CMD_ROOM_HAS_ROOM		"EN0002:已经在房间中"
#define E_CMD_ROOM_NO_SUCH_ROOM	"EN0003:没有这个房间"
#define E_CMD_ROOM_MAX_GUEST	"EN0004:房间人数已满员"
#define E_CMD_ROOM_HAS_PASSWORD	"EN0005:房间有密码"
#define E_CMD_ROOM_ENTER_ROOM_ERROR	"EN0006:进入房间失败"
#define E_CMD_ROOM_PASSWORD_ERROR	"EN0007:非法密码"
#define E_CMD_ROOM_PASSWORD_OUT_BOUND	"EN0008:密码过长"
#define E_CMD_ROOM_PASSWORD_INCORRECT	"EN0009:密码不正确"
// cmd_wchat
#define E_CMD_WCHAT_MSG_TOO_LONG	"EN0101:信息长度太长"
// guild_chat
#define E_GUILD_CHAT_NOT_IN_GUILD	"EN0201:不在公会中"
#define E_GUILD_CHAT_MSG_TOO_LONG	"EN0202:信息长度太长"
// friend_chat
#define E_FRIEND_CHAT_FRIEND_NOT_LOGIN	"EN0301:对方没有在线"
// room_kick
#define E_ROOM_KICK_ONLY_MASTER_CAN_KICK	"EN0401:只有房主可以踢人"
#define E_ROOM_KICK_PLAYER_CANNOT_KICK	"EN0402:游戏中不能踢出玩家"
#define E_ROOM_KICK_NO_SUCH_GUY	"EN0403:没有这个房客"
// room_leave
#define E_ROOM_LEAVE_NULL_ROOM	"EN0501:没有房间"
#define E_ROOM_LEAVE_IN_GAME	"EN0502:游戏中不能离开房间"
// room_chat
#define E_ROOM_CHAT_NULL_ROOM	"EN0601:没有房间"
#define E_ROOM_CHAT_MSG_TOO_LONG	"EN0602:信息长度太长"
// room_game
#define E_ROOM_GAME_NULL_ROOM	"EN0701:没有房间"
#define E_ROOM_GAME_ONLY_MASTER_START	"EN0702:只有房主能开始游戏"
#define E_ROOM_GAME_LESS_PLAYER	"EN0703:玩家数量不足"
#define E_ROOM_GAME_OPPO_OFFLINE	"EN0704:对手离线"
#define E_ROOM_GAME_ALREADY_START	"EN0705:游戏已经开始"
#define E_ROOM_GAME_INIT_ERROR	"EN0706:游戏初始化失败"
// game_reconn
#define E_GAME_RECONN_NULL_ROOM	"EN0801:没有房间"

// challenge
#define E_CHALLENGE_IN_ROOM "EN0901:你已经在游戏中"
#define E_CHALLENGE_OPPO_OFFLINE "EN0902:对手离线"
#define E_CHALLENGE_OPPO_IN_ROOM "EN0903:对手正在游戏中"

// game_fold
#define E_GAME_FOLD_HP_TOO_MUCH "EN1001:英雄血量大于10不能投降喔，继续战斗吧少年!"

// mission
#define E_LOAD_MISSION	"EN1101:读取任务错误"
#define E_MISSION_NOT_REACH	"EN1102:任务目标没有达成"

#define E_RETURN_CHECK_DECK_OVER_MAX		-202
#define E_RETURN_CHECK_DECK_LESS_MIN		-212
#define E_RETURN_CHECK_DECK_LESS_QUICK_MIN	-222

#define E_CHECK_DECK_OVER_MAX	"EN1201:卡牌数超过最大值"
#define E_CHECK_DECK_LESS_MIN	"EN1202:卡牌数小于10张"
#define E_CHECK_DECK_LESS_QUICK_MIN	"EN1203:卡牌数小于40张"


///////// ERROR MESSAGE END //////////

///////// SYSTEM MESSAGE START //////////

// #define SYS_WCHAT_GET_CARD	"wchat 1501 系统 %d 恭喜玩家[%s]获得%d星卡牌-%s"
#define SYS_WCHAT			"wchat 1501 系统 0 %d "
#define SYS_WCHAT_GET_CARD	"恭喜玩家[%s]获得%d星卡牌-%s"
///////// SYSTEM MESSAGE END //////////


///////// TYPEDEF START /////////


// @see db-init.sql : TABLE evil_mission
// peter: 2014-11-26 : only store the variable in mission_t
// NOTE: order is important:  mid, status, n1, status
// @see mission.cpp for test case for this order
// TABLE evil_mission has the same order (after db_update20141126.sql)
typedef struct _mission_struct {
	int mid;		// this is a key within a user eid
	int status;		// @see MISSION_STATUS_NULL
	int n1;  // refer to the progress of p1
	time_t last_update; // DATETIME map to seconds (variable)
} mission_t;


#define MAX_MISSION	501	// 201 // maximum 201 mission ( 1 to 200 ok, 0 spare)

// mission_t.mtype
#define MISSION_LEVEL	1		// lv up to p1
#define MISSION_AI		2		// win ai p1 time, p2=ai_id, p3=my_hero
#define MISSION_VS		3		// win VS game p1 time, p3=my_hero
#define MISSION_CHALLENGE		4	// win challenge p1 time
#define MISSION_BEI_CHALLENGE	5	// win passive challenge p1 time
#define MISSION_REPLAY	6		// press replay p1 time 
#define MISSION_CHAT  	7		// chat p1 time (p2=1,2,3=world,room,guild)
#define MISSION_FRIEND	8		// add p1 friends (not total?)
#define MISSION_SHOP  	9		// buy p1 cards p2=card_id
#define MISSION_CARD  	10		// own p1 card p2=card_id
#define MISSION_COLLECTION  	11	// own p1 unique cards
#define MISSION_PROMOTION  		12	// promote this game to p1 users
#define MISSION_VIEW            13  // view other game p1 time p2=hero_id
#define MISSION_GUILD           14  // p1 = times
#define MISSION_DECK           	15  // p1 = count, p2 = card_id
#define MISSION_GATE			16	// p1 = times, p2 = gate_id, p2 = 0 means any
#define MISSION_FIGHT_AI		17	// p1 = times
#define MISSION_FIGHT_VS		18	// p1 = times
#define MISSION_FIGHT			19	// p1 = times
#define MISSION_MONTHLY			20	// daily mission, auto finish
#define MISSION_RANK_GAME_TIMES	21	// 
#define MISSION_CHAPTER_STAGE	22	// p1 = star, p2 = chapter_id, p3=stage_id
#define MISSION_CHAPTER			23	// p1 = stage_count, p2 = chapter_id, p3 = star
#define MISSION_QUICK			24	// p1 = times
#define MISSION_QUICK_WIN		25	// p1 = times
#define MISSION_HERO_HP			26	// p1 = hp, p2 = hero_id

// update flag is bit-wise, can be 1 | 2 | 4
// client only need 1 | 2 : net_writeln("mchange %d", mchange & 3); 
#define MISSION_UP_NEW	1	// new mission
#define MISSION_UP_OK	2	// some mission is OK, can collect reward
#define MISSION_UP_NUM	4	// update a n1 only, no other change

#define MAX_MISSION_REWARD_CARD		10
#define MAX_MISSION_REWARD_PIECE	10


// mission_t.status
#define MISSION_STATUS_NULL		0 // level or pre not enough to start
#define MISSION_STATUS_READY	1 // we have this mission
#define MISSION_STATUS_OK		2 // target reach, not yet get reward
#define MISSION_STATUS_FINISH	3 // get reward already

#define MLIST_TYPE_ALL				0
#define MLIST_TYPE_CHAPTER			1
#define MLIST_TYPE_WITHOUT_CHAPTER	2


//typedef map<int,mission_t> MISSION_MAP;


#define MAX_ACHIEVEMENT				100
#define ACHIEVEMENT_LV				1
#define ACHIEVEMENT_CARD_LIST		2
#define ACHIEVEMENT_CHAPTER			3
#define ACHIEVEMENT_MERGE			4

#define ACHIEVEMENT_STATUS_NULL		0 // level or pre not enough to start
#define ACHIEVEMENT_STATUS_READY	1 // we have this mission
#define ACHIEVEMENT_STATUS_OK		2 // target reach, not yet get reward
#define ACHIEVEMENT_STATUS_FINISH	3 // get reward already

#define MAX_ACHIEVEMENT_REWARD_CARD		10
#define MAX_ACHIEVEMENT_REWARD_PIECE	10



// daily_login
#define MAX_DAILY_REWARD			3
#define MAX_DAILY_LOGIN				7
#define MAX_DAILY_REWARD_CARD		10
#define MAX_DAILY_REWARD_PIECE		10


// match status
#define MATCH_STATUS_READY			0
#define MATCH_STATUS_ROUND_START	1
#define MATCH_STATUS_ROUND_END		2
#define MATCH_STATUS_FINISHED		3
#define MATCH_STATUS_DELETE			9

// #define MAX_TEAM_ROUND			2
#define MAX_MATCH				10
#define MAX_MATCH_PLAYER		1024
#define MAX_TEAM_PLAYER			4
#define MAX_TEAM_ROUND			3
#define MAX_DAILY_ROUND			4
#define MATCH_ROUND_TIME		2100 //40 // second TODO move to design_constant
#define MATCH_CHECK_DURATION	10 // second

#define MATCH_DATA_TEAM_FLAG	0
#define MATCH_DATA_ELI_FLAG		1


typedef struct _evil_match_player_struct {
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
	char alias[EVIL_ALIAS_MAX + 1];
} match_player_t;


typedef struct _evil_match_struct {
	long match_id;
	char title[105];
	int max_player; // match can has the max member count
	int max_team; // match can has the max member count
	time_t start_time;
	time_t round_time_list[MAX_DAILY_ROUND]; // time for round start, max round per day is 4
	int status;		// wether the macth is ready/running/finished
	int round;		// now round, base 0, only nio update, not work in match.cpp, first round = 1
	match_player_t player_list[MAX_MATCH_PLAYER * MAX_TEAM_ROUND]; // team match player list
	match_player_t e_player_list[MAX_MATCH_PLAYER]; // elimination match player list
} match_t;


#define MAX_RANKING_LIST			50
#define RANKING_RANDOM_COUNT		5
#define MAX_RANKING_RANDOM_RANGE	25
#define RANKING_HISTORY_COUNT		10
#define MAX_RANKING_CHALLENGE_TIME	5

#define RANKING_CHALLENGE_TIMEOUT	10

#define RANKING_CHALLENGE_ACCEPT			1
#define RANKING_CHALLENGE_STATUS_WAITING	0
#define RANKING_CHALLENGE_STATUS_ACCEPT		1
#define RANKING_CHALLENGE_STATUS_REFUSE		2

#define MAX_ARENA_TOP_LIST					10
#define MAX_ARENA_TARGET					5
#define ARENA_GET_TARGET_BY_ORDER_RANK		20
#define ARENA_AUTO_RANK						200
#define ARENA_TIMES_STD						5
#define ARENA_TIMES_BUY_COUNT				5
#define ARENA_TIMES_BUY_CRYSTAL				50
#define MAX_ARENA_REWARD					15

#define GAME_PLAY_FREE						0
#define GAME_PLAY_AUTO						1

typedef struct {
	int hero_id;
	int mission_id;
	int status;
	int n1;
} evil_hero_mission_t;

typedef struct {
	int hero_id;
	int hp;
	int energy;
} evil_hero_t;

typedef struct {
	evil_hero_t hero;
	evil_hero_mission_t mission_list[MAX_HERO_MISSION+1];
} evil_hero_data_t;

typedef struct {
	int id;
	char data[MAX_CHAPTER_STAGE+1];
} evil_chapter_data_t;

typedef struct {
	int aid;
	int status;
	int n1;
} evil_achi_t;

typedef struct {
	int log_day;
	int load_from_db;
	int reward_day[MAX_DAILY_LOGIN+1];
} evil_daily_login_t;

typedef struct {
	
	time_t last_time; // last refresh piece shop time
	int pid_list[MAX_PIECE_SHOP_SLOT]; // save design_piece_shop.id
	int buy_flag_list[MAX_PIECE_SHOP_SLOT]; // buy flag, 0 can buy, 1 already buy

	int refresh_times; // daily refresh piece shop times
	int show_times; // total refresh piece shop times

} evil_piece_shop_t;

// @see db-init.sql : TABLE evil_user + evil_status
typedef struct _evil_user_struct {
	int eid;
	char username[EVIL_USERNAME_MAX + 1];
	char password[EVIL_PASSWORD_MAX + 1];
	char alias[EVIL_ALIAS_MAX + 1];
	time_t last_login;  // SQL DATETIME , strftime, strptime

	evil_daily_login_t daily_login;

	// -- below are evil_status specific
	int icon;
	int sex;
	char signature[EVIL_SIGNATURE_MAX+1];

	// level rating etc
	int lv;		// lv = 0 means it is not initialized
	double rating;
	int exp;

	// money related
	int gold;
	int crystal;

	// gid,gpos = reserve1, reserve2 
	int gid;
	int gpos;

	// count for auto-match only
	int game_count;
	int game_win;
	int game_lose;
	int game_draw;
	int game_run;

	// fight
	int fight_ai_time;
	int fight_gold_time;
	int fight_crystal_time;

	char signals[EVIL_SIGNAL_MAX + 1];

	// course step
	int course;

	// power for gate
	// ONLY USE IN dbio
	double power; // DO NOT USE IN nio
	long power_set_time; // NO NOT USE IN nio

	int gate_pos;

	int tower_pos;
	int tower_times;
	long tower_set_time;

	int battle_coin;

	int chapter_pos; // now chapter_pos use evil_chapter MAX(chapter_id), evil_status.chapter_pos is useless
	int arena_times;

	long arena_last_reward_time;

	char gname[EVIL_ALIAS_MAX + 1]; // guild name

	int batch_list[MAX_PICK][MAX_LOC];

	char deck[EVIL_CARD_MAX + 2];  // +1 is enough, for safety
	char card[EVIL_CARD_MAX + 2];  // +1 is enough, for safety

	mission_t mission_list[MAX_MISSION];  // no total

	time_t monthly_end_date;

	// useless
	// for win chapter stage get random reward
	// int reward_chapter;
	// int reward_stage;

	evil_hero_data_t hero_data_list[HERO_MAX+1];

	int flag_load_chapter; // if already load chapter data, flag==1, set in out_chapter_data()
	evil_chapter_data_t chapter_list[MAX_CHAPTER+1]; // base 1

	int merge_times;
	evil_achi_t achi_list[MAX_ACHIEVEMENT+1];

	evil_piece_shop_t piece_shop;

} evil_user_t;


typedef struct _game_struct {
	// note: state == 999 is a bug!
	long gameid; // room creation time @see get_usec
	int win;
	int eid1;
	int eid2;
	int seed;  
	int ver;

	// if deck[0][0] == '\0', deck[0] is empty, 
	// else deck[0][0] is either '0' or '1', deck is read
	// EVIL_CARD_MAX + 1 for reserve[0],  EVIL_CARD_MAX+2 for null-term '\0'
	char deck[EVIL_PLAYER_MAX][EVIL_CARD_MAX + 2]; 

	char cmd[3000]; // this is rather large
} game_t;

typedef struct {
	int eid;
	double rating;
	int gold;
	int crystal;
	int exp;
	int card_id;
	int rate_offset;

	int ai_times;
	int gold_times;
	int crystal_times;
	int cost_gold;
	int cost_crystal;

	int chapter_reward_gold;
	int chapter_reward_crystal;
	int chapter_reward_piece;
	int chapter_reward_card;
	int chapter_reward_exp;
	int chapter_reward_power;
} game_income_t;


// for db_design @see nio.cpp : g_constant,  load_design
typedef struct {

	int batch_refresh_gold;  // 抽卡换一批所需金币 for ptype=0, refresh batch
	int pick_gold;  // 抽卡金币 for picking inside a batch, ptype=0
	int batch_refresh_crystal; // 抽卡换一批所需水晶
	int pick_crystal; // 水晶抽卡的水晶
	int max_timeout; // 对战回合时间限制, 秒

	double guild_bonus_rate; // 公会福利百分比
	int create_guild_gold;  // 创建公会所需金币
	int create_guild_crystal;  // 创建公会所需水晶

	int lottery_one_gold;
	int lottery_one_crystal;
	int lottery_ten_gold;
	int lottery_ten_crystal;

	int first_pay_double;
	int monthly_increase_date;

	int win_gold[MAX_ROOM_TYPE];
	int cost_gold[MAX_ROOM_TYPE];
	int lose_gold[MAX_ROOM_TYPE];
	int draw_gold[MAX_ROOM_TYPE];
	int fold_gold[MAX_ROOM_TYPE];
	int win_crystal[MAX_ROOM_TYPE];
	int cost_crystal[MAX_ROOM_TYPE];
	int lose_crystal[MAX_ROOM_TYPE];
	int draw_crystal[MAX_ROOM_TYPE];
	int fold_crystal[MAX_ROOM_TYPE];
	int win_exp[MAX_ROOM_TYPE];
	int lose_exp[MAX_ROOM_TYPE];
	int draw_exp[MAX_ROOM_TYPE];
	int fold_exp[MAX_ROOM_TYPE];

	int pshop_reset_interval;
	int pshop_refresh_gold;
	int pshop_max_refresh_gold;

	int exchange_crystal_gold;

	int first_vip_extra_gold;
	int first_vip_extra_crystal;
	int first_vip_extra_card_list[MAX_FIRST_VIP_CARD_KIND][2]; //[0]card_id [1]count
	int first_vip_extra_card_kind;

	int quick_robot_flag;

} constant_t ;

// for db_design
typedef struct {
	int card_id;
	int card_buy_gold;
	int card_sell_gold;
	int card_buy_crystal;
	int card_sell_crystal;
	int piece_buy_gold;
	int piece_sell_gold;
	int piece_buy_crystal;
	int piece_sell_crystal;
} shop_t;

// for db_design
typedef struct {
	int id;  // hero_id + 20*level,  hero_id should match first 20 char in deck
	int icon;
	int lv;
	double rating;
	int rating_flag; // 0==not count rating in win_game;1==count rating in win_game
	int pid; // prize card id
	int win_gold; 
	int win_exp; 
	char alias[EVIL_ALIAS_MAX + 1];
	char deck[EVIL_CARD_MAX+1]; // +1 for null-term
} ai_t;


typedef struct {
	int target;
	int p1;
	int p2;
} solo_target_t;

typedef struct {
	int id;
	// int hero_id;
	char alias[EVIL_ALIAS_MAX + 1];
	int solo_type;
	int ai_max_ally;
	int hp2;
	// deck2 = [hero_id card_id1 card_id2 ... card_idn]
	char deck2[EVIL_CARD_MAX+1]; // +1 for null-term 
	char type_list[101];
	// deck1 = [hero_id card_id1 card_id2 ... card_idn]
	char deck1[EVIL_CARD_MAX+1]; // +1 for null-term
	int hp1;
	int start_side;
	solo_target_t target_list[MAX_SOLO_TARGET];
} solo_t;


typedef struct {
	int id;
	int rtype;
	int icon;
	int lv;
	double rating;
	// int hero_id;
	int hp;
	int energy;
	char alias[EVIL_ALIAS_MAX + 1];
	char deck[EVIL_CARD_MAX+1]; // +1 for null-term
} design_robot_t;


// for loading logic.lua card info
typedef struct {
	int id;
	int star; // temp hard code some value
	int job;
	int cost;
	char name[101]; // enough? sprintf( .. "%.99")
} card_t;


typedef struct {
	card_t	card_list[EVIL_CARD_MAX+1];
	int		size;
} star_t;


typedef struct {
	int id;
	char title[MAX_TITLE_SIZE+2]; 
	char note[MAX_NOTICE_SIZE+2]; 
} notice_t;


// for db_design
typedef struct {
	// when user select "pick", random rate on each loc
	int pick_rate[MAX_LOC];   // @see TABLE design_pick.rate

	// "change batch" : random rate of starred card appears on each loc
	int batch_rate[MAX_LOC][MAX_STAR];  
	// loc is base 0, star need base 0 to base 1 conversion: 
	// e.g.  batch_rate[2][4] => loc2 rate of star5 card appear
} pick_t;
// nio.cpp:  TODO g_pick_list[MAX_PICK]
// ,  g_pick_list[0]=gold_pick,   g_pick_list[1]=crystal_pick


typedef struct {
	long xcid;
	int eid;
	int cardid;
	int count;
	int gold;
	int crystal;
	// name is not needed
} exchange_t;


typedef struct {
	int fd;
	int main_fd;
	char db_buffer[DB_TRANS_MAX][DB_BUFFER_MAX];
	int buffer_index; // for main loop
} dbio_init_t;

typedef struct {
	int eid;
	int rank;
	double rating;
	char alias[EVIL_ALIAS_MAX+1];
	int icon;
} ladder_rating_t;

typedef struct {
	int eid;
	int rank;
	int lv;
	char alias[EVIL_ALIAS_MAX+1];
	int icon;
} ladder_level_t;

typedef struct {
	int gid;
	int rank;
	int glevel;
	char gname[EVIL_ALIAS_MAX+1];
	int icon;
} ladder_guild_t;

typedef struct {
	int eid;
	int rank;
	int count;
	char alias[EVIL_ALIAS_MAX+1];
	int icon;
} ladder_collection_t;

typedef struct {
	int eid;
	int rank;
	int gold;
	char alias[EVIL_ALIAS_MAX+1];
	int icon;
} ladder_gold_t;

typedef struct {
	int eid;
	int rank;
	int chapter_id;
	int stage_id;
	int count;
	char alias[EVIL_ALIAS_MAX+1];
	int icon;
} ladder_chapter_t;

typedef struct {
	int count;
	char name_list[MAX_LOCAL_NAME][NAME_SIZE];
} local_name_t;


typedef struct {
	int id;
	char deck[EVIL_CARD_MAX+1];
} std_deck_t;

typedef struct {
	int lv;
	int consume_gold;
	int levelup_gold;
	int member_max;
} design_guild_t;

typedef struct {
	int card_id;
	int count;
	int gold;
	int crystal;
} design_merge_t;


typedef struct {
	int pay_id;
	int pay_code;
	int channel;
	int price;
	int money_type;
	int money;
	char title[MAX_NOTICE_SIZE+2]; 
	char description[MAX_NOTICE_SIZE+2]; 
} design_pay_t;

typedef struct {
	int id;
	int game_version;
	int client_version;
} design_version_t;

typedef struct {
	int device_id; // 1 == apple ipa, 2 == android
	char website[302]; 
} design_website_t;

typedef struct {
	int mid; 
	int pre; 
	int lv; 
	int hero; 
	int daily; 
	int guild_lv; 
	int mtype; 
	int p1; 
	int p2; 
	int p3; 
	int reward_exp; 
	int reward_gold; 
	int reward_crystal; 
	int reward_power;
	int card_count;
	int reward_card[MAX_MISSION_REWARD_CARD]; 
	int piece_count;
	int reward_piece[MAX_MISSION_REWARD_PIECE][2]; 
	char reset_time[10]; // 00:00
	char mtext[302]; 
} design_mission_t;

typedef struct {
	int aid; 
	char name[302]; 
	char msg[500];

	int pre; 
	int atype; 
	int p1;
	int p2;
	char p3[EVIL_CARD_MAX+1]; // for card list achievement

	int reward_gold; 
	int reward_crystal; 
	int reward_card[MAX_ACHIEVEMENT_REWARD_CARD]; 
	int reward_piece[MAX_ACHIEVEMENT_REWARD_PIECE]; 
} design_achi_t;

// mid -> design_mission_t
// typedef map<int,design_mission_t> D_MISSION_MAP;

typedef struct {
	int id;
	int gold;
	int crystal;
} design_slot_t;


typedef struct {
	int id;
	int start;
	int end;
	int gold;
	int crystal;
	char title[EVIL_MESSAGE_TITLE_MAX + 5];
	char message[EVIL_MESSAGE_MAX + 5];
} design_rank_reward_t;

typedef struct {
	int id;
	int time;
} design_rank_time_t;

typedef struct {
	int id;
	int start;
	int end;
	int gold;
	int crystal;
	char title[EVIL_MESSAGE_TITLE_MAX + 5];
	char message[EVIL_MESSAGE_MAX + 5];
} design_arena_reward_t;

typedef struct {
	int weight_start;
	int weight_end;
	int size;
	int cards[EVIL_CARD_MAX+1];
} design_lottery_single_t;

typedef struct {
	int gold_normal_lottery_size;
	int gold_normal_lottery_max_weight;
	design_lottery_single_t gold_normal_lottery[MAX_LOTTERY_LIST];

	int crystal_normal_lottery_size;
	int crystal_normal_lottery_max_weight;
	design_lottery_single_t crystal_normal_lottery[MAX_LOTTERY_LIST];

	int gold_special_lottery_size;
	int gold_special_lottery_max_weight;
	design_lottery_single_t gold_special_lottery[MAX_LOTTERY_LIST];

	int crystal_special_lottery_size;
	int crystal_special_lottery_max_weight;
	design_lottery_single_t crystal_special_lottery[MAX_LOTTERY_LIST];

	int gold_once_lottery_size;
	int gold_once_lottery_max_weight;
	design_lottery_single_t gold_once_lottery[MAX_LOTTERY_LIST];

	int crystal_once_lottery_size;
	int crystal_once_lottery_max_weight;
	design_lottery_single_t crystal_once_lottery[MAX_LOTTERY_LIST];
} design_lottery_t;

typedef struct {
	int game_type;
	time_t open_time;
	time_t close_time;
} design_fight_schedule_t;



typedef struct {
	int size;
	char title[MAX_GATE_TITLE];
	int gold;
	int crystal;
	int exp;
	int focus_card;
	int power;
	char gate_info[EVIL_CARD_MAX+1];
} design_gate_t;

typedef struct {
	int gate_id;
	int size;
	char msg[GATE_MSG_SIZE];
} design_gate_msg_t;

typedef struct {
	int id;
	int start;
	int end;
	int battle_coin;
} design_tower_reward_t;

typedef struct {
	int id;
	int card_id;
	int count;
	int gold;
	int crystal;
	int weight;
	int hard_show;
} design_piece_shop_t;

typedef struct {
	int id;
	int count;
	int pid_list[MAX_PIECE_SHOP_SLOT];
} design_pshop_show_map_t;

/*
typedef struct {
	int target;
	int p1;
	int p2;
} design_chapter_target_t;
*/

typedef struct {
	int reward;
	int count;
	int weight_start;
	int weight_end;
} design_chapter_reward_t;

typedef struct {
	int stage_id;
	char name[210];
	char stage_msg[900];
	// design_chapter_target_t target_list[MAX_CHAPTER_TARGET];
	int solo_size;
	int solo_list[MAX_CHAPTER_AI];
	int exp;
	int power; // reduce user power
	int tips_size;
	int tips_list[MAX_CHAPTER_TIPS];
	design_chapter_reward_t reward_list[MAX_CHAPTER_REWARD];
	int reward_weight_max;
} design_chapter_stage_t;

typedef struct {
	int chapter_id;
	char name[210];
	int stage_size;
	design_chapter_stage_t stage_list[MAX_CHAPTER_STAGE+1]; // base 1
} design_chapter_t;

typedef struct {
	// design_chapter_target_t target_list[MAX_CHAPTER_TARGET];
	// int value_list[MAX_CHAPTER_TARGET];
	// int done_list[MAX_CHAPTER_TARGET];
	solo_target_t target_list[MAX_SOLO_TARGET];
	int value_list[MAX_SOLO_TARGET];
	int done_list[MAX_SOLO_TARGET];
} chapter_result_t;



typedef struct {
	int dialog_count;
	char dialog[1000];
} design_stage_dialog_t;

typedef struct {
	design_stage_dialog_t stage_dialog_list[MAX_CHAPTER_STAGE+1];
} design_chapter_dialog_t;


typedef struct {
	int type; // 1 for gold, 2 for crystal
	int count;
	int weight_start;
	int weight_end;
} design_quick_reward_t;

typedef struct {
	int max_weight;
	design_quick_reward_t reward_array[MAX_QUICK_REWARD];
} design_quick_reward_list_t;

typedef struct {
	int mission_id;
	int pre;
	int reward_type;
	int reward_count;
	int mtype;
	int p1;
	int p2;
	int p3;
	char msg[900];
} design_hero_mission_t;

typedef struct {
	int hero_id;
	int mission_size;
	design_hero_mission_t mission_list[MAX_HERO_MISSION+1];
} design_mission_hero_t;

typedef struct {
	int hero_id;
	int hp;
	int energy;
} design_hero_t;


typedef struct {
	char name[40];  // TODO use EVIL_ALIAS_MAX + 5
	int chapter_id;
	int stage_id;
} design_piece_chapter_t;

typedef struct {
	int card_id;
	int count;
	design_piece_chapter_t chapter_list[MAX_CARD_CHAPTER];
} design_card_chapter_t;

typedef struct {
	int id;
	char deck[EVIL_CARD_MAX+1];
} design_hero_slot_t;


typedef struct {
	int log_time;
	int gold;
	int crystal;
	int card_count;
	int cards[MAX_DAILY_REWARD_CARD];
	int piece_count;
	int pieces[MAX_DAILY_REWARD_PIECE][2];	// [piece_id][count]
} design_daily_reward_t;

typedef struct {
	int day;
	design_daily_reward_t daily_reward[MAX_DAILY_REWARD];
} design_daily_login_t;

typedef struct {
	shop_t   shop_list[EVIL_CARD_MAX+1]; //base 1(0 is useless)
	std_deck_t   std_deck_list[HERO_MAX+1]; //base 1(0 is useless)
	ai_t   ai_list[MAX_AI_EID+1]; //base 1(0 is useless)
	pick_t   pick_list[MAX_PICK]; 
	card_t   card_list[EVIL_CARD_MAX+1]; // base 1(0 is useless)
	//vector<card_t>   star_list[MAX_STAR];
	//vector<card_t>   extract_list[MAX_STAR];
	star_t		star_list[MAX_STAR];
	star_t		extract_list[MAX_STAR];
	constant_t   constant;
	int   max_level;
	// vector<int>   exp_list;
	int   exp_list[MAX_LEVEL+1]; // base 1(0 is useless)
	int   notice_count;
	notice_t   notice_list[MAX_NOTICE+1]; // base 1(0 is useless)
	int   guild_max_level;
	design_guild_t   guild_list[100]; // XXX hard code max guild level = 100
	design_merge_t   merge_list[EVIL_CARD_MAX+1];  // base 1(0 is useless)
	design_pay_t   pay_list[MAX_PAY_NUM];  // base 1(0 is useless)
	design_version_t   version;
	design_website_t   website_list[MAX_WEBSITE_NUM]; //base 0, hard code max website = 50
	
//	D_MISSION_MAP mission_list;
	design_mission_t	mission_list[MAX_MISSION];
	design_slot_t   slot_list[MAX_SLOT_NUM]; //base 1

	design_rank_reward_t rank_reward_list[MAX_RANK_REWARD];
	int max_rank_reward_level;

	design_rank_time_t rank_time_list[MAX_RANK_TIME];
	int max_rank_time;


	design_arena_reward_t arena_reward_list[MAX_ARENA_REWARD];
	int max_arena_reward_level;


	design_lottery_t lottery_info;

	design_fight_schedule_t fight_schedule_list[FIGHT_SCHEDULE_MAX];
	int max_fight_schedule;

	int design_gate_size;
	design_gate_t design_gate_list[MAX_GATE_LIST];

	design_gate_msg_t design_gate_msg_list[MAX_GATE_LIST];

	int hero_hp_list[HERO_MAX+1]; // base 1

	design_tower_reward_t tower_reward_list[MAX_RANK_REWARD];
	int max_tower_reward_level;

	design_piece_shop_t piece_shop_list[MAX_PIECE_SHOP_LIST];
	int max_piece_shop_weight;

	design_pshop_show_map_t pshop_show_map[MAX_PIECE_SHOP_HARD_SHOW+1];
	int max_pshop_hard_show;

	char pshop_piece_list[EVIL_CARD_MAX+1];

	
	int	pay_monthly_list[MAX_PAY_MONTHLY+1];
	int	max_pay_monthly_list;

	// int design_solo_size;
	// now design_solo_list[solo_id].id != solo_id
	solo_t design_solo_list[MAX_AI_EID+1];

	int design_robot_size;
	design_robot_t design_robot_list[MAX_AI_EID+1];

	int design_fakeman_size;
	design_robot_t design_fakeman_list[MAX_AI_EID+1];

	// for chapter
	int design_chapter_size;
	design_chapter_t design_chapter_list[MAX_CHAPTER+1]; //base 1

	int mission_hero_size;
	design_mission_hero_t mission_hero_list[HERO_MAX+1];

	int hero_size;
	design_hero_t hero_list[HERO_MAX+1];		// base 1

	design_card_chapter_t card_chapter_list[EVIL_CARD_MAX+1];

	design_hero_slot_t hero_slot_list[HERO_MAX+1];

	design_achi_t achi_list[MAX_ACHIEVEMENT+1];

	design_daily_login_t daily_login_list[MAX_DAILY_LOGIN+1];

	design_quick_reward_list_t quick_reward_list[2]; // win=0, lose=1

	design_chapter_dialog_t chapter_dialog_list[MAX_CHAPTER+1];
} design_t;

///////// TYPEDEF END   /////////

/////////////////////////////////////////////////
/////////////////// DBIO START [ ////////////////
// in_buffer and out_buffer

// username, password, ip, &platform, &channel, uid, alias
#define IN_REGISTER_SCAN	"%30s %30s %100s %d %d %400s"  // username password id alias(opt)
#define IN_REGISTER_PRINT	"%.30s %.30s %.100s %d %d %.400s"
// eid, username, password, alias,  [status]: lv, rating(%lf), gold, crystal
// , gid, gpos, count, win, lose, draw, run, icon, exp, sex, course, signature
// , gname, glevel, gate_pos
#define OUT_REGISTER_SCAN "%d %30s %30s %30s %d %lf %d %d %d %d %d %d %d %d %d %d %d %d %d %s %30s %d %d"  // 23 fields
#define OUT_REGISTER_PRINT "%d %.30s %.30s %.30s %d %lf %d %d %d %d %d %d %d %d %d %d %d %d %d %s %.30s %d %d"

#define IN_LOGIN_SCAN	"%30s %30s %100s %d %d"	// [0]username  [1]password
#define IN_LOGIN_PRINT	"%.30s %.30s %.100s %d %d"	  // special!
// same as OUT_REGISTER
#define OUT_LOGIN_SCAN	"%d %30s %30s %30s %d %lf %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %ld %50s %30s %d %400s %d %d %d %d"  // 32 fields
#define OUT_LOGIN_PRINT	"%d %.30s %.30s %.30s %d %lf %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %ld %.50s %.30s %d %.400s %d %d %d %d"

#define IN_LOAD_CARD_SCAN	"%d"	// eid
#define IN_LOAD_CARD_PRINT	"%d"
#define OUT_LOAD_CARD_SCAN	"%d %400s"
#define OUT_LOAD_CARD_PRINT	"%d %.400s"	// useless actually, keep

// peter: if we need to work on slot, IN_LOAD_DECK_SCAN = "%d %d" eid, slot
#define IN_LOAD_DECK_SCAN	"%d" 	// "%d"	// eid
#define IN_LOAD_DECK_PRINT	"%d"	// "%d"
#define OUT_LOAD_DECK_SCAN	"%d %d %s %400s"	// "%d %400s"
#define OUT_LOAD_DECK_PRINT "%d %d %s %.400s"

// save_card, save_deck, alias : 3 functions only output 0 ok, -x err_msg
#define IN_SAVE_CARD_SCAN	"%d %400s"		// eid, card400
#define IN_SAVE_CARD_PRINT	"%d %.400s"
// out is simply %d %s  int code, char msg[100]

#define IN_SAVE_DECK_SCAN	"%d %d %400s"	// eid, slot, deck400
#define IN_SAVE_DECK_PRINT	"%d %d %.400s"

#define OUT_SAVE_DECK_SCAN	"%d %d %400s"	// eid, slot, deck400
#define OUT_SAVE_DECK_PRINT	"%d %d %.400s"


#define IN_ALIAS_SCAN	"%d %30s"	// eid, alias
#define IN_ALIAS_PRINT	"%d %.30s"

// eid, alias
#define OUT_ALIAS_PRINT	"%d %.30s"
#define OUT_ALIAS_SCAN	"%d %30s"

#define IN_GAME_SCAN	"%d %d"	// eid1, eid2  Note: eid2 maybe 0 or AI
#define IN_GAME_PRINT	IN_GAME_SCAN 
#define OUT_GAME_SCAN	"%d %400s %d %400s"	//  eid1, deck1, eid2, deck2
#define OUT_GAME_PRINT	"%d %.400s %d %.400s"	// deck2="AI" if eid2 is AI

// winner,rating,eid1,eid2,gold1, crystal1,gold2,crystal2
// exp1, lv1, exp2, lv2, card_id1, card_id2
#define IN_WIN_SCAN		"%d %lf %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d" 
#define IN_WIN_PRINT	IN_WIN_SCAN

// @see nio.cpp : game_param_string,  logic.lua game_param_split()
// count(7), game_flag, type_list, ai_max_ally, hp1, hp2, energy1, energy2
#define	GAME_PARAM_PRINT	"7 %d %s %d %d %d %d %d"

// note: reminding is cmd[2000] it got space ' ', need %n
// game_id, game_type, winner, star, seed
// start_side, ver, eid1, eid2, lv1
// lv2, icon1, icon2, alias1[30], alias2[30]
// deck1[400], deck2[400], param[200], cmd[2000]
#define IN_SAVE_REPLAY_SCAN		"%ld %d %d %d %d %d %d %d %d %d %d %d %d %s %s %n"
#define IN_SAVE_REPLAY_PRINT	"%ld %d %d %d %d %d %d %d %d %d %d %d %d %.30s %.30s %.405s %.405s %.200s %.4000s"

#define IN_SAVE_DEBUG_SCAN "%d %100s %n"  // eid, filename : content using %n
#define IN_SAVE_DEBUG_PRINT	"%d %.100s %.2000s"  // eid, filename, content
// out is simply code + msg

#define IN_LOAD_DEBUG_SCAN	"%100s"	// filename[100]
#define IN_LOAD_DEBUG_PRINT	"%.100s"

#define OUT_LOAD_DEBUG_SCAN	"%d %99s %4000s"	// eid, content
#define OUT_LOAD_DEBUG_PRINT	"%d %.99s %.4000s"	

#define IN_STATUS_SCAN	"%d"	// eid
#define IN_STATUS_PRINT	"%d"	// eid
// eid, lv, rating(%lf), gold, crystal
// , res1, res2, count, win, lose, draw, run, exp, sex, signature, unread_count
// , power, power_set_time, gate_pos
#define OUT_STATUS_SCAN "%d %d %lf %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s %ld %s %d %lf %ld %d %d %d"

#define OUT_STATUS_PRINT OUT_STATUS_SCAN

#define IN_GET_CARD_COUNT_SCAN	"%d %d"
#define IN_GET_CARD_COUNT_PRINT	"%d %d"
#define OUT_GET_CARD_COUNT_SCAN		"%d %d %d"
#define OUT_GET_CARD_COUNT_PRINT	"%d %d %d"

// eid, card_id, card_type, money_type, buy_count, gold, crystal
#define IN_BUY_CARD_SCAN	"%d %d %d %d %d %d %d"
#define IN_BUY_CARD_PRINT	"%d %d %d %d %d %d %d"
#define OUT_BUY_CARD_SCAN	"%d %d %d %d %d %d %d"
#define OUT_BUY_CARD_PRINT	"%d %d %d %d %d %d %d"

// eid, card_id, card_type, money_type, sell_count, gold, crystal
#define IN_SELL_CARD_SCAN	"%d %d %d %d %d %d %d"
#define IN_SELL_CARD_PRINT	"%d %d %d %d %d %d %d"
#define OUT_SELL_CARD_SCAN	"%d %d %d %d %d %d %d"
#define OUT_SELL_CARD_PRINT	"%d %d %d %d %d %d %d"

#define IN_LOAD_BATCH_SCAN	"%d %d"
#define IN_LOAD_BATCH_PRINT	"%d %d"


// eid, ptype, card0, card1, card2, card3, card4, card5, gold, crystal   (total 6 cards)
#define IN_SAVE_BATCH_SCAN 	"%d %d %d %d %d %d %d %d %d %d"
#define IN_SAVE_BATCH_PRINT IN_SAVE_BATCH_SCAN
// out: 0 OK  or   -errcode msg

// eid, batch_type, loc, card_id, gold, crystal
#define IN_PICK_SCAN	"%d %d %d %d %d %d"
#define IN_PICK_PRINT	"%d %d %d %d %d %d"
#define OUT_PICK_SCAN	"%d %d %d %d %d %d"
#define OUT_PICK_PRINT	"%d %d %d %d %d %d"

#define IN_CCCARD_SCAN	"%d %d %d"  // eid card_id count 
#define IN_CCCARD_PRINT	"%d %d %d"  // eid card_id count (use:dbin_write, no \n)

// eid card_id count gold crystal name
#define IN_ADD_EXCHANGE_SCAN "%d %d %d %d %d %30[^\n]"  // name include space
#define IN_ADD_EXCHANGE_PRINT "%d %d %d %d %d %.30s"

// buyer_eid xcid count
#define IN_BUY_EXCHANGE_SCAN "%d %ld %d"  
#define IN_BUY_EXCHANGE_PRINT "%d %ld %d"  

// buyer_eid seller_eid cardid count gold crystal   (positive)
#define OUT_BUY_EXCHANGE_PRINT "%d %d %d %d %d %d"
#define OUT_BUY_EXCHANGE_SCAN	OUT_BUY_EXCHANGE_PRINT

// start_id page_size search_key
#define IN_LIST_EXCHANGE_SCAN "%d %d %30s" // do not use %30[^\n]
#define IN_LIST_EXCHANGE_PRINT "%d %d %.30s"

// start_id, row_size( <= page_size)
#define OUT_LIST_EXCHANGE_PRINT	"%d %d"
#define OUT_LIST_EXCHANGE_SCAN	"%d %d"
// xcid, eid, cardid, count, gold, crystal, name
#define OUT_LIST_EXCHANGE_ROW_SCAN "%d %d %d %d %d %d"
#define OUT_LIST_EXCHANGE_ROW_PRINT " %s %s %s %s %s %s"
// one_row : xcid eid cardid gold crystal (name is not need)
// output := OUT_LIST_EXCHANGE_PRINT one_row one_row one_row last_row\n

#define IN_RESET_EXCHANGE_PRINT "%ld"
#define IN_RESET_EXCHANGE_SCAN "%ld"


#define IN_LIST_GUILD_SCAN	"%d %d %30s"  // start_id page_size key_optional
#define IN_LIST_GUILD_PRINT	"%d %d %.30s"

#define OUT_LIST_GUILD_PRINT	"%d %d" // start_id page_size
// row[0]=gid, row[1]=gname, row[2]=glevel, row[3]=gold
// row[4]=crystal, row[5]=total_member
#define OUT_LIST_GUILD_ROW_PRINT	" %s %s %s %s %s %s %s"
#define OUT_LIST_GUILD_ROW_SCAN	"%d %d %d %d %d %s %s"

#define IN_CREATE_GUILD_SCAN	"%d %d %d %30s"
#define IN_CREATE_GUILD_PRINT	"%d %d %d %.30s"

#define OUT_CREATE_GUILD_SCAN	"%d %d %d %30s"
#define OUT_CREATE_GUILD_PRINT	"%d %d %d %.30s"

// gid
#define IN_DELETE_GUILD_SCAN	"%d"
#define IN_DELETE_GUILD_PRINT	IN_DELETE_GUILD_SCAN

#define OUT_DELETE_GUILD_SCAN 	"%d"	// gid
#define OUT_DELETE_GUILD_PRINT 	"%d"	// gid

// flag, start_id, page_size, gid (which is master_eid)
#define IN_LIST_GMEMBER_PRINT "%d %d %d %d"
#define IN_LIST_GMEMBER_SCAN IN_LIST_GMEMBER_PRINT

#define OUT_LIST_GMEMBER_PRINT "%d %d %d"	// flag page_size 
#define OUT_LIST_GMEMBER_SCAN "%d %d %d %n"
// eid, gpos, alias, +icon, +rating, last_login, gshare, lv(str form)
#define OUT_LIST_GMEMBER_ROW_PRINT " %s %s %s %s %s %s %s %s"	
// eid, gpos, alias, +icon, +rating(double), last_login, gshare(double), lv
#define OUT_LIST_GMEMBER_ROW_SCAN "%d %d %s %d %lf %ld %lf %d %n"	

#define IN_LIST_GSEARCH_PRINT	"%d %d %d %.50s %d"
#define IN_LIST_GSEARCH_SCAN	"%d %d %d %50s %d"
#define OUT_LIST_GSEARCH_PRINT	"%d %d %d"	// flag page_size 
#define OUT_LIST_GSEARCH_SCAN	"%d %d %d %n"
// eid, gpos, alias, +icon, +rating, last_login, gshare, lv(str form)
#define OUT_LIST_GSEARCH_ROW_PRINT " %s %s %s %s %s %s %s %s"	
// eid, gpos, alias, +icon, +rating(double), last_login, gshare(double), lv
#define OUT_LIST_GSEARCH_ROW_SCAN "%d %d %s %d %lf %ld %lf %d %n"	

// gid eid
#define IN_GUILD_APPLY_SCAN	"%d %d"
#define IN_GUILD_APPLY_PRINT IN_GUILD_APPLY_SCAN

// eid, gid, gpos, gname
#define OUT_GUILD_APPLY_PRINT "%d %d %d %.30s"
#define OUT_GUILD_APPLY_SCAN "%d %d %d %30s"

// eid pos gid member_max
#define IN_GUILD_POS_SCAN	"%d %d %d %d"
#define IN_GUILD_POS_PRINT	IN_GUILD_POS_SCAN
#define OUT_GUILD_POS_SCAN	"%d %d %d"
#define OUT_GUILD_POS_PRINT	OUT_GUILD_POS_SCAN

// eid gid
#define IN_GUILD_QUIT_PRINT	"%d %d %d"
#define IN_GUILD_QUIT_SCAN	IN_GUILD_QUIT_PRINT
// eid gid
#define OUT_GUILD_QUIT_PRINT	"%d %d"
#define OUT_GUILD_QUIT_SCAN OUT_GUILD_QUIT_PRINT	

// eid, gid, gold
#define IN_GUILD_DEPOSIT_PRINT "%d %d %d"
#define IN_GUILD_DEPOSIT_SCAN IN_GUILD_DEPOSIT_PRINT
#define OUT_GUILD_DEPOSIT_PRINT "%d %d %d"
#define OUT_GUILD_DEPOSIT_SCAN OUT_GUILD_DEPOSIT_PRINT

// eid rate
#define IN_GUILD_BONUS_SCAN	"%d %lf %d"
#define IN_GUILD_BONUS_PRINT "%d %lf %d"
// eid, get_flag, guild_gold, rate, gshare, bonus_gold, last_bonus_time(unix)
#define OUT_GUILD_BONUS_SCAN	"%d %d %d %lf %lf %d %ld "
#define OUT_GUILD_BONUS_PRINT	OUT_GUILD_BONUS_SCAN

// gid start_id page_size
#define IN_LIST_DEPOSIT_SCAN	"%d %d %d"
#define IN_LIST_DEPOSIT_PRINT	IN_LIST_DEPOSIT_SCAN

// start_id page_size 
#define OUT_LIST_DEPOSIT_PRINT	"%d %d"

// str format: deposit_date, eid, alias, +icon, gold, crystal, gpos
#define OUT_LIST_DEPOSIT_ROW_PRINT	" %s %s %s %s %s %s %s" 
// deposit_date, eid, alias, +icon, gold, crystal
#define OUT_LIST_DEPOSIT_ROW_SCAN	" %ld %d %s %d %d %d %d" 

// eid, gid
#define IN_DEPOSIT_SCAN "%d %d"
#define IN_DEPOSIT_PRINT "%d %d"
// eid, gid, my_gold, my_crystal, gshare, guild_gold, guild_crystal
#define OUT_DEPOSIT_PRINT "%d %d %d %d %lf %d %d"


#define IN_GUILD_SCAN	"%d %n"	// gid, gnotice
#define IN_GUILD_PRINT	"%d %s" 	// gid, consume, gnotice(optional)

// gid, total_member, glevel, gold, crystal, alias, icon, gname, notice
#define OUT_GUILD_PRINT "%s %s %s %s %s %s %s %s %s"
#define OUT_GUILD_SCAN "%d %d %d %d %d %s %d %.30s %.100s"	// maybe useless



// row_num
#define OUT_RATING_LADDER_PRINT "%d"
#define OUT_RATING_LADDER_SCAN	"%d %n"
// eid, rank, rating, alias, icon
#define OUT_RATING_LADDER_ROW_PRINT " %s %s %s %s %s"
#define OUT_RATING_LADDER_ROW_SCAN "%d %d %lf %s %d %n"

#define OUT_LEVEL_LADDER_PRINT " %d"  // space is needed
#define OUT_LEVEL_LADDER_SCAN	"%d %n"
// eid, rank, lv, alias, icon
#define OUT_LEVEL_LADDER_ROW_PRINT " %s %s %s %s %s"
#define OUT_LEVEL_LADDER_ROW_SCAN "%d %d %d %s %d %n"

#define OUT_GUILD_LADDER_PRINT " %d"	// space is needed
#define OUT_GUILD_LADDER_SCAN	"%d %n"
// gid, rank, glevel, gname, icon
#define OUT_GUILD_LADDER_ROW_PRINT " %s %s %s %s %s"
#define OUT_GUILD_LADDER_ROW_SCAN "%d %d %d %s %d %n"

// guild_level related
#define IN_GLV_SCAN	"%d"	// gid
#define IN_GLV_PRINT IN_GLV_SCAN
#define OUT_GLV_PRINT	"%s %s %s"	// gid, glevel, gold  (str form)
#define OUT_GLV_SCAN "%d %d %d"


#define IN_GLEVELUP_SCAN	"%d %d %d" // gid, glevel, gold
#define IN_GLEVELUP_PRINT	IN_GLEVELUP_SCAN
#define OUT_GLEVELUP_PRINT	"%d %d %d"	// gid, glevel gold(-negative)
#define OUT_GLEVELUP_SCAN	OUT_GLEVELUP_PRINT


#define OUT_COLLECTION_LADDER_PRINT " %d"
#define OUT_COLLECTION_LADDER_SCAN	"%d %n"
// eid, rank, count, alias, icon
#define OUT_COLLECTION_LADDER_ROW_PRINT " %s %s %s %s %s"
#define OUT_COLLECTION_LADDER_ROW_SCAN "%d %d %d %s %d %n"

#define OUT_GOLD_LADDER_PRINT " %d"
#define OUT_GOLD_LADDER_SCAN	"%d %n"
// eid, rank, gold, alias, icon
#define OUT_GOLD_LADDER_ROW_PRINT " %s %s %s %s %s"
#define OUT_GOLD_LADDER_ROW_SCAN "%d %d %d %s %d %n"

// ladder_type, eid, gid
#define IN_GET_LADDER_PRINT "%d %d %d"
#define IN_GET_LADDER_SCAN "%d %d %d"

// ladder_type, eid, rank, rating
#define OUT_GET_RATING_LADDER_PRINT "%d %d %d %lf"
// eid, rank, rating
#define OUT_GET_RATING_LADDER_SCAN "%d %d %lf"

// ladder_type, eid, rank, lv
#define OUT_GET_LEVEL_LADDER_PRINT "%d %d %d %d"
// eid, rank, lv
#define OUT_GET_LEVEL_LADDER_SCAN "%d %d %d"

// ladder_type, gid, rank, glevel
#define OUT_GET_GUILD_LADDER_PRINT "%d %d %d %d"
// gid, rank, glevel
#define OUT_GET_GUILD_LADDER_SCAN "%d %d %d"

// ladder_type, eid, rank, count
#define OUT_GET_COLLECTION_LADDER_PRINT "%d %d %d %d"
// eid, rank, count
#define OUT_GET_COLLECTION_LADDER_SCAN "%d %d %d"

// ladder_type, eid, rank, gold
#define OUT_GET_GOLD_LADDER_PRINT "%d %d %d %d"
// eid, rank, gold
#define OUT_GET_GOLD_LADDER_SCAN "%d %d %d"


// eid
#define IN_LIST_REPLAY_SCAN "%d"
#define IN_LIST_REPLAY_PRINT "%d"
// eid, total
#define OUT_LIST_REPLAY_PRINT "%d %d"
#define OUT_LIST_REPLAY_SCAN "%d %d"
// gameid, winner, ver, eid1, eid2, lv1, lv2, icon1, icon2, alias1, alias2
#define OUT_LIST_REPLAY_ROW_PRINT " %s %s %s %s %s %s %s %s %s %s %s"

// gameid
#define IN_LOAD_REPLAY_SCAN "%ld"
#define IN_LOAD_REPLAY_PRINT "%ld"
// 0, gameid, game_type, winner, star
//, seed, start_side, ver, eid1, eid2
//, lv1, lv2, icon1, icon2, alias1
//, alias2, deck1, deck2, param, cmd
#define OUT_LOAD_REPLAY_PRINT "%d %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s"


// eid, icon, sex, signature
#define IN_UPDATE_PROFILE_SCAN "%d %d %d %100s"
#define IN_UPDATE_PROFILE_PRINT "%d %d %d %.100s"

#define OUT_UPDATE_PROFILE_SCAN "%d %d %d %100s"
#define OUT_UPDATE_PROFILE_PRINT "%d %d %d %.100s"

// eid1, eid2
#define IN_FRIEND_ADD_SCAN "%d %d"
#define IN_FRIEND_ADD_PRINT "%d %d"
#define OUT_FRIEND_ADD_SCAN "%d %d"
#define OUT_FRIEND_ADD_PRINT "%d %d"

// eid, start_num, page_size
#define IN_FRIEND_LIST_SCAN "%d %d %d %30s"
#define IN_FRIEND_LIST_PRINT "%d %d %d %.30s"
// eid, num_row
#define OUT_FRIEND_LIST_SCAN "%d %d %d %d %n"
// eid, alias, icon
#define OUT_FRIEND_LIST_ROW_PRINT " %s %.30s %s"
#define OUT_FRIEND_LIST_ROW_SCAN " %d %30s %d %n"


#define IN_FRIEND_STA_SCAN	"%d %d"	// my_eid, eid
#define IN_FRIEND_STA_PRINT	"%d %d"	// my_eid, eid
// eid, alias, lv, rating(%lf), gold
// , crystal, gid, gpos, gname, count
// , win, lose, draw, run, icon
// , exp, sex, friend_flag, signature
#define OUT_FRIEND_STA_SCAN "%d %s %d %lf %d %d %d %d %s %d %d %d %d %d %d %d %d %d %s"
#define OUT_FRIEND_STA_PRINT OUT_FRIEND_STA_SCAN

// alias
#define IN_FRIEND_SEARCH_SCAN "%s"
#define IN_FRIEND_SEARCH_PRINT "%.30s"

#define IN_FRIEND_DEL_SCAN "%d %d"
#define IN_FRIEND_DEL_PRINT "%d %d"
#define OUT_FRIEND_DEL_SCAN "%d %d"
#define OUT_FRIEND_DEL_PRINT "%d %d"

#define OUT_FRIEND_SEARCH_ROW_PRINT "%s %s %s %s "

// piece
#define IN_LOAD_PIECE_SCAN "%d"
#define IN_LOAD_PIECE_PRINT "%d"
#define OUT_LOAD_PIECE_SCAN "%d"
#define OUT_LOAD_PIECE_PRINT "%d"

// eid, pick_type, loc, card_id, count, gold(-), crystal(-)
#define IN_PICK_PIECE_SCAN		"%d %d %d %d %d %d %d"
#define IN_PICK_PIECE_PRINT		"%d %d %d %d %d %d %d"
#define OUT_PICK_PIECE_SCAN		"%d %d %d %d %d %d %d"
#define OUT_PICK_PIECE_PRINT	"%d %d %d %d %d %d %d"

// eid, card_id, need_piece_count, gold(-), crystal(-)
#define IN_MERGE_PIECE_SCAN "%d %d %d %d %d"
#define IN_MERGE_PIECE_PRINT "%d %d %d %d %d"
#define OUT_MERGE_PIECE_SCAN "%d %d %d %d %d"
#define OUT_MERGE_PIECE_PRINT "%d %d %d %d %d"

// pay_id, eid, game_money_type, game_money, pay_type, price
#define IN_PAY_SCAN "%ld %d %d %d %d %d %d %d %d %ld %d %d %d %d %n"
#define IN_PAY_PRINT "%ld %d %d %d %d %d %d %d %d %ld %d %d %d %s"
#define OUT_PAY_SCAN "%ld %d %d %d %d %d %d %d %d %ld %d %n"
#define OUT_PAY_PRINT "%ld %d %d %d %d %d %d %d %d %ld %d"

//eid
#define IN_GET_COURSE_SCAN "%d"
#define IN_GET_COURSE_PRINT "%d"
//eid, course
#define OUT_GET_COURSE_SCAN "%d %d"
#define OUT_GET_COURSE_PRINT "%s %s"

//eid, course
#define IN_SAVE_COURSE_SCAN "%d %d"
#define IN_SAVE_COURSE_PRINT "%d %d"
//eid, course
#define OUT_SAVE_COURSE_SCAN "%d %d"
#define OUT_SAVE_COURSE_PRINT "%d %d"

// eid1, eid2
#define IN_CHALLENGE_SCAN	"%d %d"	// eid1, eid2
#define IN_CHALLENGE_PRINT	"%d %d"
// eid1, deck1, eid2, deck2
#define OUT_CHALLENGE_SCAN	"%d %400s %d %400s"
#define OUT_CHALLENGE_PRINT	"%d %.400s %d %.400s"

#define IN_LOAD_MISSION_SCAN	"%d"	// eid
#define IN_LOAD_MISSION_PRINT	"%d"

// mid, n1, status, last_update(second %ld)
// first 2 numbers:  %d %d = eid count (number of mission)
// then each mission is represented by scan / print below :
#define OUT_LOAD_MISSION_SCAN	"%d %d %d %ld %n"
#define OUT_LOAD_MISSION_PRINT	" %d %d %d %ld"


// first 2 number %d %d = eid count (num of mission)
// mid, status, n1, last_update(second %ld)   (db_update20141126.sql)
#define IN_SAVE_MISSION_PRINT	" %d %d %d %ld"
// add %n for next row
#define IN_SAVE_MISSION_SCAN	"%d %d %d %ld %n"

#define IN_MISSION_REWARD_PRINT	"%d %d %d %d %d %d %d %s"
//#define IN_MISSION_REWARD_SCAN	"%d %d %d %d %d %d %s"

#define IN_SLOT_LIST_SCAN	"%d"	// eid, id
#define IN_SLOT_LIST_PRINT	"%d"

#define IN_LOAD_SLOT_SCAN	"%d %d"	// eid, id
#define IN_LOAD_SLOT_PRINT	"%d %d"
#define OUT_LOAD_SLOT_SCAN	"%d %d %400s"
#define OUT_LOAD_SLOT_PRINT	"%d %d %.400s"	

#define IN_SAVE_SLOT_SCAN	"%d %d %s %n" // %400s" // eid, id
#define IN_SAVE_SLOT_PRINT	"%d %d %s %.400s" // eid, id
#define OUT_SAVE_SLOT_SCAN	"%d %d %s" // eid, id, name
#define OUT_SAVE_SLOT_PRINT	"%d %d %s %.400s"	// eid, id, name

#define IN_RENAME_SLOT_SCAN		"%d %d %s"  // eid, id, name
#define IN_RENAME_SLOT_PRINT	"%d %d %s"  // eid, id, name
#define OUT_RENAME_SLOT_SCAN	"%d %d %s" // eid, id
#define OUT_RENAME_SLOT_PRINT	"%d %d %s"	// eid, id

#define IN_BUY_SLOT_SCAN	"%d %d %d %d %d" //eid,flag,id,gold,crystal
#define IN_BUY_SLOT_PRINT	"%d %d %d %d %d"  
#define OUT_BUY_SLOT_SCAN	"%d %d %d %d %d" 
#define OUT_BUY_SLOT_PRINT	"%d %d %d %d %d"

#define IN_ADD_MATCH_SCAN	"%ld %100s %d %ld %ld %ld %ld %ld %d"
#define IN_ADD_MATCH_PRINT	"%ld %.100s %d %ld %ld %ld %ld %ld %d"
#define OUT_ADD_MATCH_SCAN	"%ld %100s %d %ld %ld %ld %ld %ld %d %d"
#define OUT_ADD_MATCH_PRINT	"%ld %.100s %d %ld %ld %ld %ld %ld %d %d"


#define IN_ADD_MATCH_PLAYER_SCAN	"%ld %d %d %d %d %d %d %d %d %d %s"
#define IN_ADD_MATCH_PLAYER_PRINT	"%ld %d %d %d %d %d %d %d %d %d %s"
#define OUT_ADD_MATCH_PLAYER_SCAN	"%ld %d %d %d %d %d %d %d %d %d %s"
#define OUT_ADD_MATCH_PLAYER_PRINT	"%ld %d %d %d %d %d %d %d %d %d %s"

#define IN_ADD_MATCH_AI_SCAN	"%ld %d %n"
#define IN_ADD_MATCH_AI_PRINT	"%ld %d %s"
#define OUT_ADD_MATCH_AI_SCAN	"%ld %d %n"
#define OUT_ADD_MATCH_AI_PRINT	"%ld %d"

#define IN_DELETE_MATCH_PLAYER_SCAN		"%ld %d"
#define IN_DELETE_MATCH_PLAYER_PRINT	"%ld %d"
#define OUT_DELETE_MATCH_PLAYER_SCAN	"%ld %d"
#define OUT_DELETE_MATCH_PLAYER_PRINT	"%ld %d"

#define IN_MATCH_TEAM_INIT_SCAN		"%ld %d %n"
#define IN_MATCH_TEAM_INIT_PRINT	"%ld %d %n"

#define IN_UPDATE_MATCH_PLAYER_SCAN		"%ld %d %n"
#define IN_UPDATE_MATCH_PLAYER_PRINT	"%ld %d %n"

#define IN_MATCH_ELI_INIT_SCAN		"%ld %d %n"
#define IN_MATCH_ELI_INIT_PRINT		"%ld %d %n"

// match_id, round, status
#define IN_UPDATE_MATCH_SCAN		"%ld %d %d"
#define IN_UPDATE_MATCH_PRINT		"%ld %d %d"


/////// ranking
// eid
#define IN_RANKING_LIST_SCAN			"%d"
#define IN_RANKING_LIST_PRINT			"%d"
// eid
#define IN_RANKING_TARGETS_SCAN			"%d"
#define IN_RANKING_TARGETS_PRINT		"%d"
// eid, target_eid, target_rank
#define IN_CHECK_RANKING_TARGET_SCAN	"%d %d %d"
#define IN_CHECK_RANKING_TARGET_PRINT	"%d %d %d"

// eid1, eid2, winner
#define IN_CHANGE_RANKING_DATA_SCAN		"%d %d %d"
#define IN_CHANGE_RANKING_DATA_PRINT	"%d %d %d"

// eid
#define IN_RANKING_HISTORY_SCAN			"%d"
#define IN_RANKING_HISTORY_PRINT		"%d"

// eid, challenge_time
#define IN_SAVE_RANKING_CHALLENGE_SCAN	"%d %d"
#define IN_SAVE_RANKING_CHALLENGE_PRINT	"%d %d"



#define IN_CHECK_LOGIN_SCAN				"%400s %30s %d"	// [0]uid  [1]password
#define IN_CHECK_LOGIN_PRINT			"%.400s %.30s %d"	// special!

#define IN_RANKING_CHALLENGE_SCAN		"%d %d %d"
#define IN_RANKING_CHALLENGE_PRINT		"%d %d %d"
#define OUT_RANKING_CHALLENGE_SCAN		"%d %d %d %d %d %d %s %s"
#define OUT_RANKING_CHALLENGE_PRINT		"%d %d %d %d %d %d %s %s"


#define IN_EXCHANGE_GIFT_SCAN			"%d %30s"	// eid key_code
#define IN_EXCHANGE_GIFT_PRINT			"%d %.30s"	// eid key_code

#define IN_FIGHT_LOAD_DECK_SCAN			"%d %d" 	// eid game_type
#define IN_FIGHT_LOAD_DECK_PRINT		"%d %d"		// eid game_type

#define IN_LOAD_HERO_DATA_SCAN			"%d %n"	// eid
#define IN_LOAD_HERO_DATA_PRINT			"%d%s"
#define OUT_LOAD_HERO_DATA_SCAN			"%d %d %d %d %n"
#define OUT_LOAD_HERO_DATA_PRINT		" %d %d %d %d"
#define OUT_LOAD_HERO_MISSION_SCAN		"%d %d %d %n"
#define OUT_LOAD_HERO_MISSION_PRINT		" %d %d %d"

////////////////////////////////////////////////////////
// this is for testing
int dbin_once(const char *in_buffer, char *out_buffer);

// core logic !  the pthread entry : ptr is the fd
void * dbio(void * ptr);

// must fit into in_list[x]  @see dbio.cpp
#define	DB_TEST		0
#define DB_REGISTER	1
#define DB_LOGIN	2
#define DB_LOAD_CARD	3
#define DB_LOAD_DECK	4
/////////////////////////////
#define DB_SAVE_CARD	5
#define DB_SAVE_DECK	6
#define DB_ALIAS		7
#define DB_GAME		8
#define DB_WIN		9
/////////////////////////////
#define DB_SAVE_REPLAY	10
#define DB_LIST_REPLAY	11	 
#define DB_LOAD_REPLAY	12	
#define DB_SAVE_DEBUG	13
#define DB_LOAD_DEBUG	14
/////////////////////////////
#define DB_QUICK		15
#define DB_STATUS		16
#define DB_BUY_CARD		17
#define DB_SELL_CARD	18
#define DB_CCCARD		19
/////////////////////////////
#define DB_LOAD_BATCH	20
#define DB_SAVE_BATCH	21
#define DB_PICK			22
#define DB_ADD_EXCHANGE	23
#define DB_BUY_EXCHANGE	24
/////////////////////////////
#define DB_LIST_EXCHANGE	25
#define DB_RESET_EXCHANGE	26
#define DB_LIST_GUILD		27
#define DB_CREATE_GUILD		28
#define DB_DELETE_GUILD		29
/////////////////////////////
#define DB_GUILD_APPLY		30
#define DB_GUILD_POS		31
#define DB_GUILD_QUIT		32
#define DB_GUILD_LMEMBER	33
#define DB_GUILD_DEPOSIT	34
/////////////////////////////
#define DB_GUILD_BONUS		35
#define DB_LIST_DEPOSIT		36
#define DB_CREATE_LADDER	37
#define DB_GET_LADDER		38
#define DB_UPDATE_PROFILE	39
/////////////////////////////
#define DB_FRIEND_ADD		40
#define DB_FRIEND_LIST		41
#define DB_FRIEND_STA		42
#define DB_FRIEND_SEARCH	43
#define DB_GUILD			44
/////////////////////////////
#define DB_DEPOSIT			45
#define DB_GLV				46
#define DB_GLEVELUP			47
#define DB_LOAD_PIECE		48
#define DB_PICK_PIECE		49
/////////////////////////////
#define DB_MERGE_PIECE		50
#define DB_PAY				51
#define DB_GET_COURSE		52
#define DB_SAVE_COURSE		53
#define DB_CHALLENGE		54
/////////////////////////////
#define DB_LOAD_MISSION		55
#define DB_SAVE_MISSION		56
#define DB_LOAD_SLOT		57
#define DB_SAVE_SLOT		58
#define DB_RENAME_SLOT		59
/////////////////////////////
#define DB_BUY_SLOT			60
#define DB_MISSION_REWARD	61
#define DB_SLOT_LIST		62
#define DB_ADD_MATCH		63
#define DB_MATCH_APPLY		64
/////////////////////////////
#define DB_MATCH_CANCEL				65
#define DB_MATCH_TEAM_INIT			66
#define DB_UPDATE_MATCH_PLAYER		67
#define DB_MATCH_ELI_INIT			68
#define DB_MATCH_APPLY_AI			69
/////////////////////////////
#define DB_UPDATE_MATCH				70
#define DB_FRIEND_DEL				71
#define DB_INIT_RANKING				72
#define DB_RANKING_LIST				73
#define DB_RANKING_TARGETS			74
/////////////////////////////
#define DB_CHECK_RANKING_TARGET		75
#define DB_CHANGE_RANKING_DATA		76
#define DB_GET_RANKING_HISTORY		77
#define DB_SAVE_RANKING_CHALLENGE	78
#define DB_RESET_RANKING_TIME		79
/////////////////////////////
#define DB_CHECK_LOGIN				80
#define DB_LIST_EVIL_MESSAGE		81
#define DB_READ_EVIL_MESSAGE		82
#define DB_RANK_REWARD				83
#define DB_RANKING_CHALLENGE		84
/////////////////////////////
#define DB_ADD_EVIL_MESSAGE			85
#define DB_LOTTERY					86
#define DB_EXCHANGE_GIFT			87
#define DB_GATE						88
#define DB_RESET_FIGHT_TIMES		89
/////////////////////////////
#define DB_UPDATE_GATE_POS			90
#define DB_FIGHT					91
#define DB_TOWER					92
#define DB_TOWER_RESULT				93
#define DB_TOWER_INFO				94
/////////////////////////////
#define DB_TOWER_BUFF				95
#define DB_TOWER_LADDER				96
#define DB_TOWER_REWARD				97
#define DB_SOLO						98
#define XXX_DB_UPDATE_CHAPTER_POS		99 // evil_status.chapter_pos is useless now
/////////////////////////////
#define DB_FIGHT_ROBOT				100
#define DB_UPDATE_SIGNALS			101
#define DB_CHAPTER					102
#define DB_GET_CHAPTER				103
#define DB_REPLACE_CHAPTER			104
/////////////////////////////
#define DB_CHAPTER_REWARD			105
#define DB_LOAD_HERO_DATA			106
#define DB_SUBMIT_HERO_MISSION		107
#define DB_UPDATE_HERO_MISSION		108
#define DB_LOAD_CARD_PIECE			109
/////////////////////////////
#define DB_LOAD_HERO_DECK			110
#define DB_LIST_HERO_SLOT			111
#define DB_GET_HERO_SLOT			112
#define DB_INSERT_HERO_SLOT			113
#define DB_UPDATE_HERO_SLOT			114
/////////////////////////////
#define DB_CHOOSE_HERO_SLOT			115
#define DB_GET_DAILY_LOGIN			116
#define DB_GET_DAILY_REWARD			117
#define DB_UPDATE_PIECE_SHOP		118
#define DB_GET_PIECE_SHOP			119
/////////////////////////////
#define DB_REFRESH_PIECE_SHOP		120
#define DB_PIECE_BUY				121
#define DB_ADMIN_ADD_ROBOT			122
#define DB_INIT_ARENA				123
#define DB_ARENA_TOP_LIST			124
/////////////////////////////
#define DB_ARENA_TARGET				125
#define DB_EXCHANGE_ARENA_RANK		126
#define DB_ARENA_GAME				127
#define DB_UPDATE_ARENA_TIMES		128
#define DB_GET_ARENA_REWARD			129
/////////////////////////////
#define DB_MONEY_EXCHANGE			130
#define DB_RESET_ARENA_TIMES		131
#define DB_GUILD_SEARCH				132


// share between dbio.cpp and nio.cpp (implement in dbio.cpp)
long unix_to_pts(time_t now);

/////////////////// DBIO END ] //////////////////
/////////////////////////////////////////////////


/////////////////////////////////////////////
////////// db_conn function start [ /////////

int db_init();
int db_clean();

// return >=0 for OK, which is eid
int db_register_user(evil_user_t * user, const char *username, 
	const char *password, const char *alias);
int db_login(evil_user_t * user, char *username, char *password);
int db_alias(int eid, const char *alias); 
int db_load_status(evil_user_t* user);
int db_save_status(evil_user_t* user);

int db_save_card(int eid, const char *card_array);
int db_save_deck(int eid, const char *card_array);
int db_load_card(int eid, char *card_array); 
int db_load_deck(int eid, char *card_array); 

int db_cccard(int eid, int card_id, int card_num); 

int db_win(int win_eid, int lose_eid, double elo_diff);
int db_draw(int eid0, int eid1);
int db_save_replay(long gameid, int win, int seed, int ver, int eid1, int eid2
, char *deck1, char *deck2, char *cmd);
int db_load_replay(const char *alias1, const char *alias2, game_t * pgame);

int db_load_shop(int cid, int size, char* buffer);

int db_save_debug(int eid, const char * filename, const char *content);
int db_load_debug(const char * filename, char *content);


////////// db_conn function end ] /////////
///////////////////////////////////////////

///////// db_design /////
int db_design_init();
int db_design_clean();
int db_design_load_shop(shop_t * shop_list);
int db_design_load_std_deck(std_deck_t * std_deck_list);
int db_design_load_ai(ai_t * ai_list);
int db_design_load_pick(pick_t *pick_list);
int db_design_load_constant(constant_t *cons);
int db_design_load_notice(notice_t * notice_list);
int db_design_load_exp(int * exp_list);
int db_design_load_guild(design_guild_t *design_guild_list);
int db_design_load_merge(design_merge_t *design_merge_list);
int db_design_load_pay(design_pay_t *design_pay_list);
int db_design_load_version(design_version_t *version);
int db_design_load_website(design_website_t *design_website_list);
int db_design_load_mission(design_mission_t  *mission_list);
int db_design_load_slot(design_slot_t *slot_list);
int db_design_load_rank_reward(design_rank_reward_t *reward_list);
int db_design_load_rank_time(design_rank_time_t *time_list);
int db_design_load_match(match_t * match);
int db_design_load_match_player(match_t * match);
int db_design_load_lottery(design_lottery_t *lottery);
int db_design_load_fight_schedule(design_fight_schedule_t *fight_schedult_list, int &max_fight_schedule);
int db_design_load_gate(design_gate_t *gate_list);
int db_design_load_gate_msg(design_gate_msg_t *msg_list);
int db_design_load_tower_reward(design_tower_reward_t *reward_list);
int db_design_load_piece_shop(design_piece_shop_t *shop_list, int *weight_max);
int db_design_load_solo(solo_t * solo_list);
int db_design_load_monthly(int *design_pay_monthly_list, int &max_count);
int db_design_load_robot(design_robot_t * robot_list, design_robot_t *fakeman_list);
int db_design_load_chapter(design_chapter_t *chapter_list);
int db_design_load_hero_mission(design_mission_hero_t *hero_list);
int db_design_load_hero(design_hero_t *hero_list);
int db_design_load_card_chapter(design_card_chapter_t *card_chapter_list);
int db_design_load_hero_slot(design_hero_slot_t *hero_slot_list);
int db_design_load_achievement(design_achi_t *achi_list);
int db_design_load_daily_login(design_daily_login_t *daily_login_list);
int db_design_load_arena_reward(design_arena_reward_t *reward_list, int &reward_count);
int db_design_load_quick_reward(design_quick_reward_list_t *reward_list);
int db_design_load_chapter_dialog(design_chapter_dialog_t *dialog_list);
///////// db_design /////


////////// WCHAT START [ //////////
int wchat_head_ts();
int wchat_tail_ts();
int wchat_init(int head_ts);  // usually wchat_init(0);
int wchat_add(const char * msg);
const char * wchat_get(int ts);
int wchat_ts_check(int ts);
int wchat_index_ts(int index);

// this is for debug
int wchat_print();

////////// WCHAT END ] //////////


////////// MISSION START [ ///////
// user press get reward on OK mission
int mission_finish(mission_t *mlist, design_mission_t *dlist, int mid);
// update: trigger from game logic
int mission_update(mission_t *mlist, design_mission_t *dlist, int mtype, int p1, int p2, int p3, int guild_lv);
// refresh: when lv up OR mission_finish()==0 do this
int mission_refresh(mission_t *mlist, design_mission_t *dlist, int lv, int guild_lv, const char *card_list) ;
int mlist_size(mission_t * mlist);  // size of valid mission in list
design_mission_t & get_design_mission(design_mission_t *dlist, int mid);

// print mission for debug:
int print_mission_list(mission_t * mlist, design_mission_t *dlist);
int print_design_mission_list(design_mission_t * dlist);
int print_design_mission(design_mission_t & dmis);
int print_mission(mission_t & mis, design_mission_t *dlist);
////////// MISSION END ] /////////



////////// MATCH START [ ///////

int match_create(match_t &match, int max_player, time_t start_time, time_t round_time_list[MAX_DAILY_ROUND], const char * title);
int match_apply(match_t &match, int eid, int icon, const char * alias);
int match_cancel(match_t &match, int eid);
int match_team_init(match_t &match);
int match_result(match_t &match, int eid1, int eid2, int winner, int upper);
int match_next(match_t &match, int eid, match_player_t & oppo_player);

int match_team_data(match_t &match, int team_id, char * buffer);
int match_eli_data(match_t &match, int tid, char * buffer);
match_player_t & get_player_last_round(match_t & match, int eid);

int match_eli_init(match_t &match);

// print match for debug:
int match_info(match_t& match, match_player_t *player_list);
int print_player(match_player_t &player);
int print_all_record(match_t & match, match_player_t *player_list);
int match_eli_info(match_t& match, match_player_t *player_list);
int print_all_eli_record(match_t & match, match_player_t *player_list);

////////// MATCH END ] ///////

#endif  // __EVIL_H__

