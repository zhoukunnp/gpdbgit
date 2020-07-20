package ltsclient

import (
	"context"
	"sync"
	"time"

	"github.com/juju/errors"
	log "github.com/sirupsen/logrus"
	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltslogutil"
	"strconv"
	"sync/atomic"
)

type ClientOption struct {
	HttpAddr              string
	TxntsAddr             string
	IsAutoConnected       bool
	ServerAddr            string
	ClusterID             uint32
	DataDir               string
	ReadBufferSize        int
	WriteBufferSize       int
	InitialWindowSize     int // window and conn size for dialoption
	InitialConnWindowSize int
	RequestMode           string // default is "normal"
	ReqHeaderLength       int
	ReqBodyLength         int
	TCPSetNoDelay         int
}

type ClientSimulator struct {
	sync.RWMutex

	ctx    context.Context
	cancel context.CancelFunc

	opt *ClientOption

	workers map[string]*Worker

	// workerCounter is used to generate unique worker id
	workerCounter uint64

	gClient *GRPCClient

	// Global txn ts request and resp count
	txnTSReqCount  uint64
	txnTSRespCount uint64
}

func NewClientSimulator(ctx context.Context, opt *ClientOption) *ClientSimulator {
	tmpCtx, tmpCancel := context.WithCancel(ctx)
	return &ClientSimulator{
		ctx:           tmpCtx,
		cancel:        tmpCancel,
		opt:           opt,
		workerCounter: uint64(0),
		workers:       make(map[string]*Worker),
		gClient:       NewGRPCClient(opt, ctx, "Simulator", opt.ClusterID, opt.ServerAddr),
	}
}

func (cs *ClientSimulator) Run(wg *sync.WaitGroup) {
	defer ltslogutil.LogPanic()
	defer wg.Done()

	httpServer := NewHttpServer(cs)
	go httpServer.Run()

	if err := cs.gClient.Connect(); err != nil {
		log.Errorf("Failed to connect to meta, err: %v", err)
		return
	}

	// Start sending store heartbeat and start sg workers if pool workers already exist
	if err := cs.StartAllWorkers(); err != nil {
		log.Errorf("Failed to start workers, err: %v", err)
		return
	}

	workerAmount := len(cs.workers)
	for {
		count := workerAmount
		for _, w := range cs.workers {
			if !w.IsStart() {
				log.Infof("Worker: %v is not started.", w.id)
				continue
			}
			count--
		}

		if count <= 0 {
			break
		}
	}

	log.Infof("All workers have been started !!!")

	ticker := time.NewTicker(time.Second * 10)
	defer ticker.Stop()

	prev := uint64(0)
	for {
		select {
		case <-ticker.C:
			currentSnapShot := atomic.LoadUint64(&cs.txnTSRespCount)
			result := (currentSnapShot - prev) / 10
			prev = currentSnapShot

			log.Infof("Simulator workers' GetTxnTS throughput: %v/s", result)

		case <-cs.ctx.Done():
			log.Warnf("Simulator quits the run loop ...")
			return
		}
	}
}

func (cs *ClientSimulator) RestartAllWorkers() error {
	cs.Lock()
	defer cs.Unlock()

	return cs.RestartAllWorkersLocked()
}

func (cs *ClientSimulator) RestartAllWorkersLocked() error {
	cs.StopAllWorkersLocked()
	log.Infof("Ready to restart all workers, sleep a while...")
	time.Sleep(time.Second * 10)

	for _, worker := range cs.workers {
		worker.quitChan = make(chan struct{})
		switch cs.opt.RequestMode {
		case "normal":
			go worker.RunNormalMode(cs.ctx)
		default:
			log.Error("Invalid request mode, exit !!!")
			return errors.Errorf("Invalid request mode!!!")
		}
		time.Sleep(time.Millisecond * 17)
	}

	return nil
}

func (cs *ClientSimulator) StartAllWorkers() error {
	cs.Lock()
	defer cs.Unlock()

	return cs.StartAllWorkersLocked()
}

func (cs *ClientSimulator) StartAllWorkersLocked() error {
	for _, worker := range cs.workers {
		if !worker.IsStart() {
			switch cs.opt.RequestMode {
			case "normal":
				go worker.RunNormalMode(cs.ctx)
			default:
				log.Error("Invalid request mode, exit !!!")
				return errors.Errorf("Invalid request mode!!!")
			}
			time.Sleep(time.Millisecond * 17)
		}
	}

	return nil
}

func (cs *ClientSimulator) StopWorker(id string) error {
	cs.Lock()
	defer cs.Unlock()

	if w, ok := cs.workers[id]; ok {
		if w != nil && w.IsStart() {
			w.Close()
		}
	}

	return nil
}

func (cs *ClientSimulator) StopAllWorkers() error {
	cs.Lock()
	defer cs.Unlock()

	return cs.StopAllWorkersLocked()
}

func (cs *ClientSimulator) StopAllWorkersLocked() error {
	for _, w := range cs.workers {
		w.Close()
	}

	return nil
}

// Generate workers
func (cs *ClientSimulator) GenerateWorkers(count uint64) error {
	cs.Lock()
	defer cs.Unlock()

	IDPrefix := cs.opt.HttpAddr + "-"
	for i := 1; uint64(i) <= count; i++ {
		newIdx := atomic.AddUint64(&cs.workerCounter, 1)
		newWorkerID := IDPrefix + strconv.FormatUint(newIdx, 10)

		log.Infof("New worker :%v", newWorkerID)

		tmpWorker := NewWorker(newWorkerID, i, cs)
		cs.workers[newWorkerID] = tmpWorker
	}

	log.Info("Done generating workers, sleep for a while ....")
	time.Sleep(time.Second * 3)

	return nil
}

func (cs *ClientSimulator) Close() {
	cs.Lock()
	defer cs.Unlock()

	cs.StopAllWorkersLocked()

	defer cs.gClient.Close()
}
