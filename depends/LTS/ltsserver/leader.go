package ltsserver

import (
	"context"
	"math/rand"
	"path"
	"strings"
	"sync/atomic"
	"time"

	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltsetcdutil"
	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltslogutil"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"

	"fmt"
	"github.com/coreos/etcd/clientv3"
	"github.com/coreos/etcd/mvcc/mvccpb"
	"github.com/juju/errors"
	log "github.com/sirupsen/logrus"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
)

// IsLeader returns whether server is leader or not.
func (svr *Server) IsLeaderServer() bool {
	return svr.ID() == svr.GetLeader().GetMemberId()
}

func (s *Server) EnableLeader() {
	s.Leader.Store(s.MemberInfo)
}

func (s *Server) DisableLeader() {
	s.Leader.Store(&ltsrpc.Member{})
}

func (s *Server) GetEtcdLeader() uint64 {
	return s.ETCD.Server.Lead()
}

func (svr *Server) GetLeaderPath() string {
	return path.Join(svr.RootPath, "leader")
}

func (svr *Server) GetMemberLeaderPriorityPath(id uint64) string {
	return path.Join(svr.RootPath, fmt.Sprintf("member/%d/leader_priority", id))
}

func (svr *Server) StartLeaderLoop() {
	svr.LeaderLoopCtx, svr.LeaderLoopCancel = context.WithCancel(context.Background())
	svr.LeaderLoopWg.Add(2)
	go svr.LeaderLoop()
	go svr.ETCDLeaderLoop()
}

func (svr *Server) StopLeaderLoop() {
	svr.LeaderLoopCancel()
	svr.LeaderLoopWg.Wait()
	log.Infof("StopLeaderLoop finished ...")
}

func (svr *Server) LeaderLoop() {
	defer ltslogutil.LogPanic()
	defer svr.LeaderLoopWg.Done()

	for {

		if svr.IsClosed() {
			log.Infof("Server is closed, exit leader loop")
			return
		}

		if svr.GetEtcdLeader() == 0 {
			log.Warnf("Not found etcd leader yet. Check later.")
			time.Sleep(200 * time.Millisecond)
			continue
		}

		leader, rev, err := LoadLeader(svr.GetClient(), svr.GetLeaderPath())
		if err != nil {
			log.Errorf("Get leader err %v", err)
			time.Sleep(200 * time.Millisecond)
			continue
		}
		if leader != nil {
			if svr.IsSameLeader(leader) {
				// we are already leader, we may meet something wrong
				// in previous campaignLeader. We can delete and campaign again.
				log.Warnf("Leader is still %v, delete and campaign again", leader)
				if err = svr.DeleteLeaderKey(); err != nil {
					log.Errorf("Delete leader key err %v", err)
					time.Sleep(200 * time.Millisecond)
					continue
				}
			} else {
				log.Infof("Current leader is %v, watch it", leader)
				svr.WatchLeader(leader, rev)
				log.Info("Exit WatchLeader loop. Leader may step down. Try to campaign leader.")
			}
		}

		etcdLeader := svr.ETCD.Server.Lead()
		if etcdLeader != svr.ID() {
			log.Warnf("Mismatched server ID (%v) and etcd leader ID (%v)", etcdLeader, svr.ID())
			time.Sleep(200 * time.Millisecond)
			continue
		}

		if err = svr.CampaignLeader(); err != nil {
			log.Errorf("Campaign leader err %v", errors.ErrorStack(err))
		}
	}
}

func (svr *Server) ETCDLeaderLoop() {
	defer ltslogutil.LogPanic()
	defer svr.LeaderLoopWg.Done()

	ctx, cancel := context.WithCancel(svr.LeaderLoopCtx)
	defer cancel()

	for {
		select {
		case <-time.After(ltsconf.Cfg().LeaderPriorityCheckInterval.Duration):
			etcdLeader := svr.GetEtcdLeader()
			if etcdLeader == svr.ID() || etcdLeader == 0 {
				break
			}

			myPriority, err := svr.GetMemberLeaderPriority(svr.ID())
			if err != nil {
				log.Errorf("Failed to load leader priority: %v", err)
				break
			}

			leaderPriority, err := svr.GetMemberLeaderPriority(etcdLeader)
			if err != nil {
				log.Errorf("Failed to load leader priority: %v", err)
				break
			}

			if myPriority > leaderPriority {
				err := svr.ETCD.Server.MoveLeader(ctx, etcdLeader, svr.ID())
				if err != nil {
					log.Errorf("Failed to transfer etcd leader: %v", err)
				} else {
					log.Infof("Etcd leader moved from %v to %v", etcdLeader, svr.ID())
				}
			}
		case <-ctx.Done():
			log.Warnf("ETCDLeaderLoop is done.")
			return
		}
	}
}

