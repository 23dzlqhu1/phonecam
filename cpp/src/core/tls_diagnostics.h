#pragma once

#include <QString>
#include <QStringList>

namespace phonecam {

inline QString tlsRuntimeMissingMessage()
{
    return QString::fromUtf8(
        "PhoneCam 缺少 HTTPS/TLS 运行组件，请重新安装 PhoneCam，或使用‘导入 Platform-Tools ZIP’。");
}

inline bool isTlsRuntimeAvailable(bool supportsSsl,
                                  const QStringList& availableBackends,
                                  const QString& activeBackend)
{
    return supportsSsl && !availableBackends.isEmpty() && !activeBackend.isEmpty();
}

} // namespace phonecam
