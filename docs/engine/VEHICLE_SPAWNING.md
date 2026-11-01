# Spawning a car (server-authoritative)

This guide uses the scripting APIs already built into Guts & Bolts. It does not use Roblox-only services or assume that a client can create a replicated vehicle.

## 1. Build the objects

In Studio's Explorer, create this hierarchy:

```text
ServerStorage
└── CarTemplates
    └── BasicCar (Model)
        └── Chassis (Part)       <- set this as the model's PrimaryPart if desired

Workspace
└── CarSpawnPoints
    └── Spawn1 (Part)            <- anchored, placed where the car should appear

ReplicatedStorage
└── SpawnCar (RemoteEvent)

ServerScriptService
└── CarSpawner (Script)

StarterGui
└── ScreenGui
    └── SpawnButton (TextButton)
        └── SpawnButtonClient (LocalScript)
```

Put the finished car model in `ServerStorage`, not `ReplicatedStorage`. ServerStorage is only available to server Scripts; clients cannot read the vehicle template. The car model should contain a part named `Chassis`. The example assigns it as `PrimaryPart` at runtime.

For a drivable vehicle, build the model with the engine's supported vehicle parts/seat and physics setup. Keep the chassis and other moving vehicle parts unanchored; anchor the spawn marker, not the vehicle.

## 2. Server Script: validate and spawn

Put this in `ServerScriptService > CarSpawner`. The server chooses the template and spawn point, enforces the cooldown, and replaces the player's previous car. The client does not send a model name, object reference, or position.

```lua
local Players = game:GetService("Players")
local ServerStorage = game:GetService("ServerStorage")
local ReplicatedStorage = game:GetService("ReplicatedStorage")

local carTemplates = ServerStorage:WaitForChild("CarTemplates")
local spawnPoints = workspace:WaitForChild("CarSpawnPoints")
local spawnRequest = ReplicatedStorage:WaitForChild("SpawnCar")

local activeCars = {}
local lastSpawnAt = {}
local COOLDOWN_SECONDS = 5

local function spawnCar(player)
    local now = os.clock()
    local last = lastSpawnAt[player.UserId]

    if last and now - last < COOLDOWN_SECONDS then
        spawnRequest:FireClient(player, false, "Please wait before spawning another car.")
        return
    end

    local template = carTemplates:FindFirstChild("BasicCar")
    local spawnPoint = spawnPoints:FindFirstChild("Spawn1")

    if not template or not template:IsA("Model") then
        warn("CarSpawner: ServerStorage.CarTemplates.BasicCar must be a Model")
        spawnRequest:FireClient(player, false, "The car template is missing.")
        return
    end

    if not spawnPoint or not spawnPoint:IsA("BasePart") then
        warn("CarSpawner: Workspace.CarSpawnPoints.Spawn1 must be a Part")
        spawnRequest:FireClient(player, false, "The car spawn point is missing.")
        return
    end

    local car = template:Clone()
    local chassis = car:FindFirstChild("Chassis")

    if not chassis or not chassis:IsA("BasePart") then
        car:Destroy()
        warn("CarSpawner: BasicCar needs a BasePart named Chassis")
        spawnRequest:FireClient(player, false, "The car model is missing its Chassis part.")
        return
    end

    -- Only one active vehicle per player.
    local previousCar = activeCars[player.UserId]
    if previousCar then
        previousCar:Destroy()
        activeCars[player.UserId] = nil
    end

    car.PrimaryPart = chassis
    car.Name = player.Name .. "'s Car"
    car:SetAttribute("OwnerUserId", player.UserId)
    car.Parent = workspace
    car:PivotTo(spawnPoint.CFrame * CFrame.new(0, 2, 0))

    activeCars[player.UserId] = car
    lastSpawnAt[player.UserId] = now

    spawnRequest:FireClient(player, true, "Your car has spawned.")
end

spawnRequest.OnServerEvent:Connect(spawnCar)

Players.PlayerRemoving:Connect(function(player)
    local car = activeCars[player.UserId]
    if car then
        car:Destroy()
    end

    activeCars[player.UserId] = nil
    lastSpawnAt[player.UserId] = nil
end)
```

## 3. LocalScript: request a spawn from the button

Put this LocalScript inside the `SpawnButton` TextButton. It only sends a request and displays the server's response. It never clones or moves a car itself.

```lua
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local spawnRequest = ReplicatedStorage:WaitForChild("SpawnCar")
local button = script.Parent

button.MouseButton1Click:Connect(function()
    button.Text = "Spawning..."
    spawnRequest:FireServer()
end)

spawnRequest.OnClientEvent:Connect(function(success, message)
    button.Text = success and "Spawn Car" or "Try Again"
    print(message)
end)
```

## 4. Why the server owns the operation

A LocalScript can be modified by the player. It must not decide which model to clone, where a car appears, how many cars a player owns, or whether a cooldown has expired. The RemoteEvent only tells the server that the player asked to spawn. The server uses its own fixed template and spawn marker, applies the cooldown, records ownership, and creates the vehicle in Workspace. The server also deletes the player's car when they leave.

For a production game, add a server-side check that the player is allowed to spawn (for example, round state, team, or game-pass rules). If multiple spawn pads are used, select a free pad on the server and check that the space is clear before moving the vehicle there.

## 5. Test it

1. Start with one player and confirm the button spawns the car.
2. Press the button repeatedly; requests inside the five-second cooldown should be rejected.
3. Wait five seconds and spawn again; the previous car should be removed.
4. Test with multiple players. Each player should get their own car, and leaving should remove that player's car.
5. Check Output for missing-template, missing-spawn-point, or missing-Chassis warnings.

The engine supports the APIs used here: `Players:GetPlayers()`, `PlayerAdded` / `PlayerRemoving`, `Instance:Clone()`, `Model:PivotTo()`, `RemoteEvent:FireServer()`, `OnServerEvent`, and `FireClient()`. See the main README's scripting and LocalScripts / RemoteEvents sections for the wider API reference.
```
