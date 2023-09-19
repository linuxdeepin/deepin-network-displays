#include "wirelesscasting.h"
#include "wirelesscastingmodel.h"

#include <widgets/settingsgroup.h>
#include <widgets/titledslideritem.h>
#include <widgets/titlelabel.h>
#include <widgets/labels/tipslabel.h>

#include <dtiplabel.h>
#include <DSpinner>
#include <DPaletteHelper>
#include <DIconButton>
#include <DCommandLinkButton>

#include <QPushButton>
#include <QBoxLayout>
#include <QStateMachine>
#include <QResizeEvent>
#include <QDBusInterface>
#include <QDBusReply>

using namespace dcc::widgets;
DWIDGET_USE_NAMESPACE

MonitorItem::MonitorItem(const QString &text, Monitor *monitor, DListView *parent)
    : DStandardItem(text)
    , m_monitor(monitor)
    , m_parentView(parent)
    , m_connecting(nullptr)
    , m_loadingIndicator(nullptr)
    , m_connectAction(nullptr)
    , m_connectingState(false)
{
    setFlags(Qt::ItemFlag::ItemIsEnabled | Qt::ItemFlag::ItemIsSelectable);
    setCheckable(false);
    setIcon(QIcon::fromTheme(QString("monitor")));

    init();
    m_parentView->viewport()->installEventFilter(this);
}

MonitorItem::~MonitorItem()
{
}

bool MonitorItem::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_parentView->viewport() && event->type() == QEvent::Resize)
    {
        QResizeEvent *resizeEvent = static_cast<QResizeEvent *>(event);
        int newWidth = resizeEvent->size().width()-20;
        m_connecting->setFixedWidth(newWidth);
    }
    return QObject::eventFilter(obj, event);
}

void MonitorItem::init()
{
    if (!m_parentView)
        return;

    setText(m_monitor->name());
    m_connecting = new QWidget(m_parentView->viewport());

    QHBoxLayout *layout = new QHBoxLayout(m_connecting);
    layout->setMargin(0);
    m_loadingIndicator = new DSpinner();
    m_loadingIndicator->setFixedSize(20, 20);
    m_loadingIndicator->start();
    layout->addWidget(m_loadingIndicator);

    DLabel *m_connectState = new DLabel();
    m_connectState->setText(QObject::tr("connect..."));
    m_connectState->setForegroundRole(DPalette::TextTips);
    layout->addWidget(m_connectState);
    layout->addStretch();


    QPushButton *btnCancel = new QPushButton();
    btnCancel->setText(QObject::tr("cancel"));

    layout->addWidget(btnCancel);
    m_connecting->setLayout(layout);

    m_connectAction = new DViewItemAction(Qt::AlignBottom,QSize(-1, 50),QSize(-1, 50));
    m_connectAction->setWidget(m_connecting);
    setActionList(Qt::Edge::BottomEdge, {m_connectAction});
    m_connectAction->setVisible(false);

    connect(btnCancel, &QPushButton::clicked, this, [this] (){this->disconnMonitor();});
    connect(m_monitor, &Monitor::NameChanged, this, [this] (const QString &name) {
        this->setText(name);
    });
    connect(m_monitor, &Monitor::stateChanged, this, &MonitorItem::stateChanged);
    setSizeHint(QSize(-1, 30));

    stateChanged(m_monitor->state());
}

void MonitorItem::stateChanged(const Monitor::NdSinkState state)
{
    switch (state) {
    case Monitor::ND_SINK_STATE_DISCONNECTED:
    case Monitor::ND_SINK_STATE_ERROR:
        m_connectingState = false;
        m_connectAction->setVisible(false);
        setSizeHint(QSize(-1, 30));
        break;
    default:
        m_connectingState = true;
        m_connectAction->setVisible(true);
        setSizeHint(QSize(-1, 70));
        break;
    }
}

