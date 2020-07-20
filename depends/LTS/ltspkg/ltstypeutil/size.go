package ltstypeutil

import (
	gh "github.com/dustin/go-humanize"
	"github.com/juju/errors"
	"strconv"
)

// ByteSize is a retype uint64 for TOML and JSON.
type ByteSize uint64

// MarshalJSON returns the size as a JSON string.
func (b ByteSize) MarshalJSON() ([]byte, error) {
	return []byte(`"` + gh.IBytes(uint64(b)) + `"`), nil
}

// UnmarshalJSON parses a JSON string into the bytesize.
func (b *ByteSize) UnmarshalJSON(text []byte) error {
	s, err := strconv.Unquote(string(text))
	if err != nil {
		return errors.Trace(err)
	}
	v, err := gh.ParseBytes(s)
	if err != nil {
		return errors.Trace(err)
	}
	*b = ByteSize(v)
	return nil
}

// UnmarshalText parses a Toml string into the bytesize.
func (b *ByteSize) UnmarshalText(text []byte) error {
	v, err := gh.ParseBytes(string(text))
	if err != nil {
		return errors.Trace(err)
	}
	*b = ByteSize(v)
	return nil
}
