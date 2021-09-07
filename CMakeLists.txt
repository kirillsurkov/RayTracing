cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(BvhTest VERSION 1.0)

add_executable(BvhTest main.cpp bvh.cpp bvh.hpp)

find_package(Threads REQUIRED)
target_link_libraries(BvhTest Threads::Threads)

set(OpenGL_GL_PREFERENCE "LEGACY")
find_package(OpenGL REQUIRED)
include_directories(BvhTest ${OPENGL_INCLUDE_DIRS})
target_link_libraries(BvhTest ${OPENGL_LIBRARIES})

find_package(SDL2 REQUIRED)
include_directories(BvhTest ${SDL2_INCLUDE_DIRS})
target_link_libraries(BvhTest ${SDL2_LIBRARIES})

find_package(GLEW REQUIRED)
include_directories(BvhTest ${GLEW_INCLUDE_DIRS})
target_link_libraries(BvhTest ${GLEW_LIBRARIES})

find_package(Assimp REQUIRED)
include_directories(BvhTest ${ASSIMP_INCLUDE_DIRS})
target_link_libraries(BvhTest ${ASSIMP_LIBRARIES})

target_include_directories(BvhTest PUBLIC ./bvh/include)
