package main

import (
	"bufio"
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"
)

type Health struct {
	OK              bool   `json:"ok"`
	Version         string `json:"version"`
	ProtocolVersion string `json:"protocolVersion"`
}

type Capabilities struct {
	ProtocolVersion string            `json:"protocolVersion"`
	Features        map[string]bool   `json:"features"`
	Metadata        map[string]string `json:"metadata,omitempty"`
}

type MenuEntry struct {
	ID      string      `json:"id"`
	Label   string      `json:"label"`
	Kind    string      `json:"kind"`
	Value   interface{} `json:"value,omitempty"`
	Enabled bool        `json:"enabled"`
}

type MenuState struct {
	IsOpen  bool        `json:"isOpen"`
	Path    []string    `json:"path"`
	Entries []MenuEntry `json:"entries"`
}

type Prompt struct {
	ID       string `json:"id"`
	Text     string `json:"text"`
	Priority int    `json:"priority"`
	TargetID string `json:"targetId,omitempty"`
}

type OverlayState struct {
	Environment string                 `json:"environment"`
	Prompts     []Prompt               `json:"prompts"`
	HUD         map[string]interface{} `json:"hud"`
}

type SessionState struct {
	SessionID     string `json:"sessionId"`
	ActiveSceneID string `json:"activeSceneId"`
	ActiveModeID  string `json:"activeModeId"`
}

type UiState struct {
	ProtocolVersion string       `json:"protocolVersion"`
	Session         SessionState `json:"session"`
	Menu            MenuState    `json:"menu"`
	Overlay         OverlayState `json:"overlay"`
}

type IntentEnvelope struct {
	ProtocolVersion string                 `json:"protocolVersion"`
	IntentID        string                 `json:"intentId"`
	Type            string                 `json:"type"`
	Timestamp       int64                  `json:"ts"`
	Payload         map[string]interface{} `json:"payload"`
}

type IntentResult struct {
	IntentID string        `json:"intentId"`
	OK       bool          `json:"ok"`
	Message  string        `json:"message,omitempty"`
	Patch    []interface{} `json:"patch,omitempty"`
}

type Scene struct {
	SceneID string `json:"sceneId"`
	Label   string `json:"label"`
}

type SceneList struct {
	Scenes []Scene `json:"scenes"`
}

type TravelRequest struct {
	SceneID string `json:"sceneId"`
}

type WSEvent struct {
	Type  string  `json:"type"`
	State UiState `json:"state"`
}

type Server struct {
	mu           sync.Mutex
	state        UiState
	clients      map[*wsConn]struct{}
	stateVersion int64
}

func newServer() *Server {
	return &Server{
		state: UiState{
			ProtocolVersion: "v0",
			Session: SessionState{
				SessionID:     fmt.Sprintf("sess-%d", time.Now().UnixNano()),
				ActiveSceneID: "GARAGE_OSAKA",
				ActiveModeID:  "menu",
			},
			Menu: MenuState{
				IsOpen:  true,
				Path:    []string{"PLAY"},
				Entries: defaultMenuEntries(),
			},
			Overlay: OverlayState{
				Environment: "GARAGE_OSAKA",
				Prompts:     []Prompt{},
				HUD:         map[string]interface{}{},
			},
		},
		clients:      make(map[*wsConn]struct{}),
		stateVersion: 1,
	}
}

func defaultMenuEntries() []MenuEntry {
	return []MenuEntry{
		{ID: "mode.demo", Label: "DEMO (SOLO)", Kind: "action", Enabled: true},
		{ID: "mode.battle", Label: "TRAIN", Kind: "action", Enabled: true},
		{ID: "mode.tdm", Label: "TEAM DM (BOTS)", Kind: "action", Enabled: true},
		{ID: "mode.ctf", Label: "LAN CTF", Kind: "action", Enabled: true},
		{ID: "mode.training", Label: "TRAINING", Kind: "action", Enabled: true},
		{ID: "mode.headed_bot", Label: "HEADED BOT", Kind: "action", Enabled: true},
		{ID: "mode.recorder", Label: "RECORDER", Kind: "action", Enabled: true},
		{ID: "mode.garage", Label: "OSAKA GARAGE", Kind: "action", Enabled: true},
		{ID: "mode.city", Label: "CITY", Kind: "action", Enabled: true},
		{ID: "mode.join", Label: "JOIN S.FARTHQ.COM", Kind: "action", Enabled: true},
	}
}

