package ltsserver

import (
	"fmt"
	"path"
	"strings"

	"github.com/coreos/etcd/clientv3"
	"github.com/coreos/etcd/embed"
	"github.com/coreos/etcd/wal"
	"github.com/juju/errors"

	log "github.com/sirupsen/logrus"
	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltsetcdutil"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
)

func PrepareJoinCluster() error {
	if ltsconf.Cfg().Join == "" {
		log.Info("Not found join config.")
		return nil
	}

	if ltsconf.Cfg().Join == ltsconf.Cfg().AdvertiseClientUrls {
		return errors.New("join self is forbidden")
	}

	// Cases with data directory.
	initialCluster := ""
	if wal.Exist(path.Join(ltsconf.Cfg().DataDir, "member")) {
		ltsconf.Cfg().InitialCluster = initialCluster
		ltsconf.Cfg().InitialClusterState = embed.ClusterStateFlagExisting
		return nil
	}

	// Below are cases without data directory.
	tlsConfig, err := ltsconf.Cfg().Security.ToTLSConfig()
	if err != nil {
		return errors.Trace(err)
	}

	client, err := clientv3.New(clientv3.Config{
		Endpoints:   strings.Split(ltsconf.Cfg().Join, ","),
		DialTimeout: ltsconf.Cfg().Etcd.DialTimeout.Duration,
		TLS:         tlsConfig,
	})
	if err != nil {
		return errors.Trace(err)
	}

	defer client.Close()

	listResp, err := ltsetcdutil.ListEtcdMembers(client, ltsconf.Cfg().Etcd.KVRequestTimeout.Duration)
	if err != nil {
		return errors.Trace(err)
	}

	existed := false
	for _, m := range listResp.Members {
		if m.Name == ltsconf.Cfg().Name {
			existed = true
		}
	}

	if existed {
		return errors.New("missing data or join a duplicated lts cluster")
	}

	addResp, err := ltsetcdutil.AddEtcdMember(client, []string{ltsconf.Cfg().AdvertisePeerUrls}, ltsconf.Cfg().Etcd.KVRequestTimeout.Duration)
	if err != nil {
		return errors.Trace(err)
	}

	listResp, err = ltsetcdutil.ListEtcdMembers(client, ltsconf.Cfg().Etcd.KVRequestTimeout.Duration)
	if err != nil {
		return errors.Trace(err)
	}

	var hosts []string
	for _, member := range listResp.Members {
		n := member.Name
		if member.ID == addResp.Member.ID {
			n = ltsconf.Cfg().Name
		}
		for _, m := range member.PeerURLs {
			hosts = append(hosts, fmt.Sprintf("%s=%s", n, m))
		}
	}

	initialCluster = strings.Join(hosts, ",")
	ltsconf.Cfg().InitialCluster = initialCluster
	ltsconf.Cfg().InitialClusterState = embed.ClusterStateFlagExisting

	return nil
}
