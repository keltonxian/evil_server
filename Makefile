
default:	platform all


## n=nio.cpp (main server)   db=db_conn.cpp (db testcase)
## cli=client  		ttt=testing program 	gchat=test global chat
## all:	 nio cli
## for testing: ttt design dbio stress_reg stress_play
all:	dbio design nio cli match robot mis 
## ttt nio cli design dbio mis


platform: 
	@echo Platform: $(OS)
	@echo FLAGS_WATCH: $(FLAGS_WATCH)
	@echo INC_MYSQL: $(INC_MYSQL)
	@echo LIB_MYSQL: $(LIB_MYSQL)
	@echo LIB_LUA: $(LIB_LUA)
	@echo FLAGS_LUA: $(FLAGS_LUA)

## DEBUG_FLAG:=-g 
DEBUG_FLAG:=-g

OS:=$(shell uname -s)
# OS can be one of: Darwin Linux
# Darwin is the OS name for MacOSX
# PLAT=Linux

## $(info OS=$(OS))

PATH_MAIN=main
PATH_HEADER=$(PATH_MAIN)/header
PATH_BASE=$(PATH_MAIN)/base
PATH_DB=$(PATH_MAIN)/db
PATH_GAME=$(PATH_MAIN)/game
PATH_TOOL=$(PATH_MAIN)/tool
PATH_CLIENT=$(PATH_MAIN)/client
PATH_RES=$(PATH_MAIN)/res
PATH_RES_TXT=$(PATH_RES)/txt

INC_HEADER=-I$(PATH_HEADER)
## note: copy mysql-connector-c-6.1.1-linux-glibc2.5-x86_64/include/
## to the following (recursively)
INC_MYSQL=`mysql_config --cflags` -I/usr/local/mysql-connector-c++/include

## if it is not Linux or Mac, it will be bug!
FLAGS_WATCH:=BUG
LIB_MYSQL:=BUG
LIB_LUA:=BUG
FLAGS_LUA:=BUG

ifeq "$(OS)" "Linux"
FLAGS_WATCH:=-DHAVE_SYS_POLL_H=1 -DHAVE_POLL=1
## Ubuntu 12: libmysqlclient.so.18.1.0 
## CentOS 6.2: /usr/lib64/mysql/libmysqlclient.so.16  (5.1.x)
## LIB_MYSQL:=libmysqlclient.so.18.1.0
#LIB_MYSQL:=/usr/lib64/mysql/libmysqlclient.so.16
LIB_MYSQL:=`mysql_config --libs`
LIB_LUA=luajit/libluajit_linux.a
FLAGS_LUA:=
endif


ifeq "$(OS)" "Darwin"
## FLAGS_WATCH:=-DHAVE_SYS_EVENT_H=1 -DHAVE_KQUEUE=1
FLAGS_WATCH:=-DHAVE_SYS_POLL_H=1 -DHAVE_POLL=1
LIB_MYSQL:=`mysql_config --libs`
LIB_LUA=luajit/libluajit.a
FLAGS_LUA:=-pagezero_size 10000 -image_base 100000000
## ref: http://luajit.org/install.html
## -pagezero_size 10000 -image_base 100000000
endif


## ubuntu note:
## -- Upgrade glibc 2.5 
## apt-get upgrade glibc
## -- Install documentation: man pthread_create
## apt-get install glibc-doc
## apt-get install manpages-posix-dev

clean:	
	rm -f $(PATH_MAIN)/nio 
	rm -rf $(PATH_MAIN)/nio.dSYM/
	rm -f $(PATH_DB)/db $(PATH_DB)/design $(PATH_DB)/dbio
	rm -rf $(PATH_DB)/db.dSYM/ $(PATH_DB)/design.dSYM/ $(PATH_DB)/dbio.dSYM/
	rm -f $(PATH_BASE)/fdwatch.o
	rm -f $(PATH_GAME)/gchat $(PATH_GAME)/gchat_queue.o
	rm -f $(PATH_GAME)/mis $(PATH_GAME)/mission.o
	rm -f $(PATH_GAME)/match $(PATH_GAME)/match.o
	rm -rf $(PATH_GAME)/match.dSYM/
	rm -rf $(PATH_GAME)/achi
	rm -f $(PATH_TOOL)/robot
	rm -rf $(PATH_TOOL)/robot.dSYM/
	rm -f $(PATH_CLIENT)/cli
	rm -rf $(PATH_CLIENT)/cli.dSYM/

## gcc -DHAVE_SYS_EVENT_H=1 -DHAVE_KQUEUE=1 -c fdwatch.c
## peter: use gcc, do not use g++ for pure C implementation
fdwatch.o:	$(PATH_BASE)/fdwatch.c
	gcc -g $(FLAGS_WATCH) -o $(PATH_BASE)/fdwatch.o -c $(PATH_BASE)/fdwatch.c

