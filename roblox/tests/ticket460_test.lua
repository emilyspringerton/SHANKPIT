-- A connect ticket, read the way IDUNA actually writes one.
--
-- The ticket is 36 bytes hex-encoded: uuid (16) || expiry (4, LITTLE-endian) || mac (16). These checks build
-- real tickets byte by byte, so the little-endian read is pinned by hand rather than trusted.
package.path = "roblox/src/?.lua;" .. package.path
local T = require("ticket460")
local R = require("ticket460_rules")

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

-- little-endian u32 as 4 bytes of hex -- the exact byte order IDUNA's binary.LittleEndian.PutUint32 produces
local function le32(n)
    local b1 = n % 256; n = (n - b1) / 256
    local b2 = n % 256; n = (n - b2) / 256
    local b3 = n % 256; n = (n - b3) / 256
    local b4 = n % 256
    return string.format("%02x%02x%02x%02x", b1, b2, b3, b4)
end

local UUID_HEX = "6f9619ff8b86d011b42d00c04fc964ff"
local UUID_DASHED = "6f9619ff-8b86-d011-b42d-00c04fc964ff"
local MAC = string.rep("ab", 16)          -- 32 hex chars = the 16-byte truncated MAC
local function mint(expires) return UUID_HEX .. le32(expires) .. MAC end

-- the helper itself: 2000000000 is 0x77359400, so the BYTES are 00 94 35 77
eq(le32(2000000000), "00943577", "le32 encodes little-endian (0x77359400 -> 00943577)")

local t = T.parse(mint(2000000000))
ok(t.ok, "a real 72-hex ticket parses")
eq(t.expires_at, 2000000000, "the expiry is read out of bytes 16..19 little-endian")
eq(t.player_id, UUID_DASHED, "the uuid is read out of the first 16 bytes, dashed")
eq(#t.hex, 72, "the parsed hex is 72 characters")
ok(T.parse(string.upper(mint(2000000000))).ok, "uppercase hex is still a ticket")
eq(T.parse(nil).reason, "not-a-ticket", "nil is not a ticket")
eq(T.parse("").reason, "bad-length", "empty is the wrong length")
eq(T.parse("zz").reason, "not-hex", "non-hex is refused before any byte is interpreted")
eq(T.parse(string.sub(mint(2000000000), 1, 70)).reason, "bad-length", "a truncated ticket is refused")

-- expiry: dead AT its second, not one later
local u = T.usable(mint(2000000000), 1000)
ok(u.ok, "a ticket with time left is usable")
eq(u.remaining_s, 1999999000, "remaining seconds are expires - now")
local near = T.usable(mint(2000000000), 1999999999)
eq(near.remaining_s, 1, "one second left reads as 1")
eq(T.usable(mint(2000000000), 2000000000).reason, "expired", "expired AT the expiry second")
eq(T.usable(mint(2000000000), 2000000001).reason, "expired", "and after it")
ok(T.usable(mint(2000000000)).ok, "no clock given means no expiry judgement, not a refusal")

-- IDUNA's JSON answer must agree with the bytes it signed
local good = { ticket = mint(2000000000), expires_at = 2000000000, player_id = UUID_DASHED }
local f = T.from_mint(good, 1000)
ok(f.ok, "a mint response whose fields agree is accepted")
eq(f.player_id, UUID_DASHED, "the id survives")
eq(T.from_mint(nil).reason, "no-answer", "no response is not a ticket")
eq(T.from_mint({ ticket = mint(2000000000), expires_at = 2000000000 }).reason, "no-player-id",
   "a response with no player_id is refused")
eq(T.from_mint({ ticket = mint(2000000000), player_id = "other" }, 1000).reason, "id-disagrees",
   "a player_id that disagrees with the signed uuid is refused")
eq(T.from_mint({ ticket = mint(2000000000), expires_at = 1999999999, player_id = UUID_DASHED }, 1000).reason,
   "expiry-disagrees", "an expires_at that disagrees with the payload is refused")
eq(T.from_mint({ ticket = mint(500), expires_at = 500, player_id = UUID_DASHED }, 1000).reason, "expired",
   "an expired ticket is refused through from_mint too")

-- the thresholds are PARENA's
ok(T.RULES == R, "the ticket module runs the generated PARENA rules")
eq(R.ticketShapeOk(72), 1, "generated: 72 hex chars is the shape")
eq(R.ticketShapeOk(71), 0, "generated: 71 is not")
eq(R.ticketExpired(10, 10), 1, "generated: expired at the boundary")
eq(R.ticketExpired(9, 10), 0, "generated: not before it")
eq(R.ticketRemainingS(10, 5), 0, "generated: remaining floors at zero")
eq(R.ticketRemainingS(10, 15), 5, "generated: remaining is expires - now")

-- COMMENTS ARE NOT CODE: an earlier version of this check failed on the module's own explanation ("does NOT
-- verify the MAC"). Strip comments, then look for what would have to exist if this file verified anything.
local src = io.open("roblox/src/ticket460.lua", "r")
local body = src and src:read("*a") or ""
if src then src:close() end
local code = {}
for line in (body .. "\n"):gmatch("([^\n]*)\n") do
    if not line:match("^%s*%-%-") then code[#code + 1] = line end
end
code = table.concat(code, "\n")
ok(not code:find("SECRET", 1, true) and not code:find("hmac", 1, true),
   "no secret and no MAC verification in the ticket module's CODE")
ok(not code:find("sha256", 1, true) and not code:find("md5", 1, true),
   "...and no digest routine either -- verifying is the C server's job")

print(string.format("ticket460: %d checks, %d failures", checks, failures))
os.exit(failures == 0 and 0 or 1)
