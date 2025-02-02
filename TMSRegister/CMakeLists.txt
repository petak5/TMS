CMAKE_MINIMUM_REQUIRED(VERSION 2.4.8)
SET(CMAKE_COLOR_MAKEFILE ON)
SET(CMAKE_VERBOSE_MAKEFILE ON)
SET(CMAKE_INCLUDE_CURRENT_DIR TRUE)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

INCLUDE_DIRECTORIES(
../tmolib/
.
)

#Set up libraries to link with all found packages
LINK_LIBRARIES (
dl
tmo
pthread
${CMAKE_DL_LIBS}
)

SET(TMSREGISTER_SOURCES
 TMSRegister.cpp
 TMSAlignHomographyMethod.cpp
 TMSAlignOpticalFlowMethod.cpp
 TMSAlignMTBMethod.cpp
 main.cpp
)


SET(TMSREGISTER_HEADERS
 TMSRegister.h
 TMSAlignHomographyMethod.h
 TMSAlignOpticalFlowMethod.h
 TMSAlignMTBMethod.h
)




SET(EXE_NAME "tmsregister${TAG_VERSION}" )
ADD_EXECUTABLE(${EXE_NAME}
  ${TMSREGISTER_SOURCES}
)

add_custom_command( TARGET ${EXE_NAME} POST_BUILD
        COMMAND cp -r tmsregister ../
        )

