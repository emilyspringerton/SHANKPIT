package main

import (
	"encoding/binary"
	"fmt"
	"math"
	"net"
	"sync"
	"time"

	"dragonsnshit/packages2/common"
	"dragonsnshit/server/player"
	"dragonsnshit/server/store"
	"dragonsnshit/server/system"
)

// gameWorld implements player.RaycastWorld and enforces scene isolation:
// RayTrace only hits clients in the same scene as the shooter.
type gameWorld struct {
	clients   map[string]clientInfo
	mu        *sync.Mutex
	shooterID uint8
	sceneID   int
}

// gameEntityHit is the result when the ray intersects a same-scene client.
type gameEntityHit struct {
	pos      system.Vec3
	clientID uint8
}

func (h gameEntityHit) Position() system.Vec3        { return h.pos }
func (h gameEntityHit) Entity() player.LivingEntity  { return nopEntity{} }

// nopEntity satisfies player.LivingEntity; actual damage routing is handled
// by the caller after HandleShankFire returns hitEntity=true.
type nopEntity struct{}

func (nopEntity) Hurt(_ float64, _ player.DamageSource) {}

const hitboxRadius = 0.4

func (gw *gameWorld) RayTrace(start, end system.Vec3) (player.RaycastResult, bool) {
	dir := end.Sub(start)
	maxDist := dir.Len()
	if maxDist < 1e-6 {
		return nil, false
	}
	dirN := dir.Mul(1.0 / maxDist)

	gw.mu.Lock()
	defer gw.mu.Unlock()

	bestT := maxDist + 1
	var bestHit *gameEntityHit

	for _, c := range gw.clients {
		if c.id == gw.shooterID || c.sceneID != gw.sceneID {
			continue // cross-scene attack guard: skip other-scene clients
		}
		// Approximate each player as a sphere centered at chest height.
		center := system.Vec3{X: c.pos.X, Y: c.pos.Y + 0.9, Z: c.pos.Z}
		w := center.Sub(start)
		t := w.Dot(dirN)
		if t < 0 || t > maxDist {
			continue
		}
		closest := start.Add(dirN.Mul(t))
		if center.Sub(closest).Len() < hitboxRadius && t < bestT {
			bestT = t
			h := gameEntityHit{pos: closest, clientID: c.id}
			bestHit = &h
		}
	}

	if bestHit != nil {
		return *bestHit, true
	}
	return nil, false
}

type shankPlayer struct {
	pos       system.Vec3
	eyeHeight float64
	rw        player.RaycastWorld
}

func (p *shankPlayer) Position() system.Vec3        { return p.pos }
func (p *shankPlayer) EyeHeight() float64           { return p.eyeHeight }
func (p *shankPlayer) World() player.RaycastWorld   { return p.rw }
func (p *shankPlayer) SendSound(name string, pos system.Vec3) {
	fmt.Printf("[sound] %s at %.2f %.2f %.2f\n", name, pos.X, pos.Y, pos.Z)
}

type voxelBlock struct {
	x       uint8
	y       uint8
	z       uint8
	blockID uint16
}

type chunkCoord struct {
	x int
	z int
}

const (
	ctfMinPlayers = 2           // start a CTF match when this many players are queued
	dmWarmupScene = 3           // SCENE_DUST_COMPOUND: where players warm up while searching
	ctfScene      = 1           // SCENE_STADIUM: where CTF matches take place
)

type clientInfo struct {
	id                  uint8
	lastVoxelSent       time.Time
	chunkIndex          int
	sceneID             int
	requestedMode       uint8
	portalCooldownUntil time.Time
	pos                 system.Vec3
	yaw                 float32
	remote              *net.UDPAddr
}

// matchmaker queues players for specific game modes. Players immediately join a
// DM warm-up session; once enough players share a mode, they are moved together.
type matchmaker struct {
	mu       sync.Mutex
	ctfQueue []string // client remote-addr keys
}

func (m *matchmaker) enqueue(key string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	for _, k := range m.ctfQueue {
		if k == key {
			return
		}
	}
	m.ctfQueue = append(m.ctfQueue, key)
}

