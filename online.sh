#!/bin/bash

## keep this for testing:
## the following will screw up, because it mess up \n (OUT does not hv \n)
## OUT=`(echo @online; sleep 0.1) | telnet 0.0.0.0 7710`
## this is ok, but need /tmp/out.txt (not good, we need to remove)
## (echo @online; sleep 0.1) | telnet 0.0.0.0 7710 > /tmp/out.txt

## sample: OUTPUT (without \n)
## Trying 0.0.0.0... Connected to localhost. Escape character is '^]'. @online 0 u=[] a=[] @online cid=0 127.0.0.1:61525 not_login total user=0 g_num_connect=1 g_free_connect=1

## solution 1:
## sed '1,3d' == skip 1 to 3 line (Trying 0.0.00...)
## now, we have @online 3  and the rest of lines
## sed '2,$d' == skip line 2 to end of file ($)
## now we have one line: @online 3
## COUNT=`(echo @online; sleep 0.1) | telnet 0.0.0.0 7710 |  sed '1,3d' | sed '2,$d' | sed 's/@online //g' `

## solution 2:  (simpler, we just need to use sed -n and 'p' command to print
## specific line with @online [0-9]
COUNT=`(echo @online; sleep 0.1) | telnet 0.0.0.0 7710 |  sed -n '/@online [0-9]/p' | sed 's/@online //g' `

## if something go wrong, $COUNT will be empty string, make it -1 for SQL
if [ -z $COUNT ]; then
	COUNT=-1
fi

echo online count = [$COUNT]

## TODO : use SQL="INSERT ..."
SQL="INSERT INTO evil_online VALUES (NOW(), $COUNT)"

echo SQL = $SQL

## TODO: 
echo $SQL | mysql -uevil -pNew2xin evil


## note: we need a table:
## CREATE TABLE evil_online (
##		odate DATETIME NOT NULL PRIMARY KEY  -- never run twice in a second
## ,	count INT NOT NULL -- keep this signed
## );
## 

## for crontab -e :
## run for every hour :
## 5 * * * * /home/mac/Documents/workspace/evil/evil_server/online.sh
## 
## or if we need every 10 minutes:
## 0,10,20,30,40,50 * * * * /home/mac/Documents/workspace/evil/evil_server/online.sh

