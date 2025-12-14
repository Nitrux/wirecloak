#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class VpnBackend : public QObject
{
    Q_OBJECT

public:
    explicit VpnBackend(QObject *parent = nullptr);

    Q_INVOKABLE void importProfile(const QString &sourcePath);
    Q_INVOKABLE void toggleTunnel(const QString &configName, bool enable);
    Q_INVOKABLE QVariantMap getTunnelStatus(const QString &configName);
    Q_INVOKABLE void removeProfile(const QString &configName);
    Q_INVOKABLE QStringList listProfiles();

signals:
    void profileImported();
    void operationError(const QString &message);
    void tunnelStateChanged();

private:
    QString validateInterfaceName(const QString &input);
};
