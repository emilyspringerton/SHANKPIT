package common

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"math/big"
	"net/http"
	"strings"
	"sync"
	"time"
)

// JWTClaims holds the fields we care about from an IDUNA JWT.
type JWTClaims struct {
	Sub         string `json:"sub"`
	DisplayName string `json:"display_name"` // IDUNA agent/user name
	Name        string `json:"name"`         // fallback
	Email       string `json:"email"`
	Exp         int64  `json:"exp"`
}

// JWKSCache fetches and caches the IDUNA JWKS endpoint, refreshing every 10 minutes.
type JWKSCache struct {
	mu      sync.Mutex
	url     string
	keys    map[string]*ecdsa.PublicKey // kid → key
	fetchAt time.Time
}

func NewJWKSCache(idunaURL string) *JWKSCache {
	return &JWKSCache{url: idunaURL + "/.well-known/jwks.json"}
}

func (c *JWKSCache) getKey(kid string) (*ecdsa.PublicKey, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if time.Since(c.fetchAt) > 10*time.Minute || c.keys == nil {
		if err := c.fetchLocked(); err != nil && c.keys == nil {
			return nil, err
		}
	}
	k, ok := c.keys[kid]
	if !ok {
		// stale kid — try a forced refresh
		if err := c.fetchLocked(); err != nil {
			return nil, err
		}
		k, ok = c.keys[kid]
		if !ok {
			return nil, fmt.Errorf("kid %q not found in JWKS", kid)
		}
	}
	return k, nil
}

func (c *JWKSCache) fetchLocked() error {
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(c.url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)

	var jwks struct {
		Keys []struct {
			Kid string `json:"kid"`
			Kty string `json:"kty"`
			Crv string `json:"crv"`
			X   string `json:"x"`
			Y   string `json:"y"`
		} `json:"keys"`
	}
	if err := json.Unmarshal(body, &jwks); err != nil {
		return err
	}
	m := make(map[string]*ecdsa.PublicKey, len(jwks.Keys))
	for _, k := range jwks.Keys {
		if k.Kty != "EC" || k.Crv != "P-256" {
			continue
		}
		xb, err := base64.RawURLEncoding.DecodeString(k.X)
		if err != nil {
			continue
		}
		yb, err := base64.RawURLEncoding.DecodeString(k.Y)
		if err != nil {
			continue
		}
		pub := &ecdsa.PublicKey{
			Curve: elliptic.P256(),
			X:     new(big.Int).SetBytes(xb),
			Y:     new(big.Int).SetBytes(yb),
		}
		m[k.Kid] = pub
	}
	c.keys = m
	c.fetchAt = time.Now()
	return nil
}

// ValidateJWT validates an ES256 JWT against the IDUNA JWKS cache and returns claims.
func ValidateJWT(cache *JWKSCache, token string) (*JWTClaims, error) {
	parts := strings.SplitN(token, ".", 3)
	if len(parts) != 3 {
		return nil, fmt.Errorf("malformed JWT")
	}

	// Decode header to get kid.
	headerBytes, err := base64.RawURLEncoding.DecodeString(parts[0])
	if err != nil {
		return nil, fmt.Errorf("decode header: %w", err)
	}
	var header struct {
		Alg string `json:"alg"`
		Kid string `json:"kid"`
	}
	if err := json.Unmarshal(headerBytes, &header); err != nil {
		return nil, fmt.Errorf("parse header: %w", err)
	}
	if header.Alg != "ES256" {
		return nil, fmt.Errorf("unsupported alg %q", header.Alg)
	}

	pub, err := cache.getKey(header.Kid)
	if err != nil {
		return nil, err
	}

	// Verify signature over "header.payload".
	signingInput := parts[0] + "." + parts[1]
	digest := sha256.Sum256([]byte(signingInput))

	sigBytes, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil {
		return nil, fmt.Errorf("decode sig: %w", err)
	}
	if len(sigBytes) != 64 {
		return nil, fmt.Errorf("invalid sig length %d", len(sigBytes))
	}
	r := new(big.Int).SetBytes(sigBytes[:32])
	s := new(big.Int).SetBytes(sigBytes[32:])
	if !ecdsa.Verify(pub, digest[:], r, s) {
		return nil, fmt.Errorf("invalid signature")
	}

	// Decode and parse claims.
	claimBytes, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return nil, fmt.Errorf("decode claims: %w", err)
	}
	var claims JWTClaims
	if err := json.Unmarshal(claimBytes, &claims); err != nil {
		return nil, fmt.Errorf("parse claims: %w", err)
	}
	if claims.Exp > 0 && claims.Exp < time.Now().Unix() {
		return nil, fmt.Errorf("token expired")
	}
	return &claims, nil
}

// DisplayNameFromClaims returns the best display name from JWT claims.
func DisplayNameFromClaims(c *JWTClaims) string {
	if c.DisplayName != "" {
		return c.DisplayName
	}
	if c.Name != "" {
		return c.Name
	}
	if c.Email != "" {
		at := strings.Index(c.Email, "@")
		if at > 0 {
			return c.Email[:at]
		}
		return c.Email
	}
	return c.Sub
}
