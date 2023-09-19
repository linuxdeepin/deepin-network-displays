#ifndef CASTINGMODULE_H
#define CASTINGMODULE_H

#include <interface/moduleinterface.h>
#include <QObject>

using namespace DCC_NAMESPACE;
class WirelessCasting;
class WirelessCastingModel;
class CastingModule : public QObject, public ModuleInterface
{
    Q_OBJECT

    Q_PLUGIN_METADATA(IID ModuleInterface_iid FILE "../casting.json")
    Q_INTERFACES(DCC_NAMESPACE::ModuleInterface)
public:
    CastingModule(QObject *parent = nullptr);
    ~CastingModule();
    virtual void preInitialize(bool sync = false, FrameProxyInterface::PushType = FrameProxyInterface::PushType::Normal) override;
    virtual void initialize() override;
    virtual const QString name() const override;
    virtual const QString displayName() const override;
    virtual QString translationPath() const override { return QStringLiteral(":/translations/Casting_%1.ts");}
    virtual void active() override;
    virtual void deactive() override;
    virtual int load(const QString &path) override;
    QStringList availPage() const override;
    virtual QString path() const override { return DISPLAY; }
    virtual QWidget *moduleWidget() override;
    virtual void addChildPageTrans() const override;

private:
    void initSearchData() override;
    void contentDistroy(QObject *obj);
    WirelessCasting *m_content;
    WirelessCastingModel *m_model;
};

#endif // CASTINGMODULE_H
