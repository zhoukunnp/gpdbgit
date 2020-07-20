package ltsclient

import (
	"context"
	"sync"

	log "github.com/sirupsen/logrus"
	"google.golang.org/grpc"
	"google.golang.org/grpc/connectivity"

	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"strings"
	"time"
)

type GRPCClient struct {
	sync.RWMutex
	ctx         context.Context
	name        string
	clusterID   uint32
	serverAddrs []string
	serverIdx   int
	isConnected bool
	conn        *grpc.ClientConn
	client      ltsrpc.LTSClient
	opt         *ClientOption
	cs          *ClientSimulator
}

func NewGRPCClient(opt *ClientOption, ctx context.Context, name string, clusterID uint32, serverAddr string) *GRPCClient {
	addrs := strings.Split(serverAddr, ",")

	return &GRPCClient{
		opt:         opt,
		ctx:         ctx,
		name:        name,
		clusterID:   clusterID,
		serverAddrs: addrs,
		serverIdx:   0,
		isConnected: false,
		conn:        nil,
		client:      nil,
		cs:          nil,
	}
}

func (gc *GRPCClient) GetCurrentServerAddr() string {
	return gc.serverAddrs[gc.serverIdx]
}

func (gc *GRPCClient) GetNextServerAddrLocked() string {
	gc.serverIdx++

	if gc.serverIdx >= len(gc.serverAddrs) {
		gc.serverIdx = 0
	}

	log.Infof("LTS server addr, idx: %v, addrs: %v", gc.serverIdx, gc.serverAddrs)
	return gc.serverAddrs[gc.serverIdx]
}

func (gc *GRPCClient) Connect() error {
	gc.Lock()
	defer gc.Unlock()

	return gc.ConnectLocked()
}

func (gc *GRPCClient) ConnectLocked() error {
	ltsAddr := gc.GetCurrentServerAddr()

	for {
		log.Infof("%v ready connect to new lts server : %v", gc.name, ltsAddr)
		var opts []grpc.DialOption
		opts = append(opts, grpc.WithInsecure())

		opts = append(opts, grpc.WithInitialWindowSize(int32(gc.opt.InitialWindowSize)))
		opts = append(opts, grpc.WithInitialConnWindowSize(int32(gc.opt.InitialConnWindowSize)))

		opts = append(opts, grpc.WithWriteBufferSize(gc.opt.WriteBufferSize))
		opts = append(opts, grpc.WithReadBufferSize(gc.opt.ReadBufferSize))

		conn, err := grpc.Dial(ltsAddr, opts...)

		if err != nil {
			gc.CloseLocked()
			log.Errorf("Failed to connect to lts server: %v, err:%v", ltsAddr, err)
			time.Sleep(1 * time.Second)
			ltsAddr = gc.GetNextServerAddrLocked()
			continue
		}

		req := &ltsrpc.GetMembersRequest{
			Header: &ltsrpc.RequestHeader{
				ClusterId: gc.opt.ClusterID,
			},
		}

		gc.conn = conn
		gc.client = ltsrpc.NewLTSClient(conn)

		resp, err := gc.client.GetMembers(context.Background(), req)
		if err != nil {
			gc.CloseLocked()
			log.Infof("Failed to connect to new lts server: %v, error: %v", ltsAddr, err)
			time.Sleep(time.Second * 1)
			ltsAddr = gc.GetNextServerAddrLocked()
			continue
		}

		if resp.GetHeader().Status.Code == ltsrpc.StatusCode_SC_NOT_LEADER {
			gc.CloseLocked()
			log.Infof("Chosen lts [%v] is not leader. To get next lts server..,", ltsAddr)
			time.Sleep(time.Second * 1)
			ltsAddr = gc.GetNextServerAddrLocked()
			continue
		}

		log.Debugf("%v connect to lts server: %v OK !!!", gc.name, ltsAddr)
		break
	}

	return nil
}

func (gc *GRPCClient) Close() error {
	gc.Lock()
	defer gc.Unlock()

	return gc.CloseLocked()
}

func (gc *GRPCClient) CloseLocked() error {
	if gc.conn == nil {
		return nil
	}

	if closeErr := gc.conn.Close(); closeErr != nil {
		log.Debugf("Failed to close conn due to :%v", closeErr)
		for {
			if gc.conn.GetState() != connectivity.Shutdown {
				log.Warnf("Still waiting for the conn to shut down...")
				time.Sleep(time.Second * 1)
				continue
			}
			log.Debugf("conn has been closed...")
			break
		}
	}
	return nil
}
