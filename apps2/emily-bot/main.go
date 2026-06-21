// emily-bot — Emily Prime SHANKPIT player client.
//
// Connects to the Go game server as "EMILY_PRIME", reads PacketSnapshot peer
// positions, applies heuristic aim-and-shoot AI, and sends PacketUserCmd at
// 20 Hz. Runs headless — no SDL, no window.
//
// Usage:
//
//	go run ./apps2/emily-bot                           # connect to localhost:6969
//	go run ./apps2/emily-bot -host 10.0.0.1 -port 6969
//	go run ./apps2/emily-bot -think llm                # (reserved for LLM mode)
//	GOWORK=off go build -o bin/emily-bot ./apps2/emily-bot
package main

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"math"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"dragonsnshit/packages2/common"
)

const (
	tickInterval = 50 * time.Millisecond // 20 Hz
	peerTTL      = 2 * time.Second
	spawnY       = float32(5.0)

	// Heuristic tuning
	closeRange    = float32(8.0)
	farRange      = float32(20.0)
	aimTolerance  = float32(15.0) // degrees within which we shoot
	turnRate      = float32(0.15) // lerp factor toward desired yaw
	pitchRate     = float32(0.10)
	patrolYawRate = float32(5.0) // degrees/tick when no target

	// Dead-reckoning
	moveSpeed = float32(8.0) // server move speed in units/s
	tickDT    = float32(tickInterval) / float32(time.Second)

	// Weapon range thresholds (units)
	rangeClose  = float32(8.0)
	rangeMid    = float32(30.0)
	rangeLong   = float32(50.0)
)

type peer struct {
	id      uint8
	sceneID uint8
	x, y, z float32
	yaw     float32
	seen    time.Time
}

type botState struct {
	mu      sync.Mutex
	myID    uint8
	myX     float32
	myY     float32
	myZ     float32
	myYaw   float32
	myPitch float32
	myHealth float32 // estimated health 0-100; decrements near enemies, resets on respawn
	sceneID uint8
	peers   map[uint8]*peer
	seq     uint32
	kills   int

	// Dead-reckoning: last sent inputs, integrated each tick.
	drFwd float32
	drStr float32

	// Reporting
	report        bool
	sessionStart  time.Time
	lastObserved  time.Time
	observeMinGap time.Duration

	// Replay logging
	replayFile *os.File
	replayTick uint64
	lastSnap   []byte // most recent raw PacketSnapshot for state encoding

	// GPT-2 policy
	gpt2URL    string        // empty = use heuristic
	gpt2Client *http.Client

	// Session management
	done chan struct{} // closed by receiveLoop on disconnect
}

func main() {
	host := flag.String("host", "127.0.0.1", "game server host")
	port := flag.Int("port", 6969, "game server UDP port")
	verbose := flag.Bool("v", false, "verbose packet logging")
	noReport := flag.Bool("no-report", false, "disable Emily Prime kill/event reporting")
	replayDir := flag.String("replay-dir", "", "directory to write replay NDJSON (empty = no replay log)")
	gpt2URL := flag.String("gpt2-url", "", "GPT-2 inference server URL e.g. http://localhost:8088 (empty = heuristic)")
	sessions := flag.Int("sessions", 0, "number of self-play sessions (0 = infinite)")
	sessionDur := flag.Duration("session-duration", 60*time.Second, "max duration per session")
	flag.Parse()

	for i := 1; *sessions == 0 || i <= *sessions; i++ {
		if *sessions > 0 {
			fmt.Printf("[emily-bot] session %d/%d\n", i, *sessions)
		}
		runSession(*host, *port, *verbose, !*noReport, *replayDir, *gpt2URL, *sessionDur)
		if *sessions > 0 && i >= *sessions {
			break
		}
		time.Sleep(500 * time.Millisecond)
	}
	fmt.Printf("[emily-bot] all sessions complete\n")
}

