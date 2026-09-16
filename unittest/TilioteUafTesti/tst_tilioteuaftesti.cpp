/*
   Regression for GitHub #1368 / PR #1446.

   Repro A:
     Uusi tosite -> Siirto -> Laskun maksu -> Tililtä -> Sulje
     suljeNappi: reject() (WA_DeleteOnClose -> deleteLater) then tyhjenna().
     Must not destroy TilioteKirjaaja under a nested processEvents().

   Repro B:
     Pending destroy of TilioteApuri must not run inside teeReset() —
     same timing hazard as KirjausWg::tallennettu -> type change ->
     deleteLater apuri_ while reset is on the stack.

   Repro C (#1446 follow-up, attachments):
     QPdfDocument reads LiitteetModel's QBuffer lazily, and that buffer points
     straight into the CacheLiite bytes. Switching or dropping the shown
     attachment must close the document first, or the PDF view keeps reading
     freed cache data.

   These tests must complete without crashing after the fix.
*/
#include <QtTest>
#include <QApplication>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QFile>
#include <QDir>
#include <QDate>
#include <QTimer>
#include <QEventLoop>
#include <QEvent>
#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QPageSize>
#include <QPdfDocument>
#include <QPdfWriter>

#include "db/kirjanpito.h"
#include "kieli/kielet.h"
#include "model/tosite.h"
#include "db/tositetyyppimodel.h"
#include "sqlite/sqlitemodel.h"
#include "apuri/siirtoapuri.h"
#include "apuri/tiliote/tiliotekirjaaja.h"
#include "apuri/tiliote/tilioteapuri.h"
#include "liite/liitteetmodel.h"
#include "liite/liitecache.h"
#include "liite/cacheliite.h"

namespace {

QByteArray teePdf()
{
    QByteArray tavut;
    QBuffer puskuri(&tavut);
    puskuri.open(QIODevice::WriteOnly);
    QPdfWriter kirjoittaja(&puskuri);
    kirjoittaja.setPageSize(QPageSize(QPageSize::A4));
    QPainter maalari(&kirjoittaja);
    maalari.drawText(100, 100, QStringLiteral("Liitetesti"));
    maalari.end();
    puskuri.close();
    return tavut;
}

QByteArray teeJpg()
{
    QImage kuva(16, 16, QImage::Format_RGB32);
    kuva.fill(Qt::white);

    QByteArray tavut;
    QBuffer puskuri(&tavut);
    puskuri.open(QIODevice::WriteOnly);
    kuva.save(&puskuri, "JPG");
    puskuri.close();
    return tavut;
}

}

class TilioteUafTesti : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    /** Repro A — Siirto -> Laskun maksu -> Tililtä -> Sulje */
    void siirto_laskunmaksu_tililta_sulje();

    /**
     * Repro B — pending delete must not UAF through teeReset().
     * Queues destroy before reset(); after the fix, teeReset has no
     * processEvents so destroy runs only after reset returns.
     */
    void tiliote_teeReset_pending_delete_safe();

    /**
     * vanhene() must drop child→apuri connections (TilioteKirjaaja::rejected)
     * so a queued dialog close cannot write the retired apuri into Tosite.
     */
    void tiliote_vanhene_katkaisee_lapsen_signaalit();

    /** Repro C — switching PDF → image must detach QPdfDocument. */
    void liite_vaihto_irrottaa_pdf_dokumentin();

    /** Repro C — clearing / destroying the model must detach it too. */
    void liite_tyhjennys_irrottaa_pdf_dokumentin();

    /**
     * Same CacheLiite* under two IDs (duplicate upload). tyhjenna() used to
     * delete that pointer twice when opening another book.
     */
    void liitecache_tyhjenna_ei_tuplapoista_samaa_osoitinta();

private:
    QString tiedostoPolku_;
};

void TilioteUafTesti::initTestCase()
{
    Kielet::alustaKielet(":/testidata/tulkki.json");
    kp()->asetaInstanssi(new Kirjanpito());
}

void TilioteUafTesti::init()
{
    tiedostoPolku_ = QDir::temp().absoluteFilePath("tiliote_uaf_testi.kitsas");
    QFile::remove(tiedostoPolku_);
    QVERIFY(QFile::copy(":/testidata/oy.kitsas", tiedostoPolku_));
    QVERIFY(QFile::setPermissions(tiedostoPolku_,
                                  QFileDevice::WriteUser | QFileDevice::ReadUser));
    QVERIFY(kp()->avaaTietokanta(tiedostoPolku_, false));
}

void TilioteUafTesti::cleanup()
{
    if (kp()->sqlite())
        kp()->sqlite()->sulje();
    QFile::remove(tiedostoPolku_);
}

void TilioteUafTesti::siirto_laskunmaksu_tililta_sulje()
{
    Tosite tosite;
    tosite.asetaPvm(QDate(2020, 1, 15));
    tosite.asetaTyyppi(TositeTyyppi::SIIRTO);

    SiirtoApuri siirto(nullptr, &tosite);

    auto *kirjaaja = new TilioteKirjaaja(&siirto);
    kirjaaja->setAttribute(Qt::WA_DeleteOnClose);
    kirjaaja->show();
    QTest::qWait(50);
    qApp->processEvents();

    auto *ylaTab = kirjaaja->findChild<QTabBar *>("ylaTab");
    QVERIFY(ylaTab);
    QCOMPARE(ylaTab->count(), 2);
    ylaTab->setCurrentIndex(1); // TILILTA
    qApp->processEvents();

    QPointer<TilioteKirjaaja> elossa(kirjaaja);
    auto *sulje = kirjaaja->findChild<QPushButton *>("suljeNappi");
    QVERIFY(sulje);

    // reject() + tyhjenna(); deleteLater must wait until the click stack unwinds.
    sulje->click();

    QTest::qWait(300);
    qApp->processEvents(QEventLoop::AllEvents);

    QVERIFY2(!elossa || !elossa->isVisible(),
             "Sulje should close/delete the dialog without crashing");
}

