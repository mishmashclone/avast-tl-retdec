add_library(mpark_variant INTERFACE)

target_include_directories(mpark_variant
	INTERFACE
		$<INSTALL_INTERFACE:include>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/mpark)

install(TARGETS mpark_variant
	EXPORT mpark_variant-targets
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/mpark
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/retdec
)

install(EXPORT mpark_variant-targets
	FILE MparkVariantTargets.cmake
	DESTINATION ${INSTALL_CONFIGDIR}
)

#######################################

configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/MparkVariantConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MparkVariantConfig.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/MparkVariantConfig.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)

export(EXPORT mpark_variant-targets
	FILE ${CMAKE_CURRENT_BINARY_DIR}/MparkVariantTargets.cmake
)

#######################################

export(PACKAGE mpark_variant)
