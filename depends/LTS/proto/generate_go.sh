#!/usr/bin/env bash

TDSQL_LTS_BASE_PATH=`pwd`
TDSQL_THIRD_PARTY_PATH=${TDSQL_LTS_BASE_PATH}
TDSQL_THIRD_PARTY_PROTOBUF_PATH=${TDSQL_THIRD_PARTY_PATH}/protobuf-v3.5.1

mkdir -p $TDSQL_LTS_BASE_PATH/go_code
rm -rf $TDSQL_LTS_BASE_PATH/go_code/*

export LD_LIBRARY_PATH=${TDSQL_THIRD_PARTY_PROTOBUF_PATH}/lib:$LD_LIBRARY_PATH
export PATH=${TDSQL_THIRD_PARTY_PROTOBUF_PATH}/bin/:$PATH

PROGRAM=$(basename "$0")

if [ -z $GOPATH ]; then
    printf "Error: the environment variable GOPATH is not set, please set it before running %s\n" $PROGRAM > /dev/stderr
    exit 1
fi

GO_PREFIX_PATH=ltsproto/ltsrpc

gogo_protobuf_url=github.com/gogo/protobuf
GOGO_ROOT=../src/vendor/${gogo_protobuf_url}
GO_OUT_M=
GO_INSTALL='go install'

echo "install gogoproto code/generator ..."
${GO_INSTALL} ${gogo_protobuf_url}/proto
${GO_INSTALL} ${gogo_protobuf_url}/protoc-gen-gogofaster
${GO_INSTALL} ${gogo_protobuf_url}/gogoproto

echo "install goimports ..."
${GO_INSTALL} golang.org/x/tools/cmd/goimports

# add the bin path of gogoproto generator into PATH if it's missing
echo "adding bin path of gogoproto generator into PATH..."
for path in $(echo "${GOPATH}" | sed -e 's/:/ /g'); do
    gogo_proto_bin="${path}/bin/protoc-gen-gogofaster"
    if [ -e "${gogo_proto_bin}" ]; then
        export PATH=$(dirname "${gogo_proto_bin}"):$PATH
        break
    fi
done

function collect() {
    file=$(basename $1)
    base_name=$(basename $file ".proto")
    mkdir -p ../pkg/$base_name
    if [ -z $GO_OUT_M ]; then
        GO_OUT_M="M$file=$GO_PREFIX_PATH/$base_name"
    else
        GO_OUT_M="$GO_OUT_M,M$file=$GO_PREFIX_PATH/$base_name"
    fi
}

for file in `ls *.proto`
do
  collect $file
done

echo $GO_OUT_M

ret=0

function gen() {
  base_name=$(basename $1 ".proto")
  protoc -I.:${GOGO_ROOT}:${GOGO_ROOT}/protobuf --gogofaster_out=plugins=grpc,$GO_OUT_M:go_code $1 || ret=$?
  sed -i.bak -E 's/import _ \"gogoproto\"//g' go_code/*.pb.go
  sed -i.bak -E 's/import fmt \"fmt\"//g' go_code/*.pb.go
  sed -i.bak -E 's/import io \"io\"//g' go_code/*.pb.go
  sed -i.bak -E 's/import math \"math\"//g' go_code/*.pb.go
  rm -f go_code/*.pb.go.bak
  goimports -w go_code/*.pb.go
}

for file in `ls *.proto`
do
  gen $file
done

mv -f go_code/ltsrpc.pb.go ../src/ltsproto/ltsrpc/

echo "Generate pb.go files done..."

exit $ret
