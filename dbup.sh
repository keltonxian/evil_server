#!/bin/bash

## echo update to remote 30 mysql
## mysql -uevil -p1 -h192.168.1.30 < db-init.sql

echo update to local mysql
mysql -uevil -p1 < db-init.sql

