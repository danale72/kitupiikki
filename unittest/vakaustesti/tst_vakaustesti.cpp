/*
   Copyright (C) 2026 Alexei Danilov

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include <QtTest>
#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QPointer>
#include <QSignalSpy>

#include "apuri/apuririvi.h"
#include "apuri/tiliote/laskutaulutilioteproxylla.h"
#include "apuri/tiliote/tilioteapuri.h"
#include "apuri/tiliote/tiliotekirjaaja.h"
#include "apuri/tiliote/tiliotekirjausrivi.h"
#include "apuri/tiliote/tiliotemodel.h"
#include "db/kirjanpito.h"
#include "db/tositetyyppimodel.h"
#include "kieli/kielet.h"
#include "kirjaus/kirjauswg.h"
#include "liite/liitteetmodel.h"
#include "sqlite/sqlitemodel.h"
#include "model/euro.h"
#include "model/laskutaulumodel.h"
#include "model/tosite.h"

class TestLaskuTaulu : public LaskuTauluTilioteProxylla
{
public:
    using LaskuTauluTilioteProxylla::LaskuTauluTilioteProxylla;

    void syota(const QVariantList& lista)
    {
        QVariant data = lista;
        tietoSaapuu(&data);
    }
};

class VakausTesti : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void liitteet_tyhjaMalliEiKaadu();
    void liitteet_pdfinVaihtoJaPoistoEiKaadu();
    void tiliote_tyhjaTuontiEiIlmoitaRiveja();
    void tiliote_tuontiIlmoittaaLisatytRivit();
    void laskutaulu_tyhjaPaivitysEiKaadu();
    void tiliotekirjaaja_ajastinTuhotaanTurvallisesti();
    void kirjaus_apurinVaihtoEiKaadu();

private:
    static QByteArray teePdf();

    QString tiedosto_;
};

void VakausTesti::initTestCase()
{
    static int argc = 1;
    static char arg0[] = "vakaustesti";
    static char* argv[] = { arg0, nullptr };

    auto* app = new QApplication(argc, argv);
    app->setOrganizationName(QStringLiteral("Kiswas vakaustesti"));
    app->setApplicationName(QStringLiteral("vakaustesti"));
    Kielet::alustaKielet(QStringLiteral(":/tr/tulkki.json"));
    kp()->asetaInstanssi(new Kirjanpito());
}

void VakausTesti::init()
{
    tiedosto_ = QDir::temp().filePath(QStringLiteral("vakaustesti.kitsas"));
    QFile::remove(tiedosto_);
    QVERIFY(QFile::copy(QStringLiteral(":/testidata/oy.kitsas"), tiedosto_));
    QFile::setPermissions(tiedosto_, QFileDevice::WriteUser | QFileDevice::ReadUser);
    QVERIFY(kp()->avaaTietokanta(tiedosto_));
}

void VakausTesti::cleanup()
{
    kp()->sqlite()->sulje();
    QFile::remove(tiedosto_);
}

QByteArray VakausTesti::teePdf()
{
    QByteArray tavut;
    QBuffer puskuri(&tavut);
    if (!puskuri.open(QIODevice::WriteOnly))
        return {};
    QPdfWriter kirjoitin(&puskuri);
    kirjoitin.setPageSize(QPageSize(QPageSize::A4));
    kirjoitin.setResolution(72);
    QPainter piirtaja(&kirjoitin);
    piirtaja.drawText(72, 72, QStringLiteral("vakaus"));
    piirtaja.end();
    puskuri.close();
    return tavut;
}

void VakausTesti::liitteet_tyhjaMalliEiKaadu()
{
    LiitteetModel malli;
    QCOMPARE(malli.rowCount(), 0);
    QCOMPARE(malli.data(QModelIndex()), QVariant());

    malli.nayta(-1);
    malli.nayta(4);
    QVERIFY(malli.sisalto() == nullptr);
    QVERIFY(!malli.naytettava().isValid());

    malli.lataa(QVariantList());
    QCOMPARE(malli.rowCount(), 0);
    QCOMPARE(malli.naytettavaIndeksi(), -1);

    malli.clear();
    QCOMPARE(malli.rowCount(), 0);
    QVERIFY(malli.sisalto() == nullptr);
}

void VakausTesti::liitteet_pdfinVaihtoJaPoistoEiKaadu()
{
    const QByteArray pdf = teePdf();
    QVERIFY(pdf.startsWith("%PDF"));

    auto* malli = new LiitteetModel();
    malli->asetaInteraktiiviseksi(true);

    QSignalSpy pdfSpy(malli, &LiitteetModel::naytaPdf);
    bool poistettu = false;
    connect(malli, &LiitteetModel::naytaPdf, malli, [malli, &poistettu] {
        if (!poistettu && malli->rowCount() > 0) {
            poistettu = true;
            malli->poista(0);
        }
    });

    QVERIFY(malli->lisaa(pdf, QStringLiteral("ensimmainen.pdf")));
    QCOMPARE(pdfSpy.count(), 1);
    QCOMPARE(malli->rowCount(), 0);
    QVERIFY(malli->sisalto() == nullptr);
    QVERIFY(!malli->naytettava().isValid());

    QTRY_VERIFY_WITH_TIMEOUT(
        malli->pdfDocument()->status() == QPdfDocument::Status::Ready
            || malli->pdfDocument()->status() == QPdfDocument::Status::Error,
        3000);

    QVERIFY(malli->lisaa(pdf, QStringLiteral("toinen.pdf")));
    QVERIFY(malli->lisaa(pdf, QStringLiteral("kolmas.pdf")));
    QCOMPARE(malli->rowCount(), 2);
    malli->nayta(0);
    malli->nayta(1);
    QCoreApplication::processEvents();

    malli->clear();
    QCOMPARE(malli->rowCount(), 0);
    QVERIFY(malli->sisalto() == nullptr);

    delete malli;
    QCoreApplication::processEvents();
}

void VakausTesti::tiliote_tyhjaTuontiEiIlmoitaRiveja()
{
    TilioteModel tiliote(nullptr, kp());
    QSignalSpy lisays(&tiliote, &TilioteModel::rowsInserted);

    tiliote.tuo(QVariantList());
    QCOMPARE(tiliote.rowCount(), 0);
    QCOMPARE(lisays.count(), 0);

    tiliote.lisaaRivi(QDate(2020, 1, 15));
    QCOMPARE(tiliote.rowCount(), 1);
    QVERIFY(tiliote.rivi(0).summa() == Euro::Zero);

    tiliote.tuo(QVariantList());
    QCOMPARE(tiliote.rowCount(), 0);
}

void VakausTesti::tiliote_tuontiIlmoittaaLisatytRivit()
{
    TilioteModel tiliote(nullptr, kp());
    QSignalSpy lisays(&tiliote, &TilioteModel::rowsInserted);

    QVariantMap rivi;
    rivi.insert(QStringLiteral("pvm"), QDate(2020, 1, 15));
    rivi.insert(QStringLiteral("euro"), QStringLiteral("12.50"));
    rivi.insert(QStringLiteral("selite"), QStringLiteral("testi"));
    rivi.insert(QStringLiteral("tili"), 1910);

    tiliote.tuo(QVariantList{ rivi });
    QCOMPARE(tiliote.rowCount(), 1);
    QCOMPARE(lisays.count(), 1);
    QCOMPARE(lisays.at(0).at(1).toInt(), 0);
    QCOMPARE(lisays.at(0).at(2).toInt(), 0);

    tiliote.tuo(QVariantList{ rivi, rivi });
    QCOMPARE(tiliote.rowCount(), 3);
    QCOMPARE(lisays.count(), 2);
    QCOMPARE(lisays.at(1).at(1).toInt(), 1);
    QCOMPARE(lisays.at(1).at(2).toInt(), 2);

    tiliote.tuo(QVariantList());
    QCOMPARE(tiliote.rowCount(), 3);
    QCOMPARE(lisays.count(), 2);
}

void VakausTesti::laskutaulu_tyhjaPaivitysEiKaadu()
{
    TilioteModel tiliote(nullptr, kp());
    TestLaskuTaulu laskut(nullptr, &tiliote);
    QSignalSpy muuttui(&laskut, &TestLaskuTaulu::dataChanged);

    laskut.paivitaSuoritukset();
    QCOMPARE(laskut.rowCount(), 0);
    QCOMPARE(muuttui.count(), 0);

    laskut.syota(QVariantList());
    QCOMPARE(laskut.rowCount(), 0);
    QCOMPARE(muuttui.count(), 0);

    QVariantMap era;
    era.insert(QStringLiteral("id"), 7);
    QVariantMap tapahtuma;
    tapahtuma.insert(QStringLiteral("pvm"), QDate(2020, 1, 15));
    tapahtuma.insert(QStringLiteral("euro"), QStringLiteral("40.00"));
    tapahtuma.insert(QStringLiteral("tili"), 1910);
    tapahtuma.insert(QStringLiteral("era"), era);
    tiliote.tuo(QVariantList{ tapahtuma });

    QVariantMap lasku;
    lasku.insert(QStringLiteral("avoin"), QStringLiteral("100.00"));
    lasku.insert(QStringLiteral("eraid"), 7);
    lasku.insert(QStringLiteral("summa"), QStringLiteral("100.00"));
    lasku.insert(QStringLiteral("pvm"), QDate(2020, 1, 10));
    laskut.syota(QVariantList{ lasku });

    QCOMPARE(laskut.rowCount(), 1);
    QVERIFY(muuttui.count() > 0);
    QCOMPARE(laskut.data(laskut.index(0, LaskuTauluModel::MAKSAMATTA), Qt::EditRole).toLongLong(), 6000LL);
}

void VakausTesti::tiliotekirjaaja_ajastinTuhotaanTurvallisesti()
{
    Tosite tosite;
    tosite.asetaPvm(QDate(2020, 1, 15));

    {
        TilioteApuri apuri(nullptr, &tosite);
        auto* kirjaaja = apuri.findChild<TilioteKirjaaja*>();
        QVERIFY(kirjaaja);
        apuri.model()->lisaaRivi(QDate(2020, 1, 15));
        kirjaaja->muokkaaRivia(0);
        QCoreApplication::processEvents();
    }
    QCoreApplication::processEvents();

    auto* apuri = new TilioteApuri(nullptr, &tosite);
    auto* kirjaaja = apuri->findChild<TilioteKirjaaja*>();
    QVERIFY(kirjaaja);
    QPointer<TilioteKirjaaja> elossa = kirjaaja;
    apuri->model()->lisaaRivi(QDate(2020, 1, 15));
    kirjaaja->muokkaaRivia(0);
    QVERIFY(elossa);
    delete apuri;
    QCoreApplication::processEvents();
    QVERIFY(elossa.isNull());
}

void VakausTesti::kirjaus_apurinVaihtoEiKaadu()
{
    auto* kirjaus = new KirjausWg(nullptr);
    kirjaus->tosite()->asetaTyyppi(TositeTyyppi::TULO);
    QPointer<ApuriWidget> tulo = kirjaus->apuri();
    QVERIFY(tulo);

    kirjaus->tosite()->asetaTyyppi(TositeTyyppi::TILIOTE);
    QVERIFY(tulo);
    QVERIFY(kirjaus->apuri() != tulo.data());

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(tulo.isNull());
    QVERIFY(kirjaus->apuri());

    QPointer<ApuriWidget> tiliote = kirjaus->apuri();
    kirjaus->tosite()->asetaTyyppi(TositeTyyppi::SIIRTO);
    QVERIFY(tiliote);
    delete kirjaus;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(tiliote.isNull());
}

QTEST_APPLESS_MAIN(VakausTesti)

#include "tst_vakaustesti.moc"
