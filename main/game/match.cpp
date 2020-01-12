

#include "evil.h"
#include "fatal.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
using namespace std;

#define ONE_DAY_TIME	(24 * 60 * 60)

/**
how a match run:
1.create match
2.player apply/cancel match
3.match init
4.player get next player, start a round game
5.match result

team match:
A   B   C   D 
|___|   |___|
  |       |
  W01     W11   L01     L11   
  |_______|      |_______|
      |              |
	 W12            W02     L12
	  |			     |_______|
	  |				     |
	 W13                W03

elimination match:
A(4)  B(5)  C(6)  D(7) ==> 2*n-1
|_____|     |_____|
   |           |
   W1(2)      W2(3)
   |___________|
        |    
        W3(1)  

match_player_t team_player_list[MAX_RECORD] :
{
round1	team1	player1
				player2
				player3
				player4
		team2	player5
				player6
				player7
				player8
round2	team1	player1
				player2
				player3
				player4
		team2	player5
				player6
				player7
				player8
round3	team1	player1
				player2
				player3
				player4
		team2	player5
				player6
				player7
				player8
}

match_player_t elimination_player_list
{
player1		->	tid=2 * n
player2		->	tid=2 * n - 1
player3		->	tid=2 * n - 2
...
player2*n-1	->	tid=2
player2*n	->	tid=1
}

*/

//#define MAX_TEAM			2
//#define MAX_PLAYER			MAX_TEAM_PLAYER * MAX_TEAM
//#define MAX_RECORD		MAX_TEAM_ROUND * MAX_PLAYER

#define TAB_STOP	16

int print_player(match_player_t &player)
{	
	printf("print_player:match_id=%ld eid=%d round=%d team_id=%d win=%d lose=%d draw=%d tid=%d point=%d icon=%d alias=%s\n", player.match_id
	, player.eid, player.round, player.team_id, player.win, player.lose
	, player.draw, player.tid, player.point, player.icon, player.alias);
	return 0;
}

int print_all_record(match_t & match, match_player_t *player_list)
{
	printf("------print_all_record--------\n");
	printf("[match,eid,round,team]	[w/l/d]		[tid,point]\n");
	const int max_record = match.max_player * MAX_TEAM_ROUND;
	for (int i=0; i < max_record; i++) {
		match_player_t *player = player_list + i;
		printf("%ld %d %d %d 		%d %d %d 			%d %d %d %s\n"
		, player->match_id
		, player->eid, player->round, player->team_id
		, player->win, player->lose, player->draw
		, player->tid, player->point, player->icon, player->alias);
	}
	printf("------print_all_record--------\n");
	return 0;
}

int print_all_eli_record(match_t & match, match_player_t *player_list)
{
	printf("------print_all_eli_record--------\n");
	printf("[match,eid,round,team]	[w/l/d]		[tid,point]\n");
	const int max_record = match.max_player;
	for (int i=0; i < max_record; i++) {
		match_player_t *player = player_list + i;
		printf("%ld %d %d %d 		%d %d %d 			%d %d %d %s\n"
		, player->match_id
		, player->eid, player->round, player->team_id
		, player->win, player->lose, player->draw
		, player->tid, player->point, player->icon, player->alias);
	}
	printf("------print_all_eli_record--------\n");
	return 0;
}


match_player_t & get_player(match_t & match, match_player_t *player_list
, int eid, int round, int team_id)
{
	static match_player_t empty_player = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "_"};
	/*
	for (int i = 0; i < MAX_PLAYER * MAX_TEAM_ROUND; i++) {
		match_player_t & player = player_list[i];
		if (player.eid == eid && player.round == round 
		&& player.team_id == team_id) {
			return player;
		}
	}
	*/

	int start_pos = (round - 1) * match.max_player + (team_id - 1) * MAX_TEAM_PLAYER;
	for (int i = 0; i < MAX_TEAM_PLAYER; i++) {
		match_player_t *player = player_list + start_pos + i;
		if (player->eid == eid && player->round == round
		&& player->team_id == team_id) {
			return *player;
		}
	}

	return empty_player;
}

match_player_t & get_player_last_round(match_t & match, int eid)
{
	
	static match_player_t empty_player = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "_"};
	int max_round = 0;
	match_player_t *player = NULL;
	if (match.round < MAX_TEAM_ROUND 
	|| (match.round == MAX_TEAM_ROUND && match.status == MATCH_STATUS_ROUND_START)) {
		for (int i = 0; i < match.max_player * MAX_TEAM_ROUND; i++) {	
			match_player_t *tmp = match.player_list + i;
			if (tmp->eid == eid) {
				if (tmp->round >= max_round) {
					max_round = tmp->round;
					player = tmp;
				}
			}
		}
	} else {
		for (int i = 0; i < match.max_player; i++) {	
			match_player_t *tmp = match.e_player_list + i;
			if (tmp->eid == eid) {
				if (tmp->round >= max_round) {
					max_round = tmp->round;
					player = tmp;
				}
			}
		}
	}
	if (player == NULL) {
		ERROR_PRINT(-3, "get_player_last_round:null_player %d", eid);
		return empty_player;
	}

	// printf("get_player_last_round:addr = %p\n", player);
	// XXX retrun *point =? reference
	return *player;
	// return get_player(player_list, player->eid, player->round, player->team_id);
}


time_t create_match_id()
{
	static time_t last_id = 0;
	time_t match_id = time(NULL);
	if (match_id > last_id) {
		last_id = match_id;
		return match_id;
	}
	last_id++;
	match_id = last_id;
	return match_id;
}

int match_create(match_t &match, int max_player, time_t start_time, time_t round_time_list[MAX_DAILY_ROUND], const char * title)
{
	match.match_id = create_match_id();
	// 4 players a team
	if (max_player % 4 != 0) {
		ERROR_RETURN(-5, "match_create:error_max_player");
	}
	match.max_player = max_player;
	match.max_team = max_player / MAX_TEAM_PLAYER;
	match.start_time = start_time;
	for (int i = 0; i<MAX_DAILY_ROUND; i++) {
		if (round_time_list[i] < 0) {
			ERROR_RETURN(-5, "match_create:error_round_time %d", i);
		}
		match.round_time_list[i] = round_time_list[i];
	}
	sprintf(match.title, "%.100s", title);
	match.status = MATCH_STATUS_READY;
	return 0;
}

int match_apply(match_t &match, int eid, int icon, const char * alias)
{
	if (match.status != MATCH_STATUS_READY) {
		ERROR_RETURN(-6, "match_apply:match_not_ready");
	}

	for (int i = 0; i < match.max_player; i++) {
		match_player_t *player = match.player_list + i;
		// this player has already in match
		if (player->eid == eid) {
			ERROR_RETURN(-9, "match_apply:player_already_apply");
		}
		if (player->eid == 0) {
			player->eid = eid;
			player->match_id = match.match_id;
			player->icon = icon;
			sprintf(player->alias, "%s", alias);
			return 0;
		}
	}
	ERROR_RETURN(-16, "match_apply:match_player_full");
}

