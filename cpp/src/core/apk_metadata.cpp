#include "core/apk_metadata.h"

#include <QByteArray>
#include <QFileInfo>
#include <QProcess>

#include <limits>

namespace phonecam {
namespace {

constexpr quint16 kXmlType = 0x0003;
constexpr quint16 kStringPoolType = 0x0001;
constexpr quint16 kStartElementType = 0x0102;
constexpr quint32 kUtf8Flag = 0x00000100;
constexpr quint32 kNoIndex = 0xffffffffU;
constexpr quint8 kTypeString = 0x03;
constexpr quint8 kTypeIntDec = 0x10;
constexpr quint8 kTypeIntHex = 0x11;

bool inRange(const QByteArray& data, qsizetype offset, qsizetype length)
{
    return offset >= 0 && length >= 0 && offset <= data.size() - length;
}

quint16 read16(const QByteArray& data, qsizetype offset, bool* ok)
{
    if (!inRange(data, offset, 2)) {
        *ok = false;
        return 0;
    }
    const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

quint32 read32(const QByteArray& data, qsizetype offset, bool* ok)
{
    if (!inRange(data, offset, 4)) {
        *ok = false;
        return 0;
    }
    const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16)
        | (quint32(p[3]) << 24);
}

bool readUtf8Length(const QByteArray& data, qsizetype* offset, quint32* value)
{
    if (!inRange(data, *offset, 1)) return false;
    const quint8 first = quint8(data.at((*offset)++));
    if ((first & 0x80U) == 0) {
        *value = first;
        return true;
    }
    if (!inRange(data, *offset, 1)) return false;
    *value = (quint32(first & 0x7fU) << 8) | quint8(data.at((*offset)++));
    return true;
}

bool readUtf16Length(const QByteArray& data, qsizetype* offset, quint32* value)
{
    bool ok = true;
    const quint16 first = read16(data, *offset, &ok);
    if (!ok) return false;
    *offset += 2;
    if ((first & 0x8000U) == 0) {
        *value = first;
        return true;
    }
    const quint16 second = read16(data, *offset, &ok);
    if (!ok) return false;
    *offset += 2;
    *value = (quint32(first & 0x7fffU) << 16) | second;
    return true;
}

QString decodePoolString(const QByteArray& data, qsizetype offset, bool utf8, bool* ok)
{
    if (utf8) {
        quint32 utf16Length = 0;
        quint32 byteLength = 0;
        if (!readUtf8Length(data, &offset, &utf16Length)
            || !readUtf8Length(data, &offset, &byteLength)
            || byteLength > quint32(std::numeric_limits<int>::max())
            || !inRange(data, offset, byteLength)) {
            *ok = false;
            return {};
        }
        Q_UNUSED(utf16Length);
        return QString::fromUtf8(data.constData() + offset, int(byteLength));
    }

    quint32 charLength = 0;
    if (!readUtf16Length(data, &offset, &charLength)
        || charLength > quint32(std::numeric_limits<int>::max() / 2)
        || !inRange(data, offset, qsizetype(charLength) * 2)) {
        *ok = false;
        return {};
    }
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(data.constData() + offset),
                              qsizetype(charLength));
}

bool parseStringPool(const QByteArray& manifest, qsizetype chunkOffset,
                     quint16 headerSize, quint32 chunkSize,
                     QList<QString>* strings, QString* error)
{
    bool ok = true;
    const quint32 stringCount = read32(manifest, chunkOffset + 8, &ok);
    const quint32 flags = read32(manifest, chunkOffset + 16, &ok);
    const quint32 stringsStart = read32(manifest, chunkOffset + 20, &ok);
    if (!ok || headerSize < 28 || stringsStart >= chunkSize
        || stringCount > quint32(std::numeric_limits<int>::max())
        || !inRange(manifest, chunkOffset + headerSize, qsizetype(stringCount) * 4)) {
        *error = QStringLiteral("APK 字符串池结构无效");
        return false;
    }

    strings->clear();
    strings->reserve(int(stringCount));
    const bool utf8 = (flags & kUtf8Flag) != 0;
    for (quint32 i = 0; i < stringCount; ++i) {
        const quint32 relativeOffset = read32(manifest,
            chunkOffset + headerSize + qsizetype(i) * 4, &ok);
        if (!ok || relativeOffset >= chunkSize - stringsStart) {
            *error = QStringLiteral("APK 字符串偏移无效");
            return false;
        }
        const QString decoded = decodePoolString(
            manifest, chunkOffset + stringsStart + relativeOffset, utf8, &ok);
        if (!ok) {
            *error = QStringLiteral("APK 字符串解码失败");
            return false;
        }
        strings->append(decoded);
    }
    return true;
}

QString stringAt(const QList<QString>& strings, quint32 index)
{
    if (index == kNoIndex || index >= quint32(strings.size())) return {};
    return strings.at(int(index));
}

ApkMetadata parseBinaryManifest(const QByteArray& manifest)
{
    ApkMetadata metadata;
    bool ok = true;
    const quint16 type = read16(manifest, 0, &ok);
    const quint16 headerSize = read16(manifest, 2, &ok);
    const quint32 totalSize = read32(manifest, 4, &ok);
    if (!ok || type != kXmlType || headerSize < 8 || totalSize > quint32(manifest.size())) {
        metadata.error = QStringLiteral("AndroidManifest.xml 不是有效的 Android 二进制 XML");
        return metadata;
    }

    QList<QString> strings;
    qsizetype offset = headerSize;
    while (offset + 8 <= totalSize) {
        const quint16 chunkType = read16(manifest, offset, &ok);
        const quint16 chunkHeaderSize = read16(manifest, offset + 2, &ok);
        const quint32 chunkSize = read32(manifest, offset + 4, &ok);
        if (!ok || chunkHeaderSize < 8 || chunkSize < chunkHeaderSize
            || !inRange(manifest, offset, chunkSize)) {
            metadata.error = QStringLiteral("AndroidManifest.xml 包含无效数据块");
            return metadata;
        }

        if (chunkType == kStringPoolType) {
            if (!parseStringPool(manifest, offset, chunkHeaderSize, chunkSize,
                                 &strings, &metadata.error)) {
                return metadata;
            }
        } else if (chunkType == kStartElementType && !strings.isEmpty()) {
            if (chunkHeaderSize < 16 || chunkSize < 36) {
                metadata.error = QStringLiteral("APK manifest 元素结构无效");
                return metadata;
            }
            const quint32 elementNameIndex = read32(manifest, offset + 20, &ok);
            const quint16 attributeStart = read16(manifest, offset + 24, &ok);
            const quint16 attributeSize = read16(manifest, offset + 26, &ok);
            const quint16 attributeCount = read16(manifest, offset + 28, &ok);
            if (!ok || attributeSize < 20) {
                metadata.error = QStringLiteral("APK manifest 属性结构无效");
                return metadata;
            }
            if (stringAt(strings, elementNameIndex) != QStringLiteral("manifest")) {
                offset += chunkSize;
                continue;
            }

            const qsizetype attributesOffset = offset + chunkHeaderSize + attributeStart;
            if (!inRange(manifest, attributesOffset,
                         qsizetype(attributeSize) * attributeCount)) {
                metadata.error = QStringLiteral("APK manifest 属性范围无效");
                return metadata;
            }
            for (quint16 i = 0; i < attributeCount; ++i) {
                const qsizetype attr = attributesOffset + qsizetype(i) * attributeSize;
                const quint32 nameIndex = read32(manifest, attr + 4, &ok);
                const quint32 rawValueIndex = read32(manifest, attr + 8, &ok);
                const quint8 dataType = inRange(manifest, attr + 15, 1)
                    ? quint8(manifest.at(attr + 15)) : 0;
                const quint32 data = read32(manifest, attr + 16, &ok);
                if (!ok) {
                    metadata.error = QStringLiteral("APK manifest 属性读取失败");
                    return metadata;
                }

                const QString name = stringAt(strings, nameIndex);
                QString stringValue = stringAt(strings, rawValueIndex);
                if (stringValue.isEmpty() && dataType == kTypeString) {
                    stringValue = stringAt(strings, data);
                }
                if (name == QStringLiteral("package")) {
                    metadata.packageName = stringValue;
                } else if (name == QStringLiteral("versionName")) {
                    metadata.versionName = stringValue;
                } else if (name == QStringLiteral("versionCode")
                           && (dataType == kTypeIntDec || dataType == kTypeIntHex)) {
                    metadata.versionCode = data;
                }
            }
            break;
        }
        offset += chunkSize;
    }

    if (metadata.packageName.isEmpty() || metadata.versionCode < 0
        || metadata.versionName.isEmpty()) {
        metadata.error = QStringLiteral("APK manifest 缺少 package、versionCode 或 versionName");
        return metadata;
    }
    metadata.valid = true;
    return metadata;
}

} // namespace

