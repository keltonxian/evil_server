extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fatal.h"
}
#include <string>
#include "evil.h"

int print_achi_list(design_achi_t *dlist, evil_achi_t *alist)
{
	printf("----------print_achi_list----------\n");
	for (int i=0; i<MAX_ACHIEVEMENT; i++) {
		design_achi_t & dach = dlist[i];
		if (dach.aid == 0) {
			continue;
		}

		evil_achi_t & achi = alist[i];
		if (achi.aid == 0) {
			continue;
		}
		printf("aid=%d atype=%d status=%d n1=%d ", achi.aid, dach.atype, achi.status, achi.n1);
		printf("p1=%d p2=%d ", dach.p1, dach.p2);
		printf("p3=[%.400s]\n", dach.p3);

	}
	printf("\n");
	return 0;
}

int update_achievement(evil_achi_t *alist, design_achi_t *dlist, int atype, evil_user_t * euser)
{
	int change = 0;
	for (int i=0; i<MAX_ACHIEVEMENT; i++) {
		evil_achi_t & achi = alist[i];
		if (achi.aid == 0) {
			continue;
		}
		if (achi.status >= ACHIEVEMENT_STATUS_OK) {
			continue;
		}

		// if atype == 0, update all achi
		design_achi_t & dach = dlist[i];
		if (atype != 0 && dach.atype != atype) {
			continue;
		}

		switch (dach.atype) {
			case ACHIEVEMENT_LV:
			{
				achi.n1 = euser->lv;
				if (achi.n1 >= dach.p1) {
					achi.status = ACHIEVEMENT_STATUS_OK;
					change = 1;
				}
				break;
			}
			case ACHIEVEMENT_CARD_LIST:
			{
				int total_card = 0;
				achi.n1 = 0;
				for (int j = 0; j < EVIL_CARD_MAX; j++) {
					int count = dach.p3[j] - '0';
					if (count == 0) {
						continue;
					}
					total_card++;
					if (euser->card[j] - '0' < count) {
						continue;
					}
					achi.n1++;
				}
				if (achi.n1 >= total_card) {
					achi.status = ACHIEVEMENT_STATUS_OK;
					change = 1;
				}
				break;
			}
			case ACHIEVEMENT_CHAPTER:
			{
				evil_chapter_data_t &chapter_data = euser->chapter_data_list[dach.p2];
				const int stage_size = strlen(chapter_data.star_data);
				achi.n1 = 0;
				for (int s = 0; s < stage_size; s++) {
					int star = chapter_data.star_data[s] - '0';
					if (star == CHAPTER_DATA_STATUS_START
					|| star == CHAPTER_DATA_STATUS_LOCK) {
						continue;
					}
					if (star < dach.p1) {
						continue;
					}
					achi.n1++;
				}
				if (achi.n1 >= stage_size) {
					achi.status = ACHIEVEMENT_STATUS_OK;
					change = 1;
				}
				break;
			}
			case ACHIEVEMENT_MERGE:
			{
				achi.n1 = euser->merge_times;
				if (achi.n1 >= dach.p1) {
					achi.status = ACHIEVEMENT_STATUS_OK;
					change = 1;
				}
				break;
			}
			default:
			{
				BUG_PRINT(-7, "update_achievement:design_atype_error %d aid %d"
				, dach.atype, dach.aid);
				break;
			}
		}

	}
	return change;
}

int refresh_achievement(evil_achi_t *alist, design_achi_t *dlist, evil_user_t * euser)
{
	int change = 0;
	for (int i=0; i<MAX_ACHIEVEMENT; i++) {
		design_achi_t & dach = dlist[i];
		if (dach.aid == 0) {
			continue;
		}

		if (dach.pre != 0) {
			evil_achi_t & pre = alist[dach.pre];
			if (pre.aid == 0) {
				continue;
			}
			if (pre.status != ACHIEVEMENT_STATUS_FINISH) {
				continue;
			}
		}

		evil_achi_t & achi = alist[i];
		if (achi.aid != 0) {
			continue;
		}

		achi.aid = dach.aid;
		achi.status = ACHIEVEMENT_STATUS_READY;
		change = 1;
	}

	change |= update_achievement(alist, dlist, 0, euser);
	return change;
}

