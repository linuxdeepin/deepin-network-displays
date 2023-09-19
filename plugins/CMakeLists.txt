cmake_minimum_required(VERSION 3.7)
# Install settings
set(CMAKE_INSTALL_PREFIX "/usr")

project(wirelesscasting-plugins)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# Find the library
find_package(PkgConfig REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(DtkWidget REQUIRED)
find_package(DtkCore REQUIRED)
find_package(DdeControlCenter REQUIRED)

set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common")
file(GLOB COMMON_FILE "${COMMON_DIR}/*.cpp")

#--------------------------dcc-plugin---------------------------
message("Complie dcc plugin")
set(Dcc_Plugin_Name dcc-wirelesscasting-plugin)

set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dcc-wirelesscasting-plugin/src")

# 使用 file() 命令和 GLOB 参数获取目录下的所有 .cpp 文件
file(GLOB DCC_PLUGIN_SOURCE_FILES "${SOURCE_DIR}/*.cpp")

# 将获取到的源文件添加到目标可执行文件中
add_library(${Dcc_Plugin_Name} SHARED
${DCC_PLUGIN_SOURCE_FILES}
${COMMON_FILE}
dcc-wirelesscasting-plugin/images/resources.qrc
dcc-wirelesscasting-plugin/translations/translations.qrc
)

target_include_directories(${Dcc_Plugin_Name} PUBLIC
    ${DtkWidget_INCLUDE_DIRS}
    ${DtkCore_INCLUDE_DIRS}
    ${OBJECT_BINARY_DIR}
    ${DdeControlCenter_INCLUDE_DIR}
    ${COMMON_DIR}
    )
target_link_libraries(dcc-wirelesscasting-plugin
    ${DtkWidget_LIBRARIES}
    ${DtkCore_LIBRARIES}
    ${Qt5Widgets_LIBRARIES}
    ${DdeControlCenter_LIBRARIES}
)

# 设置执行 make install 时哪个目标应该被 install 到哪个位置
install(TARGETS ${Dcc_Plugin_Name} LIBRARY DESTINATION lib/dde-control-center/modules)
