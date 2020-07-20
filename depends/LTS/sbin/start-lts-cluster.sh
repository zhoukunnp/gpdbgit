#!/usr/bin/env sh 

lts_root=$(cd `dirname $0`/..; pwd)
echo "LTS Cluster root: $lts_root"

pid=`ps -ef | grep "lts-cluster-service" | grep -v grep | awk '{print $2}'`
if [ "$pid" != "" ]; then
    echo "lts-cluster-service has already started: $pid"
else
    echo "To start LTS Cluster..."
    GODEBUG=gctrace=1 ${lts_root}/bin/lts-cluster-service --config ${lts_root}/conf/config.toml > m.out 2>&1 &
    echo "Waiting..."
    sleep 3 && tail -n 30 logs/lts-cluster.log
    echo "Start LTS Cluster done."
fi
