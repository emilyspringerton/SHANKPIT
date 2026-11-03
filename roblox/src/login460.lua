-- src/login460.lua -- how a Roblox player gets a real IDUNA identity, with no password and no OAuth.
--
-- THE PROBLEM THIS SOLVES IS A REAL ONE, not a convenience. A Roblox place cannot run Google OAuth (no
-- browser tab, no redirect back) and should never ask for an IDUNA password. IDUNA's generic game
-- accounts are the answer: `POST /api/v1/games/{game}/guest-register` creates a REAL players row (a UUID),
-- issues an ES256 player token and hands back a `guest_secret` for signing back in. That is the same
-- account system DEADWEIGHT's own client uses; SHANKPIT now has a registry row (`shankpit`) for it.
--
-- The token this returns is what /api/v1/shankpit/ticket accepts: a game player token's subject is
-- "guest:<uuid>" and its identity is the `player_id` claim, which that handler now reads (it only read
-- `sub` before, so this whole path used to 400 -- found while wiring this file).
--
-- WHAT A GUEST ACCOUNT IS, stated once: no email, no recovery, the secret IS the account. The founder's
-- own convention for DEADWEIGHT ("no email, no recovery"), and the reason `guest_secret` is treated as a
-- credential here: it is stored, never shown, and never sent to a client.
--
-- Every function takes its `http` and its clock, so the whole thing is testable without Roblox:
--   http(method, url, body, headers) -> decoded_table | nil, reason
-- which is exactly the shape lobby.server.lua's HttpService wrapper already has.

local M = {}

-- A token inside this margin is treated as needing a re-login: the ticket mint and the world connect
-- both take time, and a token that expires mid-handshake reads to a player as a lobby bug.
M.REFRESH_MARGIN_S = 300

function M.routes(base)
    return {
        register = base .. "/guest-register",
        login    = base .. "/guest-login",
        set_name = base .. "/set-display-name",
    }
end

local function struggles(v) return type(v) ~= "string" or v == "" end

-- IDUNA's answer, checked rather than trusted: an identity is only real if it carries a player id, a
-- token, and an expiry. `guest_secret` is optional because guest-login's answer does not repeat it.
function M.from_answer(decoded, now_s)
    if type(decoded) ~= "table" then return nil, "no-answer" end
    if struggles(decoded.player_id) then return nil, "no-player-id" end
    if struggles(decoded.token) then return nil, "no-token" end
    local expires = tonumber(decoded.expires_at)
    if not expires then return nil, "no-expiry" end
    local now = tonumber(now_s)
    if now and expires <= now then return nil, "expired" end
    return {
        player_id    = decoded.player_id,
        display_name = tostring(decoded.display_name or ""),
        token        = decoded.token,
        expires_at   = math.floor(expires),
        guest_secret = struggles(decoded.guest_secret) and nil or decoded.guest_secret,
        account_state = tostring(decoded.account_state or "guest"),
    }
end

-- Registering is the first-visit path: no name is required (IDUNA picks a lore-friendly one), and the
-- display name is only passed when the place has one worth keeping.
function M.sign_in(http, base, name, now_s)
    if type(http) ~= "function" then return nil, "no-http" end
    local body = "{}"
    if type(name) == "string" and name ~= "" then
        body = string.format('{"display_name":"%s"}', name:gsub('[\\"]', ""))
    end
    local decoded, reason = http("POST", M.routes(base).register, body,
        { ["Content-Type"] = "application/json" })
    if not decoded then return nil, reason or "no-answer" end
    return M.from_answer(decoded, now_s)
end

-- Signing back in is the returning path, and only the stored secret can do it -- the player id alone is
-- public. A `saved` table is whatever was persisted for this player (player_id + guest_secret).
function M.sign_back_in(http, base, saved, now_s)
    if type(saved) ~= "table" or struggles(saved.player_id) or struggles(saved.guest_secret) then
        return nil, "not-saved"
    end
    local decoded, reason = http("POST", M.routes(base).login,
        string.format('{"player_id":"%s","guest_secret":"%s"}', saved.player_id, saved.guest_secret),
        { ["Content-Type"] = "application/json" })
    if not decoded then return nil, reason or "no-answer" end
    local identity, why = M.from_answer(decoded, now_s)
    if not identity then return nil, why end
    -- guest-login does not repeat the secret; keep the one that worked so the next session can too.
    identity.guest_secret = identity.guest_secret or saved.guest_secret
    return identity
end

-- True while the token is worth using. A missing expiry is NOT fresh (an undated token is not evidence).
function M.token_ok(identity, now_s)
    if type(identity) ~= "table" or struggles(identity.token) then return false end
    local expires, now = tonumber(identity.expires_at), tonumber(now_s)
    if not expires or not now then return false end
    return expires - now > M.REFRESH_MARGIN_S
end

-- Sign in for a Roblox UserId, reusing what was saved for them if it still works. Returns
-- identity, reason, and never mutates `store` -- the caller decides what to persist, because persisting
-- is the part that needs a running place (DataStore).
function M.identity_for(http, base, store, user_id, now_s)
    local saved = type(store) == "table" and store[user_id] or nil
    if saved and M.token_ok(saved, now_s) then return saved, nil, true end
    if saved and not struggles(saved.guest_secret) then
        local identity, why = M.sign_back_in(http, base, saved, now_s)
        if identity then return identity, nil, true end
        -- A saved secret that no longer works is a real answer ("401"), not a reason to silently mint a
        -- second account for the same person: fall through and register only if the caller allows it.
        return nil, why, false
    end
    return nil, "not-saved", false
end

-- What may be persisted for a player. The token is deliberately NOT in it: a token lives 24 h and is
-- cheaper to re-earn from the secret than to store in a place many people can read.
function M.to_save(identity)
    if type(identity) ~= "table" or struggles(identity.player_id) then return nil end
    return { player_id = identity.player_id, guest_secret = identity.guest_secret,
             display_name = identity.display_name }
end

return M