int finish_achievement(evil_achi_t *alist, design_achi_t *dlist, int aid, evil_user_t * euser)
{
	design_achi_t & dach = dlist[aid];
	if (dach.aid == 0) {
		ERROR_RETURN(-3, "finish_achievement:no_such_achievement %d", aid);	
	}

	evil_achi_t & achi = alist[aid];
	if (achi.aid == 0) {
		ERROR_RETURN(-13, "finish_achievement:not_accept_achievement %d", aid);	
	}

	if (achi.status == ACHIEVEMENT_STATUS_FINISH) {
		ERROR_RETURN(-6, "finish_achievement:already_finish_achievement %d", aid);	
	};

	if (achi.status != ACHIEVEMENT_STATUS_OK) {
		ERROR_RETURN(-6, "finish_achievement:achievement_not_ok %d %d", aid, achi.status);	
	};

	achi.status = ACHIEVEMENT_STATUS_FINISH;

	return 0;
}


// refersh_achi()
// update_achi();
// compare p1, n1

// update_achi()

// finish_achi()
// refresh_achi()
// update_achi()

#ifdef TTT


static int init_user(evil_user_t *puser)
{
	puser->lv = 1;
	puser->merge_times = 0;
	sprintf(puser->chapter_data_list[1].star_data, "12222");
	sprintf(puser->card, "%.400s", "1111100000111110000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000");

	return 0;
}

int test0_empty(design_achi_t *dlist)
{
	return 0;
}

