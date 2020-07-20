package ltsetcdutil

import (
	"context"
	"crypto/tls"
	"net/http"
	"time"

	"github.com/coreos/etcd/clientv3"
	"github.com/coreos/etcd/etcdserver"
	"github.com/coreos/etcd/pkg/types"
	"github.com/juju/errors"
	log "github.com/sirupsen/logrus"
)

const (
	DefaultDialTimeout       = 30 * time.Second
	DefaultRequestTimeout    = 10 * time.Second
)

// CheckClusterID checks Etcd's cluster ID, returns an error if mismatch.
// This function will never block even quorum is not satisfied.
func CheckClusterID(localClusterID types.ID, um types.URLsMap, tlsConfig *tls.Config) error {
	if len(um) == 0 {
		return nil
	}

	var peerURLs []string
	for _, urls := range um {
		peerURLs = append(peerURLs, urls.StringSlice()...)
	}

	for _, u := range peerURLs {
		trp := &http.Transport{
			TLSClientConfig: tlsConfig,
		}
		remoteCluster, gerr := etcdserver.GetClusterFromRemotePeers([]string{u}, trp)
		trp.CloseIdleConnections()
		if gerr != nil {
			// Do not return error, because other members may be not ready.
			log.Error(gerr)
			continue
		}

		remoteClusterID := remoteCluster.ID()
		if remoteClusterID != localClusterID {
			return errors.Errorf("Etcd cluster ID mismatch, expect %d, got %d", localClusterID, remoteClusterID)
		}
	}
	return nil
}

// AddEtcdMember adds an etcd member.
func AddEtcdMember(client *clientv3.Client, urls []string, timeout time.Duration) (*clientv3.MemberAddResponse, error) {
	ctx, cancel := context.WithTimeout(client.Ctx(), timeout)
	addResp, err := client.MemberAdd(ctx, urls)
	cancel()
	return addResp, errors.Trace(err)
}

// ListEtcdMembers returns a list of internal etcd members.
func ListEtcdMembers(client *clientv3.Client, timeout time.Duration) (*clientv3.MemberListResponse, error) {
	if client == nil {
		log.Errorf("client is nil !!!")
		return nil, errors.Errorf("nil etcd client !!!")
	}
	ctx, cancel := context.WithTimeout(client.Ctx(), timeout)
	listResp, err := client.MemberList(ctx)
	cancel()
	return listResp, errors.Trace(err)
}

// RemoveEtcdMember removes a member by the given id.
func RemoveEtcdMember(client *clientv3.Client, id uint64, timeout time.Duration) (*clientv3.MemberRemoveResponse, error) {
	ctx, cancel := context.WithTimeout(client.Ctx(), timeout)
	rmResp, err := client.MemberRemove(ctx, id)
	cancel()
	return rmResp, errors.Trace(err)
}
