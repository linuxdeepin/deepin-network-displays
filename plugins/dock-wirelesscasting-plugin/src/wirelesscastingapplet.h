#ifndef WIRELESSCASTINGAPPLET_H
#define WIRELESSCASTINGAPPLET_H

#include <QStyleOption>
#include <QStylePainter>
#include <QToolButton>
#include <QWidget>
#include <QVBoxLayout>
#include <QScroller>
#include <QScrollArea>
#include <DDBusSender>

namespace dde {
namespace wirelesscasting {
class DisplaySettingButton : public QToolButton
{
public:
    DisplaySettingButton(const QIcon &icon, const QString &text, QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setIcon(icon);
        setText(text);
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(40);
        setAutoRaise(false);
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QStylePainter p(this);
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        p.drawComplexControl(QStyle::CC_ToolButton, opt);
        QToolButton::paintEvent(e);
    }

    void initStyleOption(QStyleOptionToolButton *option) const
    {
        QToolButton::initStyleOption(option);
        if (option->state.testFlag(QStyle::State_MouseOver) && option->state.testFlag(QStyle::State_Raised)) {
            option->palette.setBrush(QPalette::Button, option->palette.highlight());
            option->palette.setBrush(QPalette::Highlight, option->palette.highlightedText());
            option->state.setFlag(QStyle::State_Sunken, true);
            option->state.setFlag(QStyle::State_Raised, false);
            option->state.setFlag(QStyle::State_MouseOver, false);
        } else {
            QColor color = option->palette.brightText().color();
            option->palette.setBrush(QPalette::ButtonText, color);
            color.setAlphaF(0.05);
            option->palette.setBrush(QPalette::Button, color);
            option->state.setFlag(QStyle::State_Sunken, true);
            option->state.setFlag(QStyle::State_Raised, false);
            option->state.setFlag(QStyle::State_MouseOver, true);
        }
        option->state.setFlag(QStyle::State_HasFocus, false);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        DDBusSender()
                .service("com.deepin.dde.ControlCenter")
                .path("/com/deepin/dde/ControlCenter")
                .interface("com.deepin.dde.ControlCenter")
                .method(QString("ShowModule"))
                .arg(QString("display"))
                .call();
        QToolButton::mouseReleaseEvent(e);
    }
};

class WirelessCastingApplet : public QWidget
{
public:
    explicit WirelessCastingApplet(QWidget *wirelessCasting, QWidget *parent = nullptr)
        : QWidget{parent}
        , m_displaySetting(new DisplaySettingButton(QIcon::fromTheme("open-display-settings"), tr("Display settings")))
    {
        QPalette scrollAreaBackground;
        auto m_scrollArea = new QScrollArea(this);
        m_scrollArea->setWidgetResizable(true);
        m_scrollArea->setWidget(wirelessCasting);
        m_scrollArea->setFrameShape(QFrame::NoFrame);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setAutoFillBackground(true);
        m_scrollArea->viewport()->setAutoFillBackground(true);
        scrollAreaBackground.setColor(QPalette::Background, Qt::transparent);
        m_scrollArea->setAutoFillBackground(true);
        m_scrollArea->setPalette(scrollAreaBackground);
        m_scrollArea->setMinimumHeight(280);

        QScroller::grabGesture(m_scrollArea->viewport(), QScroller::LeftMouseButtonGesture);
        QScroller *scroller = QScroller::scroller(m_scrollArea);
        QScrollerProperties sp;
        sp.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
        scroller->setScrollerProperties(sp);

        QVBoxLayout *vLay = new QVBoxLayout(this);
        vLay->setMargin(10);
        vLay->addWidget(m_scrollArea);
        vLay->addWidget(m_displaySetting);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setLayout(vLay);
        setFixedSize(330,360);
    }
signals:

private:
    DisplaySettingButton *m_displaySetting;

};
} // namespace wirelesscasting
} // namespace dde
#endif // WIRELESSCASTINGAPPLET_H
