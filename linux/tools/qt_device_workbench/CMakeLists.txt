add_executable(rcr_qt_device_workbench
  main.cpp
  main_window.cpp
  main_window.hpp
  qt_metatypes.hpp
  workbench_controller.cpp
  workbench_controller.hpp
)

set_target_properties(rcr_qt_device_workbench PROPERTIES
  AUTOMOC ON
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