void TilioteUafTesti::tiliote_teeReset_pending_delete_safe()
{
    Tosite tosite;
    tosite.asetaPvm(QDate(2020, 1, 15));
    tosite.asetaTyyppi(TositeTyyppi::TILIOTE);

    QWidget host;
    auto *apuri = new TilioteApuri(&host, &tosite);
    QPointer<TilioteApuri> elossa(apuri);
    apuri->show();
    qApp->processEvents(QEventLoop::AllEvents);

    // Same hazard as tallennettu -> type change destroying the apuri while
    // reset is active: queue destroy before reset().
    QTimer::singleShot(0, qApp, [apuri]() {
        delete apuri;
    });

    // Must return without UAF even if a delete is already queued.
    apuri->reset();
    QVERIFY2(elossa,
             "teeReset must not drain a pending delete via processEvents");

    qApp->processEvents(QEventLoop::AllEvents);
    QVERIFY2(!elossa,
             "Queued destroy should run only after reset has returned");
}

void TilioteUafTesti::tiliote_vanhene_katkaisee_lapsen_signaalit()
{
    Tosite tosite;
    tosite.asetaPvm(QDate(2020, 1, 15));
    tosite.asetaTyyppi(TositeTyyppi::TILIOTE);
    tosite.asetaKumppani(QStringLiteral("SentinelOy"));

    QWidget host;
    auto *apuri = new TilioteApuri(&host, &tosite);
    QPointer<TilioteApuri> elossa(apuri);
    apuri->show();
    qApp->processEvents(QEventLoop::AllEvents);

    auto *kirjaaja = apuri->findChild<TilioteKirjaaja *>();
    QVERIFY(kirjaaja);

    apuri->vanhene();
    QCOMPARE(apuri->tositteelle(), false);

    // Would call tositteelle() → asetaKumppani({}) if the connection remained.
    kirjaaja->reject();
    QCOMPARE(tosite.kumppaninimi(), QStringLiteral("SentinelOy"));

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(50);
    QVERIFY2(!elossa, "vanhene() deleteLater should destroy the apuri");
}

void TilioteUafTesti::liite_vaihto_irrottaa_pdf_dokumentin()
{
    LiitteetModel liitteet;
    liitteet.asetaInteraktiiviseksi(true);

    QVERIFY(liitteet.lisaa(teePdf(), QStringLiteral("testi.pdf")));
    QTRY_COMPARE(liitteet.pdfDocument()->status(), QPdfDocument::Status::Ready);

    // Kuvaliitteeseen vaihtaminen: dokumentti ei saa jäädä lukemaan
    // pdf-liitteen tavuja, jotka voivat vapautua välimuistista.
    QVERIFY(liitteet.lisaa(teeJpg(), QStringLiteral("testi.jpg")));
    QCOMPARE(liitteet.naytettavaIndeksi(), 1);
    QCOMPARE(liitteet.pdfDocument()->status(), QPdfDocument::Status::Null);

    // Ja takaisin pdf:ään
    liitteet.nayta(0);
    QTRY_COMPARE(liitteet.pdfDocument()->status(), QPdfDocument::Status::Ready);
}

void TilioteUafTesti::liite_tyhjennys_irrottaa_pdf_dokumentin()
{
    {
        LiitteetModel liitteet;
        liitteet.asetaInteraktiiviseksi(true);

        QVERIFY(liitteet.lisaa(teePdf(), QStringLiteral("testi.pdf")));
        QTRY_COMPARE(liitteet.pdfDocument()->status(), QPdfDocument::Status::Ready);

        liitteet.clear();
        QCOMPARE(liitteet.rowCount(), 0);
        QCOMPARE(liitteet.pdfDocument()->status(), QPdfDocument::Status::Null);
    }

    // Mallin tuhoaminen pdf-liite näkyvillä ei saa lukea vapautettua dataa
    auto *liitteet = new LiitteetModel();
    liitteet->asetaInteraktiiviseksi(true);
    QVERIFY(liitteet->lisaa(teePdf(), QStringLiteral("testi.pdf")));
    QTRY_COMPARE(liitteet->pdfDocument()->status(), QPdfDocument::Status::Ready);
    delete liitteet;

    qApp->processEvents(QEventLoop::AllEvents);
}

void TilioteUafTesti::liitecache_tyhjenna_ei_tuplapoista_samaa_osoitinta()
{
    LiiteCache *cache = kp()->liiteCache();
    QVERIFY(cache);

    auto *sama = new CacheLiite();
    sama->setData(QByteArrayLiteral("tupla"));

    cache->lisaaTallennettu(11, sama);
    cache->lisaaTallennettu(22, sama);
    QCOMPARE(cache->maara(), 1);

    // Vanha hash-muoto: sama osoitin kahdella avaimella.
    cache->liitteet_.insert(11, sama);
    cache->liitteet_.insert(22, sama);
    QCOMPARE(cache->maara(), 2);

    cache->tyhjenna();
    QCOMPARE(cache->maara(), 0);
}

QTEST_MAIN(TilioteUafTesti)
#include "tst_tilioteuaftesti.moc"
