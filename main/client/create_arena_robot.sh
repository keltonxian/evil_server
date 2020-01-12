#!/bin/bash

STARTID=2000
TOTAL=200;

PWD=2015

IP=127.0.0.1


for ((c=1; c<=$TOTAL; c++))
do
	ROBOTID=`echo $STARTID+$c-1 | bc`
	# echo ROBOTID=$ROBOTID

	INFO=`head -n $c arena_robot.txt | tail -n 1`
	# echo INFO=$INFO

    # echo @addrobot robot$ROBOTID $PWD $INFO
    (echo @addrobot robot$ROBOTID $PWD $INFO; sleep 0.1;) | ./cli 99 $IP
    # (echo @addrobot robot$ROBOTID $PWD $INFO; sleep 0.1;) | telnet $IP 7710
    # (echo reg robot$ROBOTID $PWD; sleep 0.1; echo @scard 0 $DECK; sleep 0.1;) | telnet $IP 7710

done
