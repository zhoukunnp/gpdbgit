#!/usr/bin/env sh

pid=`ps -ef | grep "lts-client-simulator" | grep -v grep | awk '{print $2}'`

lts_cluster_root=$(cd `dirname $0`/..; pwd)
echo "lts_cluster root: $lts_cluster_root"

lts_cluster_service_url="127.0.0.1:62379"
txnts_service_url="127.0.0.1:62389"
client_simulator_url="127.0.0.1:8090"

if [ "$pid" != "" ]; then
    echo "client-simulator has already started: $pid"
else
    rm -f logs/simulator.log
    mkdir -p logs
    echo "To start LTS client simulator ..."
    $lts_cluster_root/bin/lts-client-simulator --log-level "Debug" \
      --log-file logs/simulator.log \
      --data data/simulator \
      --http $client_simulator_url \
      --lts-cluster $lts_cluster_service_url \
      --txnts-service $txnts_service_url \
      --worker-amount 8 --request-mode "normal" \
      --req-header-length 2 --tcp-set-no-delay 0 \
      --read-buffer-size 131072 --write-buffer-size 131072 \
      --init-window-size 0 --init-conn-window-size 0 logs/simulator.out 2>&1 &

      echo "Waiting...."
      sleep 3 && tail -n 50 logs/simulator.log
      echo "LTS client simulator has been started."
fi