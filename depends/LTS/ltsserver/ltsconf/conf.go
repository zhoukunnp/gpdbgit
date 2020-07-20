package ltsconf

import (
	"crypto/tls"
	"flag"
	"fmt"
	"net/url"
	"strings"
	"sync"
	"time"

	"github.com/BurntSushi/toml"
	"github.com/coreos/etcd/embed"
	"github.com/juju/errors"

	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltslogutil"

	"github.com/coreos/etcd/pkg/transport"
	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltstypeutil"
)

var (
	once           sync.Once
	configInstance *Config
)

// Config is the LTS server configuration.
type Config struct {
	*flag.FlagSet `json:"-"`

	Name    string `toml:"name" json:"name"`
	DataDir string `toml:"data-dir" json:"data-dir"`

	InitialCluster      string `toml:"initial-cluster" json:"initial-cluster"`
	InitialClusterState string `toml:"initial-cluster-state" json:"initial-cluster-state"`

	// Join to an existing LTS cluster, a string of endpoints.
	Join string `toml:"join" json:"join"`

	TxntsServiceUrl     string `toml:"txnts-service-url" json:"txnts-service-url"`
	ClientUrls          string `toml:"client-urls" json:"client-urls"`
	PeerUrls            string `toml:"peer-urls" json:"peer-urls"`
	AdvertiseClientUrls string `toml:"advertise-client-urls" json:"advertise-client-urls"`
	AdvertisePeerUrls   string `toml:"advertise-peer-urls" json:"advertise-peer-urls"`

	// LeaderLease time unit: second
	LeaderLease                 int64                `toml:"lease" json:"lease"`
	LeaderPriorityCheckInterval ltstypeutil.Duration `toml:"leader-priority-check-interval" json:"leader-priority-check-interval"`

	// Log related config.
	Log ltslogutil.LogConfig `toml:"log" json:"log"`

	SvrConfig ServerConfig `toml:"server" json:"server"`
	Etcd      EtcdConfig   `toml:"etcd" json:"etcd"`
	TCP       TCPConfig    `toml:"tcp" json:"tcp"`

	SnapCount    uint64 `toml:"snapshot-count" json:"snap_count"`
	MaxSnapFiles uint   `toml:"max-snapshot-files" json:"max_snap_files"`
	MaxWalFiles  uint   `toml:"max-wal-files" json:"max_wal_files"`

	// QuotaBackendBytes Raise alarms when backend size exceeds the given quota. 0 means use the default quota.
	// the default size is 2GB, the maximum is 8GB.
	QuotaBackendBytes ltstypeutil.ByteSize `toml:"quota-backend-bytes" json:"quota-backend-bytes"`

	// AutoCompactionMode is either 'periodic' or 'revision'.
	AutoCompactionMode string `toml:"auto-compaction-mode" json:"auto-compaction-mode"`

	// AutoCompactionRetention is either duration string with time unit
	// (e.g. '5m' for 5-minute), or revision unit (e.g. '5000').
	// If no time unit is provided and compaction mode is 'periodic',
	// the unit defaults to hour. For example, '5' translates into 5-hour.
	AutoCompactionRetention string `toml:"auto-compaction-retention" json:"auto-compaction-retention"`

	// TickInterval is the interval for etcd Raft tick.
	TickInterval ltstypeutil.Duration `toml:"tick-interval" json:"tick_interval"`

	// ElectionInterval is the interval for etcd Raft election.
	ElectionInterval ltstypeutil.Duration `toml:"election-interval" json:"election_interval"`
	Security         SecurityConfig       `toml:"security" json:"security" json:"security"`
	configFile       string

	// For all warnings during parsing.
	WarningMsgs []string `json:"warning_msgs"`
}

func NewConfig(arguments []string) error {
	var err error
	once.Do(func() {
		configInstance = CreateConfig()
		err = Parse(arguments)
	})
	return err
}

func Cfg() *Config {
	return configInstance
}