func runSession(host string, port int, verbose, report bool, replayDir, gpt2URL string, duration time.Duration) {
	addr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", host, port))
	if err != nil {
		fmt.Fprintf(os.Stderr, "resolve addr: %v\n", err)
		return
	}
	conn, err := net.DialUDP("udp", nil, addr)
	if err != nil {
		fmt.Fprintf(os.Stderr, "dial udp: %v\n", err)
		return
	}
	defer conn.Close()

	now := time.Now()
	state := &botState{
		peers:         make(map[uint8]*peer),
		myY:           spawnY,
		myHealth:      100,
		report:        report,
		sessionStart:  now,
		lastObserved:  now,
		observeMinGap: 15 * time.Second,
		gpt2URL:       gpt2URL,
		gpt2Client:    &http.Client{Timeout: 200 * time.Millisecond},
		done:          make(chan struct{}),
	}
	if gpt2URL != "" {
		fmt.Printf("[emily-bot] GPT-2 policy active → %s\n", gpt2URL)
	}

	if replayDir != "" {
		if err := os.MkdirAll(replayDir, 0755); err != nil {
			fmt.Fprintf(os.Stderr, "[emily-bot] replay dir: %v\n", err)
		} else {
			fname := filepath.Join(replayDir, now.UTC().Format("20060102-150405Z")+".ndjson")
			f, err := os.Create(fname)
			if err != nil {
				fmt.Fprintf(os.Stderr, "[emily-bot] replay open: %v\n", err)
			} else {
				state.replayFile = f
				defer f.Close()
				fmt.Printf("[emily-bot] replay → %s\n", fname)
			}
		}
	}

	fmt.Printf("[emily-bot] connecting to %s as EMILY_PRIME (session-duration=%s)\n", addr, duration)
	sendConnect(conn)

	go receiveLoop(conn, state, verbose)

	ticker := time.NewTicker(tickInterval)
	defer ticker.Stop()
	deadline := time.After(duration)
	for {
		select {
		case <-state.done:
			fmt.Printf("[emily-bot] session ended (server disconnect) — elapsed=%s kills=%d\n",
				time.Since(now).Round(time.Second), state.kills)
			return
		case <-deadline:
			fmt.Printf("[emily-bot] session ended (duration=%s) — elapsed=%s kills=%d\n",
				duration, time.Since(now).Round(time.Second), state.kills)
			return
		case <-ticker.C:
			cmd := state.think()
			sendUserCmd(conn, cmd)
		}
	}
}

// think computes the next UserCmd from current game state.
func (s *botState) think() common.UserCmd {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.seq++
	now := time.Now()

	// GPT-2 policy path (4 Hz = every 5 ticks at 20 Hz).
	// Falls back to heuristic if server unreachable or returns bad output.
	if s.gpt2URL != "" && s.seq%5 == 0 {
		stateStr := s.serializeState(uint64(s.seq))
		if cmd, err := s.gpt2Policy(stateStr, s.seq, now); err == nil {
			s.drFwd = cmd.Fwd
			s.drStr = cmd.Str
			return cmd
		}
	}

	// Dead-reckoning: advance own position from last tick's inputs.
	yawRad := float64(s.myYaw) * math.Pi / 180.0
	sinY, cosY := float32(math.Sin(yawRad)), float32(math.Cos(yawRad))
	s.myX += (s.drFwd*sinY + s.drStr*cosY) * moveSpeed * tickDT
	s.myZ += (s.drFwd*(-cosY) + s.drStr*sinY) * moveSpeed * tickDT

	// Prune stale peers.
	for id, p := range s.peers {
		if now.Sub(p.seen) > peerTTL {
			delete(s.peers, id)
		}
	}

	// Find nearest peer.
	var nearest *peer
	var nearestDist float32 = 9999
	for _, p := range s.peers {
		dx := p.x - s.myX
		dz := p.z - s.myZ
		d := float32(math.Sqrt(float64(dx*dx + dz*dz)))
		if d < nearestDist {
			nearestDist = d
			nearest = p
		}
	}

	var fwd, str float32
	var buttons uint32
	if nearest == nil {
		nearestDist = 0 // no target — weaponForRange returns Magnum as default
	}

	// Health estimation: decay when an enemy is close, regenerate when clear.
	if nearest != nil && nearestDist < closeRange {
		s.myHealth -= 0.5
		if s.myHealth < 0 {
			s.myHealth = 0
		}
	} else if nearest == nil && s.myHealth < 100 {
		s.myHealth += 0.2
		if s.myHealth > 100 {
			s.myHealth = 100
		}
	}

	// Retreat below 30 HP — flee from nearest enemy, don't shoot.
	retreating := s.myHealth < 30

	if nearest != nil {
		// Aim yaw (always — we need to face the enemy even when retreating).
		dx := nearest.x - s.myX
		dz := nearest.z - s.myZ
		desiredYaw := float32(math.Atan2(float64(dx), float64(-dz)) * 180.0 / math.Pi)

		diff := desiredYaw - s.myYaw
		for diff > 180 {
			diff -= 360
		}
		for diff < -180 {
			diff += 360
		}
		s.myYaw += diff * turnRate

		// Aim pitch.
		dy := nearest.y - (s.myY + 1.62) // eye height
		desiredPitch := float32(math.Atan2(float64(dy), float64(nearestDist)) * 180.0 / math.Pi)
		s.myPitch += (desiredPitch - s.myPitch) * pitchRate

		if retreating {
			// Low HP: flee backwards, strafe to avoid fire.
			fwd = -1.0
			tick := int(now.UnixMilli() / 1000)
			if tick%2 == 0 {
				str = 1.0
			} else {
				str = -1.0
			}
			if s.seq%100 == 0 {
				fmt.Printf("[emily-bot] retreating: hp=%.0f nearest=%.1f\n", s.myHealth, nearestDist)
			}
		} else {
			// Normal combat: close in when far, back off when very close.
			switch {
			case nearestDist > farRange:
				fwd = 1.0
			case nearestDist < closeRange:
				fwd = -0.5
			}

			// Strafe every ~2 seconds to avoid being an easy target.
			tick := int(now.UnixMilli() / 2000)
			if tick%2 == 0 {
				str = 0.7
			} else {
				str = -0.7
			}

			// Shoot when roughly on target.
			if float32(math.Abs(float64(diff))) < aimTolerance {
				buttons |= common.BtnAttack
			}
		}
	} else {
		// No peers visible: patrol by spinning; recover health.
		s.myYaw += patrolYawRate
		fwd = 0.3
	}

	s.drFwd = fwd
	s.drStr = str

	weaponName := []string{"knife", "magnum", "ar", "shotgun", "sniper", "katana"}
	wname := "magnum"
	wi := weaponForRange(nearestDist)
	if wi >= 0 && wi < len(weaponName) {
		wname = weaponName[wi]
	}
	actionStr := fmt.Sprintf("fwd:%.2f str:%.2f yaw_delta:%.1f shoot:%d jump:0 crouch:0 weapon:%s",
		fwd, str, 0.0, btoi(buttons&common.BtnAttack != 0), wname)
	s.logReplay(s.serializeState(uint64(s.seq)), actionStr)

	return common.UserCmd{
		Sequence:  s.seq,
		Timestamp: uint32(now.UnixMilli()),
		Msec:      uint16(tickInterval.Milliseconds()),
		Fwd:       fwd,
		Str:       str,
		Yaw:       s.myYaw,
		Pitch:     s.myPitch,
		Buttons:   buttons,
		WeaponIdx: int32(weaponForRange(nearestDist)),
	}
}

