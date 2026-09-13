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
case "$potfile" in
/*) ;;
*) potfile="$PWD/$potfile" ;;
esac

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
	`find builtin/ -name '*.lua'` || exit 1

# Тексты темы RmlUi стоят в документах в [[двойных скобках]]; xgettext
# такой разметки не знает, так что скобки переписываются в N_("…") во
# временную копию с тем же путём — по нему в шаблоне и видно, откуда строка.
work=$(mktemp -d) || abort "не создался временный каталог"
trap 'rm -rf "$work"' EXIT
rml=$(find client/ui -name '*.rml')
for f in $rml; do
	mkdir -p "$work/$(dirname "$f")"
	grep -o '\[\[[^]]*\]\]' "$f" |
		sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' \
			-e 's/^\[\[/N_("/' -e 's/\]\]$/");/' > "$work/$f"
done
test -n "$rml" || exit 0
(cd "$work" && xgettext --package-name=axis --language=C --sort-by-file \
	--add-location=file --keyword=N_ --from-code=utf-8 \
	--output "$work/theme.pot" $rml) || abort "не собрались строки темы"
# msgcat без сортировки держит порядок первого файла: иначе шаблон
# перетасовывается целиком, и правка в один экран выглядит как правка во все.
msgcat --output "$potfile" "$potfile" "$work/theme.pot"
