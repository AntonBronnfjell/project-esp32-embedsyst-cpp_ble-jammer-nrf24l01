cmake_minimum_required(VERSION 3.16)

# Reuse the RF24 component that lives in the repo root's components/ dir
# (../../components relative to this standalone sub-project).
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rf-interference-detector)