// gpt2Policy calls the GPT-2 inference server with the current state string
// and returns a UserCmd decoded from the generated action tokens.
// Returns an error if the server is unavailable or returns unusable output.
func (s *botState) gpt2Policy(stateStr string, seq uint32, now time.Time) (common.UserCmd, error) {
	reqBody, _ := json.Marshal(map[string]interface{}{
		"prompt":      stateStr,
		"max_tokens":  24,
		"temperature": 0.7,
	})
	resp, err := s.gpt2Client.Post(
		strings.TrimRight(s.gpt2URL, "/")+"/generate",
		"application/json",
		bytes.NewReader(reqBody),
	)
	if err != nil {
		return common.UserCmd{}, err
	}
	defer resp.Body.Close()

	var result struct {
		Text string `json:"text"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return common.UserCmd{}, err
	}

	// Decode action tokens to movement fields.
	fwd, str, yawDelta, shoot, wpn := parseActionTokens(result.Text)
	s.myYaw += yawDelta

	return common.UserCmd{
		Sequence:  seq,
		Timestamp: uint32(now.UnixMilli()),
		Msec:      uint16(tickInterval.Milliseconds()),
		Fwd:       fwd,
		Str:       str,
		Yaw:       s.myYaw,
		Pitch:     s.myPitch,
		Buttons:   shoot,
		WeaponIdx: int32(wpn),
	}, nil
}

// parseActionTokens extracts movement fields from a GPT-2 action token string.
// Format: "fwd:0.8 str:0.2 yaw_delta:-2.3 shoot:1 weapon:ar"
func parseActionTokens(s string) (fwd, str, yawDelta float32, buttons uint32, weaponIdx int) {
	weaponIdx = common.WpnMagnum
	weaponMap := map[string]int{"knife": 0, "magnum": 1, "ar": 2, "shotgun": 3, "sniper": 4, "katana": 5}
	for _, tok := range strings.Fields(s) {
		parts := strings.SplitN(tok, ":", 2)
		if len(parts) != 2 {
			continue
		}
		var f float64
		switch parts[0] {
		case "fwd":
			fmt.Sscanf(parts[1], "%f", &f)
			fwd = float32(f)
		case "str":
			fmt.Sscanf(parts[1], "%f", &f)
			str = float32(f)
		case "yaw_delta":
			fmt.Sscanf(parts[1], "%f", &f)
			yawDelta = float32(f)
		case "shoot":
			if parts[1] == "1" {
				buttons |= common.BtnAttack
			}
		case "jump":
			if parts[1] == "1" {
				buttons |= common.BtnJump
			}
		case "weapon":
			if idx, ok := weaponMap[parts[1]]; ok {
				weaponIdx = idx
			}
		}
	}
	return
}

func btoi(b bool) int {
	if b {
		return 1
	}
	return 0
}

// weaponForRange selects the best weapon for the given target distance.
func weaponForRange(dist float32) int {
	switch {
	case dist > rangeLong:
		return common.WpnSniper
	case dist > rangeMid:
		return common.WpnAR
	default:
		return common.WpnMagnum
	}
}

type replayRecord struct {
	Tick   uint64 `json:"tick"`
	State  string `json:"state"`
	Action string `json:"action"`
}

// logReplay writes one NDJSON record per tick to the replay file.
// state is a natural-language snapshot string; action is an action token string.
func (s *botState) logReplay(stateStr, actionStr string) {
	if s.replayFile == nil {
		return
	}
	s.replayTick++
	rec := replayRecord{Tick: s.replayTick, State: stateStr, Action: actionStr}
	line, err := json.Marshal(rec)
	if err != nil {
		return
	}
	s.replayFile.Write(line)
	s.replayFile.Write([]byte{'\n'})
}

// serializeState builds a compact state string from current bot position + known peers.
// This is a lightweight Go version of scripts/game_state.py serialize_snapshot.
func (s *botState) serializeState(tick uint64) string {
	selfStr := fmt.Sprintf("self pos:%.1f,%.1f yaw:%.0f hp:%.0f", s.myX, s.myZ, s.myYaw, s.myHealth)
	enemies := ""
	count := 0
	for _, p := range s.peers {
		if count >= 4 {
			break
		}
		dx := p.x - s.myX
		dz := p.z - s.myZ
		dist := float32(math.Sqrt(float64(dx*dx + dz*dz)))
		enemies += fmt.Sprintf(" | pos:%.1f,%.1f dist:%.1f", p.x, p.z, dist)
		count++
	}
	if enemies == "" {
		enemies = " none"
	}
	return fmt.Sprintf("shankpit state tick:%d scene:%d\n%s\nenemy:%s",
		tick, s.sceneID, selfStr, enemies)
}

// observe posts a summary to Emily Prime via `emily observe` if reporting is
// enabled and the rate-limit gap has passed. Non-blocking (goroutine).
func (s *botState) observe(summary, severity string) {
	if !s.report {
		return
	}
	s.mu.Lock()
	if time.Since(s.lastObserved) < s.observeMinGap {
		s.mu.Unlock()
		return
	}
	s.lastObserved = time.Now()
	s.mu.Unlock()

	go func() {
		cmd := exec.Command("emily", "observe", "-s", severity, summary)
		if out, err := cmd.CombinedOutput(); err != nil {
			fmt.Fprintf(os.Stderr, "[emily-bot] observe err: %v — %s\n", err, out)
		}
	}()
}

func receiveLoop(conn *net.UDPConn, state *botState, verbose bool) {
	defer func() {
		select {
		case <-state.done:
		default:
			close(state.done)
		}
	}()
	buf := make([]byte, 4096)
	for {
		n, err := conn.Read(buf)
		if err != nil {
			return
		}
		if n < 1 {
			continue
		}

		switch buf[0] {
		case common.PacketWelcome:
			if n < 2 {
				continue
			}
			state.mu.Lock()
			state.myID = buf[1]
			if n >= 10 {
				state.sceneID = buf[9]
			}
			state.mu.Unlock()
			fmt.Printf("[emily-bot] welcomed: clientID=%d sceneID=%d\n", buf[1], state.sceneID)
			state.observe(fmt.Sprintf("emily-bot: connected — clientID=%d sceneID=%d", buf[1], state.sceneID), "info")

		case common.PacketSnapshot:
			if n < 2 {
				continue
			}
			count := int(buf[1])
			const entitySize = 18
			state.mu.Lock()
			now := time.Now()
			off := 2
			for i := 0; i < count && off+entitySize <= n; i++ {
				id := buf[off]
				sceneID := buf[off+1]
				x := math.Float32frombits(binary.LittleEndian.Uint32(buf[off+2:]))
				y := math.Float32frombits(binary.LittleEndian.Uint32(buf[off+6:]))
				z := math.Float32frombits(binary.LittleEndian.Uint32(buf[off+10:]))
				yaw := math.Float32frombits(binary.LittleEndian.Uint32(buf[off+14:]))
				if id == state.myID {
					// Server-authoritative position update — reset dead-reckoning anchor.
					state.myX, state.myY, state.myZ = x, y, z
					if sceneID == state.sceneID {
						state.myYaw = yaw
					}
				} else {
					state.peers[id] = &peer{
						id: id, sceneID: sceneID,
						x: x, y: y, z: z, yaw: yaw,
						seen: now,
					}
				}
				off += entitySize
			}
			if verbose && count > 0 {
				fmt.Printf("[emily-bot] snapshot: %d peers\n", count)
			}
			state.mu.Unlock()

		case common.PacketSceneChange:
			if n < 15 {
				continue
			}
			state.mu.Lock()
			state.sceneID = buf[1]
			state.myX = math.Float32frombits(binary.LittleEndian.Uint32(buf[2:]))
			state.myY = math.Float32frombits(binary.LittleEndian.Uint32(buf[6:]))
			state.myZ = math.Float32frombits(binary.LittleEndian.Uint32(buf[10:]))
			state.mu.Unlock()
			fmt.Printf("[emily-bot] scene change: scene=%d pos=(%.1f,%.1f,%.1f)\n",
				buf[1], state.myX, state.myY, state.myZ)

		case common.PacketImpact:
			hitEntity := n >= 2 && buf[1] == 1
			if hitEntity {
				state.mu.Lock()
				state.kills++
				kills := state.kills
				seq := state.seq
				state.mu.Unlock()
				fmt.Printf("[emily-bot] kill confirmed — total=%d seq=%d\n", kills, seq)
				state.observe(fmt.Sprintf("emily-bot: kill confirmed — total=%d seq=%d", kills, seq), "info")
			} else if verbose {
				fmt.Printf("[emily-bot] impact: hit_entity=false\n")
			}
		}
	}
}

// sendConnect sends a PacketConnect with DM game mode and optional IDUNA JWT.
// Wire format: [0]=type [1:12]=zeros [12]=game_mode [13..268]=JWT (null-padded).
// JWT is loaded from SHANKPIT_AUTH_TOKEN env or ~/.shankpit/auth.json {"token":"..."}.
func sendConnect(conn *net.UDPConn) {
	const jwtFieldLen = 256
	buf := make([]byte, 13+jwtFieldLen)
	buf[0] = common.PacketConnect
	buf[12] = common.GameModeDeathmatch

	if jwt := loadShankpitJWT(); jwt != "" {
		copy(buf[13:], []byte(jwt))
		fmt.Printf("[emily-bot] auth: JWT loaded (%d bytes)\n", len(jwt))
	}
	_, _ = conn.Write(buf)
}

// loadShankpitJWT returns the IDUNA JWT for SHANKPIT auth, or "" if not set.
func loadShankpitJWT() string {
	if tok := os.Getenv("SHANKPIT_AUTH_TOKEN"); tok != "" {
		return tok
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return ""
	}
	data, err := os.ReadFile(filepath.Join(home, ".shankpit", "auth.json"))
	if err != nil {
		return ""
	}
	var authFile struct {
		Token string `json:"token"`
	}
	if err := json.Unmarshal(data, &authFile); err != nil {
		return ""
	}
	return authFile.Token
}

// sendUserCmd encodes a UserCmd into the server's expected wire format.
//
// Wire format: [0]=type [1:12]=net-header-zeros [12]=count=1 [13:49]=UserCmd
// Note: Msec advances the offset by 4 (not 2) on the server side, so 2 padding
// bytes follow Msec in the serialization.
func sendUserCmd(conn *net.UDPConn, cmd common.UserCmd) {
	buf := make([]byte, 49)
	buf[0] = common.PacketUserCmd
	// [1:12] = zeros
	buf[12] = 1 // cmd count
	off := 13
	binary.LittleEndian.PutUint32(buf[off:], cmd.Sequence)
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], cmd.Timestamp)
	off += 4
	binary.LittleEndian.PutUint16(buf[off:], cmd.Msec)
	off += 4 // 2 bytes Msec + 2 bytes padding (server parseUserCmd does off+=4)
	binary.LittleEndian.PutUint32(buf[off:], math.Float32bits(cmd.Fwd))
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], math.Float32bits(cmd.Str))
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], math.Float32bits(cmd.Yaw))
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], math.Float32bits(cmd.Pitch))
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], cmd.Buttons)
	off += 4
	binary.LittleEndian.PutUint32(buf[off:], uint32(cmd.WeaponIdx))
	_, _ = conn.Write(buf)
}
