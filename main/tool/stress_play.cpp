
#include <string>

using namespace std;

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
// socket related
#include <netdb.h> 	// hostname lookup @see prepare_addr
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

// user defined header
#include "fatal.h"
}


#define BUFFER_SIZE	2000

#define DEBUG 	0

///////// UTILITY START [ /////////

// domain: can be domain name or IP address
struct sockaddr_in prepare_addr(const char *hostname, int port)
{
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family    	= AF_INET;
	sin.sin_port      	= htons(port);
    int ip = inet_addr(hostname);
    if (ip==-1)
    {
        struct hostent *hent;
        hent = gethostbyname(hostname);
        if (hent==NULL) {
            // printf("ERROR prepare_addr: gethostbyname fail\n");
            ip = 0xffffffff; // 8 x f
        } else {
            ip = *(int *)(hent->h_addr_list[0]);
        }
        printf("DEBUG prepare_addr: resolve ip=%d.%d.%d.%d\n", ip & 0xff
               , (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff);
    }
	sin.sin_addr.s_addr = ip;

	// note: assume sockaddr, sockaddr_in same len
	return sin;
}


int sock_write(int sock, const char * fmt, ...) 
{
	int n;
	char buffer[BUFFER_SIZE + 1]; 
    va_list argptr;
    va_start(argptr, fmt);
    n = vsnprintf(buffer, BUFFER_SIZE, fmt, argptr);
    va_end(argptr);
//	buffer[n] = last;
//	buffer[n+1] = 0;
//	strcat(buffer, "\n");

	int offset = 0;
	int len = strlen(buffer);
	int ret = 0;
	int count = 0;
	printf("SEND(%3d): %s", len, buffer);
	while (offset < len) {
		ret = write(sock, buffer+offset, (len-offset));
		if (ret < 0)  {
			printf("BUG write ret %d\n", ret);
			return -5;
		}
		offset += ret;
	}
	
	if (count > 1) {
		printf("DEBUG sock_write split write into %d\n", count);
	}
	return len;
}

// like fgets, read from fd up to \n, the \n is also copied to the
// buf.   buf is null-terminated
// return the size read,  should be <= buffer_size-1
int readenter(int fd, char *buf, int buffer_size)
{
	char oneb[1];
	int ret;
	int size;

	size = 0;
	for (int i=0; i<buffer_size-1; i++) {
		ret = read(fd, oneb, 1);
		if (ret <= 0) {
			// XXX we may need to report error
			// return -1 ?
			break;
		}
		// printf("readenter: %c\n", oneb[0]);
		buf[size] = oneb[0];
		size ++;
		if (oneb[0]=='\n') break;
	}
	buf[size] = 0; // null-terminate it
	if (size == 0) {
		printf("BUG : readenter size=0\n");
		errno = -1;
		return -1;
	}
	return size;
}

int writestr(int sock, const char *buf)
{
	int len, ret;
	len = strlen(buf);
	ret = write(sock, buf, len);
	if (ret != len) {
		ERROR_NEG_PRINT(ret, "len=%d ret=%d %s", len, ret, buf);
		return -1;
	}
	return 0;
}


///////// UTILITY END ] /////////

int MAX_THREAD = 1; // init. in main

char SERVER_IP[100] = "127.0.0.1"; // is it ok?


/**
common resource tight error (full cap):  on Mac OSX 10.8.5
ERROR -15 quick_error:  stress_play.cpp:169: errno 32  2014-02-08 22:19
bugged: Broken pipe

BUGBUG -1 connect 316 stress_play.cpp:125: errno 54  2014-02-08 22:19

ERROR -25 no_room_error stress_play.cpp:176: errno 35  2014-02-08 22:15
bugged: Resource temporarily unavailable

TODO need to test on Linux (dell 5460)
note: ulimit may help!
 */

void * client_play(void *ptr)
{
	int id = (int)(size_t)(ptr);
	struct sockaddr_in addr;
	int sock;
	int ret, len;
	char str[BUFFER_SIZE+1];
	char out[BUFFER_SIZE+1];
	char cmd[100];

	int side_id;
	int current_side;

	addr = prepare_addr(SERVER_IP, 7710);

	if (addr.sin_addr.s_addr == 0xffffffff) {
		FATAL_EXIT(-1, "prepare_addr: %s", SERVER_IP);
	}

	sock = socket( addr.sin_family, SOCK_STREAM, 0);
	FATAL_NEG_EXIT(sock, "socket %d", id);
	// errno = 24 (TOO MANY open files)
	ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (ret) {
		BUG_PRINT(ret, "connect %d", id);
		return (void *)(size_t) -15;
	}

	if (DEBUG) printf("START client_play %d  sock=%d\n", id, sock);

	// TODO do not use fprintf,  use native "write" and check EAGAIN ?


	struct timeval timeout;
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;

	if (setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout,
	sizeof(timeout)) < 0) {
		perror("setsockopt RCVTIMEO failed\n");
	}

	if (setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, &timeout,
	sizeof(timeout)) < 0) {
		perror("setsockopt SNDTIMEO failed\n");
	}


	// if (id == 5) id = 550;  // for debug, REMOVE later
	len = sprintf(str, "log st%d 1\n", id);  // 2 commands
	if (writestr(sock, str) < 0) goto cleanup;

	ret = readenter(sock, out, BUFFER_SIZE-1);
