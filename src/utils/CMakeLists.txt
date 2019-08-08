
add_library(retdec-utils STATIC
	address.cpp
	alignment.cpp
	byte_value_storage.cpp
	binary_path.cpp
	conversion.cpp
	dynamic_buffer.cpp
	file_io.cpp
	filesystem_path.cpp
	math.cpp
	memory.cpp
	string.cpp
	system.cpp
	time.cpp
)
target_include_directories(retdec-utils
	PUBLIC
		$<INSTALL_INTERFACE:include>
		$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	PRIVATE
		${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(retdec-utils
	# If used in public headers propagated to users.
	PUBLIC
	# If used only internally.
	PRIVATE
		whereami
		mpark_variant
)
if(WIN32)
	target_link_libraries(retdec-utils
		PRIVATE
			shlwapi # for PathRemoveFileSpec()
	)

	# Disable the min() and max() macros to prevent errors when using e.g.
	# std::numeric_limits<...>::max()
	# (http://stackoverflow.com/questions/1904635/warning-c4003-and-errors-c2589-and-c2059-on-x-stdnumeric-limitsintmax).
	# Any target that includes "windows.h" needs to define this.
	target_compile_definitions(retdec-utils PUBLIC NOMINMAX)
endif()

set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/utils)

install(TARGETS retdec-utils
	EXPORT retdec-utils-targets
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/retdec/utils
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/retdec
)

install(EXPORT retdec-utils-targets
	FILE UtilsTargets.cmake
	NAMESPACE retdec::
	DESTINATION ${INSTALL_CONFIGDIR}
)

configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/UtilsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/UtilsConfig.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/UtilsConfig.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)

export(EXPORT retdec-utils-targets
	FILE ${CMAKE_CURRENT_BINARY_DIR}/UtilsTargets.cmake
	NAMESPACE retdec::
)

export(PACKAGE retdec-utils)
