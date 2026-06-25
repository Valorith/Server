cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})

if(NOT CMAKE_TOOLCHAIN_FILE)
	if(DEFINED ENV{VCPKG_ROOT})
		message(STATUS "Using vcpkg from VCPKG_ROOT")
		set(CMAKE_TOOLCHAIN_FILE
			"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
			CACHE FILEPATH "Vcpkg toolchain file"
		)
	else()
		message(STATUS "Using vcpkg submodule")
		set(CMAKE_TOOLCHAIN_FILE
			"${CMAKE_CURRENT_SOURCE_DIR}/submodules/vcpkg/scripts/buildsystems/vcpkg.cmake"
			CACHE FILEPATH "Vcpkg toolchain file"
		)
	endif()
endif()

project(EQEmu
	VERSION 1.2.13
	LANGUAGES CXX 
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

#explicitly set CMP0167 for Find Boost
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EQEMU_BUILD_PCH "Build with precompiled headers (Windows)" ON)
option(EQEMU_DETERMINISTIC_BUILD "Normalize build timestamps and source paths for reproducible binaries." ON)
option(EQEMU_DETERMINISTIC_STRIP_DEBUG "Strip debug sections from deterministic Unix binaries." ON)

if(EQEMU_DETERMINISTIC_BUILD)
	execute_process(
		COMMAND git -C "${CMAKE_SOURCE_DIR}" log -1 --format=%ct
		OUTPUT_VARIABLE EQEMU_SOURCE_DATE_EPOCH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)

	if(NOT EQEMU_SOURCE_DATE_EPOCH MATCHES "^[0-9]+$")
		if(DEFINED ENV{SOURCE_DATE_EPOCH} AND "$ENV{SOURCE_DATE_EPOCH}" MATCHES "^[0-9]+$")
			set(EQEMU_SOURCE_DATE_EPOCH "$ENV{SOURCE_DATE_EPOCH}")
		else()
			set(EQEMU_SOURCE_DATE_EPOCH "0")
		endif()
	endif()

	set(ENV{SOURCE_DATE_EPOCH} "${EQEMU_SOURCE_DATE_EPOCH}")
	string(TIMESTAMP EQEMU_COMPILE_DATE_VALUE "%b %d %Y" UTC)
	string(TIMESTAMP EQEMU_COMPILE_TIME_VALUE "%H:%M:%S" UTC)
	string(TIMESTAMP EQEMU_COMPILE_TIMESTAMP_VALUE "%a %b %d %H:%M:%S %Y" UTC)
	string(REGEX REPLACE " 0([1-9] )" "  \\1" EQEMU_COMPILE_DATE_VALUE "${EQEMU_COMPILE_DATE_VALUE}")
	string(REGEX REPLACE "^([A-Za-z][A-Za-z][A-Za-z] [A-Za-z][A-Za-z][A-Za-z]) 0([1-9] )" "\\1  \\2" EQEMU_COMPILE_TIMESTAMP_VALUE "${EQEMU_COMPILE_TIMESTAMP_VALUE}")

	add_compile_definitions(
		EQEMU_COMPILE_DATE="${EQEMU_COMPILE_DATE_VALUE}"
		EQEMU_COMPILE_TIME="${EQEMU_COMPILE_TIME_VALUE}"
		EQEMU_COMPILE_TIMESTAMP="${EQEMU_COMPILE_TIMESTAMP_VALUE}"
	)

	if(MSVC)
		add_compile_options(
			/experimental:deterministic
			"/pathmap:${CMAKE_SOURCE_DIR}=."
			"/pathmap:${CMAKE_BINARY_DIR}/vcpkg_installed=./vcpkg_installed"
		)
		add_link_options(/Brepro)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		add_compile_options(
			"-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
			"-ffile-prefix-map=${CMAKE_BINARY_DIR}=."
			"-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=."
			"-fmacro-prefix-map=${CMAKE_BINARY_DIR}=."
			"-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=."
			"-fdebug-prefix-map=${CMAKE_BINARY_DIR}=."
		)
		add_link_options("-Wl,--build-id=none")
		if(EQEMU_DETERMINISTIC_STRIP_DEBUG)
			add_link_options("-Wl,--strip-debug")
		endif()
	endif()
