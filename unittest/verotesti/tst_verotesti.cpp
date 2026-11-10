/*
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/
#include "vero/veroapu.h"
#include "vero/veroasetukset.h"
#include "vero/veroclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

/*
 * Vero-integraation testit. Kaikki testiaineisto on keksittyä: ei oikeita varmenteita,
 * avaimia, ohjelmatunnisteita eikä oikeita asiakkaita.
 */

namespace {

/// Yksinkertainen HTTP-palvelin, joka vastaa ensimmäiseen pyyntöön ja tallentaa sen
struct Palvelin {
    QTcpServer server;
    int status = 200;
    QByteArray vastaus = "{}";
    /// Ilmoitetaan vastauksen pituudeksi enemmän kuin lähetetään ja katkaistaan yhteys
    bool katkaise = false;

    QByteArray metodi;
    QByteArray polku;
    QMap<QByteArray, QByteArray> otsikot;
    QByteArray runko;
    int pyyntoja = 0;

    bool kaynnista()
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            QTcpSocket* socket = server.nextPendingConnection();
            auto puskuri = QSharedPointer<QByteArray>::create();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, puskuri] {
                puskuri->append(socket->readAll());
                const int paaLoppu = puskuri->indexOf("\r\n\r\n");
                if (paaLoppu < 0)
                    return;

                const QList<QByteArray> rivit = puskuri->left(paaLoppu).split('\n');
                const QList<QByteArray> ensimmainen = rivit.first().trimmed().split(' ');
                QMap<QByteArray, QByteArray> kentat;
                for (int i = 1; i < rivit.count(); i++) {
                    const int kaksoispiste = rivit.at(i).indexOf(':');
                    if (kaksoispiste > 0)
                        kentat.insert(rivit.at(i).left(kaksoispiste).trimmed().toLower(),
                                      rivit.at(i).mid(kaksoispiste + 1).trimmed());
                }
                const int pituus = kentat.value("content-length").toInt();
                if (puskuri->size() < paaLoppu + 4 + pituus)
                    return;

                metodi = ensimmainen.value(0);
                polku = ensimmainen.value(1);
                otsikot = kentat;
                runko = puskuri->mid(paaLoppu + 4, pituus);
                pyyntoja++;

                QByteArray paketti = "HTTP/1.1 " + QByteArray::number(status) + " X\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: " + QByteArray::number(vastaus.size() + (katkaise ? 1000 : 0)) + "\r\n"
                        "Connection: close\r\n\r\n" + vastaus;
                socket->write(paketti);
                socket->disconnectFromHost();
            });
        });
        return server.listen(QHostAddress::LocalHost);
    }

    QUrl osoite() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    }
};

QVariantMap esimerkkiKoodit()
{
    return QVariantMap{
        {"301", 100000},    // 1000,00 e
        {"302", 5050},
        {"304", 1200},
        {"305", 2500},
        {"307", 30000},
        {"309", 77700},
        {"310", 1000},
        {"311", 2000},
        {"318", 1250},
        {"319", 4000},
        {"308", 99999},     // Maksettava vero lasketaan Verohallinnossa, ei ilmoiteta
        {"337", 1}          // Maksuperusteisuuden merkintä, ei rahamäärä
    };
}

}   // namespace

class VeroTesti : public QObject
{
    Q_OBJECT

private slots:
    void ytunnus_data();
    void ytunnus();

    void osoitteet();
    void ymparistoAvain();
    void otsikot();

    void ilmoitus_kenttienKohdistus();
    void ilmoitus_eiToimintaa();
    void ilmoitus_korvaava();
    void ilmoitus_maksuperusteisuusVainEnnen2025();
    void ilmoitus_lahtevaSanomaLapaiseeTarkistuksen();
    void validointi_virheet();
    void validointi_2025Rajoitukset();

    void kirjeet_jasennys();
    void kirjeet_vanhinPaivays();
    void verokaudet_jasennys();
    void tokenit_normalisoiAvaimet();
    void virheteksti();
    void viitteet();
    void viitteet_ymparistoErottaaTestinJaTuotannon();
    void tukemattomatKoodit_alarajahuojennus();
    void puuttuvatLiitteet_vainPuuttuvatNimet();
    void harjoitus_eiSaaKayttaaTuotantoa();
    void samaAlvIlmoitus_vertaaVakaatKentat();
    void epaonnistunutLahetys_sailyttaaHyvaksytyn();
    void lahetysVahvistus_huomaaUudenYrityksen();
    void onVarmaHttpHylkays_viisisataaEiOleHylkays();
    void onVerkkotasonVirhe_protokollavirheOnVerkkovirhe();
    void virheteksti_verkkovirheEnnenHttpTilaa();
    void rajattuPvm();

    void asetukset_erottavatYmparistot();
    void varmenne_puuttuvaTiedostoJaVirheellinenPem();
    void varmenne_rsaJaEcSekaVaaraAvain();

    void client_lahettaaOtsikotJaRungon();
    void client_httpVirheKulkeeVastauksena();
    void client_katkennutVastausEiOleOnnistunut();
    void client_verkkovirheEiOleHttpVastaus();
};

void VeroTesti::ytunnus_data()
{
    QTest::addColumn<QString>("syote");
    QTest::addColumn<QString>("odotettu");

    QTest::newRow("oikea") << "1234567-1" << "1234567-1";
    QTest::newRow("toinen oikea") << "0112038-9" << "0112038-9";
    QTest::newRow("tarkiste nolla") << "0000000-0" << "0000000-0";
    QTest::newRow("FI-etuliite") << "FI12345671" << "1234567-1";
    QTest::newRow("valilyonnit") << "  1234567 - 1 " << "1234567-1";
    QTest::newRow("ilman valiviivaa") << "12345671" << "1234567-1";
    QTest::newRow("pieni FI") << "fi12345671" << "1234567-1";
    QTest::newRow("vaara tarkiste") << "1234567-2" << "";
    QTest::newRow("jaannos yksi") << "0000006-0" << "";
    QTest::newRow("liian lyhyt") << "123456-1" << "";
    QTest::newRow("kirjaimia") << "ABCDEFG-1" << "";
    QTest::newRow("tyhja") << "" << "";
}

void VeroTesti::ytunnus()
{
    QFETCH(QString, syote);
    QFETCH(QString, odotettu);
    QCOMPARE(Vero::normalisoiYtunnus(syote), odotettu);
}

