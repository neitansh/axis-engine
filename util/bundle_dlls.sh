#!/bin/bash
# msys2-bundledlls - Copy DLLs linked against a Windows executable for bundling with a distribution

if [ "$#" -ne 2 ];
then
	echo "Usage: ./bundledlls <executable> <directory>"
	exit
fi

mkdir -p $2

# CLANG64 ships the LLVM tools instead of binutils
if command -v objdump >/dev/null 2>&1; then
	objdump_bin=objdump
elif command -v llvm-objdump >/dev/null 2>&1; then
	objdump_bin=llvm-objdump
else
	echo "Neither objdump nor llvm-objdump is available"
	exit 1
fi

target_dir=$2
declare -A bundled

# Copies every DLL of the current MSYS2 prefix that the given file needs, both
# directly and through other bundled DLLs. Imports that do not exist in the
# prefix are provided by Windows itself and must not be shipped.
copy_deps() {
	local dll key src

	while read -r dll; do
		if [ -z "$dll" ]; then
			continue
		fi

		# DLL imports are case insensitive on Windows
		key=${dll,,}
		if [ -n "${bundled[$key]}" ]; then
			continue
		fi
		bundled[$key]=1

		src="$MINGW_PREFIX/bin/$dll"
		if [ ! -f "$src" ]; then
			continue
		fi

		echo "$src"
		cp "$src" "$target_dir/"

		copy_deps "$src"
	done < <("$objdump_bin" -p "$1" | awk '/DLL Name:/ { print $3 }')
}

copy_deps "$1"
