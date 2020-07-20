package ltsapiutil

import (
	"encoding/json"
	"io"
	"io/ioutil"

	"github.com/juju/errors"
	log "github.com/sirupsen/logrus"
)

// ReadJSON reads a JSON data from r and then close it.
func ReadJSON(r io.ReadCloser, data interface{}) error {
	defer func() {
		if err := r.Close(); err != nil {
			log.Errorf("When reading json, occurred closed err:%v", err)
		}
	}()

	b, err := ioutil.ReadAll(r)
	if err != nil {
		log.Errorf("Failed to read all, err:%v", err)
		return errors.Trace(err)
	}

	err = json.Unmarshal(b, data)
	if err != nil {
		log.Errorf("Failed to unmarshal json, err:%v", err)
		return errors.Trace(err)
	}

	return nil
}