void VeroTesti::osoitteet()
{
    QCOMPARE(Vero::osoite(Vero::TESTI, Vero::FILE_VAT_RETURN).toString(),
             QStringLiteral("https://apitest.vero.fi/FIS/Return/SAT/Test/FileVATReturn/v2"));
    QCOMPARE(Vero::osoite(Vero::TUOTANTO, Vero::FILE_VAT_RETURN).toString(),
             QStringLiteral("https://api.vero.fi/Return/SAT/FileVATReturn/v2"));
    QCOMPARE(Vero::osoite(Vero::TESTI, Vero::GET_FILED_VAT_RETURN).toString(),
             QStringLiteral("https://apitest.vero.fi/FIS/Return/SAT/Test/GetFiledVATReturn/v2"));
    QCOMPARE(Vero::osoite(Vero::TUOTANTO, Vero::GET_VAT_PERIODS).toString(),
             QStringLiteral("https://api.vero.fi/Return/SAT/GetVATPeriods/v1"));
    QCOMPARE(Vero::osoite(Vero::TESTI, Vero::LIST_LETTERS).toString(),
             QStringLiteral("https://apitest.vero.fi/FIS/Customer/Letters/Test/ListLetters/v1"));
    QCOMPARE(Vero::osoite(Vero::TUOTANTO, Vero::GET_LETTER).toString(),
             QStringLiteral("https://api.vero.fi/Customer/Letters/GetLetter/v1"));
    QCOMPARE(Vero::osoite(Vero::TESTI, Vero::GETTOKEN).toString(),
             QStringLiteral("https://apitest.vero.fi/FIS/General/Authorization/Test/GetToken/v1"));
    QCOMPARE(Vero::osoite(Vero::TUOTANTO, Vero::GETTOKEN).toString(),
             QStringLiteral("https://api.vero.fi/General/Authorization/GetToken/v1"));

    // Testi- ja tuotantoosoitteet eivät saa mennä sekaisin
    for (int rajapinta = Vero::GETTOKEN; rajapinta <= Vero::FILE_VAT_RETURN; rajapinta++) {
        QVERIFY(Vero::osoite(Vero::TESTI, static_cast<Vero::Rajapinta>(rajapinta)).host() == "apitest.vero.fi");
        QVERIFY(Vero::osoite(Vero::TUOTANTO, static_cast<Vero::Rajapinta>(rajapinta)).host() == "api.vero.fi");
    }
}

void VeroTesti::ymparistoAvain()
{
    QCOMPARE(Vero::ymparistoAvaimesta(QStringLiteral("tuotanto")), Vero::TUOTANTO);
    QCOMPARE(Vero::ymparistoAvaimesta(QStringLiteral("testi")), Vero::TESTI);
    // Tuntematon tai puuttuva arvo ei saa valita tuotantoa
    QCOMPARE(Vero::ymparistoAvaimesta(QString()), Vero::TESTI);
    QCOMPARE(Vero::ymparistoAvaimesta(QStringLiteral("Tuotanto ")), Vero::TESTI);
    QCOMPARE(Vero::ymparistoAvain(Vero::TUOTANTO), QStringLiteral("tuotanto"));
    QCOMPARE(Vero::ymparistoAvain(Vero::TESTI), QStringLiteral("testi"));
}

void VeroTesti::otsikot()
{
    const Vero::Otsikot ilman = Vero::otsikot("id", "avain", "ua/1");
    QMap<QByteArray, QByteArray> kartta;
    for (const auto& pari : ilman)
        kartta.insert(pari.first, pari.second);
    QCOMPARE(kartta.value("Vero-SoftwareId"), QByteArray("id"));
    QCOMPARE(kartta.value("Vero-SoftwareKey"), QByteArray("avain"));
    QCOMPARE(kartta.value("User-Agent"), QByteArray("ua/1"));
    QCOMPARE(kartta.value("Accept"), QByteArray("application/json"));
    QVERIFY(!kartta.contains("Vero-AuthorizationToken"));

    const Vero::Otsikot pdf = Vero::otsikot("id", "avain", "ua/1", true, "token");
    kartta.clear();
    for (const auto& pari : pdf)
        kartta.insert(pari.first, pari.second);
    QCOMPARE(kartta.value("Accept"), QByteArray("application/pdf"));
    QCOMPARE(kartta.value("Vero-AuthorizationToken"), QByteArray("token"));
}

void VeroTesti::ilmoitus_kenttienKohdistus()
{
    const QVariantMap sanoma = Vero::fileVatReturn("1234567-1", QDate(2025, 6, 30), esimerkkiKoodit(),
                                                   "Maija Meikalainen", "0401234567");

    QCOMPARE(sanoma.value("BusinessId").toString(), QStringLiteral("1234567-1"));
    QCOMPARE(sanoma.value("FilingPeriod").toString(), QStringLiteral("2025-06-30"));
    QCOMPARE(sanoma.value("ReplacementReturn").toBool(), false);
    QVERIFY(!sanoma.contains("ReplacementReason"));
    QCOMPARE(sanoma.value("NoActivity").toBool(), false);
    QCOMPARE(sanoma.value("ContactDetails").toMap().value("FullName").toString(), QStringLiteral("Maija Meikalainen"));
    QCOMPARE(sanoma.value("ContactDetails").toMap().value("PhoneNumber").toString(), QStringLiteral("0401234567"));

    const QVariantMap tiedot = sanoma.value("VATDetails").toMap();
    const QVariantMap myynti = tiedot.value("VATOnDomesticSalesByTaxRate").toMap();
    QCOMPARE(myynti.value("HighVATRate").toDouble(), 1000.0);
    QCOMPARE(myynti.value("MediumVATRate").toDouble(), 50.5);
    QCOMPARE(myynti.value("LowVATRate").toDouble(), 0.0);

    const QVariantMap ostot = tiedot.value("VATOnPurchasesAndImports").toMap();
    QCOMPARE(ostot.value("ImportOfGoodsOutsideEU").toDouble(), 12.0);
    QCOMPARE(ostot.value("GoodsFromEUMemberStates").toDouble(), 25.0);
    QCOMPARE(ostot.value("ServicesFromEUMemberStates").toDouble(), 0.0);
    QCOMPARE(ostot.value("ConstructionServicesAndScrapMetal").toDouble(), 12.5);

    QCOMPARE(tiedot.value("DeductibleVAT").toDouble(), 300.0);

    const QVariantMap kaupat = tiedot.value("SalesPurchasesImports").toMap();
    QCOMPARE(kaupat.value("ZeroVATRateTurnover").toDouble(), 777.0);
    QCOMPARE(kaupat.value("ImportsOfGoodsFromOutsideEU").toDouble(), 10.0);
    QCOMPARE(kaupat.value("SalesOfGoodsToEUMemberStates").toDouble(), 20.0);
    QCOMPARE(kaupat.value("SalesOfServicesToEUMemberStates").toDouble(), 0.0);
    QCOMPARE(kaupat.value("SalesOfConstructionServicesAndScrapMetal").toDouble(), 40.0);
    QCOMPARE(kaupat.value("PurchasesOfConstructionServicesAndScrapMetal").toDouble(), 0.0);

    // Verohallinnossa lasketut ja merkintäkoodit eivät päädy sanomaan
    const QByteArray json = QJsonDocument::fromVariant(sanoma).toJson();
    QVERIFY(!json.contains("99999"));
    QVERIFY(!json.contains("999.99"));

    // Jokainen koodi esiintyy kenttätaulukossa kerran ja kuuluu tunnettuun ryhmään
    QSet<int> nahdyt;
    for (const Vero::KoodiKentta& kentta : Vero::koodiKentat()) {
        QVERIFY(!nahdyt.contains(kentta.koodi));
        nahdyt.insert(kentta.koodi);
    }
    QVERIFY(!nahdyt.contains(308));
    QVERIFY(!nahdyt.contains(337));
}

