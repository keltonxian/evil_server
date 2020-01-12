#!/bin/bash

USER=kelton
HOST=127.0.0.1
PORT=8010
SSH_PARAM=$USER@$HOST

CMD=`cat << DELIM

cd ~/Workspace/evil/evil_server
cd encrypt
rm -rf tmp
mkdir tmp
./encrypt ../main/res/lua tmp logic.lua

cd ../update

rm -rf tmp
mkdir tmp
mkdir tmp/core
cp ../encrypt/tmp/logic.evil tmp/core/
cp ../main/res/lua/lang_zh.lua tmp/core/
cd tmp
zip -r l.zip core
mkdir slist
cp ../../main/res/slist.txt slist
zip -r slist.zip slist
cd ../
cp tmp/l.zip $TOMCAT_ROOT/webapps/evil/res/lua
cp tmp/slist.zip $TOMCAT_ROOT/webapps/evil/res
rm -rf ../encrypt/tmp
rm -rf tmp

curl http://$HOST:$PORT/evil/patch.refresh

DELIM`

echo "$CMD" | ssh $SSH_PARAM
