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
	"encoding/binary"
	"flag"
	"fmt"
	"math"
	"net"
	"os"
	"os/exec"
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
}

func main() {
	host := flag.String("host", "127.0.0.1", "game server host")
	port := flag.Int("port", 6969, "game server UDP port")
	verbose := flag.Bool("v", false, "verbose packet logging")
	noReport := flag.Bool("no-report", false, "disable Emily Prime kill/event reporting")
	flag.Parse()

	addr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", *host, *port))
	if err != nil {
		fmt.Fprintf(os.Stderr, "resolve addr: %v\n", err)
		os.Exit(1)
	}
	conn, err := net.DialUDP("udp", nil, addr)
	if err != nil {
		fmt.Fprintf(os.Stderr, "dial udp: %v\n", err)
		os.Exit(1)
	}
	defer conn.Close()

	now := time.Now()
	state := &botState{
		peers:         make(map[uint8]*peer),
		myY:           spawnY,
		report:        !*noReport,
		sessionStart:  now,
		lastObserved:  now,
		observeMinGap: 15 * time.Second,
	}

	fmt.Printf("[emily-bot] connecting to %s as EMILY_PRIME\n", addr)
	sendConnect(conn)
	state.observe("emily-bot: session start — connecting to SHANKPIT server", "info")

	go receiveLoop(conn, state, *verbose)

	ticker := time.NewTicker(tickInterval)
	defer ticker.Stop()
	for range ticker.C {
		cmd := state.think()
		sendUserCmd(conn, cmd)
	}
}

// think computes the next UserCmd from current game state.
func (s *botState) think() common.UserCmd {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.seq++
	now := time.Now()

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

	if nearest != nil {
		// Aim yaw.
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

		// Move: close in when far, back off when very close.
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
	} else {
		// No peers visible: patrol by spinning.
		s.myYaw += patrolYawRate
		fwd = 0.3
	}

	s.drFwd = fwd
	s.drStr = str

	return common.UserCmd{
		Sequence:  s.seq,
		Timestamp: uint32(now.UnixMilli()),
		Msec:      uint16(tickInterval.Milliseconds()),
		Fwd:       fwd,
		Str:       str,
		Yaw:       s.myYaw,
		Pitch:     s.myPitch,
		Buttons:   buttons,
		WeaponIdx: int32(common.WpnMagnum),
	}
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

// sendConnect sends a PacketConnect with DM game mode.
// Wire format: [0]=type [1:12]=zeros [12]=game_mode  (13 bytes)
func sendConnect(conn *net.UDPConn) {
	buf := make([]byte, 13)
	buf[0] = common.PacketConnect
	buf[12] = common.GameModeDeathmatch
	_, _ = conn.Write(buf)
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
