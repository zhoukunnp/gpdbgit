package ltsapi

import (
	"net/http"

	log "github.com/sirupsen/logrus"
	"github.com/unrolled/render"
	"git.code.oa.com/tdsql_util/LTS/ltsserver"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
)

type clusterHandler struct {
	svr *ltsserver.Server
	rd  *render.Render
}

func NewClusterHandler(svr *ltsserver.Server, rd *render.Render) *clusterHandler {
	return &clusterHandler{
		svr: svr,
		rd:  rd,
	}
}

func (h *clusterHandler) GetClusterConfig(w http.ResponseWriter, r *http.Request) {
	log.Infof("Get cluster config req: %v", r)
	if err := h.rd.JSON(w, http.StatusOK, ltsconf.Cfg()); err != nil {
		log.Errorf("GetClusterConfig err:%v", err)
	}
}