// player cancel apply
int match_cancel(match_t &match, int eid)
{
	if (match.status != MATCH_STATUS_READY) {
		ERROR_RETURN(-6, "match_cancel:match_not_ready");
	}
	for (int i = 0; i < match.max_player; i++) {
		match_player_t &player = match.player_list[i];
		if (player.eid == eid) {
			bzero(&player, sizeof(match_player_t));
			return 0;
		}
	}

	ERROR_RETURN(-5, "match_cancel:no_such_eid:%d", eid);
}



int match_team_init(match_t &match)
{

	// TODO random player_list here
	match_player_t *player_list = match.player_list;

	const int max_record = MAX_MATCH_PLAYER * MAX_TEAM_ROUND; //match.max_player * MAX_TEAM_ROUND;
	for (int i = 0; i < max_record; i++) {
		match_player_t *player = player_list + i;
		player->tid = -1;
		player->match_id = match.match_id;
	}


	// random sort the player's location
	match_player_t tmp_player;
	for (int i = 0; i < match.max_player; i++) {
		tmp_player = player_list[i];
		int pos = i + (random() % (match.max_player - i));
		player_list[i] = player_list[pos];
		player_list[pos] = tmp_player;
	}



	// for round 1
	for (int i = 0; i < match.max_player; i++) {
		match_player_t *player = player_list + i;
		if (player->eid == 0) {
			BUG_RETURN(-3, "match_team_init:eid==0");
			continue;
		}
		player->round = 1;
		player->team_id = (i / 4) + 1;
		player->tid = i % 2;
		// TODO  init other player.tid = -1
	}

	// clean 2, 3 round player data
	/*
	for (int round = 1; round < MAX_TEAM_ROUND; round++) {
		for (int i = 0; i < match.max_player; i++) {
			match_player_t *player = player_list + round * match.max_player + i;
			player->tid = 0;
			player->round = 0;
			player->eid = 0;
			sprintf(player->alias, "_");
		}
	}
	*/

	// below eid is fake eid: 
	// eid = before round -(win(2)/lose(1) * 100 + tie(1/0) * 10 + round)
	// e.g.: -101 means round 1, tid 0, loser
	// e.g.: -212 means round 2, tid 1, winner
	// round 2
	for (int i = 0; i < match.max_team; i++) {
		// winner 2 player in round 1
		{
			match_player_t *player1 = player_list + match.max_player + i * MAX_TEAM_PLAYER;
			match_player_t *player2 = player_list + match.max_player + i * MAX_TEAM_PLAYER + 1;
			player1->round = player2->round = 2;
			player1->tid = player2->tid = 1;
			player1->eid = -201;
			player2->eid = -211;		// win_tid_round
			player1->team_id = i + 1;
			player2->team_id = i + 1;
			sprintf(player1->alias, "_");
			sprintf(player2->alias, "_");
		}

		// loser 2 player in round 1
		{
			match_player_t *player1 = player_list + match.max_player + i * MAX_TEAM_PLAYER + 2;
			match_player_t *player2 = player_list + match.max_player + i * MAX_TEAM_PLAYER + 3;
			player1->round = player2->round = 2;
			player1->tid = player2->tid = 0;
			player1->eid = -101;
			player2->eid = -111;		// win_tid_round
			player1->team_id = i + 1;
			player2->team_id = i + 1;
			sprintf(player1->alias, "_");
			sprintf(player2->alias, "_");
		}

	}

	// round 3, no need add init round2 & tid=0 loser
	for (int i = 0; i < match.max_team; i++) {
		// loser in round 2 winner-match and winner in round 2 loser-match
		{
			match_player_t *player1 = player_list + 2 * match.max_player + i * MAX_TEAM_PLAYER;
			match_player_t *player2 = player_list + 2 * match.max_player + i * MAX_TEAM_PLAYER + 1;
			player1->round = player2->round = 3;
			player1->tid = player2->tid = 0;
			player1->eid = -202;		// win_tid_round
			player2->eid = -112;
			player1->team_id = i + 1;
			player2->team_id = i + 1;
			sprintf(player2->alias, "_");
		}

		{
			match_player_t *player1 = player_list + 2 * match.max_player + i * MAX_TEAM_PLAYER + 2;
			player1->round = 3;
			player1->tid = 1;
			player1->eid = -212; // win_tid_round
			player1->team_id = i + 1;
			match_player_t *player2 = player_list + 2 * match.max_player + i * MAX_TEAM_PLAYER + 3;
			player2->round = 3;
			player2->team_id = i + 1;
			sprintf(player1->alias, "_");
			sprintf(player2->alias, "_");
		}
	}

	// for nio.admin_round_start()
	match.round = 0;
	match.status = MATCH_STATUS_ROUND_END;

	return 0;
}


int match_eli_init(match_t & match) {

	if (match.status != MATCH_STATUS_ROUND_END) {
		ERROR_RETURN(-6, "match_eli_init:round_not_end");
	}

	for (int i = 0; i < match.max_player; i++) {
		match_player_t & e_player = match.e_player_list[i];

		e_player.eid = -1;
//		e_player.tid = match.max_player - i - 1;
		e_player.match_id = match.match_id;
		sprintf(e_player.alias, "_");
	}

	int e_count = 0;
	for (int i = 0; i < match.max_player; i++) {
		int pos = match.max_player * 2 + i;
		match_player_t & player = match.player_list[pos];
		if (player.point < 5) {
			continue;
		}

		match_player_t & e_player = match.e_player_list[e_count++];
		e_player.eid = player.eid;
		e_player.icon = player.icon;
		sprintf(e_player.alias, "%s", player.alias);
	}

	match_player_t tmp_player;
	for (int i = 0; i < match.max_player / 2; i++) {
		tmp_player = match.e_player_list[i];
		int pos = i + (random() % (match.max_player / 2 - i));
		match.e_player_list[i] = match.e_player_list[pos];
		match.e_player_list[pos] = tmp_player;
	}



	int total = 0;
	for (int cur_round = 4, len = match.max_player / 2, cur_tid = match.max_player-1
	; len > 0; cur_round++) {
		for (int j = 0; j < len; j++) {
			match_player_t & e_player = match.e_player_list[total + j];
			e_player.round = cur_round;
			e_player.tid = cur_tid;
			cur_tid--;
		}
		total += len;
		len /= 2;
	}

	print_all_eli_record(match, match.e_player_list);

	return 0;
}



