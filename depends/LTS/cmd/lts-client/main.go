package main

import (
	"context"
	"flag"
	"sync"

	log "github.com/sirupsen/logrus"

	"fmt"
	"github.com/gogo/protobuf/proto"
	"git.code.oa.com/tdsql_util/LTS/ltsclient"
	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltslogutil"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"math"
	"reflect"
)

var (
	help                  bool
	logLevel              string
	logFile               string
	dataDir               string
	serverAddr            string
	clusterID             uint64
	httpAddr              string
	txntsAddr             string
	isAutoConnCluster     bool
	workerAmount          uint64
	readBufferSize        int
	writeBufferSize       int
	initialWindowSize     int
	initialConnWindowSize int
	requestMode           string
	reqHeaderLength       int
	reqBodyLength         int
	tcpSetNoDelay         int
)

func init() {
	flag.BoolVar(&help, "h", false, "help center.")
	flag.StringVar(&serverAddr, "lts-cluster", "127.0.0.1:62379", "Default 127.0.0.1:62379")
	flag.StringVar(&txntsAddr, "txnts-service", "127.0.0.1:62389", "127.0.0.1:62389")
	flag.Uint64Var(&clusterID, "cluster-id", 6547452155891930739, "LTS cluster id ")
	flag.BoolVar(&isAutoConnCluster, "auto-connected", true, "Auto connect to lts.")
	flag.StringVar(&httpAddr, "http", "127.0.0.1:8090", "http service")
	flag.StringVar(&logLevel, "log-level", "debug", "log level: Debug, Info, Error etc,.")
	flag.StringVar(&logFile, "log-file", "logs/lts-client.log", "log file: lts-client.log, Info, Error etc,.")
	flag.StringVar(&dataDir, "data", "data/lts-client", "data dir: /tmp/")
	flag.Uint64Var(&workerAmount, "worker-amount", 8, "Set up the amount of workers")
	flag.IntVar(&readBufferSize, "read-buffer-size", 131072, "")
	flag.IntVar(&writeBufferSize, "write-buffer-size", 131072, "")
	flag.IntVar(&initialWindowSize, "init-window-size", 6291456, "")
	flag.IntVar(&initialConnWindowSize, "init-conn-window-size", 8388608, "")
	flag.StringVar(&requestMode, "request-mode", "normal", "to run different request mode")
	flag.IntVar(&reqHeaderLength, "req-header-length", 2, "req-header-length")
	flag.IntVar(&tcpSetNoDelay, "tcp-set-no-delay", 0, "tcp-set-no-delay")

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

	reqBodyLength = len(tmpBuf)
	fmt.Printf("[INIT] Maximum length of marshaled GetTxnTimestampCtx is: %v\n", reqBodyLength)
}

func main() {
	flag.Parse()

	if help {
		flag.Usage()
		return
	}

	option := &ltsclient.ClientOption{
		HttpAddr:              httpAddr,
		ServerAddr:            serverAddr,
		ClusterID:             uint32(clusterID),
		DataDir:               dataDir,
		IsAutoConnected:       isAutoConnCluster,
		ReadBufferSize:        readBufferSize,
		WriteBufferSize:       writeBufferSize,
		InitialWindowSize:     initialWindowSize,
		InitialConnWindowSize: initialConnWindowSize,
		RequestMode:           requestMode,
		TxntsAddr:             txntsAddr,
		ReqHeaderLength:       reqHeaderLength,
		ReqBodyLength:         reqBodyLength,
		TCPSetNoDelay:         tcpSetNoDelay,
	}

	logCfg := &ltslogutil.LogConfig{
		Level: logLevel,
		File: ltslogutil.FileLogConfig{
			Filename: logFile,
		},
		DisableTimestamp: false,
		Format:           "text", // default is "text"
	}

	err := ltslogutil.InitLogger(logCfg)
	if err != nil {
		log.Fatalf("Initialize logger error: %s\n", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	simulator := ltsclient.NewClientSimulator(ctx, option)

	// Create some workers
	simulator.GenerateWorkers(workerAmount)

	wg := &sync.WaitGroup{}
	wg.Add(1)

	// Run the client simulator
	go simulator.Run(wg)

	wg.Wait()
	cancel()
	simulator.Close()
}
