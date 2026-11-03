-- login460: a real IDUNA identity for a Roblox player, against a fake HTTP transport.
--
-- The transport is injected, so these checks exercise the module's own reading of IDUNA's answers --
-- including the wrong ones -- without a game or a network.
package.path = "roblox/src/?.lua;" .. package.path
local L = require("login460")

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

local BASE = "https://iam.example/api/v1/games/shankpit"
local PID = "6f9619ff-8b86-d011-b42d-00c04fc964ff"
local TOKEN = "eyJhbGciOiJFUzI1NiJ9.payload.sig"

-- A transport that records what it was asked and answers from a queue.
local function transport(answers)
    local calls = {}
    local i = 0
    local fn = function(method, url, body, headers)
        calls[#calls + 1] = { method = method, url = url, body = body, headers = headers }
        i = i + 1
        local a = answers[i] or answers[#answers]
        if type(a) == "table" and a.__fail then return nil, a.__fail end
        return a
    end
    return fn, calls
end

-- 1) register: the first-visit path
local http, calls = transport({ { player_id = PID, token = TOKEN, expires_at = 20000,
    display_name = "Runner-A7B2", guest_secret = "s3cret", account_state = "guest" } })
local id, why = L.sign_in(http, BASE, "Runner-A7B2", 1000)
ok(id ~= nil, "register answers an identity: " .. tostring(why))
eq(#calls, 1, "one request")
eq(calls[1].method, "POST", "register is a POST")
eq(calls[1].url, BASE .. "/guest-register", "register URL is the real endpoint")
ok(calls[1].body:find("Runner%-A7B2") ~= nil, "the display name is sent")
eq(id.player_id, PID, "player_id survives")
eq(id.token, TOKEN, "token survives")
eq(id.guest_secret, "s3cret", "the secret is kept for the next session")
eq(id.account_state, "guest", "account state is read rather than assumed")

-- 2) an answer missing its parts is not an identity
eq(select(2, L.sign_in(transport({ { token = TOKEN, expires_at = 99999 } }), BASE, nil, 1)), "no-player-id",
   "no player_id -> refused")
eq(select(2, L.sign_in(transport({ { player_id = PID, expires_at = 99999 } }), BASE, nil, 1)), "no-token",
   "no token -> refused")
eq(select(2, L.sign_in(transport({ { player_id = PID, token = TOKEN } }), BASE, nil, 1)), "no-expiry",
   "no expiry -> refused (an undated token is not fresh)")
eq(select(2, L.sign_in(transport({ { player_id = PID, token = TOKEN, expires_at = 500 } }), BASE, nil, 1000)), "expired",
   "an answer that is already expired is refused")
eq(select(2, L.sign_in(transport({ { __fail = "http-401" } }), BASE, nil, 1)), "http-401",
   "a transport failure is passed through, not disguised")
eq(select(2, L.sign_in(transport({ { player_id = PID, token = TOKEN, expires_at = 99999 } }), BASE, nil, 1)), nil,
   "a complete answer has no reason attached")

-- 3) an empty name registers without inventing one (IDUNA assigns a lore name)
local http2, calls2 = transport({ { player_id = PID, token = TOKEN, expires_at = 99999 } })
L.sign_in(http2, BASE, "", 1)
eq(calls2[1].body, "{}", "an empty name sends an empty body, not a made-up name")
ok(L.to_save({ player_id = PID, token = TOKEN, guest_secret = "s" }).token == nil,
   "the persisted form carries no token")

-- 4) token freshness, including the refresh margin itself
ok(L.token_ok({ token = TOKEN, expires_at = 2000 }, 1000), "900 s of life is usable")
eq(L.token_ok({ token = TOKEN, expires_at = 1300 }, 1000), false, "inside the 300 s margin is not")
eq(L.token_ok({ token = TOKEN, expires_at = 1301 }, 1000), true, "one second outside is")
eq(L.token_ok({ expires_at = 99999 }, 1), false, "no token is not usable")
eq(L.token_ok({ token = TOKEN }, 1), false, "no expiry is not usable")

-- 5) signing back in: only the stored secret can do it
local http3, calls3 = transport({ { player_id = PID, token = "new-token", expires_at = 99999, display_name = "Runner-A7B2" } })
local back = L.sign_back_in(http3, BASE, { player_id = PID, guest_secret = "s3cret" }, 1000)
ok(back ~= nil, "a saved secret signs back in")
eq(calls3[1].url, BASE .. "/guest-login", "guest-login URL")
ok(calls3[1].body:find("s3cret") ~= nil, "the secret is sent")
eq(back.token, "new-token", "the new token is used")
eq(back.guest_secret, "s3cret", "the working secret is carried forward even though the answer omits it")
eq(select(2, L.sign_back_in(http3, BASE, { player_id = PID }, 1)), "not-saved",
   "no saved secret -> refused rather than a silent second account")
eq(select(2, L.sign_back_in(transport({ { __fail = "http-401" } }), BASE, { player_id = PID, guest_secret = "bad" }, 1)),
   "http-401", "a rejected secret is reported as such")

-- 6) identity_for: reuse, re-earn, and never guess
local saved = { [42] = { player_id = PID, guest_secret = "s3cret", token = TOKEN, expires_at = 20000 } }
local http4, calls4 = transport({})
local reused = L.identity_for(http4, BASE, saved, 42, 1000)
ok(reused ~= nil, "a live saved identity is reused")
eq(#calls4, 0, "reuse costs no request")
local fresh = { [42] = { player_id = PID, guest_secret = "s3cret", token = TOKEN, expires_at = 1100 } }
local http5, calls5 = transport({ { player_id = PID, token = "fresh", expires_at = 99999 } })
local renewed, _, was_saved = L.identity_for(http5, BASE, fresh, 42, 1000)
ok(renewed ~= nil, "a stale token is renewed")
eq(calls5[1].url, BASE .. "/guest-login", "renewal is guest-login, not guest-register")
eq(was_saved, true, "and it is reported as a saved identity")
local http6, calls6 = transport({})
eq(select(2, L.identity_for(http6, BASE, {}, 7, 1)), "not-saved", "an unknown player has no identity yet")
eq(#calls6, 0, "and nothing is requested behind their back")

print(string.format("login460: %d checks, %d failures", checks, failures))
os.exit(failures == 0 and 0 or 1)
