path_to_add := $(addsuffix /bin,$(subst :,/bin:,$(GOPATH)))
export PATH := $(path_to_add):$(PATH)

LTS_PKG := LTS
GOFILTER := grep -vE 'vendor|testutil'
GOCHECKER := $(GOFILTER) | awk '{ print } END { if (NR > 0) { exit 1 } }'

ifeq ($(debug),y)
GCFLAGS = -gcflags "-N -l"
else
GCFLAGS = -gcflags "-N -l"
endif

COVERIGNORE := "cmd/*/*,metactl/*,metactl/*/*,server/api/bindata_assetfs.go"

ifeq ($(os), darwin) 
GO := CGO_ENABLED=0 GOOS=darwin GOARCH=amd64 go
else ifeq ($(os), windows)
GO := CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go
else
GO := CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go
endif

default: build

all: build simulator check
.PHONY: all

build:
ifeq ("$(WITH_RACE)", "1")
	CGO_ENABLED=1 go build -race -ldflags '$(LDFLAGS)' $(GCFLAGS) -o bin/lts-cluster-service cmd/lts-server/main.go
else
	$(GO) build -ldflags '$(LDFLAGS)' $(GCFLAGS) -o bin/lts-cluster-service cmd/lts-server/main.go
endif
	$(GO) build -ldflags '$(LDFLAGS)' $(GCFLAGS) -o bin/lts-client-simulator cmd/lts-client/main.go
	$(GO) build -ldflags '$(LDFLAGS)' $(GCFLAGS) -o examples/grpc_client/grpc_client examples/grpc_client/grpc_client.go
	$(GO) build -ldflags '$(LDFLAGS)' $(GCFLAGS) -o examples/http_client/http_client examples/http_client/http_client.go
	$(GO) build -ldflags '$(LDFLAGS)' $(GCFLAGS) -o examples/tcp_client/tcp_client examples/tcp_client/tcp_client.go

simulator:
	CGO_ENABLED=0 go build -o bin/lts-client-simulator cmd/lts-client/main.go


check:
	go get github.com/golang/lint/golint

	@echo "vet"
	@ go tool vet . 2>&1 | $(GOCHECKER)
	@ go tool vet --shadow . 2>&1 | $(GOCHECKER)
	@echo "golint"
	@ golint ./... 2>&1 | $(GOCHECKER)
	@echo "gofmt"
	@ gofmt -s -l . 2>&1 | $(GOCHECKER)

travis_coverage:
ifeq ("$(TRAVIS_COVERAGE)", "1")
	GOPATH=$(VENDOR) $(HOME)/gopath/bin/goveralls -service=travis-ci -ignore $(COVERIGNORE)
else
	@echo "coverage only runs in travis."
endif

update:
	which dep 2>/dev/null || go get -u github.com/golang/dep/cmd/dep
ifdef PKG
	dep ensure -add ${PKG}
else
	dep ensure -update
endif
	@echo "removing test files"
	#dep prune
	#bash ./hack/clean_vendor.sh

clean:
	-rm -rf bin/lts-cluster-service
	-rm -rf bin/lts-client-simulator
	-rm -rf examples/grpc_client/grpc_client
	-rm -rf examples/http_client/http_client
	-rm -rf examples/tcp_client/tcp_client
	-rm -rf sbin/data
	-rm -rf sbin/logs
	-rm -rf sbin/*.out

.PHONY: update clean
