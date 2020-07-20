package ltsapi

import (
	"io/ioutil"
	"net/http"
	"net/url"
	"strings"

	log "github.com/sirupsen/logrus"

	"git.code.oa.com/tdsql_util/LTS/ltsserver"
	"git.code.oa.com/tdsql_util/LTS/ltsserver/ltsconf"
)

const (
	RedirectorHeader       = "LTS-Redirector"
	ErrRedirectFailed      = "Redirect failed"
	ErrRedirectToNotLeader = "Redirect to not leader"
)

type redirector struct {
	s *ltsserver.Server
}

func newRedirector(s *ltsserver.Server) *redirector {
	return &redirector{s: s}
}

func (h *redirector) ServeHTTP(w http.ResponseWriter, r *http.Request, next http.HandlerFunc) {
	if h.s.IsLeaderServer() {
		next(w, r)
		return
	}

	// Prevent more than one redirection.
	if name := r.Header.Get(RedirectorHeader); len(name) != 0 {
		log.Errorf("redirect from %v, but %v is not leader", name, h.s.Name())
		http.Error(w, ErrRedirectToNotLeader, http.StatusInternalServerError)
		return
	}

	r.Header.Set(RedirectorHeader, h.s.Name())

	leader := h.s.GetLeader()
	if leader == nil {
		return
	}

	urls, err := ltsconf.ParseUrls(strings.Join(leader.GetClientUrls(), ","))
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	NewCustomReverseProxies(urls).ServeHTTP(w, r)
}

type customReverseProxies struct {
	urls   []url.URL
	client *http.Client
}

func NewCustomReverseProxies(urls []url.URL) *customReverseProxies {
	p := &customReverseProxies{
		client: ltsserver.DialClient,
	}

	p.urls = append(p.urls, urls...)

	return p
}

func (p *customReverseProxies) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	for _, url := range p.urls {
		r.RequestURI = ""
		r.URL.Host = url.Host
		r.URL.Scheme = url.Scheme

		resp, err := p.client.Do(r)
		if err != nil {
			log.Error(err)
			continue
		}

		b, err := ioutil.ReadAll(resp.Body)
		resp.Body.Close()
		if err != nil {
			log.Error(err)
			continue
		}

		for k, vs := range resp.Header {
			for _, v := range vs {
				w.Header().Add(k, v)
			}
		}

		w.WriteHeader(resp.StatusCode)
		if _, err := w.Write(b); err != nil {
			log.Error(err)
			continue
		}

		return
	}

	http.Error(w, ErrRedirectFailed, http.StatusInternalServerError)
}
