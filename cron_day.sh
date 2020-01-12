#!/bin/bash

## this shell script is for every day work
## cronjob for nio server:
## 1. do @reset_ranktime for reset ranking challenge times

## usage: crontab -e : add this:  (remove ##)
## 0 0 * * * /home/mac/Documents/workspace/evil/evil_server/cron_day.sh


## (echo @xcreset 86400; echo q; sleep 0.1) | telnet 127.0.0.1 7710
## (echo @ladder; echo q; sleep 0.1) | telnet 127.0.0.1 7710

(
	echo @reset_arenatimes; sleep 1; echo @reset_fighttimes; sleep 1; echo q; sleep 1
) | telnet 127.0.0.1 7710

