cmake_minimum_required(VERSION 3.20)
project(adrian-test)
list(APPEND adrian-test-src
	src/doctest.h
	src/main.cpp
)
add_executable(adrian-test ${adrian-test-src})
if (NOT TARGET adrian::adrian)
	set (ADRIAN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	add_subdirectory(../.. adrian)
endif()
target_link_libraries(adrian-test adrian::adrian)
