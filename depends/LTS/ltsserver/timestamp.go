package ltsserver

import (
	"sync/atomic"
	"time"

	"github.com/coreos/etcd/clientv3"
	"github.com/juju/errors"
	log "github.com/sirupsen/logrus"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
)

const (
	// The actual largest logical is (1 << 32) - 1
	MaxLogical    = uint64(1 << 31)
	LogicalMask   = uint64(1<<32 - 1)
	MaxRetryCount = 100
)

var (
	zeroTime = time.Time{}
)

// physicalNano is the physical time expressed in nanosecond
// physicalSec = physicalNano / 1e9, used for txnts resp
type TimestampEntity struct {
	physicalNano uint64
	_pad1        [7]uint64
	physicalSec  uint64
	_pad2        [7]uint64
	logical      uint64
	_pad3        [7]uint64
}

func (svr *Server) LoadTimestamp() (time.Time, error) {
	data, err := LoadValue(svr.GetClient(), svr.TSSavePath)
	if err != nil {
		return zeroTime, err
	}
	if len(data) == 0 {
		return zeroTime, nil
	}
	return ParseTimestamp(data)
}

// If lastTS is 0, create it. Otherwise, update it.
func (svr *Server) SaveTimestamp(newSavedTime time.Time) error {
	data := Uint64ToBytes(uint64(newSavedTime.UnixNano()))

	resp, err := svr.LeaderTxn().Then(clientv3.OpPut(svr.TSSavePath, string(data))).Commit()
	if err != nil {
		return errors.Trace(err)
	}
	if !resp.Succeeded {
		return errors.New("Save timestamp failed, maybe we lost leader")
	}

	svr.PhysicalCacheBarrier = newSavedTime
	return nil
}

func (svr *Server) SaveTimestamp2(newSavedTime uint64) error {
	data := Uint64ToBytes(newSavedTime)

	resp, err := svr.LeaderTxn().Then(clientv3.OpPut(svr.TSSavePath, string(data))).Commit()
	if err != nil {
		return errors.Trace(err)
	}
	if !resp.Succeeded {
		return errors.New("Save timestamp failed, maybe we lost leader")
	}

	// Update the cache ts barrier
	svr.PhysicalCacheBarrier = time.Unix(0, int64(newSavedTime))
	return nil
}

// Currently, we use the lower 32-bit of the elapsed second counts as the physical part
// of a global transaction timestamp. The elapsed seconds would be (expressed in 32-bit)
// 01011100101111011110011110010101 since 1970.1.1. Therefore, running out all available
// 32-bit numbers would at least take (roughly) 49years * 1 = 50 years
// In conclusion, it's more than safe.
func (svr *Server) GetTimestamp(count uint64) (uint64, error) {
	resp := uint64(0)

	for i := 0; i < MaxRetryCount; i++ {
		current, ok := svr.TS.Load().(*TimestampEntity)
		if !ok || current.physicalNano == 0 {
			log.Errorf("The timestamp may has not been synced, wait and retry, retry count %d", i)
			time.Sleep(ltsconf.Cfg().SvrConfig.UpdateTimestampStep.Duration)
			continue
		}

		// Physical part: the higher 32-bit
		physical := current.physicalSec << 32

		// Logical part: the lower 32-bit. The client needs a total amount of 'count' TS for allocation
		logical := atomic.AddUint64(&current.logical, count) & LogicalMask

		if logical >= MaxLogical {
			log.Warnf("logical part exceeds max logical interval %v, retry count %d", resp, i)
			time.Sleep(ltsconf.Cfg().SvrConfig.UpdateTimestampStep.Duration)
			continue
		}

		// Combine two parts
		// |<- 32 bits ->|<- 32 bits ->|
		// |   Physical  |   Logical   |
		resp = physical | logical

		return resp, nil
	}

	return resp, errors.New("Exceed max retry times. Cannot get timestamp")
}

func (svr *Server) UpdateTimestamp() error {
	prevTS := svr.TS.Load().(*TimestampEntity)
	now := uint64(time.Now().UnixNano())

	// Calculate the difference: now - prevTS
	jetLag := SubTimeByUint64(now, prevTS.physicalNano)
	if jetLag > 3*ltsconf.Cfg().SvrConfig.UpdateTimestampStep.Duration {
		log.Warnf("Clock offset a bit too large: %v, now: %v, prevTS: %v", jetLag, now, prevTS.physicalNano)
	}

	// If the system time jumps back, the 'now' object would be ignored, and the cached physicalNano
	// time would updated based on the previously cached physicalNano time.
	if jetLag < 0 {
		log.Warnf("WARNING: System time may jump back !!! ")
	}

	var nextPhysicalNano uint64
	prevLogical := atomic.LoadUint64(&prevTS.logical)

	// If the current system time is greater, it will be synchronized with the current system time.
	// Which means, the ts is pushed forward as 'now'
	if jetLag > ltsconf.Cfg().SvrConfig.UpdateTimestampGuard.Duration {
		nextPhysicalNano = now
	} else if prevLogical > MaxLogical/2 {
		log.Warnf("the logical time may be not enough, prevLogical: %v", prevLogical)
		nextPhysicalNano = prevTS.physicalNano + uint64(ltsconf.Cfg().SvrConfig.UpdateTimestampGuard.Duration/time.Nanosecond)
	} else {
		// It will still use the previous(i.e., current ts) physicalNano and logic time to alloc TxnTS.
		return nil
	}

	// Apply a new time window: [physicalCacheBarrier, nextPhysicalNano + s]
	if SubTimeByUint64(uint64(svr.PhysicalCacheBarrier.UnixNano()), nextPhysicalNano) <= ltsconf.Cfg().SvrConfig.UpdateTimestampGuard.Duration {
		// That means the last saved time is almost used up
		newSavedTime := nextPhysicalNano + uint64(ltsconf.Cfg().SvrConfig.UpdateTimestampStep.Duration/time.Nanosecond)
		if err := svr.SaveTimestamp2(newSavedTime); err != nil {
			return err
		}
	}

	// Use a new TimestampEntity, starting from nextPhysicalNano physicalNano time and 0 logical time.
	current := &TimestampEntity{
		physicalNano: nextPhysicalNano,
		physicalSec:  nextPhysicalNano / uint64(1e9),
		logical:      0,
	}

	// Update the cached timestamp
	svr.TS.Store(current)
	return nil
}

func (svr *Server) SyncTimestamp() error {
	last, err := svr.LoadTimestamp()
	if err != nil {
		return err
	}

	next := time.Now()

	if SubTime(next, last) < ltsconf.Cfg().SvrConfig.UpdateTimestampGuard.Duration {
		log.Errorf("system time may be incorrect: last: %v next %v", last, next)
		next = last.Add(ltsconf.Cfg().SvrConfig.UpdateTimestampGuard.Duration)
	}

	save := next.Add(ltsconf.Cfg().SvrConfig.UpdateTimestampStep.Duration)
	if err = svr.SaveTimestamp(save); err != nil {
		return err
	}

	log.Infof("Sync and save timestamp: last: %v, cache: %v, barrier: %v", last, next, save)
	current := &TimestampEntity{
		physicalNano: uint64(next.UnixNano()),
		physicalSec:  uint64(next.Unix()),
		logical:      uint64(0),
	}

	svr.TS.Store(current)

	return nil
}
