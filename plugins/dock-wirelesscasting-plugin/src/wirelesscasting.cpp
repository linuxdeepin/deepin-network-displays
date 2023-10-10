#include "wirelesscastingmodel.h"
#include "wirelesscasting.h"

#include <dtiplabel.h>
#include <DSpinner>
#include <DPaletteHelper>
#include <DIconButton>

#include <QPushButton>
#include <QBoxLayout>
#include <QResizeEvent>

DWIDGET_USE_NAMESPACE
DCORE_USE_NAMESPACE

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
    delete m_connecting;
}

bool MonitorItem::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_parentView->viewport() && event->type() == QEvent::Resize)
    {
        QResizeEvent *resizeEvent = static_cast<QResizeEvent *>(event);
        int newWidth = resizeEvent->size().width() - 20;
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

    m_connecting->setFixedWidth(m_parentView->viewport()->width() - 20);
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
    btnCancel->setFixedSize(100, 40);

    layout->addWidget(btnCancel);

    m_connectAction = new DViewItemAction(Qt::AlignBottom,QSize(-1, 50),QSize(-1, 50));
    m_connectAction->setWidget(m_connecting);
    setActionList(Qt::Edge::BottomEdge, {m_connectAction});
    m_connectAction->setVisible(false);

    connect(btnCancel, &QPushButton::clicked, this, [this] (){this->disconnMonitor();});
    connect(m_monitor, &Monitor::NameChanged, this, [this] (const QString &name) {
        this->setText(name);
    });
    connect(m_monitor, &Monitor::stateChanged, this, &MonitorItem::stateChanged);
    setSizeHint(QSize(-1, 40));

    stateChanged(m_monitor->state());
}

void MonitorItem::stateChanged(const Monitor::NdSinkState state)
{
    switch (state) {
    case Monitor::ND_SINK_STATE_DISCONNECTED:
    case Monitor::ND_SINK_STATE_ERROR:
        m_connectingState = false;
        m_connectAction->setVisible(false);
        setSizeHint(QSize(-1, 50));
        break;
    default:
        m_connectingState = true;
        m_connectAction->setVisible(true);
        setSizeHint(QSize(-1, 90));
        break;
    }
}