mission.o:	$(PATH_GAME)/mission.cpp
	g++ $(DEBUG_FLAG) $(INC_HEADER) -o $(PATH_GAME)/mission.o -c $(PATH_GAME)/mission.cpp

match.o:	$(PATH_GAME)/match.cpp
	g++ $(DEBUG_FLAG) $(INC_HEADER) -o $(PATH_GAME)/match.o -c $(PATH_GAME)/match.cpp

## gchat_queue.o:	gchat_queue.c
## 	gcc -std=c99 -c gchat_queue.c


## note: for old Macbook air, we may need strnlen.o (used by mysql lib)

## nio include database
## Linux: libmysqlclient.so.18.1.0
## Mac(Darwin): libmysqlclient.a
nio:	$(PATH_MAIN)/nio.cpp $(PATH_DB)/dbio.cpp $(PATH_DB)/db_design.cpp $(PATH_GAME)/gchat_queue.cpp fdwatch.o mission.o match.o
	g++ -Wall $(DEBUG_FLAG) $(FLAGS_LUA) -o $(PATH_MAIN)/nio \
		$(INC_HEADER) $(INC_MYSQL) \
		$(PATH_DB)/dbio.cpp $(PATH_MAIN)/nio.cpp $(PATH_GAME)/gchat_queue.cpp \
		$(PATH_BASE)/fdwatch.o $(PATH_GAME)/mission.o $(PATH_GAME)/match.o \
		$(PATH_DB)/db_design.cpp \
		$(LIB_LUA) $(LIB_MYSQL) -ldl -lpthread


## db standalone testing
## Linux: libmysqlclient.so.18.1.0
db:	$(PATH_DB)/db_conn.cpp
	g++ -g -Wall -o $(PATH_DB)/db -DTTT=1 -Wall $(INC_HEADER) $(INC_MYSQL) \
	$(PATH_DB)/db_conn.cpp $(LIB_MYSQL) -ldl -lpthread

design:	$(PATH_DB)/db_design.cpp
	g++ -g -Wall -o $(PATH_DB)/design -DTTT=1 -Wall $(INC_HEADER) $(INC_MYSQL) \
	$(PATH_DB)/db_design.cpp $(LIB_MYSQL) -ldl -lpthread
	
dbio:	$(PATH_DB)/dbio.cpp 
	g++ -g -Wall -DTTT -o $(PATH_DB)/dbio $(INC_HEADER) $(INC_MYSQL) \
	$(PATH_DB)/dbio.cpp $(LIB_MYSQL) -ldl -lpthread

## for testing only
ttt:	$(PATH_TOOL)/ttt.cpp
	g++ -o $(PATH_TOOL)/ttt $(PATH_TOOL)/ttt.cpp


## demo text client
cli:	$(PATH_CLIENT)/cli.cpp $(LIB_LUA)
	g++ -Wall -g $(FLAGS_LUA) -o $(PATH_CLIENT)/cli $(INC_HEADER) \
	$(PATH_CLIENT)/cli.cpp $(LIB_LUA) -ldl -lpthread

## osx: luajit need flags:  -pagezero_size 10000
##	g++ -pagezero_size 10000 -o cli cli.cpp $(LIB_LUA)


gchat:	$(PATH_GAME)/gchat_queue.cpp
	g++ -Wall -DTTT -o $(PATH_GAME)/gchat $(PATH_GAME)/gchat_queue.cpp


stress_reg:		$(PATH_TOOL)/stress_reg.cpp
	g++ -g -Wall -o $(PATH_TOOL)/stress_reg $(INC_HEADER) $(PATH_TOOL)/stress_reg.cpp -lpthread

stress_play:		$(PATH_TOOL)/stress_play.cpp
	g++ -g -Wall -o $(PATH_TOOL)/stress_play $(INC_HEADER) $(PATH_TOOL)/stress_play.cpp -lpthread

mis:	$(PATH_GAME)/mission.cpp
	g++ -Wall -DTTT -o $(PATH_GAME)/mis $(INC_HEADER) $(PATH_GAME)/mission.cpp

## achi:	achievement.cpp evil.h
## 	g++ -Wall -DTTT -o achi achievement.cpp

match:	$(PATH_GAME)/match.cpp 
	g++ -Wall -DTTT -o $(PATH_GAME)/match $(INC_HEADER) $(PATH_GAME)/match.cpp 


robot:	$(PATH_TOOL)/robot.cpp 
	g++ -g -Wall -DTTT $(FLAGS_LUA) $(INC_HEADER) -o $(PATH_TOOL)/robot \
	$(PATH_TOOL)/robot.cpp $(LIB_LUA) -ldl -lpthread

gen_code:	$(PATH_GAME)/gen_code.cpp
	g++ -g -Wall -DTTT -o $(PATH_GAME)/gen_code $(INC_HEADER) $(INC_MYSQL) \
	$(PATH_GAME)/gen_code.cpp $(LIB_MYSQL) -ldl

.PHONY: all clean

