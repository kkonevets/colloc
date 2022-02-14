cmake_minimum_required(VERSION 3.14)
project(colloc)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1) # clangd
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_PREFIX_PATH ~/.local) # protobuf paths

find_package(absl REQUIRED)
find_package(Protobuf REQUIRED)

include_directories(../ /usr/local/include)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
link_directories(../zlib/ ../build/lib /usr/lib/x86_64-linux-gnu/)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS grams.proto)

SET(ZIPSRC
  ../../zlib/contrib/minizip/unzip.c
  ../../zlib/contrib/minizip/ioapi.c
  ../mail-search/tests/fant-client/zipfile.cpp
  )

file(GLOB SOURCES
  "src/*.cpp"
  )

add_library(colloc STATIC ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS} ${ZIPSRC})
target_link_libraries (colloc LINK_PUBLIC morphrus baalbek mtc
  moonycode absl::bad_optional_access absl::raw_hash_set absl::hash
  tinyxml2 ${Protobuf_LIBRARIES} capnp kj z)

file(GLOB TEST_SOURCES "src/tests.cpp")
add_executable(colloc_test ${TEST_SOURCES} ${ZIPSRC})
target_link_libraries(colloc_test gtest_main colloc baalbek z ${Protobuf_LIBRARIES})
add_test(NAME test COMMAND test ${PROTO_SRCS} ${PROTO_HDRS})

add_executable(gramcat src/gramcat.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(gramcat colloc ${Protobuf_LIBRARIES})

add_executable(read_total src/read_total.cpp ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(read_total colloc ${Protobuf_LIBRARIES})

add_executable(colloc_extract src/extract.cpp ${ZIPSRC})
target_link_libraries(colloc_extract colloc z ${Protobuf_LIBRARIES})
