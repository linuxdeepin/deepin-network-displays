#ifndef WIRELESSCASTING_H
#define WIRELESSCASTING_H

#include "wirelesscastingmodel.h"
#include <widgets/settingsitem.h>

#include <DListView>
#include <DStandardItem>

#include <QWidget>

DWIDGET_BEGIN_NAMESPACE
class DListView;
class DSpinner;
class DIconButton;
class DLabel;
class DCommandLinkButton;
DWIDGET_END_NAMESPACE

class WirelessCastingModel;
class QStateMachine;
class QState;

using namespace Dtk::Widget;
using namespace dcc::widgets;

class QPushButton;

class MonitorItem : public DStandardItem, public QObject
{
public:
    explicit MonitorItem(const QString &text, Monitor *monitor, DListView *parent = nullptr);
    ~MonitorItem() Q_DECL_OVERRIDE;

    void connMonitor() { m_monitor->connMonitor(); }
    void disconnMonitor() { m_monitor->disconnMonitor(); }
    Monitor *monitor() { return m_monitor; }
    bool connectingState() { return m_connectingState; }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void init();
    void stateChanged(const Monitor::NdSinkState state);

    Monitor *m_monitor;
    DListView *m_parentView;
    QWidget *m_connecting;
    DSpinner *m_loadingIndicator;
    DViewItemAction *m_connectAction;
    bool m_connectingState;
};

class StatePanel : public SettingsItem
{
    Q_OBJECT
public:
    enum  ConnectionState{
        Connected = 1,
        NoMonitor,
        WarningInfo,
        DisabledWirelessDevice,
        NoWirelessDevice,
        Count
    };

    explicit StatePanel(WirelessCastingModel *model, QWidget *parent = nullptr);
    ~StatePanel() Q_DECL_OVERRIDE;
public slots:
    void setState(WirelessCastingModel::CastingState status);

signals:
    void disconnMonitor();
private:
    WirelessCastingModel *m_model;
    DLabel *m_iconConnected;
    DLabel *m_iconInfo;
    DLabel *m_connected;
    DLabel *m_info;
    DLabel *m_noDevice;
    QPushButton *m_disconnMonitor;
};

class WirelessCasting : public QWidget
{
    Q_OBJECT
public:
    explicit WirelessCasting(WirelessCastingModel *model, QWidget *parent = nullptr);

public slots:
    void onStateChanged(WirelessCastingModel::CastingState status);
    void onAddMonitor(const QString &path, Monitor *monitor);
    void onRemoveMonitor(const QString &path);
    bool casting();
private:
    void initUI();
    void initMonitors();
signals:
    void castingChanged(bool casting);
private:
    // 包含MonitorItem*, Monitor*的结构体
    typedef struct _MonitorMapItem {
        MonitorItem *item;
        Monitor *monitor;
    } MonitorMapItem;

    WirelessCastingModel *m_model;
    DIconButton *m_refresh;
    QWidget *m_content;
    DListView *m_monitorsListView;
    StatePanel *m_statePanel;
    QStandardItemModel *m_monitorsModel;
    QMap<QString, MonitorMapItem> m_monitors;
    Monitor *m_lastConnMonitor;
    bool m_cfgShowCasting;
    bool m_cfgEnabled;
};

#endif // WIRELESSCASTING_H
