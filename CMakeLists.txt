cmake_minimum_required(VERSION 3.10)
project(WisentBenchmarks)

# Configure CCache if available
find_program(CCACHE_EXECUTABLE ccache)
mark_as_advanced(CCACHE_EXECUTABLE)
if(CCACHE_EXECUTABLE)
  foreach(LANG C CXX)
    if(NOT DEFINED CMAKE_${LANG}_COMPILER_LAUNCHER AND NOT CMAKE_${LANG}_COMPILER MATCHES ".*/ccache")
      message(STATUS "Enabling ccache for ${LANG}")
      set(CMAKE_${LANG}_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE} CACHE STRING "")
    endif()
  endforeach()
endif()

############################## Custom build modes ###############################

set(CMAKE_CXX_FLAGS_SANITIZE "-fno-omit-frame-pointer -fsanitize=address,signed-integer-overflow,null,alignment,bounds,function,return,vla-bound -O0 -g"
  CACHE STRING
  "Flags used by the C++ compiler during Sanitize builds."
  FORCE)
set(CMAKE_C_FLAGS_SANITIZE "-fno-omit-frame-pointer -fsanitize=address,signed-integer-overflow,null,alignment,bounds,function,return,vla-bound -O0 -g"
  CACHE STRING
  "Flags used by the C compiler during Sanitize builds."
  FORCE)
set(CMAKE_EXE_LINKER_FLAGS_SANITIZE
  ${CMAKE_EXE_LINKER_FLAGS_DEBUG} CACHE STRING
  "Flags used for linking binaries during Sanitize builds."
  FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_SANITIZE
  ${CMAKE_SHARED_LINKER_FLAGS_DEBUG} CACHE STRING
  "Flags used by the shared libraries linker during Sanitize builds."
  FORCE)
mark_as_advanced(
  CMAKE_CXX_FLAGS_SANITIZE		  CMAKE_EXE_LINKER_FLAGS_SANITIZE
  CMAKE_C_FLAGS_SANITIZE		  CMAKE_SHARED_LINKER_FLAGS_SANITIZE
  )

set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING
  "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel Sanitize."
  FORCE)

############################### External Projects ###############################

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE) # make sure all dependent libraries are compiled with that option

include(ExternalProject)

ExternalProject_Add(catch2
	URL https://github.com/catchorg/Catch2/archive/v2.13.6.tar.gz
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads/${CMAKE_PROJECT_NAME}
	CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${WisentBenchmarks_BINARY_DIR}/deps -DCATCH_BUILD_TESTING=NO -DBUILD_TESTING=NO -DCATCH_ENABLE_WERROR=NO -DCATCH_INSTALL_DOCS=NO
               -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
  )

 ExternalProject_Add(
  cpp-httplib
  URL https://github.com/yhirose/cpp-httplib/archive/v0.12.6.tar.gz
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${WisentBenchmarks_BINARY_DIR}/deps/include/cpp-httplib
          COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/httplib.h ${WisentBenchmarks_BINARY_DIR}/deps/include/cpp-httplib/)

ExternalProject_Add(
  googlebenchmark
  URL "https://github.com/google/benchmark/archive/refs/tags/v1.8.0.zip"
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${WisentBenchmarks_BINARY_DIR}/deps -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON -DBENCHMARK_ENABLE_GTEST_TESTS=OFF $<IF:$<CONFIG:>,,-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
             -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
             -DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE} -DCMAKE_CXX_FLAGS_RELWITHDEBINFO=${CMAKE_CXX_FLAGS_RELWITHDEBINFO}
  BUILD_BYPRODUCTS ${WisentBenchmarks_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark${CMAKE_STATIC_LIBRARY_SUFFIX}
                   ${WisentBenchmarks_BINARY_DIR}/deps/lib/${CMAKE_SHARED_LIBRARY_PREFIX}benchmark_main${CMAKE_STATIC_LIBRARY_SUFFIX}
)

ExternalProject_Add(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG  tags/v1.1.0
  DOWNLOAD_DIR $ENV{HOME}/.cmake-downloads
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${WisentBenchmarks_BINARY_DIR}/deps $<IF:$<CONFIG:>,,-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
             -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
             -DCMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE} -DCMAKE_CXX_FLAGS_RELWITHDEBINFO=${CMAKE_CXX_FLAGS_RELWITHDEBINFO}
             -DRAPIDJSON_BUILD_DOC=OFF -DRAPIDJSON_BUILD_EXAMPLES=OFF -DRAPIDJSON_BUILD_TESTS=OFF -DRAPIDJSON_BUILD_CXX11=OFF -DRAPIDJSON_BUILD_CXX17=ON
)

####################### Other Options and Library Dependencies #######################

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(Boost REQUIRED) # for BOOST interprocess

include(FetchContent)

