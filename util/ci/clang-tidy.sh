#! /bin/bash -eu

cmake -B build -DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	-DRUN_IN_PLACE=TRUE \
	-DENABLE_GETTEXT=FALSE \
	-DBUILD_SERVER=TRUE
cmake --build build --target GenerateVersion GenerateBuiltinFilesCpp

# Свой код и форк Irrlicht, но не lib/: там лежат чужие зависимости как есть —
# Lua, Catch2, tiniergltf, — и править их под свой линтер значит расходиться с
# теми, у кого они взяты. Прежний 'src/.*' был не привязан ни к чему и потому
# цеплял и lib/lua/src, и irr/src заодно.
# Заголовки проверяются наравне с .cpp — но только свои. Без этого довода
# clang-tidy 22 тащит в отчёт и SDL2, и catch2, и tiniergltf; с пустым же
# значением он не смотрит заголовки вовсе, а половина кода движка живёт в них.
#
# Образец собирается от корня репозитория, а не пишется руками: в проверке
# заголовков работает регулярка LLVM, а она не умеет заглядывать вперёд, и
# отсечь `lib/` тем же приёмом, что в отборе файлов, не выйдет.
root=$(pwd)

./util/ci/run-clang-tidy.py \
	-clang-tidy-binary=$CLANG_TIDY -p build \
	-quiet -config="$(cat .clang-tidy)" \
	-header-filter="^${root}/(src|irr)/" \
	'^(?!.*/lib/).*/(src|irr)/'
