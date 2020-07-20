package ltsserver

import (
	"bufio"
	"encoding/binary"
	"io"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltscore"
	"net"
	"runtime"
	"sync"
	"sync/atomic"
	"time"

	"context"
	"github.com/gogo/protobuf/proto"
	log "github.com/sirupsen/logrus"
)

// RawTxnServer is the dedicated module for serving txn timestamp request
type RawTxnServer struct {
	serveWG   sync.WaitGroup
	serveCtxs sync.Map // remote addr as key
	lis       *net.TCPListener
	svr       *Server
	isServing int32
}

func (svr *Server) NewRawTxnServer() *RawTxnServer {
	return &RawTxnServer{
		svr:       svr,
		lis:       nil,
		isServing: 0,
	}
}

func (rs *RawTxnServer) Start() error {
	tcpAddr, resolveErr := net.ResolveTCPAddr("tcp", rs.svr.ServerCfg.TxntsServiceUrl)
	if resolveErr != nil {
		log.Errorf("Resolve txnts service url err: %v", resolveErr)
		return resolveErr
	}

	tcpListener, listenErr := net.ListenTCP("tcp", tcpAddr)
	if listenErr != nil {
		log.Errorf("Listen txnts service port failed: %v", listenErr)
		return listenErr
	}

	rs.lis = tcpListener
	atomic.StoreInt32(&rs.isServing, 1)

	go rs.Serve()
	go rs.BackgroundJob()

	return nil
}

// Each TxnServeCtx corresponds to one connection
type TxnServeCtx struct {
	ctx             context.Context
	cancelFunc      context.CancelFunc

	isServing       int32
	tcpConn         *net.TCPConn
	remoteAddr      string
	msgBuffer       *ltscore.RingBuffer
	serveWG         sync.WaitGroup
	svr             *Server
	consumerWaiting int32    // If consumer is waiting, set to 1
	signalChan      chan int // 1 for wakeup signal, -1 for quit signal
}

func (rs *RawTxnServer) NewTxnServeCtx(conn *net.TCPConn) *TxnServeCtx {
	bucketSize := ltsconf.Cfg().TCP.TCPMaxHeaderLength + ltsconf.Cfg().TCP.TCPMaxBodyLength
	ctx, cancelFunc := context.WithCancel(context.Background())
	return &TxnServeCtx{
		ctx:             ctx,
		cancelFunc:      cancelFunc,

		isServing:       1,
		tcpConn:         conn,
		remoteAddr:      conn.RemoteAddr().String(),
		svr:             rs.svr,
		signalChan:      make(chan int, 1),
		consumerWaiting: 1,
		msgBuffer:       ltscore.NewRingBuffer(uint64(bucketSize), uint64(ltsconf.Cfg().TCP.TCPRingbufCapacity)),
	}
}

func (rs *RawTxnServer) Serve() error {
	rs.serveWG.Add(1)
	defer rs.serveWG.Done()

	log.Infof("Ready to serve raw Txn connections")

	for {
		tcpConn, acceptErr := rs.lis.AcceptTCP()
		if acceptErr != nil {
			log.Errorf("Accept tcp connection err: %v", acceptErr)
			if atomic.LoadInt32(&rs.isServing) == 0 {
				log.Warnf("Txn server is closed. Exit listen loop...")
				return nil
			}
			continue
		}

		log.Infof("Established a Txn tcp conn from: %v", tcpConn.RemoteAddr())

		// Launch a new serve ctx to serve this connection
		newCtx := rs.NewTxnServeCtx(tcpConn)
		rs.serveCtxs.Store(newCtx.remoteAddr, newCtx)

		rs.serveWG.Add(1)

		go func() {
			newCtx.ConfigureTCPConn()
			newCtx.MonitorLoop()
			newCtx.SendLoop()
			newCtx.ServeLoop()
			rs.serveWG.Done()
		}()
	}

	return nil
}

func (tctx TxnServeCtx) ConfigureTCPConn() {
	tctx.tcpConn.SetKeepAlive(true)
	tctx.tcpConn.SetKeepAlivePeriod(time.Minute)
	tctx.tcpConn.SetReadBuffer(ltsconf.Cfg().TCP.TCPReadBufferSize)
	tctx.tcpConn.SetWriteBuffer(ltsconf.Cfg().TCP.TCPWriteBufferSize)
	tctx.tcpConn.SetNoDelay(ltsconf.Cfg().TCP.TCPSetNoDelay != 0)
}

