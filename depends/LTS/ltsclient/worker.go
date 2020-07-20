package ltsclient

import (
	"context"
	"sync"
	"time"

	log "github.com/sirupsen/logrus"

	"bufio"
	"encoding/binary"
	"github.com/golang/protobuf/proto"
	"io"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"net"
	"sync/atomic"
)

type Worker struct {
	sync.RWMutex
	ctx context.Context

	id       string
	idx      int
	isStart  bool
	cs       *ClientSimulator
	gClient  *GRPCClient
	quitChan chan struct{}
}

func NewWorker(idStr string, innerIdx int, cs *ClientSimulator) *Worker {
	w := &Worker{
		id:       idStr,
		idx:      innerIdx,
		isStart:  false,
		cs:       cs,
		quitChan: make(chan struct{}),
		gClient:  NewGRPCClient(cs.opt, cs.ctx, idStr, cs.opt.ClusterID, cs.opt.ServerAddr),
	}

	w.gClient.cs = cs
	return w
}

func (w *Worker) IsStart() bool {
	return w.isStart
}

const (
	WriteBuffer = 128 * 1024 * 1024
	ReadBuffer  = 128 * 1024 * 1024
)

func (w *Worker) RunNormalMode(ctx context.Context) {
	defer w.Close()
	w.ctx = ctx
	w.isStart = true

	log.Infof("Start running worker: %v", w.idx)
	tcpAddr, _ := net.ResolveTCPAddr("tcp", w.cs.opt.TxntsAddr)
	ltsConn, dialErr := net.DialTCP("tcp", nil, tcpAddr)
	if dialErr != nil {
		log.Errorf("Dial server error: %v", dialErr)
		return
	}

	// Configure the connection
	ltsConn.SetKeepAlive(true)
	ltsConn.SetKeepAlivePeriod(time.Minute * 1)
	ltsConn.SetNoDelay(w.cs.opt.TCPSetNoDelay != 0)
	ltsConn.SetWriteBuffer(WriteBuffer)
	ltsConn.SetReadBuffer(ReadBuffer)

	log.Infof("Dial server ok: %v, from: %v", ltsConn.RemoteAddr(), ltsConn.LocalAddr())
	defer ltsConn.Close()

	txnReq := &ltsrpc.GetTxnTimestampCtx{
		TxnId: 1,
		TxnTs: 1,
	}

	reqHeaderLength, reqBodyLength := w.cs.opt.ReqHeaderLength, w.cs.opt.ReqBodyLength

	// Prepare the sending buffer by marshaling the request entity into it
	sendBuf := make([]byte, reqHeaderLength+reqBodyLength, reqHeaderLength+reqBodyLength)
	dataBuf, _ := proto.Marshal(txnReq)
	dataLen := copy(sendBuf[reqHeaderLength:], dataBuf)
	binary.BigEndian.PutUint16(sendBuf[:reqHeaderLength], uint16(dataLen))

	// Recv buffer
	recvBuf := make([]byte, reqHeaderLength+reqBodyLength, reqHeaderLength+reqBodyLength)
	connReader := bufio.NewReader(ltsConn)

	// Receiver routine
	go func() {
		for {
			_, recvLenErr := io.ReadFull(connReader, recvBuf)
			if recvLenErr != nil {
				log.Errorf("Recv data error: %v", recvLenErr)
				return
			}

			// For collecting throughput
			atomic.AddUint64(&w.cs.txnTSRespCount, 1)
		}
	}()

	// Sender
	for {
		if _, sendErr := ltsConn.Write(sendBuf); sendErr != nil {
			log.Errorf("Send request err: %v", sendErr)
			return
		}

		//time.Sleep(time.Millisecond * 500)

		select {
		case <-ctx.Done():
			return
		default:
		}
	}
}

func (w *Worker) Close() {
	w.Lock()
	defer w.Unlock()

	if !w.isStart {
		return
	}

	w.isStart = false

	if w.gClient != nil {
		w.gClient.Close()
	}

	close(w.quitChan)
	log.Infof("Worker %v has been closed.", w.id)
}
