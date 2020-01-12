
#include "evil.h"
#include "fatal.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>

using namespace std;


//////////  PRINT START //////////

// (header)  [mtype]  {status/last}
int print_mission(mission_t & mis, design_mission_t * dlist)
{
	// TODO change the date
	char *time_str;
	time_str = asctime(localtime(&mis.last_update));
	design_mission_t &dmis = get_design_mission(dlist, mis.mid);
	printf("mission:  mid,status=(%d,%d)  n1=[%d]  last=%ld   daily=%d p1,p2,p3=%d,%d,%d, time=%s\n"
		, mis.mid, mis.status , mis.n1 , mis.last_update, dmis.daily
		, dmis.p1, dmis.p2, dmis.p3, time_str);
	return 0;
}


// (header)  [mtype]  {reward}
int print_design_mission(design_mission_t & dmis)
{
	char tmp_buffer[1000];
	char *tmp_ptr;
	tmp_ptr = tmp_buffer;
	tmp_ptr += sprintf(tmp_ptr, "card_count[%d] card[", dmis.card_count);
	for (int cdx = 0; cdx < dmis.card_count; cdx++) {
		tmp_ptr += sprintf(tmp_ptr, "%d ", dmis.reward_card[cdx]);
	}
	tmp_ptr += sprintf(tmp_ptr, "] piece_count[%d] piece["
	, dmis.piece_count);
	for (int pdx = 0; pdx < dmis.piece_count; pdx++) {
		tmp_ptr += sprintf(tmp_ptr, "%d:%d "
		, dmis.reward_piece[pdx][0], dmis.reward_piece[pdx][1]);
	}
	tmp_ptr += sprintf(tmp_ptr, "]");
	printf("design_mission: mid,pre,lv,hero,daily,guild_lv=(%d,%d,%d,%d,%d,%d)   mtype,p1,p2,p3=[%d,%d,%d,%d]   %s  exp,gold,crystal={%d,%d,%d} reset_time=%s  mtext=%s\n"
		, dmis.mid, dmis.pre, dmis.lv, dmis.hero, dmis.daily
		, dmis.mtype, dmis.guild_lv
		, dmis.p1, dmis.p2, dmis.p3
		, tmp_buffer
		, dmis.reward_exp, dmis.reward_gold, dmis.reward_crystal
		, dmis.reset_time, dmis.mtext);
		
	return 0;
}

int print_design_mission_list(design_mission_t * dlist)
{
	int count = 0;
	printf("design_mission_list\n");
	for (int i = 1; i < MAX_MISSION; i++) {
		design_mission_t & mis = dlist[i];
		// mid == 0 is normal
		if (mis.mid == 0) {
			// WARN_PRINT(-7, "00design_mission");
			continue;
		}

		print_design_mission(mis);
		count++;
	}
	/*
	for (D_MISSION_MAP::iterator it=dlist.begin(); it!=dlist.end();it++) {
//		if ((*it).second.mid == 0) continue;
		if ((*it).second.mid == 0) {
			WARN_PRINT(-7, "00design_mission");
			continue;
		}
		print_design_mission( (*it).second );
	}
	*/
	printf("design_list count=%d\n", count);
	printf("----------\n");
	return 0;
}

int print_mission_list(mission_t *mlist, design_mission_t *dlist)
{
	for (int i=0; i<MAX_MISSION; i++) {
		// if size == 0, it != mlist.end()
		if (mlist[i].mid == 0) {
			continue;
		}
		print_mission( mlist[i], dlist );
	}

	printf("----------\n");
	return 0;
}

//////////  PRINT END //////////


////////// UTILITY START //////////

// reset_time = "hh:ii"  (hour:minute) , e.g. "19:03"
// now = time(NULL);
// time_t day_time_offset(time_t day, char *tt)
// e.g. day = now (today)
//      tt = "19:03"
// output: today 19:03 in time_t format
time_t reset_time(time_t now, const char *rtime_str) {

    struct tm timestruct;
	time_t rtime;
	int ret;

    localtime_r(&now, &timestruct);

    timestruct.tm_sec = 0;
    timestruct.tm_min = 0;
    timestruct.tm_hour = 0;

	ret = sscanf(rtime_str, "%d:%d", &timestruct.tm_hour, &timestruct.tm_min);
	if (ret != 2) {
		printf("BUGBUG reset_time:sscanf %d", ret);
	}

    rtime = mktime(&timestruct);

	// printf("reset_time: now=%ld rtime=%ld  [%s]\n", now, rtime
	// , asctime(&timestruct));

	return rtime;
}
////////// UTILITY END //////////



int mlist_size(mission_t * mlist)
{
	int size = 0;
	for (int i=0; i<MAX_MISSION; i++) {
		if (mlist[i].mid != 0) {
			size ++;
		}
	}
	return size;
}


