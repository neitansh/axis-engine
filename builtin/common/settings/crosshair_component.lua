-- Настройка перекрестья.
--
-- Перекрестье — единственное, на что игрок смотрит всё время, и вкус тут у
-- каждого свой. Поэтому оно не картинка, а описание: форма, длина штриха,
-- толщина, просвет, точка, обводка, цвета (crosshair.h). Здесь это описание
-- показывается и правится.
--
-- Три вещи, ради которых компонент вообще нужен:
--
--   * предпросмотр. Числа сами по себе ничего не говорят: «просвет 3» видно
--     только глазами. Рисуется он теми же прямоугольниками, которые движок
--     кладёт на экран, — их отдаёт `core.get_crosshair()`, и второй раз эта
--     геометрия нигде не считается;
--   * наборы. Десяток привычных перекрестий одним нажатием, чтобы не
--     подбирать пять чисел с нуля;
--   * код. Строка вида `AXCH1-cross-6-2-3-0-1-...`, которой можно
--     поделиться: скопировать себе, отправить в чат, взять чужую.

-- Экранный пиксель в единицах formspec. Предпросмотр крупнее настоящего
-- перекрестья: разглядывать его нужно, а не целиться им.
local PX = 0.12
-- Сторона окна предпросмотра.
local PREVIEW = 2.4

--[[
Наборы. Формы взяты из того, чем люди пользуются на самом деле: точка,
крест с просветом и без, короткие штрихи, уголки, кольцо, рамка. Числа —
свои: это описание, а не чужие картинки.
]]
local PRESETS = {
	{ name = fgettext_ne("Cross"),            code = "AXCH1-cross-6-2-3-0-0" },
	{ name = fgettext_ne("Cross, no gap"),    code = "AXCH1-cross-7-2-0-0-0" },
	{ name = fgettext_ne("Thick cross"),      code = "AXCH1-cross-4-3-0-0-0" },
	{ name = fgettext_ne("Ticks"),            code = "AXCH1-cross-3-2-5-0-0" },
	{ name = fgettext_ne("Cross with dot"),   code = "AXCH1-cross-6-2-3-2-0" },
	{ name = fgettext_ne("Dot"),              code = "AXCH1-dot-3-1-0-0-0" },
	{ name = fgettext_ne("Small dot"),        code = "AXCH1-dot-1-1-0-0-0" },
	{ name = fgettext_ne("Ring with dot"),    code = "AXCH1-circle-5-1-0-1-0" },
	{ name = fgettext_ne("Frame with dot"),   code = "AXCH1-square-5-1-0-1-0" },
	{ name = fgettext_ne("Brackets"),         code = "AXCH1-brackets-4-2-4-0-0" },
	{ name = fgettext_ne("Diagonal cross"),   code = "AXCH1-x-5-2-2-0-0" },
	{ name = fgettext_ne("Nothing"),          code = "AXCH1-none-0-1-0-0-0" },
}

---Разобрать цвет, записанный человеком.
---
---Принимается то, что люди на самом деле пишут и пересылают: `#ff8800`,
---`ff8800`, короткая запись `#f80`. Всё остальное — отказ: молча подставлять
---что-то своё вместо непонятого хуже, чем сказать «не понял».
local function parse_hex(text)
	if type(text) ~= "string" then
		return nil
	end
	local hex = text:match("^%s*#?(%x%x%x%x%x%x)%s*$")
	if hex then
		return "#" .. hex:lower()
	end
	local short = text:match("^%s*#?(%x%x%x)%s*$")
	if short then
		-- Короткая запись — это та же длинная с удвоенными разрядами.
		return ("#%s%s%s%s%s%s"):format(
			short:sub(1, 1), short:sub(1, 1),
			short:sub(2, 2), short:sub(2, 2),
			short:sub(3, 3), short:sub(3, 3)):lower()
	end
	return nil
end

---Дописать к набору цвета текущего перекрестья: наборы задают форму, а цвет
---игрок выбирает отдельно и терять его при смене формы незачем.
local function preset_code(short)
	local current = core.get_crosshair(1)
	local tail = current.code:match("^AXCH1%-[^-]+%-%d+%-%d+%-%d+%-%d+%-%d+%-(.+)$")
	return tail and (short .. "-" .. tail) or short
end

---Цвет из настройки вида `(r,g,b)` в вид `#rrggbb`.
local function setting_color(name, fallback)
	local value = core.settings:get(name)
	if not value then
		return fallback
	end
	local r, g, b = value:match("^%s*%(?%s*(%d+)%s*,%s*(%d+)%s*,%s*(%d+)%s*%)?%s*$")
	if not r then
		return fallback
	end
	return string.format("#%02x%02x%02x", tonumber(r), tonumber(g), tonumber(b))
