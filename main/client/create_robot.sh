#!/bin/bash

STARTID=1000
TOTAL=1000;

PWD=2015

IP=127.0.0.1


## register and save card start ##
for ((c=1; c<=$TOTAL; c++))
do
	ROBOTID=`echo $STARTID+$c-1 | bc`
	echo ROBOTID=$ROBOTID

	COUNT=`echo $c%20+1 | bc`
	echo COUNT=$COUNT

	DECK=`head -n $COUNT ai_deck.txt | tail -n 1`
	echo DECK=$DECK

    (echo reg robot$ROBOTID $PWD; sleep 0.1; echo @scard 0 $DECK; sleep 0.1;) | telnet $IP 7710

done
## register and save card end ##

## rename start ##
for ((c=1; c<=$TOTAL; c++))
do

	ROBOTID=`echo $STARTID+$c-1 | bc`
	echo ROBOTID=$ROBOTID

	NAME=`head -n $c name1000.txt | tail -n 1`
	echo NAME=$NAME

	(echo log robot$ROBOTID $PWD; sleep 0.1; echo alias $NAME; sleep 0.1; echo q) | ./cli 99 $IP

done
## rename end ##
