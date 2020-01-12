#!/bin/bash

(
	echo @rank_reward; sleep 1; echo q; sleep 1
) | telnet 127.0.0.1 7710

