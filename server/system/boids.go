package system

import "math"

type BoidState struct {
	Pos Vec3
	Vel Vec3
}

type BoidConfig struct {
	NeighborRadius   float64
	SeparationRadius float64
	AlignmentWeight  float64
	CohesionWeight   float64
	SeparationWeight float64
	MaxSpeed         float64
}

func StepBoids(boids []BoidState, cfg BoidConfig) []BoidState {
	if len(boids) == 0 {
		return nil
	}
	updated := make([]BoidState, len(boids))
	neighborCells := make(map[boidCell][]int, len(boids))

	if cfg.NeighborRadius > 0 {
		invCellSize := 1.0 / cfg.NeighborRadius
		for i, b := range boids {
			cell := boidCellForPos(b.Pos, invCellSize)
			neighborCells[cell] = append(neighborCells[cell], i)
		}
	}

	for i := range boids {
		b := boids[i]
		var align Vec3
		var cohesion Vec3
		var separation Vec3
		count := 0
		eachNeighbor(boids, neighborCells, i, b, cfg.NeighborRadius, func(o BoidState, d float64) {
			align = align.Add(o.Vel)
			cohesion = cohesion.Add(o.Pos)
			if d < cfg.SeparationRadius {
				separation = separation.Add(vec3Sub(b.Pos, o.Pos))
			}
			count++
		})

		if count > 0 {
			inv := 1.0 / float64(count)
			align = vec3Normalize(align.Mul(inv))
			cohesion = vec3Normalize(vec3Sub(cohesion.Mul(inv), b.Pos))
			separation = vec3Normalize(separation)
		}

		force := align.Mul(cfg.AlignmentWeight).
			Add(cohesion.Mul(cfg.CohesionWeight)).
			Add(separation.Mul(cfg.SeparationWeight))

		newVel := b.Vel.Add(force)
		if cfg.MaxSpeed > 0 {
			if vec3Len(newVel) == 0 && vec3Len(force) > 0 {
				newVel = vec3Normalize(force).Mul(cfg.MaxSpeed)
			} else {
				newVel = vec3Normalize(newVel).Mul(cfg.MaxSpeed)
			}
		}
		updated[i] = BoidState{Pos: b.Pos.Add(newVel), Vel: newVel}
	}

	return updated
}

type boidCell struct {
	X int
	Y int
	Z int
}

func boidCellForPos(pos Vec3, invCellSize float64) boidCell {
	return boidCell{
		X: int(math.Floor(pos.X * invCellSize)),
		Y: int(math.Floor(pos.Y * invCellSize)),
		Z: int(math.Floor(pos.Z * invCellSize)),
	}
}

func eachNeighbor(boids []BoidState, cells map[boidCell][]int, self int, selfBoid BoidState, neighborRadius float64, fn func(BoidState, float64)) {
	if neighborRadius <= 0 {
		for j := range boids {
			if self == j {
				continue
			}
			o := boids[j]
			d := vec3Len(vec3Sub(o.Pos, selfBoid.Pos))
			if d <= 0 {
				continue
			}
			fn(o, d)
		}
		return
	}

	invCellSize := 1.0 / neighborRadius
	origin := boidCellForPos(selfBoid.Pos, invCellSize)
	for dz := -1; dz <= 1; dz++ {
		for dy := -1; dy <= 1; dy++ {
			for dx := -1; dx <= 1; dx++ {
				cell := boidCell{X: origin.X + dx, Y: origin.Y + dy, Z: origin.Z + dz}
				for _, j := range cells[cell] {
					if self == j {
						continue
					}
					o := boids[j]
					delta := vec3Sub(o.Pos, selfBoid.Pos)
					d := vec3Len(delta)
					if d <= 0 || d > neighborRadius {
						continue
					}
					fn(o, d)
				}
			}
		}
	}
}

func vec3Sub(a, b Vec3) Vec3 {
	return Vec3{X: a.X - b.X, Y: a.Y - b.Y, Z: a.Z - b.Z}
}

func vec3Len(v Vec3) float64 {
	return math.Sqrt(v.X*v.X + v.Y*v.Y + v.Z*v.Z)
}

func vec3Normalize(v Vec3) Vec3 {
	l := vec3Len(v)
	if l == 0 {
		return Vec3{}
	}
	return v.Mul(1 / l)
}
