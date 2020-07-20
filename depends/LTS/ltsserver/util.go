package ltsserver

import (
	"context"
	"encoding/binary"
	"math/rand"
	"net/http"
	"time"

	"github.com/coreos/etcd/clientv3"
	"github.com/gogo/protobuf/proto"
	"github.com/juju/errors"
	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltsetcdutil"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
	log "github.com/sirupsen/logrus"
)

// DialClient used to dail http request.
var DialClient = &http.Client{
	Transport: &http.Transport{
		DisableKeepAlives: true,
	},
}

// Return boolean to indicate whether the key exists or not.
func LoadProtoMsg(c *clientv3.Client, key string, msg proto.Message, opts ...clientv3.OpOption) (bool, error) {
	value, err := LoadValue(c, key, opts...)
	if err != nil {
		return false, errors.Trace(err)
	}
	if value == nil {
		return false, nil
	}

	if err = proto.Unmarshal(value, msg); err != nil {
		return false, errors.Trace(err)
	}

	return true, nil
}

func LoadProtoMsgWithModRev(c *clientv3.Client, key string, msg proto.Message, opts ...clientv3.OpOption) (bool, int64, error) {
	resp, err := get(c, key, opts...)
	if err != nil {
		return false, 0, err
	}
	if resp == nil {
		return false, 0, nil
	}
	value := resp.Kvs[0].Value
	if err = proto.Unmarshal(value, msg); err != nil {
		return false, 0, err
	}
	return true, resp.Kvs[0].ModRevision, nil
}

// A helper function to get value with key from etcd.
func LoadValue(c *clientv3.Client, key string, opts ...clientv3.OpOption) ([]byte, error) {
	resp, err := KVGet(c, key, opts...)
	if err != nil {
		return nil, errors.Trace(err)
	}

	if n := len(resp.Kvs); n == 0 {
		return nil, nil
	} else if n > 1 {
		return nil, errors.Errorf("invalid get value resp %v, must only one", resp.Kvs)
	}

	return resp.Kvs[0].Value, nil
}

// EtcdKVGet returns the etcd GetResponse by given key or key prefix
func EtcdKVGet(c *clientv3.Client, key string, opts ...clientv3.OpOption) (*clientv3.GetResponse, error) {
	ctx, cancel := context.WithTimeout(c.Ctx(), ltsconf.Cfg().Etcd.KVRequestTimeout.Duration)
	defer cancel()

	start := time.Now()
	resp, err := clientv3.NewKV(c).Get(ctx, key, opts...)
	if err != nil {
		log.Errorf("load from etcd error: %v", err)
	}
	if cost := time.Since(start); cost > ltsconf.Cfg().Etcd.KVSlowRequestTime.Duration {
		log.Warnf("kv gets too slow, key: %v, cost: %v", key, cost)
	}

	return resp, err
}

func get(c *clientv3.Client, key string, opts ...clientv3.OpOption) (*clientv3.GetResponse, error) {
	resp, err := EtcdKVGet(c, key, opts...)
	if err != nil {
		return nil, err
	}

	if n := len(resp.Kvs); n == 0 {
		return nil, nil
	} else if n > 1 {
		return nil, errors.Errorf("invalid get value resp %v, must only one", resp.Kvs)
	}
	return resp, nil
}

// GetValue gets value with key from etcd.
func GetValue(c *clientv3.Client, key string, opts ...clientv3.OpOption) ([]byte, error) {
	resp, err := get(c, key, opts...)
	if err != nil {
		return nil, err
	}
	if resp == nil {
		return nil, nil
	}
	return resp.Kvs[0].Value, nil
}

