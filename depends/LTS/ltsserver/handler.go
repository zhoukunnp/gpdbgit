package ltsserver

type Handler struct {
	s *Server
}

func newHandler(svr *Server) *Handler {
	return &Handler{
		s: svr,
	}
}