// input: dmis, lv, card_list
// output: mis (ref)
// assume card_list[400] is valid
int mission_from_design_mission(mission_t &mis, design_mission_t &dmis, int lv, const char * card_list) {
	int card_id;
	int change = 0;
	bzero(&mis, sizeof(mis));

	mis.mid = dmis.mid;
	mis.n1 = 0; //dmis.p1; // core logic from user data
	mis.status = MISSION_STATUS_READY;
	// mis.last_update = time(NULL); // keep mission changes

	// use this: if we have buffer overflow, use last_update=0
	/**
	if (mis.daily > 0) {
		mis.last_update = time(NULL);
	} else {
		mis.last_update = 0; // save buffer space
	}
	**/

	switch (dmis.mtype) {
		case MISSION_LEVEL:
			mis.n1 = lv;
			break;
		case MISSION_CARD:
			card_id = dmis.p2; // base 1
			if (card_id <= 0 || card_id > EVIL_CARD_MAX) {
				ERROR_RETURN(-2, "mission_from_design_mission card_id out_bound %d", card_id);
			}
			// mis.n1  = (int)card_list[card_id];  // ??? - '0' ??
			mis.n1  = card_list[card_id-1] - '0';  // ??? - '0' ??
			break;

		case MISSION_COLLECTION:
			int collect = 0;
			// card_list is base 0
			for (int i=0; i< EVIL_CARD_MAX; i++) {
				if (card_list[i] > '0') {  // ??? - '0'
					collect ++;
				}
			}
			mis.n1 = collect;
			break;
	}
	// rest of the MISSION_XXX is 0 and dynamically fill up in game

	// some new mission is already ok, so it can collect reward now
	// so we need a change notification
	if (mis.n1 >= dmis.p1) {
		mis.status = MISSION_STATUS_OK; // target reached
		mis.last_update = time(NULL);   // reset logic
		change = MISSION_UP_OK;  // OK mission, not yet got reward
	}

	return change;
}


design_mission_t & get_design_mission(design_mission_t *dlist, int mid)
{
	static design_mission_t empty_dmis = {0, 0, 0, 0, 0, 0,   0, 0,0,0
		,  0,0,0, 0, 0,{}, 0,{},  ""};
	// design_mission_t &dmis = *(dlist + mid);
	design_mission_t &dmis = dlist[mid];
	if (dmis.mid == 0) {
		// this is ok, when delete mission in design_mission
		// BUG_PRINT(-3, "get_design_mission:not_found %d", mid);
		return empty_dmis;
	}

	return dmis;
	/*
	static design_mission_t empty_dmis = {0, 0, 0, 0, 0,   0, 0,0,0}
	
	D_MISSION_MAP::iterator it;
	it = dlist.find(mid);
	if (it == dlist.end()) {
		BUG_PRINT(-3, "get_design_mission:not_found %d", mid);
		return empty_dmis;
	}
	return (*it).second;
	*/
}
	

// status from MISSION_STATUS_OK -> MISSION_STATUS_FINISH
// donot check n1, p1
// input: mlist, dlist, mid
// output: mlist.status change
int mission_finish(mission_t *mlist, design_mission_t *dlist, int mid)
{
	design_mission_t & dmis = get_design_mission(dlist, mid);
	if (dmis.mid == 0) {
		ERROR_RETURN(-3, "mission_finish:mid_not_found %d", mid);
	}
	

	if (0==mlist[mid].mid) {
		ERROR_RETURN(-33, "mission_finish:not_found mid=%d", mid);
	}


	mission_t & mis = mlist[mid];  // XXX ?
	if (mis.mid != mid) {
		ERROR_RETURN(-43, "mission_finish:mid_mismatch %d %d"
		, mis.mid, mid);
	}
	// here: mid match, double check mis.p1, update mis, 

	if (mis.status == MISSION_STATUS_NULL) {
		ERROR_RETURN(-8, "mission_finish:status_null %d/%d",
			mis.n1, dmis.p1);
	}

	if (mis.status == MISSION_STATUS_FINISH) {
		ERROR_RETURN(-18, "mission_finish:status_finish %d/%d",
			mis.n1, dmis.p1);
	}

	// this could be some minor bug that forget to do mission_update
	// STATUS_OK is the only check,  n1 and p1 check are not important
	if (mis.status != MISSION_STATUS_OK) {
		ERROR_RETURN(-6, "mission_finish:status_not_ok %d"
		, mis.status);
	}

	// peter: do not return when n1<p1, because client will assume it is done!
	// it is normal to have this when we update the mission design.
	// since:  when the mis.status is OK, mission_update() never change n1
	if (mis.n1 < dmis.p1) {
	 	WARN_PRINT(-26, "mission_finish:target_not_reach mid=%d %d/%d"
	 	, mis.mid, mis.n1, dmis.p1);
	 }

	// core logic: set status as finish
	mis.status = MISSION_STATUS_FINISH;
	return 0;
}

