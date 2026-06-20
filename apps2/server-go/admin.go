package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"
)

// AdminServer exposes read-only observability and light admin operations over HTTP.
// Default port :6970. All writes require Bearer token matching adminToken (if set).
//
// Endpoints:
//
//	GET  /healthz              — 200 OK {"status":"up","uptime":"...","players":N}
//	GET  /admin/status         — full server state snapshot
//	GET  /admin/players        — list connected players
//	POST /admin/kick?id=N      — disconnect player N (auth required)
type AdminServer struct {
	mu         *sync.Mutex
	clients    *map[string]clientInfo
	startTime  time.Time
	adminToken string // empty = no auth required
}

type playerInfo struct {
	ID          uint8   `json:"id"`
	Addr        string  `json:"addr"`
	SceneID     int     `json:"scene_id"`
	X           float32 `json:"x"`
	Y           float32 `json:"y"`
	Z           float32 `json:"z"`
	Yaw         float32 `json:"yaw"`
	PlayerID    string  `json:"player_id,omitempty"`
	DisplayName string  `json:"display_name,omitempty"`
}

type statusResponse struct {
	Status    string       `json:"status"`
	Uptime    string       `json:"uptime"`
	UptimeSec int          `json:"uptime_sec"`
	Players   int          `json:"players"`
	Scenes    map[int]int  `json:"scenes"` // sceneID → player count
	ServerAt  string       `json:"server_at"`
	AdminAt   string       `json:"admin_at"`
}

func newAdminServer(mu *sync.Mutex, clients *map[string]clientInfo, token string) *AdminServer {
	return &AdminServer{
		mu:         mu,
		clients:    clients,
		startTime:  time.Now(),
		adminToken: token,
	}
}

func (a *AdminServer) start(port int) {
	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", a.handleHealthz)
	mux.HandleFunc("/admin/status", a.handleStatus)
	mux.HandleFunc("/admin/players", a.handlePlayers)
	mux.HandleFunc("/admin/kick", a.handleKick)

	addr := fmt.Sprintf(":%d", port)
	log.Printf("[admin] HTTP server on %s", addr)
	if err := http.ListenAndServe(addr, mux); err != nil {
		log.Printf("[admin] server error: %v", err)
	}
}

func (a *AdminServer) checkAuth(w http.ResponseWriter, r *http.Request) bool {
	if a.adminToken == "" {
		return true
	}
	auth := r.Header.Get("Authorization")
	if !strings.HasPrefix(auth, "Bearer ") || strings.TrimPrefix(auth, "Bearer ") != a.adminToken {
		http.Error(w, `{"error":"unauthorized"}`, http.StatusUnauthorized)
		return false
	}
	return true
}

func (a *AdminServer) snapshot() ([]playerInfo, map[int]int) {
	a.mu.Lock()
	defer a.mu.Unlock()
	players := make([]playerInfo, 0, len(*a.clients))
	scenes := make(map[int]int)
	for _, c := range *a.clients {
		players = append(players, playerInfo{
			ID:          c.id,
			Addr:        c.remote.String(),
			SceneID:     c.sceneID,
			X:           float32(c.pos.X),
			Y:           float32(c.pos.Y),
			Z:           float32(c.pos.Z),
			Yaw:         c.yaw,
			PlayerID:    c.playerID,
			DisplayName: c.displayName,
		})
		scenes[c.sceneID]++
	}
	return players, scenes
}

func (a *AdminServer) handleHealthz(w http.ResponseWriter, r *http.Request) {
	players, _ := a.snapshot()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":  "up",
		"uptime":  time.Since(a.startTime).Round(time.Second).String(),
		"players": len(players),
	})
}

func (a *AdminServer) handleStatus(w http.ResponseWriter, _ *http.Request) {
	players, scenes := a.snapshot()
	up := time.Since(a.startTime)
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(statusResponse{
		Status:    "up",
		Uptime:    up.Round(time.Second).String(),
		UptimeSec: int(up.Seconds()),
		Players:   len(players),
		Scenes:    scenes,
		ServerAt:  ":6969",
		AdminAt:   ":6970",
	})
}

func (a *AdminServer) handlePlayers(w http.ResponseWriter, _ *http.Request) {
	players, _ := a.snapshot()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(players)
}

func (a *AdminServer) handleKick(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, `{"error":"POST required"}`, http.StatusMethodNotAllowed)
		return
	}
	if !a.checkAuth(w, r) {
		return
	}
	idStr := r.URL.Query().Get("id")
	targetID, err := strconv.Atoi(idStr)
	if err != nil || targetID < 0 || targetID > 255 {
		http.Error(w, `{"error":"invalid id"}`, http.StatusBadRequest)
		return
	}

	a.mu.Lock()
	var found string
	for key, c := range *a.clients {
		if int(c.id) == targetID {
			found = key
			break
		}
	}
	if found != "" {
		delete(*a.clients, found)
	}
	a.mu.Unlock()

	if found == "" {
		http.Error(w, `{"error":"player not found"}`, http.StatusNotFound)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{"kicked": targetID})
	log.Printf("[admin] kicked player id=%d", targetID)
}
