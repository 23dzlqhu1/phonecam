#pragma once

// ═══════════════════════════════════════════════════════════════
// adb_locator.h — PhoneCam 共享 ADB 定位 / 验证小工具
//
// 主程序 (phonecam) 与 USB 连接设置工具 (phonecam-adb-setup)
// 共用同一套 ADB 发现规则，避免两边各写一份 findAdb()。
// ═══════════════════════════════════════════════════════════════

#include <QString>
#include <QStringList>

namespace phonecam {

// ADB 验证结果：valid=true 表示该 exe 能真正执行 adb version 并输出合法内容
struct AdbValidation {
    bool valid = false;   // 是否通过验证
    QString versionText;  // 版本行文本（如 "Android Debug Bridge version 1.0.41"）
    QString error;        // 验证失败时的原因（无法启动 / 执行超时 / exit code=N / 输出不合法）
};

// 纯静态工具类：负责"找 ADB"与"验证 ADB"
class AdbLocator {
public:
    AdbLocator() = delete;  // 静态工具类，不允许实例化

    // 按固定优先级返回所有候选 adb.exe 路径（包括不存在的路径，用于诊断），已去重。
    // 搜索顺序见 adb_locator.cpp 顶部注释，与旧 ConnectionManager::findAdb() 的来源一致。
    static QStringList locateAdbCandidates();

    // 返回第一个"存在且验证通过"的 ADB 路径；找不到返回空字符串。
    // 验证成功的路径会在进程内缓存，调用方在"用户手动选择了新 adb.exe"后应调用 clearCache()。
    static QString resolveAdb();

    // 真正执行 "<adbPath> version"（失败再试 "--version"）做验证。
    // 此函数是同步阻塞的，单次最长约 5 秒；调用方应放在非 GUI 线程或用户操作时调用。
    static AdbValidation validateAdb(const QString& adbPath);

    // 清除 resolveAdb() 的进程内缓存（用户手动选择了新的 adb.exe 后调用）
    static void clearCache();

private:
    static QString s_resolvedPath;  // 最近一次验证成功的 ADB 路径缓存（空 = 未缓存）
};

} // namespace phonecam
