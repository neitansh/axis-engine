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

-- Отправка не ждёт ответа и не занимает главный поток.
--
-- `fetch_async` здесь единственный годный способ, и вот почему. Синхронный
-- запрос движок с главного потока не пускает вовсе — и правильно делает.
-- Асинхронное состояние (`core.handle_async`) живёт при меню и уходит вместе с
-- ним, а самый важный отчёт — как раз тот, что отправляют, уходя в игру: его
-- бы и потеряли. `fetch_async` отдаёт запрос общей качалке движка, которая
-- живёт весь запуск клиента, и возвращается сразу.
--
-- Ответ не читаем: узнавать нам нечего, а `fetch_async_get` пришлось бы
-- опрашивать — и опять из меню, которого уже нет.
local function post(url, key, body)
	local http = core.get_http_api()
	if not http then
		return
	end
	http.fetch_async({
		url = url .. "/presence",
		method = "POST",
		-- Секунда на свою же машину — с запасом. Лаунчер, который не ответил и
		-- за неё, всё равно ничего не покажет.
		timeout = 1,
		extra_headers = {
			"Authorization: Bearer " .. key,
			"Content-Type: application/json",
		},
		data = body,
	})
end

local function send(what)
	local url, key = door()
	if not url then
		return
	end
	local body = core.write_json(what)
	if body == said then
		return
	end
	said = body
	post(url, key, body)
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

--- Уход в игру. Следом закрывается меню, поэтому запрос и уходит общей качалке
--- движка, а не состоянию при меню.
function presence.playing(what)
	send(what)
end