func (s *Server) snapshot() UiState {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.state
}

func (s *Server) updateState(update func(state *UiState)) {
	s.mu.Lock()
	update(&s.state)
	s.stateVersion++
	s.mu.Unlock()
}

func (s *Server) startBroadcaster() {
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()
		var lastVersion int64
		for range ticker.C {
			s.mu.Lock()
			version := s.stateVersion
			s.mu.Unlock()
			if version != lastVersion {
				lastVersion = version
				s.broadcastState()
			}
		}
	}()
}

func (s *Server) broadcastState() {
	state := s.snapshot()
	event := WSEvent{Type: "event.uiState", State: state}
	payload, err := json.Marshal(event)
	if err != nil {
		return
	}

	s.mu.Lock()
	clients := make([]*wsConn, 0, len(s.clients))
	for c := range s.clients {
		clients = append(clients, c)
	}
	s.mu.Unlock()

	for _, c := range clients {
		_ = c.writeText(payload)
	}
}

func (s *Server) handleIntent(intent IntentEnvelope) IntentResult {
	if intent.ProtocolVersion == "" {
		intent.ProtocolVersion = "v0"
	}
	result := IntentResult{IntentID: intent.IntentID, OK: true}
	switch intent.Type {
	case "intent.activate":
		entryID, _ := intent.Payload["entryId"].(string)
		s.updateState(func(state *UiState) {
			state.Menu.IsOpen = false
			switch entryID {
			case "mode.garage":
				state.Session.ActiveSceneID = "GARAGE_OSAKA"
				state.Session.ActiveModeID = "mode.garage"
				state.Overlay.Environment = "GARAGE_OSAKA"
			case "mode.city":
				state.Session.ActiveSceneID = "CITY"
				state.Session.ActiveModeID = "mode.city"
				state.Overlay.Environment = "CITY"
			case "mode.join":
				state.Session.ActiveModeID = "mode.join"
			default:
				state.Session.ActiveSceneID = "STADIUM"
				state.Session.ActiveModeID = entryID
				state.Overlay.Environment = "STADIUM"
			}
		})
	case "intent.open":
		s.updateState(func(state *UiState) {
			state.Menu.IsOpen = true
		})
	case "intent.back":
		s.updateState(func(state *UiState) {
			state.Menu.IsOpen = true
			state.Session.ActiveModeID = "menu"
		})
	case "intent.portalTravel":
		sceneID, _ := intent.Payload["sceneId"].(string)
		if sceneID != "" {
			s.updateState(func(state *UiState) {
				state.Session.ActiveSceneID = sceneID
				state.Overlay.Environment = sceneID
			})
		}
	case "intent.enterVehicle":
		result.Message = "vehicle intent accepted"
	default:
		result.OK = false
		result.Message = "unsupported intent"
	}

	s.broadcastState()
	return result
}

func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, Health{OK: true, Version: "0.1.0", ProtocolVersion: "v0"})
}

func (s *Server) handleCapabilities(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, Capabilities{
		ProtocolVersion: "v0",
		Features: map[string]bool{
			"menus":     true,
			"overlays":  true,
			"scenes":    true,
			"recorder":  false,
			"intents":   true,
			"transport": true,
		},
	})
}

func (s *Server) handleUiState(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, s.snapshot())
}

func (s *Server) handleIntentHTTP(w http.ResponseWriter, r *http.Request) {
	var intent IntentEnvelope
	if err := json.NewDecoder(r.Body).Decode(&intent); err != nil {
		writeJSON(w, IntentResult{IntentID: "", OK: false, Message: "invalid json"})
		return
	}
	if intent.IntentID == "" {
		intent.IntentID = fmt.Sprintf("intent-%d", time.Now().UnixNano())
	}
	result := s.handleIntent(intent)
	writeJSON(w, result)
}

