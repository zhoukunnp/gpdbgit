#!/usr/bin/env bash


# perf all cpu
# screen -dmS test bash -c "cd $1; sleep 5; perf record -a -F 99 --call-graph dwarf sleep 10"

if [[ $8 != 1 ]]
then
	# perf session
	screen -dmS test bash -c "cd $4/tmp/$1_perf_$6"_"$7/session; sleep $2; perf record -p \$(ps -aux|grep $5|grep postgres|grep con| grep -v grep|awk '{print \$2}' | head -n 1) -F 99 --call-graph dwarf sleep $3; chmod 666 perf.data;"
	screen -dmS all bash -c "cd $4/tmp/$1_perf_$6"_"$7/all; sleep $2; perf record -a -F 97 --call-graph dwarf sleep 10; chmod 666 perf.data;"
else
	#storage process perf
	#echo "cd $4/tmp/$1_perf_$6"_"$7/storage; sleep $2; perf record -p \$(ps -aux|grep storage|grep postgres|grep $8|grep -v grep|awk '{print \$2}' | head -n 1) -F 99 --call-graph dwarf sleep $3;" > temp
	screen -dmS test bash -c "cd $4/tmp/$1_perf_$6"_"$7/storage; sleep $2; perf record -p \$(ps -aux|grep storage|grep postgres|grep $9|grep -v grep|awk '{print \$2}' | head -n 1) -F 97 --call-graph dwarf sleep $3; chmod 666 perf.data;"

	screen -dmS all bash -c "cd $4/tmp/$1_perf_$6"_"$7/all; sleep $2; perf record -a -F 97 --call-graph dwarf sleep 10; chmod 666 perf.data;"
	#echo "cd $4/tmp/$1_perf_$6"_"$7/session; sleep $2; perf record -p \$(ps -aux|grep seg|grep postgres|grep con|grep $9| grep -v grep|awk '{print \$2}' | head -n 1) -F 99 --call-graph dwarf sleep $3;" > temp2
	#session process perf
	screen -dmS session bash -c "cd $4/tmp/$1_perf_$6"_"$7/session; sleep $2; perf record -p \$(ps -aux|grep seg|grep postgres|grep con|grep $9| grep -v grep|awk '{print \$2}' | head -n 1) -F 99 --call-graph dwarf sleep $3; chmod 666 perf.data;"
fi
# mv perf.data perf.data.session_$1"_"$6"_"$7