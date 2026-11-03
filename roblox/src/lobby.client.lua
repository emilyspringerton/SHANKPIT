-- src/lobby.client.lua -- what a body actually sees: the queue, the board, and a match to join.
--
-- LocalScript. In Studio it belongs in STARTERPLAYERSCRIPTS (not beside the server Script), and the README says
-- so, because putting it in the same folder is the mistake that makes a lobby look finished with nothing on
-- screen.
--
-- It draws and it asks. It never decides: no seat, no result and no board row is computed here. Every number on
-- this screen arrived from the server, which got it from the world (`SHANKPIT460_Board`) or from the queue
-- (`SHANKPIT460_Queue`).

local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")

local player = Players.LocalPlayer
local queueEvent = ReplicatedStorage:WaitForChild("SHANKPIT460_Queue")
local boardEvent = ReplicatedStorage:WaitForChild("SHANKPIT460_Board")
local matchEvent = ReplicatedStorage:WaitForChild("SHANKPIT460_MatchFound")

local gui = Instance.new("ScreenGui")
gui.Name = "SHANKPIT460"
gui.ResetOnSpawn = false
gui.Parent = player:WaitForChild("PlayerGui")

local panel = Instance.new("Frame")
panel.Size = UDim2.new(0, 320, 0, 372)
panel.Position = UDim2.new(0, 16, 0, 16)
panel.BackgroundColor3 = Color3.fromRGB(18, 18, 20)
panel.BorderSizePixel = 0
panel.Parent = gui

local function label(text, y, height, size, color)
    local l = Instance.new("TextLabel")
    l.Size = UDim2.new(1, -16, 0, height)
    l.Position = UDim2.new(0, 8, 0, y)
    l.BackgroundTransparency = 1
    l.Font = Enum.Font.Code
    l.TextSize = size or 14
    l.TextXAlignment = Enum.TextXAlignment.Left
    l.TextColor3 = color or Color3.fromRGB(232, 232, 232)
    l.Text = text
    l.Parent = panel
    return l
end

local function button(text, y, x, width)
    local b = Instance.new("TextButton")
    b.Size = UDim2.new(0, width, 0, 26)
    b.Position = UDim2.new(0, x, 0, y)
    b.BackgroundColor3 = Color3.fromRGB(38, 38, 44)
    b.BorderSizePixel = 0
    b.Font = Enum.Font.Code
    b.TextSize = 13
    b.TextColor3 = Color3.fromRGB(240, 240, 240)
    b.Text = text
    b.Parent = panel
    return b
end

local title = label("SHANKPIT // 460", 8, 20, 16)
title.TextColor3 = Color3.fromRGB(120, 220, 190)

local state = label("state: connecting", 32, 18, 12, Color3.fromRGB(160, 160, 160))
local boardHead = label("LADDER", 58, 16, 12, Color3.fromRGB(160, 160, 160))
local board = Instance.new("TextLabel")
board.Size = UDim2.new(1, -16, 0, 132)
board.Position = UDim2.new(0, 8, 0, 76)
board.BackgroundTransparency = 1
board.Font = Enum.Font.Code
board.TextSize = 12
board.TextXAlignment = Enum.TextXAlignment.Left
board.TextYAlignment = Enum.TextYAlignment.Top
board.TextColor3 = Color3.fromRGB(220, 220, 220)
board.Text = ""
board.Parent = panel

local selfLine = label("you: not queued", 214, 18, 12, Color3.fromRGB(200, 200, 140))
local casual = button("CASUAL", 236, 8, 96)
local ranked = button("RANKED", 236, 112, 96)
local signin = button("SIGN IN", 236, 216, 96)
local leave = button("LEAVE QUEUE", 266, 8, 300)

local matchFrame = Instance.new("Frame")
matchFrame.Size = UDim2.new(1, -16, 0, 64)
matchFrame.Position = UDim2.new(0, 8, 0, 298)
matchFrame.BackgroundColor3 = Color3.fromRGB(28, 46, 40)
matchFrame.BorderSizePixel = 0
matchFrame.Visible = false
matchFrame.Parent = panel