// CreateConfig creates a new config.
func CreateConfig() *Config {
	cfg := &Config{}
	cfg.FlagSet = flag.NewFlagSet("lts", flag.ContinueOnError)
	fs := cfg.FlagSet

	fs.StringVar(&cfg.configFile, "config", "", "Config file")
	fs.StringVar(&cfg.Name, "name", defaultName, "human-readable name for this lts server")
	fs.StringVar(&cfg.DataDir, "data-dir", "", "path to the data directory (default 'default.${name}')")
	fs.StringVar(&cfg.TxntsServiceUrl, "txnts-service-url", "", "dedicate port for txn ts service")
	fs.StringVar(&cfg.ClientUrls, "client-urls", defaultClientUrls, "url for client traffic")
	fs.StringVar(&cfg.AdvertiseClientUrls, "advertise-client-urls", "", "advertise url for client traffic (default '${client-urls}')")
	fs.StringVar(&cfg.PeerUrls, "peer-urls", defaultPeerUrls, "url for peer traffic")
	fs.StringVar(&cfg.AdvertisePeerUrls, "advertise-peer-urls", "", "advertise url for peer traffic (default '${peer-urls}')")
	fs.StringVar(&cfg.InitialCluster, "initial-cluster", "", "initial cluster configuration for bootstrapping, e,g. lts=http://127.0.0.1:2380")
	fs.StringVar(&cfg.Join, "join", "", "join to an existing cluster (usage: cluster's '${advertise-client-urls}'")

	fs.StringVar(&cfg.Log.Level, "L", "", "log level: debug, info, warn, error, fatal (default 'debug')")
	fs.StringVar(&cfg.Log.File.Filename, "log-file", "", "log file path")
	fs.BoolVar(&cfg.Log.File.LogRotate, "log-rotate", true, "rotate log")

	fs.StringVar(&cfg.Security.CAPath, "cacert", "", "Path of file that contains list of trusted TLS CAs")
	fs.StringVar(&cfg.Security.CertPath, "cert", "", "Path of file that contains X509 certificate in PEM format")
	fs.StringVar(&cfg.Security.KeyPath, "key", "", "Path of file that contains X509 key in PEM format")

	return cfg
}

const (
	defaultLeaderLease                 = int64(3)
	defaultLeaderPriorityCheckInterval = time.Minute
	defaultAutoCompactionRetention     = "10000"
	defaultAutoCompactionMode          = "revision"
	defaultQuotaBackendBytes           = 1024 * 1024 * 1024 * 8
	defaultSnapCount                   = 100000
	defaultMaxSnapFiles                = 100
	defaultMaxWalFiles                 = 100
	defaultName                        = "lts-cluster"
	defaultClientUrls                  = "http://127.0.0.1:2379"
	defaultPeerUrls                    = "http://127.0.0.1:2380"
	defaultInitialClusterState         = embed.ClusterStateFlagNew
	defaultTickInterval                = 500 * time.Millisecond
	defaultElectionInterval            = 3000 * time.Millisecond
)

func adjustString(v *string, defValue string) {
	if len(*v) == 0 {
		*v = defValue
	}
}

func adjustInt32(v *int32, defValue int32) {
	if *v == 0 {
		*v = defValue
	}
}

func adjustUint64(v *uint64, defValue uint64) {
	if *v == 0 {
		*v = defValue
	}
}

func adjustInt64(v *int64, defValue int64) {
	if *v == 0 {
		*v = defValue
	}
}

func adjustUint32(v *uint32, defValue uint32) {
	if *v == 0 {
		*v = defValue
	}
}

func adjustUint(v *uint, defValue uint) {
	if *v == 0 {
		*v = defValue
	}
}

func adjustInt(v *int, defValue int) {
	if *v == 0 {
		*v = defValue
	}
}

func adjustFloat64(v *float64, defValue float64) {
	if *v == 0 {
		*v = defValue
	}
}

func adjustDuration(v *ltstypeutil.Duration, defValue time.Duration) {
	if v.Duration == 0 {
		v.Duration = defValue
	}
}

// Parse parses flag definitions from the argument list.
func Parse(arguments []string) error {
	// Parse first to get config file.
	err := Cfg().FlagSet.Parse(arguments)
	if err != nil {
		return errors.Trace(err)
	}

	// Load config file if specified.
	if Cfg().configFile != "" {
		err = Cfg().ConfigFromFile(Cfg().configFile)
		if err != nil {
			return errors.Trace(err)
		}
	}

	// Parse again to replace with command line options.
	err = Cfg().FlagSet.Parse(arguments)
	if err != nil {
		return errors.Trace(err)
	}

	if len(Cfg().FlagSet.Args()) != 0 {
		return errors.Errorf("'%s' is an invalid flag", Cfg().FlagSet.Arg(0))
	}

	err = Cfg().Adjust()
	return errors.Trace(err)
}

