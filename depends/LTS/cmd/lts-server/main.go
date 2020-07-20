package main

import (
	"flag"
	"github.com/juju/errors"

	log "github.com/sirupsen/logrus"

	"encoding/json"
	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltslogutil"
	"git.code.oa.com/tdsql_util/LTS/ltsserver"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsapi"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"reflect"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"math"
	"github.com/gogo/protobuf/proto"
	"fmt"
)


var (
	bodyLen uint32 = 16 // Will be updated by init()
)

// init calculates the maximum length of marshaled GetTxnTimestampCtx
func init() {
	mockReq := reflect.New(reflect.TypeOf(ltsrpc.GetTxnTimestampCtx{}))
	for i := 0; i < mockReq.Elem().NumField(); i++ {
		switch mockReq.Elem().Field(i).Kind() {
		case reflect.Uint64:
			mockReq.Elem().Field(i).SetUint(math.MaxUint64)
		case reflect.Uint32:
			mockReq.Elem().Field(i).SetUint(math.MaxUint32)
		case reflect.Uint16:
			mockReq.Elem().Field(i).SetUint(math.MaxUint16)
		case reflect.Uint8:
			mockReq.Elem().Field(i).SetUint(math.MaxUint8)
		case reflect.Int64:
			mockReq.Elem().Field(i).SetUint(math.MaxInt64)
		case reflect.Int32:
			mockReq.Elem().Field(i).SetUint(math.MaxInt32)
		case reflect.Int16:
			mockReq.Elem().Field(i).SetUint(math.MaxInt16)
		case reflect.Int8:
			mockReq.Elem().Field(i).SetUint(math.MaxInt8)
		case reflect.Bool:
			mockReq.Elem().Field(i).SetBool(true)
		case reflect.Int:
			mockReq.Elem().Field(i).SetInt(math.MaxInt64)
		case reflect.Uint:
			mockReq.Elem().Field(i).SetUint(math.MaxUint64)
		}
	}

	tmpReq := mockReq.Elem().Interface().(ltsrpc.GetTxnTimestampCtx)
	tmpBuf, _ := proto.Marshal(&tmpReq)

	bodyLen = uint32(len(tmpBuf))
	fmt.Printf("[INIT] Maximum length of marshaled GetTxnTimestampCtx is: %v\n", bodyLen)
}


func main() {
	err := ltsconf.NewConfig(os.Args[1:])
	defer ltslogutil.LogPanic()

	switch errors.Cause(err) {
	case nil:
	case flag.ErrHelp:
		os.Exit(0)
	default:
		log.Fatalf("parse cmd flags error: %s\n", err)
	}

	dataDir, _ := filepath.Abs(ltsconf.Cfg().DataDir)
	logFile, _ := filepath.Abs(ltsconf.Cfg().Log.File.Filename)
	rel, _ := filepath.Rel(dataDir, filepath.Dir(logFile))

	if !strings.HasPrefix(rel, "..") {
		log.Fatalf("initialize logger error: log directory shouldn't be the subdirectory of data directory")
	}

	err = ltslogutil.InitLogger(&ltsconf.Cfg().Log)
	if err != nil {
		log.Fatalf("initialize logger error: %s\n", err)
	}

	for _, msg := range ltsconf.Cfg().WarningMsgs {
		log.Warn(msg)
	}

	ltsconf.Cfg().TCP.TCPMaxBodyLength = bodyLen

	jsonConfig, _ := json.Marshal(ltsconf.Cfg())
	log.Infof("Print config: %v", string(jsonConfig))

	err = ltsserver.PrepareJoinCluster()
	if err != nil {
		log.Fatal("join error ", errors.ErrorStack(err))
	}

	svr, err := ltsserver.CreateServer(ltsconf.Cfg(), ltsapi.NewHandler)
	if err != nil {
		log.Fatalf("Create LTS server failed: %v", errors.ErrorStack(err))
	}

	if err = ltsserver.InitHTTPClient(svr); err != nil {
		log.Fatalf("Init http client for http_api handler failed: %v", errors.ErrorStack(err))
	}

	sc := make(chan os.Signal, 1)
	signal.Notify(sc,
		syscall.SIGHUP,
		syscall.SIGINT,
		syscall.SIGTERM,
		syscall.SIGQUIT)

	if err := svr.Run(); err != nil {
		log.Fatalf("Run LTS server failed: %v", errors.ErrorStack(err))
	}

	sig := <-sc
	log.Infof("Got signal [%d] to exit.", sig)

	svr.Close()

	switch sig {
	case syscall.SIGTERM:
		os.Exit(0)
	default:
		os.Exit(1)
	}

	return
}
