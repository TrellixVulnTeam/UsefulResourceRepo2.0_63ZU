# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Copied from: eden/CMake/FindSqlite3.cmake

include(FindPackageHandleStandardArgs)

find_path(SQLITE3_INCLUDE_DIR NAMES sqlite3.h)
find_library(SQLITE3_LIBRARY NAMES sqlite3)
find_package_handle_standard_args(
  SQLITE3
  DEFAULT_MSG
  SQLITE3_INCLUDE_DIR
  SQLITE3_LIBRARY
)
mark_as_advanced(SQLITE3_INCLUDE_DIR SQLITE3_LIBRARY)
