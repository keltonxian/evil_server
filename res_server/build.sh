#!/bin/bash

javac -d classes src/com/evil/common/ZipUtil.java
javac -classpath $TOMCAT_ROOT/lib/servlet-api.jar:classes:. -d classes src/com/evil/web/PatchRefreshServlet.java
javac -classpath $TOMCAT_ROOT/lib/servlet-api.jar:classes:. -d classes src/com/evil/web/ServerListServlet.java
javac -classpath $TOMCAT_ROOT/lib/servlet-api.jar:classes:. -d classes src/com/evil/web/ServerListVersionServlet.java
javac -classpath $TOMCAT_ROOT/lib/servlet-api.jar:classes:. -d classes src/com/evil/web/PatchCoreServlet.java
javac -classpath $TOMCAT_ROOT/lib/servlet-api.jar:classes:. -d classes src/com/evil/web/PatchCoreVersionServlet.java

DST_ROOT=$TOMCAT_ROOT/webapps/evil
WEB_ROOT=$DST_ROOT/WEB-INF

cp classes/com/evil/common/ZipUtil.class $WEB_ROOT/classes/com/evil/common/
cp classes/com/evil/web/PatchRefreshServlet.class $WEB_ROOT/classes/com/evil/web/
cp classes/com/evil/web/ServerListServlet.class $WEB_ROOT/classes/com/evil/web/
cp classes/com/evil/web/ServerListVersionServlet.class $WEB_ROOT/classes/com/evil/web/
cp classes/com/evil/web/PatchCoreServlet.class $WEB_ROOT/classes/com/evil/web/
cp classes/com/evil/web/PatchCoreVersionServlet.class $WEB_ROOT/classes/com/evil/web/

cp etc/web.xml $WEB_ROOT/
