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

static const QString kPkexec = "/usr/bin/pkexec";
static const QString kInstall = "/usr/bin/install";
static const QString kRm = "/usr/bin/rm";
static const QString kLs = "/usr/bin/ls";
static const QString kWg = "/usr/bin/wg";
static const QString kWgQuick = "/usr/bin/wg-quick";
static const QString kOverlayChroot = "/usr/sbin/overlayroot-chroot";

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
        if (outStdout) *outStdout = {};
        if (outStderr) *outStderr = "Failed to start process.";
        if (outExitCode) *outExitCode = -1;
        return false;
    }
    if (!p.waitForFinished(kProcTimeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        if (outStdout) *outStdout = {};
        if (outStderr) *outStderr = "Process timed out.";
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
    static const QRegularExpression re("^[a-zA-Z0-9_\\-]+\\.conf$");
    return re.match(name).hasMatch();
}

static bool isValidInterfaceName(const QString &name)
{
    static const QRegularExpression re("^[a-zA-Z0-9_\\-]+$");
    return re.match(name).hasMatch();
}

static QString interfaceNameFromProfile(const QString &input)
{
    QString name = QFileInfo(input).fileName();
    if (name.endsWith(".conf")) name.chop(5);
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
        emit operationError("Could not open source file.");
        return;
    }

    QString cleanContent;
    QTextStream in(&sourceFile);

    static const QRegularExpression dnsLine("^\\s*DNS\\s*=", QRegularExpression::CaseInsensitiveOption);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (dnsLine.match(line).hasMatch()) continue;
        cleanContent += line;
        cleanContent += '\n';
    }

    sourceFile.close();

    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        emit operationError("Failed to create temporary staging file.");
        return;
    }

    tempFile.write(cleanContent.toUtf8());
    tempFile.flush();

    const QString configName = QFileInfo(localPath).fileName();
    if (!isValidProfileFileName(configName)) {
        emit operationError("Invalid filename. Use alphanumeric characters ending in .conf only.");
        return;
    }

    const QString dstPath = "/etc/wireguard/" + configName;

    QString out;
    QString err;
    int code = 0;

    const bool liveOk = runPkexec(
        {kInstall, "-o", "root", "-g", "root", "-m", "600", tempFile.fileName(), dstPath},
        &out, &err, &code
    );

    if (!liveOk || code != 0) {
        emit operationError("Failed to install profile to live system.");
        return;
    }

    const bool persistOk = runPkexec(
        {kOverlayChroot, kInstall, "-o", "root", "-g", "root", "-m", "600", tempFile.fileName(), dstPath},
        &out, &err, &code
    );

    if (!persistOk || code != 0) {
        emit operationError("Installed to live system, but failed to persist. Reboot may be required.");
    }

    emit profileImported();
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
        emit operationError("Invalid interface name.");
        return;
    }

    QString out;
    QString err;
    int code = 0;

    if (enable && QFile::exists("/sys/class/net/" + ifname)) {
        runPkexec({kWgQuick, "down", ifname}, &out, &err, &code);
    }

    const QString action = enable ? "up" : "down";

    const bool ok = runPkexec({kWgQuick, action, ifname}, &out, &err, &code);
    if (!ok || code != 0) {
        const QString details = err.trimmed();
        emit operationError(details.isEmpty() ? "Failed to toggle tunnel." : ("Failed to toggle tunnel: " + details));
        return;
    }

    emit tunnelStateChanged();
}

QVariantMap VpnBackend::getTunnelStatus(const QString &configName)
{
    QVariantMap status;

    const QString ifname = validateInterfaceName(configName);
    if (ifname.isEmpty()) {
        status["active"] = false;
        status["handshake"] = 0;
        status["rx"] = 0;
        status["tx"] = 0;
        return status;
    }

    if (!QFile::exists("/sys/class/net/" + ifname)) {
        status["active"] = false;
        status["handshake"] = 0;
        status["rx"] = 0;
        status["tx"] = 0;
        return status;
    }

    status["active"] = true;

    QString out;
    QString err;
    int code = 0;

    StatusCache cache = s_statusCache.value(ifname);

    qint64 maxHandshake = cache.hasHandshake ? cache.handshake : 0;

    const bool hsOk = runProcess(kWg, {"show", ifname, "latest-handshakes"}, &out, &err, &code);
    if (hsOk && code == 0) {
        qint64 newest = 0;
        const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() < 2) continue;
            bool okNum = false;
            const qint64 ts = parts.last().toLongLong(&okNum);
            if (okNum && ts > newest) newest = ts;
        }
        maxHandshake = newest;
        cache.handshake = newest;
        cache.hasHandshake = true;
    }

    status["handshake"] = maxHandshake;

    qint64 rxSum = cache.hasTransfer ? cache.rx : 0;
    qint64 txSum = cache.hasTransfer ? cache.tx : 0;

    out.clear();
    err.clear();
    code = 0;

    const bool txOk = runProcess(kWg, {"show", ifname, "transfer"}, &out, &err, &code);
    if (txOk && code == 0) {
        qint64 rxNew = 0;
        qint64 txNew = 0;

        const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
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

    status["rx"] = rxSum;
    status["tx"] = txSum;

    return status;
}

void VpnBackend::removeProfile(const QString &configName)
{
    const QString fileName = QFileInfo(configName).fileName();
    if (!isValidProfileFileName(fileName)) {
        emit operationError("Invalid profile name.");
        return;
    }

    const QString dstPath = "/etc/wireguard/" + fileName;

    QString out;
    QString err;
    int code = 0;

    runPkexec({kRm, "-f", dstPath}, &out, &err, &code);
    runPkexec({kOverlayChroot, kRm, "-f", dstPath}, &out, &err, &code);

    emit profileImported();
}

QStringList VpnBackend::listProfiles()
{
    QStringList profiles;

    QDir dir("/etc/wireguard");
    if (dir.exists() && dir.isReadable()) {
        profiles = dir.entryList({"*.conf"}, QDir::Files);
    }

    if (profiles.isEmpty()) {
        QString out;
        QString err;
        int code = 0;

        const bool ok = runPkexec({kLs, "/etc/wireguard"}, &out, &err, &code);
        if (ok && code == 0) {
            const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
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