int test1_lv(design_achi_t *dlist)
{
	int ret;
	evil_user_t user;
	bzero(&user, sizeof(user));
	init_user(&user);
	evil_user_t *euser = &user;

	int change = 0;

	change = refresh_achievement(euser->achi_list, dlist, euser);
	DEBUG_PRINT(0, "test1_lv:refresh change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	euser->lv = 2; // update lv

	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_LV, euser);
	DEBUG_PRINT(0, "test1_lv:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	ret = finish_achievement(euser->achi_list, dlist, 1, euser);
	DEBUG_PRINT(0, "test1_lv:finish ret=%d", ret);
	print_achi_list(dlist, euser->achi_list);

	change = refresh_achievement(euser->achi_list, dlist, euser);
	DEBUG_PRINT(0, "test1_lv:refresh change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	return 0;
}

int test2_card_list(design_achi_t *dlist)
{
	int ret;
	evil_user_t user;
	bzero(&user, sizeof(user));
	init_user(&user);
	evil_user_t *euser = &user;

	int change = 0;

	change = refresh_achievement(euser->achi_list, dlist, euser);
	DEBUG_PRINT(0, "test2_card_list:refresh change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	euser->card[5] = '1';
	
	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_CARD_LIST, euser);
	DEBUG_PRINT(0, "test2_card_list:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	euser->card[6] = '1';
	euser->card[7] = '1';
	euser->card[8] = '1';
	euser->card[9] = '1';
	/*
	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_CARD_LIST, euser);
	DEBUG_PRINT(0, "test2_card_list:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);
	*/

	change = update_achievement(euser->achi_list, dlist, 0, euser);
	DEBUG_PRINT(0, "test2_card_list:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	ret = finish_achievement(euser->achi_list, dlist, 4, euser);
	DEBUG_PRINT(0, "test2_card_list:finish ret=%d", ret);
	print_achi_list(dlist, euser->achi_list);

	change = refresh_achievement(euser->achi_list, dlist, euser);
	DEBUG_PRINT(0, "test2_card_list:refresh change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	euser->card[15] = '1';
	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_CARD_LIST, euser);
	DEBUG_PRINT(0, "test2_card_list:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	ret = finish_achievement(euser->achi_list, dlist, 5, euser);
	DEBUG_PRINT(0, "test2_card_list:finish ret=%d", ret);
	print_achi_list(dlist, euser->achi_list);

	change = refresh_achievement(euser->achi_list, dlist, euser);
	DEBUG_PRINT(0, "test2_card_list:refresh change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	return 0;
}

int test3_chapter(design_achi_t *dlist)
{
	int ret;
	evil_user_t user;
	bzero(&user, sizeof(user));
	init_user(&user);
	evil_user_t *euser = &user;

	int change = 0;

	change = refresh_achievement(euser->achi_list, dlist, euser);
	DEBUG_PRINT(0, "test3_chapter:refresh change=%d", change);
	print_achi_list(dlist, euser->achi_list);
	
	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_CHAPTER, euser);
	DEBUG_PRINT(0, "test3_chapter:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	sprintf(euser->chapter_data_list[1].star_data, "22223");
	
	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_CHAPTER, euser);
	DEBUG_PRINT(0, "test3_chapter:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	ret = finish_achievement(euser->achi_list, dlist, 7, euser);
	DEBUG_PRINT(0, "test3_chapter:finish ret=%d", ret);
	print_achi_list(dlist, euser->achi_list);

	change = refresh_achievement(euser->achi_list, dlist, euser);
	DEBUG_PRINT(0, "test3_chapter:refresh change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	sprintf(euser->chapter_data_list[1].star_data, "23333");
	
	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_CHAPTER, euser);
	DEBUG_PRINT(0, "test3_chapter:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	ret = finish_achievement(euser->achi_list, dlist, 8, euser);
	DEBUG_PRINT(0, "test3_chapter:finish ret=%d", ret);
	print_achi_list(dlist, euser->achi_list);

	sprintf(euser->chapter_data_list[1].star_data, "33333");
	
	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_CHAPTER, euser);
	DEBUG_PRINT(0, "test3_chapter:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	ret = finish_achievement(euser->achi_list, dlist, 8, euser);
	DEBUG_PRINT(0, "test3_chapter:finish ret=%d", ret);
	print_achi_list(dlist, euser->achi_list);

	return 0;
}

int test4_merge(design_achi_t *dlist)
{
	int ret;
	evil_user_t user;
	bzero(&user, sizeof(user));
	init_user(&user);
	evil_user_t *euser = &user;

	int change = 0;

	change = refresh_achievement(euser->achi_list, dlist, euser);
	DEBUG_PRINT(0, "test4_merge:refresh change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	euser->merge_times = 1;
	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_MERGE, euser);
	DEBUG_PRINT(0, "test4_merge:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	euser->merge_times = 2;
	change = update_achievement(euser->achi_list, dlist, ACHIEVEMENT_MERGE, euser);
	DEBUG_PRINT(0, "test4_merge:update change=%d", change);
	print_achi_list(dlist, euser->achi_list);


	ret = finish_achievement(euser->achi_list, dlist, 9, euser);
	DEBUG_PRINT(0, "test4_merge:finish ret=%d", ret);
	print_achi_list(dlist, euser->achi_list);

	change = refresh_achievement(euser->achi_list, dlist, euser);
	DEBUG_PRINT(0, "test4_merge:refresh change=%d", change);
	print_achi_list(dlist, euser->achi_list);

	return 0;
}

typedef int (*testcase_t) (design_achi_t *); 

testcase_t test_list[] = {
	test0_empty
,	test1_lv
,	test2_card_list
,	test3_chapter
,	test4_merge
};


int test_selector(design_achi_t *dlist, int testcase)
{
	int testmax = sizeof(test_list) / sizeof(test_list[0]);

	printf("RUN test%d:\n", testcase);
	if (testcase < 0 || testcase >= testmax) {
		printf("ERR invalid testcase %d\n", testcase);
		return -2;
	}

	int ret = test_list[testcase](dlist);
	
	printf("RET %d\n", ret);
	if (ret != 0 ) {
		printf("XXXXXXXXX BUG ret!=0: %d\n", ret);
	}
	
	return ret;
}

int main(int argc, char *argv[])
{
	printf("hello achievement\n");
	design_achi_t dach;
	design_achi_t dlist[MAX_ACHIEVEMENT];

	bzero(dlist, sizeof(dlist));


/*
typedef struct {
	int aid; 
	char name[302]; 
	char msg[500];

	int pre; 
	int atype; 
	int p1;
	int p2;
	int p3[50]; // for card list achievement

	int reward_gold; 
	int reward_crystal; 
	int reward_card[10]; 
	int reward_piece[10]; 
} design_achi_t;
*/
	/*
	{
		aid, name, msg
		, pre, atype, p1, p2, p3[]
		, reward_gold, reward_crystal, reward_card[10], reward_piece[10]
	}
	*/

	{
		design_achi_t tmp = {1, "achi_1", "achi_1_msg"
		, 0, ACHIEVEMENT_LV, 2, 0, ""
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;

	{
		design_achi_t tmp = {2, "achi_1", "achi_1_msg"
		, 1, ACHIEVEMENT_LV, 3, 0, ""
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;

	{
		design_achi_t tmp = {3, "achi_1", "achi_1_msg"
		, 2, ACHIEVEMENT_LV, 4, 0, ""
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;

	{
		design_achi_t tmp = {4, "achi_4", "achi_1_msg"
		, 0, ACHIEVEMENT_CARD_LIST, 0, 0, "1111111111000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;

	{
		design_achi_t tmp = {5, "achi_4", "achi_1_msg"
		, 4, ACHIEVEMENT_CARD_LIST, 0, 0, "0000000000111111000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;

	{
		design_achi_t tmp = {6, "achi_4", "achi_1_msg"
		, 5, ACHIEVEMENT_CARD_LIST, 0, 0, "0000000000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;

	/*
	{
		aid, name, msg
		, pre, atype, p1, p2, p3[50]
		, reward_gold, reward_crystal, reward_card[10], reward_piece[10]
	}
	*/

	{
		design_achi_t tmp = {7, "achi_4", "achi_1_msg"
		, 0, ACHIEVEMENT_CHAPTER, 2, 1, {}
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;

	{
		design_achi_t tmp = {8, "achi_4", "achi_1_msg"
		, 7, ACHIEVEMENT_CHAPTER, 3, 1, {}
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;

	{
		design_achi_t tmp = {9, "achi_4", "achi_1_msg"
		, 0, ACHIEVEMENT_MERGE, 2, 0, {}
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;

	{
		design_achi_t tmp = {10, "achi_4", "achi_1_msg"
		, 9, ACHIEVEMENT_MERGE, 5, 0, {}
		, 0, 0, {0}, {0}};

		dach = tmp;
	}
	dlist[dach.aid] = dach;


	if (argc > 1 && strcmp("all", argv[1])==0) {
		int testmax = sizeof(test_list) / sizeof(test_list[0]);
		int error_count = 0;
		for (int i=0; i<testmax; i++) {
			int ret;
			ret = test_selector(dlist, i); // changed argv
			if (ret != 0) { error_count++; break;}
		}
		printf("TEST ALL SUMMARY: error_count=%d\n", error_count);
	} else if (argc > 1) {
		int testcase = atoi(argv[1]);
		test_selector(dlist, testcase);
	} else {
		int testmax = sizeof(test_list) / sizeof(test_list[0]);
		int testcase = testmax - 1 ; // TESTCASE_MAX-1;
		test_selector(dlist, testcase);
	}

	return 0;
}
#endif
