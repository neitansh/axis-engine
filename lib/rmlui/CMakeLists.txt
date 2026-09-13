cmake_minimum_required(VERSION 3.12)

project(rmlui VERSION 6.3 LANGUAGES CXX)

file(GLOB_RECURSE rmlui_SRCS
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Debugger/*.cpp")

add_library(rmlui STATIC ${rmlui_SRCS})
add_library(RmlUi::RmlUi ALIAS rmlui)

target_compile_features(rmlui PUBLIC cxx_std_17)
set_target_properties(rmlui PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(rmlui SYSTEM PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/Include")

target_compile_definitions(rmlui
	PUBLIC RMLUI_STATIC_LIB
	PRIVATE RMLUI_FONT_ENGINE_FREETYPE "RMLUI_VERSION=\"${PROJECT_VERSION}\"")

target_link_libraries(rmlui PRIVATE Freetype::Freetype)

if(MSVC)
	target_compile_definitions(rmlui PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(rmlui PRIVATE /permissive-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
	target_compile_options(rmlui PRIVATE -Wno-stringop-overflow)
endif()

# Рендер на OpenGL 3 из поставки библиотеки. Он написан под глобальные
# gl*-функции; заголовок в RMLUI_GL3_LOADER сводит их к загрузчику, которым
# пользуется потребитель, и без него бэкенд не собирается.
if(RMLUI_GL3_LOADER)
	add_library(rmlui_gl3 STATIC
		"${CMAKE_CURRENT_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp")
	add_library(RmlUi::GL3 ALIAS rmlui_gl3)
	target_include_directories(rmlui_gl3 SYSTEM PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}/Backends")
	# Слои без MSAA: кадр сцены копируется в слой RmlUi блитом, а блит из
	# обычного буфера в многосэмпловый OpenGL запрещает. Без этой копии
	# backdrop-filter размывал бы пустоту — сцена рисуется не через RmlUi.
	target_compile_definitions(rmlui_gl3 PRIVATE
		"RMLUI_GL3_CUSTOM_LOADER=\"${RMLUI_GL3_LOADER}\""
		RMLUI_NUM_MSAA_SAMPLES=0)
	target_link_libraries(rmlui_gl3
		PUBLIC rmlui
		PRIVATE ${RMLUI_GL3_LOADER_LIBRARIES})
	if(MSVC)
		target_compile_definitions(rmlui_gl3 PRIVATE _CRT_SECURE_NO_WARNINGS)
	endif()
endif()