// write match_data to char buffer
// TODO add filter on round/team
int match_team_data(match_t& match, int team_id, char *out_buffer)
{

	char *ptr;
	ptr = out_buffer;

	ptr += sprintf(ptr, "%ld %d ", match.match_id, match.status);
	if (match.status == MATCH_STATUS_READY) {
		return 0;
	}

	ptr += sprintf(ptr, "%d %d ", MATCH_DATA_TEAM_FLAG, team_id);
	// game_t = {round, player1, player2}
	// team max_game game1 game2
	const int MAX_GAME = 5;
	for (int i = 0; i < match.max_team; i++) {
		if (i != team_id - 1) {	// team_id in match.player_list record base 1
			continue;
		}
		ptr += sprintf(ptr, "%d ", MAX_GAME);
		for (int round = 0; round < MAX_TEAM_ROUND; round++) {
			for (int j = 0; j < MAX_TEAM_PLAYER; j++) {
				match_player_t *player1 = match.player_list
				+ MAX_TEAM_PLAYER * i 
				+ match.max_player * round + j;
				for (int k = j+1; k < MAX_TEAM_PLAYER; k++) {
					match_player_t *player2 = match.player_list
					+ MAX_TEAM_PLAYER * i + match.max_player * round + k;
					if (player1->round != player2->round
						|| player1->tid != player2->tid) {
						continue;
					}

					ptr += sprintf(ptr, "%d ", round+1);
					ptr += sprintf(ptr, "%d %d %d %d %d %d %s "
					, player1->eid, player1->win, player1->lose
					, player1->draw, player1->tid, player1->point
					, player1->alias);
					ptr += sprintf(ptr, "%d %d %d %d %d %d %s "
					, player2->eid, player2->win, player2->lose
					, player2->draw, player2->tid, player2->point
					, player2->alias);
				}
			}
		}
	}


	return 0;
}


int match_eli_data(match_t& match, int tid, char *out_buffer)
{
	char *ptr;
	ptr = out_buffer;

	ptr += sprintf(ptr, "%ld %d ", match.match_id, match.status);
	if (match.status == MATCH_STATUS_READY) {
		return 0;
	}
	ptr += sprintf(ptr, "%d ", MATCH_DATA_ELI_FLAG);

	if (tid >= match.max_player / 2) {
		tid /= 2;
	}

	int eli_tid_list[7] = {0};

	int parent_tid = tid / 2;
	if (parent_tid == 0) {
		parent_tid = 1;
	}
	eli_tid_list[0] = parent_tid;

	int count = 1;
	for (int i = 1; i < 6; i+=2) {
		eli_tid_list[i] = eli_tid_list[i/2] * 2;
		if (eli_tid_list[i] <= 0 || eli_tid_list[i] >= match.max_player) {
			eli_tid_list[i] = 0;
			break;
		}

		eli_tid_list[i + 1] = eli_tid_list[i/2] * 2 + 1;
		if (eli_tid_list[i+1] <= 0 || eli_tid_list[i+1] >= match.max_player) {
			eli_tid_list[i+1] = 0;
			break;
		}

		count += 2;
	}

	ptr += sprintf(ptr, "%d ", count);

	for (int i = 0; i < count; i++) {
		int eli_tid = eli_tid_list[i];
		if (eli_tid == 0) {
			continue;
		}

		match_player_t &player = match.e_player_list[match.max_player - eli_tid - 1];
		if (player.tid != eli_tid) {
			BUG_PRINT(-6, "match_eli_data:player_tid_mismatch %d %d"
			, player.tid, eli_tid);
			continue;
		}

		ptr += sprintf(ptr, "%d %d %d %d %d %d %s "
		, player.eid, player.win, player.lose
		, player.draw, player.tid, player.point
		, player.alias);

	}

	return 0;
}




// print match info
int match_info(match_t& match, match_player_t *player_list)
{
	printf("---------match_info-------\n");
	switch (match.status) {
	case MATCH_STATUS_READY:
		printf("\tmatch.status=MATCH_STATUS_READY\n");
		break;
	case MATCH_STATUS_ROUND_START:
		printf("\tmatch.status=MATCH_STATUS_ROUND_START\n");
		break;
	case MATCH_STATUS_ROUND_END:
		printf("\tmatch.status=MATCH_STATUS_ROUND_END\n");
		break;
	case MATCH_STATUS_FINISHED:
		printf("\tmatch.status=MATCH_STATUS_FINISHED\n");
		break;
	}
	printf("match.round=%d\n", match.round);

	char p1[10], p2[10];
	for (int i = 0; i < match.max_team; i++) {
		printf("\n\nteam: %d", i+1);
		for (int round = 0; round < MAX_TEAM_ROUND; round++) {
			if (round > 0 && match.status == MATCH_STATUS_READY) {
				break;
			}
			printf("\nround: %d\n", round+1);
			for (int j = 0; j < MAX_TEAM_PLAYER; j++) {
				match_player_t *player1 = player_list
				+ MAX_TEAM_PLAYER * i 
				+ match.max_player * round + j;
				if (player1->eid == 0) {
					continue;
				}
				for (int k = j+1; k < MAX_TEAM_PLAYER; k++) {
					match_player_t *player2 = player_list
					+ MAX_TEAM_PLAYER * i + match.max_player * round + k;
					if (player2->eid == 0) {
						continue;
					}
					if (player1->round != player2->round
						|| player1->tid != player2->tid) {
						continue;
					}
					char star = player1->point >= 5 ? '*' : ' ';
					sprintf(p1, "%4d%c", player1->eid, star);
					star = player2->point >= 5 ? '*' : ' ';
					sprintf(p2, "%c%-4d", star, player2->eid);
					printf(" [%1d%1d: %4s (%1d)vs(%1d) %-4s] "
					, player1->tid, player1->round
					, p1, player1->point, player2->point, p2);

				}
			}
			if (round+1 == MAX_TEAM_ROUND) {
				// print 2 final winner in team
				printf("\n final winner:");
				for (int j = 0; j < MAX_TEAM_PLAYER; j++) {
					match_player_t *player = player_list
					+ MAX_TEAM_PLAYER * i 
					+ match.max_player * round + j;
					if (player->point >= 5) {
						printf("	%d", player->eid);
					}
				}
				printf("\n");
			}
		}
	}

	printf("\n");
	return 0;
}



int match_eli_info(match_t& match, match_player_t *player_list)
{
	printf("---------match_eli_info-------\n");
	switch (match.status) {
	case MATCH_STATUS_READY:
		printf("\tmatch.status=MATCH_STATUS_READY\n");
		break;
	case MATCH_STATUS_ROUND_START:
		printf("\tmatch.status=MATCH_STATUS_ROUND_START\n");
		break;
	case MATCH_STATUS_ROUND_END:
		printf("\tmatch.status=MATCH_STATUS_ROUND_END\n");
		break;
	case MATCH_STATUS_FINISHED:
		printf("\tmatch.status=MATCH_STATUS_FINISHED\n");
		break;
	}
	printf("match.round=%d\n", match.round);
	char p1[10], p2[10];
	int round = 0;

	int max_record = match.max_player;
	for (int i = 0; i < max_record; i += 2) {
		match_player_t *player1 = player_list + i;
		match_player_t *player2 = player_list + i + 1;

		if (player2->tid == 0) {
			printf("\n--------------------\n");
			printf("  final winner: %d\n", player1->eid);
			printf("--------------------\n");
			break;
		}

		if (player1->round > round) {
			round = player1->round;
			printf("\nround: %d\n", round);
		}

		char star = player1->point >= 5 ? '*' : ' ';
		sprintf(p1, "%4d%c", player1->eid, star);
		star = player2->point >= 5 ? '*' : ' ';
		sprintf(p2, "%c%-4d", star, player2->eid);
		printf(" [%1d%1d: %4s (%1d)vs(%1d) %-4s] "
		, player1->tid, player1->round
		, p1, player1->point, player2->point, p2);

	}

	printf("\n");
	return 0;
}




