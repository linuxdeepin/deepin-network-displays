#include "wirelesscastingmodel.h"

#include <ddbusinterface.h>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMessage>
#include <QDebug>

const QString dbusService = "com.deepin.Cooperation.NetworkDisplay";
const QString dbusPath = "/com/deepin/Cooperation/NetworkDisplay";
const QString dbusInterface = "com.deepin.Cooperation.NetworkDisplay";
const QString sinkInterface = "com.deepin.Cooperation.NetworkDisplay.Sink";

DCORE_USE_NAMESPACE

WirelessCastingModel::WirelessCastingModel(QObject *parent)
    : QObject{parent}
    , m_dbus(nullptr)
    , m_dbusNM(nullptr)
    , m_dbusDdeNetwork(nullptr)
    , m_state(NoMonitor)
    , m_curConnected(nullptr)
    , m_wirelessDevCheck(false)
    , m_wirelessEnabled(false)
{
    prepareDbus();
    initData();
    startTimer(std::chrono::minutes(1));
}

WirelessCastingModel::~WirelessCastingModel()
{
    m_dbus->deleteLater();
}

void WirelessCastingModel::refresh()
{
    QDBusMessage reply = m_dbus->call("Refresh");
    if (reply.type() == QDBusMessage::ReplyMessage) {
        qInfo () << reply.arguments().value(0);
    } else {
        qWarning() << "Method call failed. Error:" << reply.errorMessage();
    }
}

void WirelessCastingModel::disconnMonitor()
{
    if(nullptr != m_curConnected)
        m_curConnected->disconnMonitor();
}

const QString WirelessCastingModel::curMonitorName()
{
    if(nullptr != m_curConnected)
        return m_curConnected->name();
    else
        return QString();
}

void WirelessCastingModel::initData()
{
    m_dbus = new DDBusInterface(dbusService, dbusPath, dbusInterface, QDBusConnection::sessionBus(), this);
    QVariant var = m_dbus->property("SinkList");
    if (!var.isValid())
        return;
    updateSinkList(var);
    var = m_dbus->property("MissingCapabilities");
    if (!var.isValid())
        return;
    updateWarningInfo(var);
    connect(this, &WirelessCastingModel::SinkListChanged, this, [this] (const QList<QDBusObjectPath> &sinkList) {
        updateSinkList(QVariant::fromValue(sinkList));
    });
    connect(this, &WirelessCastingModel::MissingCapabilitiesChanged, this, [this] (const QStringList &missingList) {
        updateWarningInfo(missingList);
    });
}

void WirelessCastingModel::updateSinkList(const QVariant &var)
{
    QList<QDBusObjectPath> list = qdbus_cast<QList<QDBusObjectPath>>(var);

    QStringList tmpList;

    bool hasConnected = false;

    foreach (const QDBusObjectPath &var, list) {
        QString path = var.path();
        tmpList.append(path);
        if (!m_monitors.contains(path)) {
            Monitor *mon = new Monitor(path);
            m_monitors.insert(path, mon);
            connect(mon, &Monitor::stateChanged, this, &WirelessCastingModel::handleMonitorStateCanged);
            Q_EMIT addMonitor(path, mon);
        }
    }

    auto it = m_monitors.begin();
    while (it != m_monitors.end()) {
        if (tmpList.contains(it.key())) {
            if (it.value()->state() == Monitor::ND_SINK_STATE_STREAMING) {
                m_curConnected = it.value();
                hasConnected = true;
            }
            ++it;
            continue;
        }
        disconnect(it.value(), &Monitor::stateChanged, this, &WirelessCastingModel::handleMonitorStateCanged);
        Q_EMIT removeMonitor(it.key());
        it.value()->deleteLater();
        it = m_monitors.erase(it);
    }

    if (!hasConnected)
        m_curConnected = nullptr;
    checkState();
}

void WirelessCastingModel::updateWarningInfo(const QVariant &var)
{
    m_warningInfo = var.toStringList();
    checkState();
}

void WirelessCastingModel::checkState()
{
    if (!m_wirelessDevCheck && NoWirelessDevice != m_state) {
        m_state = NoWirelessDevice;
        Q_EMIT stateChanged(NoWirelessDevice);
        return;
    } else if (!m_wirelessEnabled) {
        if (DisabledWirelessDevice != m_state)
            m_state = DisabledWirelessDevice;
        Q_EMIT stateChanged(DisabledWirelessDevice);
        return;
    }

    if (!m_warningInfo.isEmpty() && WarningInfo != m_state) {
        m_state = WarningInfo;
        Q_EMIT stateChanged(m_state);
        return;
    }
    if (nullptr == m_curConnected) {
        if (m_monitors.empty()) {
            if (NoMonitor != m_state) {
            m_state = NoMonitor;
            Q_EMIT stateChanged(m_state);
            }
        } else {
            if (List != m_state) {
            m_state = List;
            Q_EMIT stateChanged(m_state);
            }
        }
    } else {
        m_state = Connected;
        Q_EMIT stateChanged(m_state);
    }
}

