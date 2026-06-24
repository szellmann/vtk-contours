cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

project(vtk-contours)

# keep on top:
find_package(viskores)

find_package(VTK COMPONENTS 
  CommonColor
  CommonCore
  CommonDataModel
  FiltersCore
  FiltersGeometry
  FiltersModeling
  InteractionStyle
  RenderingContextOpenGL2
  RenderingCore
  RenderingFreeType
  RenderingGL2PSOpenGL2
  RenderingOpenGL2
  IOLegacy
  AcceleratorsVTKmCore
  AcceleratorsVTKmDataModel
  FiltersSources
)

if (NOT VTK_FOUND)
  message(FATAL_ERROR "Unable to find the VTK build folder.")
endif()

# Prevent a "command line is too long" failure in Windows.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE "ON" CACHE BOOL "Force Ninja to use response files.")
add_executable(${PROJECT_NAME} MACOSX_BUNDLE main.cpp )
target_link_libraries(${PROJECT_NAME} PRIVATE ${VTK_LIBRARIES} viskores::filter_scalar_topology)
# vtk_module_autoinit is needed
vtk_module_autoinit(
  TARGETS ${PROJECT_NAME}
  MODULES ${VTK_LIBRARIES}
)