int match_team_result(match_t &match, match_player_t *player_list, int eid1, int eid2, int winner, int upper)
{
	int ret;
	int round;
	int team_id;
	int tid;
	int max_round1 = 0;
	int max_round2 = 0;
	match_player_t *player1 = NULL;
	match_player_t *player2 = NULL;
	match_player_t *round_winner = NULL;
	match_player_t *round_loser = NULL;
	const int max_record = match.max_player * MAX_TEAM_ROUND;
	for (int i = 0; i < max_record; i++) {	
		match_player_t *player = player_list + i;
		if (player->eid == eid1) {
			if (player->round > max_round1) {
				max_round1 = player->round;
				player1 = player;
			}
		}
		if (player->eid == eid2) {
			if (player->round > max_round2) {
				max_round2 = player->round;
				player2 = player;
			}
		}
	}

	if (player1 == NULL) {
		ERROR_RETURN(-3, "match_result:no_such_player1 %d", eid1);
	}

	if (player2 == NULL) {
		ERROR_RETURN(-13, "match_result:no_such_player2 %d", eid2);
	}

	// print_player(*player1);
	// print_player(*player2);

	if (player1->team_id != player2->team_id) {
		ERROR_RETURN(-6, "match_result:not_same_team %d %d", eid1, eid2);
	}

	if (player1->round != player2->round) {
		ERROR_RETURN(-16, "match_result:not_same_round %d %d", eid1, eid2);
	}

	if (player1->tid != player2->tid) {
		ERROR_RETURN(-26, "match_result:not_same_tid %d %d", eid1, eid2);
	}

	// in here, both player tid, round, team_id are the same
	tid = player1->tid;
	round = player1->round;
	team_id = player1->team_id;

	// core logic
	// 1.update this round info
	if (winner == 1) {
		player1->win++;
		player1->point += 3;
		player2->lose++;
	}

	if (winner == 2) {
		player1->lose++;
		player2->win++;
		player2->point += 3;
	}

	if (winner == 9) {
		if (upper == 1) {
			player1->draw++;
			player1->point += 1;
			player2->draw++;
			player2->point += 2;
		} 
		if (upper == 2) {
			player1->draw++;
			player1->point += 2;
			player2->draw++;
			player2->point += 1;
		} 
	}


	if (player1->point >= 5 && player2->point >= 5) {
		BUG_RETURN(-7, "match_result:point_error %d %d %d"
		, player1->eid, player2->eid, round);
	}

	if (player1->point >= 5) {
		round_winner = player1;
		round_loser = player2;
	}
	if (player2->point >= 5) {
		round_winner = player2;
		round_loser = player1;
	}
	
	// no winner in this round now, early exit 
	if (round_winner == NULL || round_loser == NULL) {
		return 0;
	}

	// 2.point >= 5, round finish, insert next round(2,3) info
	switch (round) {
	case 1: 
	{
		// this is winner
		int fake_eid1 = -(2 * 100 + tid * 10 + round);
		match_player_t & player_new1 = get_player(match, player_list
		, fake_eid1, round+1, team_id);
		if (player_new1.eid == 0) {
			ERROR_RETURN(-36, "match_result:no_such_player1 %d"
			, fake_eid1);
		}
		player_new1.eid = round_winner->eid;
		player_new1.icon = round_winner->icon;
		sprintf(player_new1.alias, "%s", round_winner->alias);

		// this is loser
		int fake_eid2 = -(1 * 100 + tid * 10 + round);
		match_player_t & player_new2 = get_player(match, player_list
		, fake_eid2, round+1, team_id);
		if (player_new2.eid == 0) {
			ERROR_RETURN(-46, "match_result:no_such_player2 %d"
			, fake_eid2);
		}
		player_new2.eid = round_loser->eid;
		player_new2.icon = round_loser->icon;
		sprintf(player_new2.alias, "%s", round_loser->alias);

		break;
	}
	case 2: 
	{
		// this is winner
		int fake_eid1 = -(2 * 100 + tid * 10 + round);
		match_player_t & player_new1 = get_player(match, player_list
		, fake_eid1, round+1, team_id);
		if (player_new1.eid == 0) {
			ERROR_RETURN(-56, "match_result:no_such_player1 %d"
			, fake_eid1);
		}
		player_new1.eid = round_winner->eid;
		player_new1.icon = round_winner->icon;
		sprintf(player_new1.alias, "%s", round_winner->alias);

		// winner in round2 and tid=1, auto win in round3
		if (tid == 1) {
			player_new1.win = 2;
			player_new1.point = 6;
		}

		// only loser in round2 and tid = 1 need to insert into player_list; loser in round2 and tid = 0, no need to insert into player_list
		if (tid == 0) {
			break;
		}
		// this is loser
		int fake_eid2 = -(1 * 100 + tid * 10 + round);
		match_player_t & player_new2 = get_player(match, player_list
		, fake_eid2, round+1, team_id);
		if (player_new2.eid == 0) {
			ERROR_RETURN(-66, "match_result:no_such_player2 %d"
			, fake_eid2);
		}
		player_new2.eid = round_loser->eid;
		player_new2.icon = round_loser->icon;
		sprintf(player_new2.alias, "%s", round_loser->alias);
		break;
	}
	}

	ret = 0;
	return ret;
}




