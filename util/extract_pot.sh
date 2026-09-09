#!/bin/sh

# Собирает шаблон перевода из исходников.
#
# Отдельным скриптом, потому что зовут его двое: util/updatepo.sh, чтобы
# обновить po/axis.pot, и util/check_translations.sh, чтобы сверить его с кодом.
# Список ключевых слов xgettext, написанный в двух местах, разошёлся бы на
# первой же правке, и сторож перестал бы сторожить ровно то, что должен.
#
# Довод — куда положить шаблон.

abort() {
	test -n "$1" && echo >&2 "$1"
	exit 1
}

potfile="$1"
test -n "$potfile" || abort "нужен путь к .pot"

scriptisin="$( cd "$( dirname "$0" )" && pwd )"
cd "$scriptisin/.." || abort "не найден корень репозитория"

# Тексты настроек доходят до xgettext только через сгенерированный
# src/settings_translation_file.cpp: сам settingtypes.txt он не читает.
lua=$(command -v luajit || command -v lua || command -v lua5.1) ||
	abort "нужен lua или luajit: им собирается src/settings_translation_file.cpp"
"$lua" util/generate_settings_translation_file.lua ||
	abort "не собрался src/settings_translation_file.cpp"

xgettext --package-name=axis \
	--add-comments='TRANSLATORS:'\
	--sort-by-file \
	--add-location=file \
	--keyword=N_ \
	--keyword=wgettext \
	--keyword=fwgettext \
	--keyword=fgettext \
	--keyword=fgettext_ne \
	--keyword=hgettext \
	--keyword=strgettext \
	--keyword=wstrgettext \
	--keyword=core.gettext \
	--keyword=showTranslatedStatusText \
	--keyword=fmtgettext \
	--output "$potfile" \
	--from-code=utf-8 \
	`find src/ -name '*.cpp' -o -name '*.h'` \
	`find builtin/ -name '*.lua'`