// The receiver loop of the serve ctx and the producer of the ring buffer
func (tctx *TxnServeCtx) ServeLoop() {
	tctx.serveWG.Add(1)
	defer func() {
		if r := recover(); r != nil {
			log.Warnf("Capture the panicking when exiting serve ctx %v :%v",
				tctx.remoteAddr, r)
		}

		tctx.serveWG.Done()
		log.Debugf("Exit servectx loop: The connection with: %v has been closed..", tctx.remoteAddr)
		tctx.Close()
	}()

	// |<- 2 Bytes ->|<- n Bytes ->|
	// | Body Length | Body Entity |
	connReader := bufio.NewReader(tctx.tcpConn)
	// ringbufBatchProduceLimit := ltsconf.Cfg().TCP.TCPRingbufBatchProduceLimit

	for {
		// The 'producer' for the msg ring buffer
		WritePos := tctx.msgBuffer.WriteCursor

		if tctx.msgBuffer.IsFull() {
			if atomic.LoadInt32(&tctx.isServing) == 0 {
				log.Warnf("Exit Serve Ctx of %v", tctx.remoteAddr)

				select {
				case tctx.signalChan <- -1:
				default:
				}

				return
			}

			// Ring buffer is full, sync the write cursor with the stub one
			// tctx.msgBuffer.SetWriteCursor(stubWritePos)

			// Then, yield the processor
			runtime.Gosched()
			continue
		}

		// Calculate the next bucket position in the ring buffer to write to.
		// tmpBuf is the reference of that to-be-overwritten bucket.
		beg := WritePos * tctx.msgBuffer.BucketSize
		tmpBuf := tctx.msgBuffer.Buf[beg : beg+tctx.msgBuffer.BucketSize]

		// Read from the socket until tmpBuf is filled up.
		_, recvErr := io.ReadFull(connReader, tmpBuf)
		if recvErr != nil {
			log.Errorf("Recv data error: %v", recvErr)
			return
		}

		// Push forward the stub write cursor => produce one element to the ring buffer
		tctx.msgBuffer.IncWriteCursor()

		// Batch size limit meets. Enable the consumer to sense this portion of unhandled
		// buckets by syncing the write cursor with the stub write cursor.
		// In addition, send wake-up signal to the consumer if necessary.
		if atomic.CompareAndSwapInt32(&tctx.consumerWaiting, 1, 0) {
			// tctx.msgBuffer.SetWriteCursor(newStubPos)
			tctx.signalChan <- 1
		}
	}
}

// The sender loop of the sever ctx and the consumer of the ring buffer
func (tctx *TxnServeCtx) SendLoop() {
	maxHeaderLength := ltsconf.Cfg().TCP.TCPMaxHeaderLength
	maxRespBodyLength := ltsconf.Cfg().TCP.TCPMaxBodyLength

	tctx.serveWG.Add(1)
	go func() {
		defer func() {
			if r := recover(); r != nil {
				log.Warnf("Capture the panicking when exiting send loop: %v :%v",
					tctx.remoteAddr, r)
			}

			tctx.serveWG.Done()
			log.Debugf("tctx :%v send loop wg done", tctx.remoteAddr)
			tctx.Close()
		}()

		// Pre-allocate reusable objects
		sendBufLen := maxHeaderLength + maxRespBodyLength
		sendBuf := make([]byte, sendBufLen, sendBufLen)
		resp := &ltsrpc.GetTxnTimestampCtx{}

		for {
			if tctx.msgBuffer.IsEmpty() {
				if atomic.LoadInt32(&tctx.isServing) == 0 {
					log.Warnf("Serve Ctx is no longer serving. Exit send loop of %v", tctx.remoteAddr)
					return
				}

				// Set consumerWaiting to be true, and then blocks at the signalChan
				atomic.StoreInt32(&tctx.consumerWaiting, 1)

				select {
				case sig := <-tctx.signalChan:
					if sig == -1 {
						log.Warnf("Receive exit signal. Quit send loop of %v", tctx.remoteAddr)
						return
					}
				}
			}

			// Fetch the next unhandled data in ring buffer and push forward the read cursor
			if !tctx.msgBuffer.Read(sendBuf) {
				continue
			}

			dataLen := binary.BigEndian.Uint16(sendBuf[:maxHeaderLength])
			if uint32(dataLen) > maxRespBodyLength {
				log.Errorf("Illegal dataLen: %v", dataLen)
				continue
			}

			// Next, unmarshal the bytes into pb message and assign values
			unmarshalErr := proto.Unmarshal(sendBuf[maxHeaderLength:maxHeaderLength+uint32(dataLen)], resp)
			if unmarshalErr != nil {
				log.Errorf("Server Unmarshal err: %v", unmarshalErr)
				continue
			}

			// Next, obtain a timestamp and assign it to the resp
			res, _ := tctx.svr.GetTimestamp(1)
			if resp.TxnId == 0 {
				// The timestamp is recommended to used as the transaction ID.
				resp.TxnId = res
			}
			resp.TxnTs = res

			// Next, marshal the body length and entity into the sendBuf
			respBuf, marshalErr := proto.Marshal(resp)
			if marshalErr != nil {
				log.Errorf("Marshal resp error: %v", marshalErr)
				continue
			}

			respLen := copy(sendBuf[maxHeaderLength:], respBuf)
			binary.BigEndian.PutUint16(sendBuf[:maxHeaderLength], uint16(respLen))

			// Finally, send it back to client.
			_, sendErr := tctx.tcpConn.Write(sendBuf)
			if sendErr != nil {
				log.Errorf("Send response error, exit send loop, error: %v", sendErr)
				return
			}
		}
	}()
}