int match_eli_result(match_t &match, match_player_t *player_list, int eid1, int eid2, int winner, int upper)
{
	int ret;
	int max_record;
	match_player_t *player1, *player2;
	int next_tid;
	int round;
	match_player_t *round_winner;

	if (winner != 1 && winner != 2 && winner != 9) {
		ERROR_RETURN(-5, "match_eli_result:winner_undefined %d", winner);
	}
	
	if (upper != 1 && upper != 2) {
		ERROR_RETURN(-15, "match_eli_result:upper_undefined %d", upper);
	}

	max_record = match.max_player;
	for (int pos = 0, max_round1 = 0, max_round2 = 0; pos < max_record; pos++) {
		match_player_t *tmp_player = player_list + pos;

		if (tmp_player->eid == eid1 && tmp_player->round > max_round1) {
			player1 = tmp_player;
			max_round1 = tmp_player->round;
		}
		if (tmp_player->eid == eid2 && tmp_player->round > max_round2) {
			player2 = tmp_player;
			max_round2 = tmp_player->round;
		}
	}

	if (player1 == NULL || player2 == NULL) {
		ERROR_RETURN(-3, "match_eli_result:player_null: %d %d", eid1, eid2);
	}

	if (player1->round != player2->round) {
		ERROR_RETURN(-6, "match_eli_result:player_not_same_round %d %d %d %d"
		, eid1, eid2, player1->round, player2->round);
	}

	if (player1->point >= 5 || player2->point >= 5) {
		BUG_RETURN(-16, "match_eli_result:player_has_win_game %d %d %d %d"
		, eid1, eid2, player1->point, player2->point);
	}

	round = player1->round;
	int max_tid = max(player1->tid, player2->tid);
	int min_tid = min(player1->tid, player2->tid);
	if (max_tid - min_tid != 1 || ((min_tid % 2) != 0)) {
		ERROR_RETURN(-26, "match_eli_result:player_not_in_same_game %d %d %d %d"
		, eid1, eid2, player1->tid, player2->tid);
	}

	// core logic
	// 1.update this round info
	switch (winner) {
		case 1:
		{
			player1->win++;
			player1->point += 3;
			player2->lose++;
			break;
		}
		case 2:
		{
			player1->lose++;
			player2->win++;
			player2->point += 3;
			break;
		}
		case 9:
		{
			if (upper == 1) {
				player1->draw++;
				player1->point += 1;
				player2->draw++;
				player2->point += 2;
			} 
			if (upper == 2) {
				player1->draw++;
				player1->point += 2;
				player2->draw++;
				player2->point += 1;
			} 
			break;
		}
		default:
		{
			ERROR_RETURN(-7, "match_eli_result:winner_undefined %d", winner);
		}
	}


	if (player1->point >= 5 && player2->point >= 5) {
		BUG_RETURN(-17, "match_eli_result:point_error %d %d %d"
		, player1->eid, player2->eid, round);
	}

	if (player1->point < 5 && player2->point < 5) {
		return 0;
	}

	round_winner = (player1->point >= 5) ? player1 : player2;
	
	next_tid = min_tid / 2;
	int num = 0;
	for (int i = 0; i < max_record; i++) {
		match_player_t * tmp_player = player_list + i;
		if (tmp_player->tid == next_tid) {
			tmp_player->eid = round_winner->eid;
			tmp_player->icon = round_winner->icon;
			sprintf(tmp_player->alias, "%s", round_winner->alias);
			num = 1;
			if (tmp_player->tid == 1) {
				tmp_player->point = 6;
			}
			break;
		}
	}

	if (num != 1) {
		BUG_RETURN(-27, "match_eli_result:no_next_player %d %d"
		, round_winner->eid, next_tid);
	}

	ret = 0;
	return ret;
}


// handle match game result
// winner = 1(win), 2(lose), 9(draw)
// e.g.: match_reslut(match, player_list, 547, 548, 1, 2);
// e.g.: match_reslut(match, player_list, 547, 548, 9, 1);
int match_result(match_t &match, int eid1, int eid2, int winner, int upper)
{
	if (match.round <= MAX_TEAM_ROUND) {
		return match_team_result(match, match.player_list, eid1, eid2, winner, upper);
	}
	return match_eli_result(match, match.e_player_list, eid1, eid2, winner, upper);
}




// get next round opponent_player
// 1.loser in loser match in round2, match_next() will return round2 opponent_player, and oppo_player.point >= 5
// 2. winner in winner match in round2, match_next() will return a empty_player
// 3. round3 player use match_next() will return round3 oppo_player, no matter round3 is over
// 4. if return oppo_player.eid < 0, means oppo_player is not sure, player should wait beside match over in same round
// 5. if oppo_player.point >= 5 or my_player.point >= 5, means this round is over and no more round for now
int match_team_next(match_t &match, match_player_t *player_list, int eid, match_player_t & oppo_player)
{
	int max_round = 0;
	match_player_t *player = NULL;
	match_player_t *opponent = NULL;
	for (int i = 0; i < match.max_player * MAX_TEAM_ROUND; i++) {	
		match_player_t *tmp = player_list + i;
		if (tmp->eid == eid) {
			if (tmp->round > max_round) {
				max_round = tmp->round;
				player = tmp;
			}
		}
	}

	if (player == NULL) {
		ERROR_RETURN(-3, "match_next:no_such_player %d", eid);
	}

	int start_pos = (player->round - 1) * match.max_player + (player->team_id - 1) * MAX_TEAM_PLAYER;
	for (int i = 0; i < MAX_TEAM_PLAYER; i++) {
		match_player_t *tmp = player_list + start_pos + i;
		if (tmp->eid != eid && tmp->round == player->round
		&& tmp->tid == player->tid) {
			opponent = tmp;
			break;
		}
	}


	if (opponent == NULL && player->round < 3) {
		ERROR_RETURN(-3, "match_next:no_such_opponent %d", eid);
	}
	
	if (opponent != NULL) {
		oppo_player = *opponent;
	}
	
	
	
	return 0;
}


int match_eli_next(match_t &match, match_player_t *player_list, int eid, match_player_t & oppo_player)
{
	int max_round = 0;
	int max_record = match.max_player;
	match_player_t *player = NULL;
	match_player_t *opponent = NULL;
	for (int i = 0; i < max_record; i++) {	
		match_player_t *tmp = player_list + i;
		if (tmp->eid == eid && tmp->round > max_round) {
			max_round = tmp->round;
			player = tmp;
		}
	}

	if (player == NULL) {
		ERROR_RETURN(-3, "match_eli_next:no_such_player %d", eid);
	}

	int oppo_tid = player->tid + (player->tid % 2 == 0 ? 1 : -1);
	opponent = player_list + (max_record - oppo_tid - 1);
	
	if (opponent == NULL) {
		ERROR_RETURN(-13, "match_eli_next:no_such_opponent %d", eid);
	}

	if (opponent->tid != 0 && opponent->round != player->round) {
		ERROR_RETURN(-6, "match_eli_next:player_round_error %d %d %d %d", eid, opponent->eid, player->round, opponent->round);
	}

	oppo_player = *opponent;
	
	return 0;
}


int match_next(match_t &match, int eid, match_player_t & oppo_player)
{
	// in match.round == MAX_TEAM_ROUND, status = MATCH_STATUS_ROUND_END, should get elimination player list
	// if (match.round <= MAX_TEAM_ROUND) {
	if (match.round < MAX_TEAM_ROUND 
	|| (match.round == MAX_TEAM_ROUND && match.status == MATCH_STATUS_ROUND_START)) {
		return match_team_next(match, match.player_list, eid, oppo_player);
	}
	return match_eli_next(match, match.e_player_list, eid, oppo_player);
}



#ifdef TTT

