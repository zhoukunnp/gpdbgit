#!/bin/bash

# This file has the .sql extension, but it is actually launched as a shell
# script. This contortion is necessary because pg_regress normally uses
# psql to run the input scripts, and requires them to have the .sql
# extension, but we use a custom launcher script that runs the scripts using
# a shell instead.

TESTNAME=ao_rewind

. sql/config_test.sh

# Do an insert in master.
function before_standby
{
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL <<EOF
CREATE TABLE aotable (d text) with (appendonly=true);
INSERT INTO aotable VALUES ('in master');
CREATE TABLE cotable (a text, b text) with (appendonly=true, orientation=column);
INSERT INTO cotable VALUES ('in master', NULL);
CREATE TABLE aotable_copytail (d text) with (appendonly=true);
INSERT INTO aotable_copytail VALUES ('in master');
CHECKPOINT;
EOF
}

function standby_following_master
{
# Insert additional data on master that will be replicated to standby
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "INSERT INTO aotable values ('in master, before promotion');"
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "INSERT INTO cotable values ('in master, before promotion', NULL);"
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "INSERT INTO aotable_copytail values ('in master, before promotion');"

# Launch checkpoint after standby has been started
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "CHECKPOINT;"
}

# This script runs after the standby has been promoted. Old Master is still
# running.
function after_promotion
{
# Insert a row in the old master. This causes the master and standby to have
# "diverged", it's no longer possible to just apply the standby's logs over
# master directory - you need to rewind.
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "INSERT INTO aotable values ('in master, after promotion');"
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "SELECT * from aotable;"
PGOPTIONS=${PGOPTIONS_UTILITY} $STANDBY_PSQL -c "INSERT INTO aotable values ('in standby, after promotion');"
PGOPTIONS=${PGOPTIONS_UTILITY} $STANDBY_PSQL -c "SELECT * from aotable;"

PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "INSERT INTO cotable values ('in master, after promotion', NULL);"
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "SELECT * from cotable;"
PGOPTIONS=${PGOPTIONS_UTILITY} $STANDBY_PSQL -c "INSERT INTO cotable values ('in standby, after promotion', NULL);"
PGOPTIONS=${PGOPTIONS_UTILITY} $STANDBY_PSQL -c "SELECT * from cotable;"

PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "DELETE from aotable_copytail;"
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "VACUUM full aotable_copytail;"
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "SELECT * from aotable_copytail;"
# Also insert a new row in the standby, which won't be present in the old
# master.
PGOPTIONS=${PGOPTIONS_UTILITY} $STANDBY_PSQL -c "INSERT INTO aotable_copytail VALUES ('in standby, after promotion');"
}

function pattern_exist_in_log
{
pattern=`PGOPTIONS=${PGOPTIONS_UTILITY} $STANDBY_PSQL_TUPLES_ONLY -c \
    "SELECT format('/%s${3} (${2})', relfilenode) FROM pg_class WHERE relname='${1}';" \
    | xargs echo -n`

if [ -z "$pattern" ]; then
    echo "\"${1}\" table not found";
    return;
fi

if grep -q "$pattern" $log_path; then
	echo "${2} \"${1}\" ${3} file found in pg_rewind logs"
else
	echo "Missing ${2} for ${3} file of \"${1}\" in pg_rewind logs"
fi
}

# Compare results generated by querying new master after rewind
# no copy done should be what happens
function after_rewind
{
pattern_exist_in_log "aotable" "COPY_TAIL"
PGOPTIONS=${PGOPTIONS_UTILITY} $STANDBY_PSQL -c "SELECT * from aotable;"

pattern_exist_in_log "cotable" "COPY_TAIL"
# .128 is for file corresponding to second column for cotable
pattern_exist_in_log "cotable" "COPY_TAIL" ".128"
PGOPTIONS=${PGOPTIONS_UTILITY} $STANDBY_PSQL -c "SELECT * from cotable;"

pattern_exist_in_log "aotable_copytail" "COPY_TAIL"
PGOPTIONS=${PGOPTIONS_UTILITY} $STANDBY_PSQL -c "SELECT * from aotable_copytail;"
}

# Compare results generated by querying original master after rewind
# Specifically, the tuples which would have been eliminated by deleting from aotable have been restored
function after_master_promotion
{
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "SELECT * from aotable;"
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "SELECT * from cotable;"
PGOPTIONS=${PGOPTIONS_UTILITY} $MASTER_PSQL -c "SELECT * from aotable_copytail;"
}

# Run the test
. sql/run_test.sh
