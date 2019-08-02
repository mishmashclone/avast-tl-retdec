
add_library(whereami STATIC
	whereami.c
)

target_include_directories(whereami 
	PUBLIC
		$<INSTALL_INTERFACE:include>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/whereami)

install(TARGETS whereami
	EXPORT whereami-targets
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/whereami
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/retdec
)

install(EXPORT whereami-targets
	FILE WhereamiTargets.cmake
	DESTINATION ${INSTALL_CONFIGDIR}
)

#######################################

configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/WhereamiConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WhereamiConfig.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/WhereamiConfig.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)

export(EXPORT whereami-targets
	FILE ${CMAKE_CURRENT_BINARY_DIR}/WhereamiTargets.cmake
)

#######################################

export(PACKAGE whereami)
