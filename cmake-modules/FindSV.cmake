# Locate Surround View library
#
# This sets the following variables:
# SV_FOUND
# SV_INCLUDE_DIRS
# SV_LIBRARIES
#

find_path(SV_INCLUDE_DIRS
    NAMES svlib.h
    PATHS
	/usr/local/include
	/usr/share/include
	/usr/include
)

find_library(SV_LIBRARIES
    NAMES sv
)

message(STATUS "SV_LIBRARIES: ${SV_LIBRARIES}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SV
    REQUIRED_VARS SV_INCLUDE_DIRS SV_LIBRARIES
)

