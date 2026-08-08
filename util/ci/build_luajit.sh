#!/bin/bash -eu
cd $HOME
# На self-hosted раннере $HOME переживает прогоны: ephemeral чистит только
# _work, а клон остаётся и роняет следующий git clone. Сносим его, чтобы
# сборка вела себя одинаково и здесь, и на GitHub-hosted с чистой машиной.
rm -rf LuaJIT
git clone --depth 1 https://github.com/LuaJIT/LuaJIT
pushd LuaJIT
make -j$(nproc)
popd
