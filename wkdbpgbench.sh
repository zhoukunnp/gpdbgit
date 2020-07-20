#!/usr/bin/env bash

var=$3
curResult=$4
gptmp=${GPCOMPILE}

# $1 for object type, heap focc sss etc.
# $2 for test type, tpcb select update.
# $3 for 
INFO "$2-$1-${var} start..."
export PGDATABASE=tpcb$1
if [[ "$2" == "tpcb" ]]
then
	if [[ "$1" == "focc" ]]
	then
		pgbench -c ${var} -j ${var} -r -T 60 -n -P 1 -O >> ${curResult}/$2-$1-${var}
	elif [[ "$1" == "bocc" ]]
	then
		pgbench -c ${var} -j ${var} -r -T 60 -n -P 1 -O >> ${curResult}/$2-$1-${var}
	else
		pgbench -c ${var} -j ${var} -r -T 60 -n -P 1 -N >> ${curResult}/$2-$1-${var}
	fi
elif [[ "$2" == "select" ]]
then
	pgbench -c ${var} -j ${var} -r -T 60 -n -P 1 -S >> ${curResult}/$2-$1-${var}
elif [[ "$2" == "update" ]]
then
	pgbench -c ${var} -j ${var} -r -T 60 -n -P 1 -f ${gptmp}/contrib/pgbench/sysbench/update-only.sql >> ${curResult}/$2-$1-${var}
fi

INFO "$2-$1-${var} stop."

screen -dms pgbenchtest bash -c "./wkdbpgbench.sh $1 $2"
