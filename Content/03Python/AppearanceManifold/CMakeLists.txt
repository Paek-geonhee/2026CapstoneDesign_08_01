cmake_minimum_required(VERSION 3.14)

project(geodesic_backend)

set(CMAKE_CXX_STANDARD 17)

find_package(pybind11 REQUIRED)

find_package(Eigen3 REQUIRED)

find_package(OpenMP)

pybind11_add_module(
    geodesic_backend
    Geodesic_Backend.cpp
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