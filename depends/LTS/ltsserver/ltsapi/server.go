package ltsapi

import (
	"net/http"

	"github.com/gorilla/mux"
	"github.com/urfave/negroni"
	"git.code.oa.com/tdsql_util/LTS/ltsserver"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
)

// NewHandler creates a HTTP handler for API.
func NewHandler(svr *ltsserver.Server) http.Handler {
	apiPrefix := ltsconf.Cfg().SvrConfig.LTSAPIPrefix
	engine := negroni.New()

	recovery := negroni.NewRecovery()
	engine.Use(recovery)

	router := mux.NewRouter()
	router.PathPrefix(apiPrefix).Handler(negroni.New(newRedirector(svr),
		negroni.Wrap(CreateRouter(apiPrefix, svr)),
	))

	engine.UseHandler(router)

	return engine
}
