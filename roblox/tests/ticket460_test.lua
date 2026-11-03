-- A ticket: shape here, verdict elsewhere. These checks exist to prove the place costs no round trip on
-- garbage and never pretends to have verified anything itself.
package.path = "roblox/src/?.lua;" .. package.path
local T = require("ticket460")

local checks, failures = 0, 0
local function ok(cond, what)
    checks = checks + 1
    if not cond then failures = failures + 1; print("  FAIL  " .. what) end
end
local function eq(got, want, what)
    checks = checks + 1
    if got ~= want then
        failures = failures + 1
        print(string.format("  FAIL  %s (got %s want %s)", what, tostring(got), tostring(want)))
    end
end

local sig = string.rep("a1", 32)   -- 64 hex characters, the honest shape
local good = "s460:1234:" .. sig

local p = T.parse(good)
ok(p.ok, "a well-formed ticket parses")
eq(p.pid, 1234, "the pid comes out of the middle field")
eq(T.parse("s460:12:abc").reason, "short-signature", "a short signature is refused")
eq(T.parse("nope:12:" .. sig).reason, "wrong-game", "another game's ticket is refused")
eq(T.parse("s460:x:" .. sig).reason, "malformed", "a non-numeric pid is malformed")
eq(T.parse("s460:0:" .. sig).reason, "bad-pid", "pid 0 is not a body")
eq(T.parse(nil).reason, "not-a-ticket", "nil is not a ticket")
eq(T.parse("").reason, "malformed", "empty is malformed")

-- a malformed ticket must cost ZERO round trips
local calls = 0
local verdict, consulted = T.accept("garbage", function() calls = calls + 1; return { ok = true } end)
ok(not verdict.ok, "garbage is refused")
eq(consulted, false, "the verifier was not consulted for garbage")
eq(calls, 0, "and was not even called")

local counted = 0
local accepted, wasCalled = T.accept(good, function(t)
    counted = counted + 1
    eq(t, good, "the verifier receives the ticket unchanged")
    return { ok = true, pid = 77, name = "witness" }
end)
ok(accepted.ok, "a verifier saying yes means accepted")
eq(accepted.pid, 77, "the verifier's pid wins over the shape's pid")
eq(accepted.name, "witness", "the name comes through")
eq(wasCalled, true, "and the flag says it was consulted")
eq(counted, 1, "exactly one call")

local refused = T.accept(good, function() return { ok = false, reason = "expired" } end)
ok(not refused.ok, "a verifier saying no means refused")
eq(refused.reason, "expired", "the verifier's reason survives")
eq(T.accept(good, function() return { ok = false } end).reason, "refused", "no reason reads as refused")
local nover = T.accept(good, nil)
ok(not nover.ok, "no verifier means no acceptance")
eq(nover.reason, "no-verifier", "...and it says so rather than guessing")

-- there is no local HMAC verification anywhere in this module, which is the point
-- COMMENTS ARE NOT CODE, and the first version of this check proved it by failing on the module's own
-- explanation ("there is no local HMAC here"). Strip the comment lines, then look at what is left: a module that
-- VERIFIES would have to mention hmac/md5/sha and hold something secret, and neither may appear in code.
local src = io.open("roblox/src/ticket460.lua", "r")
local body = src and src:read("*a") or ""
if src then src:close() end
local code = {}
for line in (body .. "\n"):gmatch("([^\n]*)\n") do
    if not line:match("^%s*%-%-") then code[#code + 1] = line end
end
code = table.concat(code, "\n")
ok(not code:find("SECRET", 1, true) and not code:find("hmac", 1, true),
   "no secret and no local hmac in the ticket module\'s CODE")
ok(not code:find("sha256", 1, true) and not code:find("md5", 1, true),
   "...and no digest routine either -- verifying is IDUNA\'s job")

print(string.format("ticket460: %d checks, %d failures", checks, failures))
os.exit(failures == 0 and 0 or 1)