func InitOrGetClusterID(c *clientv3.Client, key string) (uint32, error) {
	ctx, cancel := context.WithTimeout(c.Ctx(), ltsconf.Cfg().SvrConfig.RequestTimeout.Duration)
	defer cancel()

	clusterID := ltsconf.Cfg().SvrConfig.ClusterID

	// Generate a random cluster ID.
	if clusterID == uint32(0) {
		ts := uint32(time.Now().Unix())
		clusterID = (ts << 16) + ((uint32(rand.Uint32())) >> 16)
	}

	value := Uint32ToBytes(clusterID)

	resp, err := c.Txn(ctx).
		If(clientv3.Compare(clientv3.CreateRevision(key), "=", 0)).
		Then(clientv3.OpPut(key, string(value))).
		Else(clientv3.OpGet(key)).
		Commit()
	if err != nil {
		return 0, errors.Trace(err)
	}

	// Txn commits ok, return the generated cluster ID.
	if resp.Succeeded {
		return clusterID, nil
	}

	// Otherwise, parse the committed cluster ID.
	if len(resp.Responses) == 0 {
		return 0, errors.Errorf("Txn returns empty response: %v", resp)
	}

	response := resp.Responses[0].GetResponseRange()
	if response == nil || len(response.Kvs) != 1 {
		return 0, errors.Errorf("Txn returns invalid range response: %v", resp)
	}

	return BytesToUint32(response.Kvs[0].Value)
}

func BytesToUint64(b []byte) (uint64, error) {
	if len(b) != 8 {
		return 0, errors.Errorf("invalid data, must 8 bytes, but %d", len(b))
	}

	return binary.BigEndian.Uint64(b), nil
}

func BytesToUint32(b []byte) (uint32, error) {
	if len(b) != 4 {
		return 0, errors.Errorf("invalid data, must 4 bytes, but %d", len(b))
	}

	return binary.BigEndian.Uint32(b), nil
}

func Uint64ToBytes(v uint64) []byte {
	b := make([]byte, 8)
	binary.BigEndian.PutUint64(b, v)
	return b
}

func Uint32ToBytes(v uint32) []byte {
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, v)
	return b
}

// SlowLogTxn wraps etcd transaction and log slow one.
type SlowLogTxn struct {
	clientv3.Txn
	cancel context.CancelFunc
}

func NewSlowLogTxn(client *clientv3.Client) clientv3.Txn {
	ctx, cancel := context.WithTimeout(client.Ctx(), ltsconf.Cfg().SvrConfig.RequestTimeout.Duration)
	return &SlowLogTxn{
		Txn:    client.Txn(ctx),
		cancel: cancel,
	}
}

func (t *SlowLogTxn) If(cs ...clientv3.Cmp) clientv3.Txn {
	return &SlowLogTxn{
		Txn:    t.Txn.If(cs...),
		cancel: t.cancel,
	}
}

func (t *SlowLogTxn) Then(ops ...clientv3.Op) clientv3.Txn {
	return &SlowLogTxn{
		Txn:    t.Txn.Then(ops...),
		cancel: t.cancel,
	}
}

// Commit implements Txn Commit interface.
func (t *SlowLogTxn) Commit() (*clientv3.TxnResponse, error) {
	resp, err := t.Txn.Commit()
	t.cancel()
	return resp, errors.Trace(err)
}

// GetMembers return a slice of Members.
func GetMembers(etcdClient *clientv3.Client) ([]*ltsrpc.Member, error) {
	listResp, err := ltsetcdutil.ListEtcdMembers(etcdClient, ltsconf.Cfg().Etcd.KVRequestTimeout.Duration)
	if err != nil {
		return nil, errors.Trace(err)
	}

	members := make([]*ltsrpc.Member, 0, len(listResp.Members))
	for _, m := range listResp.Members {
		info := &ltsrpc.Member{
			Name:       m.Name,
			MemberId:   m.ID,
			ClientUrls: m.ClientURLs,
			PeerUrls:   m.PeerURLs,
		}
		members = append(members, info)
	}

	return members, nil
}

// InitHTTPClient initials a http client.
func InitHTTPClient(svr *Server) error {
	tlsConfig, err := svr.GetSecurityConfig().ToTLSConfig()
	if err != nil {
		return errors.Trace(err)
	}

	DialClient = &http.Client{
		Transport: &http.Transport{
			TLSClientConfig:   tlsConfig,
			DisableKeepAlives: true,
		}}
	return nil
}

func ParseTimestamp(data []byte) (time.Time, error) {
	nano, err := BytesToUint64(data)
	if err != nil {
		return zeroTime, err
	}

	return time.Unix(0, int64(nano)), nil
}

func SubTime(after time.Time, before time.Time) time.Duration {
	return time.Duration(after.UnixNano() - before.UnixNano())
}

func SubTimeByUint64(after uint64, before uint64) time.Duration {
	return time.Duration(int64(after - before))
}
