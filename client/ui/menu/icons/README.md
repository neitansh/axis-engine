# Иконки вкладок настроек

Из набора Lucide (https://lucide.dev, лицензия ISC, текст в LICENSE.txt),
файлы `.svg` взяты как есть. `.png` — растр 48×48 для RmlUi (SVG она без
плагина не читает): `magick -background none -density 384 имя.svg -resize 48x48 имя.png`
после замены `stroke="currentColor"` на белый.

Имя файла — id страницы настроек из `settings_catalog.cpp`; остальные
(`back`, `chevron`, `check`, `reset`, `plus`, `package`, `star`, `alert`) — по смыслу, растр
того размера, в каком стоят на экране.