void VeroTesti::ilmoitus_eiToimintaa()
{
    QVERIFY(!Vero::onkoToimintaa(QVariantMap()));
    QVERIFY(!Vero::onkoToimintaa(QVariantMap{{"301", 0}, {"307", 0}}));
    // Pelkkä lasketun veron koodi tai merkintä ei ole toimintaa
    QVERIFY(!Vero::onkoToimintaa(QVariantMap{{"308", 500}, {"337", 1}}));
    QVERIFY(Vero::onkoToimintaa(QVariantMap{{"307", 1}}));

    const QVariantMap sanoma = Vero::fileVatReturn("1234567-1", QDate(2025, 3, 31), QVariantMap(),
                                                   "Maija", "040");
    QCOMPARE(sanoma.value("NoActivity").toBool(), true);
    QVERIFY(!sanoma.contains("VATDetails"));
    QVERIFY(Vero::validoiFileVatReturn(sanoma).isEmpty());
}

void VeroTesti::ilmoitus_korvaava()
{
    const QVariantMap sanoma = Vero::fileVatReturn("1234567-1", QDate(2025, 6, 30), esimerkkiKoodit(),
                                                   "Maija", "040", true, "CLC");
    QCOMPARE(sanoma.value("ReplacementReturn").toBool(), true);
    QCOMPARE(sanoma.value("ReplacementReason").toString(), QStringLiteral("CLC"));
    QVERIFY(Vero::validoiFileVatReturn(sanoma).isEmpty());

    const QVariantMap ilmanSyyta = Vero::fileVatReturn("1234567-1", QDate(2025, 6, 30), esimerkkiKoodit(),
                                                       "Maija", "040", true, QString());
    QVERIFY(!Vero::validoiFileVatReturn(ilmanSyyta).isEmpty());
    const QVariantMap vaaraSyy = Vero::fileVatReturn("1234567-1", QDate(2025, 6, 30), esimerkkiKoodit(),
                                                     "Maija", "040", true, "XYZ");
    QVERIFY(!Vero::validoiFileVatReturn(vaaraSyy).isEmpty());
}

void VeroTesti::ilmoitus_maksuperusteisuusVainEnnen2025()
{
    const auto kaytossa = [](const QDate& kausi, bool maksuperusteinen) {
        const QVariantMap sanoma = Vero::fileVatReturn("1234567-1", kausi, esimerkkiKoodit(),
                                                       "Maija", "040", false, QString(), maksuperusteinen);
        return sanoma.value("VATDetails").toMap().value("VATOnCashBasis").toBool();
    };
    QVERIFY(kaytossa(QDate(2024, 12, 31), true));
    QVERIFY(!kaytossa(QDate(2024, 12, 31), false));
    // Vuoden 2025 alusta maksuperusteisuutta ei voi ilmoittaa, vaikka kirjanpidossa se olisi päällä
    QVERIFY(!kaytossa(QDate(2025, 1, 31), true));
    QVERIFY(!kaytossa(QDate(2026, 6, 30), true));
}

void VeroTesti::ilmoitus_lahtevaSanomaLapaiseeTarkistuksen()
{
    for (int vuosi : {2024, 2025, 2026}) {
        const QVariantMap sanoma = Vero::fileVatReturn("1234567-1", QDate(vuosi, 6, 30), esimerkkiKoodit(),
                                                       "Maija", "040", false, QString(), true);
        QVERIFY2(Vero::validoiFileVatReturn(sanoma).isEmpty(),
                 qPrintable(Vero::validoiFileVatReturn(sanoma).join("; ")));
    }
}

void VeroTesti::validointi_virheet()
{
    QVariantMap hyva = Vero::fileVatReturn("1234567-1", QDate(2025, 6, 30), esimerkkiKoodit(), "Maija", "040");
    QVERIFY(Vero::validoiFileVatReturn(hyva).isEmpty());

    QVariantMap vaaraYtunnus = hyva;
    vaaraYtunnus.insert("BusinessId", "12345671");
    QCOMPARE(Vero::validoiFileVatReturn(vaaraYtunnus).count(), 1);

    QVariantMap vaaraKausi = hyva;
    vaaraKausi.insert("FilingPeriod", "30.6.2025");
    QCOMPARE(Vero::validoiFileVatReturn(vaaraKausi).count(), 1);

    QVariantMap mahdotonKausi = hyva;
    mahdotonKausi.insert("FilingPeriod", "2025-02-30");
    QCOMPARE(Vero::validoiFileVatReturn(mahdotonKausi).count(), 1);

    const QVariantMap ilmanNimea = Vero::fileVatReturn("1234567-1", QDate(2025, 6, 30), esimerkkiKoodit(), "  ", "040");
    QCOMPARE(Vero::validoiFileVatReturn(ilmanNimea).count(), 1);

    const QVariantMap ilmanPuhelinta = Vero::fileVatReturn("1234567-1", QDate(2025, 6, 30), esimerkkiKoodit(), "Maija", "");
    QCOMPARE(Vero::validoiFileVatReturn(ilmanPuhelinta).count(), 1);

    QVariantMap ilmanTietoja = hyva;
    ilmanTietoja.remove("VATDetails");
    QVERIFY(!Vero::validoiFileVatReturn(ilmanTietoja).isEmpty());

    // Useampi virhe kerralla raportoidaan kaikki
    QVariantMap monta = vaaraYtunnus;
    monta.insert("ContactDetails", QVariantMap());
    QVERIFY(Vero::validoiFileVatReturn(monta).count() >= 3);

    QVERIFY(!Vero::validoiFileVatReturn(QVariantMap()).isEmpty());
}

