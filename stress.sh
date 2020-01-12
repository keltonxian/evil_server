#!/bin/bash

date
for ((i=1; i<=100; i++))
do
./cli 100 > /dev/null
done

date