end

---Записать цвет `#rrggbb` в настройку вида `(r,g,b)`.
local function set_setting_color(name, hex)
	local r, g, b = hex:match("^#(%x%x)(%x%x)(%x%x)$")
	if not r then
		return
	end
	core.settings:set(name, ("(%d,%d,%d)"):format(
		tonumber(r, 16), tonumber(g, 16), tonumber(b, 16)))
end

---Прямоугольники предпросмотра, обрезанные по его окну.
---
---Перекрестье может оказаться больше окна — тогда лучше показать середину,
---чем растянуть всё до неузнаваемости: игрок настраивает то, что увидит в
---игре, а не картинку.
local function preview_boxes(x0, y0, pieces, color)
	local out = {}
	local cx, cy = x0 + PREVIEW / 2, y0 + PREVIEW / 2
	for _, p in ipairs(pieces) do
		local bx, by = cx + p.x * PX, cy + p.y * PX
		local bw, bh = p.w * PX, p.h * PX

		-- обрезка по окну
		local x1, y1 = math.max(bx, x0), math.max(by, y0)
		local x2, y2 = math.min(bx + bw, x0 + PREVIEW), math.min(by + bh, y0 + PREVIEW)
		if x2 > x1 and y2 > y1 then
			out[#out + 1] = ("box[%f,%f;%f,%f;%s]"):format(x1, y1, x2 - x1, y2 - y1, color)
		end
	end
	return table.concat(out)
end

return {
	query_text = "Crosshair",
	context = "client",

	get_formspec = function(self, avail_w)
		local ch = core.get_crosshair(1)

		local fs = {}

		-- Заголовок раздела рисует сам список настроек, второй здесь не нужен.
		-- Предпросмотр на двух фонах разом: тёмном и светлом. Перекрестье
		-- живёт и там, и там — небо и земля, — и обводку видно только так.
		local px, py = 0, 0.1
		fs[#fs + 1] = ("box[%f,%f;%f,%f;#101014]"):format(px, py, PREVIEW / 2, PREVIEW)
		fs[#fs + 1] = ("box[%f,%f;%f,%f;#c8c8c8]"):format(px + PREVIEW / 2, py,
			PREVIEW / 2, PREVIEW)
		fs[#fs + 1] = preview_boxes(px, py, ch.outline_pieces, ch.outline_color)
		fs[#fs + 1] = preview_boxes(px, py, ch.pieces, ch.color)

		-- Правая колонка: набор и три цвета. Подписи стоят слева от полей, а
		-- не над ними: подпись над полем съедает строку и в тесной колонке
		-- налезает на соседа.
		local right = px + PREVIEW + 0.35
		local label_w = 1.6
		local field_x = right + label_w
		local field_w = math.min(2.3, math.max(1.3, avail_w - field_x - 0.1))

		local names = { core.formspec_escape(fgettext("Custom")) }
		for i, preset in ipairs(PRESETS) do
			names[i + 1] = core.formspec_escape(preset.name)
		end

		local row_h = 0.75
		local rows = {
			{ label = fgettext("Preset") },
			{ label = fgettext("Color"), name = "crosshair_hex",
				value = setting_color("crosshair_color", "#ffffff") },
			{ label = fgettext("Outline"), name = "crosshair_outline_hex",
				value = setting_color("crosshair_outline_color", "#000000") },
			{ label = fgettext("On target"), name = "crosshair_object_hex",
				value = setting_color("crosshair_object_color", "#ff5050") },
		}

		for i, row in ipairs(rows) do
			local y = py + 0.1 + (i - 1) * row_h
			-- Подпись выравнивается по середине поля: у formspec метка стоит
			-- по базовой линии, и без этой поправки она едет вверх.
			fs[#fs + 1] = ("label[%f,%f;%s]"):format(right, y + 0.3, row.label)
			if row.name then
				-- Своя подложка: у поля в этой теме фона нет, и белый текст
				-- висит прямо на фоне списка — читается как обрывок, а не как
				-- поле ввода.
				fs[#fs + 1] = ("box[%f,%f;%f,0.62;#00000060]"):format(
					field_x, y, field_w)
				fs[#fs + 1] = ("field[%f,%f;%f,0.62;%s;;%s]"):format(
					field_x + 0.08, y, field_w - 0.16, row.name,
					core.formspec_escape(row.value))
				fs[#fs + 1] = ("field_close_on_enter[%s;false]"):format(row.name)
			else
				-- Последним параметром просим номер пункта, а не название:
				-- имена переводятся, и сравнивать по ним значило бы ломаться
				-- при первой же смене языка.
				-- Списку отдаётся вся оставшаяся ширина: названия наборов
				-- длиннее полей с цветом, и в общую мерку они не влезают.
				fs[#fs + 1] = ("dropdown[%f,%f;%f,0.62;crosshair_preset;%s;1;true]"):format(
					field_x, y, math.max(field_w, avail_w - field_x - 0.1),
					table.concat(names, ","))
			end
		end

		local set_y = py + 0.1 + #rows * row_h
		fs[#fs + 1] = ("button[%f,%f;%f,0.7;crosshair_colors;%s]"):format(
			field_x, set_y, field_w, fgettext("Set colors"))

		-- Обмен. Самого кода на экране нет: он длинный, в колонку не влезает и
		-- обрезается на середине, а читать его глазами незачем — он ездит
		-- через буфер обмена.
		local code_y = math.max(py + PREVIEW, set_y + 0.7) + 0.35
		fs[#fs + 1] = ("label[0,%f;%s]"):format(code_y + 0.3, fgettext("Share"))
		fs[#fs + 1] = ("button[1.2,%f;2.0,0.7;crosshair_copy;%s]"):format(
			code_y, fgettext("Copy"))
		fs[#fs + 1] = ("button[3.3,%f;2.0,0.7;crosshair_apply;%s]"):format(
			code_y, fgettext("Paste"))
		fs[#fs + 1] = ("tooltip[crosshair_copy;%s]"):format(
			fgettext("Copy your crosshair as a code you can send to others"))
		fs[#fs + 1] = ("tooltip[crosshair_apply;%s]"):format(
			fgettext("Apply a crosshair code from the clipboard"))

		-- Об удачном копировании отвечает сам движок — строкой внизу окна;
		-- второй такой же ответ здесь был бы просто дублем. А вот про чужую
		-- строку в буфере сказать больше некому.
		if self.pasted_bad then
			fs[#fs + 1] = ("label[5.5,%f;%s]"):format(code_y + 0.3,
				core.colorize("#f88", fgettext("No crosshair code in the clipboard")))
		end

		return table.concat(fs), code_y + 0.9
	end,

	on_submit = function(self, fields)
		-- Порядок здесь важнее, чем кажется.
		--
		-- Формспек присылает не только то, что нажали, но и содержимое всех
		-- своих полей и списков разом. Выпадающий список наборов приходит
		-- каждый раз, и обработчик, начинавшийся с него, выходил на первом же
		-- условии: цвета и кнопки обмена до дела не доходили вовсе.
		--
		-- Поэтому сперва разбираются нажатия, потом всё остальное, и ветки не
		-- обрываются возвратом, пока не сделано всё.
		local changed = false

		-- Кнопки — первыми: они говорят о намерении прямо, а поля и списки
		-- приходят вместе с ними просто за компанию.
		if fields.crosshair_copy then
			core.copy_to_clipboard(core.get_crosshair(1).code)
			return false
		end

		if fields.crosshair_apply then
			-- Код приходит из буфера обмена: игрок скопировал его из чата или
			-- у товарища. Чужую строку не разбираем на части — либо она
			-- целиком наша, либо не применяется вовсе.
			local code = core.paste_from_clipboard()
			self.pasted_bad = not (code and core.set_crosshair_code(code))
			return true
		end

		-- Цвета. Берутся при любой отправке, а не только по кнопке: игрок
		-- правит поле и жмёт что угодно, ожидая, что введённое возьмётся.
		for field, setting in pairs({
			crosshair_hex = "crosshair_color",
			crosshair_outline_hex = "crosshair_outline_color",
			crosshair_object_hex = "crosshair_object_color",
		}) do
			local text = fields[field]
			if text and text ~= "" then
				local hex = parse_hex(text)
				-- Непонятое значение не трогаем вовсе: подставлять вместо
				-- него что-то своё хуже, чем оставить как было.
				if hex and hex ~= setting_color(setting, "") then
					set_setting_color(setting, hex)
					changed = true
				end
			end
		end

		-- Набор. Первый пункт списка — «своё», он ничего не меняет; после
		-- применения список снова показывает его, так что повторно тот же
		-- набор не навязывается.
		local idx = tonumber(fields.crosshair_preset)
		local preset = idx and idx > 1 and PRESETS[idx - 1]
		if preset and core.set_crosshair_code(preset_code(preset.code)) then
			changed = true
		end

		return changed
	end,
}
