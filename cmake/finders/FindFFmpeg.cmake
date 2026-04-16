# FindFFmpeg.cmake — locates FFmpeg libs from the obs-deps prebuilt package.
# Creates imported targets: FFmpeg::avcodec, FFmpeg::avformat,
#                           FFmpeg::avutil,  FFmpeg::swresample

include(FindPackageHandleStandardArgs)

# The obs-deps bootstrap adds the extracted deps dir to CMAKE_PREFIX_PATH.
# Look there first, then fall back to system paths.
find_path(
  FFMPEG_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  PATH_SUFFIXES include
)

set(_ffmpeg_components avcodec avformat avutil swresample swscale avfilter avdevice)

foreach(_comp IN LISTS _ffmpeg_components)
  find_library(
    FFMPEG_${_comp}_LIBRARY
    NAMES ${_comp}
    PATH_SUFFIXES lib
  )
endforeach()

# Validate requested components
foreach(_comp IN LISTS FFmpeg_FIND_COMPONENTS)
  if(FFMPEG_${_comp}_LIBRARY AND FFMPEG_INCLUDE_DIR)
    set(FFmpeg_${_comp}_FOUND TRUE)
  else()
    set(FFmpeg_${_comp}_FOUND FALSE)
    if(FFmpeg_FIND_REQUIRED_${_comp})
      message(SEND_ERROR "FindFFmpeg: required component '${_comp}' not found.\n"
                         "  Include dir: ${FFMPEG_INCLUDE_DIR}\n"
                         "  Lib: ${FFMPEG_${_comp}_LIBRARY}")
    endif()
  endif()
endforeach()

find_package_handle_standard_args(
  FFmpeg
  REQUIRED_VARS FFMPEG_INCLUDE_DIR
  HANDLE_COMPONENTS
)

# Create imported targets for found components
if(FFmpeg_FOUND)
  foreach(_comp IN LISTS _ffmpeg_components)
    if(FFMPEG_${_comp}_LIBRARY AND NOT TARGET FFmpeg::${_comp})
      add_library(FFmpeg::${_comp} UNKNOWN IMPORTED)
      set_target_properties(
        FFmpeg::${_comp}
        PROPERTIES
          IMPORTED_LOCATION "${FFMPEG_${_comp}_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}")
    endif()
  endforeach()
endif()