StatePanel::StatePanel(WirelessCastingModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_iconConnected(new DLabel(this))
    , m_iconNoDevice(new DLabel(this))
    , m_connected(new DLabel(this))
    , m_info(new DLabel(this))
    , m_noDevice(new DLabel(this))
    , m_disconnMonitor(new QPushButton)
{
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(10, 2, 10, 2);

    m_iconConnected->setPixmap(QIcon::fromTheme("success").pixmap(QSize(128, 128)));
    m_iconConnected->setFixedSize(128, 128);
    m_iconNoDevice->setPixmap(QIcon::fromTheme("none").pixmap(QSize(128, 128)));
    m_iconNoDevice->setFixedSize(128, 128);
    m_disconnMonitor->setText(tr("Disconnect"));
    m_disconnMonitor->setFixedSize(130, 40);
    m_info->setText(tr("enable wireless network"));
    m_info->setWordWrap(true);
    m_info->setAlignment(Qt::AlignCenter);
    m_noDevice->setText(tr("No available casting wireless monitors found"));
    m_noDevice->setAlignment(Qt::AlignCenter);
    m_noDevice->setForegroundRole(DPalette::PlaceholderText);

    QVBoxLayout *connected = new QVBoxLayout();
    connected->setMargin(0);
    connected->setSpacing(20);
    connected->setAlignment(Qt::AlignCenter);
    connected->addWidget(m_iconConnected, 0, Qt::AlignCenter);
    connected->addWidget(m_connected, 0, Qt::AlignCenter);
    connected->addWidget(m_disconnMonitor, 0, Qt::AlignCenter);
    layout->addLayout(connected);

    QVBoxLayout *warningInfo = new QVBoxLayout();
    warningInfo->setMargin(0);
    warningInfo->setAlignment(Qt::AlignHCenter);
    warningInfo->addWidget(m_info);
    layout->addLayout(warningInfo);

    QVBoxLayout *noDevice = new QVBoxLayout();
    noDevice->setContentsMargins(50, 0, 50, 0);
    noDevice->setSpacing(20);
    noDevice->setAlignment(Qt::AlignVCenter);
    noDevice->addWidget(m_iconNoDevice, 0, Qt::AlignCenter);
    m_noDevice->setAlignment(Qt::AlignCenter);
    m_noDevice->setWordWrap(true);
    noDevice->addWidget(m_noDevice);
    layout->addLayout(noDevice);

    setLayout(layout);

    setState(m_model->state());
    connect(m_disconnMonitor, &QPushButton::clicked, this, &StatePanel::disconnMonitor);
    connect(m_model, &WirelessCastingModel::stateChanged, this, &StatePanel::setState);
}

StatePanel::~StatePanel()
{

}

void StatePanel::setState(WirelessCastingModel::CastingState state)
{
    setVisible(WirelessCastingModel::List != state);
    switch (state) {
    case WirelessCastingModel::Connected:
        m_iconConnected->setVisible(true);
        m_iconNoDevice->setVisible(false);
        m_connected->setText(tr("Successfully cast the screen to \"%1\"").arg(m_model->curMonitorName()));
        m_connected->setVisible(true);
        m_info->setVisible(false);
        m_noDevice->setVisible(false);
        m_disconnMonitor->setVisible(true);
        break;
    case WirelessCastingModel::NoMonitor:
        m_iconConnected->setVisible(false);
        m_iconNoDevice->setVisible(true);
        m_connected->setVisible(false);
        m_info->setVisible(false);
        m_noDevice->setVisible(true);
        m_disconnMonitor->setVisible(false);
        break;
    case WirelessCastingModel::WarningInfo:
    case WirelessCastingModel::DisabledWirelessDevice:
        m_iconConnected->setVisible(false);
        m_iconNoDevice->setVisible(false);
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
WirelessCasting::WirelessCasting(WirelessCastingModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_refresh(new DIconButton(this))
    , m_content(new QWidget(this))
    , m_monitorsListView(new DListView(this))
    , m_statePanel(new StatePanel(m_model, this))
    , m_lastConnMonitor(nullptr)
    , m_cfgShowCasting(true)
    , m_cfgEnabled(true)
{
    initUI();
    initMonitors();
    qRegisterMetaType<WirelessCastingModel::CastingState>("WirelessCastingModel::CastingState");
    connect(m_model, &WirelessCastingModel::stateChanged, this, &WirelessCasting::onStateChanged);
    connect(m_statePanel, &StatePanel::disconnMonitor, this, [this] () {
        m_model->disconnMonitor();
    });
    connect(m_model, &WirelessCastingModel::stateChanged, this, [this] (WirelessCastingModel::CastingState state) {
        Q_EMIT castingChanged(state == WirelessCastingModel::Connected);
    });
    onStateChanged(m_model->state());
}

void WirelessCasting::onStateChanged(WirelessCastingModel::CastingState state)
{
    setEnabled(m_cfgEnabled);
    if (WirelessCastingModel::NoWirelessDevice == state) {
        hide();
        return;
    } else {
        setVisible(m_cfgShowCasting);
    }

    m_monitorsListView->setVisible(WirelessCastingModel::List == state);

    switch (state) {
    case WirelessCastingModel::NoMonitor:
    case WirelessCastingModel::List:
        m_refresh->show();
        break;
    case WirelessCastingModel::Connected:
        m_lastConnMonitor = nullptr;
    default:
        m_refresh->hide();
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

bool WirelessCasting::casting() const
{
    return m_model->state() == WirelessCastingModel::Connected;
}

void WirelessCasting::initUI()
{
    QVBoxLayout *centralLayout = new QVBoxLayout(this);
    centralLayout->setAlignment(Qt::AlignCenter);
    centralLayout->setMargin(0);

    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->setAlignment(Qt::AlignVCenter);

    QLabel *title = new QLabel(tr("Wireless Casting"), this);

    m_refresh->setFixedSize(24, 24);
    m_refresh->setIcon(QIcon::fromTheme("refresh"));
    titleLayout->addWidget(title);
    m_refresh->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    titleLayout->addWidget(m_refresh);
    centralLayout->addLayout(titleLayout);

    QVBoxLayout *contentLayout = new QVBoxLayout();
    contentLayout->setMargin(0);

    m_monitorsListView->setVisible(false);
    m_monitorsModel = new QStandardItemModel(m_monitorsListView);
    m_monitorsListView->setAccessibleName("List_wirelesslist");
    m_monitorsListView->setModel(m_monitorsModel);
    m_monitorsListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_monitorsListView->setBackgroundType(DStyledItemDelegate::RoundedBackground);
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
    setFixedWidth(310);
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
