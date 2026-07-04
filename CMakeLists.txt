cmake_minimum_required(VERSION 3.20)
project(RootEngine VERSION 3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=x86-64-v2 -flto -DNDEBUG")

add_executable(Root
    src/main.cpp
    src/position.cpp
    src/attacks.cpp
    src/evaluation.cpp
    src/search.cpp
    src/uci.cpp
    src/tt.cpp
    src/book.cpp
    src/nnue.cpp
    src/datagen.cpp
)

if(MINGW)
    target_link_options(Root PRIVATE -static -static-libgcc -static-libstdc++)
endif()

find_package(Threads REQUIRED)
target_link_libraries(Root PRIVATE Threads::Threads)
