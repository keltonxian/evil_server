System Info:

MySQL Version:
Server version: 8.0.18 MySQL Community Server - GPL

-----------------------------------------------------

Install Environment:

Edit ~/.profile
export PATH=$PATH:/usr/local/mysql/bin
# mysql
alias mysql_start="sudo /usr/local/mysql/support-files/mysql.server start"
alias mysql_stop="sudo /usr/local/mysql/support-files/mysql.server stop"
alias mysql_restart="sudo /usr/local/mysql/support-files/mysql.server restart"
alias mysql_evil="/usr/local/mysql/bin/mysql --default-character-set=utf8 -uevil -p1 evil_base"

-----------------------------------------------------

Init Database:
$ mysql_start
$ mysql -uroot -p
mysql> CREATE USER 'evil'@'localhost' IDENTIFIED BY '1'; 
mysql> CREATE DATABASE evil_base;
mysql> CREATE DATABASE evil_design;
mysql> GRANT all on evil_base.* to 'evil'@'localhost';
mysql> GRANT all on evil_design.* to 'evil'@'localhost';
mysql> exit
$ mysql_evil < db-init.sql 
$ mysql_evil < db-design.sql 

-----------------------------------------------------

ERROR:
dyld: Library not loaded: @rpath/libmysqlclient.21.dylib
  Referenced from: /Users/kelton/Workspace/test/database/./test_connect
  Reason: image not found
Abort trap: 6

Edit .profile:
export DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:/usr/local/mysql/lib/

-----------------------------------------------------

$ mysql_config --libs
-L/usr/local/mysql/lib -lmysqlclient -lssl -lcrypto
$ mysql_config --cflags
-I/usr/local/mysql/include

About Makefile:
test_connect: test_connect.cpp
    g++  test_connect.cpp -o test_connect  `mysql_config --cflags --libs`

-----------------------------------------------------

Make:
$ make clean
$ make

Start:
$ mysql_start
$ ./nio > ~/Desktop/evil.log &

Stop:
$ ps awx|grep "./nio"
$ kill $pid

Check Log:
$ tail -f ~/Desktop/evil.log

-----------------------------------------------------
after server run:
cd to client
$ ./create_robot.sh
$ ./create_arena_robot.sh