func (c *Config) Validate() error {
	if c.Join != "" && c.InitialCluster != "" {
		return errors.New("-initial-cluster and -join can not be provided at the same time")
	}
	return nil
}

func (c *Config) Adjust() error {
	if err := c.Validate(); err != nil {
		return errors.Trace(err)
	}

	adjustString(&c.Name, defaultName)
	adjustString(&c.DataDir, fmt.Sprintf("default.%s", c.Name))

	adjustString(&c.ClientUrls, defaultClientUrls)
	adjustString(&c.AdvertiseClientUrls, c.ClientUrls)
	adjustString(&c.PeerUrls, defaultPeerUrls)
	adjustString(&c.AdvertisePeerUrls, c.PeerUrls)

	if len(c.InitialCluster) == 0 {
		items := strings.Split(c.AdvertisePeerUrls, ",")
		sep := ""
		for _, item := range items {
			c.InitialCluster += fmt.Sprintf("%s%s=%s", sep, c.Name, item)
			sep = ","
		}
	}

	adjustString(&c.InitialClusterState, defaultInitialClusterState)

	if len(c.Join) > 0 {
		if _, err := url.Parse(c.Join); err != nil {
			return errors.Errorf("failed to parse join addr:%s, err:%v", c.Join, err)
		}
	}

	adjustInt64(&c.LeaderLease, defaultLeaderLease)

	if c.MaxSnapFiles == 0 {
		c.MaxSnapFiles = defaultMaxSnapFiles
	}

	if c.MaxWalFiles == 0 {
		c.MaxWalFiles = defaultMaxWalFiles
	}

	if c.SnapCount == 0 {
		c.SnapCount = defaultSnapCount
	}

	if c.QuotaBackendBytes == 0 {
		c.QuotaBackendBytes = defaultQuotaBackendBytes
	}

	if len(c.AutoCompactionMode) == 0 {
		c.AutoCompactionMode = defaultAutoCompactionMode
	}

	if len(c.AutoCompactionRetention) == 0 {
		c.AutoCompactionRetention = defaultAutoCompactionRetention
	}

	adjustDuration(&c.TickInterval, defaultTickInterval)
	adjustDuration(&c.ElectionInterval, defaultElectionInterval)
	adjustDuration(&c.LeaderPriorityCheckInterval, defaultLeaderPriorityCheckInterval)

	c.SvrConfig.Adjust()
	c.Etcd.Adjust(c)
	c.TCP.Adjust()

	return nil
}

func (c *Config) String() string {
	if c == nil {
		return "<nil>"
	}
	return fmt.Sprintf("Config(%+v)", *c)
}

// ConfigFromFile loads config from file.
func (c *Config) ConfigFromFile(path string) error {
	_, err := toml.DecodeFile(path, c)
	return errors.Trace(err)
}

// ServerConfig is the server configuration
type ServerConfig struct {
	ClusterID          uint32 `toml:"cluster-id,omitempty" json:"cluster-id,omitempty"`
	LTSRootPath        string `toml:"lts-root-path,omitempty" json:"lts-root-path,omitempty"`
	LTSAPIPrefix       string `toml:"lts-api-prefix,omitempty" json:"lts-api-prefix,omitempty"`
	LTSClusterIDPath   string `toml:"lts-cluster-id-path,omitempty" json:"lts-cluster-id-path,omitempty"`
	LTSClusterMetaPath string `toml:"lts-cluster-meta-path,omitempty" json:"lts-cluster-meta-path,omitempty"`
	LTSTimestampPath   string `toml:"lts-timestamp-path" json:"lts-timestamp-path"`

	UpdateTimestampStep     ltstypeutil.Duration `toml:"update-timestamp-step,omitempty" json:"update-timestamp-step,omitempty"`
	UpdateTimestampGuard    ltstypeutil.Duration `toml:"update-timestamp-guard,omitempty" json:"update-timestamp-guard,omitempty"`
	UpdateTimestampInterval ltstypeutil.Duration `toml:"update-timestamp-interval,omitempty" json:"update-timestamp-interval,omitempty"`

	ResignLeaderTimeout          ltstypeutil.Duration `toml:"resign-leader-timeout,omitempty" json:"resign-leader-timeout,omitempty"`
	NextLeaderTTL                ltstypeutil.Duration `toml:"next-leader-ttl,omitempty" json:"next-leader-ttl,omitempty"`
	RequestTimeout               ltstypeutil.Duration `toml:"request-timeout,omitempty" json:"request-timeout,omitempty"`
	SlowRequestTime              ltstypeutil.Duration `toml:"slow-request-time,omitempty" json:"slow-request-time,omitempty"`
	ThroughputCollectionInterval ltstypeutil.Duration `toml:"throughput-collection-interval,omitempty" json:"throughput-collection-interval,omitempty"`
}

