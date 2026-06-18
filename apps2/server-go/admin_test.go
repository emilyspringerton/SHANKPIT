package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"

	"dragonsnshit/server/system"
)

func makeTestAdmin(token string) (*AdminServer, *map[string]clientInfo, *sync.Mutex) {
	clients := make(map[string]clientInfo)
	var mu sync.Mutex
	return newAdminServer(&mu, &clients, token), &clients, &mu
}

func TestAdminHealthz_empty(t *testing.T) {
	a, _, _ := makeTestAdmin("")
	rec := httptest.NewRecorder()
	a.handleHealthz(rec, httptest.NewRequest("GET", "/healthz", nil))
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rec.Code)
	}
	var body map[string]interface{}
	json.NewDecoder(rec.Body).Decode(&body)
	if body["status"] != "up" {
		t.Errorf("expected status=up, got %v", body["status"])
	}
	if body["players"].(float64) != 0 {
		t.Errorf("expected 0 players")
	}
}

func TestAdminStatus_withPlayers(t *testing.T) {
	a, clients, mu := makeTestAdmin("")
	mu.Lock()
	(*clients)["10.0.0.1:1234"] = clientInfo{id: 1, sceneID: 0, pos: system.Vec3{X: 1, Y: 5, Z: 2}}
	(*clients)["10.0.0.1:5678"] = clientInfo{id: 2, sceneID: 1, pos: system.Vec3{X: 10, Y: 5, Z: 20}}
	mu.Unlock()

	rec := httptest.NewRecorder()
	a.handleStatus(rec, httptest.NewRequest("GET", "/admin/status", nil))
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rec.Code)
	}
	var resp statusResponse
	json.NewDecoder(rec.Body).Decode(&resp)
	if resp.Players != 2 {
		t.Errorf("expected 2 players, got %d", resp.Players)
	}
	if resp.Scenes[0] != 1 || resp.Scenes[1] != 1 {
		t.Errorf("unexpected scene counts: %v", resp.Scenes)
	}
}

func TestAdminKick_noAuth(t *testing.T) {
	a, clients, mu := makeTestAdmin("secret")
	mu.Lock()
	(*clients)["10.0.0.1:1234"] = clientInfo{id: 3}
	mu.Unlock()

	rec := httptest.NewRecorder()
	a.handleKick(rec, httptest.NewRequest("POST", "/admin/kick?id=3", nil))
	if rec.Code != http.StatusUnauthorized {
		t.Fatalf("expected 401 without token, got %d", rec.Code)
	}
	// Player must still be connected after rejected kick.
	mu.Lock()
	defer mu.Unlock()
	if _, ok := (*clients)["10.0.0.1:1234"]; !ok {
		t.Error("player was kicked despite failed auth")
	}
}

func TestAdminKick_withAuth(t *testing.T) {
	a, clients, mu := makeTestAdmin("secret")
	mu.Lock()
	(*clients)["10.0.0.2:9999"] = clientInfo{id: 7}
	mu.Unlock()

	req := httptest.NewRequest("POST", "/admin/kick?id=7", nil)
	req.Header.Set("Authorization", "Bearer secret")
	rec := httptest.NewRecorder()
	a.handleKick(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d: %s", rec.Code, rec.Body.String())
	}
	mu.Lock()
	defer mu.Unlock()
	if _, ok := (*clients)["10.0.0.2:9999"]; ok {
		t.Error("player still connected after successful kick")
	}
}

func TestAdminPlayers_list(t *testing.T) {
	a, clients, mu := makeTestAdmin("")
	mu.Lock()
	(*clients)["1.2.3.4:1111"] = clientInfo{id: 5, sceneID: 2}
	mu.Unlock()

	rec := httptest.NewRecorder()
	a.handlePlayers(rec, httptest.NewRequest("GET", "/admin/players", nil))
	if rec.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", rec.Code)
	}
	var players []playerInfo
	json.NewDecoder(rec.Body).Decode(&players)
	if len(players) != 1 || players[0].ID != 5 || players[0].SceneID != 2 {
		t.Errorf("unexpected players: %+v", players)
	}
}
