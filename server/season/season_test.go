package season_test

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"

	"dragonsnshit/server/season"
)

func validSeason() season.Season {
	return season.Season{
		ID:      "S1",
		Name:    "TRAPX Closed Alpha",
		StartAt: time.Date(2026, 7, 1, 0, 0, 0, 0, time.UTC),
		EndAt:   time.Date(2026, 10, 1, 0, 0, 0, 0, time.UTC),
		MapPool: []string{"map_a", "map_b"},
	}
}

func TestValidatePasses(t *testing.T) {
	if err := validSeason().Validate(); err != nil {
		t.Errorf("expected valid season to pass, got: %v", err)
	}
}

func TestValidateMissingID(t *testing.T) {
	s := validSeason()
	s.ID = ""
	if err := s.Validate(); err == nil {
		t.Error("expected error for missing ID")
	}
}

func TestValidateMissingName(t *testing.T) {
	s := validSeason()
	s.Name = ""
	if err := s.Validate(); err == nil {
		t.Error("expected error for missing Name")
	}
}

func TestValidateMissingStartAt(t *testing.T) {
	s := validSeason()
	s.StartAt = time.Time{}
	if err := s.Validate(); err == nil {
		t.Error("expected error for zero StartAt")
	}
}

func TestValidateMissingEndAt(t *testing.T) {
	s := validSeason()
	s.EndAt = time.Time{}
	if err := s.Validate(); err == nil {
		t.Error("expected error for zero EndAt")
	}
}

func TestValidateEndBeforeStart(t *testing.T) {
	s := validSeason()
	s.EndAt = s.StartAt.Add(-time.Hour)
	if err := s.Validate(); err == nil {
		t.Error("expected error when EndAt before StartAt")
	}
}

func TestValidateEmptyMapPool(t *testing.T) {
	s := validSeason()
	s.MapPool = nil
	if err := s.Validate(); err == nil {
		t.Error("expected error for empty MapPool")
	}
}

func TestPhaseUpcoming(t *testing.T) {
	s := season.Season{
		ID:      "S1",
		Name:    "Test",
		StartAt: time.Now().UTC().Add(72 * time.Hour),
		EndAt:   time.Now().UTC().Add(100 * 24 * time.Hour),
		MapPool: []string{"m"},
	}
	if p := s.CurrentPhase(); p != season.PhaseUpcoming {
		t.Errorf("want PhaseUpcoming, got %d", p)
	}
	if s.IsActive() {
		t.Error("upcoming season should not be active")
	}
}

func TestPhaseActive(t *testing.T) {
	s := season.Season{
		ID:      "S1",
		Name:    "Test",
		StartAt: time.Now().UTC().Add(-time.Hour),
		EndAt:   time.Now().UTC().Add(72 * time.Hour),
		MapPool: []string{"m"},
	}
	if p := s.CurrentPhase(); p != season.PhaseActive {
		t.Errorf("want PhaseActive, got %d", p)
	}
	if !s.IsActive() {
		t.Error("expected season to be active")
	}
}

func TestPhaseEnded(t *testing.T) {
	s := season.Season{
		ID:      "S1",
		Name:    "Test",
		StartAt: time.Now().UTC().Add(-100 * 24 * time.Hour),
		EndAt:   time.Now().UTC().Add(-time.Hour),
		MapPool: []string{"m"},
	}
	if p := s.CurrentPhase(); p != season.PhaseEnded {
		t.Errorf("want PhaseEnded, got %d", p)
	}
	if s.IsActive() {
		t.Error("ended season should not be active")
	}
}

func TestLobbyBannerActive(t *testing.T) {
	s := season.Season{
		ID:      "S1",
		Name:    "TRAPX Closed Alpha",
		StartAt: time.Now().UTC().Add(-time.Hour),
		EndAt:   time.Date(2026, 10, 1, 0, 0, 0, 0, time.UTC),
		MapPool: []string{"m"},
	}
	banner := s.LobbyBanner()
	if banner == "" {
		t.Error("LobbyBanner should not be empty")
	}
	if banner[:5] != "[S1: " {
		t.Errorf("LobbyBanner should start with [S1: , got: %s", banner)
	}
}

func TestLobbyBannerUpcoming(t *testing.T) {
	s := season.Season{
		ID:      "S2",
		Name:    "Next Season",
		StartAt: time.Date(2027, 1, 1, 0, 0, 0, 0, time.UTC),
		EndAt:   time.Date(2027, 4, 1, 0, 0, 0, 0, time.UTC),
		MapPool: []string{"m"},
	}
	banner := s.LobbyBanner()
	if banner == "" {
		t.Error("LobbyBanner should not be empty")
	}
}

func TestSaveLoad(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "current-season.json")
	s := validSeason()
	if err := season.Save(path, s); err != nil {
		t.Fatalf("save: %v", err)
	}
	loaded, err := season.Load(path)
	if err != nil {
		t.Fatalf("load: %v", err)
	}
	if loaded.ID != s.ID {
		t.Errorf("ID: want %q, got %q", s.ID, loaded.ID)
	}
	if loaded.Name != s.Name {
		t.Errorf("Name: want %q, got %q", s.Name, loaded.Name)
	}
	if len(loaded.MapPool) != len(s.MapPool) {
		t.Errorf("MapPool len: want %d, got %d", len(s.MapPool), len(loaded.MapPool))
	}
}

func TestLoadInvalidJSON(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "bad.json")
	_ = os.WriteFile(path, []byte("{not json}"), 0o644)
	_, err := season.Load(path)
	if err == nil {
		t.Error("expected error for invalid JSON")
	}
}

func TestLoadMissingFile(t *testing.T) {
	_, err := season.Load("/nonexistent/path/season.json")
	if err == nil {
		t.Error("expected error for missing file")
	}
}

func TestSaveInvalidSeason(t *testing.T) {
	dir := t.TempDir()
	s := season.Season{} // missing required fields
	if err := season.Save(filepath.Join(dir, "bad.json"), s); err == nil {
		t.Error("expected error saving invalid season")
	}
}

func TestSeason1IsValid(t *testing.T) {
	if err := season.Season1.Validate(); err != nil {
		t.Errorf("Season1 should be valid, got: %v", err)
	}
}

func TestSeason1MapPool(t *testing.T) {
	if len(season.Season1.MapPool) == 0 {
		t.Error("Season1.MapPool should not be empty")
	}
}

func TestSeasonJSONRoundTrip(t *testing.T) {
	s := validSeason()
	data, err := json.Marshal(s)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	var s2 season.Season
	if err := json.Unmarshal(data, &s2); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if s2.ID != s.ID {
		t.Errorf("ID mismatch after round-trip")
	}
}
