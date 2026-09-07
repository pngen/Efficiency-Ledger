cmake_minimum_required(VERSION 3.20)
project(EfficiencyLedger VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EL_BUILD_TESTS "Build tests" ON)
option(EL_BUILD_EXAMPLES "Build examples" ON)
option(EL_BUILD_BENCHMARKS "Build benchmarks" ON)
option(EL_BUILD_TOOLS "Build CLI tools" ON)
option(EL_BUILD_CUDA_PROOF "Build the real CUDA proof" ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Core library
add_library(efficiency_ledger
  src/crc32c.cpp
  src/digest.cpp
  src/ledger.cpp
  src/protocol.cpp
  src/version.cpp
)
add_library(EfficiencyLedger::EfficiencyLedger ALIAS efficiency_ledger)

target_include_directories(efficiency_ledger PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(efficiency_ledger PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(efficiency_ledger PRIVATE /W4 /WX /permissive- /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX)
  target_compile_options(efficiency_ledger PUBLIC /utf-8)
else()
  target_compile_options(efficiency_ledger PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

find_package(Threads REQUIRED)
target_link_libraries(efficiency_ledger PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(efficiency_ledger PUBLIC ws2_32)
endif()

# CLI + distributed role executables
if(EL_BUILD_TOOLS)
  add_executable(el tools/el_cli.cpp)
  target_link_libraries(el PRIVATE efficiency_ledger)
  add_executable(el_coordinator tools/el_coordinator.cpp)
  target_link_libraries(el_coordinator PRIVATE efficiency_ledger)
  add_executable(el_worker tools/el_worker.cpp)
  target_link_libraries(el_worker PRIVATE efficiency_ledger)
  foreach(tgt el el_coordinator el_worker)
    if(MSVC)
      target_compile_options(${tgt} PRIVATE /W4 /WX /utf-8)
    else()
      target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
  endforeach()
endif()

# Examples
if(EL_BUILD_EXAMPLES)
  file(GLOB EL_EXAMPLE_SOURCES CONFIGURE_DEPENDS examples/*.cpp)
  foreach(ex_src IN LISTS EL_EXAMPLE_SOURCES)
    get_filename_component(ex_name ${ex_src} NAME_WE)
    add_executable(example_${ex_name} ${ex_src})
    target_link_libraries(example_${ex_name} PRIVATE efficiency_ledger)
    target_compile_features(example_${ex_name} PUBLIC cxx_std_20)
    if(MSVC)
      target_compile_options(example_${ex_name} PRIVATE /W4 /WX /utf-8)
    else()
      target_compile_options(example_${ex_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
  endforeach()
endif()

# Benchmarks
if(EL_BUILD_BENCHMARKS)
  add_executable(el_bench benchmarks/el_bench.cpp)
  target_link_libraries(el_bench PRIVATE efficiency_ledger)
  if(MSVC)
    target_compile_options(el_bench PRIVATE /W4 /WX /utf-8)
  else()
    target_compile_options(el_bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endif()

# Tests
if(EL_BUILD_TESTS)
  enable_testing()
  file(GLOB EL_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  foreach(test_src IN LISTS EL_TEST_SOURCES)
    get_filename_component(test_name ${test_src} NAME_WE)
    string(REGEX REPLACE "^test_" "" test_base ${test_name})
    add_executable(test_${test_base} ${test_src})
    target_link_libraries(test_${test_base} PRIVATE efficiency_ledger)
    target_compile_features(test_${test_base} PUBLIC cxx_std_20)
    if(MSVC)
      target_compile_options(test_${test_base} PRIVATE /W4 /WX /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX)
    else()
      target_compile_options(test_${test_base} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${test_base} COMMAND test_${test_base})
  endforeach()
endif()

# CUDA proof
if(EL_BUILD_CUDA_PROOF)
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    # Build the real CUDA proof by invoking nvcc directly. CMake's built-in CUDA
    # language integration injects a malformed "-Fd.../,-FS" host-compiler flag on
    # MSVC/Ninja (the known nvcc PDB quoting bug), so we compile the .cu ourselves.
    if(CUDAToolkit_NVCC_EXECUTABLE)
      set(EL_CU_OBJ "${CMAKE_CURRENT_BINARY_DIR}/el_cuda_proof.obj")
      set(EL_CU_ARCH "120")
      add_custom_command(
        OUTPUT "${EL_CU_OBJ}"
        COMMAND "${CUDAToolkit_NVCC_EXECUTABLE}" -ccbin "${CMAKE_CXX_COMPILER}"
                -std=c++20 -O2 --generate-code=arch=compute_${EL_CU_ARCH},code=sm_${EL_CU_ARCH}
                -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
                -I "${CUDAToolkit_INCLUDE_DIRS}"
                -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS
                -Xcompiler=/W4,/MD,/utf-8
                -c "${CMAKE_CURRENT_SOURCE_DIR}/cu/el_cuda_proof.cu" -o "${EL_CU_OBJ}"
        DEPENDS cu/el_cuda_proof.cu
        VERBATIM)
      add_executable(el_cuda_proof "${EL_CU_OBJ}")
      target_link_libraries(el_cuda_proof PRIVATE efficiency_ledger CUDA::cudart)
    endif()
  endif()
endif()

# Install / export
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS efficiency_ledger
  EXPORT EfficiencyLedgerTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(EL_BUILD_TOOLS)
  install(TARGETS el RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/efficiency_ledger DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT EfficiencyLedgerTargets
  FILE EfficiencyLedgerTargets.cmake
  NAMESPACE EfficiencyLedger::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EfficiencyLedger
)

configure_package_config_file(
  cmake/EfficiencyLedgerConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/EfficiencyLedgerConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EfficiencyLedger
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/EfficiencyLedgerConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/EfficiencyLedgerConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/EfficiencyLedgerConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EfficiencyLedger
)
