#!/usr/bin/env sh

pid=`ps -ef | grep "lts-client-simulator" | grep -v grep | awk '{print $2}'`
echo $pid

if [ "$pid" != "" ] ; then
  echo "do kill:$pid"
  kill $pid
else
  echo "not found."
fi
