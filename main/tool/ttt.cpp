/**
 * ttt.c : small test case for general testing
 * g++ ttt.c
 *
 */

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdarg.h>

#include <time.h>
#include <sys/time.h>	// for gettimeofday

#include <errno.h>
#include <locale.h>  // peter: for testing chinese
#include "fatal.h"

}


// C++ header
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <list>
#include <deque>
#include <locale>
#include <typeinfo>   // for exception
#include <exception>


using namespace std;


////////// CONSTANT START //////////
#define USERNAME_MAX	100
#define PASSWORD_MAX	30
#define ALIAS_MAX	30

#define STATE_FREE 	0
// non-zero is non-free

#define STUB_MAX	20
#define USER_MAX	40
#define ROOM_MAX	10	// should be same as USER_MAX
#define CHANNEL_MAX	3	// 0, 1, 2
////////// CONSTANT END //////////



////////// CLASS START //////////
class array_t {
	public:
	int state;
	int next_free;

	virtual int get_max(void) { return 0; } // this is default
};

array_t aaa;  // for static ?

// @see db-init.sql : TABLE evil_user + evil_status
// typedef struct _evil_user_struct {
class evil_user_t : public array_t {
	friend ostream& operator<<(ostream& os, const evil_user_t & user) {
		// os << "EVIL_USER_T";
		os << "evil_user_t:[eid=" << user.eid << ",lv=" << user.lv << ",name=" 
			<< user.username 
			<< ",info=" << user.info 
			<< "]";

		return os;
	}

	public:
	// JAVA: evil_user_t   eu = evil_user_t();
	// evil_user_t  eu;   // eu() is wrong
	// evil_user_t  eu(303);  // this is ok:  evil_user_t(int _eid)
	evil_user_t() : eid(0) {	 // constructor (empty)
		// bzero(this, sizeof(evil_user_t));    // segmentation fault
		// bzero(this, sizeof(this));
		eid = 0;  // same as : eid(0)
		bzero(username, sizeof(username));  // set zero
		strcpy(username, "no_name");
		password[0] = '\0';
		// init: alias, ...
	}
	evil_user_t(int _eid) : eid(_eid) {
	}

	int get_max(void) { return USER_MAX; }

	int eid;
	// string info;
	string info;
	char username[USERNAME_MAX + 1];
	char password[PASSWORD_MAX + 1];
	char alias[ALIAS_MAX + 1];
	struct tm last_login;  // SQL DATETIME , strftime, strptime

	// -- below are evil_status specific

	// level rating etc
	int lv;		// lv = 0 means it is not initialized
	int rating;

	// money related
	int gold;
	int crystal;

	// reserve1, reserve2 for future use!
	int reserve1;
	int reserve2;

	// count for auto-match only
	int game_count;
	int game_win;
	int game_lose;
	int game_draw;
	int game_run;

};



class stub_t : public array_t {
	public:
	int id;		// assert(g_connect_list[id].id == id)
	//////////////////
	int eid;	// 16 bytes

	int euser_index;
	int room_index;
	/*** simplified stub_t
	int next_free;  // index to the next free slot (link-list)
	int conn_fd;  // shall we put this as the first variable in struct?
	struct sockaddr_in address;

	char buffer[BUFFER_SIZE+1];  // TODO this is too large!
	int offset;   // write offset
	int outlen;   // buffer[offset] to buffer[outlen-1] is the pending write
	// every ret = write(...),  offset+=ret (positive case)
	// when offset=outlen, all are written, set offset=outlen=0
	// to append new write:  buffer[outlen .. outlen+newlen] = new data
	// if outlen + newlen > BUFFER_SIZE then overflow!
	int level;
	***/

	// stub level, 0=not_login,  5=login'ed,  10=room_created 15=game

	// game specific: TODO move it to game context

	static const int max = STUB_MAX;
	int get_max() { return max; }
};


#define MAX_GUEST 10
class room_t : public array_t {
	public:
	int id;  // master eid   key for hash_map
	int channel_id;
	int guest[MAX_GUEST];
	int num_guest;  // <= MAX_GUEST

	const static int max = ROOM_MAX;
	int get_max() { return max; }
};

// useless later
typedef map<int, evil_user_t>	MAP_USER;	// eid -> evil_user_t
typedef map<int, room_t>	MAP_ROOM;		// eid -> room_t