// note: only update n1, donot change status
// d1 is the offset, d2 and d3 are fix param
// MISSION_LEVEL :  mission_update(mlist, MISSION_LEVEL, lv, 0, 0)
// MISSION_AI:  mission_update(mlist, MISSION_AI, 1, ai_id, my_hero_id)
// note:  ai_id is the oppo ai_id,  my_hero_id : deck [1-20] id
// LEVEL, CARD, COLLECTION : p1 is the real value
// other:  p1 is the offset (usually 1)
int mission_update(mission_t *mlist, design_mission_t *dlist, int mtype, int p1, int p2, int p3, int guild_lv)
{
	int change = 0;
	for (int i=0; i<MAX_MISSION; i++) {
		if (mlist[i].mid == 0) {
			continue;
		}
		mission_t & mis = mlist[i];
		design_mission_t & dmis = get_design_mission(dlist, mis.mid);
		if (dmis.mid == 0) {
			WARN_PRINT(-3, "mission_update:design_mission_missing mid=%d"
			, mis.mid);
			continue;
		}

		if (dmis.mtype != mtype) {
			continue; // skip
		}

		// check if mission is guild mission or guild_lv ok
		if (dmis.guild_lv > guild_lv) {
			continue; // skip
		}

		// only ready/ok will update
		if (mis.status != MISSION_STATUS_READY) {
			continue; // skip if not ready
		}

		// special handling for MISSION_REPLAY and MISSION_VIEW
		// p2 = hero1,  p3=hero2  (p2 and p3 are the 2 heros in battle)
		// if dmis.p2 == 0 then OK, fall through
		// if dmis.p2 == p2 OR mis.p2 == p3 then OK, fall through
		// resolve:  if (dmis.p2==0 OR dmis.p2==p2 OR dmis.p2==p3) fall
		if (mtype == MISSION_REPLAY || mtype==MISSION_VIEW) {
			if (dmis.p2!=0 && dmis.p2!=p2 && dmis.p2!=p3) {
		 		continue;
			}
		} else {
			// if n2==0 this is wildcard: any p2 is accept, same for n3
			if (	(dmis.p2 != p2 && dmis.p2 != 0) 
			||  	(dmis.p3 != p3 && dmis.p3 != 0) ) {
				continue; // skip if fix param not match
			}
		}


		// LEVEL, CARD, COLLECTION, DECK, special handling
		// set instead of offset change
		if (mtype==MISSION_LEVEL || mtype==MISSION_CARD 
		|| mtype==MISSION_COLLECTION || mtype==MISSION_DECK
		|| mtype==MISSION_CHAPTER || mtype==MISSION_CHAPTER_STAGE
		|| mtype==MISSION_HERO_HP
		) {
			// DEBUG_PRINT(0, "mission_update: mis.n1=%d p1=%d", mis.n1, p1);
			// set n1
			if (mis.n1 != p1) {
				mis.n1 = p1; 
				change |= MISSION_UP_NUM; // for collection, deck change
			}

			// change status, last_update in mission_refresh()
			/*
			if (mis.n1 >= dmis.p1) {
				mis.status = MISSION_STATUS_OK; // OK=ready to finish
				change |= MISSION_UP_OK; // from ready to ok
			}
			*/
			continue; // done
		}

		// below are other mission 

		// peter: only check p1, no need to check n1
		if (p1 == 0) {
			BUG_PRINT(-7, "mission_update p1==0");
			// consider: return error code ?
			continue;
		}

		// core logic : update n1, and check if n1>=p1 : set STATUS_OK
		mis.n1 += p1;
		change |= MISSION_UP_NUM;
		// change statu, last_update in mission_refresh()
		/*
		if (mis.n1 >= dmis.p1) {
			mis.status = MISSION_STATUS_OK; // OK=ready to finish
			change |= MISSION_UP_OK;
		}
		*/
	}

	return change;
}


