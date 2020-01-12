
echo Start execute cmd_init.sh

sleep 1;
(echo @ladder; sleep 3;
echo @arena; sleep 3
) | telnet 0 7710
#echo @ranking; sleep 5;

echo End execute cmd_init.sh