func (s *Server) handleScenes(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, SceneList{Scenes: []Scene{
		{SceneID: "GARAGE_OSAKA", Label: "Osaka Garage"},
		{SceneID: "STADIUM", Label: "Stadium"},
		{SceneID: "VOXWORLD", Label: "Voxworld"},
		{SceneID: "DUST_COMPOUND", Label: "Dust Compound"},
		{SceneID: "OIL_TANKER", Label: "Oil Tanker"},
		{SceneID: "POO_POO_ISLAND", Label: "Poo Poo Island"},
		{SceneID: "CITY", Label: "City"},
	}})
}

func (s *Server) handleTravel(w http.ResponseWriter, r *http.Request) {
	var req TravelRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeJSON(w, IntentResult{IntentID: "", OK: false, Message: "invalid json"})
		return
	}
	s.updateState(func(state *UiState) {
		state.Session.ActiveSceneID = req.SceneID
		state.Overlay.Environment = req.SceneID
	})
	s.broadcastState()
	writeJSON(w, IntentResult{IntentID: "travel", OK: true, Message: "travel requested"})
}

func (s *Server) handleOpenAPI(w http.ResponseWriter, r *http.Request) {
	data, err := os.ReadFile("openapi.yaml")
	if err != nil {
		http.Error(w, "openapi not found", http.StatusNotFound)
		return
	}
	w.Header().Set("Content-Type", "application/yaml")
	_, _ = w.Write(data)
}

