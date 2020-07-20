package ltsapi

import (
	"net/http"

	"github.com/gorilla/mux"
	"github.com/unrolled/render"
	"git.code.oa.com/tdsql_util/LTS/ltsserver"

	"fmt"
	log "github.com/sirupsen/logrus"
	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltslogutil"
)

type loggerHandler struct {
	svr *ltsserver.Server
	rd  *render.Render
}

func NewLoggerHandler(svr *ltsserver.Server, rd *render.Render) *loggerHandler {
	return &loggerHandler{
		svr: svr,
		rd:  rd,
	}
}

func (l *loggerHandler) SetLogLevel(w http.ResponseWriter, r *http.Request) {
	vars := mux.Vars(r)
	logLevelStr := vars["level"]

	log.SetLevel(ltslogutil.StringToLogLevel(logLevelStr))
	log.Infof("Set log level: %v", logLevelStr)

	l.rd.JSON(w, http.StatusOK, fmt.Sprintf("Log level: %v. input:%v", log.GetLevel(), logLevelStr))
}
