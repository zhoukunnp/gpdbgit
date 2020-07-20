package ltsserver

import (
	"context"
	"time"

	"github.com/coreos/etcd/clientv3"
	"github.com/juju/errors"
	log "github.com/sirupsen/logrus"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
	"sync"
)

var (
	errTxnFailed = errors.New("failed to commit transaction")
)

type etcdKVBase struct {
	sync.RWMutex
	server *Server
	client *clientv3.Client
}

func NewEtcdKVBase(s *Server, cli *clientv3.Client) *etcdKVBase {
	return &etcdKVBase{
		server: s,
		client: cli,
	}
}

func (kv *etcdKVBase) Load(key string) (string, error) {
	resp, err := KVGet(kv.client, key)
	if err != nil {
		return "", errors.Trace(err)
	}
	if n := len(resp.Kvs); n == 0 {
		return "", nil
	} else if n > 1 {
		return "", errors.Errorf("load more than one kvs: key %v kvs %v", key, n)
	}
	return string(resp.Kvs[0].Value), nil
}

func (kv *etcdKVBase) Save(key, value string) error {
	resp, err := kv.server.LeaderTxn().Then(clientv3.OpPut(key, value)).Commit()
	if err != nil {
		log.Errorf("save to etcd error: %v", err)
		return errors.Trace(err)
	}

	if !resp.Succeeded {
		log.Errorf("resp status not succeed: %v", resp)
		return errors.Trace(errTxnFailed)
	}

	return nil
}

func (kv *etcdKVBase) Delete(key string) error {
	resp, err := kv.server.LeaderTxn().Then(clientv3.OpDelete(key)).Commit()
	if err != nil {
		log.Errorf("delete from etcd error: %v", err)
		return errors.Trace(err)
	}
	if !resp.Succeeded {
		return errors.Trace(errTxnFailed)
	}
	return nil
}

func KVGet(c *clientv3.Client, key string, opts ...clientv3.OpOption) (*clientv3.GetResponse, error) {
	ctx, cancel := context.WithTimeout(c.Ctx(), ltsconf.Cfg().Etcd.KVRequestTimeout.Duration)
	defer cancel()

	start := time.Now()
	resp, err := clientv3.NewKV(c).Get(ctx, key, opts...)
	if err != nil {
		log.Errorf("load from etcd error: %v", err)
	}

	if cost := time.Since(start); cost > ltsconf.Cfg().Etcd.KVSlowRequestTime.Duration {
		log.Warnf("kv get too slow: key %v cost %v err %v", key, cost, err)
	}

	return resp, errors.Trace(err)
}

func KVPut(c *clientv3.Client, key string, value string, opts ...clientv3.OpOption) (*clientv3.PutResponse, error) {
	ctx, cancel := context.WithTimeout(c.Ctx(), ltsconf.Cfg().Etcd.KVRequestTimeout.Duration)
	defer cancel()

	start := time.Now()
	resp, err := clientv3.NewKV(c).Put(ctx, key, value)
	if err != nil {
		log.Errorf("put value to etcd error: %v", err)
	}

	if cost := time.Since(start); cost > ltsconf.Cfg().Etcd.KVSlowRequestTime.Duration {
		log.Warnf("kv put too slow: key %v cost %v err %v", key, cost, err)
	}

	return resp, errors.Trace(err)
}
