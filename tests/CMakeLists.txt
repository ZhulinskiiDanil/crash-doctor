cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The parts of the mod that don't need Geode or Windows, tested on their own
project(crash-doctor-tests CXX)

enable_testing()

add_executable(decode-tests DecodeTests.cpp ../src/Decode.cpp)
add_test(NAME decode COMMAND decode-tests)

add_executable(analysis-tests AnalysisTests.cpp ../src/Analysis.cpp ../src/Decode.cpp)
add_test(NAME analysis COMMAND analysis-tests)

add_executable(crumbs-tests CrumbsTests.cpp ../src/Crumbs.cpp)
add_test(NAME crumbs COMMAND crumbs-tests)

add_executable(report-tests ReportTests.cpp ../src/Report.cpp ../src/Analysis.cpp ../src/Decode.cpp ../src/Crumbs.cpp)
add_test(NAME report COMMAND report-tests)

# The reports' Russian texts are UTF-8 in the sources
if (MSVC)
    add_compile_options(/utf-8)
endif()
