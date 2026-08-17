cmake_minimum_required(VERSION 3.20)
project(RoadCraftCameraNorthFix VERSION 0.2.0 LANGUAGES CXX ASM RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/RoadCraftWrappedYawDeltaStub.bin
  COMMAND ${CMAKE_ASM_COMPILER} -c ../WrappedYawDeltaStub.S -o WrappedYawDeltaStub.o
  COMMAND ${CMAKE_OBJCOPY} -O binary --only-section=.text WrappedYawDeltaStub.o RoadCraftWrappedYawDeltaStub.bin
  DEPENDS WrappedYawDeltaStub.S
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  VERBATIM)
add_custom_target(RoadCraftWrappedYawDeltaStub ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/RoadCraftWrappedYawDeltaStub.bin)

set(FIXER_RESOURCE_FILE ${CMAKE_CURRENT_BINARY_DIR}/RoadCraftCameraFixUI.rc)
file(WRITE ${FIXER_RESOURCE_FILE} "101 RCDATA \"RoadCraftWrappedYawDeltaStub.bin\"\n")
set_source_files_properties(${FIXER_RESOURCE_FILE} PROPERTIES
  GENERATED TRUE
  OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/RoadCraftWrappedYawDeltaStub.bin)

add_executable(RoadCraftCameraFixUI WIN32 GuiHost.cpp ${FIXER_RESOURCE_FILE})
add_dependencies(RoadCraftCameraFixUI RoadCraftWrappedYawDeltaStub)
target_compile_definitions(RoadCraftCameraFixUI PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(RoadCraftCameraFixUI PRIVATE comctl32)

if(MINGW)
  target_link_options(RoadCraftCameraFixUI PRIVATE
    -municode -mwindows -static -static-libgcc -static-libstdc++)
endif()
