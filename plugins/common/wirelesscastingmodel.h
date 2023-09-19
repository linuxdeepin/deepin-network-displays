#ifndef WIRELESSCASTINGMODEL_H
#define WIRELESSCASTINGMODEL_H

//#include <ddbusinterface.h>

#include <QObject>
#include <QDBusMessage>

class QDBusObjectPath;
class QDBusVariant;
namespace Dtk {
namespace Core {
class DDBusInterface;
}
}

class Monitor : public QObject
{
    Q_OBJECT
public:
    typedef enum {
        ND_SINK_STATE_DISCONNECTED       =      0x0,
        ND_SINK_STATE_ENSURE_FIREWALL    =     0x50,
        ND_SINK_STATE_WAIT_P2P           =    0x100,
        ND_SINK_STATE_WAIT_SOCKET        =    0x110,
        ND_SINK_STATE_WAIT_STREAMING     =    0x120,
        ND_SINK_STATE_STREAMING          =   0x1000,
        ND_SINK_STATE_ERROR              =  0x10000,
    } NdSinkState;

    explicit Monitor(QString monitorPath, QObject *parent = nullptr);
    ~Monitor() Q_DECL_OVERRIDE;

    void connMonitor();
    void disconnMonitor();
    QString name() { return m_name; }
    QString hwAddress() { return m_name; }
    NdSinkState state() { return m_state; }
public slots:
    void handlePropertiesChanged(QDBusMessage msg);

signals:
    void stateChanged(const NdSinkState state);
    void StatusChanged(int status);
    void NameChanged(const QString &name);

private:
    void initData();
    void checkState(const QVariant &var);

    Dtk::Core::DDBusInterface *m_dbus;
    QString m_hwAddress;
    QString m_name;
    NdSinkState m_state;
    quint32 m_strength;
};

class WirelessCastingModel : public QObject
{
    Q_OBJECT
public:
    enum  CastingState{
        List,
        Connected,
        NoMonitor,
        WarningInfo,
        DisabledWirelessDevice,
        NoWirelessDevice,
        Count
    };

    explicit WirelessCastingModel(QObject *parent = nullptr);
    ~WirelessCastingModel() Q_DECL_OVERRIDE;

    QMap<QString, Monitor*> monitors() { return m_monitors; }
    CastingState state() { return m_state; }
    const QStringList &warningInfo() { return m_warningInfo; }
    void refresh();
    void disconnMonitor();
    const QString curMonitorName();
public slots:
    void handleMonitorStateCanged(const Monitor::NdSinkState state);
signals:
    void addMonitor(const QString &path, Monitor *monitor);
    void removeMonitor(const QString &path);
    void stateChanged(CastingState state);
    void wirelessDevChanged(bool wirelessDevCheck);

    void SinkListChanged(const QList<QDBusObjectPath> &sinkList);
    void MissingCapabilitiesChanged(const QStringList &missingList);

    void AllDevicesChanged(const QList<QDBusObjectPath> &devList);
    void DeviceEnabled(const QString &, bool);

protected:
    void timerEvent(QTimerEvent *event) override;
private:
    void initData();
    void updateSinkList(const QVariant &var);
    void updateWarningInfo(const QVariant &var);
    void checkState();
    void prepareDbus();

    Dtk::Core::DDBusInterface *m_dbus;
    Dtk::Core::DDBusInterface *m_dbusNM;
    Dtk::Core::DDBusInterface *m_dbusDdeNetwork;
    QMap<QString, Monitor*> m_monitors;
    CastingState m_state;
    Monitor *m_curConnected;
    QStringList m_warningInfo;
    bool m_wirelessDevCheck;
    bool m_wirelessEnabled;
};
Q_DECLARE_METATYPE(WirelessCastingModel::CastingState)
#endif // WIRELESSCASTINGMODEL_H