void VeroTesti::validointi_2025Rajoitukset()
{
    QVariantMap sanoma = Vero::fileVatReturn("1234567-1", QDate(2025, 6, 30), esimerkkiKoodit(), "Maija", "040");

    QVariantMap tiedot = sanoma.value("VATDetails").toMap();
    tiedot.insert("VATOnCashBasis", true);
    sanoma.insert("VATDetails", tiedot);
    QVERIFY(!Vero::validoiFileVatReturn(sanoma).isEmpty());

    tiedot.insert("VATOnCashBasis", false);
    tiedot.insert("VATRelief", QVariantMap{{"FileVATRelief", true}});
    sanoma.insert("VATDetails", tiedot);
    QVERIFY(!Vero::validoiFileVatReturn(sanoma).isEmpty());

    // Vuonna 2024 sama on vielä sallittu
    QVariantMap vanha = Vero::fileVatReturn("1234567-1", QDate(2024, 6, 30), esimerkkiKoodit(), "Maija", "040", false, QString(), true);
    QVERIFY(Vero::validoiFileVatReturn(vanha).isEmpty());
}

void VeroTesti::kirjeet_jasennys()
{
    const QByteArray json = R"({
        "Letters": [
            {"LetterNumber": "L001", "LetterType": "A", "LetterNameFI": "Päätös", "LetterNameEN": "Decision", "PrintedDate": "2026-03-04"},
            {"LetterNumber": "L002", "PrintedDate": "2026-02-01"},
            {"LetterType": "ilman numeroa"}
        ]})";
    const QVariantMap vastaus = QJsonDocument::fromJson(json).object().toVariantMap();
    const QList<Vero::Kirje> lista = Vero::kirjeet(vastaus);

    // Ilman kirjenumeroa olevaa kirjettä ei voi hakea eikä tunnistaa, joten se ohitetaan
    QCOMPARE(lista.count(), 2);
    QCOMPARE(lista.at(0).numero, QStringLiteral("L001"));
    QCOMPARE(lista.at(0).tyyppi, QStringLiteral("A"));
    QCOMPARE(lista.at(0).nimiFi, QStringLiteral("Päätös"));
    QCOMPARE(lista.at(0).nimiEn, QStringLiteral("Decision"));
    QCOMPARE(lista.at(0).paivays, QDate(2026, 3, 4));
    QCOMPARE(lista.at(1).numero, QStringLiteral("L002"));

    QVERIFY(Vero::kirjeet(QVariantMap()).isEmpty());
}

void VeroTesti::kirjeet_vanhinPaivays()
{
    QList<Vero::Kirje> lista;
    QVERIFY(!Vero::vanhinPaivays(lista).isValid());

    Vero::Kirje a; a.numero = "A"; a.paivays = QDate(2026, 3, 4);
    Vero::Kirje b; b.numero = "B"; b.paivays = QDate(2026, 1, 9);
    Vero::Kirje c; c.numero = "C";      // Ei päivää
    lista << a << b << c;
    QCOMPARE(Vero::vanhinPaivays(lista), QDate(2026, 1, 9));
}

void VeroTesti::verokaudet_jasennys()
{
    const QByteArray json = R"({
        "FilingPeriod": [
            {"StartDate": "2025-01-01", "EndDate": "2025-01-31", "DueDate": "2025-03-12", "Frequency": "Monthly", "Status": "Processed"},
            {"StartDate": "2025-02-01", "Period": "2025-02-28", "Status": "Received"},
            {"Status": "Processed"}
        ]})";
    const QList<Vero::Verokausi> kaudet = Vero::verokaudet(QJsonDocument::fromJson(json).object().toVariantMap());

    QCOMPARE(kaudet.count(), 2);
    QCOMPARE(kaudet.at(0).alkaa, QDate(2025, 1, 1));
    QCOMPARE(kaudet.at(0).loppuu, QDate(2025, 1, 31));
    QCOMPARE(kaudet.at(0).erapaiva, QDate(2025, 3, 12));
    QCOMPARE(kaudet.at(0).tiheys, QStringLiteral("Monthly"));
    QVERIFY(kaudet.at(0).kasitelty());
    // EndDate puuttuu: kauden loppu otetaan Period-kentästä
    QCOMPARE(kaudet.at(1).loppuu, QDate(2025, 2, 28));
    // Vastaanotettu ilmoitus ei ole käsitelty (FileVATReturn on eräajo)
    QVERIFY(!kaudet.at(1).kasitelty());
}

void VeroTesti::tokenit_normalisoiAvaimet()
{
    const QByteArray json = R"({"Authorizations": [
        {"PrincipalIdentifier": "FI12345671", "AuthorizationToken": "t1"},
        {"PrincipalIdentifier": "0112038-9", "AuthorizationToken": "t2"},
        {"PrincipalIdentifier": "1111111-1", "AuthorizationToken": ""},
        {"PrincipalIdentifier": "", "AuthorizationToken": "t4"}
    ]})";
    const QMap<QString, QString> tokenit = Vero::tokenit(QJsonDocument::fromJson(json).object().toVariantMap());
    QCOMPARE(tokenit.count(), 2);
    QCOMPARE(tokenit.value("1234567-1"), QStringLiteral("t1"));
    QCOMPARE(tokenit.value("0112038-9"), QStringLiteral("t2"));
}

void VeroTesti::virheteksti()
{
    const QString verkko = Vero::virheteksti(0, QVariantMap(), "Connection refused");
    QVERIFY(verkko.contains("Connection refused"));

    const QString http = Vero::virheteksti(400, QVariantMap{{"ErrorCode", "E123"}, {"ErrorText", "Virheellinen kausi"}});
    QVERIFY(http.contains("400"));
    QVERIFY(http.contains("E123"));
    QVERIFY(http.contains("Virheellinen kausi"));

    // Vastaus ilman virhesanomaa ei saa kaatua
    QVERIFY(Vero::virheteksti(500, QVariantMap()).contains("500"));
}

void VeroTesti::viitteet()
{
    QCOMPARE(Vero::lahetettyAlvViite(Vero::TUOTANTO, QDate(2026, 6, 30)), QStringLiteral("FVR20260630"));
    QCOMPARE(Vero::saapunutAlvViite(Vero::TUOTANTO, QDate(2026, 6, 30)), QStringLiteral("GFV20260630"));
    QVERIFY(Vero::lahetettyAlvViite(Vero::TUOTANTO, QDate(2026, 6, 30)) != Vero::saapunutAlvViite(Vero::TUOTANTO, QDate(2026, 6, 30)));

    // Tositteen viite-haku poistaa muut kuin kirjain- ja numeromerkit, joten avaimen on säilyttävä sellaisenaan
    const QString kirje = Vero::kirjeViite(Vero::TUOTANTO, "L-0014 322/017");
    QCOMPARE(kirje, QStringLiteral("VKIL0014322017"));
    QCOMPARE(kirje.indexOf(QRegularExpression("\\W")), -1);
    QVERIFY(Vero::kirjeViite(Vero::TUOTANTO, QString(200, 'x')).length() <= 64);
    // Eri kirjenumerot eivät saa törmätä
    QVERIFY(Vero::kirjeViite(Vero::TUOTANTO, "L001") != Vero::kirjeViite(Vero::TUOTANTO, "L002"));
}

