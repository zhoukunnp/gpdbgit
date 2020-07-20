package ltsapi

import (
	"github.com/gorilla/mux"
	"github.com/unrolled/render"

	"git.code.oa.com/tdsql_util/LTS/ltsserver"
)

func CreateRouter(prefix string, svr *ltsserver.Server) *mux.Router {
	router := mux.NewRouter().PathPrefix(prefix).Subrouter()
	rd := render.New(render.Options{
		IndentJSON: true,
	})

	// Members
	memberHandler := NewMemberHandler(svr, rd)
	router.HandleFunc("/members", memberHandler.ListMembers).Methods("GET")
	router.HandleFunc("/members/delete/{id}", memberHandler.DeleteMemberByID).Methods("DELETE")

	// Leader
	metaLeaderHandler := NewLeaderHandler(svr, rd)
	router.HandleFunc("/leader", metaLeaderHandler.GetLeader).Methods("GET")
	router.HandleFunc("/leader/resign", metaLeaderHandler.Resign).Methods("GET")
	router.HandleFunc("/leader/transfer/{next_leader}", metaLeaderHandler.Transfer).Methods("PUT")

	// Cluster
	clusterHandler := NewClusterHandler(svr, rd)
	router.HandleFunc("/cluster-config", clusterHandler.GetClusterConfig).Methods("GET")

	// Log
	logHandler := NewLoggerHandler(svr, rd)
	router.HandleFunc("/logger/{level}", logHandler.SetLogLevel).Methods("PUT")

	// Timestamp
	tsHandler := NewTimestampHandler(svr, rd)
	router.HandleFunc("/ts/{count}", tsHandler.GetTimestamp).Methods("GET")

	return router
}
