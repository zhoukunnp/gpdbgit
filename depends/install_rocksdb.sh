#!/usr/bin/env bash

depsrc=$GPDEPEND
devhome=$GPDEPEND_HOME
echo 'We are going to install dependencies under '$depsrc','
echo 'If this is not correct, please set the right path for depsrc in install_rocksdb.sh'
deptmp=$GPDEPENDCOMPILE

echo 'extracting src from tarballs...'
tar zxf $depsrc/zstd-1.1.3.tar.gz -C $deptmp/
tar zxf $depsrc/gflags-2.0.tar.gz -C $deptmp/
tar zxf $depsrc/rocksdb-5.17.2.tar.gz -C $deptmp/

cd $deptmp

echo 'installing gflags from src...'
cd gflags-2.0
./configure
make
echo 'su to root and continue installing gflags.'
make install
sed -i '$a\\n' $devhome/.bashrc
sed -i '$a\export CPATH=/usr/local/include' $devhome/.bashrc
sed -i '$a\\n' $devhome/.bashrc
sed -i '$a\export LIBRARY_PATH=/usr/local/lib' $devhome/.bashrc
source $devhome/.bashrc
cd ..

echo 'installing dependencies from yum...'
echo 'please ensure that Internet is available.'
yum install -y snappy snappy-devel zlib zlib-devel bzip2 bzip2-devel lz4-devel libasan

echo 'installing zstandard from src...'
cd zstd-1.1.3
make
make install

cd ..

echo 'installing rocksdb from src...'
cd rocksdb-5.17.2
make install-shared INSTALL_PATH=/usr/local

ldconfig

echo 'rocksdb has been successfully installed.'

chown -R gpadmin:gpadmin $deptmp/gflags-2.0
chown -R gpadmin:gpadmin $deptmp/zstd-1.1.3
chown -R gpadmin:gpadmin $deptmp/rocksdb-5.17.2
