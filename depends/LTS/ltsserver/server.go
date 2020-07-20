package ltsserver

import (
	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltsetcdutil"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltscore"

	"math/rand"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/coreos/etcd/clientv3"
	"github.com/coreos/etcd/embed"
	"github.com/coreos/etcd/pkg/types"
	"github.com/juju/errors"
	log "github.com/sirupsen/logrus"

	"context"
	"github.com/gogo/protobuf/proto"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

type Server struct {
	// Server state.
	IsServing            int64
	Handler              *Handler
	ResignCh             chan struct{} // For resign notify.

	ServerCfg            *ltsconf.Config
	ETCDCfg              *embed.Config // Etcd and cluster information.
	ETCD                 *embed.Etcd
	KV                   *ltscore.KV
	ETCDClient           *clientv3.Client
	ServerID             uint64 // etcd server id.
	ClusterID            uint32 // cluster id

	RootPath             string // server root path for etcd.
	Leader               atomic.Value
	MemberInfo           *ltsrpc.Member
	LeaderValue          string // leader value saved in etcd as leader key.
	LeaderLoopCtx        context.Context
	LeaderLoopCancel     func()
	LeaderLoopWg         sync.WaitGroup
	TxntsServer          *RawTxnServer // For transaction timestamp service

	TS                   atomic.Value
	PhysicalCacheBarrier time.Time // PhysicalCacheBarrier is the cached physicalNano barrier
	TSSavePath           string

	// For collecting throughput statistics
	TxnTSRespCount       uint64
	TxnTSReqCount        uint64
}

// CreateServer creates a server
func CreateServer(cfg *ltsconf.Config, apiRegister func(*Server) http.Handler) (*Server, error) {
	rand.Seed(time.Now().UnixNano())

	s := &Server{
		ResignCh:       make(chan struct{}),
		ServerCfg:      cfg,
		TxnTSReqCount:  uint64(0),
		TxnTSRespCount: uint64(0),
		TxntsServer:    nil,
	}

	s.Handler = newHandler(s)
	etcdCfg, err := s.ServerCfg.GenEmbedEtcdConfig()
	if err != nil {
		return nil, errors.Trace(err)
	}
	if apiRegister != nil {
		etcdCfg.UserHandlers = map[string]http.Handler{
			s.ServerCfg.SvrConfig.LTSAPIPrefix: apiRegister(s),
		}
	}

	etcdCfg.ServiceRegister = func(gs *grpc.Server) {
		ltsrpc.RegisterLTSServer(gs, s)
	}

	s.ETCDCfg = etcdCfg

	return s, nil
}

var timeMonitorOnce sync.Once

// Run is the main entrance for launching the server
func (svr *Server) Run() error {
	timeMonitorOnce.Do(func() {
		go StartMonitor(time.Now, func() {
			log.Errorf("system time jumps backward.")
		})
	})

	if err := svr.StartEtcd(); err != nil {
		return errors.Trace(err)
	}

	if err := svr.StartServer(); err != nil {
		return errors.Trace(err)
	}

	log.Info("Done preparing basic etcd and server environment. Ready to start leader loop.")

	svr.StartLeaderLoop()
	return nil
}

// Close closes the server.
func (svr *Server) Close() {
	if !atomic.CompareAndSwapInt64(&svr.IsServing, 1, 0) {
		log.Warnf("Server has already been shut down")
		return
	}

	log.Info("Server is set to no service")

	log.Info("To close txnts server")
	svr.TxntsServer.ShutDown()

	log.Info("To stop leader loop.")
	svr.StopLeaderLoop()

	log.Info("To close etcd.")
	if svr.ETCD != nil {
		svr.ETCD.Close()
	}

	log.Infof("To close etcd clients...")
	svr.ETCDClient.Close()

	log.Info("Server is fully closed")
}

// GetAddr returns the server urls for clients.
func (svr *Server) GetAddr() string {
	return svr.ServerCfg.AdvertiseClientUrls
}

// GetHandler returns the handler for API.
func (svr *Server) GetHandler() *Handler {
	return svr.Handler
}

// GetClient returns builtin etcd client.
func (svr *Server) GetClient() *clientv3.Client {
	return svr.ETCDClient
}

func (svr *Server) GetKV() *ltscore.KV {
	return svr.KV
}

// ID returns the unique etcd ID for this server in etcd cluster.
func (svr *Server) ID() uint64 {
	return svr.ServerID
}

// Name returns the unique etcd Name for this server in etcd cluster.
func (svr *Server) Name() string {
	return svr.ServerCfg.Name
}

// GetSecurityConfig get the security config.
func (svr *Server) GetSecurityConfig() *ltsconf.SecurityConfig {
	return &svr.ServerCfg.Security
}

// StartEtcd runs the underlying ETCD instance.
func (svr *Server) StartEtcd() error {
	log.Info("To start embed etcd...")
	etcd, err := embed.StartEtcd(svr.ETCDCfg)
	if err != nil {
		log.Errorf("Start etcd failed: %v", err)
		return errors.Trace(err)
	}

	// Check cluster ID
	urlmap, err := types.NewURLsMap(svr.ServerCfg.InitialCluster)
	if err != nil {
		return errors.Trace(err)
	}

	tlsConfig, err := svr.ServerCfg.Security.ToTLSConfig()
	if err != nil {
		return errors.Trace(err)
	}

	if err = ltsetcdutil.CheckClusterID(etcd.Server.Cluster().ID(), urlmap, tlsConfig); err != nil {
		return errors.Trace(err)
	}

	sc := make(chan os.Signal, 1)
	signal.Notify(sc,
		syscall.SIGHUP,
		syscall.SIGINT,
		syscall.SIGTERM,
		syscall.SIGQUIT)

	select {
	// Wait etcd until it is ready
	case <-etcd.Server.ReadyNotify():
	case sig := <-sc:
		return errors.Errorf("Receive signal %v when waiting embed etcd to be ready", sig)
	}

	// Create server's etcd client
	client, createClientErr := clientv3.New(clientv3.Config{
		Endpoints:   []string{svr.ETCDCfg.ACUrls[0].String()},
		DialTimeout: svr.ServerCfg.Etcd.EtcdTimeout.Duration,
		TLS:         tlsConfig,
	})

	if createClientErr != nil {
		log.Errorf("Create etcd client failed: %v", createClientErr)
		return createClientErr
	}

	svr.ETCDClient = client

	// Double check etcd cluster to ensure information consistency
	etcdServerID := uint64(etcd.Server.ID())
	etcdMembers, err := ltsetcdutil.ListEtcdMembers(svr.GetClient(), svr.ServerCfg.Etcd.KVRequestTimeout.Duration)
	if err != nil {
		return errors.Trace(err)
	}

	for _, m := range etcdMembers.Members {
		if etcdServerID == m.ID {
			etcdPeerURLs := strings.Join(m.PeerURLs, ",")
			if svr.ServerCfg.AdvertisePeerUrls != etcdPeerURLs {
				log.Infof("update advertise peer urls from %v to %v", svr.ServerCfg.AdvertisePeerUrls, etcdPeerURLs)
				svr.ServerCfg.AdvertisePeerUrls = etcdPeerURLs
			}
		}
	}

	svr.ServerID = etcdServerID
	svr.ETCD = etcd

	log.Infof("ETCD server has been started, ID: %v", etcdServerID)
	return nil
}

func (svr *Server) RemovePrefixFromUrls(urls []string, prefix string) []string {
	outUrls := make([]string, 0, len(urls))
	for _, s := range urls {
		if strings.HasPrefix(s, prefix) {
			outUrls = append(outUrls, s[len(prefix):])
		} else {
			outUrls = append(outUrls, s)
		}
	}

	return outUrls
}

// StartServer creates the server instance.
func (svr *Server) StartServer() error {
	var err error
	if err = svr.InitClusterID(); err != nil {
		return errors.Trace(err)
	}

	log.Infof("Get ClusterID: %v", svr.ClusterID)

	svr.RootPath = ltsconf.Cfg().SvrConfig.LTSRootPath
	svr.MemberInfo, svr.LeaderValue = svr.MarshalMemberInfo()
	svr.TSSavePath = ltsconf.Cfg().SvrConfig.LTSTimestampPath

	kvBase := NewEtcdKVBase(svr, svr.ETCDClient)
	svr.KV = ltscore.NewKV(kvBase)

	meta := &ltsrpc.Cluster{}
	ok, loadErr := svr.KV.LoadClusterMeta(meta)

	// Load cluster meta err
	if loadErr != nil {
		log.Errorf("Load cluster meta error: %v", loadErr)
		return loadErr
	}

	// No cluster meta found, create a new one and persist ie
	if !ok {
		meta = &ltsrpc.Cluster{
			ClusterId: svr.ClusterID,
		}

		value, _ := proto.Marshal(meta)
		_, putErr := KVPut(svr.ETCDClient, ltsconf.Cfg().SvrConfig.LTSClusterMetaPath, string(value))
		if putErr != nil {
			log.Errorf("Save new cluster meta error: %v", putErr)
			return putErr
		}
	}

	// Server has been started.
	atomic.StoreInt64(&svr.IsServing, 1)
	return nil
}

// InitClusterID creates a key for this cluster.
func (svr *Server) InitClusterID() error {
	// Get any cluster key to parse the cluster ID.
	resp, err := KVGet(svr.GetClient(), ltsconf.Cfg().SvrConfig.LTSRootPath, clientv3.WithFirstCreate()...)
	if err != nil {
		log.Errorf("KVGet failed: %v", err)
		return errors.Trace(err)
	}

	// If no key exist, generate a random cluster ID.
	if len(resp.Kvs) == 0 {
		log.Info("No cluster exists yet. Ready to create a new ClusterID...")
		svr.ClusterID, err = InitOrGetClusterID(svr.ETCDClient, ltsconf.Cfg().SvrConfig.LTSClusterIDPath)
		if err != nil {
			log.Errorf("init cluster ID failed: %v", err)
		}
		return errors.Trace(err)
	}

	key := string(resp.Kvs[0].Key)

	// If the key is "LTSClusterIDPath", parse the cluster ID from it.
	if key == svr.ServerCfg.SvrConfig.LTSClusterIDPath {
		svr.ClusterID, err = BytesToUint32(resp.Kvs[0].Value)
		return errors.Trace(err)
	}

	// Parse the cluster ID from any other keys for compatibility.
	elems := strings.Split(key, "/")
	if len(elems) < 3 {
		return errors.Errorf("invalid cluster key %v", key)
	}

	tmpID, err := strconv.ParseUint(elems[2], 10, 32)
	svr.ClusterID = uint32(tmpID)

	log.Infof("Init and load cluster id: %d", svr.ClusterID)
	return errors.Trace(err)
}

// IsClosed checks whether server is closed or not.
func (svr *Server) IsClosed() bool {
	return atomic.LoadInt64(&svr.IsServing) == 0
}

// txn returns an etcd client transaction wrapper.
// The wrapper will set a request timeout to the context and log slow transactions.
func (svr *Server) Txn() clientv3.Txn {
	return NewSlowLogTxn(svr.ETCDClient)
}

// LeaderTxn returns txn() with a leader comparison to guarantee that
// the transaction can be executed only if the server is leader.
func (svr *Server) LeaderTxn(cs ...clientv3.Cmp) clientv3.Txn {
	return svr.Txn().If(append(cs, svr.LeaderCmp())...)
}

func (svr *Server) ValidateRequest(header *ltsrpc.RequestHeader, isCheckCluster bool) *ltsrpc.ResponseHeader {
	if svr.IsClosed() {
		log.Errorf("Server is closed.")
		return svr.ErrorHeader(status.Errorf(codes.Unavailable, "Server not started"))
	}

	if !svr.IsLeaderServer() {
		log.Errorf("Current server is not leader.")
		return svr.NotLeaderHeader(svr.GetLeader().GetClientUrls())
	}

	if isCheckCluster && header.ClusterId != svr.ClusterID {
		log.Errorf("Request cluster %v not matched to expected %v, ", header.GetClusterId(), svr.ClusterID)
		return svr.ErrorHeader(status.Errorf(codes.InvalidArgument,
			"Mismatch ClusterId, expected %d but got %d", svr.ClusterID, header.GetClusterId()))
	}

	return nil
}

func (svr *Server) OKHeader(retMsg string) *ltsrpc.ResponseHeader {
	return &ltsrpc.ResponseHeader{
		ClusterId: svr.ClusterID,
		Status: &ltsrpc.Status{
			Code: ltsrpc.StatusCode_SC_OK,
			Msg:  retMsg,
		},
	}
}

func (svr *Server) ErrorHeader(err error) *ltsrpc.ResponseHeader {
	return &ltsrpc.ResponseHeader{
		ClusterId: svr.ClusterID,
		Status: &ltsrpc.Status{
			Code: ltsrpc.StatusCode_SC_ERROR,
			Msg:  err.Error(),
		},
	}
}

func (svr *Server) EmptyErrorHeader() *ltsrpc.ResponseHeader {
	return &ltsrpc.ResponseHeader{
		ClusterId: svr.ClusterID,
		Status: &ltsrpc.Status{
			Code: ltsrpc.StatusCode_SC_ERROR,
		},
	}
}

func (svr *Server) StatusHeader(status *ltsrpc.Status) *ltsrpc.ResponseHeader {
	return &ltsrpc.ResponseHeader{
		ClusterId: svr.ClusterID,
		Status:    status,
	}
}

func (svr *Server) NotLeaderHeader(leaderUrls []string) *ltsrpc.ResponseHeader {
	if len(leaderUrls) == 0 {
		return svr.StatusHeader(&ltsrpc.Status{
			Code: ltsrpc.StatusCode_SC_LEADER_NOT_FOUND,
			Msg:  "Not found leader.",
		})
	}

	lUrl := leaderUrls[0]
	if strings.HasPrefix(lUrl, "http://") {
		lUrl = lUrl[len("http://"):]
	}

	return svr.StatusHeader(&ltsrpc.Status{
		Code: ltsrpc.StatusCode_SC_NOT_LEADER,
		Msg:  lUrl,
	})
}

// GetCluster gets cluster.
func (svr *Server) GetCluster() *ltsrpc.Cluster {
	return &ltsrpc.Cluster{
		ClusterId: svr.ClusterID,
	}
}

// GetMemberLeaderPriority loads a member's priority to be elected as the etcd leader.
func (svr *Server) GetMemberLeaderPriority(id uint64) (int, error) {
	key := svr.GetMemberLeaderPriorityPath(id)
	res, err := KVGet(svr.GetClient(), key)
	if err != nil {
		return 0, errors.Trace(err)
	}
	if len(res.Kvs) == 0 {
		return 0, nil
	}
	priority, err := strconv.ParseInt(string(res.Kvs[0].Value), 10, 32)
	if err != nil {
		return 0, errors.Trace(err)
	}
	return int(priority), nil
}

// DeleteMemberLeaderPriority removes a member's priority config.
func (svr *Server) DeleteMemberLeaderPriority(id uint64) error {
	key := svr.GetMemberLeaderPriorityPath(id)
	res, err := svr.LeaderTxn().Then(clientv3.OpDelete(key)).Commit()
	if err != nil {
		return errors.Trace(err)
	}
	if !res.Succeeded {
		return errors.New("delete leader priority failed, maybe not leader")
	}
	return nil
}

// SetMemberLeaderPriority saves a member's priority to be elected as the etcd leader.
func (svr *Server) SetMemberLeaderPriority(id uint64, priority int) error {
	key := svr.GetMemberLeaderPriorityPath(id)
	res, err := svr.LeaderTxn().Then(clientv3.OpPut(key, strconv.Itoa(priority))).Commit()
	if err != nil {
		return errors.Trace(err)
	}
	if !res.Succeeded {
		return errors.New("save leader priority failed, maybe not leader")
	}
	return nil
}

// StartMonitor calls systimeErrHandler if system time jump backward.
func StartMonitor(now func() time.Time, systimeErrHandler func()) {
	log.Info("start system time monitor")
	tick := time.NewTicker(100 * time.Millisecond)
	defer tick.Stop()
	for {
		last := now().UnixNano()
		<-tick.C
		if now().UnixNano() < last {
			log.Errorf("system time jump backward, last:%v", last)
			systimeErrHandler()
		}
	}
}
