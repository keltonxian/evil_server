
#ifndef __FATAL_H__
#define __FATAL_H__

#include <time.h>
#include <errno.h>
#include <stdlib.h>

#define COLOR_TAG	1
// #undef COLOR_TAG

#ifdef COLOR_TAG
	#define FATAL_TAG	"\x1B[31m\x1B[4mFATAL "
	#define BUG_TAG		"\x1B[35m\x1B[4mBUGBUG "
	#define ERROR_TAG	"\x1B[31mERROR "
	#define WARN_TAG	"\x1B[33mWARN "
	#define DEBUG_TAG	"\x1B[36mDEBUG "
	#define INFO_TAG	"\x1B[34mINFO "
	#define END_TAG		"\x1B[0m\n"	// clean up color code
#else
	#define FATAL_TAG	"FATAL "
	#define BUG_TAG		"BUGBUG "
	#define ERROR_TAG	"ERROR "
	#define WARN_TAG	"WARN "
	#define DEBUG_TAG	"DEBUG "
	#define INFO_TAG	"INFO "
	#define END_TAG		"\n"
#endif

#define FATAL_EXIT(ret, ...)     do { if (0!=(ret)) { int __errno=(errno); printf(FATAL_TAG __FILE__ ":%d: ret %d errno %d ", __LINE__, (ret), __errno); printf(__VA_ARGS__); printf(END_TAG); fflush(stdout); exit(ret); }  } while(0)

#define FATAL_NEG_EXIT(ret, ...)     do { if ((ret) < 0) { int __errno=(errno); printf(FATAL_TAG __FILE__ ":%d: ret %d errno %d ", __LINE__, (ret), __errno); printf(__VA_ARGS__); printf(END_TAG); fflush(stdout); exit(ret); } } while(0)

// internal use: by ERROR_PRINT, BUG_PRINT etc.
#define __PRINT_ERRNO_TIME(ret, tag, ...)     do { { int __errno=(errno); char __tbuff[31]; time_t __t = time(NULL); struct tm __tm; localtime_r(&__t, &__tm); strftime(__tbuff, 30, "%F %R", &__tm); printf("%s%d ", (tag), ret); printf(__VA_ARGS__);  printf(" %s:%d: errno %d  %s", __FILE__, __LINE__, __errno, __tbuff); printf(END_TAG); fflush(stdout); } } while(0)

// this is related impossible bug, but not as fatal as FATAL case
#define BUG_PRINT(ret, ...)     do { if (0!=(ret)) { __PRINT_ERRNO_TIME((ret), BUG_TAG, __VA_ARGS__); } } while(0)

#define BUG_NEG_PRINT(ret, ...)  do { if ((ret) < 0) { __PRINT_ERRNO_TIME((ret), BUG_TAG, __VA_ARGS__); }} while(0)

#define BUG_RETURN(ret, ...)  do { if ((ret) != 0) { __PRINT_ERRNO_TIME((ret), BUG_TAG, __VA_ARGS__); return (ret); }} while(0)

#define BUG_NEG_RETURN(ret, ...)  do { if ((ret) < 0) { __PRINT_ERRNO_TIME((ret), BUG_TAG, __VA_ARGS__); return (ret); }} while(0)


#define ERROR_PRINT(ret, ...)     do { if (0!=(ret)) { __PRINT_ERRNO_TIME((ret), ERROR_TAG, __VA_ARGS__);} } while(0)

#define ERROR_NEG_PRINT(ret, ...)     do { if ((ret) < 0) { __PRINT_ERRNO_TIME((ret), ERROR_TAG, __VA_ARGS__);} } while(0)

#define ERROR_RETURN(ret, ...)     do { if (0!=(ret)) { __PRINT_ERRNO_TIME((ret), ERROR_TAG, __VA_ARGS__);  return(ret); } } while(0)

#define ERROR_NEG_RETURN(ret, ...)     do { if ((ret) < 0) { __PRINT_ERRNO_TIME((ret), ERROR_TAG, __VA_ARGS__);  return(ret); } } while(0)

// WARN, DEBUG, INFO, only print, do NOT return!!

#define WARN_PRINT(ret, ...)     do { if (0!=(ret)) { __PRINT_ERRNO_TIME((ret), WARN_TAG, __VA_ARGS__); } } while(0)

// DEBUG and INFO : do not check ret,   DEBUG no timestamp (too many)
#define DEBUG_PRINT(ret, ...)    do { printf(DEBUG_TAG); printf(__VA_ARGS__); printf( " %s:%d: ret %d ", __FILE__, __LINE__, (ret)); printf(END_TAG); fflush(stdout); } while(0)

#define INFO_PRINT(ret, ...) do { __PRINT_ERRNO_TIME((ret), INFO_TAG, __VA_ARGS__);} while(0)

#endif


