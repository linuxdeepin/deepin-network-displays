#include "castingmodule.h"
#include "wirelesscasting.h"

#include <QNetworkConfigurationManager>
#include <QNetworkSession>

CastingModule::CastingModule(QObject *parent)
    : QObject(parent)
    , m_content(nullptr)
{

}

CastingModule::~CastingModule()
{

}

void CastingModule::preInitialize(bool sync, FrameProxyInterface::PushType)
{
    m_model = new WirelessCastingModel(this);
    addChildPageTrans();
    initSearchData();
}

void CastingModule::initialize()
{

}

const QString CastingModule::name() const
{
    return QString(tr("Casting"));
}

const QString CastingModule::displayName() const
{
    return QString("Casting");
}

void CastingModule::active()
{

}

void CastingModule::deactive()
{

}

int CastingModule::load(const QString &path)
{
    Q_UNUSED(path);
    return 0;
}

QStringList CastingModule::availPage() const
{
    return QStringList();
}

QWidget *CastingModule::moduleWidget()
{
    if (nullptr == m_content) {
        m_content = new WirelessCasting(m_model);
        connect(m_content, &WirelessCasting::destroyed, this, &CastingModule::contentDistroy);
    }
    return m_content;
}

void CastingModule::addChildPageTrans() const
{
    m_frameProxy->addChildPageTrans("Wireless Casting", tr("Wireless Casting"));
}

void CastingModule::initSearchData()
{
    m_frameProxy->setWidgetVisible(tr("Display"), tr("Wireless Casting"), true);
    m_frameProxy->updateSearchData("Display");
}

void CastingModule::contentDistroy(QObject *obj)
{
    m_content = nullptr;
}