void WirelessCastingModel::prepareDbus()
{
    auto checkWirelessDev = [=] () {
        m_wirelessDevCheck = false;
        m_wirelessEnabled = false;
        // 获取设备列表
        QList<QDBusObjectPath> devices = qdbus_cast<QList<QDBusObjectPath>>(m_dbusNM->property("AllDevices"));

        // 遍历设备列表并检查是否有无线网卡设备
        foreach (const QDBusObjectPath &devicePath, devices) {
            DDBusInterface deviceInterface("org.freedesktop.NetworkManager", devicePath.path(), "org.freedesktop.NetworkManager.Device", QDBusConnection::systemBus());

            // 获取设备类型
            auto reply = deviceInterface.property("DeviceType");
            if (reply.isValid() && reply.toUInt() == 2) {
                m_wirelessDevCheck =  true;

                // 检查设备是否启用
                QDBusMessage reply = m_dbusDdeNetwork->call("IsDeviceEnabled", devicePath.path());
                if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().value(0).toBool()) {
                    m_wirelessEnabled =  true;
                    break;
                } else {
                    qWarning() << "Method call failed. Error:" << reply.errorMessage();
                }
            }

        }
        checkState();
    };

    // 创建 NetworkManager D-Bus 接口
    m_dbusNM = new DDBusInterface("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.NetworkManager", QDBusConnection::systemBus(), this);

    m_dbusDdeNetwork = new DDBusInterface("com.deepin.daemon.Network", "/com/deepin/daemon/Network", "com.deepin.daemon.Network", QDBusConnection::sessionBus(), this);

    QDBusConnection::sessionBus().connect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",this,
                                SLOT(onDBusNameOwnerChanged(QString, QString, QString)));


    checkWirelessDev();
    // 设置监听事件
    connect(this, &WirelessCastingModel::DeviceEnabled, this, [=] (const QString &, bool) {
        checkWirelessDev();
    });
    connect(this, &WirelessCastingModel::AllDevicesChanged, this, [=] (const QList<QDBusObjectPath> &devList) {
        checkWirelessDev();
    });
}

void WirelessCastingModel::onDBusNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    if ("com.deepin.Cooperation.NetworkDisplay" == name && newOwner.isEmpty()) {
        resetNetworkDisplayData();
        checkState();
    }
}

void WirelessCastingModel::resetNetworkDisplayData()
{
    delete m_dbus;
    auto it = m_monitors.begin();
    while (it != m_monitors.end()) {
        disconnect(it.value(), &Monitor::stateChanged, this, &WirelessCastingModel::handleMonitorStateCanged);
        Q_EMIT removeMonitor(it.key());
        it.value()->deleteLater();
        it = m_monitors.erase(it);
    }
    initData();
}

void WirelessCastingModel::handleMonitorStateCanged(const Monitor::NdSinkState state)
{
    switch (state) {
    case Monitor::ND_SINK_STATE_DISCONNECTED:
    case Monitor::ND_SINK_STATE_ERROR:
        if (sender() == m_curConnected)
            m_curConnected = nullptr;
        break;
    case Monitor::ND_SINK_STATE_STREAMING:
        if (sender() != m_curConnected)
            m_curConnected = qobject_cast<Monitor*>(sender());
        break;
    default:
        break;
    }
    checkState();
}

void WirelessCastingModel::timerEvent(QTimerEvent *event)
{
    refresh();
}

Monitor::Monitor(QString monitorPath, QObject *parent)
    : QObject(parent)
    , m_dbus(nullptr)
    , m_state(ND_SINK_STATE_DISCONNECTED)
    , m_strength(0)
{
    m_dbus = new DDBusInterface(dbusService, monitorPath, sinkInterface, QDBusConnection::sessionBus(), this);

    initData();
}

Monitor::~Monitor()
{
    disconnect();
    m_dbus->deleteLater();
}

void Monitor::connMonitor()
{
    QDBusMessage reply = m_dbus->call("Connect");
    if (reply.type() == QDBusMessage::ReplyMessage) {
        qInfo () << reply.arguments().value(0);
    } else {
        qWarning() << "Method call failed. Error:" << reply.errorMessage();
    }
}

void Monitor::disconnMonitor()
{
    QDBusMessage reply = m_dbus->call("Cancel");
    if (reply.type() == QDBusMessage::ReplyMessage) {
        qInfo() << reply.arguments().value(0);
    } else {
        qWarning() << "Method call failed. Error:" << reply.errorMessage();
    }
}

void Monitor::initData()
{
    QVariant var = m_dbus->property("Name");
    if (var.isValid())
        m_name = var.toString();

    var = m_dbus->property("HwAddress");
    if (var.isValid())
        m_hwAddress = var.toString();

    var = m_dbus->property("Status");
    if (var.isValid())
        m_state = static_cast<NdSinkState>(var.toUInt());

    var = m_dbus->property("Strength");
    if (var.isValid())
        m_strength = var.toUInt();

    connect(this, &Monitor::StatusChanged, this, [this] (int status) {
        qInfo() << "Monitor" << m_name << "status changed to " << status;
        checkState(status);
    });
}

void Monitor::checkState(const QVariant &var)
{
    if (!var.isValid())
        return;
    NdSinkState state = static_cast<NdSinkState>(var.toUInt());
    if (state != m_state) {
        m_state = state;
        Q_EMIT stateChanged(m_state);
    }
}
