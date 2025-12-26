cmake_minimum_required(VERSION 3.20)
project(adrian-test)
list(APPEND adrian-test-src
	src/doctest.h
	src/main.cpp
)
add_executable(adrian-test ${adrian-test-src})
target_link_libraries(adrian-test adrian::adrian)
add_test(NAME adrian-test COMMAND adrian-test)
