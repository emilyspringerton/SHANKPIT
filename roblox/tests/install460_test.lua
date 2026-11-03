-- The copy-paste installer, EXECUTED against a stub of the Roblox API under plain Lua.
-- The claim being tested is "paste this into Studio and the layer is there": so the test runs the exact committed
-- chunk, with Instance/game stubbed, and then checks the tree it built -- every class, every name, every parent,
-- every embedded source byte-for-byte against src/ -- and that running it twice replaces rather than duplicates.
-- It cannot prove Studio runs it; it does prove the chunk parses, executes, and installs the right bytes.

local INSTALLER = "roblox/SHANKPIT_460_INSTALL.luau"

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

local function readFile(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("a")
    f:close()
    return s
end

local chunkSrc = readFile(INSTALLER)
if not chunkSrc then
    print("  FAIL  missing " .. INSTALLER .. " (run `make roblox-install`)")
    print("install460: 1 checks, 1 failures")
    os.exit(1)
end

-- ---- the stub: just enough Roblox for this chunk, and strict about the rest ----------------------------
-- Methods live in their own table so `__index` can point at them; pointing it at the instance itself is an
-- infinite chain (it was, and Lua said so). `Parent` goes through `__newindex` because Roblox implicitly files a
-- child under its new parent, and the installer relies on that to find what it built.
local M = {}
function M:FindFirstChild(name)
    for _, c in ipairs(self.children) do
        if not c.destroyed and c.Name == name then return c end
    end
    return nil
end
function M:GetChildren()
    local out = {}
    for _, c in ipairs(self.children) do if not c.destroyed then out[#out + 1] = c end end
    return out
end
function M:Destroy()
    if self.destroyed then return end
    if self.Parent then
        local sibs = self.Parent.children
        for i, c in ipairs(sibs) do
            if c == self then table.remove(sibs, i); break end
        end
    end
    self.destroyed = true
    for _, c in ipairs(self.children) do c:Destroy() end
    self.children = {}
end

local function newInstance(class)
    local inst = { ClassName = class, Name = class, children = {}, Parent = nil, destroyed = false }
    setmetatable(inst, {
        __index = M,
        __newindex = function(t, k, v)
            rawset(t, k, v)
            if k == "Parent" and v ~= nil then
                local kids = rawget(v, "children")
                if kids then kids[#kids + 1] = t end
            end
        end,
    })
    return inst
end

local services = {}
local function service(name)
    if not services[name] then services[name] = newInstance(name) end
    return services[name]
end

local game = {
    GetService = function(_, name)
        local svc = service(name)
        if name == "StarterPlayer" and not svc:FindFirstChild("StarterPlayerScripts") then
            local sps = newInstance("StarterPlayerScripts")
            sps.Name = "StarterPlayerScripts"
            sps.Parent = svc
        end
        return svc
    end,
}

local made = 0
local Instance = {
    new = function(class)
        made = made + 1
        return newInstance(class)
    end,
}

local printed, warnings = {}, {}
local env = {
    game = game, Instance = Instance,
    print = function(...)
        local parts = {}
        for i = 1, select("#", ...) do parts[i] = tostring(select(i, ...)) end
        printed[#printed + 1] = table.concat(parts, " ")
    end,
    warn = function(...) warnings[#warnings + 1] = tostring((...)) end,
    string = string, table = table, math = math, os = os,
    ipairs = ipairs, pairs = pairs, select = select, type = type, tostring = tostring, tonumber = tonumber,
    error = error, pcall = pcall,
}

local chunk, loadErr = load(chunkSrc, "@SHANKPIT_460_INSTALL", "t", env)
ok(chunk ~= nil, "the installer chunk parses (" .. tostring(loadErr) .. ")")
if not chunk then
    print(string.format("install460: %d checks, %d failures", checks, failures))
    os.exit(1)
end

-- ---- 1. running it once installs the tree where it actually executes -----------------------------------
local ran, runErr = pcall(chunk)
ok(ran, "the installer runs without error (" .. tostring(runErr) .. ")")

local sss = service("ServerScriptService")
local folder = sss:FindFirstChild("SHANKPIT460")
ok(folder ~= nil, "a SHANKPIT460 folder exists in ServerScriptService")
eq(folder and folder.ClassName, "Folder", "and it is a Folder")

local stp = service("StarterPlayer"):FindFirstChild("StarterPlayerScripts")
local client = stp and stp:FindFirstChild("lobby_client")
ok(client ~= nil, "the LocalScript is in StarterPlayerScripts (where it actually runs)")
eq(client and client.ClassName, "LocalScript", "and is a LocalScript")

-- name -> src file. Kept here rather than derived from the installer, so a wrong mapping in the GENERATOR is a
-- failure and not a tautology.
local EXPECT = {
    { name = "net460", cls = "ModuleScript", file = "roblox/src/net460.lua" },
    { name = "queue460", cls = "ModuleScript", file = "roblox/src/queue460.lua" },
    { name = "ticket460", cls = "ModuleScript", file = "roblox/src/ticket460.lua" },
    { name = "login460", cls = "ModuleScript", file = "roblox/src/login460.lua" },
    { name = "queue460_rules", cls = "ModuleScript", file = "roblox/src/queue460_rules.lua" },
    { name = "ticket460_rules", cls = "ModuleScript", file = "roblox/src/ticket460_rules.lua" },
    { name = "arena460", cls = "ModuleScript", file = "roblox/src/arena460.lua" },
    { name = "stats460", cls = "ModuleScript", file = "roblox/src/stats460.lua" },
    { name = "lobby", cls = "Script", file = "roblox/src/lobby.server.lua" },
}

local installed = 0
for _, e in ipairs(EXPECT) do
    local inst = folder and folder:FindFirstChild(e.name)
    ok(inst ~= nil, "folder holds " .. e.name)
    if inst then
        installed = installed + 1
        eq(inst.ClassName, e.cls, e.name .. " is a " .. e.cls)
        local want = readFile(e.file)
        ok(want ~= nil, e.file .. " is readable")
        eq(#tostring(inst.Source or ""), #(want or ""), e.name .. ": source length matches the file")
        ok(inst.Source == want, e.name .. ": source is byte-identical to " .. e.file)
    end
end
eq(installed, 9, "all nine server-side parts are present")
eq(#folder:GetChildren(), 9, "and nothing else was put in the folder")
eq(client.Source, readFile("roblox/src/lobby.client.lua"), "the client source is byte-identical too")

-- ---- 2. the server Script arrives Disabled, and the modules arrive runnable ----------------------------
eq(folder:FindFirstChild("lobby").Disabled, true, "the server Script is Disabled on purpose")
for _, e in ipairs(EXPECT) do
    local inst = folder:FindFirstChild(e.name)
    if e.cls == "ModuleScript" and inst then
        eq(inst.Disabled, nil, e.name .. " is not disabled")
    end
end

-- ---- 3. an 11-instance install, and the same number on a second run (replace, never duplicate) ----------
eq(made, 11, "eleven instances created: one folder, nine parts, one client")
local before = made
local reran, rerunErr = pcall(chunk)
ok(reran, "running it again succeeds (" .. tostring(rerunErr) .. ")")
local folders = 0
for _, c in ipairs(sss:GetChildren()) do
    if c.Name == "SHANKPIT460" then folders = folders + 1 end
end
eq(folders, 1, "there is still exactly one SHANKPIT460 folder")
eq(made - before, 11, "the second run rebuilt the same 11 instances")
local again = sss:FindFirstChild("SHANKPIT460")
eq(#again:GetChildren(), 9, "and the folder still holds nine parts, not eighteen")
eq(again:FindFirstChild("arena460").Source, readFile("roblox/src/arena460.lua"), "sources are still exact after a re-run")

-- ---- 4. it says what it did, and what it deliberately did not do ----------------------------------------
local said = table.concat(printed, "\n")
ok(string.find(said, "SHANKPIT460: 11 instances installed", 1, true) ~= nil, "it reports what it installed")
ok(string.find(said, "Disabled", 1, true) ~= nil, "it says the server Script is left Disabled")
ok(string.find(said, "HttpEnabled", 1, true) ~= nil, "it reports the HTTP setting instead of flipping it")

print(string.format("install460: %d checks, %d failures", checks, failures))
if failures > 0 then os.exit(1) end