endif()

if(EQEMU_DETERMINISTIC_BUILD AND UNIX AND NOT APPLE AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
	set(EQEMU_VCPKG_TRIPLET_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
	if(EXISTS "${EQEMU_VCPKG_TRIPLET_ROOT}/lib/libcrypto.a")
		set(OPENSSL_CRYPTO_LIBRARY "${EQEMU_VCPKG_TRIPLET_ROOT}/lib/libcrypto.a" CACHE FILEPATH "Deterministic OpenSSL crypto library" FORCE)
	endif()
	if(EXISTS "${EQEMU_VCPKG_TRIPLET_ROOT}/lib/libssl.a")
		set(OPENSSL_SSL_LIBRARY "${EQEMU_VCPKG_TRIPLET_ROOT}/lib/libssl.a" CACHE FILEPATH "Deterministic OpenSSL SSL library" FORCE)
	endif()
	if(EXISTS "${EQEMU_VCPKG_TRIPLET_ROOT}/lib/libluajit-5.1.a")
		find_library(EQEMU_LUAJIT_MATH_LIBRARY m)
		set(LUAJIT_LIBRARY "${EQEMU_VCPKG_TRIPLET_ROOT}/lib/libluajit-5.1.a" CACHE FILEPATH "Deterministic LuaJIT library" FORCE)
		set(LUAJIT_LIBRARIES "${LUAJIT_LIBRARY};${EQEMU_LUAJIT_MATH_LIBRARY}" CACHE STRING "Deterministic LuaJIT libraries" FORCE)
	endif()
	if(EXISTS "${EQEMU_VCPKG_TRIPLET_ROOT}/lib/pkgconfig")
		set(ENV{PKG_CONFIG_PATH} "${EQEMU_VCPKG_TRIPLET_ROOT}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
	endif()
endif()

if(EQEMU_DETERMINISTIC_BUILD AND UNIX AND NOT APPLE)
	find_package(Python3 COMPONENTS Interpreter QUIET)
	set(EQEMU_OPENSSL_BUILT_ON_VALUE "built on: Thu Jan  1 00:00:00 1970 UTC")
	function(eqemu_normalize_unix_binary target_name)
		if(Python3_Interpreter_FOUND)
			add_custom_command(TARGET ${target_name} POST_BUILD
				COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/utils/scripts/release/normalize-linux-binary.py" "$<TARGET_FILE:${target_name}>" "${EQEMU_OPENSSL_BUILT_ON_VALUE}"
				VERBATIM
			)
		endif()
	endfunction()
else()
	function(eqemu_normalize_unix_binary target_name)
	endfunction()
endif()

if(MSVC)
	add_compile_options(/bigobj)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX CRASH_LOGGING _HAS_AUTO_PTR_ETC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

	option(EQEMU_DISABLE_MSVC_WARNINGS "Disable MSVC compile warnings." ON)
	if(EQEMU_DISABLE_MSVC_WARNINGS)
		add_compile_options(/W0 /wd4005 /wd4996 /nologo /Os)
	endif()

	option(EQEMU_MSVC_DEBUG_CRT "Use the debug CRT on MSVC debug builds" OFF)
	if(EQEMU_MSVC_DEBUG_CRT)
		add_compile_definitions(_CRT_DBG_MAP_ALLOC)
	endif()
else()
	add_compile_definitions(HAS_UNION_SEMUN)
endif()

#FreeBSD support
if(UNIX)
	if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
		add_compile_definitions(FREEBSD)
		add_compile_definitions(_GLIBCXX_USE_C99)
		set(FREEBSD TRUE)
	endif()
	if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
		add_compile_definitions(DARWIN)
		set(DARWIN TRUE)
	endif()
endif()

find_package(Boost REQUIRED)
set(EQEMU_BOOST_HEADER_TARGET Boost::headers)
if(NOT TARGET Boost::headers)
	set(EQEMU_BOOST_HEADER_TARGET Boost::boost)
endif()
find_package(cereal CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(unofficial-libmariadb CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LuaJit REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(PerlLibs)

message(STATUS "**************************************************")
message(STATUS "* Library Detection                              *")
message(STATUS "**************************************************")

if(MARIADB_FOUND)
	message(STATUS "* MariaDB:                                 FOUND *")
else()
	message(STATUS "* MariaDB:                               MISSING *")
endif()

if(ZLIB_FOUND)
	message(STATUS "* ZLIB:                                    FOUND *")
else()
	message(STATUS "* ZLIB:                                  MISSING *")
endif()

if(LuaJit_FOUND)
	message(STATUS "* LuaJIT:                                  FOUND *")
else()
	message(STATUS "* LuaJIT:                                MISSING *")
endif()

if(PerlLibs_FOUND)
	message(STATUS "* Perl:                                    FOUND *")
else()
	message(STATUS "* Perl:                                  MISSING *")
endif()

if(OpenSSL_FOUND)
	message(STATUS "* OpenSSL:                                 FOUND *")
else()
	message(STATUS "* OpenSSL:                               MISSING *")
endif()

message(STATUS "PERL_INCLUDE_PATH: ${PERL_INCLUDE_PATH}")
message(STATUS "PERL_LIBRARY: ${PERL_LIBRARY}")
message(STATUS "PERL_INCLUDE_DIR: ${PERL_INCLUDE_DIR}")
message(STATUS "PERL_INCLUDE_DIRS: ${PERL_INCLUDE_DIRS}")
message(STATUS "PERL_LIBRARIES: ${PERL_LIBRARIES}")
message(STATUS "PERL_VERSION: ${PERL_VERSION}")
message(STATUS "**************************************************")

#options
option(EQEMU_BUILD_SERVER "Build the game server." ON)
option(EQEMU_BUILD_LOGIN "Build the login server." ON)
option(EQEMU_BUILD_TESTS "Build utility tests." OFF)
option(EQEMU_BUILD_CLIENT_FILES "Build Client Import/Export Data Programs." ON)

if(PerlLibs_FOUND)
	option(EQEMU_BUILD_PERL "Build Perl parser." ON)

	if(EQEMU_BUILD_PERL)
		set(PERL_LIBRARY_TYPE "    Perl")
	else()
		set(PERL_LIBRARY_TYPE " Missing")
	endif()
else()
	set(PERL_LIBRARY_TYPE "Disabled")
endif()

message(STATUS "")
message(STATUS "**************************************************")
message(STATUS "* Library Usage                                  *")
message(STATUS "**************************************************")
message(STATUS "* Database:                              MariaDB *")
message(STATUS "* TLS:                                   OpenSSL *")
message(STATUS "* Lua:                                    LuaJIT *")
message(STATUS "* Perl:                                 ${PERL_LIBRARY_TYPE} *")
message(STATUS "* zlib:                                     ZLIB *")
message(STATUS "**************************************************")


option(EQEMU_BUILD_LUA "Build Lua parser." ON)

if(EQEMU_BUILD_SERVER OR EQEMU_BUILD_LOGIN OR EQEMU_BUILD_TESTS OR EQEMU_BUILD_CLIENT_FILES)
	add_subdirectory(common)
	add_subdirectory(libs)
else()
	message(FATAL_ERROR "No targets were selected to build, we must build at least one target.")
endif()

if(EQEMU_BUILD_SERVER)
	add_subdirectory(shared_memory)
	add_subdirectory(world)
	add_subdirectory(zone)
	add_subdirectory(ucs)
	add_subdirectory(queryserv)
	add_subdirectory(eqlaunch)
endif()

if(EQEMU_BUILD_LOGIN)
	add_subdirectory(loginserver)
endif()

if(EQEMU_BUILD_TESTS)
	add_subdirectory(tests)
endif()

if(EQEMU_BUILD_CLIENT_FILES)
	add_subdirectory(client_files)
endif()