ApkMetadata readApkMetadata(const QString& apkPath)
{
    const QFileInfo apkInfo(apkPath);
    if (!apkInfo.isAbsolute() || !apkInfo.exists() || !apkInfo.isFile()) {
        return { false, {}, -1, {},
                 QStringLiteral("APK 路径不是存在的绝对文件：%1").arg(apkPath) };
    }

    // Windows 10/11 ships bsdtar. Argument-list invocation keeps user paths out
    // of a shell command string and returns the binary manifest on stdout.
    QProcess tar;
    tar.start(QStringLiteral("tar"),
              { QStringLiteral("-xOf"), apkInfo.absoluteFilePath(),
                QStringLiteral("AndroidManifest.xml") });
    if (!tar.waitForStarted(5000)) {
        return { false, {}, -1, {}, QStringLiteral("无法启动系统 tar 读取 APK") };
    }
    if (!tar.waitForFinished(30000)) {
        tar.kill();
        tar.waitForFinished(1000);
        return { false, {}, -1, {}, QStringLiteral("读取 APK manifest 超时") };
    }
    const QByteArray manifest = tar.readAllStandardOutput();
    if (tar.exitCode() != 0 || manifest.isEmpty()) {
        return { false, {}, -1, {},
                 QStringLiteral("无法从 APK 读取 AndroidManifest.xml：%1")
                     .arg(QString::fromUtf8(tar.readAllStandardError()).trimmed()) };
    }
    return parseBinaryManifest(manifest);
}

} // namespace phonecam
