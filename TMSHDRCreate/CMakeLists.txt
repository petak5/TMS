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

# ONNX Runtime (installed via Homebrew or system package)
find_library(ONNXRUNTIME_LIB onnxruntime
  HINTS /opt/homebrew/lib /usr/local/lib
)
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
  HINTS /opt/homebrew/include/onnxruntime /usr/local/include/onnxruntime
)
if(NOT ONNXRUNTIME_LIB OR NOT ONNXRUNTIME_INCLUDE_DIR)
  message(FATAL_ERROR "ONNX Runtime not found. Install with: brew install onnxruntime")
endif()

INCLUDE_DIRECTORIES(
../tmolib/
${OpenCV_INCLUDE_DIRS}
${ONNXRUNTIME_INCLUDE_DIR}
.
)

#Set up libraries to link with all found packages
LINK_LIBRARIES (
dl
tmo
pthread
${CMAKE_DL_LIBS}
${OpenCV_LIBS}
${ONNXRUNTIME_LIB}
)

SET(TMSHDRCREATELIB_SOURCES
 TMSHDRCreate.cpp
 TMSHDRMergeDIS.cpp
 TMSHDRMergeHDRPlus.cpp
 TMSHDRMergeSAFNet.cpp
 SrgbTransform.cpp
)

SET(TMSHDRCREATE_HEADERS
 TMSHDRCreate.h
 TMSHDRMergeDIS.h
 TMSHDRMergeHDRPlus.h
 TMSHDRMergeSAFNet.h
 TMSHDRMergeUtil.h
 SrgbTransform.hpp
)

ADD_LIBRARY(TMSHDRCreateLib STATIC ${TMSHDRCREATELIB_SOURCES})
TARGET_INCLUDE_DIRECTORIES(TMSHDRCreateLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tmolib
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIR}
)
TARGET_LINK_LIBRARIES(TMSHDRCreateLib
    tmo
    ${OpenCV_LIBS}
    ${ONNXRUNTIME_LIB}
)

SET(EXE_NAME "tmshdrcreate${TAG_VERSION}" )
ADD_EXECUTABLE(${EXE_NAME} main.cpp)
TARGET_LINK_LIBRARIES(${EXE_NAME} TMSHDRCreateLib dl pthread)

add_custom_command( TARGET ${EXE_NAME} POST_BUILD
        COMMAND true
        )
