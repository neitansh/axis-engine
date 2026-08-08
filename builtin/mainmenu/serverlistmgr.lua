-- Luanti
-- Copyright (C) 2020 rubenwardy
-- SPDX-License-Identifier: LGPL-2.1-or-later

serverlistmgr = {
	-- continent code we detected for ourselves
	my_continent = nil,

	-- list of locally favorites servers
	favorites = nil,

	-- list of servers fetched from public list
	servers = nil,
}

do
	if check_cache_age("geoip_last_checked", 3600) then
		local tmp = cache_settings:get("geoip") or ""
		if tmp:match("^[A-Z][A-Z]$") then
			serverlistmgr.my_continent = tmp
		end
	end
end

-- The public Luanti list is not used: the Axis only talks to its own servers.
local OFFICIAL_SERVERS = {
	{
		name = "the Axis · " .. fgettext_ne("Russia"),
		address = "135.106.173.139",
		port = 30000,
		description = fgettext_ne("Official server of the Axis, Russia"),
	},
	{
		name = "the Axis · " .. fgettext_ne("Europe"),
		address = "65.109.68.114",
		port = 30000,
		description = fgettext_ne("Official server of the Axis, Europe"),
	},
}

local ping_in_flight = false

-- Round trip time to each official server, measured off the main thread so the
-- menu keeps drawing while the packets are out.
local function measure_pings()
	if ping_in_flight or not core.handle_async or not core.ping_server then
		return
	end
	ping_in_flight = true

	local targets = {}
	for i, server in ipairs(serverlistmgr.servers or {}) do
		targets[i] = { address = server.address, port = server.port }
	end

	core.handle_async(function(list)
		local result = {}
		if not core.ping_server then
			return result
		end

		for i, target in ipairs(list) do
			result[i] = core.ping_server(target.address, target.port, 2000)
		end
		return result
	end, targets, function(result)
		ping_in_flight = false
		if not result then
			return
		end

		for i, info in pairs(result) do
			local server = serverlistmgr.servers[i]
			if server and type(info) == "table" then
				-- The list wants seconds, the engine reports milliseconds
				server.ping = info.ping and info.ping / 1000 or nil
				server.clients = info.clients
				server.clients_max = info.clients_max
			end
		end
		core.event_handler("Refresh")
	end)
end

function serverlistmgr.sync()
	local servers = {}

	for _, server in ipairs(OFFICIAL_SERVERS) do
		servers[#servers + 1] = table.copy(server)
	end

	serverlistmgr.servers = servers
	measure_pings()
end

--------------------------------------------------------------------------------
local function get_favorites_path(folder)
	local base = core.get_user_path() .. DIR_DELIM .. "client" .. DIR_DELIM .. "serverlist" .. DIR_DELIM
	if folder then
		return base
	end
	return base .. core.settings:get("serverlist_file")
end

--------------------------------------------------------------------------------
local function save_favorites(favorites)
	local filename = core.settings:get("serverlist_file")
	-- If setting specifies legacy format change the filename to the new one
	if filename:sub(#filename - 3):lower() == ".txt" then
		core.settings:set("serverlist_file", filename:sub(1, #filename - 4) .. ".json")
	end

	assert(core.create_dir(get_favorites_path(true)))
	core.safe_file_write(get_favorites_path(), core.write_json(favorites))
end

--------------------------------------------------------------------------------
function serverlistmgr.read_legacy_favorites(path)
	local file = io.open(path, "r")
	if not file then
		return nil
	end

	local lines = {}
	for line in file:lines() do
		lines[#lines + 1] = line
	end
	file:close()

	local favorites = {}

	local i = 1
	while i < #lines do
		local function pop()
			local line = lines[i]
			i = i + 1
			return line and line:trim()
		end

		if pop():lower() == "[server]" then
			local name = pop()
			local address = pop()
			local port = tonumber(pop())
			local description = pop()

			if name == "" then
				name = nil
			end

			if description == "" then
				description = nil
			end

			if not address or #address < 3 then
				core.log("warning", "Malformed favorites file, missing address at line " .. i)
			elseif not port or port < 1 or port > 65535 then
				core.log("warning", "Malformed favorites file, missing port at line " .. i)
			elseif (name and name:upper() == "[SERVER]") or
					(address and address:upper() == "[SERVER]") or
					(description and description:upper() == "[SERVER]") then
				core.log("warning", "Potentially malformed favorites file, overran at line " .. i)
			else
				favorites[#favorites + 1] = {
					name = name,
					address = address,
					port = port,
					description = description
				}
			end
		end
	end

	return favorites
end

--------------------------------------------------------------------------------
local function read_favorites()
	local path = get_favorites_path()

	-- If new format configured fall back to reading the legacy file
	if path:sub(#path - 4):lower() == ".json" then
		local file = io.open(path, "r")
		if file then
			local json = file:read("*all")
			file:close()
			return core.parse_json(json)
		end

		path = path:sub(1, #path - 5) .. ".txt"
	end

	local favs = serverlistmgr.read_legacy_favorites(path)
	if favs then
		save_favorites(favs)
		os.remove(path)
	end
	return favs
end

--------------------------------------------------------------------------------
local function delete_favorite(favorites, del_favorite)
	for i=1, #favorites do
		local fav = favorites[i]

		if fav.address == del_favorite.address and fav.port == del_favorite.port then
			table.remove(favorites, i)
			return
		end
	end
end

--------------------------------------------------------------------------------
function serverlistmgr.get_favorites()
	if serverlistmgr.favorites then
		return serverlistmgr.favorites
	end

	serverlistmgr.favorites = {}

	-- Add favorites, removing duplicates
	local seen = {}
	for _, fav in ipairs(read_favorites() or {}) do
		local key = ("%s:%d"):format(fav.address:lower(), fav.port)
		if not seen[key] then
			seen[key] = true
			serverlistmgr.favorites[#serverlistmgr.favorites + 1] = fav
		end
	end

	return serverlistmgr.favorites
end

--------------------------------------------------------------------------------
function serverlistmgr.add_favorite(new_favorite)
	assert(type(new_favorite.port) == "number")

	-- Whitelist favorite keys
	new_favorite = {
		name = new_favorite.name,
		address = new_favorite.address,
		port = new_favorite.port,
		description = new_favorite.description,
	}

	local favorites = serverlistmgr.get_favorites()
	delete_favorite(favorites, new_favorite)
	table.insert(favorites, 1, new_favorite)
	save_favorites(favorites)
end

--------------------------------------------------------------------------------
function serverlistmgr.delete_favorite(del_favorite)
	local favorites = serverlistmgr.get_favorites()
	delete_favorite(favorites, del_favorite)
	save_favorites(favorites)
end