void VeroTesti::viitteet_ymparistoErottaaTestinJaTuotannon()
{
    const QDate kausi(2026, 6, 30);
    QCOMPARE(Vero::lahetettyAlvViite(Vero::TESTI, kausi), QStringLiteral("TFVR20260630"));
    QCOMPARE(Vero::saapunutAlvViite(Vero::TESTI, kausi), QStringLiteral("TGFV20260630"));
    QCOMPARE(Vero::kirjeViite(Vero::TESTI, "L-1"), QStringLiteral("TVKIL1"));

    // Testisanoma ei saa koskaan osua tuotannon tunnisteeseen eikä päinvastoin
    QVERIFY(Vero::lahetettyAlvViite(Vero::TESTI, kausi) != Vero::lahetettyAlvViite(Vero::TUOTANTO, kausi));
    QVERIFY(Vero::saapunutAlvViite(Vero::TESTI, kausi) != Vero::saapunutAlvViite(Vero::TUOTANTO, kausi));
    QVERIFY(Vero::kirjeViite(Vero::TESTI, "L-1") != Vero::kirjeViite(Vero::TUOTANTO, "L-1"));

    // Etuliite ei saa ylittää 64 merkin rajaa pitkilläkään kirjenumeroilla, ja viite on aina haettavissa
    const QString pitka = Vero::kirjeViite(Vero::TESTI, QString(200, 'x'));
    QVERIFY(pitka.length() <= 64);
    QCOMPARE(pitka.indexOf(QRegularExpression("\\W")), -1);
}

void VeroTesti::tukemattomatKoodit_alarajahuojennus()
{
    // Esimerkkikoodeissa ei ole huojennusta
    QVERIFY(Vero::tukemattomatKoodit(esimerkkiKoodit()).isEmpty());

    // Nollasta poikkeava alarajahuojennus estää lähetyksen
    QVariantMap koodit = esimerkkiKoodit();
    koodit.insert("316", 1500);
    QCOMPARE(Vero::tukemattomatKoodit(koodit), QStringList{QStringLiteral("316")});
    koodit.insert("315", -200);
    koodit.insert("317", 10);
    QCOMPARE(Vero::tukemattomatKoodit(koodit), (QStringList{"315", "316", "317"}));

    // Nolla ei ole huojennusta
    QVariantMap nollat = esimerkkiKoodit();
    nollat.insert("315", 0);
    nollat.insert("316", 0);
    QVERIFY(Vero::tukemattomatKoodit(nollat).isEmpty());
}

void VeroTesti::puuttuvatLiitteet_vainPuuttuvatNimet()
{
    QCOMPARE(Vero::puuttuvatLiitteet({}, {}), QStringList());
    QCOMPARE(Vero::puuttuvatLiitteet({QStringLiteral("a.pdf")}, {QStringLiteral("a.pdf")}), QStringList());
    QCOMPARE(Vero::puuttuvatLiitteet({QStringLiteral("a.pdf"), QStringLiteral("b.json")},
                                    {QStringLiteral("a.pdf")}),
             QStringList{QStringLiteral("b.json")});
}

void VeroTesti::harjoitus_eiSaaKayttaaTuotantoa()
{
    QVERIFY(Vero::harjoitukselleSallittu(false, Vero::TUOTANTO));
    QVERIFY(Vero::harjoitukselleSallittu(false, Vero::TESTI));
    QVERIFY(Vero::harjoitukselleSallittu(true, Vero::TESTI));
    QVERIFY(!Vero::harjoitukselleSallittu(true, Vero::TUOTANTO));
}

void VeroTesti::samaAlvIlmoitus_vertaaVakaatKentat()
{
    const QVariantMap lahetetty = Vero::fileVatReturn("1234567-1", QDate(2026, 10, 31), esimerkkiKoodit(),
                                                      "Maija Mallikas", "0401234567", false, QString(), false);
    QVariantMap haettu = lahetetty;
    haettu.insert("AcceptedTime", "2026-11-01T08:00:00");
    QVERIFY(Vero::samaAlvIlmoitus(lahetetty, haettu));

    // Verohallinto voi kääriä ilmoituksen yhden tason sisään
    QVariantMap kiedottu;
    kiedottu.insert("FiledReturn", haettu);
    QVERIFY(Vero::samaAlvIlmoitus(lahetetty, kiedottu));

    QVariantMap eriSumma = haettu;
    QVariantMap tiedot = eriSumma.value("VATDetails").toMap();
    QVariantMap kotimaa = tiedot.value("VATOnDomesticSalesByTaxRate").toMap();
    kotimaa.insert("HighVATRate", 1.0);
    tiedot.insert("VATOnDomesticSalesByTaxRate", kotimaa);
    eriSumma.insert("VATDetails", tiedot);
    QVERIFY(!Vero::samaAlvIlmoitus(lahetetty, eriSumma));

    QVariantMap eriKausi = haettu;
    eriKausi.insert("FilingPeriod", "2026-09-30");
    QVERIFY(!Vero::samaAlvIlmoitus(lahetetty, eriKausi));

    QVERIFY(!Vero::samaAlvIlmoitus(lahetetty, QVariantMap()));
    QVERIFY(!Vero::samaAlvIlmoitus(QVariantMap(), haettu));

    // Kokonaisluku ja desimaali ovat sama euromäärä
    QVariantMap kokonaisluku = lahetetty;
    QVariantMap kokonaisTiedot = kokonaisluku.value("VATDetails").toMap();
    QVariantMap kokonaisKotimaa = kokonaisTiedot.value("VATOnDomesticSalesByTaxRate").toMap();
    kokonaisKotimaa.insert("HighVATRate", 1000);
    kokonaisTiedot.insert("VATOnDomesticSalesByTaxRate", kokonaisKotimaa);
    kokonaisluku.insert("VATDetails", kokonaisTiedot);
    QVERIFY(Vero::samaAlvIlmoitus(lahetetty, kokonaisluku));
}

