package ltsapi

import (
	"github.com/gorilla/mux"
	log "github.com/sirupsen/logrus"
	"github.com/unrolled/render"
	"git.code.oa.com/tdsql_util/LTS/ltsserver"
	"net/http"
	"strconv"
)

type timestampHandler struct {
	svr *ltsserver.Server
	rd  *render.Render
}

func NewTimestampHandler(svr *ltsserver.Server, rd *render.Render) *timestampHandler {
	return &timestampHandler{
		svr: svr,
		rd:  rd,
	}
}

func (h *timestampHandler) GetTimestamp(w http.ResponseWriter, r *http.Request) {
	log.Infof("Receive GetTimestamp request: %v", r)
	countStr := mux.Vars(r)["count"]
	count, err := strconv.ParseUint(countStr, 10, 64)
	if err != nil {
		h.rd.JSON(w, http.StatusInternalServerError, err.Error())
		return
	}

	ts, getErr := h.svr.GetTimestamp(count)
	if getErr != nil {
		log.Errorf("Try to get ts error: %v", getErr)
		h.rd.JSON(w, http.StatusInternalServerError, getErr.Error())
		return
	}

	if err := h.rd.Text(w, http.StatusOK, strconv.FormatUint(ts, 10)); err != nil {
		log.Errorf("GetTimestamp err:%v", err)
	}

	log.Debugf("return ts: %v", ts)
}
