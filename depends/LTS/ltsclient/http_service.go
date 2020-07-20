package ltsclient

import (
	"context"
	"net/http"
	"time"

	"github.com/gorilla/mux"
	"github.com/unrolled/render"
	"github.com/urfave/negroni"

	log "github.com/sirupsen/logrus"
)

const (
	apiPrefix = "/lts-client/api"
)

type HttpServer struct {
	ctx    context.Context
	cancel context.CancelFunc

	cs      *ClientSimulator
	httpSvr *http.Server
}

func NewHttpServer(cs *ClientSimulator) *HttpServer {
	ctx, cancel := context.WithCancel(context.Background())

	return &HttpServer{
		ctx:    ctx,
		cancel: cancel,

		cs: cs,
		httpSvr: &http.Server{
			Addr:           cs.opt.HttpAddr,
			Handler:        NewHttpHandler(cs),
			ReadTimeout:    10 * time.Second,
			WriteTimeout:   10 * time.Second,
			MaxHeaderBytes: 1 << 20,
		},
	}
}

func NewHttpHandler(cs *ClientSimulator) http.Handler {
	engine := negroni.New()

	recovery := negroni.NewRecovery()
	engine.Use(recovery)

	router := mux.NewRouter()
	router.PathPrefix(apiPrefix).Handler(negroni.New(
		negroni.Wrap(createRouter(apiPrefix, cs)),
	))

	engine.UseHandler(router)
	return engine
}

func (hs *HttpServer) Run() {
	defer hs.httpSvr.Close()

	log.Info("To start http service: ", hs.cs.opt.HttpAddr)
	log.Fatal(hs.httpSvr.ListenAndServe())
}

func createRouter(prefix string, cs *ClientSimulator) *mux.Router {
	router := mux.NewRouter().PathPrefix(prefix).Subrouter()
	rd := render.New(render.Options{
		IndentJSON: true,
	})

	clientHandler := newClientHandler(cs, rd)
	router.HandleFunc("/restart-all-workers", clientHandler.RestartAllWorkers).Methods("GET")
	router.HandleFunc("/stop-all-workers", clientHandler.StopAllWorkers).Methods("GET")

	return router
}
