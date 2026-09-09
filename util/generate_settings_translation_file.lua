-- Собирает src/settings_translation_file.cpp из builtin/settingtypes.txt.
--
-- xgettext читает только .cpp и .lua, а тексты настроек лежат в
-- settingtypes.txt. Файл с фальшивыми вызовами gettext — единственный способ
-- показать их сборщику: чего в нём нет, того не будет ни в .pot, ни в .po, ни
-- в .mo, и настройка молча останется английской.
--
-- Раньше файл собирался запуском самой игры: в builtin/common/settings/init.lua
-- расшивался dofile, движок запускался, файл забирался из bin/, строка
-- возвращалась в комментарий. Шаг ручной, ничем не проверялся и потому
-- пропускался — так и разошлись 97 строк.
--
-- Разбор и сама сборка берутся у движка как есть: settingtypes.lua и
-- generate_from_settingtypes.lua здесь те же, что читает игра. Своё тут только
-- то, без чего они не запускаются вне движка.
--
-- Запускать из корня репозитория: lua util/generate_settings_translation_file.lua

local root = arg[0]:match("^(.*)/util/[^/]+$") or "."

core = {
	get_builtin_path = function() return root .. "/builtin/" end,
	log = function(level, message)
		io.stderr:write(level .. ": " .. message .. "\n")
	end,
}

DIR_DELIM = "/"
-- Разбор смотрит на INIT, чтобы решить, дошли ли до настроек крейтов и модов;
-- их здесь не читают — parse_config_file вызывается с parse_mods = false.
INIT = "mainmenu"

-- В разборе строки только помечаются к переводу, а не переводятся.
function fgettext_ne(text) return text end
function fgettext(text) return text end

dofile(root .. "/builtin/common/misc_helpers.lua")
dofile(root .. "/builtin/common/settings/settingtypes.lua")
dofile(root .. "/builtin/common/settings/generate_from_settingtypes.lua")

-- Разбор молчит о своих потерях: ошибочную строку он пишет в лог и идёт
-- дальше. Пустой файл на выходе значит, что не разобралось ничего.
local written = assert(io.open("src/settings_translation_file.cpp", "r"))
local _, count = written:read("*a"):gsub("gettext%(", "")
written:close()
assert(count > 100, "собралось всего " .. count .. " строк — разбор сломался")
io.stderr:write("строк настроек: " .. count .. "\n")
