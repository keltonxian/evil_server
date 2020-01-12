// wchat_queue.cpp : chat queue for global chat
#include <stdio.h>
#include <string.h>
#include "fatal.h"

// header declaration in evil.h 
// 
// fix-size circular queue for fast insert and fast truncate (first half)

// queue_len is the best with even number
#ifdef TTT
	#define WCHAT_QUEUE_MAX		6		
	#define WCHAT_MSG_MAX		100
#else
	#define WCHAT_QUEUE_MAX		1000		// TODO : 1000 for busy server
	// #define WCHAT_MSG_MAX		(100+30+15)	// 100=msg, 30=alias, 15=wchat [eid]
	#define WCHAT_MSG_MAX		(200+30+15)	// 100=msg, 30=alias, 15=wchat [eid]
#endif

// TODO move this struct to evil.h, include it
typedef struct msg_struct {
	const char * msg;
	int len;
} msg_t;
		// for fast calculation:
		// typedef struct msg_struct { const char *msg;  int len; } MSG_T;
char wchat_queue[WCHAT_QUEUE_MAX][WCHAT_MSG_MAX+1];

// static like private, can only be accessed locally
static int g_head = 0;  // init 0
static int g_tail = 0;  // both 0 means empty
static int g_timestamp = 0; // timestamp of head msg w.r.t.  wchat_queue[g_head]


int wchat_head_ts()
{
	return g_timestamp;
}


int wchat_tail_ts()
{
	// if g_tail > g_head
	int diff ;
	// g_tail = 5, g_head = 0, diff = 5
	// g_tail = 2, g_head = 3, WCHAT_QUEUE_MAX=6, 
	// diff = WCHAT_QUEUE_MAX - g_head + g_tail
	if (g_tail >= g_head) {
		diff = g_tail - g_head;
	} else {
		diff = WCHAT_QUEUE_MAX - g_head + g_tail;
	}
	return g_timestamp + diff;
}


int wchat_init(int head_ts)
{
	g_head = 0;
	g_tail = 0;
	g_timestamp = head_ts;
	return 0;
}


// TODO filtered before add ?
// -3 : null
// -2 : too long
// -22 : msg queue overflow (auto truncate, so it should not appear)
// return 0 for success
int wchat_add(const char * msg)
{
	if (NULL==msg) {
		return -3;
	}
	int len = strlen(msg);
	if (len >= WCHAT_MSG_MAX) {
		return -2; // overflow, do not copy, not accept truncated msg
	}

	// 2 cases
	// ghead ... gtail
	// ... gtail    ghead ...

	strcpy(wchat_queue[g_tail], msg);
	// int ts = wchat_tail_ts(); // order is important, before g_tail ++
	g_tail ++;
	// round-robin
	if (g_tail >= WCHAT_QUEUE_MAX) {
		g_tail = 0;
	}

	if (g_head==g_tail) {
		// DEBUG_PRINT(0, "wchat_queue full, truncate by half");
		// this means queue is full, truncate by half
		int shift = WCHAT_QUEUE_MAX >> 1;
		g_head = (g_head + shift) % WCHAT_QUEUE_MAX;
		g_timestamp += shift;
	}
	return 0;
}



// caller should NEVER free the pointer returned
const char * wchat_get(int ts)
{
	if (ts < wchat_head_ts()  || ts >= wchat_tail_ts()) {
		// printf("BUG : wchat_get outbound\n");
		ERROR_PRINT(-22, "wchat_get:outbound ts %d  head_ts %d  tail_ts %d"
			, ts, wchat_head_ts(), wchat_tail_ts());
		return NULL; // outbound
	}
	int diff = ts - wchat_head_ts();

	int index = (g_head + diff) % WCHAT_QUEUE_MAX;
	return wchat_queue[index];
}

// internal use
// ts -> index
int wchat_ts_check(int ts)
{
	if (ts < wchat_head_ts()  || ts >= wchat_tail_ts()) {
		return -2;
	}

	return 0;
}

// internal use
int wchat_index_ts(int index)
{
	int ts = -1;
	int diff;

	if (index >= g_tail || index < g_head) {
		return ts;
	}

	if (g_tail >= g_head) {
		diff = index - g_head;
		return wchat_head_ts() + diff;
	}

	// implicit: g_tail < g_head
	if (index >= g_head) {
		diff = index - g_head;
		return wchat_head_ts() + diff;
	}
	if (index < g_tail) {
		diff = WCHAT_QUEUE_MAX - g_head + index;
		// wchat_head_ts() + diff;
		if ((wchat_tail_ts() + index - g_tail) != (wchat_head_ts() + diff)) {
			BUG_PRINT(-7, "wchat_tail_ts and wchat_head_ts invalid");
		}
		return wchat_head_ts() + diff;
	}
	// index >= g_tail and index < g_head : out of range
	return ts; // let it be 9999 for out of range
}


int wchat_print()
{
	printf("----- head=%d(ts:%d)  tail=%d(ts:%d) -----\n"
		, g_head, wchat_head_ts(), g_tail, wchat_tail_ts());
		
	int ts;
	for (int i=0; i<WCHAT_QUEUE_MAX; i++) {
		char head_char = ' ';
		char tail_char = ' ';
		if (i==g_head) head_char = 'H';
		if (i==g_tail) tail_char = 'T';
		ts = wchat_index_ts(i);
		printf("%c%c [%-4d] ts:%-4d : %s\n", head_char, tail_char
		, i , ts, wchat_queue[i]);
	}
	printf("===== ts ordered view =====\n");
	for (int i=wchat_head_ts(); i<wchat_tail_ts(); i++) {
		printf("ts:%-4d : %s\n", i, wchat_get(i));
	}
	printf("+++++ +++++ +++++ +++++\n");
	return 0;
}

////////// ////////// ////////// ////////// //////////
//////////    ABOVE is production code      //////////
////////// ////////// ////////// ////////// //////////
//////////    BELOW is testing code         //////////
////////// ////////// ////////// ////////// //////////

#ifdef TTT

int error_test(int tag)
{
	switch (tag) 
	{
		case 0: BUG_RETURN(5, "bug return"); 
		case 1: ERROR_RETURN(6, "error return");
		case 2: 
			BUG_NEG_RETURN(5, "bug neg return  NOT DISPLAY"); 
			BUG_NEG_RETURN(-5, "bug neg return  OK"); 

		case 3: 
			ERROR_NEG_RETURN(6, "error neg return NOT DISPLAY");
			ERROR_NEG_RETURN(-6, "error neg return OK");
		case 9: FATAL_EXIT(7, "fatal exit");
	}

	printf("unknown tag: %d\n", tag);
	return 0;
}

// for testing fatal.h
int all_error_test()
{
	error_test(0);  // BUG
	error_test(1);  // ERROR
	error_test(2);  // BUG NEG
	error_test(3);  // ERROR NEG
	error_test(9);  // FATAL (this will exit)
	return 0;
}

int main(int argc, char *argv[])
{


	wchat_init(0);

	// assume WCHAT_QUEUE_MAX = 6
	wchat_print();

	wchat_add("AAAA");
	wchat_add("BBBB");
	wchat_add("CCCC");
	wchat_add("DDDD");


	wchat_print();
	
	int ts = wchat_index_ts(5);
	printf("index = 5 , ts = %d\n", ts);

	wchat_add("EEEE");
	wchat_print();
	wchat_add("LAST");

	wchat_print();


	wchat_add("XXX");
	wchat_add("YYY");
	wchat_print();

	wchat_add("ZZZ_full");
	wchat_print();
	return -10;
}

#endif

