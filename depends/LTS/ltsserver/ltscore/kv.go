package ltscore

import (
	"github.com/gogo/protobuf/proto"
	"github.com/pkg/errors"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
)

type KVBase interface {
	Load(key string) (string, error)
	Save(key, value string) error
	Delete(key string) error
}

// KV wraps all kv operations, keep it stateless.
type KV struct {
	KVBase
}

// NewKV creates KV instance with KVBase.
func NewKV(base KVBase) *KV {
	return &KV{
		KVBase: base,
	}
}

func (kv *KV) LoadProto(key string, msg proto.Message) (bool, error) {
	value, err := kv.Load(key)
	if err != nil {
		return false, errors.WithStack(err)
	}
	if value == "" {
		return false, nil
	}
	return true, proto.Unmarshal([]byte(value), msg)
}

func (kv *KV) SaveProto(key string, msg proto.Message) error {
	value, err := proto.Marshal(msg)
	if err != nil {
		return errors.WithStack(err)
	}
	return kv.Save(key, string(value))
}

// LoadMeta loads cluster meta from KV store.
func (kv *KV) LoadClusterMeta(clusterMeta *ltsrpc.Cluster) (bool, error) {
	return kv.LoadProto(ltsconf.Cfg().SvrConfig.LTSClusterMetaPath, clusterMeta)
}

// SaveMeta save cluster meta to KV store.
func (kv *KV) SaveClusterMeta(clusterMeta *ltsrpc.Cluster) error {
	return kv.SaveProto(ltsconf.Cfg().SvrConfig.LTSClusterMetaPath, clusterMeta)
}
