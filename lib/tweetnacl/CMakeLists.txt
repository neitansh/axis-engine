# Проверка подписей Ed25519. Чужой код, взят целиком — см. README.md.
#
# Собирается как C, а не C++: в исходнике есть конструкции, которые C++
# компилятор не пропустит (константы без инициализатора), и переписывать их
# нельзя — файл заменяется целиком при обновлении.
project(tweetnacl C)

add_library(tweetnacl STATIC tweetnacl.c tweetnacl.h)

target_include_directories(tweetnacl INTERFACE .)
