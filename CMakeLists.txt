cmake_minimum_required(VERSION 3.21)

# ---- Options ----

option(COPY_BUILD "Copy the build output to the Fallout 4 directory." OFF)

set(COMMONLIBF4RD_PATH "" CACHE PATH
	"Path to a CommonLibF4RD checkout. Defaults to external/CommonLibF4RD.")

# ---- Cache build vars ----

macro(set_from_environment VARIABLE)
	if (NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} $ENV{${VARIABLE}})
	endif ()
endmacro()

set_from_environment(Fallout4Path)
set_from_environment(VCPKG_ROOT)
set_from_environment(VCPKG_INSTALLATION_ROOT)

if (NOT DEFINED VCPKG_ROOT AND DEFINED VCPKG_INSTALLATION_ROOT)
	set(VCPKG_ROOT "${VCPKG_INSTALLATION_ROOT}")
endif ()

if (DEFINED VCPKG_ROOT)
	set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
	set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
else ()
	message(
		WARNING
		"Neither VCPKG_ROOT nor VCPKG_INSTALLATION_ROOT is set. Continuing without vcpkg."
	)
endif ()

set(Boost_USE_STATIC_RUNTIME OFF CACHE BOOL "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")

# ---- Project ----

project(
	AnimatedWorld
	VERSION 1.0.0
	LANGUAGES CXX
)

include(FetchContent)

FetchContent_Declare(
	MinHook
	GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
	GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
	GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(MinHook)

if (NOT TARGET minhook)
	message(FATAL_ERROR "MinHook target was not created")
endif()
get_target_property(_minhook_type minhook TYPE)
if (NOT _minhook_type STREQUAL "STATIC_LIBRARY")
	message(FATAL_ERROR "MinHook must be a STATIC_LIBRARY, found '${_minhook_type}'")
endif()
unset(_minhook_type)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	@ONLY
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	@ONLY
)

# ---- Include guards ----

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(
		FATAL_ERROR
		"In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
	)
endif ()

# ---- Globals ----

if (MSVC)
	if (NOT ${CMAKE_GENERATOR} STREQUAL "Ninja")
		add_compile_options(
			/MP	# Build with Multiple Processes
		)
	endif ()
endif ()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

set(Boost_USE_STATIC_LIBS ON)

# ---- Dependencies ----

if (NOT TARGET CommonLibF4)
	if (COMMONLIBF4RD_PATH)
		set(_commonlibf4rd_root "${COMMONLIBF4RD_PATH}")
	else ()
		set(_commonlibf4rd_root "${CMAKE_CURRENT_SOURCE_DIR}/external/CommonLibF4RD")
	endif ()

	if (NOT EXISTS "${_commonlibf4rd_root}/CommonLibF4/CMakeLists.txt")
		message(
			FATAL_ERROR
			"CommonLibF4RD was not found at '${_commonlibf4rd_root}'.\n"
			"Run this once from the project root:\n"
			"    git clone https://github.com/Zzyxz/CommonLibF4RD.git external/CommonLibF4RD\n"
			"or configure with -DCOMMONLIBF4RD_PATH=<path to a CommonLibF4RD checkout>."
		)
	endif ()

	set(COMMONLIBF4RD_COMMIT "8a1da09250c16ac909de2bca94c8a66a4e7fd956" CACHE STRING
		"Pinned CommonLibF4RD commit")
	find_package(Git REQUIRED)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${_commonlibf4rd_root}" rev-parse HEAD
		OUTPUT_VARIABLE _commonlibf4rd_head
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE _commonlibf4rd_git_result
	)
	if (NOT _commonlibf4rd_git_result EQUAL 0 OR NOT "${_commonlibf4rd_head}" STREQUAL "${COMMONLIBF4RD_COMMIT}")
		message(
			FATAL_ERROR
			"CommonLibF4RD must be checked out at ${COMMONLIBF4RD_COMMIT}; found '${_commonlibf4rd_head}'."
		)
	endif ()

	add_subdirectory("${_commonlibf4rd_root}/CommonLibF4" CommonLibF4)
endif ()

# CommonLibF4RD's own sources do not build warning-clean at /W4, and its
# CMakeLists suppresses a list that does not include C4100.  Never put /W4 /WX
# in CMAKE_CXX_FLAGS: that leaks into this target and stops the library
# producing CommonLibF4.lib, which then surfaces here as a confusing
# "LNK1181: cannot open input file 'CommonLibF4\Release\CommonLibF4.lib'".
# Warnings-as-errors belongs on OUR target only (see below).
if (MSVC AND TARGET CommonLibF4)
	target_compile_options(
		CommonLibF4
		PRIVATE
			/wd4100 # unreferenced formal parameter
			/wd4245 # conversion from 'type1' to 'type2', signed/unsigned mismatch
			/wd4267 # conversion from 'size_t' to 'type', possible loss of data
	)