if(NOT TARGET rapidcsv)
    FetchContent_Declare(
      rapidcsv
      GIT_REPOSITORY "https://github.com/d99kris/rapidcsv.git"
      GIT_TAG        "v8.75"
    )
    FetchContent_MakeAvailable(rapidcsv)
endif()

if(NOT TARGET nlohmann_json)
    FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
    FetchContent_MakeAvailable(json)
endif()

if(NOT TARGET simdjson)
    FetchContent_Declare(
      simdjson
      GIT_REPOSITORY https://github.com/simdjson/simdjson.git
      GIT_TAG  tags/v3.2.0
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(simdjson)
endif()

################################ ITT module interface ################################

set(VTune_DIR "" CACHE PATH "Where to look for VTune installation")
find_path(ITT_NOTIFY_INCLUDE_DIR "ittnotify.h" HINTS ${VTune_DIR}/include)

add_library(ITTNotifySupport INTERFACE)
target_sources(ITTNotifySupport INTERFACE ${PROJECT_SOURCE_DIR}/Benchmarks/ITTNotifySupport.hpp)
target_include_directories(ITTNotifySupport INTERFACE ${PROJECT_SOURCE_DIR}/Benchmarks/)
if(ITT_NOTIFY_INCLUDE_DIR)
  message(VERBOSE "found itt notify header in ${ITT_NOTIFY_INCLUDE_DIR}")
  target_include_directories(ITTNotifySupport SYSTEM INTERFACE ${ITT_NOTIFY_INCLUDE_DIR})
  target_link_libraries(ITTNotifySupport INTERFACE ${ITT_NOTIFY_INCLUDE_DIR}/../lib64/libittnotify${CMAKE_STATIC_LIBRARY_SUFFIX})
  if(NOT WIN32)
    target_link_libraries(ITTNotifySupport dl)
  endif(NOT WIN32)
  if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ITTNotifySupport INTERFACE -DWITH_ITT_NOTIFY)
  else()
    target_compile_options(ITTNotifySupport INTERFACE -DINTEL_NO_ITTNOTIFY_API)
  endif()
endif(ITT_NOTIFY_INCLUDE_DIR)

#################################### Targets ####################################

set(WisentBenchmarksFiles Source/WisentBenchmarks.cpp)

set(WisentServerFiles Source/WisentServer.cpp)
set(WisentSerializerFiles Source/WisentSerializer.cpp Source/SharedMemorySegment.cpp)
set(BsonSerializerFiles Source/BsonSerializer.cpp)
set(WisentBenchmarkFiles Benchmarks/WisentBenchmarks.cpp)

# WisentSerializer Plugin
add_library(WisentSerializer SHARED ${WisentSerializerFiles})

# Wisent Server
add_executable(WisentServer ${WisentSerializerFiles} ${BsonSerializerFiles} ${WisentServerFiles})
add_dependencies(WisentServer cpp-httplib)

# Benchmarks
add_executable(Benchmarks ${WisentSerializerFiles} ${BsonSerializerFiles} ${WisentBenchmarkFiles})
add_dependencies(Benchmarks googlebenchmark)
target_link_libraries(Benchmarks PRIVATE ITTNotifySupport)
target_link_libraries(Benchmarks PRIVATE benchmark)
target_link_libraries(Benchmarks PRIVATE benchmark_main)
target_compile_options(Benchmarks PUBLIC -DBENCHMARK_STATIC_DEFINE)
add_dependencies(Benchmarks cpp-httplib)
add_dependencies(Benchmarks rapidjson)

list(APPEND AllExeTargets WisentServer Benchmarks)
list(APPEND AllTargets WisentServer Benchmarks WisentSerializer)

foreach(Target IN LISTS AllTargets)
    target_link_libraries(${Target} PRIVATE Threads::Threads)
    set_target_properties(${Target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${Target} PUBLIC "/Zc:__cplusplus")
        target_compile_options(${Target} PUBLIC "/EHsc")
    endif()
    set_target_properties(${Target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_include_directories(${Target} SYSTEM PUBLIC ${WisentBenchmarks_BINARY_DIR}/deps/include)
    target_link_directories(${Target} SYSTEM PUBLIC ${WisentBenchmarks_BINARY_DIR}/deps/lib)
    if(UNIX AND NOT APPLE)
        target_link_libraries(${Target} PRIVATE rt)
    endif()
    target_include_directories(${Target} SYSTEM PUBLIC ${Boost_INCLUDE_DIRS})
    target_link_libraries(${Target} PRIVATE rapidcsv)
    target_link_libraries(${Target} PRIVATE nlohmann_json)
    target_link_libraries(${Target} PRIVATE simdjson)
endforeach()

set_target_properties(WisentSerializer PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS WisentSerializer LIBRARY DESTINATION lib)
install(TARGETS WisentServer RUNTIME DESTINATION bin)
