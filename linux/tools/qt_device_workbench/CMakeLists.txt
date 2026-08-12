add_executable(rcr_qt_device_workbench
  app/main.cpp
  controller/qt_metatypes.hpp
  controller/workbench_controller.cpp
  controller/workbench_controller.hpp
  ui/main_window.cpp
  ui/main_window.hpp
)

# AUTOMOC：CMake 自动跑 moc，给带 Q_OBJECT 的类生成 signal/slot 元数据。
# 不必手写 moc_*.cpp；缺这一行，Q_OBJECT 类会链接失败。
set_target_properties(rcr_qt_device_workbench PROPERTIES
  AUTOMOC ON
)

# 头文件按 app / controller / ui 分层；include 一律相对本工具根目录。
target_include_directories(rcr_qt_device_workbench PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rcr_qt_device_workbench PRIVATE
  rcr::rcr
  rcr::workbench
  Qt6::Core
  Qt6::Widgets
)

# Qt 的 signals/slots/emit 宏会污染 Runtime 头中的普通 C++ 标识符（例如
# CanIoLoop 构造参数 signals）。仅在本 target 禁用关键字宏，并显式使用 Q_* 形式。
target_compile_definitions(rcr_qt_device_workbench PRIVATE QT_NO_KEYWORDS)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rcr_qt_device_workbench PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  )
endif()

rcr_apply_sanitizers(rcr_qt_device_workbench)

install(TARGETS rcr_qt_device_workbench
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
