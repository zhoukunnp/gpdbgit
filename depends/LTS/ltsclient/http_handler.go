package ltsclient

import (
	"fmt"
	"github.com/unrolled/render"
	"net/http"
)

type ClientHandler struct {
	cs *ClientSimulator
	rd *render.Render
}

func newClientHandler(cs *ClientSimulator, rd *render.Render) *ClientHandler {
	return &ClientHandler{
		cs: cs,
		rd: rd,
	}
}

func (ch *ClientHandler) StopAllWorkers(w http.ResponseWriter, r *http.Request) {

	if err := ch.cs.StopAllWorkers(); err != nil {
		ch.rd.JSON(w, http.StatusInternalServerError, fmt.Sprintf("Failed to stop all workers %v", err))
		return
	}

	ch.rd.JSON(w, http.StatusOK, fmt.Sprintf("Stop all workers OK"))
	return
}

func (ch *ClientHandler) RestartAllWorkers(w http.ResponseWriter, r *http.Request) {

	if err := ch.cs.RestartAllWorkers(); err != nil {
		ch.rd.JSON(w, http.StatusInternalServerError, fmt.Sprintf("Failed to start all workers %v", err))
		return
	}

	ch.rd.JSON(w, http.StatusOK, fmt.Sprintf("Start all workers OK"))
	return
}