func (m *matchmaker) remove(key string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	for i, k := range m.ctfQueue {
		if k == key {
			m.ctfQueue = append(m.ctfQueue[:i], m.ctfQueue[i+1:]...)
			return
		}
	}
}

// pollForMatch returns the matched client keys and clears the queue if the
// threshold is met, otherwise returns nil.
func (m *matchmaker) pollForMatch() []string {
	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.ctfQueue) < ctfMinPlayers {
		return nil
	}
	matched := append([]string{}, m.ctfQueue...)
	m.ctfQueue = m.ctfQueue[:0]
	return matched
}

func main() {
	addr, err := net.ResolveUDPAddr("udp", ":6969")
	if err != nil {
		panic(err)
	}
	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		panic(err)
	}
	defer conn.Close()

	fmt.Println("Go backend listening on :6969")
	buf := make([]byte, 2048)
	clientStore := store.NewMemoryClientStore()
	clients := make(map[string]clientInfo)
	nextClientID := uint8(0)
	var mu sync.Mutex
	mm := &matchmaker{}
	var backend system.WorldBackend = &system.StaticBackend{}

	go broadcastSnapshots(conn, &mu, clients)

	for {
		// 250 ms is the read *poll timeout* — not the tick rate.
		// The server processes packets immediately as they arrive; this deadline
		// just prevents the loop from blocking forever when the server is idle.
		// Snapshots are sent independently by broadcastSnapshots at ~30 Hz.
		conn.SetReadDeadline(time.Now().Add(250 * time.Millisecond))
		n, remote, err := conn.ReadFromUDP(buf)
		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			fmt.Printf("read error: %v\n", err)
			continue
		}
		if n < 1 {
			continue
		}
		const netHeaderSize = 12
		const userCmdSize = 36
		switch buf[0] {
		case common.PacketConnect:
			var requestedMode uint8 = common.GameModeDeathmatch
			if n > netHeaderSize {
				requestedMode = buf[netHeaderSize]
			}
			mu.Lock()
			info, ok := clients[remote.String()]
			if !ok {
				info = clientInfo{
					id:            nextClientID,
					remote:        remote,
					sceneID:       dmWarmupScene,
					requestedMode: requestedMode,
				}
				if nextClientID < 255 {
					nextClientID++
				}
				clients[remote.String()] = info
			}
			mu.Unlock()
			// Always place the connecting player in the DM warm-up session immediately.
			sendWelcome(conn, remote, info.id, dmWarmupScene, common.GameModeDeathmatch)
			sendVoxelPacket(conn, remote, info, backend)
			fmt.Printf("[LOBBY] client=%d addr=%s requested_mode=%d → warm-up dm scene=%d\n",
				info.id, remote.String(), requestedMode, dmWarmupScene)

			if requestedMode == common.GameModeCTF {
				mm.enqueue(remote.String())
				fmt.Printf("[MATCHMAKER] CTF queue size=%d threshold=%d\n", len(mm.ctfQueue)+1, ctfMinPlayers)
				if matched := mm.pollForMatch(); matched != nil {
					fmt.Printf("[MATCHMAKER] CTF match starting — moving %d players to scene=%d\n", len(matched), ctfScene)
					spawn := system.Vec3{X: 0, Y: 5, Z: 0}
					mu.Lock()
					for _, key := range matched {
						if ci, ok2 := clients[key]; ok2 {
							ci.sceneID = ctfScene
							clients[key] = ci
							sendSceneChange(conn, ci.remote, ctfScene, spawn, common.GameModeCTF)
						}
					}
					mu.Unlock()
				}
			}
		case common.PacketUserCmd:
			if n < netHeaderSize+1+userCmdSize {
				continue
			}
			mu.Lock()
			info, ok := clients[remote.String()]
			if !ok {
				info = clientInfo{id: nextClientID, remote: remote}
				if nextClientID < 255 {
					nextClientID++
				}
			}
			info.remote = remote
			mu.Unlock()
			info = sendVoxelPacket(conn, remote, info, backend)
			count := int(buf[netHeaderSize])
			if count < 1 {
				mu.Lock()
				clients[remote.String()] = info
				mu.Unlock()
				continue
			}
			cmd := parseUserCmd(buf, netHeaderSize+1)
			clientStore.Upsert(remote.String(), cmd)
			info.yaw = cmd.Yaw
			if cmd.Buttons&common.BtnAttack != 0 {
				gw := &gameWorld{clients: clients, mu: &mu, shooterID: info.id, sceneID: info.sceneID}
				shooter := &shankPlayer{pos: info.pos, eyeHeight: 1.62, rw: gw}
				hit, pos, hitEntity := player.HandleShankFire(shooter, float64(cmd.Yaw), float64(cmd.Pitch), int(cmd.WeaponIdx))
				if hit {
					sendImpact(conn, remote, pos, hitEntity, 0)
				}
			}
			if cmd.Buttons&common.BtnUse != 0 && time.Now().After(info.portalCooldownUntil) {
				portalID, triggered := system.PortalTriggered(info.sceneID, info.pos.X, info.pos.Z)
				if triggered {
					dest, ok := backend.ResolvePortalDestination(info.sceneID, portalID, int(info.id))
					if ok {
						fmt.Printf("[PORTAL] client=%d from_scene=%d to_scene=%d dest=(%.1f,%.1f,%.1f)\n",
							info.id, info.sceneID, dest.Scene, dest.X, dest.Y, dest.Z)
						backend.OnPlayerLeaveScene(int(info.id), info.sceneID)
						info.sceneID = dest.Scene
						info.pos = system.Vec3{X: dest.X, Y: dest.Y, Z: dest.Z}
						info.portalCooldownUntil = time.Now().Add(time.Second)
						backend.OnPlayerEnterScene(int(info.id), dest.Scene)
						sendSceneChange(conn, remote, dest.Scene, info.pos, common.GameModeDeathmatch)
					}
				}
			}
			mu.Lock()
			clients[remote.String()] = info
			mu.Unlock()
		}
	}
}

