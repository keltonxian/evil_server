#!/bin/bash

## cronjob for nio server:
## 1. do @xcreset for reseting the exchange
## 2. do @ladder for generate ladder

## usage: crontab -e : add this:  (remove ##)
## 10 4 * * * /home/mac/Documents/workspace/evil/evil_server/cron.sh


## (echo @xcreset 86400; echo q; sleep 0.1) | telnet 127.0.0.1 7710
## (echo @ladder; echo q; sleep 0.1) | telnet 127.0.0.1 7710

(
	echo @ladder; sleep 1; echo q; sleep 1
) | telnet 127.0.0.1 7710