void VeroTesti::epaonnistunutLahetys_sailyttaaHyvaksytyn()
{
    const QVariantMap tapahtuma{{"http", 400}, {"tulos", "hylatty"},
                                {"sanoma", QVariantMap{{"BusinessId", "9999999-9"}}}};
    const QVariantMap hyvaksytty{
        {"tila", "kasittelyssa"},
        {"sanoma", QVariantMap{{"BusinessId", "1234567-8"}}},
        {"vastaus", QVariantMap{{"Accepted", true}}},
        {"lahetetty", "2026-11-01T08:00:00"},
        {"korvaava", false},
        {"lahetykset", QVariantList{QVariantMap{{"tulos", "kasittelyssa"}}}}
    };
    const QVariantMap uusi{
        {"tila", "lahettamassa"},
        {"sanoma", QVariantMap{{"BusinessId", "9999999-9"}}},
        {"korvaava", true},
        {"korvauksenSyy", "CLC"},
        {"lahetykset", QVariantList{QVariantMap{{"tulos", "lahettamassa"}}}}
    };

    const QVariantMap sailytetty = Vero::epaonnistuneenLahetyksenVero(hyvaksytty, uusi, tapahtuma);
    QCOMPARE(sailytetty.value("tila").toString(), QStringLiteral("kasittelyssa"));
    QCOMPARE(sailytetty.value("sanoma").toMap().value("BusinessId").toString(), QStringLiteral("1234567-8"));
    QCOMPARE(sailytetty.value("vastaus").toMap().value("Accepted").toBool(), true);
    QCOMPARE(sailytetty.value("lahetetty").toString(), QStringLiteral("2026-11-01T08:00:00"));
    QCOMPARE(sailytetty.value("korvaava").toBool(), false);
    QCOMPARE(sailytetty.value("lahetykset").toList().size(), 2);
    QCOMPARE(sailytetty.value("lahetykset").toList().last().toMap().value("http").toInt(), 400);
    QCOMPARE(sailytetty.value("lahetykset").toList().last().toMap().value("sanoma").toMap().value("BusinessId").toString(),
             QStringLiteral("9999999-9"));

    const QVariantMap kasitelty = Vero::epaonnistuneenLahetyksenVero(
                QVariantMap{{"tila", "kasitelty"}, {"sanoma", hyvaksytty.value("sanoma")}}, uusi, tapahtuma);
    QCOMPARE(kasitelty.value("tila").toString(), QStringLiteral("kasitelty"));
    QCOMPARE(kasitelty.value("sanoma").toMap().value("BusinessId").toString(), QStringLiteral("1234567-8"));

    const QVariantMap tuntematon = Vero::epaonnistuneenLahetyksenVero(
                QVariantMap{{"tila", "tuntematon"}, {"sanoma", hyvaksytty.value("sanoma")}}, uusi, tapahtuma);
    QCOMPARE(tuntematon.value("tila").toString(), QStringLiteral("tuntematon"));
    QCOMPARE(tuntematon.value("sanoma").toMap().value("BusinessId").toString(), QStringLiteral("1234567-8"));

    const QVariantMap ensimmainen = Vero::epaonnistuneenLahetyksenVero(QVariantMap(), uusi, tapahtuma);
    QCOMPARE(ensimmainen.value("tila").toString(), QStringLiteral("hylatty"));
    QCOMPARE(ensimmainen.value("sanoma").toMap().value("BusinessId").toString(), QStringLiteral("9999999-9"));

    const QVariantMap aiemminHylatty = Vero::epaonnistuneenLahetyksenVero(
                QVariantMap{{"tila", "hylatty"}, {"sanoma", hyvaksytty.value("sanoma")}}, uusi, tapahtuma);
    QCOMPARE(aiemminHylatty.value("tila").toString(), QStringLiteral("hylatty"));
    QCOMPARE(aiemminHylatty.value("sanoma").toMap().value("BusinessId").toString(), QStringLiteral("9999999-9"));
}

void VeroTesti::lahetysVahvistus_huomaaUudenYrityksen()
{
    const QVariantMap aiempi{{"tila", "tuntematon"}, {"yritetty", "2026-11-11T10:00:00"},
                             {"lahetykset", QVariantList{QVariantMap{{"tulos", "tuntematon"}}}}};
    const QString vanha = Vero::lahetysVahvistus(4, aiempi);
    QVERIFY(!vanha.isEmpty());
    QCOMPARE(Vero::lahetysVahvistus(0, aiempi), QString());

    QVariantMap uusiYritys = aiempi;
    uusiYritys.insert("yritetty", "2026-11-11T10:05:00");
    QVariantList lahetykset = uusiYritys.value("lahetykset").toList();
    lahetykset.append(QVariantMap{{"tulos", "lahettamassa"}});
    uusiYritys.insert("lahetykset", lahetykset);
    QVERIFY(Vero::lahetysVahvistus(4, uusiYritys) != vanha);

    // Sama tila ja sama yritys pysyy samana tunnisteena
    QCOMPARE(Vero::lahetysVahvistus(4, aiempi), vanha);
}

void VeroTesti::onVarmaHttpHylkays_viisisataaEiOleHylkays()
{
    QVERIFY(Vero::onVarmaHttpHylkays(400, QString()));
    QVERIFY(Vero::onVarmaHttpHylkays(422, QString()));
    QVERIFY(!Vero::onVarmaHttpHylkays(500, QString()));
    QVERIFY(!Vero::onVarmaHttpHylkays(503, QString()));
    QVERIFY(!Vero::onVarmaHttpHylkays(200, QString()));
    QVERIFY(!Vero::onVarmaHttpHylkays(0, QString()));
    QVERIFY(!Vero::onVarmaHttpHylkays(400, QStringLiteral("Connection closed")));
}

void VeroTesti::onVerkkotasonVirhe_protokollavirheOnVerkkovirhe()
{
    QVERIFY(!Vero::onVerkkotasonVirhe(QNetworkReply::NoError, 200));
    QVERIFY(Vero::onVerkkotasonVirhe(QNetworkReply::TimeoutError, 0));
    QVERIFY(Vero::onVerkkotasonVirhe(QNetworkReply::RemoteHostClosedError, 200));
    QVERIFY(Vero::onVerkkotasonVirhe(QNetworkReply::ProtocolFailure, 200));
    // Qt kuvaa HTTP 400:n tällä koodilla. Se on hylkäys, ei katkennut vastaus.
    QVERIFY(!Vero::onVerkkotasonVirhe(QNetworkReply::ProtocolInvalidOperationError, 400));
    QVERIFY(Vero::onVerkkotasonVirhe(QNetworkReply::ProtocolInvalidOperationError, 200));
    QVERIFY(!Vero::onVerkkotasonVirhe(QNetworkReply::ContentAccessDenied, 403));
    QVERIFY(!Vero::onVerkkotasonVirhe(QNetworkReply::InternalServerError, 500));
}

void VeroTesti::virheteksti_verkkovirheEnnenHttpTilaa()
{
    // HTTP 200 -otsikot mutta katkennut runko: verkkovirhe on selitys, ei "HTTP 200"
    const QString teksti = Vero::virheteksti(200, QVariantMap(), "Connection closed");
    QVERIFY(teksti.contains("Connection closed"));

    Vero::Vastaus vastaus;
    vastaus.http = 200;
    QVERIFY(vastaus.ok());
    vastaus.verkkovirhe = "Connection closed";
    QVERIFY(!vastaus.ok());
}

