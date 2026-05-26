cmake_minimum_required(VERSION 3.14)

project(geodesic_backend)

#list(APPEND CMAKE_PREFIX_PATH "/home/pgh/miniforge3/envs/rapids-25.04/lib/python3.12/site-packages/pybind11/share/cmake/pybind11")

set(CMAKE_CXX_STANDARD 17)

# Python 경로를 자동으로 찾는 로직 추가
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
# 이제 이 아래에 기존 코드를 두면 됩니다.
find_package(pybind11 CONFIG REQUIRED)

find_package(Eigen3 REQUIRED)

find_package(OpenMP)

#include_directories(
#    ${EIGEN3_INCLUDE_DIR}
#    ${CMAKE_CURRENT_SOURCE_DIR}/spectra/include
#)

pybind11_add_module(
    geodesic_backend
    geodesic_backend.cpp
)

target_link_libraries(
    geodesic_backend
    PRIVATE Eigen3::Eigen
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(
        geodesic_backend
        PRIVATE OpenMP::OpenMP_CXX
    )
endif()