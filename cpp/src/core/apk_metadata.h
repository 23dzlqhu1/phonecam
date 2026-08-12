#pragma once

#include <QString>

namespace phonecam {

struct ApkMetadata {
    bool valid = false;
    QString packageName;
    qint64 versionCode = -1;
    QString versionName;
    QString error;
};

// Reads AndroidManifest.xml from the APK and parses its binary XML manifest.
// It never trusts the APK filename for identity or version verification.
ApkMetadata readApkMetadata(const QString& apkPath);

} // namespace phonecam