void VeroTesti::rajattuPvm()
{
    const QDate alku(2019, 1, 1);
    const QDate loppu(2020, 12, 31);
    QCOMPARE(Vero::rajattuPvm(QDate(2019, 6, 1), alku, loppu), QDate(2019, 6, 1));
    QCOMPARE(Vero::rajattuPvm(QDate(2018, 6, 1), alku, loppu), alku);
    QCOMPARE(Vero::rajattuPvm(QDate(2026, 6, 1), alku, loppu), loppu);
    QCOMPARE(Vero::rajattuPvm(QDate(), alku, loppu), alku);
}

void VeroTesti::asetukset_erottavatYmparistot()
{
    QTemporaryDir hakemisto;
    QVERIFY(hakemisto.isValid());
    QSettings settings(hakemisto.filePath("vero.ini"), QSettings::IniFormat);

    QCOMPARE(Vero::Asetukset::valittuYmparisto(&settings), Vero::TESTI);
    QVERIFY(!Vero::Asetukset::lataa(&settings, Vero::TESTI).kokonainen());

    Vero::Asetukset testi;
    testi.ymparisto = Vero::TESTI;
    testi.varmenne = "/tmp/testi.pem";
    testi.avain = "/tmp/testi.key";
    testi.ohjelmaId = "testi-id";
    testi.ohjelmaAvain = "testi-avain";
    testi.tilitoimisto = "1234567-1";
    testi.tallenna(&settings);

    Vero::Asetukset tuotanto;
    tuotanto.ymparisto = Vero::TUOTANTO;
    tuotanto.varmenne = "/tmp/tuotanto.pem";
    tuotanto.avain = "/tmp/tuotanto.key";
    tuotanto.ohjelmaId = "tuotanto-id";
    tuotanto.ohjelmaAvain = "tuotanto-avain";
    tuotanto.tallenna(&settings);

    const Vero::Asetukset luettuTesti = Vero::Asetukset::lataa(&settings, Vero::TESTI);
    const Vero::Asetukset luettuTuotanto = Vero::Asetukset::lataa(&settings, Vero::TUOTANTO);
    QVERIFY(luettuTesti.kokonainen());
    QVERIFY(luettuTuotanto.kokonainen());
    QCOMPARE(luettuTesti.ohjelmaId, QStringLiteral("testi-id"));
    QCOMPARE(luettuTesti.tilitoimisto, QStringLiteral("1234567-1"));
    QCOMPARE(luettuTuotanto.ohjelmaId, QStringLiteral("tuotanto-id"));
    QCOMPARE(luettuTuotanto.varmenne, QStringLiteral("/tmp/tuotanto.pem"));
    QVERIFY(luettuTuotanto.tilitoimisto.isEmpty());

    // Ympäristön valinta säilyy, eikä sen vaihto sekoita tunnisteita
    Vero::Asetukset::asetaYmparisto(&settings, Vero::TUOTANTO);
    QCOMPARE(Vero::Asetukset::valittuYmparisto(&settings), Vero::TUOTANTO);
    QCOMPARE(Vero::Asetukset::lataa(&settings, Vero::TESTI).ohjelmaAvain, QStringLiteral("testi-avain"));

    // Puutteellinen asetus ei ole kokonainen
    Vero::Asetukset vajaa = luettuTesti;
    vajaa.ohjelmaAvain.clear();
    QVERIFY(!vajaa.kokonainen());
}

void VeroTesti::varmenne_puuttuvaTiedostoJaVirheellinenPem()
{
    VeroClient asiakas;
    QString virhe;
    QVERIFY(!asiakas.asetaVarmenne("/ei/ole/olemassa.pem", "/ei/ole/olemassa.key", &virhe));
    QVERIFY(!virhe.isEmpty());

    QTemporaryFile roska;
    QVERIFY(roska.open());
    roska.write("tämä ei ole PEM-tiedosto");
    roska.flush();

    virhe.clear();
    QVERIFY(!asiakas.asetaVarmenne(roska.fileName(), roska.fileName(), &virhe));
    QVERIFY(!virhe.isEmpty());
    QVERIFY(asiakas.varmenne().isNull());
    QVERIFY(asiakas.varmenteenYtunnus().isEmpty());
    QVERIFY(!asiakas.onkoTestivarmenne());
}

void VeroTesti::varmenne_rsaJaEcSekaVaaraAvain()
{
    // Keksittyjä testivarmenteita; openssl pitää olla PATH:ssa CI:ssä ja kehityskoneella.
    QTemporaryDir hakemisto;
    QVERIFY(hakemisto.isValid());
    const QString rsaAvain = hakemisto.filePath(QStringLiteral("rsa.key"));
    const QString rsaPem = hakemisto.filePath(QStringLiteral("rsa.pem"));
    const QString ecAvain = hakemisto.filePath(QStringLiteral("ec.key"));
    const QString ecPem = hakemisto.filePath(QStringLiteral("ec.pem"));

    const auto aja = [](const QStringList& args) {
        QProcess p;
        p.start(QStringLiteral("openssl"), args);
        return p.waitForFinished(15000) && p.exitCode() == 0;
    };
    QVERIFY(aja({QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"),
                 QStringLiteral("rsa:2048"), QStringLiteral("-keyout"), rsaAvain,
                 QStringLiteral("-out"), rsaPem, QStringLiteral("-days"), QStringLiteral("1"),
                 QStringLiteral("-nodes"), QStringLiteral("-subj"), QStringLiteral("/CN=1234567-1")}));
    QVERIFY(aja({QStringLiteral("ecparam"), QStringLiteral("-name"), QStringLiteral("prime256v1"),
                 QStringLiteral("-genkey"), QStringLiteral("-noout"), QStringLiteral("-out"), ecAvain}));
    QVERIFY(aja({QStringLiteral("req"), QStringLiteral("-new"), QStringLiteral("-x509"),
                 QStringLiteral("-key"), ecAvain, QStringLiteral("-out"), ecPem,
                 QStringLiteral("-days"), QStringLiteral("1"), QStringLiteral("-subj"),
                 QStringLiteral("/CN=1234567-1")}));

    VeroClient asiakas;
    QString virhe;
    QVERIFY2(asiakas.asetaVarmenne(rsaPem, rsaAvain, &virhe), qPrintable(virhe));
    QCOMPARE(asiakas.varmenteenYtunnus(), QStringLiteral("1234567-1"));

    // Eri algoritmi hylätään; samanpituista väärää RSA-avainta Qt:n API ei erota.
    virhe.clear();
    QVERIFY(!asiakas.asetaVarmenne(rsaPem, ecAvain, &virhe));
    QVERIFY(virhe.contains(QStringLiteral("vastaa"), Qt::CaseInsensitive)
            || virhe.contains(QStringLiteral("RSA"), Qt::CaseInsensitive)
            || virhe.contains(QStringLiteral("EC"), Qt::CaseInsensitive));
    QVERIFY(asiakas.varmenne().isNull());

    virhe.clear();
    QVERIFY2(asiakas.asetaVarmenne(ecPem, ecAvain, &virhe), qPrintable(virhe));
    QCOMPARE(asiakas.varmenteenYtunnus(), QStringLiteral("1234567-1"));
}

