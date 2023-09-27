#include "wirelesscastingapplet.h"
#include "quickpanelwidget.h"
#include "wirelesscastingitem.h"
#include "wirelesscasting.h"
#include "wirelesscastingmodel.h"

#include <constants.h>

#include <DGuiApplicationHelper>

#include <QPixmap>

namespace dde {
namespace wirelesscasting {
WirelessCastingItem::WirelessCastingItem(QWidget *parent)
    : QWidget(parent)
    , m_model(new WirelessCastingModel(this))
    , m_quickPanel(new QuickPanelWidget())
    , m_appletWidget(new WirelessCastingApplet(new WirelessCasting(m_model)))
    , m_trayIcon(new QIcon())
    , m_tips(new QLabel())
{
    init();
    auto syncState = [this] {
        WirelessCastingModel::CastingState state = m_model->state();
        m_canCasting = WirelessCastingModel::NoWirelessDevice == state ? false : true;
        Q_EMIT canCastingChanged(m_canCasting);

        if (WirelessCastingModel::Connected != state) {
            *m_trayIcon = QIcon::fromTheme("network-display-failed");
            m_quickPanel->setIcon(QIcon::fromTheme("network-display-failed"));
            m_quickPanel->setDescription(tr("Not connected"));
            m_tips->setText(tr("Not connected"));
        } else {
            *m_trayIcon = QIcon::fromTheme("network-display-succeed");
            m_quickPanel->setIcon(QIcon::fromTheme("network-display-succeed"));
            m_quickPanel->setDescription(m_model->curMonitorName());
            m_tips->setText(m_model->curMonitorName());
        }
        update();

    };
    connect(m_model, &WirelessCastingModel::stateChanged, this, [ = ] (WirelessCastingModel::CastingState state) {
        syncState();
    });
    syncState();
    connect(m_quickPanel, &QuickPanelWidget::panelClicked, this, &WirelessCastingItem::requestExpand);
}

WirelessCastingItem::~WirelessCastingItem() 
{
    delete m_quickPanel;
    delete m_trayIcon;
}

QWidget *WirelessCastingItem::quickPanelWidget() const
{
    return m_quickPanel;
}

QWidget *WirelessCastingItem::trayIcon()
{
    return this;
}

QWidget *WirelessCastingItem::appletWidget() const
{
    return m_appletWidget;
}

void WirelessCastingItem::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const Dock::Position position = qApp->property(PROP_POSITION).value<Dock::Position>();
    // 保持横纵比
    if (position == Dock::Bottom || position == Dock::Top) {
        setMaximumWidth(height());
        setMaximumHeight(QWIDGETSIZE_MAX);
    } else {
        setMaximumHeight(width());
        setMaximumWidth(QWIDGETSIZE_MAX);
    }
}

void WirelessCastingItem::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    m_trayIcon->paint(&painter,rect());
}

void WirelessCastingItem::init()
{
    m_quickPanel->setActive(true);
    m_quickPanel->setText(tr("Wireless Casting"));
    m_quickPanel->setDescription(tr("Not connected"));
    m_quickPanel->setIcon(QIcon::fromTheme("network-display-failed"));

    *m_trayIcon = QIcon::fromTheme("network-display-succeed");
    setForegroundRole(QPalette::BrightText);
    m_tips->setText(tr("Not connected"));
    m_tips->setContentsMargins(10, 0, 10, 0);
    m_tips->setForegroundRole(QPalette::BrightText);
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, [ = ] {

    });
}


} // namespace wirelesscasting
} // namespace dde