int test_create_match(match_t &match, int max_player)
{
	int ret;
	
	time_t round_time_list[MAX_DAILY_ROUND];
	bzero(round_time_list, sizeof(round_time_list));
	round_time_list[0] = 20 * 60 * 60; // 20:00
	round_time_list[1] = 22 * 60 * 60; // 22:00

	// set match.round_time_list
	time_t now = time(NULL);
	time_t start_time;
	struct tm timestruct;
	localtime_r(&now, &timestruct);
	timestruct.tm_sec = 0;
	timestruct.tm_min = 0;
	timestruct.tm_hour = 0;
	start_time = mktime(&timestruct);

	ret = match_create(match, max_player, start_time, round_time_list, "match1");
	if (ret < 0) {
		ERROR_RETURN(-5, "match_create_fail %d", ret);
	}

	return ret;
}


int test0_match_info(int argc, char * argv[])
{
	int ret;
	printf("test0_match_info\n");
	match_t match;
	bzero(&match, sizeof(match));
	test_create_match(match, 8);

	match_apply(match, 1, 0, "1");

	match_player_t & player = get_player_last_round(match, 1);
	print_player(player);

	match_info(match, match.player_list);


	match_apply(match, 2, 0, "2");
	match_apply(match, 3, 0, "3");
	match_apply(match, 4, 0, "4");

	ret = match_team_init(match);
	ERROR_RETURN(-3 - ret, "test0:match_team_init");


	match_apply(match, 5, 0, "5");
	match_apply(match, 6, 0, "6");
	match_apply(match, 7, 0, "7");
	match_apply(match, 8, 0, "8");
	ret = match_apply(match, 9, 0, "9");
	ERROR_RETURN(-16 - ret, "test0:match_apply_full");

	match_team_init(match);

	print_all_record(match, match.player_list);
	match_info(match, match.player_list);

	match_player_t & player2 = get_player_last_round(match, 1);
	print_player(player2);

	ret = 0;
	return ret;
}


int test1_match_result(int argc, char * argv[]) 
{
	printf("test1_match_result\n");
	int ret;
	match_t match;
	bzero(&match, sizeof(match));
	test_create_match(match, 8);

	match_player_t (&player_list)[MAX_MATCH_PLAYER * MAX_TEAM_ROUND] = match.player_list;

	match_apply(match, 1, 0, "1");
	match_apply(match, 2, 0, "2");
	match_apply(match, 3, 0, "3");
	match_apply(match, 4, 0, "4");

	match_apply(match, 5, 0, "5");
	match_apply(match, 6, 0, "6");
	match_apply(match, 7, 0, "7");
	match_apply(match, 8, 0, "8");
	match_team_init(match);

	match.round = 1;
	// team1
	printf("------ team1 ------\n");

	printf("\n--------test1_before_match_result1--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 1, 3, 1, 1);
	printf("\n--------test1_after_match_result1--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 1, 3, 1, 1);
	printf("\n--------test1_after_match_result2--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 2, 4, 1, 1);
	printf("\n--------test1_after_match_result3--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 2, 4, 1, 1);
	printf("\n--------test1_after_match_result4--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match.round = 2;

	match_result(match, 2, 1, 1, 1);
	printf("\n--------test1_after_match_result5--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 2, 1, 1, 1);
	printf("\n--------test1_after_match_result6--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 4, 3, 1, 1);
	printf("\n--------test1_after_match_result7--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 4, 3, 1, 1);
	printf("\n--------test1_after_match_result8--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);
	
	match.round = 3;

	match_result(match, 4, 1, 1, 1);
	printf("\n--------test1_after_match_result9--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 4, 1, 1, 1);
	printf("\n--------test1_after_match_result10--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);


	// team2
	match.round = 1;
	printf("------ team2 ------\n");
	match_result(match, 7, 5, 1, 1);
	printf("\n--------test1_after_match_result11--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 7, 5, 1, 1);
	printf("\n--------test1_after_match_result12--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 8, 6, 2, 1);
	printf("\n--------test1_after_match_result13--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 8, 6, 2, 1);
	printf("\n--------test1_after_match_result14--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match.round = 2;

	match_result(match, 8, 5, 9, 1);
	printf("\n--------test1_after_match_result15--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 8, 5, 9, 2);
	printf("\n--------test1_after_match_result16--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 8, 5, 9, 1);
	printf("\n--------test1_after_match_result17--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 7, 6, 1, 1);
	printf("\n--------test1_after_match_result18--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	ret = match_result(match, 5, 7, 2, 1);
	ERROR_RETURN(-26-ret, "test1:normal_tid_mismatch");
	printf("\n--------test1_after_match_result19--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	return 0;

}

int test2_match_result_draw(int argc, char * argv[]) 
{
	printf("test2_match_result\n");
	match_t match;
	bzero(&match, sizeof(match));
	test_create_match(match, 8);

	match_player_t (&player_list)[MAX_MATCH_PLAYER * MAX_TEAM_ROUND] = match.player_list;

	match_apply(match, 1, 0, "1");
	match_apply(match, 2, 0, "2");
	match_apply(match, 3, 0, "3");
	match_apply(match, 4, 0, "4");

	match_apply(match, 5, 0, "5");
	match_apply(match, 6, 0, "6");
	match_apply(match, 7, 0, "7");
	match_apply(match, 8, 0, "8");
	match_team_init(match);

	// team2
	printf("------ team2 ------\n");
	match_result(match, 7, 5, 9, 1);
	printf("\n--------test2_after_match_result1--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 7, 5, 9, 2);
	printf("\n--------test2_after_match_result2--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);

	match_result(match, 7, 5, 1, 1);
	printf("\n--------test2_after_match_result3--------\n");
	print_all_record(match, player_list);
	match_info(match, player_list);
	return 0;
}


int test3_match_next(int argc, char* argv[])
{
	int ret;
	match_t match;
	bzero(&match, sizeof(match));
	test_create_match(match, 8);
	match_player_t opponent;

	printf("test3_match_next\n");

	match_player_t (&player_list)[MAX_MATCH_PLAYER * MAX_TEAM_ROUND] = match.player_list;

	match_apply(match, 1, 0, "1");
	match_apply(match, 2, 0, "2");
	match_apply(match, 3, 0, "3");
	match_apply(match, 4, 0, "4");

	match_apply(match, 5, 0, "5");
	match_apply(match, 6, 0, "6");
	match_apply(match, 7, 0, "7");
	match_apply(match, 8, 0, "8");
	match_team_init(match);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 5, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 5);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 7, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 7);

	// team2
	printf("------ team2 ------\n");
	match_result(match, 7, 5, 9, 1);
	printf("\n--------test3_after_match_result1--------\n");
	match_info(match, player_list);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 5, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 5);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 7, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 7);


	match_result(match, 7, 5, 9, 2);
	printf("\n--------test3_after_match_result2--------\n");
	match_info(match, player_list);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 5, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 5);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 7, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 7);


	match_result(match, 7, 5, 1, 1);
	printf("\n--------test3_after_match_result3--------\n");
	match_info(match, player_list);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 5, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 5);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 7, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 7);

	
	match_result(match, 6, 8, 1, 1);
	printf("\n--------test3_after_match_result3--------\n");
	match_info(match, player_list);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 6, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 6);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 8, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 8);

	
	match_result(match, 6, 8, 1, 1);
	printf("\n--------test3_after_match_result3--------\n");
	match_info(match, player_list);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 6, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 6);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 8, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 8);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 5, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 5);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 7, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 7);


	// round 2	
	match_result(match, 6, 7, 1, 1);
	printf("\n--------test3_after_match_result3--------\n");
	match_info(match, player_list);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 6, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 6);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 7, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 7);


	match_result(match, 6, 7, 1, 1);
	printf("\n--------test3_after_match_result3--------\n");
	match_info(match, player_list);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 6, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 6);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 7, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 7);
	

	match_result(match, 5, 8, 1, 1);
	printf("\n--------test3_after_match_result3--------\n");
	match_info(match, player_list);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 5, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 5);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 8, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 8);


	match_result(match, 5, 8, 1, 1);
	printf("\n--------test3_after_match_result3--------\n");
	match_info(match, player_list);


	match_result(match, 5, 7, 1, 1);
	printf("\n--------test3_after_match_result3--------\n");
	match_info(match, player_list);


	match_result(match, 5, 7, 1, 1);
	printf("\n--------test3_after_match_result3--------\n");
	match_info(match, player_list);


	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 5, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 5);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 6, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 6);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 7, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 7);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, 8, opponent);
	ERROR_RETURN(ret, "test3_match_next: eid=%d", 8);


	return 0;
}