func (tctx *TxnServeCtx) MonitorLoop() {
	tctx.serveWG.Add(1)
	go func() {
		defer tctx.serveWG.Done()

		ticker := time.NewTicker(time.Second * 10)
		defer ticker.Stop()

		log.Info("begin to monitor....")
		for {
			select {
			case <-ticker.C:
				log.Info("Throughput: \n")

			case <-tctx.ctx.Done():
				log.Warn("Exit monitor loop...")
				return
			}

			if atomic.LoadInt32(&tctx.isServing) == 0 {
				log.Warn("Exit monitor loop...")
				return
			}
		}
	}()
}

func (tctx *TxnServeCtx) Close() {
	if tctx == nil {
		log.Infof("nil txnts serve ctx...")
		return
	}

	// CAS here ensures the cancel and close would only be executed once.
	if !atomic.CompareAndSwapInt32(&tctx.isServing, 1, 0) {
		log.Infof("txnts serve ctx: %v has been closed...", tctx.remoteAddr)
		return
	}

	tctx.cancelFunc()
	close(tctx.signalChan)
	if closeErr := tctx.tcpConn.Close(); closeErr != nil {
		log.Warnf("Close %v tcp conn err: %v", tctx.remoteAddr, closeErr)
	}

	tctx.serveWG.Wait()
	tctx.msgBuffer = nil

	log.Infof("Txn serve ctx shuts down: %v", tctx.remoteAddr)
}

func (rs *RawTxnServer) BackgroundJob() {
	ticker := time.NewTicker(time.Second * 3)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			rs.ResourceReclaim()
		}

		if atomic.LoadInt32(&rs.isServing) == 0 {
			log.Warnf("Exit RawTxnServer background job loop...")
			return
		}
	}
}

// Release the ring buffer space of closed connection
func (rs *RawTxnServer) ResourceReclaim() {
	if rs == nil || atomic.LoadInt32(&rs.isServing) == 0 {
		return
	}

	rs.serveCtxs.Range(func(key, value interface{}) bool {
		ctx := value.(*TxnServeCtx)
		if atomic.LoadInt32(&ctx.isServing) == 0 {
			log.Debugf("To reclaim the resource of raw txnts conn: %v", key)
			ctx.msgBuffer = nil
			rs.serveCtxs.Delete(key)
		}
		return true
	})
}

func (rs *RawTxnServer) ShutDown() {
	if rs == nil {
		log.Infof("No Txn service to be shut down ...")
		return
	}

	atomic.StoreInt32(&rs.isServing, 0)

	// ShutDown all serve ctxs
	rs.serveCtxs.Range(func(key, value interface{}) bool {
		value.(*TxnServeCtx).Close()
		return true
	})

	log.Infof("All Txn serve ctxs are closed...")

	rs.serveCtxs = sync.Map{}

	log.Infof("To close listener..")
	if closeErr := rs.lis.Close(); closeErr != nil {
		log.Warnf("close listener err: %v", closeErr)
	}

	rs.serveWG.Wait()
	log.Infof("RawTxnServer has been shut down ....")
}
