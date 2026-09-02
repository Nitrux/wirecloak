// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#include "vpnbackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>

namespace
{
static constexpr int kProcTimeoutMs = 30000;

static const QString kPkexec = QStringLiteral("/usr/bin/pkexec");
static const QString kInstall = QStringLiteral("/usr/bin/install");
static const QString kRm = QStringLiteral("/usr/bin/rm");
static const QString kLs = QStringLiteral("/usr/bin/ls");
static const QString kWg = QStringLiteral("/usr/bin/wg");
static const QString kWgQuick = QStringLiteral("/usr/bin/wg-quick");
static const QString kOverlayChroot = QStringLiteral("/usr/sbin/overlayroot-chroot");

static QString localPathFromUrlOrPath(const QString &sourcePath)
{
    QUrl url(sourcePath);
    if (url.isValid() && url.isLocalFile()) return url.toLocalFile();
    return sourcePath;
}

static bool runProcess(const QString &program,
                       const QStringList &args,
                       QString *outStdout,
                       QString *outStderr,
                       int *outExitCode)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(kProcTimeoutMs)) {
        if (outStdout) *outStdout = QString();
        if (outStderr) *outStderr = QStringLiteral("Failed to start process.");
        if (outExitCode) *outExitCode = -1;
        return false;
    }
    if (!p.waitForFinished(kProcTimeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        if (outStdout) *outStdout = QString();
        if (outStderr) *outStderr = QStringLiteral("Process timed out.");
        if (outExitCode) *outExitCode = -1;
        return false;
    }
    if (outStdout) *outStdout = QString::fromUtf8(p.readAllStandardOutput());
    if (outStderr) *outStderr = QString::fromUtf8(p.readAllStandardError());
    if (outExitCode) *outExitCode = p.exitCode();
    return p.exitStatus() == QProcess::NormalExit;
}

static bool runPkexec(const QStringList &args,
                      QString *outStdout,
                      QString *outStderr,
                      int *outExitCode)
{
    return runProcess(kPkexec, args, outStdout, outStderr, outExitCode);
}

static bool isValidProfileFileName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral(R"(^[a-zA-Z0-9_-]+\.conf$)"));
    return re.match(name).hasMatch();
}

static bool isValidInterfaceName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral(R"(^[a-zA-Z0-9_-]+$)"));
    return re.match(name).hasMatch();
}

static QString interfaceNameFromProfile(const QString &input)
{
    QString name = QFileInfo(input).fileName();
    if (name.endsWith(QStringLiteral(".conf"))) name.chop(5);
    if (!isValidInterfaceName(name)) return {};
    return name;
}

struct StatusCache
{
    qint64 handshake = 0;
    qint64 rx = 0;
    qint64 tx = 0;
    bool hasHandshake = false;
    bool hasTransfer = false;
};

static QHash<QString, StatusCache> s_statusCache;
}

VpnBackend::VpnBackend(QObject *parent) : QObject(parent) {}

void VpnBackend::importProfile(const QString &sourcePath)
{
    const QString localPath = localPathFromUrlOrPath(sourcePath);

    QFile sourceFile(localPath);
    if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT operationError(QStringLiteral("Could not open source file."));
        return;
    }

    QString cleanContent;
    QTextStream in(&sourceFile);

    static const QRegularExpression dnsLine(QStringLiteral(R"(^\s*DNS\s*=)"), QRegularExpression::CaseInsensitiveOption);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (dnsLine.match(line).hasMatch()) continue;
        cleanContent += line;
        cleanContent += QChar(u'\n');
    }

    sourceFile.close();

    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        Q_EMIT operationError(QStringLiteral("Failed to create temporary staging file."));
        return;
    }

    tempFile.write(cleanContent.toUtf8());
    tempFile.flush();

    const QString configName = QFileInfo(localPath).fileName();
    if (!isValidProfileFileName(configName)) {
        Q_EMIT operationError(QStringLiteral("Invalid filename. Use alphanumeric characters ending in .conf only."));
        return;
    }

    const QString dstPath = QStringLiteral("/etc/wireguard/") + configName;

    QString out;
    QString err;
    int code = 0;

    const bool liveOk = runPkexec(
        {kInstall, QStringLiteral("-o"), QStringLiteral("root"), QStringLiteral("-g"), QStringLiteral("root"), QStringLiteral("-m"), QStringLiteral("600"), tempFile.fileName(), dstPath},
        &out, &err, &code
    );

    if (!liveOk || code != 0) {
        Q_EMIT operationError(QStringLiteral("Failed to install profile to live system."));
        return;
    }

    const bool persistOk = runPkexec(
        {kOverlayChroot, kInstall, QStringLiteral("-o"), QStringLiteral("root"), QStringLiteral("-g"), QStringLiteral("root"), QStringLiteral("-m"), QStringLiteral("600"), tempFile.fileName(), dstPath},
        &out, &err, &code
    );

    if (!persistOk || code != 0) {
        Q_EMIT operationError(QStringLiteral("Installed to live system, but failed to persist. Reboot may be required."));
    }

    Q_EMIT profileImported();
}

QString VpnBackend::validateInterfaceName(const QString &input)
{
    const QString name = interfaceNameFromProfile(input);
    return name;
}