const (
	defaultLTSRootPath                  = "/lts-cluster"
	defaultLTSAPIPrefix                 = "/lts-cluster/api"
	defaultLTSClusterIDPath             = "/lts-cluster/cluster_id"
	defaultLTSClusterMetaPath           = "/lts-cluster/meta"
	defaultLTSTimestampPath             = "/lts-cluster/ts"
	defaultUpdateTimestampStep          = time.Second * 5
	defaultUpdateTimestampGuard         = time.Second * 1
	defaultUpdateTimestampInterval      = time.Second * 1
	defaultResignLeaderTimeout          = time.Second * 5
	defaultNextLeaderTTL                = time.Second * 10
	defaultRequestTimeout               = time.Second * 10
	defaultSlowRequestTime              = time.Second * 1
	defaultThroughputCollectionInterval = time.Second * 10
)

func (c *ServerConfig) Adjust() {
	adjustUint32(&c.ClusterID, 0)
	adjustString(&c.LTSRootPath, defaultLTSRootPath)
	adjustString(&c.LTSAPIPrefix, defaultLTSAPIPrefix)
	adjustString(&c.LTSClusterIDPath, defaultLTSClusterIDPath)
	adjustString(&c.LTSClusterMetaPath, defaultLTSClusterMetaPath)
	adjustString(&c.LTSTimestampPath, defaultLTSTimestampPath)

	adjustDuration(&c.UpdateTimestampStep, defaultUpdateTimestampStep)
	adjustDuration(&c.UpdateTimestampGuard, defaultUpdateTimestampGuard)
	adjustDuration(&c.UpdateTimestampInterval, defaultUpdateTimestampInterval)

	adjustDuration(&c.ResignLeaderTimeout, defaultResignLeaderTimeout)
	adjustDuration(&c.NextLeaderTTL, defaultNextLeaderTTL)
	adjustDuration(&c.RequestTimeout, defaultRequestTimeout)
	adjustDuration(&c.SlowRequestTime, defaultSlowRequestTime)
	adjustDuration(&c.ThroughputCollectionInterval, defaultThroughputCollectionInterval)
}

type TCPConfig struct {
	TCPReadBufferSize           int                  `toml:"tcp-read-buffer-size" json:"tcp-read-buffer-size"`
	TCPWriteBufferSize          int                  `toml:"tcp-write-buffer-size" json:"tcp-write-buffer-size"`
	TCPSetNoDelay               int                  `toml:"tcp-set-no-delay" json:"tcp-set-no-delay"`
	TCPRingbufCapacity          uint64               `toml:"tcp-ringbuf-capacity" json:"tcp-ringbuf-capacity"`
	TCPRingbufBatchProduceLimit uint64               `toml:"tcp-ringbuf-batch-produce-limit" json:"tcp-ringbuf-batch-produce-limit"`
	TCPReadTimeout              ltstypeutil.Duration `toml:"tcp-read-timeout" json:"tcp-read-timeout"`
	TCPWriteTimeout             ltstypeutil.Duration `toml:"tcp-write-timeout" json:"tcp-write-timeout"`
	TCPMaxHeaderLength          uint32               `toml:"tcp-max-header-length" json:"tcp-max-header-length"`
	TCPMaxBodyLength            uint32               `toml:"tcp-max-body-length,omitempty" json:"tcp-max-body-length,omitempty"`
	TCPRingbufMonInterval       ltstypeutil.Duration `toml:"tcp-ringbuf-mon-interval" json:"tcp-ringbuf-mon-interval"`
}