void VeroTesti::client_lahettaaOtsikotJaRungon()
{
    Palvelin palvelin;
    QVERIFY(palvelin.kaynnista());
    palvelin.vastaus = R"({"Result":"ok"})";

    VeroClient asiakas;
    asiakas.asetaOhjelma("kokeilu-id", "kokeilu-avain");
    asiakas.asetaUserAgent("kitsas-test/1");
    asiakas.asetaTestipalvelin(palvelin.osoite());

    bool valmis = false;
    Vero::Vastaus tulos;
    const QVariantMap sanoma = Vero::fileVatReturn("1234567-1", QDate(2025, 6, 30), esimerkkiKoodit(), "Maija", "040");
    asiakas.kutsu(Vero::osoite(Vero::TESTI, Vero::FILE_VAT_RETURN), sanoma, "valtuutus-token", false,
                  [&](const Vero::Vastaus& vastaus) { tulos = vastaus; valmis = true; });
    QTRY_VERIFY_WITH_TIMEOUT(valmis, 5000);

    QVERIFY(tulos.ok());
    QCOMPARE(tulos.http, 200);
    QCOMPARE(tulos.json().value("Result").toString(), QStringLiteral("ok"));

    // Polku säilyy, palvelin korvataan
    QCOMPARE(palvelin.metodi, QByteArray("POST"));
    QCOMPARE(palvelin.polku, QByteArray("/FIS/Return/SAT/Test/FileVATReturn/v2"));
    QCOMPARE(palvelin.otsikot.value("vero-softwareid"), QByteArray("kokeilu-id"));
    QCOMPARE(palvelin.otsikot.value("vero-softwarekey"), QByteArray("kokeilu-avain"));
    QCOMPARE(palvelin.otsikot.value("vero-authorizationtoken"), QByteArray("valtuutus-token"));
    QCOMPARE(palvelin.otsikot.value("user-agent"), QByteArray("kitsas-test/1"));
    QCOMPARE(palvelin.otsikot.value("accept"), QByteArray("application/json"));

    // Rungossa on täsmälleen rakennettu sanoma
    const QVariantMap lahetetty = QJsonDocument::fromJson(palvelin.runko).object().toVariantMap();
    QCOMPARE(lahetetty.value("BusinessId").toString(), QStringLiteral("1234567-1"));
    QCOMPARE(lahetetty.value("FilingPeriod").toString(), QStringLiteral("2025-06-30"));
    QCOMPARE(lahetetty.value("VATDetails").toMap().value("DeductibleVAT").toDouble(), 300.0);
}

void VeroTesti::client_httpVirheKulkeeVastauksena()
{
    Palvelin palvelin;
    QVERIFY(palvelin.kaynnista());
    palvelin.status = 400;
    palvelin.vastaus = R"({"ErrorCode":"E42","ErrorText":"Kausi puuttuu"})";

    VeroClient asiakas;
    asiakas.asetaTestipalvelin(palvelin.osoite());

    bool valmis = false;
    Vero::Vastaus tulos;
    asiakas.kutsu(Vero::osoite(Vero::TESTI, Vero::GET_VAT_PERIODS), QVariantMap{{"BusinessId", "1234567-1"}},
                  QString(), false, [&](const Vero::Vastaus& vastaus) { tulos = vastaus; valmis = true; });
    QTRY_VERIFY_WITH_TIMEOUT(valmis, 5000);

    QVERIFY(!tulos.ok());
    QCOMPARE(tulos.http, 400);
    QCOMPARE(tulos.json().value("ErrorCode").toString(), QStringLiteral("E42"));
    QVERIFY(Vero::virheteksti(tulos.http, tulos.json(), tulos.verkkovirhe).contains("Kausi puuttuu"));
    // Valtuutustunnistetta ei lähetetä, kun sitä ei ole annettu
    QVERIFY(!palvelin.otsikot.contains("vero-authorizationtoken"));
}

void VeroTesti::client_katkennutVastausEiOleOnnistunut()
{
    // Palvelin vastaa HTTP 200 mutta katkaisee yhteyden kesken rungon
    Palvelin palvelin;
    QVERIFY(palvelin.kaynnista());
    palvelin.katkaise = true;
    palvelin.vastaus = R"({"Result":"ok")";     // Katkennut JSON

    VeroClient asiakas;
    asiakas.asetaTestipalvelin(palvelin.osoite());

    bool valmis = false;
    Vero::Vastaus tulos;
    asiakas.kutsu(Vero::osoite(Vero::TESTI, Vero::FILE_VAT_RETURN), QVariantMap{{"BusinessId", "1234567-1"}},
                  "token", false, [&](const Vero::Vastaus& vastaus) { tulos = vastaus; valmis = true; });
    QTRY_VERIFY_WITH_TIMEOUT(valmis, 10000);

    QCOMPARE(tulos.http, 200);
    QVERIFY(!tulos.verkkovirhe.isEmpty());
    // Epätäydellistä vastausta ei saa hyväksyä onnistuneeksi, vaikka HTTP-tila on 200
    QVERIFY(!tulos.ok());
}

void VeroTesti::client_verkkovirheEiOleHttpVastaus()
{
    // Portti 1 ei kuuntele: yhteys evätään eikä HTTP-vastausta tule
    VeroClient asiakas;
    asiakas.asetaTestipalvelin(QUrl("http://127.0.0.1:1"));

    bool valmis = false;
    Vero::Vastaus tulos;
    asiakas.kutsu(Vero::osoite(Vero::TESTI, Vero::LIST_LETTERS), QVariantMap(), QString(), false,
                  [&](const Vero::Vastaus& vastaus) { tulos = vastaus; valmis = true; });
    QTRY_VERIFY_WITH_TIMEOUT(valmis, 10000);

    QVERIFY(!tulos.ok());
    QCOMPARE(tulos.http, 0);
    QVERIFY(!tulos.verkkovirhe.isEmpty());
    QVERIFY(Vero::virheteksti(tulos.http, tulos.json(), tulos.verkkovirhe).contains(tulos.verkkovirhe));
}

QTEST_GUILESS_MAIN(VeroTesti)

#include "tst_verotesti.moc"