// sendWelcome sends a 13-byte welcome packet.
// Layout mirrors the C NetHeader (12 bytes with padding) + 1 game_mode byte:
//   [0]=type [1]=clientID [2:4]=seq [4:8]=timestamp [8]=entity_count [9]=scene_id [10:12]=pad [12]=game_mode
func sendWelcome(conn *net.UDPConn, remote *net.UDPAddr, id uint8, sceneID int, gameMode uint8) {
	payload := make([]byte, 13)
	payload[0] = common.PacketWelcome
	payload[1] = id
	binary.LittleEndian.PutUint32(payload[4:], uint32(time.Now().UnixMilli()))
	payload[8] = 0 // entity_count
	payload[9] = uint8(sceneID)
	payload[12] = gameMode
	_, _ = conn.WriteToUDP(payload, remote)
}

func sendVoxelPacket(conn *net.UDPConn, remote *net.UDPAddr, info clientInfo, backend system.WorldBackend) clientInfo {
	now := time.Now()
	if now.Sub(info.lastVoxelSent) < 500*time.Millisecond {
		return info
	}
	chunks := nearbyChunks(0, 0, 1)
	if len(chunks) == 0 {
		return info
	}
	chunk := chunks[info.chunkIndex%len(chunks)]

	// Route through WorldBackend seam; fall back to procedural generator when backend returns nil.
	var blocks []voxelBlock
	if backendBlocks := backend.SceneVoxelPayload(info.sceneID, chunk.x, chunk.z); backendBlocks != nil {
		for _, b := range backendBlocks {
			blocks = append(blocks, voxelBlock{x: b.X, y: b.Y, z: b.Z, blockID: b.BlockID})
		}
	} else {
		blocks = scanChunkForVoxelBlocks(chunk.x, chunk.z)
	}

	headerSize := 16
	blockSize := 6
	payload := make([]byte, headerSize+len(blocks)*blockSize)
	payload[0] = common.PacketVoxelData
	binary.LittleEndian.PutUint32(payload[4:], uint32(chunk.x))
	binary.LittleEndian.PutUint32(payload[8:], uint32(chunk.z))
	binary.LittleEndian.PutUint16(payload[12:], uint16(len(blocks)))

	offset := headerSize
	for _, blk := range blocks {
		payload[offset] = blk.x
		payload[offset+1] = blk.y
		payload[offset+2] = blk.z
		payload[offset+3] = 0
		binary.LittleEndian.PutUint16(payload[offset+4:], blk.blockID)
		offset += blockSize
	}
	_, _ = conn.WriteToUDP(payload, remote)
	info.lastVoxelSent = now
	info.chunkIndex++
	return info
}

