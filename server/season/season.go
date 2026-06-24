// Package season manages SHANKPIT season configuration and lobby broadcasts.
package season

import (
	"encoding/json"
	"errors"
	"os"
	"time"
)

// Season represents one competitive season of SHANKPIT.
type Season struct {
	ID      string    `json:"id"`
	Name    string    `json:"name"`
	StartAt time.Time `json:"start_at"`
	EndAt   time.Time `json:"end_at"`
	MapPool []string  `json:"map_pool"`
}

// Phase describes the current position of now relative to the season window.
type Phase int

const (
	PhaseUpcoming Phase = iota // StartAt is in the future
	PhaseActive                // now is within [StartAt, EndAt)
	PhaseEnded                 // EndAt is in the past
)

// CurrentPhase returns the phase of the season relative to now (UTC).
func (s Season) CurrentPhase() Phase {
	now := time.Now().UTC()
	if now.Before(s.StartAt) {
		return PhaseUpcoming
	}
	if now.Before(s.EndAt) {
		return PhaseActive
	}
	return PhaseEnded
}

// IsActive returns true when now falls within [StartAt, EndAt).
func (s Season) IsActive() bool { return s.CurrentPhase() == PhaseActive }

// LobbyBanner returns the join message shown to players when they enter the lobby.
// Example: "[SEASON 1: TRAPX Closed Alpha] Active until 2026-10-01"
func (s Season) LobbyBanner() string {
	phase := s.CurrentPhase()
	switch phase {
	case PhaseUpcoming:
		return "[" + s.ID + ": " + s.Name + "] Starting " + s.StartAt.Format("2006-01-02")
	case PhaseActive:
		return "[" + s.ID + ": " + s.Name + "] Active until " + s.EndAt.Format("2006-01-02")
	default:
		return "[" + s.ID + ": " + s.Name + "] Season ended"
	}
}

// Load reads and parses a season JSON file.
// The format is the marshalled Season struct.
func Load(path string) (Season, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return Season{}, err
	}
	var s Season
	if err := json.Unmarshal(data, &s); err != nil {
		return Season{}, err
	}
	if err := s.Validate(); err != nil {
		return Season{}, err
	}
	return s, nil
}

// Save marshals the season to JSON and writes it to path.
func Save(path string, s Season) error {
	if err := s.Validate(); err != nil {
		return err
	}
	data, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(data, '\n'), 0o644)
}

// Validate returns a non-nil error if the season is malformed.
func (s Season) Validate() error {
	if s.ID == "" {
		return errors.New("season.ID must not be empty")
	}
	if s.Name == "" {
		return errors.New("season.Name must not be empty")
	}
	if s.StartAt.IsZero() {
		return errors.New("season.StartAt must be set")
	}
	if s.EndAt.IsZero() {
		return errors.New("season.EndAt must be set")
	}
	if !s.EndAt.After(s.StartAt) {
		return errors.New("season.EndAt must be after StartAt")
	}
	if len(s.MapPool) == 0 {
		return errors.New("season.MapPool must have at least one map")
	}
	return nil
}

// Season1 is the TRAPX Closed Alpha season definition.
var Season1 = Season{
	ID:      "S1",
	Name:    "TRAPX Closed Alpha",
	StartAt: time.Date(2026, 7, 1, 0, 0, 0, 0, time.UTC),
	EndAt:   time.Date(2026, 10, 1, 0, 0, 0, 0, time.UTC),
	MapPool: []string{
		"trapx_downtown",
		"trapx_industrial",
		"trapx_underground",
	},
}
