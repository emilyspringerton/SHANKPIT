-- src/ticket460.lua -- a connect ticket is a claim, and this module does not believe claims.
--
-- SHANKPIT-460 DNA is: IDUNA mints a connect ticket, the game server verifies it, nobody invents an identity in
-- between. A Roblox server cannot be the verifier, and the reason is not squeamishness -- it is that verifying
-- requires the signing secret, and every secret that reaches a place is a secret some client can read. So this
-- module does the ONE thing it is entitled to do:
--
--   * check the ticket's SHAPE locally, so a garbage string costs no round trip (fast, honest failure), and
--   * hand the real decision to an injected verifier -- in the place, a server-to-server call to IDUNA.
--
-- There is deliberately no `verify_hmac` here and no secret constant. If someone adds one, this file has failed
-- at its only job. The shape check is also not authentication: a well-formed ticket proves nothing on its own,
-- which is why `accept` reports whether the verifier was even consulted.

local M = {}

M.PREFIX = "s460"   -- the game this ticket is for; the C server's own tickets carry the game in the same slot
M.MIN_SIG = 32      -- a hex signature shorter than this is a truncated string, not a signature

-- `s460:<pid>:<hex signature>` -- shape only. Returns {ok=true, pid=...} or {ok=false, reason=...}.
function M.parse(ticket)
    if type(ticket) ~= "string" then return { ok = false, reason = "not-a-ticket" } end
    local prefix, id, sig = string.match(ticket, "^(%w+):(%d+):([0-9a-fA-F]+)$")
    if not prefix then return { ok = false, reason = "malformed" } end
    if prefix ~= M.PREFIX then return { ok = false, reason = "wrong-game" } end
    if #sig < M.MIN_SIG then return { ok = false, reason = "short-signature" } end
    local pid = tonumber(id)
    if not pid or pid < 1 then return { ok = false, reason = "bad-pid" } end
    return { ok = true, pid = pid }
end

-- Returns the verdict and a second value saying whether the verifier was consulted. A caller that wants to
-- count round trips (or a test that wants to prove a malformed ticket costs none) reads that flag.
function M.accept(ticket, verifier)
    local shape = M.parse(ticket)
    if not shape.ok then return shape, false end
    if type(verifier) ~= "function" then
        return { ok = false, reason = "no-verifier" }, false
    end
    local v = verifier(ticket)
    if type(v) ~= "table" or not v.ok then
        local reason = "refused"
        if type(v) == "table" and v.reason ~= nil then reason = tostring(v.reason) end
        return { ok = false, reason = reason }, true
    end
    return {
        ok = true,
        pid = tonumber(v.pid) or shape.pid,
        name = tostring(v.name or ""),
    }, true
end

return M
