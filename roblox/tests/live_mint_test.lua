-- Take a ticket IDUNA just minted, over real HTTP, and read it with the module the place uses.
-- Arguments are the three fields from IDUNA's own JSON answer, so nothing here invents a ticket.
package.path = "roblox/src/?.lua;" .. package.path
local T = require("ticket460")
local ticket, expires, pid = arg[1], tonumber(arg[2]), arg[3]
if not ticket then print("usage: live_mint_test.lua <ticket> <expires_at> <player_id>"); os.exit(2) end
local t = T.from_mint({ ticket = ticket, expires_at = expires, player_id = pid }, os.time())
if not t.ok then print("FAIL from_mint refused IDUNA's live answer: " .. tostring(t.reason)); os.exit(1) end
if t.player_id ~= pid then print("FAIL player_id mismatch"); os.exit(1) end
if t.expires_at ~= math.floor(expires) then print("FAIL expiry mismatch"); os.exit(1) end
print(string.format("live ticket: 72 hex chars, expiry %d (%d s left), player %s", t.expires_at, t.remaining_s, t.player_id))
