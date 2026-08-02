file(GLOB config_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

set(config_SRCS
	${config_HDRS}
	${CMAKE_CURRENT_SOURCE_DIR}/config_domains.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/config_manager.cpp
	PARENT_SCOPE
)
