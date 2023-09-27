#ifndef WIRELESSCASTINGITEM_H
#define WIRELESSCASTINGITEM_H

#include <QWidget>
#define WIRELESS_CASTING_KEY "wireless-casting-item-key"

class QPixmap;
class QLabel;
class WirelessCastingModel;
namespace dde {
namespace wirelesscasting {
class QuickPanelWidget;
class WirelessCastingApplet;

class WirelessCastingItem : public QWidget
{
    Q_OBJECT
public:
    WirelessCastingItem(QWidget *parent = nullptr);
    ~WirelessCastingItem() override;

    QWidget *quickPanelWidget() const;
    QWidget *trayIcon();
    QWidget *appletWidget() const;
    QLabel *tips() const{ return m_tips; }
    bool canCasting() const{ return m_canCasting; }

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

public Q_SLOTS:

signals:
    void canCastingChanged(bool canCasting);
    void requestExpand();
private:
    void init();

    WirelessCastingModel *m_model;
    QuickPanelWidget *m_quickPanel;
    WirelessCastingApplet *m_appletWidget;
    QIcon *m_trayIcon;
    QLabel *m_tips;
    bool m_canCasting;
};

} // namespace wirelesscasting
} // namespace dde
#endif // WIRELESSCASTINGITEM_H