void VpnBackend::toggleTunnel(const QString &configName, bool enable)
{
    const QString ifname = validateInterfaceName(configName);
    if (ifname.isEmpty()) {
        Q_EMIT operationError(QStringLiteral("Invalid interface name."));
        return;
    }

    QString out;
    QString err;
    int code = 0;

    if (enable && QFile::exists(QStringLiteral("/sys/class/net/") + ifname)) {
        runPkexec({kWgQuick, QStringLiteral("down"), ifname}, &out, &err, &code);
    }

    const QString action = enable ? QStringLiteral("up") : QStringLiteral("down");

    const bool ok = runPkexec({kWgQuick, action, ifname}, &out, &err, &code);
    if (!ok || code != 0) {
        const QString details = err.trimmed();
        Q_EMIT operationError(details.isEmpty() ? QStringLiteral("Failed to toggle tunnel.") : (QStringLiteral("Failed to toggle tunnel: ") + details));
        return;
    }

    Q_EMIT tunnelStateChanged();
}

QVariantMap VpnBackend::getTunnelStatus(const QString &configName)
{
    QVariantMap status;

    const QString ifname = validateInterfaceName(configName);
    if (ifname.isEmpty()) {
        status[QStringLiteral("active")] = false;
        status[QStringLiteral("handshake")] = 0;
        status[QStringLiteral("rx")] = 0;
        status[QStringLiteral("tx")] = 0;
        return status;
    }

    if (!QFile::exists(QStringLiteral("/sys/class/net/") + ifname)) {
        status[QStringLiteral("active")] = false;
        status[QStringLiteral("handshake")] = 0;
        status[QStringLiteral("rx")] = 0;
        status[QStringLiteral("tx")] = 0;
        return status;
    }

    status[QStringLiteral("active")] = true;

    QString out;
    QString err;
    int code = 0;

    StatusCache cache = s_statusCache.value(ifname);

    qint64 maxHandshake = cache.hasHandshake ? cache.handshake : 0;

    const bool hsOk = runProcess(kWg, {QStringLiteral("show"), ifname, QStringLiteral("latest-handshakes")}, &out, &err, &code);
    if (hsOk && code == 0) {
        qint64 newest = 0;
        const QStringList lines = out.split(QChar(u'\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.trimmed().split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
            if (parts.size() < 2) continue;
            bool okNum = false;
            const qint64 ts = parts.last().toLongLong(&okNum);
            if (okNum && ts > newest) newest = ts;
        }
        maxHandshake = newest;
        cache.handshake = newest;
        cache.hasHandshake = true;
    }

    status[QStringLiteral("handshake")] = maxHandshake;

    qint64 rxSum = cache.hasTransfer ? cache.rx : 0;
    qint64 txSum = cache.hasTransfer ? cache.tx : 0;

    out.clear();
    err.clear();
    code = 0;

    const bool txOk = runProcess(kWg, {QStringLiteral("show"), ifname, QStringLiteral("transfer")}, &out, &err, &code);
    if (txOk && code == 0) {
        qint64 rxNew = 0;
        qint64 txNew = 0;

        const QStringList lines = out.split(QChar(u'\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.trimmed().split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
            if (parts.size() >= 3) {
                bool okRx = false;
                bool okTx = false;
                const qint64 rx = parts[1].toLongLong(&okRx);
                const qint64 tx = parts[2].toLongLong(&okTx);
                if (okRx) rxNew += rx;
                if (okTx) txNew += tx;
            } else if (parts.size() == 2) {
                bool okRx = false;
                bool okTx = false;
                const qint64 rx = parts[0].toLongLong(&okRx);
                const qint64 tx = parts[1].toLongLong(&okTx);
                if (okRx) rxNew += rx;
                if (okTx) txNew += tx;
            }
        }

        rxSum = rxNew;
        txSum = txNew;

        cache.rx = rxNew;
        cache.tx = txNew;
        cache.hasTransfer = true;
    }

    s_statusCache.insert(ifname, cache);

    status[QStringLiteral("rx")] = rxSum;
    status[QStringLiteral("tx")] = txSum;

    return status;
}

void VpnBackend::removeProfile(const QString &configName)
{
    const QString fileName = QFileInfo(configName).fileName();
    if (!isValidProfileFileName(fileName)) {
        Q_EMIT operationError(QStringLiteral("Invalid profile name."));
        return;
    }

    const QString dstPath = QStringLiteral("/etc/wireguard/") + fileName;

    QString out;
    QString err;
    int code = 0;

    runPkexec({kRm, QStringLiteral("-f"), dstPath}, &out, &err, &code);
    runPkexec({kOverlayChroot, kRm, QStringLiteral("-f"), dstPath}, &out, &err, &code);

    Q_EMIT profileImported();
}

QStringList VpnBackend::listProfiles()
{
    QStringList profiles;

    QDir dir(QStringLiteral("/etc/wireguard"));
    if (dir.exists() && dir.isReadable()) {
        profiles = dir.entryList({QStringLiteral("*.conf")}, QDir::Files);
    }

    if (profiles.isEmpty()) {
        QString out;
        QString err;
        int code = 0;

        const bool ok = runPkexec({kLs, QStringLiteral("/etc/wireguard")}, &out, &err, &code);
        if (ok && code == 0) {
            const QStringList lines = out.split(QChar(u'\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QString name = line.trimmed();
                if (isValidProfileFileName(name)) profiles.append(name);
            }
        }
    }

    profiles.removeDuplicates();
    profiles.sort();
    return profiles;
}