const (
	//TCPReadBuffer tcp read buffer length
	defaultTCPReadBufferSize = 128 * 1024 * 1024
	//TCPWriteBuffer tcp write buffer length
	defaultTCPWriteBufferSize = 128 * 1024 * 1024
	//TCPNoDelay default is 0, which means delayed ack is on by default.
	defaultTCPSetNoDelay               = 0
	defaultTCPRingbufCapacity          = 1048576
	defaultTCPRingbufBatchProduceLimit = 1024
	defaultTCPReadTimeout              = time.Second * 10
	defaultTCPWriteTimeout             = time.Second * 10
	defaultTCPMaxHeaderLength          = 2
	defaultTCPMaxBodyLength            = 22
	defaultTCPRingbufMonInterval       = time.Millisecond * 10
)

func (c *TCPConfig) Adjust() {
	adjustInt(&c.TCPReadBufferSize, defaultTCPReadBufferSize)
	adjustInt(&c.TCPWriteBufferSize, defaultTCPWriteBufferSize)
	adjustInt(&c.TCPSetNoDelay, defaultTCPSetNoDelay)
	adjustUint64(&c.TCPRingbufCapacity, defaultTCPRingbufCapacity)
	adjustUint64(&c.TCPRingbufBatchProduceLimit, defaultTCPRingbufBatchProduceLimit)
	adjustDuration(&c.TCPReadTimeout, defaultTCPReadTimeout)
	adjustDuration(&c.TCPWriteTimeout, defaultTCPWriteTimeout)
	adjustUint32(&c.TCPMaxHeaderLength, defaultTCPMaxHeaderLength)
	adjustUint32(&c.TCPMaxBodyLength, defaultTCPMaxBodyLength)
	adjustDuration(&c.TCPRingbufMonInterval, defaultTCPRingbufMonInterval)
}

type EtcdConfig struct {
	EtcdTimeout             ltstypeutil.Duration `toml:"etcd-timeout" json:"etcd-timeout"`
	KVRequestTimeout        ltstypeutil.Duration `toml:"kv-request-timeout" json:"kv-request-timeout"`
	KVSlowRequestTime       ltstypeutil.Duration `toml:"kv-slow-request-time" json:"kv-slow-request-time"`
	DialTimeout             ltstypeutil.Duration `toml:"dial-timeout" json:"dial-timeout"`
	MaxTxnOps               uint                 `toml:"max-txn-ops" json:"max-txn-ops"`
	EtcdClientUrls          string               `toml:"etcd-client-urls" json:"etcd-client-urls"`
	EtcdPeerUrls            string               `toml:"etcd-peer-urls" json:"etcd-peer-urls"`
	EtcdAdvertiseClientUrls string               `toml:"etcd-advertise-client-urls" json:"etcd-advertise-client-urls"`
	EtcdAdvertisePeerUrls   string               `toml:"etcd-advertise-peer-urls" json:"etcd-advertise-peer-urls"`
	ReadBufferSize          int                  `toml:"read-buffer-size" json:"read-buffer-size"`
	WriteBufferSize         int                  `toml:"write-buffer-size" json:"write-buffer-size"`
	InitialWindowSize       int                  `toml:"initial-window-size" json:"initial-window-size"`
	InitialConnWindowSize   int                  `toml:"initial-conn-window-size" json:"initial-conn-window-size"`
	MaxRequestSize          uint                 `toml:"max-request-size" json:"max-request-size"`
}

const (
	defaultEtcdTimeout           = time.Second * 3
	defaultKVRequestTimeout      = time.Second * 10
	defaultKVSlowRequestTime     = time.Second * 1
	defaultDialTimeout           = time.Second * 30
	defaultMaxTxnOps             = 1024
	defaultEtcdClientUrls        = "http://127.0.0.1:2379"
	defaultEtcdPeerUrls          = "http://127.0.0.1:2380"
	defaultMaxRequestSize        = 1.5 * 1024 * 1024
)

func (c *EtcdConfig) Adjust(cfg *Config) {
	adjustDuration(&c.EtcdTimeout, defaultEtcdTimeout)
	adjustDuration(&c.KVSlowRequestTime, defaultKVSlowRequestTime)
	adjustDuration(&c.KVRequestTimeout, defaultKVRequestTimeout)
	adjustDuration(&c.DialTimeout, defaultDialTimeout)
	adjustUint(&c.MaxTxnOps, defaultMaxTxnOps)

	adjustString(&c.EtcdClientUrls, defaultEtcdClientUrls)
	adjustString(&c.EtcdAdvertiseClientUrls, c.EtcdClientUrls)

	adjustString(&c.EtcdPeerUrls, defaultEtcdPeerUrls)
	adjustString(&c.EtcdAdvertisePeerUrls, c.EtcdPeerUrls)
	adjustUint(&c.MaxRequestSize, defaultMaxRequestSize)
}