// note: mission_refresh() only change status from MISSION_STATUS_READY -> MISSION_STATUS_OK, MISSION_STATUS_FINISH -> MISSSION_STATUS_READY, also reset n1
// input dlist, mlist(finish mission), lv, data(mtype->n1)
// output mlist
// call this if:  
// 1) level up  (win_game etc.)
// 2) after mission_finish(), after get_reward(), may trigger level up?
// @return 0=no change,  >0 means some changes
// note: change > 0 : write to database
// 
int mission_refresh(mission_t *mlist, design_mission_t *dlist, int lv, int guild_lv, const char *card_list)  // card[400]
{
	int ret;
	int change = 0;

	// core logic:
	// 1.  check each mission_list, exists in design_mission_list
	// 	   remove non-exist from mission_list
	
	time_t now = time(NULL);
	long yesterday_offset = -( 24 * 60 * 60);
	for (int i=0; i<MAX_MISSION; i++) {
		if (mlist[i].mid == 0) {
			continue;
		}
		mission_t &mis = mlist[i];

		design_mission_t &dmis = get_design_mission(dlist, mis.mid);
		if (dmis.mid == 0) {
			WARN_PRINT(-7, "mission_refresh:found_unknown_mission %d"
			, mis.mid);
			bzero(mlist+i, sizeof(mission_t));  // erase mission
			change |= MISSION_UP_NUM;  // notify save_mission
			continue;
		}

		// only update status in mission_refresh()
		if (mis.n1 >= dmis.p1 && mis.status == MISSION_STATUS_READY) {
			mis.status = MISSION_STATUS_OK;
			mis.last_update = time(NULL); 
			// WARN_PRINT(-16, "mission_refresh:mission_status_mismatch mid=%d"
			// , mis.mid);
			change |= MISSION_UP_OK;
		}

		if (dmis.daily == 0 || mis.status != MISSION_STATUS_FINISH) {
			continue;
		}

		// assume: mis is daily mission and status == finish
		time_t today_reset = reset_time(now, dmis.reset_time);
		if ((now >= today_reset && mis.last_update <= today_reset)
		|| (now <= today_reset && mis.last_update <= today_reset + yesterday_offset)){
			mis.n1 = 0;
			mis.status = MISSION_STATUS_READY;
			change |= MISSION_UP_NEW; // reset mission consider new
		}

		// mission exists in design mission
		// reset logic:
		// now = time(NULL); yesterday = time(NULL) - (24*60*60)
		// if mis.daily == 1 and mis.status == MISSION_STATUS_FINISH
		//    design_mission_t &dmis =  (*dit).second
		//    today_reset = reset_time(now, dmis.reset_time)
		//    if now >= today_reset AND mis.last_update <= today_reset
		//        YES reset
		//    end
		//    if now <= today_reset AND mis.last_update <= yesterday_reset
		//        YES reset
		//    end
	}
	
	// printf("----- mlist after refresh.erase:\n");
	// print_mission_list(mlist);
	
	// printf("----- dlist after refresh.erase:\n");
	// print_design_mission_list(dlist);


	// 2.  from design_mission_list -> fill up mission_list
//	if (dlist.size() > 0) {
//		for (D_MISSION_MAP::iterator it=dlist.begin();it!=dlist.end();it++) {
//			design_mission_t &dmis = (*it).second;
		for (int i = 1; i < MAX_MISSION; i++) {
			design_mission_t &dmis = dlist[i];

			// convert dlist from verctor to array
			// mid = 0 or mtype = 0 is normal
			if (dmis.mid == 0 || dmis.mtype==0) {
//				WARN_PRINT(-7, "mission_refresh:dmis00 %d %d"
//				, dmis.mid, dmis.mtype);
				continue;
			}

			if (lv < dmis.lv) {
				continue;
			}

			// user not in guild or guild_lv not enough
			if (guild_lv < dmis.guild_lv) {
				continue;
			}

			// core logic: 
			// 

			// mlist[x].mid = x means already has mission in mlist
			// mlist[x].mid = 0 or other value, should replace
			if (mlist[dmis.mid].mid == dmis.mid) {
				// TODO remove this empty check ? 
				if (mlist[dmis.mid].status == MISSION_STATUS_OK) {
					// change |= MISSION_UP_OK;
				}
				continue;
			}

			// check pre-mission:  
			// if pre == 0, no pre-mission (skip)
			// if pre != 0, mlist[pre].status != MISSION_STATUS_FINISH
			// (skip)
			if (dmis.pre != 0) {
				mission_t &mit = mlist[dmis.pre];
				if (mit.mid==0 || mit.status != MISSION_STATUS_FINISH) {
					continue;
				}
			}

			// check hero exist
			if (dmis.hero != 0) {
				// peter: hero with same job are adjacent, e.g. 5, 6 are mage
				// index is to map 1,2->0   3,4->2,  5,6->4
				int index = ((dmis.hero-1) >> 1) << 1;
				if (index < 0 || index >= HERO_MAX) {
					BUG_PRINT(-2, "mission_refresh hero out bound %d  mid=%d"
					, dmis.hero, dmis.mid);
					continue;
				}
				// card_list[index] is base 0
				if (card_list[index] <= '0' && card_list[index+1] <= '0') {
					continue;
				}
			}

			// core logic: new mission
			mission_t mis;
			ret = mission_from_design_mission(mis, dmis, lv, card_list);
			if (ret > 0) {
				change |= ret; // usually 2 = ok mission (target reach)
			}
			mlist[dmis.mid] = mis;  // memory copy
			change |= MISSION_UP_NEW;
			// TODO : check if hero>0 and card_list[hero] > 0
		}
	//}


	return change;
}


