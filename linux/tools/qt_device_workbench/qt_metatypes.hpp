#pragma once

#include "rcr/workbench/application_model.hpp"
#include "rcr/workbench/test_runner.hpp"

#include <QMetaType>

// 这些声明只属于 Qt 适配层。Core DTO 保持纯 C++，避免 Runtime 反向依赖 Qt。
Q_DECLARE_METATYPE(rcr::workbench::RuntimeTelemetrySnapshot)
Q_DECLARE_METATYPE(rcr::workbench::TestResult)
