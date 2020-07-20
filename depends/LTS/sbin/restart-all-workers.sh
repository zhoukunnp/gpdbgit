#!/usr/bin/env sh

client_simulator_ip="9.30.17.130"

client_simulator_urls=(
  "${client_simulator_ip}:8090" 
)

if [ "$1" == "batch" ]; then
client_simulator_urls=(
    "${client_simulator_ip}:8090"
    "${client_simulator_ip}:8091"
    "${client_simulator_ip}:8092"
    "${client_simulator_ip}:8093"
    "${client_simulator_ip}:8094"
    "${client_simulator_ip}:8095"
    "${client_simulator_ip}:8096"
    "${client_simulator_ip}:8097"
)
fi

for idx in ${!client_simulator_urls[@]}
do
    echo "To restart workers of simulator[$idx]...."
    curl -XGET http://${client_simulator_urls[$idx]}/lts-client/api/v1/restart-workers
    echo "Waiting...."
    sleep 3
done

echo "==DONE=="