func sendImpact(conn *net.UDPConn, remote *net.UDPAddr, pos system.Vec3, hitEntity bool, blockID uint16) {
	payload := make([]byte, 20)
	payload[0] = common.PacketImpact
	if hitEntity {
		payload[1] = 1
	}
	binary.LittleEndian.PutUint32(payload[4:], math.Float32bits(float32(pos.X)))
	binary.LittleEndian.PutUint32(payload[8:], math.Float32bits(float32(pos.Y)))
	binary.LittleEndian.PutUint32(payload[12:], math.Float32bits(float32(pos.Z)))
	binary.LittleEndian.PutUint16(payload[16:], blockID)
	_, _ = conn.WriteToUDP(payload, remote)
}

// sendSceneChange tells a client their new scene, spawn position, and game mode.
// Format: type(1) + sceneID(1) + x(4) + y(4) + z(4) + gameMode(1) = 15 bytes.
func sendSceneChange(conn *net.UDPConn, remote *net.UDPAddr, sceneID int, pos system.Vec3, gameMode uint8) {
	payload := make([]byte, 15)
	payload[0] = common.PacketSceneChange
	payload[1] = uint8(sceneID)
	binary.LittleEndian.PutUint32(payload[2:], math.Float32bits(float32(pos.X)))
	binary.LittleEndian.PutUint32(payload[6:], math.Float32bits(float32(pos.Y)))
	binary.LittleEndian.PutUint32(payload[10:], math.Float32bits(float32(pos.Z)))
	payload[14] = gameMode
	_, _ = conn.WriteToUDP(payload, remote)
}

// broadcastSnapshots runs at ~30Hz (matching the C server's SERVER_SNAPSHOT_INTERVAL_TICKS=2
// at 60Hz tick = 32ms) and sends each client a PacketSnapshot of peers in the same scene.
// Per-entity: clientID(1) + sceneID(1) + x(4) + y(4) + z(4) + yaw(4) = 18 bytes.
func broadcastSnapshots(conn *net.UDPConn, mu *sync.Mutex, clients map[string]clientInfo) {
	const entitySize = 18
	ticker := time.NewTicker(33 * time.Millisecond)
	defer ticker.Stop()
	for range ticker.C {
		mu.Lock()
		type peerEntry struct {
			info   clientInfo
			remote *net.UDPAddr
		}
		all := make([]peerEntry, 0, len(clients))
		for _, info := range clients {
			if info.remote != nil {
				all = append(all, peerEntry{info, info.remote})
			}
		}
		mu.Unlock()

		for _, recv := range all {
			var peers []clientInfo
			for _, e := range all {
				if e.info.sceneID == recv.info.sceneID && e.info.id != recv.info.id {
					peers = append(peers, e.info)
				}
			}
			if len(peers) == 0 {
				continue
			}
			payload := make([]byte, 2+len(peers)*entitySize)
			payload[0] = common.PacketSnapshot
			payload[1] = uint8(len(peers))
			off := 2
			for _, peer := range peers {
				payload[off] = peer.id
				payload[off+1] = uint8(peer.sceneID)
				binary.LittleEndian.PutUint32(payload[off+2:], math.Float32bits(float32(peer.pos.X)))
				binary.LittleEndian.PutUint32(payload[off+6:], math.Float32bits(float32(peer.pos.Y)))
				binary.LittleEndian.PutUint32(payload[off+10:], math.Float32bits(float32(peer.pos.Z)))
				binary.LittleEndian.PutUint32(payload[off+14:], math.Float32bits(peer.yaw))
				off += entitySize
			}
			_, _ = conn.WriteToUDP(payload, recv.remote)
		}
	}
}