func (s *Server) handleWS(w http.ResponseWriter, r *http.Request) {
	ws, err := upgradeWebSocket(w, r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	s.mu.Lock()
	s.clients[ws] = struct{}{}
	s.mu.Unlock()

	_ = ws.writeJSON(WSEvent{Type: "event.uiState", State: s.snapshot()})

	go func() {
		pingTicker := time.NewTicker(5 * time.Second)
		defer func() {
			pingTicker.Stop()
			s.mu.Lock()
			delete(s.clients, ws)
			s.mu.Unlock()
			_ = ws.close()
		}()

		for {
			select {
			case <-pingTicker.C:
				_ = ws.writePing()
			default:
				msg, err := ws.readText()
				if err != nil {
					return
				}
				var intent IntentEnvelope
				if err := json.Unmarshal(msg, &intent); err != nil {
					_ = ws.writeJSON(map[string]string{"type": "event.notice", "message": "invalid intent"})
					continue
				}
				if intent.IntentID == "" {
					intent.IntentID = fmt.Sprintf("intent-%d", time.Now().UnixNano())
				}
				_ = ws.writeJSON(s.handleIntent(intent))
			}
		}
	}()
}

func writeJSON(w http.ResponseWriter, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	enc := json.NewEncoder(w)
	enc.SetIndent("", "  ")
	_ = enc.Encode(data)
}

type wsConn struct {
	conn net.Conn
	r    *bufio.Reader
	mu   sync.Mutex
}

func upgradeWebSocket(w http.ResponseWriter, r *http.Request) (*wsConn, error) {
	if !strings.EqualFold(r.Header.Get("Connection"), "Upgrade") && !strings.Contains(strings.ToLower(r.Header.Get("Connection")), "upgrade") {
		return nil, errors.New("missing upgrade header")
	}
	if !strings.EqualFold(r.Header.Get("Upgrade"), "websocket") {
		return nil, errors.New("missing websocket upgrade")
	}
	key := r.Header.Get("Sec-WebSocket-Key")
	if key == "" {
		return nil, errors.New("missing websocket key")
	}
	accept := computeWebSocketAccept(key)

	h, ok := w.(http.Hijacker)
	if !ok {
		return nil, errors.New("hijacking not supported")
	}
	conn, buf, err := h.Hijack()
	if err != nil {
		return nil, err
	}

	response := fmt.Sprintf("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept)
	if _, err := buf.WriteString(response); err != nil {
		_ = conn.Close()
		return nil, err
	}
	if err := buf.Flush(); err != nil {
		_ = conn.Close()
		return nil, err
	}

	return &wsConn{conn: conn, r: bufio.NewReader(conn)}, nil
}

func computeWebSocketAccept(key string) string {
	const magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
	sum := sha1.Sum([]byte(key + magic))
	return base64.StdEncoding.EncodeToString(sum[:])
}

func (w *wsConn) close() error {
	return w.conn.Close()
}

func (w *wsConn) writeJSON(v interface{}) error {
	data, err := json.Marshal(v)
	if err != nil {
		return err
	}
	return w.writeText(data)
}

func (w *wsConn) writePing() error {
	return w.writeFrame(0x9, nil)
}

func (w *wsConn) writeText(payload []byte) error {
	return w.writeFrame(0x1, payload)
}

func (w *wsConn) writeFrame(opcode byte, payload []byte) error {
	w.mu.Lock()
	defer w.mu.Unlock()

	header := []byte{0x80 | opcode, 0}
	length := len(payload)
	switch {
	case length < 126:
		header[1] = byte(length)
	case length <= 65535:
		header[1] = 126
		ext := make([]byte, 2)
		binary.BigEndian.PutUint16(ext, uint16(length))
		header = append(header, ext...)
	default:
		header[1] = 127
		ext := make([]byte, 8)
		binary.BigEndian.PutUint64(ext, uint64(length))
		header = append(header, ext...)
	}

	if _, err := w.conn.Write(header); err != nil {
		return err
	}
	if length > 0 {
		if _, err := w.conn.Write(payload); err != nil {
			return err
		}
	}
	return nil
}

func (w *wsConn) readText() ([]byte, error) {
	for {
		opcode, payload, err := w.readFrame()
		if err != nil {
			return nil, err
		}
		if opcode == 0x8 {
			return nil, io.EOF
		}
		if opcode == 0x9 {
			_ = w.writeFrame(0xA, payload)
			continue
		}
		if opcode == 0x1 {
			return payload, nil
		}
	}
}

func (w *wsConn) readFrame() (byte, []byte, error) {
	header := make([]byte, 2)
	if _, err := io.ReadFull(w.r, header); err != nil {
		return 0, nil, err
	}
	fin := header[0]&0x80 != 0
	opcode := header[0] & 0x0F
	if !fin {
		return 0, nil, errors.New("fragmented frame not supported")
	}
	masked := header[1]&0x80 != 0
	length := int(header[1] & 0x7F)
	if length == 126 {
		ext := make([]byte, 2)
		if _, err := io.ReadFull(w.r, ext); err != nil {
			return 0, nil, err
		}
		length = int(binary.BigEndian.Uint16(ext))
	} else if length == 127 {
		ext := make([]byte, 8)
		if _, err := io.ReadFull(w.r, ext); err != nil {
			return 0, nil, err
		}
		length64 := binary.BigEndian.Uint64(ext)
		if length64 > 1<<20 {
			return 0, nil, errors.New("frame too large")
		}
		length = int(length64)
	}
	mask := make([]byte, 4)
	if masked {
		if _, err := io.ReadFull(w.r, mask); err != nil {
			return 0, nil, err
		}
	}
	payload := make([]byte, length)
	if length > 0 {
		if _, err := io.ReadFull(w.r, payload); err != nil {
			return 0, nil, err
		}
	}
	if masked {
		for i := 0; i < length; i++ {
			payload[i] ^= mask[i%4]
		}
	}
	return opcode, payload, nil
}

func main() {
	port := flag.Int("port", 17777, "port to listen on")
	flag.Parse()

	srv := newServer()
	srv.startBroadcaster()
	mux := http.NewServeMux()
	mux.HandleFunc("/api/v0/health", srv.handleHealth)
	mux.HandleFunc("/api/v0/capabilities", srv.handleCapabilities)
	mux.HandleFunc("/api/v0/ui/state", srv.handleUiState)
	mux.HandleFunc("/api/v0/ui/intent", srv.handleIntentHTTP)
	mux.HandleFunc("/api/v0/world/scenes", srv.handleScenes)
	mux.HandleFunc("/api/v0/world/travel", srv.handleTravel)
	mux.HandleFunc("/api/v0/ws", srv.handleWS)
	mux.HandleFunc("/openapi.yaml", srv.handleOpenAPI)

	server := &http.Server{
		Addr:              fmt.Sprintf(":%d", *port),
		ReadHeaderTimeout: 5 * time.Second,
		Handler:           mux,
	}

	log.Printf("UI server listening on :%d", *port)
	if err := server.ListenAndServe(); err != nil {
		log.Fatal(err)
	}
}
