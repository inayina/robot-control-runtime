#pragma once

#include "rcr/workbench/application/application_model.hpp"
#include "rcr/workbench/profile/mock_actuator_profile.hpp"
#include "rcr/workbench/services/test_runner.hpp"

#include <QMetaType>

// 最小 Qt 污染面：只告诉 moc/queued connection“这些纯 C++ DTO 可以按值拷贝”。
// DECLARE 放头文件，REGISTER 在 Controller 构造里做一次。两边都要，缺一则跨线程
// 传 snapshot/TestResult 会失败或运行期报类型未注册。
//
// 本头可以 include workbench DTO，但不要 include runtime_daemon.hpp。
// Core 库仍然没有 Qt 类型；不要把 DTO 改成 QObject / QVariantMap。
Q_DECLARE_METATYPE(rcr::workbench::RuntimeTelemetrySnapshot)
Q_DECLARE_METATYPE(rcr::workbench::TestResult)
Q_DECLARE_METATYPE(rcr::workbench::ActuatorSnapshot)
Q_DECLARE_METATYPE(rcr::workbench::ActuatorCommandReply)
