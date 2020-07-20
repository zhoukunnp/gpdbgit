package ltsapi

import (
	"context"
	"fmt"
	"net/http"
	"strconv"

	"github.com/gorilla/mux"
	"github.com/juju/errors"
	log "github.com/sirupsen/logrus"
	"github.com/unrolled/render"

	"git.code.oa.com/tdsql_util/LTS/ltspkg/ltsetcdutil"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"git.code.oa.com/tdsql_util/LTS/ltsserver"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
)

type memberHandler struct {
	svr *ltsserver.Server
	rd  *render.Render
}

func NewMemberHandler(svr *ltsserver.Server, rd *render.Render) *memberHandler {
	return &memberHandler{
		svr: svr,
		rd:  rd,
	}
}

func (h *memberHandler) ListMembers(w http.ResponseWriter, r *http.Request) {
	req := &ltsrpc.GetMembersRequest{
		Header: &ltsrpc.RequestHeader{
			ClusterId: h.svr.ClusterID,
		},
	}

	log.Infof("ListMembers, req:%v", req)

	resp, err := h.svr.GetMembers(context.Background(), req)
	if err != nil {
		h.rd.JSON(w, http.StatusInternalServerError, err.Error())
		return
	}
	h.rd.JSON(w, http.StatusOK, resp)
}

func (h *memberHandler) listMembers() (*ltsrpc.GetMembersResponse, error) {
	req := &ltsrpc.GetMembersRequest{Header: &ltsrpc.RequestHeader{ClusterId: h.svr.ClusterID}}
	members, err := h.svr.GetMembers(context.Background(), req)
	return members, errors.Trace(err)
}

func (h *memberHandler) DeleteMemberByID(w http.ResponseWriter, r *http.Request) {
	idStr := mux.Vars(r)["id"]
	id, err := strconv.ParseUint(idStr, 10, 64)
	if err != nil {
		h.rd.JSON(w, http.StatusInternalServerError, err.Error())
		return
	}

	log.Infof("DeleteMemberByID:%v", id)

	// Delete config.
	err = h.svr.DeleteMemberLeaderPriority(id)
	if err != nil {
		h.rd.JSON(w, http.StatusInternalServerError, err.Error())
		return
	}

	client := h.svr.GetClient()
	_, err = ltsetcdutil.RemoveEtcdMember(client, id, ltsconf.Cfg().Etcd.KVRequestTimeout.Duration)
	if err != nil {
		h.rd.JSON(w, http.StatusInternalServerError, err.Error())
		return
	}
	h.rd.JSON(w, http.StatusOK, fmt.Sprintf("removed, meta: %v", id))
}

type leaderHandler struct {
	svr *ltsserver.Server
	rd  *render.Render
}

func NewLeaderHandler(svr *ltsserver.Server, rd *render.Render) *leaderHandler {
	return &leaderHandler{
		svr: svr,
		rd:  rd,
	}
}

func (h *leaderHandler) GetLeader(w http.ResponseWriter, r *http.Request) {
	leader := h.svr.GetLeader()
	if leader == nil {
		h.rd.JSON(w, http.StatusInternalServerError, "Leader not found")
		return
	}

	h.rd.JSON(w, http.StatusOK, leader)
}

func (h *leaderHandler) Resign(w http.ResponseWriter, r *http.Request) {
	err := h.svr.ResignLeader("")
	if err != nil {
		h.rd.JSON(w, http.StatusInternalServerError, err.Error())
		return
	}

	leader := h.svr.GetLeader()
	if leader == nil {
		h.rd.JSON(w, http.StatusInternalServerError, "Leader not found")
		return
	}

	h.rd.JSON(w, http.StatusOK, fmt.Sprintf("New leader:%v", leader.Name))
}

func (h *leaderHandler) Transfer(w http.ResponseWriter, r *http.Request) {
	leaderName := mux.Vars(r)["next_leader"]
	if len(leaderName) == 0 {
		h.rd.JSON(w, http.StatusInternalServerError, "Failed to do leader transfer, not found leader name from request.")
		return
	}

	leader := h.svr.GetLeader()
	if leader == nil {
		h.rd.JSON(w, http.StatusInternalServerError, "Leader not found")
		return
	}

	oldName := leader.Name
	if err := h.svr.ResignLeader(leaderName); err != nil {
		h.rd.JSON(w, http.StatusInternalServerError, err.Error())
		return
	}

	h.rd.JSON(w, http.StatusOK, fmt.Sprintf("OK, leader transferred from %v to leander: %v", oldName, leaderName))
}
