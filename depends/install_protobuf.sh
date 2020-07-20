#!/usr/bin/env bash

depsrc=$GPDEPEND
devhome=$GPDEPEND_HOME
echo 'We are going to install dependencies under '$depsrc','
deptmp=$GPDEPENDCOMPILE

echo 'extracting src from tarballs...'
tar zxf $deptmp/protobuf-all-3.9.1.tar.gz -C $deptmp/

cd $deptmp

echo 'installing protobuf from src...'
cd protobuf-3.9.1
./autogen.sh
./configure
make -j4
make install

ldconfig

echo 'protobuf has been successfully installed.'

chown -R gpadmin:gpadmin $deptmp/protobuf-3.9.1
cd $GPDEPEND
cd ../src/backend/paxos_storage
protoc --cpp_out=.paxos_msg.proto
