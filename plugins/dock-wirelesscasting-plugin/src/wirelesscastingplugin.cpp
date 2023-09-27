#include "wirelesscastingplugin.h"
#include "wirelesscastingitem.h"

#include <QLabel>

namespace dde {
namespace wirelesscasting {

WirelessCastingPlugin::WirelessCastingPlugin(QObject *parent)
    : QObject(parent)
    , m_wirelessCastingItem(nullptr)
{

}

void WirelessCastingPlugin::init(PluginProxyInterface *proxyInter)
{
    m_proxyInter = proxyInter;

    if (m_wirelessCastingItem)
        return;
    m_wirelessCastingItem = new WirelessCastingItem();
    if (m_wirelessCastingItem->canCasting())
        m_proxyInter->itemAdded(this, WIRELESS_CASTING_KEY);

    connect(m_wirelessCastingItem, &WirelessCastingItem::canCastingChanged, this, [this] (bool canCasting) {
        if (canCasting)
            m_proxyInter->itemAdded(this, WIRELESS_CASTING_KEY);
        else
            m_proxyInter->itemRemoved(this, WIRELESS_CASTING_KEY);
    });
    connect(m_wirelessCastingItem, &WirelessCastingItem::requestExpand, this, [this] {
        m_proxyInter->requestSetAppletVisible(this, WIRELESS_CASTING_KEY, true);
    });
}

void WirelessCastingPlugin::pluginStateSwitched()
{

}

bool WirelessCastingPlugin::pluginIsDisable()
{
    return false;
}

QWidget *WirelessCastingPlugin::itemWidget(const QString &itemKey)
{
    if ("quick_item_key" == itemKey)
        return m_wirelessCastingItem->quickPanelWidget();
    if (WIRELESS_CASTING_KEY == itemKey)
        return m_wirelessCastingItem->trayIcon();
    return nullptr;
}

QWidget *WirelessCastingPlugin::itemTipsWidget(const QString &itemKey)
{
    return m_wirelessCastingItem->tips();
}

QWidget *WirelessCastingPlugin::itemPopupApplet(const QString &itemKey)
{
    return m_wirelessCastingItem->appletWidget();
}

const QString WirelessCastingPlugin::itemContextMenu(const QString &itemKey)
{
    return QString();
}

void WirelessCastingPlugin::invokedMenuItem(const QString &itemKey, const QString &menuId, const bool checked)
{

}

int WirelessCastingPlugin::itemSortKey(const QString &itemKey)
{
    const QString key = QString("pos_%1_%2").arg(itemKey).arg(Dock::Efficient);

    return m_proxyInter->getValue(this, key, 4).toInt();
}

void WirelessCastingPlugin::setSortKey(const QString &itemKey, const int order)
{
    const QString key = QString("pos_%1_%2").arg(itemKey).arg(Dock::Efficient);

    m_proxyInter->saveValue(this, key, order);
}

void WirelessCastingPlugin::refreshIcon(const QString &itemKey)
{

}

void WirelessCastingPlugin::pluginSettingsChanged()
{

}

} // namespace wirelesscasting
} // namespace dde
