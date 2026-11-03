-- src/ticket460.lua -- reading a SHANKPIT connect ticket, and never pretending to verify one.
--
-- THE FORMAT IS CHECKED, NOT ASSUMED. `IDUNA/internal/http/handlers/shankpit_ticket.go`:
--
--   POST /api/v1/shankpit/ticket   (Bearer IDUNA JWT)
--     -> {"ticket": "<72 hex chars>", "expires_at": <unix seconds>, "player_id": "<uuid>"}
--
-- and the ticket is 36 raw BYTES, hex-encoded:
--
--   player_id (16 bytes, raw UUID) || expires_at (4 bytes, LITTLE-endian u32) || hmac_sha256(secret, payload)[:16]
--
-- TTL is 5 minutes (`ShankpitTicketTTL`), because a ticket is presented ONCE at PACKET_CONNECT rather than held
-- for a session. An earlier version of this file invented a `s460:<pid>:<hex>` string -- there is no such
-- prefix anywhere in IDUNA, and the pid is a UUID rather than an integer; that is why this file now parses the
-- bytes above and nothing else.
--
-- WHAT THIS MODULE IS ALLOWED TO SAY, and it is deliberately little: the SHAPE is right, the expiry has or has
-- not passed, and which player it claims to be for. It does NOT verify the MAC and holds no secret -- the C
-- server holds `SHANKPIT_TICKET_SECRET` and verifies with a constant-time compare
-- (shankpit-460's `packages/common/hmac_sha256.h`). A place every client can read is the last place a signing
-- secret belongs, so the honest thing for the Roblox side to do is mint, hand over, and shut up.
--
-- The shape and expiry arithmetic are `ticket460_rules.lua`, emitted from
-- `PARENA/stdlib/shankpit/ticket_rules.prn` (`make regen-roblox-rules`) -- not restated here.

local rules = require("ticket460_rules")

local M = {}

M.RULES = rules

-- Bytes a ticket carries, and therefore the hex length. Kept for a reader; the rule itself lives in the .prn.
M.BYTES = 36

local function dashUuid(idHex)
    return string.format("%s-%s-%s-%s-%s",
        idHex:sub(1, 8), idHex:sub(9, 12), idHex:sub(13, 16), idHex:sub(17, 20), idHex:sub(21, 32))
end

-- `s460` in the old tests is gone: a real ticket is 72 hex characters and nothing else.
function M.parse(ticket)
    if type(ticket) ~= "string" then return { ok = false, reason = "not-a-ticket" } end
    local hex = ticket:lower()
    if hex:find("[^0-9a-f]") then return { ok = false, reason = "not-hex" } end
    if rules.ticketShapeOk(#hex) ~= 1 then return { ok = false, reason = "bad-length" } end

    local idHex = hex:sub(1, 32)
    local b = {}
    for i = 1, 4 do
        b[i] = tonumber(hex:sub(32 + i * 2 - 1, 32 + i * 2), 16)
    end
    -- LITTLE-endian: the first byte is the least significant, which is the order the C server reads.
    local expires = b[1] + b[2] * 256 + b[3] * 65536 + b[4] * 16777216

    return {
        ok = true,
        player_id = dashUuid(idHex),
        expires_at = expires,
        hex = hex,
    }
end

-- Shape + expiry. Omit now_s and the expiry is not judged (there is nothing honest to judge it against), which
-- is why every caller in the place passes one.
function M.usable(ticket, now_s)
    local t = M.parse(ticket)
    if not t.ok then return t end
    local now = tonumber(now_s)
    if now ~= nil then
        now = math.floor(now)
        if rules.ticketExpired(now, t.expires_at) == 1 then
            return { ok = false, reason = "expired", expires_at = t.expires_at, player_id = t.player_id }
        end
        t.remaining_s = rules.ticketRemainingS(now, t.expires_at)
    end
    return t
end

-- IDUNA's own JSON answer, checked against the bytes it signed. Two sources of truth for one ticket (the JSON
-- fields and the ticket's own payload) must AGREE -- if they do not, one of them is not the ticket IDUNA meant
-- to hand out, and the player should not be sent anywhere.
function M.from_mint(decoded, now_s)
    if type(decoded) ~= "table" then return { ok = false, reason = "no-answer" } end
    local t = M.usable(decoded.ticket, now_s)
    if not t.ok then return t end

    local pid = decoded.player_id
    if type(pid) ~= "string" or pid == "" then return { ok = false, reason = "no-player-id" } end
    if pid:lower() ~= t.player_id then return { ok = false, reason = "id-disagrees" } end

    local expires = tonumber(decoded.expires_at)
    if expires ~= nil and math.floor(expires) ~= t.expires_at then
        return { ok = false, reason = "expiry-disagrees" }
    end

    t.player_id = pid
    return t
end

return M