int test4_get_round_time(int argc, char* argv[])
{
	match_t match;
	bzero(&match, sizeof(match));
	test_create_match(match, 8);
	time_t round_time = 0;
	time_t end_round_time = 0;
	int round;
	int hour_offset;
	int end_hour_offset;
	time_t now = time(NULL);

	round = 1;
	// round_time = get_round_time(match, round);
	// end_round_time = get_round_end_time(match, round);
	printf("test4_get_round_time:round=%d round_time=%ld round_end_time=%ld\n"
	, round, round_time, end_round_time);

	hour_offset = (round_time - now) / 60 / 60;
	end_hour_offset = (end_round_time - now) / 60 / 60;
	printf("round=%d hour_offset=%d end_hour_offset=%d\n"
	, round, hour_offset, end_hour_offset);

	round = 2;
	// round_time = get_round_time(match, round);
	// end_round_time = get_round_end_time(match, round);
	printf("test4_get_round_time:round=%d round_time=%ld round_end_time=%ld\n"
	, round, round_time, end_round_time);

	hour_offset = (round_time - now) / 60 / 60;
	end_hour_offset = (end_round_time - now) / 60 / 60;
	printf("round=%d hour_offset=%d end_hour_offset=%d\n"
	, round, hour_offset, end_hour_offset);

	round = 3;
	// round_time = get_round_time(match, round);
	// end_round_time = get_round_end_time(match, round);
	printf("test4_get_round_time:round=%d round_time=%ld round_end_time=%ld\n"
	, round, round_time, end_round_time);

	hour_offset = (round_time - now) / 60 / 60;
	end_hour_offset = (end_round_time - now) / 60 / 60;
	printf("round=%d hour_offset=%d end_hour_offset=%d\n"
	, round, hour_offset, end_hour_offset);

	return 0;
}

int test5_match_opponent(int argc, char* argv[])
{
	int ret;
	match_t match;
	bzero(&match, sizeof(match));
	test_create_match(match, 8);
	match_player_t opponent;
//	int round;
	int eid1;
	int eid2;

	printf("test3_match_next\n");

	// match_player_t player_list[match.max_player * MAX_TEAM_ROUND];
	match_player_t (&player_list)[MAX_MATCH_PLAYER * MAX_TEAM_ROUND] = match.player_list;

	match_apply(match, 1, 0, "1");
	match_apply(match, 2, 0, "2");
	match_apply(match, 3, 0, "3");
	match_apply(match, 4, 0, "4");

	match_apply(match, 5, 0, "5");
	match_apply(match, 6, 0, "6");
	match_apply(match, 7, 0, "7");
	match_apply(match, 8, 0, "8");
	match_team_init(match);
	match_info(match, player_list);

	// 5 win 7  2times 
//	round = 1;
	eid1 = 5; eid2 = 7;
	printf("-------test5:before %d win %d 2 times\n", eid1, eid2);
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid1, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid1);
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid2, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid2);
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	match_result(match, eid1, eid2, 1, 1);
	match_result(match, eid1, eid2, 1, 1);
	printf("-------test5:after %d win %d 2 times\n", eid1, eid2);
	match_info(match, player_list);

//	round = 2;
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid1, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid1);
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid2, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid2);
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	//////////////////////////

	// 8 win 6  2times 
	eid1 = 8; eid2 = 6;
	match_result(match, eid1, eid2, 1, 1);
	match_result(match, eid1, eid2, 1, 1);
	printf("-------test5:after %d win %d 2 times\n", eid1, eid2);
	match_info(match, player_list);

//	round = 2;
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid1, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid1);
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid2, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid2);
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	//////////////////////////
	// 8 win 5  2times, 7 win 6 2times 
	eid1 = 8; eid2 = 5;
	match_result(match, eid1, eid2, 1, 1);
	match_result(match, eid1, eid2, 1, 1);
	printf("-------test5:after %d win %d 2 times\n", eid1, eid2);
	eid1 = 7; eid2 = 6;
	match_result(match, eid1, eid2, 1, 1);
	match_result(match, eid1, eid2, 1, 1);
	printf("-------test5:after %d win %d 2 times\n", eid1, eid2);
	print_all_record(match, player_list);
	match_info(match, player_list);

	eid1 = 8; eid2 = 5;
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid1, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid1);
	printf("below is me\n");
	print_player(get_player_last_round(match, eid1));	
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid2, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid2);
	printf("below is me\n");
	print_player(get_player_last_round(match, eid2));	
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	eid1 = 7; eid2 = 6;
	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid1, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid1);
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid2, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid2);
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	//////////////////////////
	// 7 win 5  2times
	eid1 = 7; eid2 = 5;
	match_result(match, eid1, eid2, 1, 1);
	match_result(match, eid1, eid2, 1, 1);
	printf("-------test5:after %d win %d 2 times\n", eid1, eid2);
	print_all_record(match, player_list);
	match_info(match, player_list);

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid1, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid1);
	printf("below is me\n");
	print_player(get_player_last_round(match, eid1));	
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	bzero(&opponent, sizeof(opponent));
	ret = match_next(match, eid2, opponent);
	ERROR_RETURN(ret, "test5_match_next: eid=%d", eid2);
	printf("below is me\n");
	print_player(get_player_last_round(match, eid2));	
	printf("below is opponent\n");
	print_player(opponent);
	printf("\n");

	print_all_record(match, player_list);
	return 0;
}

int test6_create_match_id(int argc, char* argv[])
{
	long match_id = 0;
	for (int i = 0; i < 10; i++) {
		match_id = create_match_id();
		printf("%ld\n", match_id);
	}
	printf("%ld\n", time(NULL));

	return 0;
}
		
