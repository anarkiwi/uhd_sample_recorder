cmake_minimum_required(VERSION 3.16)

project(uhd_sample_recorder)
set(CMAKE_BUILD_TYPE Release)
set(SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CMAKE_CXX_STANDARD 20)
set(ENABLE_STATIC_LIBS ON)
include(FindPkgConfig)
include(CTest)

find_package(UHD REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(libsigmf REQUIRED)

find_package(
  Boost ${Boost_Version}
  COMPONENTS filesystem iostreams thread unit_test_framework program_options
  REQUIRED)

add_library(sample_writer sample_writer.cpp)

add_library(sample_pipeline sample_pipeline.cpp)
target_link_libraries(sample_pipeline ${Boost_LIBRARIES})

add_executable(sample_pipeline_test sample_pipeline_test.cpp)
target_link_libraries(sample_pipeline_test sample_pipeline sample_writer
                      ${Boost_LIBRARIES} libsigmf::libsigmf)

add_test(NAME sample_pipeline_test COMMAND sample_pipeline_test)

add_executable(uhd_sample_recorder uhd_sample_recorder.cpp)
target_link_libraries(
  uhd_sample_recorder
  sample_pipeline
  sample_writer
  ${Boost_LIBRARIES}
  ${UHD_LIBRARIES}
  ${UHD_STATIC_LIB_DEPS}
  libsigmf::libsigmf)
