CMAKE_MINIMUM_REQUIRED(VERSION 2.4.8)
SET(CMAKE_COLOR_MAKEFILE ON)
SET(CMAKE_VERBOSE_MAKEFILE ON)
SET(CMAKE_INCLUDE_CURRENT_DIR TRUE)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

FIND_PACKAGE(OpenCV REQUIRED)
IF(OpenCV_VERSION VERSION_LESS "2.4.9")
  message(FATAL_ERROR "ERROR: Can't find OpenCV 2.4.9+")
ENDIF()

INCLUDE_DIRECTORIES(
../tmolib/
${OpenCV_INCLUDE_DIRS}
.
)

#Set up libraries to link with all found packages
LINK_LIBRARIES (
dl
tmo
pthread
${CMAKE_DL_LIBS}
${OpenCV_LIBS}
)

SET(TMSHDRCREATE_SOURCES
 TMSHDRCreate.cpp
 TMSHDRMergeDIS.cpp
 TMSHDRMergeHDRPlus.cpp
 SrgbTransform.cpp
 main.cpp
)


SET(TMSHDRCREATE_HEADERS
 TMSHDRCreate.h
 TMSHDRMergeDIS.h
 TMSHDRMergeHDRPlus.h
 TMSHDRMergeUtil.h
 SrgbTransform.hpp
)




SET(EXE_NAME "tmshdrcreate${TAG_VERSION}" )
ADD_EXECUTABLE(${EXE_NAME}
  ${TMSHDRCREATE_SOURCES}
)

add_custom_command( TARGET ${EXE_NAME} POST_BUILD
        COMMAND true
        )
