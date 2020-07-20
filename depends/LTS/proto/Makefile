LTS_BASE_PATH=$(shell pwd)
LTS_THIRD_PARTY_PATH=$(LTS_BASE_PATH)

# Protobuf
LTS_THIRD_PARTY_PROTOBUF_PATH=$(LTS_THIRD_PARTY_PATH)/protobuf-v3.5.1
LTS_THIRD_PARTY_PROTOC=$(LTS_THIRD_PARTY_PROTOBUF_PATH)/bin/protoc
LTS_THIRD_PARTY_PROTOBUF_LIB_PATH=$(LTS_THIRD_PARTY_PROTOBUF_PATH)/lib

# grpc
LTS_THIRD_PARTY_GRPC_PATH=$(LTS_THIRD_PARTY_PATH)/grpc-v1.8.6

# go plugin
LTS_THIRD_PARTY_GRPC_GO_PLUGIN_PATH=$(LTS_BASE_PATH)/../bin/

all: prepare
	@echo "To build code: go"

prepare:
	-mkdir -p go_code
	sh generate_go.sh

.PRECIOUS: clean
clean:
	-rm -rf go_code
