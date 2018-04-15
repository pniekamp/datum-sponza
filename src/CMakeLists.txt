#
# datum sponza
#

set(CMAKE_CXX_STANDARD 14)

if(UNIX OR MINGW)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wfloat-conversion -Wno-unused-parameter -Wno-missing-field-initializers")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wfloat-conversion -Wno-unused-parameter -Wno-missing-field-initializers")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffast-math")
endif(UNIX OR MINGW)

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)

set(SRCS ${SRCS} datumsponza.h datumsponza.cpp platform.h platform.cpp)

if(WIN32)
  set(SRCS ${SRCS} datumsponza-win32.cpp)
endif(WIN32)

if(UNIX)
  set(SRCS ${SRCS} datumsponza-xcb.cpp)
endif(UNIX)

add_executable(datumsponza ${SRCS})

target_link_libraries(datumsponza leap datum vulkan)

if(UNIX)
  target_link_libraries(datumsponza ${XCB_LIBRARIES})
endif(UNIX)

if(WIN32)
  target_link_libraries(datumsponza mingw32)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
endif(WIN32)

#
# envmapgen
#

include_directories(${DATUM_TOOLS})

add_executable(envmapgen envmapgen.cpp platform.h platform.cpp ${DATUM_TOOLS}/assetpacker.cpp ${DATUM_TOOLS}/bc3.cpp ${DATUM_TOOLS}/hdr.cpp ${DATUM_TOOLS}/ibl.cpp)

target_link_libraries(envmapgen leap datum vulkan)

#
# install
#

INSTALL(TARGETS datumsponza DESTINATION bin)

