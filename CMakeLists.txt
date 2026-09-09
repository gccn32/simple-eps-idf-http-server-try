# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(simple-http-server)


# set(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/front-end/dist")
# set(DESTINATION_DIR "${CMAKE_BINARY_DIR}/static")

# # Command to copy the entire directory structure
# add_custom_command(
#     TARGET app PRE_BUILD
#     COMMAND ${CMAKE_COMMAND} -E rm -rf "${DESTINATION_DIR}"
#     COMMAND ${CMAKE_COMMAND} -E copy_directory "${SOURCE_DIR}" "${DESTINATION_DIR}"
#     COMMENT "Copying folder to ESP32-S3 directory"
#     VERBATIM
# )


# find_program(NODE_EXECUTABLE NAMES node nodejs REQUIRED)

# set(GENERATED_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/main/web-modules/main-page/main_page_template.c")

# add_custom_command(
#     TARGET app
#     PRE_BUILD
#     COMMAND ${NODE_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/template-generator/dist/index.js"
#     COMMENT "Running Node.js script to generate ESP32-S3 templates..."
#     VERBATIM
# )