//	printf("first readenter %d %s\n", ret, out);
	ERROR_NEG_PRINT(ret, "read_log [%s]", out);
	ret = -1; sscanf(out, "%s %d", cmd, &ret);
	if (ret < 0) {
		goto cleanup;
	}
	printf("OK login : %s", out);

	if (writestr(sock, "quick\n") < 0) goto cleanup;

	ret = readenter(sock, out, BUFFER_SIZE-1);
	ERROR_NEG_PRINT(ret, "read_quick");
	if (ret < 0) goto cleanup;
//	printf("read quick: %s", out);

	ret = -15; sscanf(out, "%s %d", cmd, &ret);
	BUG_NEG_PRINT(ret, "quick_error: %s", out);
	if (ret < 0) goto cleanup;
	
	ret = readenter(sock, out, BUFFER_SIZE-1);
	ERROR_NEG_PRINT(ret, "read_room");
	if (ret <= 0) goto cleanup;
//	printf("read room : %s", out);

	ret = -35; cmd[0]=0; sscanf(out, "%s %d", cmd, &ret);
	if (strstr(cmd, "quick")==cmd) {
		printf("NORMAL: quick output: %s", out);
		ret = 0;
		goto cleanup;
	}
	ERROR_NEG_PRINT(ret, "room_error: %s", out);
	if (ret < 0) goto cleanup;

	
	if (writestr(sock, "ginfo\n") < 0) goto cleanup;

	ret = readenter(sock, out, BUFFER_SIZE-1);
	ERROR_NEG_PRINT(ret, "read_ginfo");
	if (ret <= 0) goto cleanup;
	ret = -40; cmd[0]=0; sscanf(out, "%s %d", cmd, &ret);
	if (strstr(cmd, "ginfo")!=cmd) {
		BUG_PRINT(ret, "expect ginfo output: out\n");
		goto cleanup;
	}


	ret = -45; sscanf(out, "%s %d", cmd, &ret);
	ERROR_NEG_PRINT(ret, "ginfo_error");
	if (ret < 0) goto cleanup;

	side_id = ret;
	current_side = 1;

	for (int i = 0;i<5;i++) {

		if (side_id == current_side) {
			if (writestr(sock, "s 0\n") < 0) goto cleanup;
			if (writestr(sock, "n\n") < 0) goto cleanup;
		}

		ret = readenter(sock, out, BUFFER_SIZE-1);
		ERROR_NEG_PRINT(ret, "play1_s_0");
		if (ret < 0) goto cleanup;

		ret = readenter(sock, out, BUFFER_SIZE-1);
		ERROR_NEG_PRINT(ret, "play2_n");
		if (ret < 0) goto cleanup;

		current_side = 3 - current_side;
	}

	if (side_id == 1) { // room master lose the game
		ret = write(sock, "fold\n", 5); 
		ret = (ret == 5) ? 0 : -55;
		ERROR_NEG_PRINT(ret, "write_fold");
		if (ret < 0) goto cleanup;
	}

	ret = readenter(sock, out, BUFFER_SIZE-1);
	ERROR_NEG_PRINT(ret, "win_err1");
	if (ret < 0) goto cleanup;

	ret = -55; sscanf(out, "%s %d", cmd, &ret);
	ERROR_NEG_PRINT(ret, "win_err2");
	if (ret < 0) goto cleanup;
		
	printf("OK %s", out);
	errno = 0;
	ret = 0;


cleanup:
	
	if (ret < 0 || errno!=0) perror("bugged");
	close(sock);

	if (DEBUG) printf("END client_play %d\n", id);

	return (void*)(size_t)ret;  // make sure the last ret is ready
}


/**
 * run registeration for N uses
 * stress_reg 10
 * reg st1 1\njob 1\n
 * reg st2 1\njob 1\n
 * ...
 * reg st10 1\njob 1\n
 */
int main(int argc, char *argv[])
{
	int ret;
	time_t start_time, end_time;
	if (argc > 1) { MAX_THREAD = atoi(argv[1]); }
	if (argc > 2) { sprintf(SERVER_IP, "%.99s", argv[2]); }

	printf("stress_play [%d] [%s]\n", MAX_THREAD, SERVER_IP);

	if (MAX_THREAD <= 0) {
		printf("Usage: stress_play [max_thread] [SERVER_IP]\n");
		return 0;
	}

	// some signal handler to avoid accidentally quit
	signal(SIGPIPE, SIG_IGN); // get EPIPE instead

	start_time = time(NULL);  // record the time, @see end_time

	pthread_t client_thread[MAX_THREAD];

	for (int i=0;i<MAX_THREAD; i++) {
		ret = pthread_create(client_thread+i, NULL, client_play
			, (void*)(size_t)(i+1));
		FATAL_EXIT(ret, "pthread_create %d", i);
	}


	int error_count = 0;
	int code;
	for (int i=0; i<MAX_THREAD; i++) {
		void *ptr;
		ret = pthread_join(client_thread[i], &ptr);
		FATAL_EXIT(ret, "pthread_join %d", i);
		code = (int)(size_t)ptr;
		if (code < 0) {
			error_count ++;
		}
		// printf("END thread %d  ret=%d\n", i, code);
	}

	end_time = time(NULL); // @see start_time

	printf("SUMMARY error_count=%d  time(s)=%ld\n"
		, error_count, end_time-start_time);
	return 0;
}