// SecurityConfig is the configuration for supporting tls.
type SecurityConfig struct {
	// CAPath is the path of file that contains list of trusted SSL CAs. if set, following four settings shouldn't be empty
	CAPath string `toml:"cacert-path" json:"cacert-path"`
	// CertPath is the path of file that contains X509 certificate in PEM format.
	CertPath string `toml:"cert-path" json:"cert-path"`
	// KeyPath is the path of file that contains X509 key in PEM format.
	KeyPath string `toml:"key-path" json:"key-path"`
}

// ToTLSConfig generatres tls config.
func (s SecurityConfig) ToTLSConfig() (*tls.Config, error) {
	if len(s.CertPath) == 0 && len(s.KeyPath) == 0 {
		return nil, nil
	}
	tlsInfo := transport.TLSInfo{
		CertFile:      s.CertPath,
		KeyFile:       s.KeyPath,
		TrustedCAFile: s.CAPath,
	}
	tlsConfig, err := tlsInfo.ClientConfig()
	if err != nil {
		return nil, errors.Trace(err)
	}
	return tlsConfig, nil
}

// ParseUrls parse a string into multiple urls.
// Urls are separated by ","
// Export for http_api.
func ParseUrls(s string) ([]url.URL, error) {
	items := strings.Split(s, ",")
	urls := make([]url.URL, 0, len(items))
	for _, item := range items {
		u, err := url.Parse(item)
		if err != nil {
			return nil, errors.Trace(err)
		}

		urls = append(urls, *u)
	}

	return urls, nil
}

// Generates configuration for embedded etcd.
func (c *Config) GenEmbedEtcdConfig() (*embed.Config, error) {
	cfg := embed.NewConfig()

	cfg.Name = c.Name
	cfg.Dir = c.DataDir
	cfg.WalDir = ""
	cfg.InitialCluster = c.InitialCluster
	cfg.ClusterState = c.InitialClusterState
	cfg.EnablePprof = true
	cfg.TickMs = uint(c.TickInterval.Duration / time.Millisecond)
	cfg.ElectionMs = uint(c.ElectionInterval.Duration / time.Millisecond)
	cfg.MaxSnapFiles = c.MaxSnapFiles
	cfg.MaxWalFiles = c.MaxWalFiles
	cfg.SnapCount = c.SnapCount
	cfg.AutoCompactionMode = c.AutoCompactionMode
	cfg.AutoCompactionRetention = c.AutoCompactionRetention
	cfg.QuotaBackendBytes = int64(c.QuotaBackendBytes)

	cfg.ClientTLSInfo.ClientCertAuth = len(c.Security.CAPath) != 0
	cfg.ClientTLSInfo.TrustedCAFile = c.Security.CAPath
	cfg.ClientTLSInfo.CertFile = c.Security.CertPath
	cfg.ClientTLSInfo.KeyFile = c.Security.KeyPath
	cfg.PeerTLSInfo.TrustedCAFile = c.Security.CAPath
	cfg.PeerTLSInfo.CertFile = c.Security.CertPath
	cfg.PeerTLSInfo.KeyFile = c.Security.KeyPath

	cfg.MaxTxnOps = c.Etcd.MaxTxnOps
	cfg.MaxRequestBytes = c.Etcd.MaxRequestSize

	var err error

	cfg.LPUrls, err = ParseUrls(c.Etcd.EtcdPeerUrls)
	if err != nil {
		return nil, errors.Trace(err)
	}

	cfg.APUrls, err = ParseUrls(c.Etcd.EtcdAdvertisePeerUrls)
	if err != nil {
		return nil, errors.Trace(err)
	}

	cfg.LCUrls, err = ParseUrls(c.Etcd.EtcdClientUrls)
	if err != nil {
		return nil, errors.Trace(err)
	}

	cfg.ACUrls, err = ParseUrls(c.Etcd.EtcdAdvertiseClientUrls)
	if err != nil {
		return nil, errors.Trace(err)
	}

	return cfg, nil
}