int test7_match_cancel(int argc, char* argv[])
{
	int ret;
	match_t match;
	bzero(&match, sizeof(match));
	test_create_match(match, 8);

	match_player_t (&player_list)[MAX_MATCH_PLAYER * MAX_TEAM_ROUND] = match.player_list;

	match_apply(match, 1, 0, "1");
	match_apply(match, 2, 0, "2");
	match_apply(match, 3, 0, "3");
	match_apply(match, 4, 0, "4");
	match_apply(match, 5, 0, "5");
	ret = match_cancel(match, 4);

	print_all_record(match, player_list);

	match_apply(match, 6, 0, "6");
	match_apply(match, 7, 0, "7");
	match_apply(match, 8, 0, "8");
	match_apply(match, 9, 0, "9");

	ret = match_cancel(match, 10);
	ERROR_RETURN(-5-ret, "test7:match_cancel_normal");
	print_all_record(match, player_list);

	ret = match_team_init(match);
	ERROR_RETURN(ret, "test7:match_team_init");

	ret = match_cancel(match, 5);
	ERROR_RETURN(-6-ret, "test7:match_is_running");

	match_info(match, player_list);

	return 0;
}

int test8_match_data(int argc, char* argv[])
{
//	int ret;
	match_t match;
	char out_buffer[2000];

	bzero(&match, sizeof(match));
	test_create_match(match, 8);

	match_player_t (&player_list)[MAX_MATCH_PLAYER * MAX_TEAM_ROUND] = match.player_list;

	match_apply(match, 1, 0, "1");
	match_apply(match, 2, 0, "2");
	match_apply(match, 3, 0, "3");
	match_apply(match, 4, 0, "4");
	match_apply(match, 5, 0, "5");
	match_apply(match, 6, 0, "6");
	match_apply(match, 7, 0, "7");
	match_apply(match, 8, 0, "8");

	bzero(out_buffer, sizeof(out_buffer));
	match_team_data(match, 0, out_buffer);
	printf("out_buffer: %s\n", out_buffer);
	match_team_data(match, 1, out_buffer);
	printf("out_buffer: %s\n", out_buffer);

	match_team_init(match);

	match_info(match, player_list);

	bzero(out_buffer, sizeof(out_buffer));
	match_team_data(match, 0, out_buffer);
	printf("out_buffer: %s\n", out_buffer);
	match_team_data(match, 1, out_buffer);
	printf("out_buffer: %s\n", out_buffer);

	return 0;
}


int test9_match_eli_init(int argc, char* argv[])
{
	int ret;
	match_t match;
	bzero(&match, sizeof(match));
	test_create_match(match, 4);
	match_player_t oppo;
	// char out_buffer[2000];
	int eid1;
	int eid2;
	int eid3;
	int eid4;

	printf("test9_match_eli_init\n");

	// match_player_t player_list[match.max_player * MAX_TEAM_ROUND];
	match_player_t (&player_list)[MAX_MATCH_PLAYER * MAX_TEAM_ROUND] = match.player_list;
	match_player_t (&e_player_list)[MAX_MATCH_PLAYER] = match.e_player_list;

	match_apply(match, 1, 0, "p1");
	match_apply(match, 2, 0, "p2");
	match_apply(match, 3, 0, "p3");
	match_apply(match, 4, 0, "p4");

	match_team_init(match);
	match_info(match, player_list);

	// round 1, eid1 eid3 win, eid2 eid4 lose
	eid1 = 1;
	bzero(&oppo, sizeof(oppo));
	ret = match_next(match, eid1, oppo);
	eid2 = oppo.eid;
	match_result(match, eid1, eid2, 1, 1);
	match_result(match, eid1, eid2, 1, 1);

	if (eid2 == 2) {
		eid3 = 3; 
	} else {
		eid3 = 2;
	}

	bzero(&oppo, sizeof(oppo));
	ret = match_next(match, eid3, oppo);
	ERROR_RETURN(ret, "match_eli_init: match_next_error");
	eid4 = oppo.eid;
	match_result(match, eid3, eid4, 1, 1);
	match_result(match, eid3, eid4, 1, 1);
	match_info(match, player_list);

	// round 2, eid1 eid2 win, eid3 eid4 lose
	match_result(match, eid1, eid3, 1, 1);
	match_result(match, eid1, eid3, 1, 1);

	match_result(match, eid2, eid4, 1, 1);
	match_result(match, eid2, eid4, 1, 1);

	print_all_record(match, player_list);
	match_info(match, player_list);


	// round 3, eid2 win, eid3 lose
	match_result(match, eid2, eid3, 1, 1);
	match_result(match, eid2, eid3, 1, 1);
	match_info(match, player_list);

	// round 4
	match_eli_init(match);

	match.round = 4;
	match_result(match, eid1, eid2, 1, 1);
	match_result(match, eid1, eid2, 1, 1);
	print_all_eli_record(match, e_player_list);

	match_eli_info(match, e_player_list);

	return 0;
}




typedef int (*testcase_t) (int, char*[]); 

testcase_t test_list[] = {
	test0_match_info
,	test1_match_result
,	test2_match_result_draw
,	test3_match_next
,	test4_get_round_time
,	test5_match_opponent
,	test6_create_match_id
,	test7_match_cancel
,	test8_match_data
,	test9_match_eli_init
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
	if (ret != 0 ) {
		printf("XXXXXXXXX BUG ret!=0: %d\n", ret);
	}
	
	return ret;
}


int main(int argc, char* argv[]) {

	
	/*
	int ret;
	match_t match;
	bzero(&match, sizeof(match));
	time_t round_time_list[MAX_DAILY_ROUND];
	round_time_list[0] = 20 * 60 * 60; // 20:00
	round_time_list[1] = 22 * 60 * 60; // 22:00
	int max_player = 8;

	// set match.round_time_list
	time_t now = time(NULL);
	time_t start_time;
	struct tm timestruct;
	localtime_r(&now, &timestruct);
	timestruct.tm_sec = 0;
	timestruct.tm_min = 0;
	timestruct.tm_hour = 0;
	start_time = mktime(&timestruct);

	ret = match_create(match, max_player, start_time, round_time_list, "match1");
	if (ret < 0) {
		ERROR_RETURN(-5, "match_create_fail %d", ret);
	}
	*/
		


	if (argc > 1 && strcmp("all", argv[1])==0) {
		int testmax = sizeof(test_list) / sizeof(test_list[0]);
		int error_count = 0;
		for (int i=0; i<testmax; i++) {
			int ret;
			char str[10];
			char *alist[2] = {argv[0], str};
			sprintf(str, "%d", i);
			ret = test_selector(argc, alist); // changed argv
			if (ret != 0) { error_count++; break;}
		}
		printf("TEST ALL SUMMARY: error_count=%d\n", error_count);
	} else {
		test_selector(argc, argv);
	}

	return 0;
}

#endif