#ifdef TTT

int test0_empty(design_mission_t *dlist, int argc, char *argv[])
{
	return 0;
}

int test1_refresh(design_mission_t *dlist, int argc, char *argv[])
{
	int lv = 0;
	int ret;
	mission_t mlist[MAX_MISSION];
	mission_t mis;

	// log x x \n lcard \n :
	const char * card_list = "1000000000000000000002200331030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	// mlist.clear();
	bzero(mlist, sizeof(mission_t) * MAX_MISSION);

	// mid, status, n1, last

	{
		mission_t tmp = {80, 3,   1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;
	{
		mission_t tmp = {81, 3,   1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;

	{
		mission_t tmp = {1, 3,   1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;

	{
		mission_t tmp = {90, 3,   1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;
	{
		mission_t tmp = {91, 3,   1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;

	{
		mission_t tmp = {2, 2,   1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;

	{
		// mid, n1, status, last
		mission_t tmp = {92, 3,   1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;
	{
		// mid, n1, status, last
		mission_t tmp = {93, 3,   1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;
	{
		// mid, n1, status, last
		mission_t tmp = {94, 3,   1, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;



//	printf("------ before refresh:\n");
//	print_mission_list(mlist);

	ret = mission_refresh(mlist, dlist, lv, 0, card_list);
	WARN_PRINT(ret, "mission_refresh_fail");

//	printf("------ after refresh:\n");
//	print_mission_list(mlist);
	// check mlist.size == 2
	int msize = mlist_size(mlist);
	ERROR_RETURN(msize != 4, "refresh:size %d", msize);

//	for (int i=0; i<MAX_MISSION; i++) {
//		mission_t & mis = mlist[i];
//		ERROR_RETURN(mis.mid >= 80, "refresh:mid>=80 exists %d", mis.mid);
//	}

	return 0;
}

int test2_update(design_mission_t *dlist, int argc, char * argv[])
{
	int lv = 1; // should be 1
	int ret;
	mission_t mlist[MAX_MISSION];
	// mission_t mis;

	// log x x \n lcard \n :
	const char * card_list = "1000000000000000000002200331030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	bzero(mlist, sizeof(mission_t) * MAX_MISSION);

	// create a clean mlist
	ret = mission_refresh(mlist, dlist, lv, 0, card_list) ;
	ERROR_NEG_RETURN(ret, "test_mission_update:refresh");


//	printf("------ before update 1111 (level):\n");
//	print_mission_list(mlist);
	int msize;
	msize = mlist_size(mlist);
	ERROR_RETURN(msize!=4, "test_update:after_refresh size=%d"
	,	msize);

	// level up to 2
	ret = mission_update(mlist, dlist, MISSION_LEVEL, 2, 0, 0, 0);
//	printf("------ after update 1111 (level 2):\n");
//	print_mission_list(mlist);
	ERROR_RETURN(mlist[1].n1!=2, "test_update:lv2 %d", mlist[1].n1);

	// level up to 3
	ret = mission_update(mlist, dlist, MISSION_LEVEL, 3, 0, 0, 0);

	mission_refresh(mlist, dlist, 3, 0, card_list);
	printf("------ after update and refresh (level 3):\n");
	print_mission_list(mlist, dlist);

	ret = mission_finish(mlist, dlist, 1); // mid=1
//	printf("------ after finish :\n");
//	print_mission_list(mlist, dlist);
	ERROR_RETURN(mlist[1].status != MISSION_STATUS_FINISH, "test_update:finish status=%d" , mlist[1].status);


	///// hard code once: mission_refresh()
	mission_refresh(mlist, dlist, 3, 0, card_list);
//	printf("------ after refresh :\n");
//	print_mission_list(mlist);	// include mid=2

	mission_refresh(mlist, dlist, 3, 0, card_list);  // refresh again, no bug

	////// ok, we are doing AI fighting!

	// win AI=5 update
	mission_update(mlist, dlist, MISSION_AI, 1, 5, 0, 0); // last=hero id

//	printf("------ after win AI 5 :\n");
//	print_mission_list(mlist);	
	ERROR_RETURN(mlist[2].n1 != 0, "test_update:ai5_8 mlist[2].n1=%d"
	, mlist[2].n1);
	ERROR_RETURN(mlist[3].n1 != 1, "test_update:ai5_8 mlist[3].n1=%d"
	, mlist[3].n1);


	// win AI=8 update
	mission_update(mlist, dlist, MISSION_AI, 1, 8, 0, 0); // last=hero id

//	printf("------ after win AI 5 and 8 :\n");
//	print_mission_list(mlist);	
	ERROR_RETURN(mlist[2].n1 != 1, "test_update:ai5_8 mlist[2].n1=%d"
	, mlist[2].n1);
	ERROR_RETURN(mlist[3].n1 != 2, "test_update:ai5_8 mlist[3].n1=%d"
	, mlist[3].n1);

	return 0;
}


int test3_finish(design_mission_t *dlist, int argc, char * argv[])
{
	int ret;
	mission_t mlist[MAX_MISSION];
	mission_t mis;
	const char * card_list = "1000000000000000000002200331030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	
	// empty all mission list
	bzero(mlist, sizeof(mission_t) * MAX_MISSION);  // mlist[...] OK

	// mid		status
	// 1		3		// bug: already finish
	// 2		2		// normal
	// 3		1		// finish will warn, still ok  (n1 reach target)
	// 4		0		// bug: not yet ready 
	// 80		2		// mid not found in dlist (BUG ???)
	{
		// mid, status, n1, last
		mission_t tmp = {1, 3,   1, 0}; // LEVEL
		// mid,status,		mtype,n1,n2,n3,		daily,last
		// mission_t tmp = {1, 3, 		MISSION_LEVEL, 1,0,0,   0, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;

	{
		// mid, status, n1, last
		mission_t tmp = {2, 2,   2, 0};	// AI
		// mid,status,		mtype,n1,n2,n3,		daily,last
		// mission_t tmp = {2, 2, 		MISSION_AI, 2,8,0,   0, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;

	{
		// mid, status, n1, last
		mission_t tmp = {3, 1,   3, 0};	// AI
		// mid,status,		mtype,n1,n2,n3,		daily,last
		// mission_t tmp = {3, 1, 		MISSION_AI, 3,0,0,   0, 0};
		mis = tmp; // warning but ok
	}
	mlist[mis.mid] = mis;

	print_mission_list(mlist, dlist);
	// order is important:  refresh  add?

	printf(">>>>> following error is normal\n");
	ret = mission_finish(mlist, dlist, 1);
	ERROR_RETURN(ret!=-18, "test_finish:already_finish %d", ret);

	ret = mission_finish(mlist, dlist, 2);
	ERROR_NEG_RETURN(ret, "test_finish:normal %d", ret);

	ret = mission_refresh(mlist, dlist, 3, 0, card_list);
	ERROR_NEG_RETURN(ret, "test_finish:refresh(1)");


	printf(">>>>> following warning is normal\n");
	ret = mission_finish(mlist, dlist, 3);
	print_mission_list(mlist, dlist);
	ERROR_NEG_RETURN(ret, "test_finish:warn_ready %d", ret);

	// after this refresh, has mid=4
	ret = mission_refresh(mlist, dlist, 3, 0, card_list);
	printf("----- after this refresh, we have mid=4\n");
	print_mission_list(mlist, dlist);
	ERROR_RETURN(ret<=0, "test_finish:refresh(2) %d", ret);

	// finish 4 is invalid
	printf(">>>>> following warn/error is normal\n");
	ret = mission_finish(mlist, dlist, 4);
	ERROR_RETURN(ret!=-6, "test_finish:target_not_reach %d", ret);

	ret = mission_update(mlist, dlist, MISSION_VS, 1, 0, 0, 0);
	ERROR_RETURN(ret<0, "test_finish:update1 %d", ret);

	ret = mission_refresh(mlist, dlist, 4, 0, card_list);

	printf(">>>>> following warn/error is normal\n");
	ret = mission_finish(mlist, dlist, 4);
	ERROR_RETURN(ret!=-6, "test_finish:target_not_reach %d", ret);

	ret = mission_update(mlist, dlist, MISSION_VS, 1, 0, 0, 0);
	ERROR_RETURN(ret<0, "test_finish:update2 %d", ret);
	ERROR_RETURN(mlist[4].n1 != 2, "test_finish:n1==2 %d", mlist[4].n1);
	ERROR_RETURN(mlist[4].status != MISSION_STATUS_READY, "test_finish:status_ready %d", mlist[4].status);

	ret = mission_refresh(mlist, dlist, 4, 0, card_list);
	ERROR_RETURN(mlist[4].status != MISSION_STATUS_OK, "test_finish:status_ok %d", mlist[4].status);

	// this is ok
	ret = mission_finish(mlist, dlist, 4);
	ERROR_RETURN(ret!=0, "test_finish:finish4 %d", ret);
	return 0;
}

int test4_guild_mission(design_mission_t *dlist, int argc, char * argv[])
{
	int lv = 1; // should be 1
	int ret;
	mission_t mlist[MAX_MISSION];
	// mission_t mis;

	// log x x \n lcard \n :
	const char * card_list = "1000000000000000000002200331030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	bzero(mlist, sizeof(mission_t) * MAX_MISSION);

	// create a clean mlist
	ret = mission_refresh(mlist, dlist, lv, 0, card_list) ;
	ERROR_NEG_RETURN(ret, "test_mission_update:refresh");
	printf("------ after refresh guild_lv=0:\n");
	print_mission_list(mlist, dlist);

	ret = mission_refresh(mlist, dlist, lv, 1, card_list) ;
	ERROR_NEG_RETURN(ret, "test_mission_update:refresh");
	printf("------ after refresh guild_lv=1:\n");
	print_mission_list(mlist, dlist);

	ret = mission_refresh(mlist, dlist, lv, 2, card_list) ;
	ERROR_NEG_RETURN(ret, "test_mission_update:refresh");
	printf("------ after refresh guild_lv=2:\n");
	print_mission_list(mlist, dlist);


	// test a MISSION_VS guild mission
	// 1. guild_lv == 0, mission 7 update
	// 2. guild_lv == 1, 1 mission 7 update
	// 3. guild_lv == 2, 2 mission 6,7 update

	// 1.
	ret = mission_update(mlist, dlist, MISSION_VS, 1, 0, 0, 0);
	ERROR_RETURN(ret-MISSION_UP_NUM, "test_update:vs %d", mlist[1].n1);
	printf("------ after update 1111:\n");
	print_mission_list(mlist, dlist);

	// 2.
	ret = mission_update(mlist, dlist, MISSION_VS, 1, 0, 0, 1);
	ERROR_RETURN(ret-MISSION_UP_NUM, "test_update:vs %d", mlist[1].n1);
	printf("------ after update 2222:\n");
	print_mission_list(mlist, dlist);

	// 3.
	ret = mission_update(mlist, dlist, MISSION_VS, 1, 0, 0, 2);
	ERROR_RETURN(ret-MISSION_UP_NUM, "test_update:vs %d", mlist[1].n1);
	printf("------ after update 3333:\n");
	print_mission_list(mlist, dlist);

	// 
	ret = mission_refresh(mlist, dlist, lv, 2, card_list) ;
	ERROR_NEG_RETURN(ret, "test_mission_update:refresh");
	printf("------ final refresh:\n");
	print_mission_list(mlist, dlist);

	// 4.
	ret = mission_update(mlist, dlist, MISSION_VS, 2, 0, 0, 2);
	ERROR_RETURN(ret-MISSION_UP_NUM, "test_update:vs %d", mlist[1].n1);
	printf("------ after update 4444:\n");
	print_mission_list(mlist, dlist);

	// 
	ret = mission_refresh(mlist, dlist, lv, 2, card_list) ;
	ERROR_NEG_RETURN(ret, "test_mission_update:refresh");
	printf("------ final refresh:\n");
	print_mission_list(mlist, dlist);

	return 0;
}

int test5_reset(design_mission_t *dlist, int argc, char *argv[])
{
	int lv = 0;
	int ret;
	mission_t mlist[MAX_MISSION];
	mission_t mis;

	// log x x \n lcard \n :
	const char * card_list = "1000000000000000000002200331030000000000000000000000000000002022112022000000000000000000000000000000000000000000000000000000000000111110000000000000002002200000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

	// mlist.clear();
	bzero(mlist, sizeof(mission_t) * MAX_MISSION);

	// mid, status, n1, last

	{
		// mid, n1, status, last
		mission_t tmp = {23, 3,   3, 0};
		mis = tmp;
	}
	mlist[mis.mid] = mis;


	time_t now = time(NULL);
	time_t today_reset = reset_time(now, dlist[23].reset_time);
	long yesterday_offset = -( 24 * 60 * 60);
	DEBUG_PRINT(0, "today_reset=%ld", today_reset);
	if ((now >= today_reset && mis.last_update <= today_reset)
	|| (now <= today_reset && mis.last_update <= today_reset + yesterday_offset)){
		DEBUG_PRINT(0, "reset mission");
	} else {
		DEBUG_PRINT(0, "not reset mission");
	}

//	printf("------ before refresh:\n");
//	print_mission_list(mlist, dlist);

	ret = mission_refresh(mlist, dlist, lv, 0, card_list);
	WARN_PRINT(ret, "mission_refresh_fail");

//	printf("------ after refresh:\n");
//	print_mission_list(mlist, dlist);
	// check mlist.size == 2
	int msize = mlist_size(mlist);
	ERROR_RETURN(msize != 4, "refresh:size %d", msize);

	for (int i=0; i<MAX_MISSION; i++) {
		mission_t & mis = mlist[i];
		if (mis.mid <= 0) {
			continue;
		}
		print_mission(mis, dlist);
		// ERROR_RETURN(mis.mid >= 80, "refresh:mid>=80 exists %d", mis.mid);
	}

	return 0;
}

typedef int (*testcase_t) (design_mission_t *, int, char*[]); 

testcase_t test_list[] = {
	test0_empty
,	test1_refresh
,	test2_update
,	test3_finish
,	test4_guild_mission
,	test5_reset
};


int test_selector(design_mission_t *dlist, int argc, char *argv[])
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
	ret = test_list[testcase](dlist, argc, argv);
	
	printf("RET %d\n", ret);
	if (ret != 0 ) {
		printf("XXXXXXXXX BUG ret!=0: %d\n", ret);
	}
	
	return ret;
}


int main(int argc, char* argv[]) {
	
	// MISSION_MAP mlist;
	design_mission_t dmis;

	// D_MISSION_MAP dlist;
	//map<int,design_mission_t> dlist;
//	dlist.clear();
	design_mission_t dlist[MAX_MISSION];
	bzero(dlist, sizeof(dlist));

	// (mid, pre, lv, hero, daily)
	{
 		design_mission_t tmp = {1, 0, 0, 0, 0, 0,   MISSION_LEVEL, 2,0,0
		,  5,10,0, 0, 1,{22},0,{},  "00:00", "lv up to 2 get card 22, exp 5"};	
		// reward card,exp,gold, crystal
		dmis = tmp;
	}
	dlist[dmis.mid] = dmis;

	{
 		design_mission_t tmp = {2, 1, 0, 0, 0, 0,   MISSION_AI, 2,8,0
		,  6,7,1, 0, 1,{23},0,{}, "00:00", "win_zhanna_8 x 2 times get card 23, exp 6, gold 7, crystal 1"};
		dmis = tmp;
	}
	dlist[dmis.mid] = dmis;

	{
 		design_mission_t tmp = {3, 0, 3, 0, 0, 0,   MISSION_AI, 3,0,0
		,  0,8,2, 0, 1,{24},0,{},  "00:00", "pre_lv=3  win any AI x 3 times get card 24, gold 8, crystal 2"};
		dmis = tmp;
	}
	dlist[dmis.mid] = dmis;

	{
 		design_mission_t tmp = {4, 3, 0, 0, 1, 0,   MISSION_VS, 2,0,0
		,  0,0,0, 0, 1,{24},0,{}, "15:30",  "pre=3  win VS x 2 times get card 24"};
		dmis = tmp;
	}
	dlist[dmis.mid] = dmis;

	// guild mission
	{
 		design_mission_t tmp = {6, 0, 0, 0, 0, 2,   MISSION_VS, 3,0,0
		,  0,0,0, 0, 1,{25},0,{}, "15:30",  "guild win VS x 3 times get card 25"};
		dmis = tmp;
	}
	dlist[dmis.mid] = dmis;

	// not guild mission
	{
 		design_mission_t tmp = {7, 0, 0, 0, 0, 0,   MISSION_VS, 3,0,0
		,  0,0,0, 0, 1,{25},0,{}, "15:30",  "win VS x 3 times get card 25"};
		dmis = tmp;
	}
	dlist[dmis.mid] = dmis;

	{
 		design_mission_t tmp = {23, 0, 1, 0, 1, 0,   MISSION_AI, 3,0,0
		,  0,0,10, 0, 1,{24},0,{}, "10:00",  "win ai 3 times"};
		dmis = tmp;
	}
	dlist[dmis.mid] = dmis;

	{
 		design_mission_t tmp = {110, 0, 0, 0, 1, 0,   MISSION_MONTHLY, 1,0,0
		,  0,0,50, 0, 1,{24},0,{}, "00:00",  "MONTH VIP daily crystal"};
		dmis = tmp;
	}
	dlist[dmis.mid] = dmis;

	/**
	D_MISSION_MAP::iterator it;
	int mid = 66;
	it = dlist.find(mid);
	if (it != dlist.end()) {
		design_mission_t &dmis = (*it).second;
		printf("------mid=%d find\n", mid);
		print_design_mission(dmis);
	} else {
		printf("mid=%d not find\n", mid);
	}
	printf("-------\n");
	//     **/

	// this will create 2 empty records in dlist, with all 0
	/**
	design_mission_t ddd = dlist[66];
	design_mission_t ddd2 = dlist[68];
	if (ddd.mid == 0 || ddd2.mid==0) {
		printf("66 68 not exists\n");
	}
	// 		**/


	print_design_mission_list(dlist);
	// test1_refresh(dlist);

	if (argc > 1 && strcmp("all", argv[1])==0) {
		int testmax = sizeof(test_list) / sizeof(test_list[0]);
		int error_count = 0;
		for (int i=0; i<testmax; i++) {
			int ret;
			char str[10];
			char *alist[2] = {argv[0], str};
			sprintf(str, "%d", i);
			ret = test_selector(dlist, argc, alist); // changed argv
			if (ret != 0) { error_count++; break;}
		}
		printf("TEST ALL SUMMARY: error_count=%d\n", error_count);
	} else {
		test_selector(dlist, argc, argv);
	}

	return 0;
}

#endif

