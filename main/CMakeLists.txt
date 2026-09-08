

idf_component_register(SRCS  "main.c" "helpers/time_set_up.c" "services/request_counter.c" "helpers/wifi_config.c" "http_server_handling.c" 
"helpers/fs_operations.c" "services/zlib_compressor.c" "api/requests_quantity/requests_quantity.c" "api/error_handlers/error_handlers.c"
 "helpers/led.c" "services/sys_information.c" "api/sys_info/sys_info.c" "api/led/led.c" "helpers/touch_events_helper.c"
"web-modules/main-page/main_page.c" "web-modules/main-page/main_page_template.c" "api/performance_testing/performance_testing.c" 
"web-modules/lib/template_helpers.c" "api/static_files/static_files.c" "api/lib/api_lib.c" "api/image_upload/image_upload.c" 
"web-modules/temp-preview/temp_preview_page.c" "web-modules/temp-preview/temp_preview_template.c" "web-modules/temp-preview/temp_download_file.c"

                    INCLUDE_DIRS ".") 


# Requires the esp_littlefs component added to your project
# set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../front/static/*")
# if(EXISTS ${WEB_SRC_DIR})
#     littlefs_create_partition_image(storage ${WEB_SRC_DIR} FLASH_IN_PROJECT)
# else()
#     message(FATAL_ERROR "'${WEB_SRC_DIR}' doesn't exist. Please run 'pnpm build' under '${WEB_SRC_DIR}'")
# endif()