// utility function for timing
long get_micro_time()
{
	struct timeval tv;
	gettimeofday(&tv, NULL) ;
	return tv.tv_sec * 1000000 + tv.tv_usec;
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
int split9(char *tok[9], char *str)
{
	const char *sep = " \t\r\n";
	// char * ptr = NULL;
	int i;

	tok[0] = strtok(str, sep);
	if (tok[0]==NULL) {
		return 0;
	}

	i = 1;
	for (i=1; i<9; i++) {
		tok[i]=strtok(NULL, sep);
		if (tok[i]==NULL) {
			return i;
		}
	}
	return i;
}


// @see http://stackoverflow.com/questions/10324/how-can-i-convert-a-hexadecimal-number-to-base-10-efficiently-in-c
// no need: gcc -std=c99
// note: g++ does not work on this
/**
static const int hextable[] = {
	[0 ... 255] = -1, // bit aligned access into this table is considerably
	['0'] = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, // faster for most modern processors,
	['A'] = 10, 11, 12, 13, 14, 15,       // for the space conscious, reduce to
	['a'] = 10, 11, 12, 13, 14, 15        // signed char.
};
**/

/**
char * const cp; ( * 读成 pointer to ) 
cp is a const pointer to char 

const char * p; 
p is a pointer to const char; 

char const * p; 
同上因为C++里面没有const*的运算符，所以const只能属于前面的类型。 
**/

int fun1(char const * c1, const char * c2, char * const c3)
{
	// c1[1] = 'A'; // compile error
	// c2[0] = 'B'; // compile error
	// c3 = NULL; // compile error

	// c1 = malloc(20);  // this is ok
	// c2 = malloc(20);  // this is ok

	sprintf((char*)c1, "change1"); // ok compile : warning
	sprintf((char*)c2, "change2"); // ok compile : warning
	// c2[2] = 'X'; // compile error even after malloc
	// c3 = "boy"; // compile error
	// c3[1 ... 2] = 'Z';  // compile error
	c3[1] = 'Z';
	c3[2] = 'X';
	
	// this is interesting!  C99 or C++ ?
	return 0;
}

// string test and simple array test
int test0(int argc, char **argv)
{
	// printf("hex (b) = %d\n", hextable['b']);
	printf("%%\n");

	char str[20];
	char *token[18];
	int len;
	strcpy(str, "AAA  bb   cCc");
	len = strlen(str);
	int total = split9(token, str);

	for (int i=0; i<total; i++) {
		printf("t%d = [%s]\n", i, token[i]);
	}

	printf("original str = ");
	for (int i=0; i<len; i++) {
		printf(" %d", str[i]);
	}
	printf("\n");



	return 0;
}

int test1(int argc, char *argv[]) 
{
	char str1[100];
	char str2[100];
	char str3[100];

	strcpy(str1, "str1");
	strcpy(str2, "str2");
	strcpy(str3, "str3");

	fun1(str1, str2, str3);
	

	printf("str1 = [%s]\n", str1);
	printf("str2 = [%s]\n", str2);
	printf("str3 = [%s]\n", str3);
	return 0;
}


int safe_write(int flag, const char*fmt, ...) 
{
	char buffer[1000];
	va_list argptr;
	int count;
	va_start(argptr, fmt);
	count = vsprintf(buffer, fmt, argptr);
	va_end(argptr);

	printf("flag %d  sss=[%s]\n", flag, buffer);

	return count;
}


int test2(int argc, char *argv[]) {
	int flag = 0;
	return safe_write(flag, "d=%d  s=[%s]", 33, "bb");
}


/**
 * map erase usage (how to remove item in map iterator)
 * http://zhangyonggameres.blog.163.com/blog/static/1347349032010014950661/
 */
int test3(int argc, char *argv[])
{
	map<int, room_t> room_map;
	room_t *room_ptr;
	room_t room0, room1, room2, room3;


	room0.id = 20;
	room0.num_guest = 1;
	room0.guest[0] = 20;
	room0.guest[1] = 21;

	room1.id = 21;
	room1.num_guest = 2;

	room_map[20] = room0;
	room_map[21] = room1; // content copy?

	printf("&room0=%p  %p\n", &room0, &(room_map[20]));
	printf("room_map.size = %zu\n", room_map.size());

	room_map.erase(20);
	printf("after erase room_map.size = %zu\n", room_map.size());

	printf("&room0=%p  %p\n", &room0, &(room_map[20]));

	printf("room[21].id = %d\n", room_map[21].id);
	printf("room[20].id = %d\n", room_map[20].id);

	for (int i=30; i<1000; i++) {
	}

	return 0;
}


/***
 * test: room_t &  in stub
 * what if the referenced memory is cleared?
  std::map<eid, euser_t>::iterator it;
 */
int test4(int argc, char *argv[]) 
{
	cout << "sizeof( stub_t ) = " << sizeof(stub_t) << endl;
	cout << "sizeof( room_t ) = " << sizeof(room_t) << endl;
	MAP_USER user_map;

	// evil_user_t  	euser();  // this is wrong for empty constructor
	evil_user_t  	euser;

	euser.eid = 3;
	euser.lv = 33;
	user_map[euser.eid] = euser;

	euser.eid = 4;
	euser.lv = 444;
	user_map[euser.eid] = euser;

	euser.eid = 505;
	euser.lv = 55;
	user_map[euser.eid] = euser;

	cout << euser << endl;

	cout << user_map[4] << endl;
	cout << "Invalid key 66: " << endl;
	cout << user_map[66] << endl;

	return 0;
}


// this is wrong!!!
int valid_iterator(const MAP_USER & map, const MAP_USER::iterator& it, int key)
{
	if (it == map.end() ) {
		return 0;
	}


	// ok it < map.end()
	
	// now make sure the key match!
	return it->first == key;
}


int test5(int argc, char *argv[]) 
{
	MAP_USER user_map;

	cout << "test5 111111" << endl;
	// evil_user_t  	euser();  // this is wrong for empty constructor
	evil_user_t  	euser;  // constructor 1

	euser.eid = 3;
	euser.lv = 33;
	user_map[euser.eid] = euser;  // ??

	cout << "test5 AAAAAA" << endl;

	euser.eid = 4;
	euser.lv = 444;
	user_map[euser.eid] = euser;

	euser.eid = 505;
	euser.lv = 55;
	user_map[euser.eid] = euser;

	cout << "test5 222222" << endl;
	
	// evil_user_t & refuser = user_map[505];
	//cout << "refuser (999) = " << refuser << endl;

	MAP_USER::iterator it;
	it = user_map.find(505);
	cout << "find(505)==end ? : " << (user_map.end()==it) << endl;


	it = user_map.find(66);
	cout << "find(66)==end ? : " << (user_map.end()==it) << endl;

	it = user_map.find(4);
	cout << "find(4)==end ? : " << (user_map.end()==it) << endl;
	cout << it->second << endl;

	user_map.clear();
	cout << "after clear() : " << endl << it->second << endl;

	cout << "valid(4) : " << valid_iterator(user_map, it, 4) << endl;


	cout << "CONCLUSION: we cannot gurantee iterator is valid!!" << endl;
	
	return 0;
}


evil_user_t		g_euser_list[1]; // [USER_MAX];	// need bzero
room_t 	g_room_list[ROOM_MAX];	// need bzero
stub_t	g_stub_list[STUB_MAX];

// eid -> index (g_euser_list[index])
map<int, int>	g_euser_index_map;


int init_global_list() {
	printf("sizeof g_euser_list %zu\n", sizeof(g_euser_list));
//	bzero(g_euser_list, sizeof(evil_user_t) * USER_MAX);
	// add channel
//	bzero(g_room_list, sizeof(room_t) * ROOM_MAX * CHANNEL_MAX);
//	bzero(g_stub_list, sizeof(stub_t) * STUB_MAX);

	// init: next_free
	for (int i=0; i<stub_t::max; i++) {
		g_stub_list[i].state = STATE_FREE;
		g_stub_list[i].next_free = i + 1;
	}
	// g_stub_list[0].max = STUB_MAX;

	for (int i=0; i<USER_MAX; i++) {
		g_euser_list[i].state = STATE_FREE;  // STATE_FREE is 0, bzero already did
		g_euser_list[i].next_free = i + 1; 
	}
	// g_euser_list[0].max = USER_MAX;

	for (int i=0; i<ROOM_MAX; i++) {
		g_room_list[i].state = STATE_FREE;
		g_room_list[i].next_free = i + 1;
	}

	return 0;
}

int print_max(array_t & array) 
{
	cout << array.get_max() << endl;
	return 0;
}

int test6(int argc, char *argv[]) 
{
	init_global_list();
	cout << "g_euser_list : " << endl;
	evil_user_t & euser = g_euser_list[0];
	cout << "next_free = " << euser.next_free << endl;
	// segment:
//	cout << "get_max = " << (&euser)->get_max() << endl;
//	print_max(euser);
	evil_user_t *puser = new evil_user_t;
	cout << "puser->get_max: " << puser->get_max() << endl;

	evil_user_t realuser;
	cout << "realuser.get_max: " << realuser.get_max() << endl;

	cout << "print_max(real_user)";
	print_max(realuser);

	// this: segmentation fault
	// cout << "g_euser_list[0].get_max: " << g_euser_list[0].get_max() << endl;

/**
	for (int i=1; i<USER_MAX; i++) {
		euser.eid = i + 100;
		euser.lv = (i % 10) + 1;
		g_euser_list[i] = euser; // memory copy
	}
**/
	return 0;
}

int test7(int argc, char *argv[])
{
	const int user_max = 10000;
	const int access_max = 5000000;  // 1 mil
	map<int,evil_user_t> user_map;  // eid -> evil_user_t
	evil_user_t user_list[user_max]; // array[]
	map<int, int> user_index_map;  // eid -> index  to array

	cout << "sizeof(evil_user_t) = " << sizeof(evil_user_t) << endl;

	evil_user_t euser;  // this is not a pointer
	evil_user_t euser2;
	evil_user_t & euser3 = euser2;  // this is ref
	// euser3 = euser2;  
	strcpy(euser.username, "NaMe");
	euser.eid = 5;
	euser.lv = 50;
	euser2 = euser;  // memory copy clone
	euser2.info = "boy";
	euser2.info += " is good";
	cout << "euser: " << euser << endl;
	cout << "euser2: " << euser2 << endl;
	cout << "euser3: " << euser3 << endl;
	euser2.lv = 88;
	cout << "after=88 euser2: " << euser2 << endl;
	cout << "after=88 euser: " << euser << endl;
	cout << "after=88 euser3: " << euser3 << endl;
	euser = euser2; // copy 
	cout << "after euser=euser2 euser: " << euser << endl;
	cout << "after euser=euser2 euser2: " << euser2 << endl;

	// ???  info is not shared ?
	euser.info = "girl";
	euser.lv = 3;
	cout << "after info=girl euser: " << euser << endl;
	cout << "after info=girl euser2: " << euser2 << endl;

	cout << "sizeof(euser) = " << sizeof(euser) << endl;
	cout << "sizeof(euser2) = " << sizeof(euser2) << endl;

	if (1) return 0;

	for (int i=0; i<user_max; i++) {
		euser.eid = i+1000;  // start from 1000
		euser.lv = 10 + (i % 10); // 10<=lv<=19
		
		
		user_map[euser.eid] = euser;  // hash map
		user_list[i] = euser;
		user_index_map[euser.eid] = i;
	}

	// access N times
	int count = 0;
	long before, after;

	count = 0;
	before = get_micro_time();
	for (int i=0; i<access_max; i++) {
		int eid = (i % user_max) + 1000;
		evil_user_t & refuser = user_map[ eid ];
		if (10 == refuser.lv) {
			count ++ ;
		}
	}
	after = get_micro_time();
	cout << "map ref count = " << count << "   time diff: " 
	<< (after - before) << endl;

	/***  map assign are similar to map ref **/
	count = 0;
	before = get_micro_time();
	for (int i=0; i<access_max; i++) {
		int eid = (i % user_max) + 1000;
		evil_user_t user = user_map[ eid ];  // no &, this is memory copy
		if (10 == user.lv) {
			count ++ ;
		}
	}
	after = get_micro_time();
	cout << "map assign count = " << count << "   time diff: " 
	<< (after - before) << endl;
	// ***/

	count = 0;
	before = get_micro_time();
	for (int i=0; i<access_max; i++) {
		evil_user_t & refuser = user_list[i % user_max];
		if (10 == refuser.lv) {
			count ++ ;
		}
	}
	after = get_micro_time();
	cout << "list ref count = " << count << "   time diff: " 
	<< (after - before) << endl;


	count = 0;
	before = get_micro_time();
	for (int i=0; i<access_max; i++) {
		evil_user_t user ;
		user = user_list[i % user_max];
		if (10 == user.lv) {
			count ++ ;
		}
	}
	after = get_micro_time();
	cout << "list assign count = " << count << "   time diff: " 
	<< (after - before) << endl;

	/*** ptr are similar to ref 
	count = 0;
	before = get_micro_time();
	for (int i=0; i<access_max; i++) {
		evil_user_t * puser = user_list + ( i % user_max);
		if (10 == puser->lv) {
			count ++ ;
		}
	}
	after = get_micro_time();
	cout << "list ptr count = " << count << "   time diff: " 
	<< (after - before) << endl;
	***/

	count = 0;
	before = get_micro_time();
	for (int i=0; i<access_max; i++) {
		int eid = 1000 + (i % user_max);
		int index = user_index_map[ eid ];  // hash map
		evil_user_t & refuser = user_list[index];
		if (10 == refuser.lv) {
			count ++ ;
		}
	}
	after = get_micro_time();
	cout << "list index ref count = " << count << "   time diff: " 
	<< (after - before) << endl;
	return 0;
}

int test8(int argc, char *argv[])
{
	int total = 10;
	if (argc >= 2)
	{
		total = atoi(argv[2]);
	}
	cout << "total = " << total << endl;
	return 0;
}

class Base {
	public:
	virtual int get_max() { return 0; } ;
};

class Child : public Base {
	public:
	const static int MAX_USER = 10;
	virtual int get_max() { return MAX_USER; } ;
};

int test9(int argc, char *argv[]) 
{
	Child child;
	Child child_list[10];
	void * ptr;
	int (*funptr) (void);
	ptr = (void*) &child;
	funptr = (int (*)())ptr;
	cout << "*((int*)ptr) = " << *((int*)ptr) << endl;
//	cout << "funptr() = " << funptr() << endl;
//	cout << "child = " << *((int *) &child) << endl;
//	cout << "child_list[0] = " << *((int*) child_list) << endl;
//	cout << "child max = " << child.get_max() << endl;

	bzero(child_list, sizeof(Child) * 10);
	// cout << "child_list[0] max = " << child_list[0].get_max() << endl;
	return 0;
}



// test10 for C++ pair usage
int test10(int argc, char *argv[]) 
{
	// eid -> <channel.id, room.id>
	map<int,  pair<int, int> >  eid_room_map;
	pair<int, int>  room_pair;
	int eid;

	eid_room_map[10] = make_pair(0, 3);  // channel=0,  room=3
	eid_room_map[11] = make_pair(1, 1);  // channel=1,  room=1
	eid_room_map[12] = make_pair(1, 2);  // channel=1,  room=2


	eid = 11;
	room_pair = eid_room_map[eid];
	cout << "eid:" << eid << "  room_pair channel=" << room_pair.first 
		<< "  room=" << room_pair.second << endl;

	eid = 10;
	room_pair = eid_room_map[eid];
	cout << "eid:" << eid << "  room_pair channel=" << room_pair.first 
		<< "  room=" << room_pair.second << endl;

	eid = 12;
	room_pair = eid_room_map[eid];
	cout << "eid:" << eid << "  room_pair channel=" << room_pair.first 
		<< "  room=" << room_pair.second << endl;

	eid = 30;
	room_pair = eid_room_map[eid];
	cout << "eid:" << eid << "  room_pair channel=" << room_pair.first 
		<< "  room=" << room_pair.second << endl;

	return 0;
}


// test vector with string,   cpp string <-> c string
int test11(int argc, char *argv[]) 
{
	vector<string> play_list;
	char cstr[100];
	string cppstr;
	strcpy(cstr, "four");

	play_list.push_back("hello");  // play_list[0]
	play_list.push_back("hello222"); // play_list[1]
	play_list.push_back("play 333");
	play_list.push_back(cstr);  // cstr -> cppstr and add to vector

	for (int i=0; i<play_list.size(); i++) {
		cout << i << ":" << play_list[i] << endl;
	}


	cppstr = play_list[2];
	strcpy(cstr, cppstr.c_str());
	printf("cstr = [%s]\n", cstr);
	

	return 0;
}

int test12(int argc, char *argv[]) 
{

	printf("\x1B[36mC36\x1B[0m\n"); /// this color works
	printf("\x1B[33mC33\x1B[0m\n");

	for (int i=30; i<=38; i++) {
		printf("\x1B[%dmC%d\x1B[0m\n", i, i);
	}

	printf("\x1B[35m\x1B[4mC35+4m\x1B[0m\n");
	// 31m = red  // ERROR
	// 34m = blue
	// 33m = dark yellow (WARN)
	// 35m = pink
	return 0;
}

typedef struct {
	vector<string> play_list;
} test_t;
int test13(int argc, char *argv[]) 
{
	test_t  tt;

	tt.play_list.push_back("hello1");
	tt.play_list.push_back("hello2");
	tt.play_list.clear();

	if ( tt.play_list.size() >= 1) {
		cout << "before: play_list[0] = " << tt.play_list[0] << endl;
	}

	bzero(&tt, sizeof(test_t));
	cout <<  "after size: " << tt.play_list.size() << endl;

	tt.play_list.push_back("xxxx");

	cout <<  "after push size: " << tt.play_list.size() << endl;
	if ( tt.play_list.size() >= 1) {
		cout << "play_list[0] = " << tt.play_list[0] << endl;
	}

	return 0;
}


int test14(int argc, char *argv[]) 
{
	struct timeval tv;
	#define MAX_TIME	1000
	long vlist[MAX_TIME];

	gettimeofday(&tv, NULL);
	printf("tv : %ld  %d \n", tv.tv_sec, (int)tv.tv_usec);
	long vv = tv.tv_sec * 1000000 + tv.tv_usec;

	for (int i=0; i<MAX_TIME; i++) {
		gettimeofday(&tv, NULL);
		
		vlist[i] = tv.tv_sec * 1000000 + tv.tv_usec;
		printf("vlist[%d] = %ld\n", i, vlist[i]);
	}

	for (int i=0; i<MAX_TIME; i++) {
		printf("vlist[%d] = %ld\n", i, vlist[i]);
	}

	printf("sizeof(long) = %zu\n", sizeof(long));
	return 0;
}

// note: dequeue and list:
// list can be access/erase at front/end (pop_front/pop_end)
// but it cannot be random access, e.g. list[i] is invalid (list.at(i))
// dequeue is similar to list, but it can be randomly accessed,
// e.g. dequeue[i] or dequeue.at(i)
int test15(int argc, char *argv[]) 
{
#define INTLIST	list<int>
	INTLIST quick_list;

	quick_list.push_back(10);
	quick_list.push_back(15);
	quick_list.push_back(20);

	cout << "before:" << endl;
	INTLIST::iterator it = quick_list.begin();
	while (it != quick_list.end()) {
		cout << *it++ << endl;
	}

	int eid;
	eid = quick_list.front();
	quick_list.pop_front();
	printf("pop front eid=%d\n", eid);

	cout << "after:" << endl;
	it = quick_list.begin();
	while (it != quick_list.end()) {
		cout << *it++ << endl;
	}


	printf("-------- deque below --------\n");

#define INTDEQ	deque<int>
	INTDEQ	quick_deq;
	quick_deq.push_back(3);
	quick_deq.push_back(6);
	quick_deq.push_back(9);
	for (int i=0; i<quick_deq.size(); i++) {
		cout << i << ":" << quick_deq[i] << endl;
	}
	eid = quick_deq.front();
	cout << "front eid : " << eid << endl;
	quick_deq.pop_front();

	for (int i=0; i<quick_deq.size(); i++) {
		cout << i << ":" << quick_deq[i] << endl;
	}

	quick_deq.push_back(11);
	quick_deq.push_back(13);
	quick_deq.push_back(15);

	cout << "before erase:"<< endl;
	for (int i=0; i<quick_deq.size(); i++) {
		cout << i << ":" << quick_deq[i] << endl;
	}


	// TODO need to match several pair, and erase more than 1 pair!
	// erase 9, 13
	INTDEQ::iterator it1;
	INTDEQ::iterator it2;
	for (it1 = quick_deq.begin(); it1!=quick_deq.end(); it1++) {
		for (it2=it1+1;  it2!=quick_deq.end(); it2++) {
			if (*it1==9 && *it2==13) {  // hard code pair
				quick_deq.erase(it2);
				quick_deq.erase(it1);
				goto outter;
			}
		}
	}
	outter:	


	// quick_deq.erase(6);
	// quick_deq.erase(3);
	cout << "after erase 6, 3:"<< endl;
	for (int i=0; i<quick_deq.size(); i++) {
		cout << i << ":" << quick_deq[i] << endl;
	}

	return 0;
}

typedef struct quick_struct {
	int eid;
	int rating;
	time_t start_time;
} quick_t;

#define QUICKLIST	deque<int>


// peter: {1, 5} will break the code!   (it1 != qlist.end()
// note: change to it1 < qlist.end() works!!
// {1,5}, {2,4}  will skip {2,4},  checking 3,4
// erase {1, 2} will make it1=1
int test16(int argc, char *argv[]) 
{
	QUICKLIST	qlist;
	quick_t quick;

	for (int i=1; i<=6; i++) {
		qlist.push_back(i); 
	}

	QUICKLIST::iterator it1, it2, ittemp;


#define TAG_SIZE	3
	int tag[TAG_SIZE][2] = {
		{ 4, 3 }
, 		{ 2, 5 }
,		{ 2, 5 }
//		{ 1, 5 }
//		{ 1, 2 }	// OK1
//		{ 1, 3 }	// OK1	OK2(4,5)
//		{ 1, 4 }	// OK1
//		{ 2, 4 }	// OK1 OK2(1,3)
//		{ 2, 5 }	// OK1
//,		{ 4, 5 }	// OK1	OK2(1,3)
//,		{ 3, 5 }
//		{ 1, 2 }
//,		{ 5, 6 }	// OK again?
	};

	// 
	for (it1=qlist.begin(); it1 < qlist.end(); it1++)  {
		if (*it1==0) { continue; }
		for (it2=it1+1; it2 < qlist.end(); it2++) {
			if (*it1==0) { break; }  // safety
			if (*it2==0) { continue; }
			printf("--- Checking: %d, %d\n", *it1, *it2);

			for (int i=0; i<TAG_SIZE; i++) {
				if ((*it1)==tag[i][0] && (*it2)==tag[i][1]) {
					int flag = 0;
					printf("Erase %d %d as zero\n", *it1, *it2);
					*it1 = 0;
					*it2 = 0;
					// order is important, erase it2 first
					// it2 = qlist.erase(it2);  // erase return next iter
					// it1 = qlist.erase(it1);
					// printf("*** after erase %d %d\n", *it1, *it2);
					break;
				}
			}
		}  // end for it2
		
		// {}
	}  // end for it1

	// erase those zero objects
	for (it1=qlist.begin(); it1<qlist.end(); ) {
		if (*it1==0) {
			it1 = qlist.erase(it1);  // no need ++
		} else {
			it1++;
		}
	}

after_erase:
	printf("----- AFTER erase -----\n");
	for (it1=qlist.begin(); it1!=qlist.end(); it1++) {
		cout << (*it1) << ", ";
	}
	cout << endl;

	return 0;
}

// elo rating 2 to the power X
int test17(int argc, char *argv[]) 
{
	double x, y;
	double result;

	x = 2;
	y = 5;
	result = pow(x,  y);
	printf("result = %lf\n", result);


	double rating1 = 1613;
	double rating2 = 1609;
	double kfactor = 32;

	printf("Before rating1,2 = %lf  %lf\n", rating1, rating2);
	
	double q1, q2, e1, e2;

	q1 = pow(10.0, rating1 / 400.0); // 10 ^ (...)
	q2 = pow(10.0, rating2 / 400.0);

	double base = q1 + q2;
#define TOO_SMALL	(0.00000000001f)
#define NOT_TOO_SMALL	(0.00000001f); // how small is small but not too small ?
	if (base <=TOO_SMALL && base>=-TOO_SMALL) {
		ERROR_PRINT(-7, "elo_div_zero");
		base = 0.00000001f; // how small is small but not too small ?
	}
	e1 = q1 / base;
	e2 = 1.0 - e1; // q2 / base;
	
	int win = 1;
	double diff = 0.0;

	if (win == 1) {
	 	diff = kfactor * (1.0 - e1);
		rating1 = rating1 + kfactor * (1.0 - e1);
		rating2 = rating2 + kfactor * (0.0 - e2);
	} else {
	 	diff = kfactor * (1.0 - e2);
		rating1 = rating1 + kfactor * (0.0 - e1);
		rating2 = rating2 + kfactor * (1.0 - e2);
	}
	printf("After win rating1,2,diff = %lf  %lf  %lf\n", rating1, rating2
	, diff);


	char out[300];

	sprintf(out, "%.5s  |%5.2lf|", "123456789", 12.345);
	printf("out=[%s]\n", out);
	return 0;
}

// unicode, wchar
// chinese: U+4E00..U+9FFF
// ref: http://blog.csdn.net/wallaceli1981/article/details/6116738
// shell: locale -a    // show all available locale
int test18(int argc, char *argv[]) 
{
	printf("setlocale ret: %s\n", setlocale(LC_ALL, "zh_CN.UTF-8"));
	cout << "111" << endl;
	locale loc; // ("chs");
	cout << "current locale: " << loc.name() << endl;
	try {
		locale lc("zh_CN.UTF-8");
		cout << "new locale: " << lc.name() << endl;
		wcout.imbue(lc);
	}
	catch (exception& e) {
		cerr << "Error: " << e.what() << endl;
		cerr << "Type:" << typeid(e).name() << endl;
	}

	wchar_t wStr[] = L"中文";
	wcout << "Chinese: " << wStr << endl;
	return 0;
}


// return YYMMDDhhiissxx
// where xx range from 00-99 (if overflow, return 99)
long ptimestamp()
{
	static long last_ts = 0;
	long pts;
	time_t now;
	struct tm tt;
	
	now = time(NULL);
	localtime_r(&now, &tt);
	pts = (long)(tt.tm_year-100) 
					* 1000000000000L
	+ (long)(tt.tm_mon+1)	* 10000000000L
	+ (long)tt.tm_mday 	* 100000000L
	+ (long)tt.tm_hour 	* 1000000L
	+ (long)tt.tm_min 	* 10000L
	+ (long)tt.tm_sec 	* 100L;
	
	if (pts > last_ts) {
		last_ts = pts;
	} else {
		last_ts++;  // eventually after the burst, now will > last_ts
		pts = last_ts;

		// peter: this is wrong, got duplicate ts
		/** old buggy way: @see test19  will hv duplicate ts
		if ((last_ts % 100) >= 99) {
			pts = last_ts;
		} else {
			last_ts ++;
			pts = last_ts;
		}
		**/
	}
	
	return pts;
}


int test19(int argc, char *argv[]) 
{
	struct tm tt;
	time_t now;
	int max = 120; // more than 99 for testing
	int max2 = 150;
	long ts_list[max2+1];

	now = time(NULL);
	localtime_r(&now, &tt);
	printf("YY[%2d] MM[%02d] DD[%02d]  hh[%02d] ii[%02d] ss[%02d]\n"
	, tt.tm_year, tt.tm_mon+1, tt.tm_mday, tt.tm_hour, tt.tm_min
	, tt.tm_sec);

	// do not print while generating, print is too slow
	for (int i=0; i<max; i++) {
		ts_list[i] = ptimestamp();
	}
	sleep(2);  // try sleep 1 and 2
	// sleep over 1 second let the burst over
	for (int i=max; i<max2; i++) {
		ts_list[i] = ptimestamp();
	}

	// print the list after generation
	for (int i=0; i<max; i++) {
		printf("pts = %ld\n", ts_list[i]);
	}
	printf("----- after sleep\n");
	for (int i=max; i<max2; i++) {
		printf("pts = %ld\n", ts_list[i]);
	}

	// more than 99, to ensure the xx overflow
	/**
	for (int i=0; i<120; i++) {
		printf("pts = %ld\n", ptimestamp());
		for (int j=0; j<3000000; j++) {
			double v = sqrt((j + 100) * (i+2));
			if (v < 0.000001) {
				printf("----------------\n");
			}
		}
	}
	**/
	return 0;
}

int test20(int argc, char *argv[]) 
{
	time_t now = time(NULL);
	char buff[100];

	ctime_r(&now, buff);

	printf("now=%ld  ctime = %s\n", now, buff);
	return 0;
}

// test yesterday
int test21(int argc, char *argv[])
{
// get yesterday 23:59:59
	time_t tt = time(NULL);
	time_t yesterday;
	struct tm timestruct;

	// second become m_day
	localtime_r(&tt, &timestruct);

	timestruct.tm_sec = 0;
	timestruct.tm_min = 0;
	timestruct.tm_hour = 0;

	yesterday = mktime(&timestruct);

	printf("today 00:00:00 = %ld\n", yesterday);
	printf("yesterday 23:59:59 = %ld\n", yesterday-1);

	yesterday --;
	localtime_r(&yesterday, &timestruct);
	printf("yes str: %s\n", asctime(&timestruct));

	return 0;
}


time_t sqldate_to_time(const char *sqldate)
{
	int ret;
	struct tm tm;
	time_t now = time(NULL);
	bzero(&tm, sizeof(tm));
	localtime_r(&now, &tm);

	ret = sscanf(sqldate, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon
	, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
	if (ret != 6) {
		BUG_PRINT(-5, "sqldate_to_time:sscanf %d", ret);
		return -1;
	}

	tm.tm_year -= 1900;
	tm.tm_mon --;  // base 0  (Jan=0)
	tm.tm_wday = 0;  // reset to zero, this maybe dangerous
	tm.tm_yday = 0;	// reset to zero

	return mktime(&tm);
}

// SQL datetime to time_t
// 2014-03-12 11:30:20 -> convert to unix time
int test22(int argc, char *argv[])
{
	int ret;
	struct tm tm;
	time_t now = time(NULL);
	time_t t;

	// char buf[100] = "2014-03-12 11:30:20";
	char buf[100] = "2014-03-13 11:32:20";


	t = sqldate_to_time(buf);
	localtime_r(&t, &tm);

	printf("asctime: %s\n", asctime(&tm));

	printf("now=%ld  t=%ld\n", now, t);
	return 0;
}


// input: max, str, sep(separator)
// output: tok (assume caller alloc > [max] char)
// @return pointer to str after the token + separator,  NULL if no more
// e.g. str = "s 1201;n;s 2201;n;s 1202;n;"
const char * str_token(char *tok, int max, const char * str
, const char * sep)
{
	const char * ptr;
	int len;
	ptr = strstr(str, sep);
	if (ptr == NULL) {
		if (str==NULL) {
			tok[0] = '\0';
		} else {
			strcpy(tok, str);
		}
		return NULL;
	}
	len = ptr - str + 1;
	if (len >= max) { // >= is safer  caller: char tok[max] OK
		ERROR_PRINT(-2, "str_token:overflow len=%d sep=%s str=%s "
		, len, sep, str);
		return NULL;
	}
	snprintf(tok, len, "%s", str);
	ptr = ptr + strlen(sep);
	if (*ptr == '\0') { // last one
		return NULL;
	}
	return ptr;
}


int test23(int argc, char *argv[])
{
	int ret;
	// const char *cmd_ptr = "s 1201;n;s 2201;n;s 1202;n;s 2202";
	const char *cmd_ptr111 = "s 1201;n;s 2201;n;s 1202;n;s 2202";
	const char *cmd_ptr222 = "s 1201;n;s 2201;n;s 1202;n;s 2202;";
	const char *cmd_ptr333 = "s 1201;n;s 2201;n;s 1202;n;s 2202;;;;";
	const char *ptr, *old_ptr;
	char cmd[200];


	printf("----- cmd_ptr111 = %s\n", cmd_ptr111);
	ptr = cmd_ptr111;
	for (int i=0; i<20 && ptr!=NULL; i++) {
		ptr = str_token(cmd, 200, ptr, ";");
		printf("cmd=[%s]  ptr=[%s]\n", cmd, ptr);
	}

	printf("----- cmd_ptr222 = %s\n", cmd_ptr222);
	ptr = cmd_ptr222;
	for (int i=0; i<20 && ptr!=NULL; i++) {
		ptr = str_token(cmd, 200, ptr, ";");
		printf("cmd=[%s]  ptr=[%s]\n", cmd, ptr);
	}

	printf("----- cmd_ptr333 = %s\n", cmd_ptr333);
	ptr = cmd_ptr333;
	for (int i=0; i<20 && ptr!=NULL; i++) {
		ptr = str_token(cmd, 200, ptr, ";");
		printf("cmd=[%s]  ptr=[%s]\n", cmd, ptr);
	}

		/***
		old_ptr = ptr;
		ptr = strstr(old_ptr, ";");
		if (ptr == NULL) {
			printf("no more\n");
			return 0;
		}
		snprintf(cmd, (ptr-old_ptr+1), "%s", old_ptr);
		printf("cmd=[%s]  ptr = [%s]  n=%zu \n", cmd, ptr, (ptr-old_ptr));
		ptr++; // must hv
		***/

	return 0;
}


typedef int (*testcase_t) (int, char*[]); //  testcase_t;

// #define TESTCASE_MAX	9
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
, 	test10
, 	test11
,	test12	// color print
, 	test13	// vector , bzero clean up?
, 	test14	// tv_sec, tv_usec
, 	test15	// list pop front
, 	test16	// 2d for loop erase deque
,	test17	// elo rating
,	test18	// unicode test
,	test19	// time format test
,	test20	// ctime 
, 	test21	// yesterday
,	test22	// sql datetime to time_t
,	test23	// replay cmd split
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
	// TESTCASE_MAX) {
	if (testcase < 0 || testcase >= testmax) { 
		printf("ERR invalid testcase %d\n", testcase);
		return -2;
	}
	ret = (*test_list[testcase])(argc, argv);
	
	printf("RET %d\n", ret);
	
	return ret;
}


int main(int argc, char *argv[])
{
	test_selector(argc, argv);

	/**
	testcase_t ttt;
	ttt = test2;
	ttt(argc, argv);
	**/
	return 0;
}

