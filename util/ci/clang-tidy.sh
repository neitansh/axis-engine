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
./util/ci/run-clang-tidy.py \
	-clang-tidy-binary=$CLANG_TIDY -p build \
	-quiet -config="$(cat .clang-tidy)" \
	'^(?!.*/lib/).*/(src|irr)/'