func parseUserCmd(data []byte, offset int) common.UserCmd {
	off := offset
	cmd := common.UserCmd{}
	cmd.Sequence = binary.LittleEndian.Uint32(data[off:])
	off += 4
	cmd.Timestamp = binary.LittleEndian.Uint32(data[off:])
	off += 4
	cmd.Msec = binary.LittleEndian.Uint16(data[off:])
	off += 4
	cmd.Fwd = math.Float32frombits(binary.LittleEndian.Uint32(data[off:]))
	off += 4
	cmd.Str = math.Float32frombits(binary.LittleEndian.Uint32(data[off:]))
	off += 4
	cmd.Yaw = math.Float32frombits(binary.LittleEndian.Uint32(data[off:]))
	off += 4
	cmd.Pitch = math.Float32frombits(binary.LittleEndian.Uint32(data[off:]))
	off += 4
	cmd.Buttons = binary.LittleEndian.Uint32(data[off:])
	off += 4
	cmd.WeaponIdx = int32(binary.LittleEndian.Uint32(data[off:]))
	return cmd
}

const (
	chunkSize    = 16
	chunkHeight  = 16
	stoneBlockID = 1
	grassBlockID = 2
	dirtBlockID  = 3
	logBlockID   = 17
	leafBlockID  = 18
	groundY      = 2 // grass surface y; trees grow at groundY+1
)

func nearbyChunks(centerX, centerZ, radius int) []chunkCoord {
	if radius < 0 {
		return nil
	}
	chunks := make([]chunkCoord, 0, (radius*2+1)*(radius*2+1))
	for dz := -radius; dz <= radius; dz++ {
		for dx := -radius; dx <= radius; dx++ {
			chunks = append(chunks, chunkCoord{x: centerX + dx, z: centerZ + dz})
		}
	}
	return chunks
}

func scanChunkForVoxelBlocks(chunkX, chunkZ int) []voxelBlock {
	blocks := make([]uint16, chunkSize*chunkHeight*chunkSize)

	// Flat ground: stone subsurface (y=0,1), grass surface (y=groundY).
	for z := 0; z < chunkSize; z++ {
		for x := 0; x < chunkSize; x++ {
			setBlock(blocks, x, 0, z, stoneBlockID)
			setBlock(blocks, x, 1, z, stoneBlockID)
			setBlock(blocks, x, groundY, z, grassBlockID)
		}
	}

	// Trees grow one block above the grass surface.
	for _, tree := range treeSeedsForChunk(chunkX, chunkZ) {
		placeTree(blocks, tree.x, tree.z, groundY+1)
	}

	results := make([]voxelBlock, 0, 300)
	for y := 0; y < chunkHeight; y++ {
		for z := 0; z < chunkSize; z++ {
			for x := 0; x < chunkSize; x++ {
				blockID := blocks[chunkIndex(x, y, z)]
				if blockID == 0 {
					continue
				}
				results = append(results, voxelBlock{
					x:       uint8(x),
					y:       uint8(y),
					z:       uint8(z),
					blockID: blockID,
				})
			}
		}
	}
	return results
}

type treeSeed struct {
	x int
	z int
}

func treeSeedsForChunk(chunkX, chunkZ int) []treeSeed {
	switch {
	case chunkX == 0 && chunkZ == 0:
		return []treeSeed{{x: 8, z: 8}, {x: 3, z: 12}}
	case chunkX == 1 && chunkZ == 0:
		return []treeSeed{{x: 6, z: 5}}
	case chunkX == -1 && chunkZ == -1:
		return []treeSeed{{x: 11, z: 4}}
	default:
		return nil
	}
}

func placeTree(blocks []uint16, trunkX, trunkZ, baseY int) {
	for y := 0; y < 4; y++ {
		setBlock(blocks, trunkX, baseY+y, trunkZ, logBlockID)
	}
	leafY := baseY + 4
	for dz := -1; dz <= 1; dz++ {
		for dx := -1; dx <= 1; dx++ {
			setBlock(blocks, trunkX+dx, leafY, trunkZ+dz, leafBlockID)
		}
	}
	setBlock(blocks, trunkX, leafY+1, trunkZ, leafBlockID)
}

func setBlock(blocks []uint16, x, y, z int, blockID uint16) {
	if x < 0 || x >= chunkSize || y < 0 || y >= chunkHeight || z < 0 || z >= chunkSize {
		return
	}
	blocks[chunkIndex(x, y, z)] = blockID
}

func chunkIndex(x, y, z int) int {
	return (y*chunkSize+z)*chunkSize + x
}