WirelessCasting::WirelessCasting(WirelessCastingModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_refresh(new DIconButton(this))
    , m_content(new QWidget(this))
    , m_monitorsListView(new DListView(this))
    , m_statePanel(new StatePanel(m_model, this))
    , m_lastConnMonitor(nullptr)
{
    initUI();
    onStateChanged(m_model->state());
    initMonitors();
    qRegisterMetaType<WirelessCastingModel::CastingState>("WirelessCastingModel::CastingState");
    connect(m_model, &WirelessCastingModel::stateChanged, this, &WirelessCasting::onStateChanged);
    connect(m_statePanel, &StatePanel::disconnMonitor, this, [this] () {
        m_model->disconnMonitor();
    });
    connect(m_model, &WirelessCastingModel::stateChanged, this, [this] (WirelessCastingModel::CastingState state) {
        Q_EMIT castingChanged(state == WirelessCastingModel::Connected);
    });
}

void WirelessCasting::onStateChanged(WirelessCastingModel::CastingState state)
{
    if (WirelessCastingModel::NoWirelessDevice == state) {
        hide();
        return;
    } else {
        show();
    }

    if (WirelessCastingModel::List == state){
        m_monitorsListView->show();
    } else {
        m_monitorsListView->hide();
    }
    switch (state) {
    case WirelessCastingModel::Connected:
        m_lastConnMonitor = nullptr;
        break;
    default:
        break;
    }
}

void WirelessCasting::onAddMonitor(const QString &path, Monitor *monitor)
{
    MonitorItem *item = new MonitorItem(monitor->name(), monitor, m_monitorsListView);
    item->setData(QVariant::fromValue(monitor), Qt::UserRole);
    m_monitorsModel->appendRow(item);
    m_monitors[path] = MonitorMapItem{item, monitor};
}

void WirelessCasting::onRemoveMonitor(const QString &path)
{
    m_monitorsModel->removeRow(m_monitorsModel->indexFromItem(m_monitors[path].item).row());
    if (m_lastConnMonitor == m_monitors.value(path).monitor)
        m_lastConnMonitor = nullptr;
    m_monitors.remove(path);
}

bool WirelessCasting::casting()
{
    return m_model->state() == WirelessCastingModel::Connected;
}

void WirelessCasting::initUI()
{
    QVBoxLayout *centralLayout = new QVBoxLayout(this);
    centralLayout->setMargin(10);
    centralLayout->setSpacing(10);
    centralLayout->setAlignment(Qt::AlignCenter);

    //~ contents_path /display/Wireless Casting
    TitleLabel *title = new TitleLabel(tr("Wireless Casting"), this);

    centralLayout->addWidget(title);

    QHBoxLayout *tipsLayout = new QHBoxLayout();
    tipsLayout->setAlignment(Qt::AlignVCenter);

    DTipLabel *tipsLabel = new DTipLabel(tr("Cast the screen to a wireless monitor"), this);

    tipsLabel->setForegroundRole(DPalette::TextTips);
    tipsLabel->setWordWrap(true);
    tipsLabel->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
    tipsLabel->adjustSize();
    tipsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    m_refresh->setFixedSize(36, 36);
    m_refresh->setIcon(QIcon::fromTheme("dcc_refresh"));
    tipsLayout->addWidget(tipsLabel);
    m_refresh->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    tipsLayout->addWidget(m_refresh);
    centralLayout->addLayout(tipsLayout);

    QVBoxLayout *contentLayout = new QVBoxLayout();
    contentLayout->setMargin(0);

    m_monitorsListView->setVisible(false);
    m_monitorsModel = new QStandardItemModel(m_monitorsListView);
    m_monitorsListView->setAccessibleName("List_wirelesslist");
    m_monitorsListView->setModel(m_monitorsModel);
    m_monitorsListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_monitorsListView->setBackgroundType(DStyledItemDelegate::BackgroundType::ClipCornerBackground);
    m_monitorsListView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_monitorsListView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_monitorsListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_monitorsListView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_monitorsListView->setSelectionMode(QAbstractItemView::NoSelection);

    contentLayout->addWidget(m_monitorsListView);
    contentLayout->addWidget(m_statePanel);
    m_content->setLayout(contentLayout);
    centralLayout->addWidget(m_content);
    setLayout(centralLayout);
    connect(m_refresh, &QPushButton::clicked, m_model, &WirelessCastingModel::refresh);
    connect(m_monitorsListView, &DListView::doubleClicked, this, [ this ] (const QModelIndex & idx) {
        const QStandardItemModel *monitorModel = dynamic_cast<const QStandardItemModel *>(idx.model());
        auto *item = dynamic_cast<MonitorItem *>(monitorModel->item(idx.row()));
        if (item) {
            if (nullptr != m_lastConnMonitor) {
                if (item->monitor() == m_lastConnMonitor && item->connectingState() == true)
                    return;
                m_lastConnMonitor->disconnMonitor();
            }
            item->connMonitor();
            m_lastConnMonitor = item->monitor();
        }
    });
}

void WirelessCasting::initMonitors()
{
    auto monitors = m_model->monitors();
    // 遍历所有设备
    for (auto it = monitors.begin(); it != monitors.end(); ++it) {
        MonitorItem *item = new MonitorItem(it.key(), it.value(), m_monitorsListView);
        item->setData(QVariant::fromValue(it.value()), Qt::UserRole);
        m_monitorsModel->appendRow(item);
        m_monitors[it.key()] = MonitorMapItem{item, it.value()};
    }
    connect(m_model, &WirelessCastingModel::addMonitor, this, &WirelessCasting::onAddMonitor);
    connect(m_model, &WirelessCastingModel::removeMonitor, this, &WirelessCasting::onRemoveMonitor);
}

StatePanel::StatePanel(WirelessCastingModel *model, QWidget *parent)
    : SettingsItem(parent)
    , m_model(model)
    , m_iconConnected(new DLabel(this))
    , m_iconInfo(new DLabel(this))
    , m_connected(new DLabel(this))
    , m_info(new DLabel(this))
    , m_noDevice(new DLabel(this))
    , m_disconnMonitor(new QPushButton)
{
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(10, 2, 10, 2);

    m_iconConnected->setPixmap(QPixmap());
    m_iconInfo->setPixmap(QPixmap());
    m_disconnMonitor->setText(tr("Disconnect"));
    m_disconnMonitor->setFixedWidth(100);
    m_info->setText(tr("enable wireless network"));
    m_noDevice->setText(tr("No available casting wireless monitors found"));
    m_noDevice->setAlignment(Qt::AlignCenter);
    m_noDevice->setForegroundRole(DPalette::PlaceholderText);

    QHBoxLayout *connectedH = new QHBoxLayout();
    connectedH->setMargin(0);
    connectedH->setAlignment(Qt::AlignCenter);
    connectedH->addWidget(m_iconConnected);
    connectedH->addWidget(m_connected);

    QVBoxLayout *connected = new QVBoxLayout();
    connected->setMargin(0);
    connected->setAlignment(Qt::AlignCenter);
    connected->addLayout(connectedH);
    connected->addWidget(m_disconnMonitor);
    layout->addLayout(connected);

    QHBoxLayout *warningInfo = new QHBoxLayout();
    warningInfo->setMargin(0);
    warningInfo->setAlignment(Qt::AlignVCenter);
    warningInfo->insertStretch(0);
    warningInfo->addWidget(m_iconInfo);
    warningInfo->addWidget(m_info);
    warningInfo->insertStretch(warningInfo->count());
    layout->addLayout(warningInfo);

    QHBoxLayout *noDevice = new QHBoxLayout();
    noDevice->setMargin(0);
    noDevice->setAlignment(Qt::AlignVCenter);
    noDevice->addWidget(m_noDevice);
    layout->addLayout(noDevice);

    setLayout(layout);
    setFixedHeight(150);
    addBackground();


    setState(m_model->state());
    connect(m_disconnMonitor, &QPushButton::clicked, this, &StatePanel::disconnMonitor);
    connect(m_model, &WirelessCastingModel::stateChanged, this, &StatePanel::setState);
}

StatePanel::~StatePanel()
{

}

void StatePanel::setState(WirelessCastingModel::CastingState state)
{
    if (WirelessCastingModel::List != state){
        show();
    } else {
        hide();
    }
    switch (state) {
    case WirelessCastingModel::Connected:
        m_iconConnected->setVisible(true);
        m_iconInfo->setVisible(false);
        m_connected->setText(tr("Successfully cast the screen to %1").arg(m_model->curMonitorName()));
        m_connected->setVisible(true);
        m_info->setVisible(false);
        m_noDevice->setVisible(false);
        m_disconnMonitor->setVisible(true);
        break;
    case WirelessCastingModel::NoMonitor:
        m_iconConnected->setVisible(false);
        m_iconInfo->setVisible(false);
        m_connected->setVisible(false);
        m_info->setVisible(false);
        m_noDevice->setVisible(true);
        m_disconnMonitor->setVisible(false);
        break;
    case WirelessCastingModel::WarningInfo:
    case WirelessCastingModel::DisabledWirelessDevice:
        m_iconConnected->setVisible(false);
        m_iconInfo->setVisible(true);
        m_connected->setVisible(false);
        m_info->setVisible(true);
        m_noDevice->setVisible(false);
        m_disconnMonitor->setVisible(false);
        break;
    default:
        break;
    }

    if (WirelessCastingModel::WarningInfo == state)
        m_info->setText(tr("The wireless card doesn't support P2P， and cannot cast the screen to a wireless monitor"));
    else if (WirelessCastingModel::DisabledWirelessDevice == state)
        m_info->setText(tr("You need to enable wireless network to cast the screen to a wireless monitor"));


}
