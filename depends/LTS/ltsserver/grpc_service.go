package ltsserver

import (
	"context"
	log "github.com/sirupsen/logrus"

	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
)

// GetMembers returns the cluster members
func (svr *Server) GetMembers(ctx context.Context, request *ltsrpc.GetMembersRequest) (*ltsrpc.GetMembersResponse, error) {
	if h := svr.ValidateRequest(request.GetHeader(), true); h != nil {
		return &ltsrpc.GetMembersResponse{
			Header: h,
		}, nil
	}

	members, err := GetMembers(svr.GetClient())
	if err != nil {
		return &ltsrpc.GetMembersResponse{
			Header: svr.ErrorHeader(err),
		}, nil
	}

	for _, m := range members {
		leaderPriority, e := svr.GetMemberLeaderPriority(m.GetMemberId())
		if e != nil {
			return &ltsrpc.GetMembersResponse{
				Header: svr.ErrorHeader(e),
			}, nil
		}

		m.PeerUrls = svr.RemovePrefixFromUrls(m.PeerUrls, "http://")
		m.ClientUrls = svr.RemovePrefixFromUrls(m.ClientUrls, "http://")

		m.LeaderPriority = int32(leaderPriority)
	}

	leader := svr.GetLeader()
	if err != nil {
		return &ltsrpc.GetMembersResponse{
			Header: svr.ErrorHeader(err),
		}, nil
	}
	if leader != nil {
		leader.PeerUrls = svr.RemovePrefixFromUrls(leader.PeerUrls, "http://")
		leader.ClientUrls = svr.RemovePrefixFromUrls(leader.ClientUrls, "http://")
	}

	var etcdLeader *ltsrpc.Member
	leadID := svr.ETCD.Server.Lead()
	for _, m := range members {
		if m.MemberId == leadID {
			etcdLeader = m
			break
		}
	}

	return &ltsrpc.GetMembersResponse{
		Header:     svr.OKHeader("Get members ok"),
		Members:    members,
		Leader:     leader,
		EtcdLeader: etcdLeader,
	}, nil
}

// Basic gRPC manner to obtain transaction timestamp
func (svr *Server) GetTxnTimestamp(ctx context.Context, resp *ltsrpc.GetTxnTimestampCtx) (*ltsrpc.GetTxnTimestampCtx, error) {
	timestamp, err := svr.GetTimestamp(1)
	if err != nil {
		log.Errorf("Failed to obtain ts due to :%v", err)
		resp.TxnId = 0
	} else {
		resp.TxnTs = timestamp
		if resp.TxnId == 0 {
			resp.TxnId = timestamp
		}
	}

	return resp, err
}
