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


#define BUFFER_SIZE	1000

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
///////// UTILITY END ] /////////

int MAX_THREAD = 1; // init. in main

char SERVER_IP[100] = "127.0.0.1"; // is it ok?



void * client_reg(void *ptr)
{
	int id = (int)(size_t)(ptr);
	struct sockaddr_in addr;
	int sock;
	int ret, len;
	char str[500];
	char out[500];
	char cmd[100];

	// TODO: use write and read, do not use fp

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

	if (DEBUG) printf("START client_reg %d  sock=%d\n", id, sock);

	// TODO do not use fprintf,  use native "write" and check EAGAIN ?

	FILE *fp;
	fp = fdopen(sock, "r+");
	len = sprintf(str, "reg st%d 1\njob 1\n", id);  // 2 commands
	ret = fprintf(fp, "%s", str);
	// fflush(fp);
	ERROR_PRINT(len != ret, "fprintf len%d!=ret%d  id=%d", len, ret, id);

	// TODO : append error string into global
	ret = 0;
	for (int i=0; i<2; i++) {
		char *ptr;
		ret = -77;
		ptr = fgets(out, 499, fp);
		ERROR_PRINT(ptr==NULL, "client_reg:fgets_null%d", i);  // null0  err 54
		if (ptr == NULL) {
			break;
		}
		// printf("client_reg(%d,%d):%s", id, i, str);
		sscanf(out, "%s %d", cmd, &ret);
		ERROR_NEG_PRINT(ret, "client_error"); //  [%s]", out);
		if (ret < 0) {
			break;
		}
	}

	fclose(fp);
	close(sock);

	if (DEBUG) printf("END client_reg %d\n", id);

	return (void*)(size_t)ret;
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
	if (argc > 1) { MAX_THREAD = atoi(argv[1]); }
	if (argc > 2) { sprintf(SERVER_IP, "%.99s", argv[2]); }

	printf("stress_reg [%d] [%s]\n", MAX_THREAD, SERVER_IP);

	if (MAX_THREAD <= 0) {
		printf("Usage: stress_reg [max_thread] [SERVER_IP]\n");
		return 0;
	}

	signal(SIGPIPE, SIG_IGN); // get EPIPE instead

	pthread_t client_thread[MAX_THREAD];

	for (int i=0;i<MAX_THREAD; i++) {
		ret = pthread_create(client_thread+i, NULL, client_reg
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

	printf("SUMMARY error_count=%d\n", error_count);
	return 0;
}