local matchText = Instance.new("TextLabel")
matchText.Size = UDim2.new(1, -12, 1, -8)
matchText.Position = UDim2.new(0, 6, 0, 4)
matchText.BackgroundTransparency = 1
matchText.Font = Enum.Font.Code
matchText.TextSize = 12
matchText.TextXAlignment = Enum.TextXAlignment.Left
matchText.TextYAlignment = Enum.TextYAlignment.Top
matchText.TextColor3 = Color3.fromRGB(210, 245, 235)
matchText.Text = ""
matchText.Parent = matchFrame

-- The ticket is what the C server will verify, so it is shown rather than hidden: a player who cannot see it
-- cannot tell a lobby bug from a server one.
local match = nil
local matchExpiresAt = 0

-- A token may still be handed in by hand (_G) for testing; leaving it unset is the normal path, where the
-- SERVER signs the player in with IDUNA's guest account and never shows a secret to this client.
local function requestJoin(mode)
    queueEvent:FireServer("join", { mode = mode, token = _G.SHANKPIT460_IDUNA_TOKEN })
end

casual.MouseButton1Click:Connect(function() requestJoin("casual") end)
ranked.MouseButton1Click:Connect(function() requestJoin("ranked") end)
signin.MouseButton1Click:Connect(function()
    state.Text = "state: signing in with IDUNA"
    queueEvent:FireServer("signin")
end)
leave.MouseButton1Click:Connect(function()
    match = nil
    matchFrame.Visible = false
    queueEvent:FireServer("leave")
end)

local function renderBoard(payload)
    if type(payload) ~= "table" then return end
    local lines = {}
    local pop = payload.population or {}
    lines[#lines + 1] = string.format("%d bodies, meta %s%s", pop.count or 0, tostring(pop.meta_kind or "none"),
        payload.stale and "  (STALE)" or "")
    local rows = payload.rows or {}
    if #rows == 0 then
        lines[#lines + 1] = "no bodies yet"
    end
    for i = 1, math.min(#rows, 8) do
        local r = rows[i]
        local rating = r.rating and tostring(r.rating) or "?"
        lines[#lines + 1] = string.format("%d. %-12s %5s  %d/%d", r.standing, tostring(r.name), rating,
            r.wins, r.games)
    end
    if payload.supports and payload.supports.stage then
        lines[#lines + 1] = "stage: published"
    end
    board.Text = table.concat(lines, "\n")
end

boardEvent.OnClientEvent:Connect(renderBoard)

queueEvent.OnClientEvent:Connect(function(payload)
    if type(payload) ~= "table" then return end
    if payload.signed_in then
        state.Text = string.format("state: signed in as %s (%s)", tostring(payload.display_name),
            tostring(payload.account_state))
        selfLine.Text = "iduna: " .. tostring(payload.player_id):sub(1, 8)
    elseif payload.fillable then
        state.Text = string.format("state: a citizen may fill %s (still queued)", tostring(payload.fillable.mode))
    elseif payload.position then
        state.Text = string.format("state: queued %s at #%d (rating %s)", tostring(payload.mode),
            payload.position, tostring(payload.rating))
    end
end)

matchEvent.OnClientEvent:Connect(function(payload)
    if type(payload) ~= "table" then return end
    match = payload
    matchExpiresAt = tonumber(payload.expires_at) or 0
    matchFrame.Visible = true
end)

player:GetAttributeChangedSignal("SHANKPIT460_State"):Connect(function()
    state.Text = "state: " .. tostring(player:GetAttribute("SHANKPIT460_State"))
end)

task.spawn(function()
    while true do
        if match then
            local left = matchExpiresAt - os.time()
            if left < 0 then left = 0 end
            local ticket = tostring(match.ticket or "")
            matchText.Text = string.format(
                "MATCH %s vs %s (gap %d)\nticket %s... %ds left\nopen SHANKPIT and paste the ticket",
                tostring(match.mode), tostring(match.opponent), tonumber(match.gap) or 0,
                string.sub(ticket, 1, 12), left)
            if left == 0 then
                matchFrame.BackgroundColor3 = Color3.fromRGB(58, 34, 34)
            end
        end
        task.wait(1)
    end
end)
