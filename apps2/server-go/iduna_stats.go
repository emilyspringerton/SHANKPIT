package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"sync"
	"time"
)

// reportPlayerSession POSTs kill/death/session stats to IDUNA when a player disconnects.
// Non-blocking: called from a goroutine. Safe to ignore errors (stats are best-effort).
func reportPlayerSession(idunaURL, jwtToken, playerID string, kills, deaths int) {
	if idunaURL == "" || playerID == "" {
		return
	}
	body, _ := json.Marshal(map[string]int{"kills": kills, "deaths": deaths})
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	req, err := http.NewRequestWithContext(ctx, http.MethodPost,
		idunaURL+"/api/v1/players/"+playerID+"/session",
		bytes.NewReader(body),
	)
	if err != nil {
		return
	}
	req.Header.Set("Content-Type", "application/json")
	if jwtToken != "" {
		req.Header.Set("Authorization", "Bearer "+jwtToken)
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		fmt.Printf("[iduna-stats] session report failed for player=%s: %v\n", playerID, err)
		return
	}
	resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		fmt.Printf("[iduna-stats] session report player=%s status=%d\n", playerID, resp.StatusCode)
	} else {
		fmt.Printf("[iduna-stats] session reported player=%s kills=%d\n", playerID, kills)
	}
}

// getEnvFallback returns the env var value or def if unset.
func getEnvFallback(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

// pruneIdleClients removes clients not seen for idleTimeout and reports their stats.
// Runs as a goroutine. Calls idunaURL stat endpoint for authenticated players.
func pruneIdleClients(mu *sync.Mutex, clients map[string]clientInfo, idleTimeout time.Duration, idunaURL, serverToken string) {
	ticker := time.NewTicker(15 * time.Second)
	defer ticker.Stop()
	for range ticker.C {
		now := time.Now()
		mu.Lock()
		var pruned []clientInfo
		for key, c := range clients {
			if !c.lastPacket.IsZero() && now.Sub(c.lastPacket) > idleTimeout {
				pruned = append(pruned, c)
				delete(clients, key)
			}
		}
		mu.Unlock()
		for _, c := range pruned {
			fmt.Printf("[LOBBY] client=%d (%s) idle timeout — kills=%d\n", c.id, c.displayName, c.kills)
			if c.playerID != "" {
				go reportPlayerSession(idunaURL, serverToken, c.playerID, c.kills, 0)
			}
		}
	}
}