// LoadLeader restores server leader from etcd.
func LoadLeader(c *clientv3.Client, leaderPath string) (*ltsrpc.Member, int64, error) {
	leader := &ltsrpc.Member{}
	ok, rev, err := LoadProtoMsgWithModRev(c, leaderPath, leader)
	if err != nil {
		return nil, 0, errors.Trace(err)
	}
	if !ok {
		log.Warnf("Leader not found by LeaderPath: %v", leaderPath)
		return nil, 0, nil
	}

	return leader, rev, nil
}

// GetLeader returns meta cluster leader.
func (svr *Server) GetLeader() *ltsrpc.Member {
	leader := svr.Leader.Load()
	if leader == nil {
		return nil
	}
	member := leader.(*ltsrpc.Member)
	if member.GetMemberId() == 0 {
		return nil
	}
	return member
}

func (svr *Server) IsSameLeader(leader *ltsrpc.Member) bool {
	return leader.GetMemberId() == svr.ID()
}

func (svr *Server) MarshalMemberInfo() (member *ltsrpc.Member, marshalStr string) {
	leader := &ltsrpc.Member{
		Name:       svr.Name(),
		MemberId:   svr.ID(),
		ClientUrls: strings.Split(svr.ServerCfg.AdvertiseClientUrls, ","),
		PeerUrls:   strings.Split(svr.ServerCfg.AdvertisePeerUrls, ","),
	}

	data, err := leader.Marshal()
	if err != nil {
		// Panic here
		log.Fatal("marshal leader error: %v", err)
	}

	return leader, string(data)
}

func (svr *Server) CampaignLeader() error {
	log.Infof("LTS server: %v begins to campaign leader", svr.Name())

	lessor := clientv3.NewLease(svr.GetClient())
	defer lessor.Close()

	start := time.Now()
	ctx, cancel := context.WithTimeout(svr.GetClient().Ctx(), ltsconf.Cfg().SvrConfig.RequestTimeout.Duration)
	leaseResp, err := lessor.Grant(ctx, ltsconf.Cfg().LeaderLease)
	cancel()

	if cost := time.Since(start); cost > ltsconf.Cfg().SvrConfig.SlowRequestTime.Duration {
		log.Warnf("Lessor grants too slow, cost %v", cost)
	}

	if err != nil {
		log.Errorf("Error: %v", err)
		return errors.Trace(err)
	}

	leaderKey := svr.GetLeaderPath()
	resp, err := svr.Txn().
		If(clientv3.Compare(clientv3.CreateRevision(leaderKey), "=", 0)).
		Then(clientv3.OpPut(leaderKey, svr.LeaderValue, clientv3.WithLease(clientv3.LeaseID(leaseResp.ID)))).
		Commit()

	if err != nil {
		return errors.Trace(err)
	}

	if !resp.Succeeded {
		return errors.New("Campaign leader failed, other server may succeed")
	}

	// Make the leader keep-alive.
	ctx, cancel = context.WithCancel(svr.LeaderLoopCtx)
	defer cancel()

	keepaliveChan, err := lessor.KeepAlive(ctx, clientv3.LeaseID(leaseResp.ID))
	if err != nil {
		return errors.Trace(err)
	}
	log.Infof("%v campaigns to be LEADER !!", svr.Name())

	if err = svr.SyncTimestamp(); err != nil {
		return err
	}
	defer svr.TS.Store(&TimestampEntity{
		physicalNano: uint64(0),
		physicalSec:  uint64(0),
		logical:      uint64(0),
	})

	// Prepare the dedicated Txn Engine for txn ts service
	// Since this service can only be offered by leader after the saved timestamp is synced
	txnServer := svr.NewRawTxnServer()
	svr.TxntsServer = txnServer

	if startErr := txnServer.Start(); startErr != nil {
		log.Errorf("start Txn service failed: %v", startErr)
		return startErr
	}
	defer txnServer.ShutDown()

	svr.EnableLeader()
	defer svr.DisableLeader()

	log.Infof("LTS Cluster leader: %v is ready to serve", svr.Name())

	// Currently, update the persisted timestamp every 1s
	tsTicker := time.NewTicker(ltsconf.Cfg().SvrConfig.UpdateTimestampInterval.Duration)
	defer tsTicker.Stop()

	// go svr.CollectThroughput(ctx)

	for {
		select {
		case _, ok := <-keepaliveChan:
			if !ok {
				log.Warnf("Keep alive channel is closed")
				return nil
			}
		case <-tsTicker.C:
			if err = svr.UpdateTimestamp(); err != nil {
				log.Errorf("Update timestamp error: %v", err)
				return err
			}
			etcdLeaderID := svr.EtcdLeaderID()
			if etcdLeaderID != svr.ID() {
				log.Errorf("Etcd leader changed, %v resigns leadership", svr.Name())
				return nil
			}
		case <-svr.ResignCh:
			log.Infof("Exit campaignLeader for %v resigns leadership", svr.Name())
			return nil
		case <-ctx.Done():
			log.Infof("Exit campaignLeader for %v is closed", svr.Name())
			return nil
		}
	}
}

