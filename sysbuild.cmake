# without netcore we add the SD config to the main app
if(NOT DEFINED SB_CONFIG_NETCORE_IMAGE_NAME OR (SB_CONFIG_NETCORE_IMAGE_NAME STREQUAL ""))
    add_overlay_config( ${DEFAULT_IMAGE} ${APP_DIR}/sysbuild/ipc_radio.conf )
endif()
