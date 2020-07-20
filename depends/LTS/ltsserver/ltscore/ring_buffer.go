package ltscore

import (
	log "github.com/sirupsen/logrus"
	"sync/atomic"
)

// Lock free buffer struct. Each element is called a 'bucket', which is
// a sub-slice within the []byte buffer.
// Padding arrays are used to avoid CPU cache false-sharing.
type RingBuffer struct {
	BucketCount uint64 // The amount of requests that can be stored
	_pad1       [7]uint64

	BucketSize uint64 // Size of one single request in bytes
	_pad2      [7]uint64

	MaskValue uint64 // BucketCount - 1
	_pad3     [7]uint64

	ReadCursor uint64 // The current position of the consumer
	_pad4      [7]uint64

	WriteCursor uint64 // The current position of the producer
	_pad5       [7]uint64

	StubWriteCursor uint64 // To enable producing contents in batch for consumer
	_pad6           [7]uint64

	Buf []byte // The actual data buffer
}

func NewRingBuffer(bucketSize uint64, bucketCount uint64) *RingBuffer {
	if bucketCount <= 1 || bucketSize == 0 {
		log.Errorf("Bucket count should be larger than 1 and bucket size should not be zero")
		return nil
	}

	// Make sure the capacity of the ring buffer is a power of 2
	bucketCount = RoundUpToPowerOf2(bucketCount)
	log.Infof("The input capacity is rounded up to: %v", bucketCount)

	rb := &RingBuffer{
		BucketCount:     bucketCount,
		BucketSize:      bucketSize,
		MaskValue:       bucketCount - 1,
		ReadCursor:      uint64(0),
		WriteCursor:     uint64(0),
		StubWriteCursor: uint64(0),
		Buf:             make([]byte, bucketSize*bucketCount, bucketSize*bucketCount),
		_pad1:           [7]uint64{uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0)},
		_pad2:           [7]uint64{uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0)},
		_pad3:           [7]uint64{uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0)},
		_pad4:           [7]uint64{uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0)},
		_pad5:           [7]uint64{uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0)},
		_pad6:           [7]uint64{uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0), uint64(0)},
	}

	return rb
}

// IsEmpty is called by the consumer: compare the read and write cursor
func (rb *RingBuffer) IsEmpty() bool {
	return rb.ReadCursor == atomic.LoadUint64(&rb.WriteCursor)
}

// IsFull is called by the producer: compare the read and STUB write cursor
// Note that even when IsFull() returns true, there is still one empty bucket in the buffer,
// which is the one that StubWriteCursor points to.
// In another word, there can be at most BucketCount - 1 elements within the buffer.
func (rb *RingBuffer) IsFull() bool {
	return atomic.LoadUint64(&rb.ReadCursor) == ((rb.WriteCursor + 1) & rb.MaskValue)
}

// Write writes the new element at the position of stub write cursor
// Return if the write succeeds, and the position of this write
func (rb *RingBuffer) Write(element []byte) (bool, uint64) {
	if rb.IsFull() {
		return false, 0
	}

	oldWriteCursor := rb.WriteCursor
	startOffset := oldWriteCursor * rb.BucketSize

	copy(rb.Buf[startOffset:startOffset+rb.BucketSize], element)

	rb.IncWriteCursor()

	return true, oldWriteCursor
}

// Read copies the content of the bucket that the read cursor points to.
func (rb *RingBuffer) Read(bytes []byte) bool {
	if rb.IsEmpty() {
		return false
	}

	start := rb.ReadCursor * rb.BucketSize
	copy(bytes, rb.Buf[start:start+rb.BucketSize])

	rb.IncReadCursor()

	return true
}

// ReadAt returns the bucket reference at the given position
func (rb *RingBuffer) ReadAt(pos uint64) (bool, []byte) {
	if rb.IsEmpty() {
		return false, nil
	}

	startOffset := pos * rb.BucketSize
	retBuf := rb.Buf[startOffset : startOffset+rb.BucketSize]

	return true, retBuf
}

// IncWriteCursor adds the write cursor by 1
func (rb *RingBuffer) IncWriteCursor() uint64 {
	atomic.StoreUint64(&rb.WriteCursor, (rb.WriteCursor+1)&rb.MaskValue)
	return rb.WriteCursor
}

// IncReadCursor is called by the consumer after one bucket is handled
func (rb *RingBuffer) IncReadCursor() uint64 {
	atomic.StoreUint64(&rb.ReadCursor, (rb.ReadCursor+1)&rb.MaskValue)
	return rb.ReadCursor
}

// SetWriteCursor sets the write cursor to a new position
func (rb *RingBuffer) SetWriteCursor(newPos uint64) {
	atomic.StoreUint64(&rb.WriteCursor, newPos)
}

// WriterGap returns the 'saved-but-yet-unhandled' buffer bucket count
// between stub writer and the real one
func (rb *RingBuffer) WriterGap(swc uint64) uint64 {
	wc := atomic.LoadUint64(&rb.WriteCursor)

	// Empty
	if wc == swc {
		return 0
	}

	// At most BucketCount - 1
	if swc > wc {
		return swc - wc
	}

	return swc + rb.BucketCount - wc
}

// UnhandledBucketCount returns the bucket number between read cursor and stub write cursor
func (rb *RingBuffer) UnhandledBucketCount(stubWriteCursor uint64) uint64 {
	rc, wc := atomic.LoadUint64(&rb.ReadCursor), stubWriteCursor

	// Empty
	if wc == rc {
		return 0
	}

	// At most BucketCount - 1
	if wc > rc {
		return wc - rc
	}

	return wc + rb.BucketCount - rc
}

// UnhandledBucketCount returns the number of buckets between read cursor and stub write cursor
func (rb *RingBuffer) UnhandledCount() uint64 {
	rc, wc := atomic.LoadUint64(&rb.ReadCursor), atomic.LoadUint64(&rb.WriteCursor)

	// Empty
	if wc == rc {
		return 0
	}

	// At most BucketCount - 1
	if wc > rc {
		return wc - rc
	}

	return wc + rb.BucketCount - rc
}

// Clear releases the buffer space
func (rb *RingBuffer) Clear() {
	rb.Buf = make([]byte, 0)
}

// Reset resets the status and content of ring buffer to initial status.
func (rb *RingBuffer) Reset() {
	atomic.StoreUint64(&rb.ReadCursor, 0)
	atomic.StoreUint64(&rb.WriteCursor, 0)
	rb.Buf = make([]byte, rb.BucketSize*rb.BucketCount, rb.BucketSize*rb.BucketCount)
}

// RoundUpToPowerOf2 adjusts the input to the next lowest number of 2^n
func RoundUpToPowerOf2(v uint64) uint64 {
	v--
	v |= v >> 1
	v |= v >> 2
	v |= v >> 4
	v |= v >> 8
	v |= v >> 16
	v |= v >> 32
	v++
	return v
}