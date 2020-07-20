package ltstestutil

import (
	"fmt"
	log "github.com/sirupsen/logrus"
	"net"
	"sync"
	"time"
)

var (
	testAddrMutex sync.Mutex
	testAddrMap   = make(map[string]struct{})
)

// AllocTestURL allocates a local URL for testing.
func AllocTestURL() string {
	for i := 0; i < 10; i++ {
		if u := tryAllocTestURL(); u != "" {
			log.Infof("Allot test url:%v", u)
			return u
		}
		time.Sleep(time.Second)
	}
	log.Fatal("failed to alloc test URL")
	return ""
}

func tryAllocTestURL() string {
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		log.Fatal(err)
	}
	addr := fmt.Sprintf("http://%s", l.Addr())
	err = l.Close()
	if err != nil {
		log.Fatal(err)
	}

	testAddrMutex.Lock()
	defer testAddrMutex.Unlock()
	if _, ok := testAddrMap[addr]; ok {
		return ""
	}
	testAddrMap[addr] = struct{}{}
	return addr
}