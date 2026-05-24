cmake_minimum_required(VERSION 3.14)

project(mds)

list(APPEND CMAKE_PREFIX_PATH "/home/pgh/miniforge3/envs/rapids-25.04/lib/python3.12/site-packages/pybind11/share/cmake/pybind11")

set(CMAKE_CXX_STANDARD 17)

find_package(pybind11 REQUIRED)

find_package(Eigen3 REQUIRED)

find_package(OpenMP)

include_directories(
    ${EIGEN3_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/spectra/include
)

pybind11_add_module(
    mds
    MDS.cpp
)

target_link_libraries(
    mds
    PRIVATE Eigen3::Eigen
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(
        mds
        PRIVATE OpenMP::OpenMP_CXX
    )
endif()