// Only used in testing
func (svr *Server) CollectThroughput(ctx context.Context) {
	throughputTicker := time.NewTicker(time.Second * 10)
	defer throughputTicker.Stop()

	prev := uint64(0)
	for {
		select {
		case <-throughputTicker.C:
			currentSnapShot := atomic.LoadUint64(&svr.TxnTSRespCount)
			result := (currentSnapShot - prev) / 10
			prev = currentSnapShot

			log.Infof("GetTxnTS throughput: %v/s", result)

		case <-ctx.Done():
			return
		}
	}

}

// GetEtcdLeader returns the etcd leader ID.
func (svr *Server) EtcdLeaderID() uint64 {
	return svr.ETCD.Server.Lead()
}

func (svr *Server) WatchLeader(leader *ltsrpc.Member, rev int64) {
	svr.Leader.Store(leader)
	defer svr.Leader.Store(&ltsrpc.Member{})

	watcher := clientv3.NewWatcher(svr.GetClient())
	defer watcher.Close()

	ctx, cancel := context.WithCancel(svr.LeaderLoopCtx)
	defer cancel()

	for {
		rch := watcher.Watch(ctx, svr.GetLeaderPath(), clientv3.WithRev(rev))
		for wresp := range rch {
			if wresp.CompactRevision != 0 {
				log.Warnf("Required revision: %v has been compacted, use the compact revision: %v",
					rev, wresp.CompactRevision)

				rev = wresp.CompactRevision
				break
			}

			if wresp.Canceled {
				log.Errorf("Leader watch is canceled with revision: %v", rev)
				return
			}

			for _, ev := range wresp.Events {
				if ev.Type == mvccpb.DELETE {
					log.Info("Leader is deleted")
					return
				}
			}
		}

		select {
		case <-ctx.Done():
			return
		default:
		}
	}
}

// ResignLeader resigns current LTS cluster's leadership.
// If nextLeader is empty, all other LTS servers can campaign.
func (svr *Server) ResignLeader(nextLeader string) error {
	log.Infof("%v tries to resign leader with next leader: %v", svr.Name(), nextLeader)
	// Determine next leaders.
	var leaderIDs []uint64
	res, err := ltsetcdutil.ListEtcdMembers(svr.GetClient(), ltsconf.Cfg().Etcd.KVRequestTimeout.Duration)
	if err != nil {
		return errors.Trace(err)
	}

	for _, member := range res.Members {
		if (nextLeader == "" && member.ID != svr.ServerID) || (nextLeader != "" && member.Name == nextLeader) {
			leaderIDs = append(leaderIDs, member.GetID())
		}
	}
	if len(leaderIDs) == 0 {
		return errors.New("No valid lts server to transfer leader")
	}

	nextLeaderID := leaderIDs[rand.Intn(len(leaderIDs))]
	log.Infof("%v ready to resign leader, next leader: %v", svr.Name(), nextLeaderID)
	err = svr.ETCD.Server.MoveLeader(svr.LeaderLoopCtx, svr.ID(), nextLeaderID)
	if err != nil {
		return errors.Trace(err)
	}

	// Resign leader.
	select {
	case svr.ResignCh <- struct{}{}:
		return nil
	case <-time.After(ltsconf.Cfg().SvrConfig.ResignLeaderTimeout.Duration):
		return errors.Errorf("Failed to send resign signal, maybe not leader")
	}
}

func (svr *Server) DeleteLeaderKey() error {
	// delete leader itself and let others start a new election again.
	leaderKey := svr.GetLeaderPath()
	resp, err := svr.LeaderTxn().Then(clientv3.OpDelete(leaderKey)).Commit()
	if err != nil {
		return errors.Trace(err)
	}
	if !resp.Succeeded {
		return errors.New("Resign leader failed, this server is not leader anymore")
	}

	return nil
}

// LeaderCmp returns a etcd cmp struct to check leadership
func (svr *Server) LeaderCmp() clientv3.Cmp {
	return clientv3.Compare(clientv3.Value(svr.GetLeaderPath()), "=", svr.LeaderValue)
}
