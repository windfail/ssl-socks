# Set the minimum version of CMake that can be used
# To find the cmake version run
# $ cmake --version
cmake_minimum_required(VERSION 3.5)
#set(CMAKE_C_COMPILER /usr/lib/llvm/11/bin/clang)
#set(CMAKE_CXX_COMPILER /usr/lib/llvm/11/bin/clang++)
#set(CMAKE_CXX_FLAGS "--std=c++2a -I/usr/include/c++/v1/" )
# Set the project name
project (netsock)
SET(CMAKE_BUILD_TYPE "Release")
#SET(CMAKE_BUILD_TYPE "Debug")
# Create a sources variable with a link to all cpp files to compile
set(SOURCES
  src/util.c
    src/ssl_relay.cpp
    src/raw_relay.cpp
    src/relay_server.cpp
    #    src/server.cpp
    src/gfwlist.cpp
    src/main.cpp
)
set(CMAKE_CXX_STANDARD 17)

# Add an executable with the above sources
add_executable(${PROJECT_NAME} ${SOURCES})
#target_link_options(${PROJECT_NAME}
#  PRIVATE "-lboost_system"
#  PRIVATE "-pthread")
include(FindPkgConfig)
pkg_check_modules(libcrypto REQUIRED )
pkg_check_modules(libssl REQUIRED )
pkg_check_modules(pthread REQUIRED )

# find a boost install with the libraries filesystem and system
find_package(Boost 1.71 REQUIRED COMPONENTS system log log_setup coroutine)

add_definitions(-DBOOST_LOG_DYN_LINK)
# check if boost was found
if(Boost_FOUND)
    message ("boost found")
else()
    message (FATAL_ERROR "Cannot find Boost")
endif()

target_link_libraries( ${PROJECT_NAME}
    PRIVATE
	Boost::log_setup
	Boost::log
	Boost::system
	Boost::coroutine
	pthread
	ssl
	crypto
)

# Set the directories that should be included in the build command for this target
# when running g++ these will be included as -I/directory/path/
target_include_directories(${PROJECT_NAME}
    PRIVATE
	${PROJECT_SOURCE_DIR}/include
)