endif ()

find_package(fmt REQUIRED CONFIG)
find_package(spdlog REQUIRED CONFIG)

# ---- Add source files ----

include(cmake/sourcelist.cmake)

source_group(
	TREE ${CMAKE_CURRENT_SOURCE_DIR}
	FILES ${SOURCES}
)

source_group(
	TREE ${CMAKE_CURRENT_BINARY_DIR}
	FILES ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
)

# ---- Create DLL ----

add_library(
	${PROJECT_NAME}
	SHARED
	${SOURCES}
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	.clang-format
)

target_compile_definitions(
	${PROJECT_NAME}
	PRIVATE
		_UNICODE
)

target_compile_features(
	${PROJECT_NAME}
	PRIVATE
		cxx_std_20
)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		CommonLibF4::CommonLibF4
		fmt::fmt
		minhook
		spdlog::spdlog
)

target_precompile_headers(
	${PROJECT_NAME}
	PRIVATE
		src/PCH.h
)

if (MSVC)
	target_compile_options(
		${PROJECT_NAME}
		PRIVATE
			/sdl	# Enable Additional Security Checks
			/utf-8	# Set Source and Executable character sets to UTF-8
			/Zi	# Debug Information Format

			/permissive-	# Standards conformance
			/Zc:preprocessor	# Enable preprocessor conformance mode

			/W4	# Warning level
			/WX	# Warning level (warnings are errors)

			/external:anglebrackets	# Treat angle-bracket includes as external
			/external:W0	# ...and do not warn about them

			# /Ob3 was carried over from the old project; CMake's Release flags
			# already pass /Ob2 and MSVC warns D9025 about the override on every
			# file, so it is left out.
			"$<$<CONFIG:DEBUG>:>"
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC->"

			# disable warnings
			/wd4061 # enumerator 'identifier' in switch of enum 'enumeration' is not explicitly handled by a case label
			/wd4100 # unreferenced formal parameter
			/wd4200 # nonstandard extension used : zero-sized array in struct/union
			/wd4201 # nonstandard extension used : nameless struct/union
			/wd4264 # 'virtual_function' : no override available for virtual member function from base 'class'; function is hidden
			/wd4265 # 'type': class has virtual functions, but its non-trivial destructor is not virtual
			/wd4266 # 'function' : no override available for virtual member function from base 'type'; function is hidden
			/wd4324 # 'struct_name' : structure was padded due to __declspec(align())
			/wd4371 # 'classname': layout of class may have changed from a previous version of the compiler
			/wd4514 # 'function' : unreferenced inline function has been removed
			/wd4582 # 'type': constructor is not implicitly called
			/wd4583 # 'type': destructor is not implicitly called
			/wd4623 # 'derived class' : default constructor was implicitly defined as deleted
			/wd4625 # 'derived class' : copy constructor was implicitly defined as deleted
			/wd4626 # 'derived class' : assignment operator was implicitly defined as deleted
			/wd4686 # 'user-defined type' : possible change in behavior, change in UDT return calling convention
			/wd4710 # 'function' : function not inlined
			/wd4711 # function 'function' selected for inline expansion
			/wd4820 # 'bytes' bytes padding added after construct 'member_name'
			/wd5026 # 'type': move constructor was implicitly defined as deleted
			/wd5027 # 'type': move assignment operator was implicitly defined as deleted
			/wd5045 # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
			/wd5053 # support for 'explicit(<expr>)' in C++17 and earlier is a vendor extension
			/wd5105 # macro expansion producing 'defined' has undefined behavior
			/wd5204 # 'type-name': class has virtual functions, but its trivial destructor is not virtual
			/wd5220 # 'member': a non-static data member with a volatile qualified type ...
	)

	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			/WX	# Treat Linker Warnings as Errors

			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif ()

# ---- File copying ----

if (COPY_BUILD)
	if (DEFINED Fallout4Path)
		add_custom_command(
			TARGET ${PROJECT_NAME}
			POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${Fallout4Path}/Data/F4SE/Plugins/
			COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PDB_FILE:${PROJECT_NAME}> ${Fallout4Path}/Data/F4SE/Plugins/
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/config/AnimatedWorld.ini ${Fallout4Path}/Data/F4SE/Plugins/
		)
	else ()
		message(
			WARNING
			"Variable Fallout4Path is not defined. Skipping post-build copy command."
		)
	endif ()
endif ()
