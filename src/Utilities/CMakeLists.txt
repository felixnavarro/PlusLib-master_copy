SET(Utilities_INCLUDE_DIRS "" CACHE INTERNAL "" FORCE) 

# These are third-party libraries. Don't perform their tests as part of PlusLib tests.
SET(BUILD_TESTING OFF)

# Generic utilities
SUBDIRS(
  Ransac
  xio
  )

# Hardware-dependent utilities
IF(PLUS_USE_MICRONTRACKER)
  SUBDIRS(MicronTrackerInterface)
ENDIF()

IF(PLUS_USE_MMF_VIDEO)
  SUBDIRS(MfVideoCaptureLibrary)
ENDIF()