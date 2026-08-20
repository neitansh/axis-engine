-- Axis
-- SPDX-License-Identifier: LGPL-2.1-or-later
-- Copyright (C) 2026 the Axis contributors

-- Рассказать лаунчеру, чем игрок занят.
--
-- Показывает это лаунчер — в профиле Discord, — и он же сочиняет слова. Отсюда
-- уезжают только факты: где игрок и сколько народу с ним рядом. Ни адресов, ни
-- имён миров: то, что попало в чужой профиль, всё равно что сказано вслух.
--
-- Дорога та же, что и за билетом: дверца лаунчера и ключ этого запуска. Нет их
-- — игру запустили не лаунчером, показывать некому, и молчим.

presence = {}

-- Что отправили в прошлый раз. Одно и то же дважды слать незачем: опрос
-- очереди идёт кругом, а состояние в нём меняется редко.
local said = nil

local function door()
	local url = core.settings:get("axis_ticket_url") or ""
	local key = core.settings:get("axis_ticket_key") or ""
	if url == "" or key == "" then
		return nil
	end
	return url, key
end

-- Отправка живёт в отдельном состоянии: главное не имеет права стоять, пока
-- идёт запрос, даже к себе же на машину.
local function post(url, key, body)
	core.handle_async(function(p)
		local http = core.get_http_api()
		if not http then
			return
		end
		http.fetch_sync({
			url = p.url .. "/presence",
			method = "POST",
			timeout = 5,
			extra_headers = {
				"Authorization: Bearer " .. p.key,
				"Content-Type: application/json",
			},
			data = p.body,
		})
	end, { url = url, key = key, body = body }, function() end)
end

-- Уходя в игру, ждём отправку: асинхронную за нас никто не дождётся — меню
-- закрывается, и с ним уходит то состояние, в котором она шла.
local function post_now(url, key, body)
	local http = core.get_http_api()
	if not http then
		return
	end
	http.fetch_sync({
		url = url .. "/presence",
		method = "POST",
		-- Секунда на свою же машину — с запасом. Лаунчер, который не ответил и
		-- за неё, всё равно ничего не покажет, а игрок ждать не должен.
		timeout = 1,
		extra_headers = {
			"Authorization: Bearer " .. key,
			"Content-Type: application/json",
		},
		data = body,
	})
end

local function send(what, now)
	local url, key = door()
	if not url then
		return
	end
	local body = core.write_json(what)
	if body == said then
		return
	end
	said = body
	if now then
		post_now(url, key, body)
	else
		post(url, key, body)
	end
end

function presence.in_menu()
	send({ where = "menu" })
end

--- Очередь: сколько ждёт и сколько нужно.
function presence.in_queue(mode, room, waiting, needed)
	send({
		where = "queue",
		mode = mode or "",
		room = room or "",
		waiting = waiting or 0,
		needed = needed or 0,
	})
end

--- Уход в игру. Ждём отправки: следом закрывается меню.
function presence.playing(what)
	send(what, true)
end
