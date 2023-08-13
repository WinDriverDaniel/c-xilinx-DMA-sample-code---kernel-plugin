cmake_minimum_required(VERSION 3.0)

project(xdma_diag C)
include(../../../../include/wd.cmake)
include_directories(
    ../../shared
    ../../../../include
    )

set (SRCS
    xdma_diag.c
    xdma_lib.c
    xdma_lib.h
    xdma_diag_transfer.c
    xdma_diag_transfer.h)

add_executable(xdma_diag ${SRCS} ${SAMPLE_SHARED_SRCS})
target_link_libraries(xdma_diag ${WDAPI_LIB})
set_target_properties(xdma_diag PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${ARCH}/")
add_compile_definitions(HAS_INTS)

