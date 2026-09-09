#!/bin/sh

# Сторож перевода: не разошёлся ли он с кодом.
#
# Строка доходит до игрока длинной цепочкой — settingtypes.txt →
# settings_translation_file.cpp → axis.pot → ru/axis.po → axis.mo, — и любое
# звено, которое забыли прогнать, роняет строку в английский молча: ни сборка,
# ни тесты этого не замечают. Так и уехало на прод 97 строк настроек.
#
# Проверяет четыре вещи:
#   1. src/settings_translation_file.cpp собран из нынешнего settingtypes.txt;
#   2. подписи, которые переводятся не на месте, помечены N_;
#   3. po/axis.pot содержит ровно те строки, что есть в коде;
#   4. в русском переводе не осталось пустых и неточных записей.
#
# Третье по умолчанию только предупреждает: пока строку пишут, перевода у неё
# ещё и не должно быть. С доводом --complete оно становится ошибкой — этим
# сторож и стоит перед выпуском, которому английские строки уже не простительны.

abort() {
	test -n "$1" && echo >&2 "$1"
	exit 1
}

strict=""
test "$1" = "--complete" && strict=1

scriptisin="$( cd "$( dirname "$0" )" && pwd )"
cd "$scriptisin/.." || abort "не найден корень репозитория"

work=$(mktemp -d) || abort "не создался временный каталог"
trap 'rm -rf "$work"' EXIT

trouble=0

# Сборщик пишет строго в src/settings_translation_file.cpp, поэтому копию
# кладём рядом и возвращаем на место: сторож смотрит, а не правит.
cp src/settings_translation_file.cpp "$work/settings.cpp"
"$scriptisin/extract_pot.sh" "$work/fresh.pot" >/dev/null ||
	abort "не собрался шаблон перевода"

if ! cmp -s "$work/settings.cpp" src/settings_translation_file.cpp; then
	echo "::error::src/settings_translation_file.cpp отстал от builtin/settingtypes.txt"
	echo "Прогони util/updatepo.sh ru и переведи добавившееся."
	diff "$work/settings.cpp" src/settings_translation_file.cpp | head -20
	trouble=1
fi
cp "$work/settings.cpp" src/settings_translation_file.cpp

# Подписи вкладок и разделов объявляют в одном месте, а переводят в другом —
# `fgettext(caption)`, где caption переменная. Сборщику строк видно только
# литералы прямо в вызове, поэтому такая подпись не попадает ни в шаблон, ни в
# перевод: не «перевод потерялся», а его никогда не просили. Ни сборка, ни
# сверка шаблона этого не замечают — обе стороны одинаково слепы. Отметка N_
# показывает строку сборщику, ничего не переводя; здесь и проверяем, что её не
# забыли.
#
# Пустые строки и присваивания переменным пропускаем: `caption = ""` и
# `local tooltip = ""` — заглушки, а не подписи.
bare=$(grep -rnE '\b(caption|title|heading|query_text|info_text|tooltip)[[:space:]]*=[[:space:]]*"[^"]' \
	builtin --include='*.lua' | grep -vE 'N_\(|fgettext' || true)
if test -n "$bare"; then
	echo "::error::подпись не помечена к переводу — оберни литерал в N_()"
	echo "$bare"
	trouble=1
fi

# Шаблон сверяем по строкам, а не побайтно: в шапке .pot стоит время сборки,
# и оно разное при каждом запуске.
only=$(msgcomm --unique --output-file=- "$work/fresh.pot" po/axis.pot |
	grep -c '^msgid "' || true)
if test "$only" -gt 0; then
	echo "::error::po/axis.pot разошёлся с кодом: строк только с одной стороны — $only"
	echo "Прогони util/updatepo.sh ru."
	trouble=1
fi

# Неточная запись переводом не считается: gettext её не берёт, и человек видит
# ту же английскую строку, что и при пустой.
stats=$(LC_ALL=C msgfmt --statistics -o /dev/null po/ru/axis.po 2>&1)
echo "русский: $stats"
case "$stats" in
	*fuzzy*|*untranslated*)
		if test -n "$strict"; then
			echo "::error::в русском переводе есть непереведённые или неточные строки"
			trouble=1
		else
			echo "::warning::в русском переводе есть непереведённые или неточные строки"
		fi
		;;
esac

exit $trouble
