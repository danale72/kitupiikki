/*
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "veroapu.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <cmath>

namespace Vero {

const QStringList korvauksenSyyt = { QStringLiteral("CLC"), QStringLiteral("LGL"),
                                     QStringLiteral("TXA"), QStringLiteral("LAW") };

QString ymparistoAvain(Ymparisto ymparisto)
{
    return ymparisto == TUOTANTO ? QStringLiteral("tuotanto") : QStringLiteral("testi");
}

Ymparisto ymparistoAvaimesta(const QString &avain)
{
    // Tuntematon arvo tulkitaan aina testiympäristöksi, ettei tuotantoon päädytä vahingossa
    return avain == QLatin1String("tuotanto") ? TUOTANTO : TESTI;
}

QString ymparistoNimi(Ymparisto ymparisto)
{
    return ymparisto == TUOTANTO ?
                QCoreApplication::translate("Vero", "Tuotanto") :
                QCoreApplication::translate("Vero", "Testi");
}

QUrl osoite(Ymparisto ymparisto, Rajapinta rajapinta)
{
    const bool tuotanto = ymparisto == TUOTANTO;

    QString kanta;
    QString polku;
    switch (rajapinta) {
    case GETTOKEN:
        kanta = tuotanto ? "https://api.vero.fi/General/Authorization/"
                         : "https://apitest.vero.fi/FIS/General/Authorization/Test/";
        polku = "GetToken/v1";
        break;
    case LIST_LETTERS:
        kanta = tuotanto ? "https://api.vero.fi/Customer/Letters/"
                         : "https://apitest.vero.fi/FIS/Customer/Letters/Test/";
        polku = "ListLetters/v1";
        break;
    case GET_LETTER:
        kanta = tuotanto ? "https://api.vero.fi/Customer/Letters/"
                         : "https://apitest.vero.fi/FIS/Customer/Letters/Test/";
        polku = "GetLetter/v1";
        break;
    case GET_VAT_PERIODS:
        kanta = tuotanto ? "https://api.vero.fi/Return/SAT/"
                         : "https://apitest.vero.fi/FIS/Return/SAT/Test/";
        polku = "GetVATPeriods/v1";
        break;
    case GET_FILED_VAT_RETURN:
        kanta = tuotanto ? "https://api.vero.fi/Return/SAT/"
                         : "https://apitest.vero.fi/FIS/Return/SAT/Test/";
        polku = "GetFiledVATReturn/v2";
        break;
    case FILE_VAT_RETURN:
        kanta = tuotanto ? "https://api.vero.fi/Return/SAT/"
                         : "https://apitest.vero.fi/FIS/Return/SAT/Test/";
        polku = "FileVATReturn/v2";
        break;
    }
    return QUrl(kanta + polku);
}

QString normalisoiYtunnus(const QString &teksti)
{
    QString str = teksti.simplified().remove(QLatin1Char(' ')).toUpper();
    if (str.startsWith(QLatin1String("FI")))
        str = str.mid(2);
    if (str.length() == 8)
        str.insert(7, QLatin1Char('-'));

    static const QRegularExpression muoto(QStringLiteral("^\\d{7}-\\d$"));
    if (!muoto.match(str).hasMatch())
        return QString();

    const int summa = str.at(0).digitValue() * 7 +
                      str.at(1).digitValue() * 9 +
                      str.at(2).digitValue() * 10 +
                      str.at(3).digitValue() * 5 +
                      str.at(4).digitValue() * 8 +
                      str.at(5).digitValue() * 4 +
                      str.at(6).digitValue() * 2;
    const int jaannos = summa % 11;
    if (jaannos == 1)
        return QString();
    const int tarkiste = jaannos == 0 ? 0 : 11 - jaannos;
    return tarkiste == str.at(8).digitValue() ? str : QString();
}

Otsikot otsikot(const QString &ohjelmaId, const QString &ohjelmaAvain, const QString &userAgent,
                bool pdf, const QString &valtuutusToken)
{
    Otsikot lista;
    const auto lisaa = [&lista](const char* nimi, const QByteArray& arvo) {
        lista.append(qMakePair(QByteArray(nimi), arvo));
    };
    lisaa("User-Agent", userAgent.toUtf8());
    lisaa("Vero-SoftwareId", ohjelmaId.toUtf8());
    lisaa("Vero-SoftwareKey", ohjelmaAvain.toUtf8());
    lisaa("Accept", pdf ? QByteArray("application/pdf") : QByteArray("application/json"));
    lisaa("Content-Type", "application/json");
    lisaa("Cache-Control", "no-cache");
    if (!valtuutusToken.isEmpty())
        lisaa("Vero-AuthorizationToken", valtuutusToken.toUtf8());
    return lista;
}

const QList<KoodiKentta> &koodiKentat()
{
    static const QList<KoodiKentta> kentat = {
        { 301, "VATOnDomesticSalesByTaxRate", "HighVATRate" },
        { 302, "VATOnDomesticSalesByTaxRate", "MediumVATRate" },
        { 303, "VATOnDomesticSalesByTaxRate", "LowVATRate" },
        { 305, "VATOnPurchasesAndImports", "GoodsFromEUMemberStates" },
        { 306, "VATOnPurchasesAndImports", "ServicesFromEUMemberStates" },
        { 304, "VATOnPurchasesAndImports", "ImportOfGoodsOutsideEU" },
        { 318, "VATOnPurchasesAndImports", "ConstructionServicesAndScrapMetal" },
        { 307, "", "DeductibleVAT" },
        { 309, "SalesPurchasesImports", "ZeroVATRateTurnover" },
        { 311, "SalesPurchasesImports", "SalesOfGoodsToEUMemberStates" },
        { 312, "SalesPurchasesImports", "SalesOfServicesToEUMemberStates" },
        { 313, "SalesPurchasesImports", "PurchasesOfGoodsFromEUMemberStates" },
        { 314, "SalesPurchasesImports", "PurchasesOfServicesFromEUMemberStates" },
        { 310, "SalesPurchasesImports", "ImportsOfGoodsFromOutsideEU" },
        { 319, "SalesPurchasesImports", "SalesOfConstructionServicesAndScrapMetal" },
        { 320, "SalesPurchasesImports", "PurchasesOfConstructionServicesAndScrapMetal" }
    };
    return kentat;
}

bool onkoToimintaa(const QVariantMap &koodit)
{
    for (const KoodiKentta& kentta : koodiKentat()) {
        if (koodit.value(QString::number(kentta.koodi)).toLongLong() != 0)
            return true;
    }
    return false;
}

QVariantMap fileVatReturn(const QString &ytunnus, const QDate &kausiLoppu, const QVariantMap &koodit,
                          const QString &yhteyshenkilo, const QString &puhelin,
                          bool korvaava, const QString &korvauksenSyy, bool maksuperusteinen)
{
    QVariantMap sanoma;
    sanoma.insert("ReplacementReturn", korvaava);
    if (korvaava)
        sanoma.insert("ReplacementReason", korvauksenSyy);
    sanoma.insert("BusinessId", ytunnus);
    sanoma.insert("FilingPeriod", kausiLoppu.toString(Qt::ISODate));

    const bool toimintaa = onkoToimintaa(koodit);
    sanoma.insert("NoActivity", !toimintaa);
    sanoma.insert("ContactDetails", QVariantMap{
                      {"FullName", yhteyshenkilo.simplified()},
                      {"PhoneNumber", puhelin.simplified()}});

    if (!toimintaa)
        return sanoma;

    QVariantMap tiedot;
    // Maksuperusteisuutta ei voi ilmoittaa vuoden 2025 alusta alkaen
    tiedot.insert("VATOnCashBasis", maksuperusteinen && kausiLoppu.year() < 2025);

    QMap<QString, QVariantMap> ryhmat;
    ryhmat.insert("VATOnDomesticSalesByTaxRate", QVariantMap());
    ryhmat.insert("VATOnPurchasesAndImports", QVariantMap());
    ryhmat.insert("SalesPurchasesImports", QVariantMap());

    for (const KoodiKentta& kentta : koodiKentat()) {
        const double euroa = koodit.value(QString::number(kentta.koodi)).toLongLong() / 100.0;
        if (QString::fromLatin1(kentta.ryhma).isEmpty()) {
            tiedot.insert(QString::fromLatin1(kentta.kentta), euroa);
        } else {
            QVariantMap& ryhma = ryhmat[QString::fromLatin1(kentta.ryhma)];
            ryhma.insert(QString::fromLatin1(kentta.kentta), euroa);
        }
    }
    for (auto iter = ryhmat.constBegin(); iter != ryhmat.constEnd(); ++iter)
        tiedot.insert(iter.key(), iter.value());

    sanoma.insert("VATDetails", tiedot);
    return sanoma;
}

QStringList validoiFileVatReturn(const QVariantMap &sanoma)
{
    const auto t = [](const char* teksti) { return QCoreApplication::translate("Vero", teksti); };
    QStringList virheet;

    const QString ytunnus = sanoma.value("BusinessId").toString();
    if (ytunnus.length() != 9)
        virheet << t("Y-tunnuksen on oltava 9 merkkiä (1234567-8)");

    QDate kausi;
    if (sanoma.value("FilingPeriod").typeId() == QMetaType::QString) {
        const QString teksti = sanoma.value("FilingPeriod").toString();
        kausi = QDate::fromString(teksti, Qt::ISODate);
        if (!kausi.isValid() || kausi.toString(Qt::ISODate) != teksti)
            kausi = QDate();
    }
    if (!kausi.isValid())
        virheet << t("Verokausi puuttuu tai on virheellinen");

    if (sanoma.value("ReplacementReturn").typeId() != QMetaType::Bool) {
        virheet << t("ReplacementReturn puuttuu");
    } else if (sanoma.value("ReplacementReturn").toBool() &&
               !korvauksenSyyt.contains(sanoma.value("ReplacementReason").toString())) {
        virheet << t("Korvaavan ilmoituksen syy on valittava (CLC, LGL, TXA tai LAW)");
    }

    const QVariantMap yhteys = sanoma.value("ContactDetails").toMap();
    if (yhteys.value("FullName").toString().trimmed().isEmpty())
        virheet << t("Yhteyshenkilön nimi puuttuu");
    if (yhteys.value("PhoneNumber").toString().trimmed().isEmpty())
        virheet << t("Yhteyshenkilön puhelinnumero puuttuu");

    const bool ilmanToimintaa = sanoma.value("NoActivity").toBool();
    if (!ilmanToimintaa && !sanoma.value("VATDetails").toMap().size())
        virheet << t("Alv-tiedot puuttuvat");

    if (kausi.isValid() && kausi.year() >= 2025) {
        const QVariantMap tiedot = sanoma.value("VATDetails").toMap();
        if (tiedot.value("VATOnCashBasis").toBool())
            virheet << t("Maksuperusteisuutta ei voi ilmoittaa vuoden 2025 alusta alkaen");
        if (tiedot.value("VATRelief").toMap().value("FileVATRelief").toBool())
            virheet << t("Alarajahuojennusta ei voi ilmoittaa vuoden 2025 alusta alkaen");
    }
    return virheet;
}

QList<Kirje> kirjeet(const QVariantMap &vastaus)
{
    QList<Kirje> lista;
    const QVariantList kirjeLista = vastaus.value("Letters").toList();
    for (const QVariant& item : kirjeLista) {
        const QVariantMap map = item.toMap();
        Kirje kirje;
        kirje.numero = map.value("LetterNumber").toString();
        if (kirje.numero.isEmpty())
            continue;
        kirje.tyyppi = map.value("LetterType").toString();
        kirje.nimiFi = map.value("LetterNameFI").toString();
        kirje.nimiEn = map.value("LetterNameEN").toString();
        kirje.paivays = QDate::fromString(map.value("PrintedDate").toString(), Qt::ISODate);
        lista.append(kirje);
    }
    return lista;
}

QDate vanhinPaivays(const QList<Kirje> &kirjeet)
{
    QDate vanhin;
    for (const Kirje& kirje : kirjeet) {
        if (kirje.paivays.isValid() && (!vanhin.isValid() || kirje.paivays < vanhin))
            vanhin = kirje.paivays;
    }
    return vanhin;
}

QList<Verokausi> verokaudet(const QVariantMap &vastaus)
{
    QList<Verokausi> lista;
    const QVariantList kaudet = vastaus.value("FilingPeriod").toList();
    for (const QVariant& item : kaudet) {
        const QVariantMap map = item.toMap();
        Verokausi kausi;
        kausi.alkaa = QDate::fromString(map.value("StartDate").toString(), Qt::ISODate);
        kausi.loppuu = QDate::fromString(map.value("EndDate").toString(), Qt::ISODate);
        if (!kausi.loppuu.isValid())
            kausi.loppuu = QDate::fromString(map.value("Period").toString(), Qt::ISODate);
        if (!kausi.loppuu.isValid())
            continue;
        kausi.erapaiva = QDate::fromString(map.value("DueDate").toString(), Qt::ISODate);
        kausi.tila = map.value("Status").toString();
        kausi.tiheys = map.value("Frequency").toString();
        lista.append(kausi);
    }
    return lista;
}

QMap<QString, QString> tokenit(const QVariantMap &vastaus)
{
    QMap<QString, QString> tokenit;
    const QVariantList valtuutukset = vastaus.value("Authorizations").toList();
    for (const QVariant& item : valtuutukset) {
        const QVariantMap map = item.toMap();
        const QString raaka = map.value("PrincipalIdentifier").toString().trimmed();
        const QString normalisoitu = normalisoiYtunnus(raaka);
        const QString asiakas = normalisoitu.isEmpty() ? raaka : normalisoitu;
        const QString token = map.value("AuthorizationToken").toString();
        if (!asiakas.isEmpty() && !token.isEmpty())
            tokenit.insert(asiakas, token);
    }
    return tokenit;
}

QString virheteksti(int http, const QVariantMap &runko, const QString &verkkovirhe)
{
    // Verkkovirhe voi tulla myös HTTP-otsikoiden jälkeen (esim. vastauksen luku katkeaa),
    // jolloin vastaus on epätäydellinen eikä sen sisältöön saa luottaa
    if (http == 0 || !verkkovirhe.isEmpty())
        return QCoreApplication::translate("Vero", "Yhteys Verohallintoon epäonnistui: %1").arg(verkkovirhe);

    const QString koodi = runko.value("ErrorCode").toString();
    const QString teksti = runko.value("ErrorText").toString();
    QString viesti = QCoreApplication::translate("Vero", "Verohallinto vastasi virheellä (HTTP %1)").arg(http);
    if (!teksti.isEmpty() || !koodi.isEmpty())
        viesti.append(QString(": %1 (%2)").arg(teksti, koodi));
    return viesti;
}

static QString ymparistoEtuliite(Ymparisto ymparisto)
{
    return ymparisto == TUOTANTO ? QString() : QStringLiteral("T");
}

QString kirjeViite(Ymparisto ymparisto, const QString &kirjenumero)
{
    // Tositteen viite-haku poistaa kaikki muut kuin kirjain- ja numeromerkit
    // eikä viite saa olla yli 64 merkkiä
    QString puhdas = kirjenumero;
    puhdas.remove(QRegularExpression(QStringLiteral("\\W")));
    const QString alku = ymparistoEtuliite(ymparisto) + QStringLiteral("VKI");
    return alku + puhdas.left(64 - alku.length());
}

QString lahetettyAlvViite(Ymparisto ymparisto, const QDate &kausiLoppu)
{
    return ymparistoEtuliite(ymparisto) + QStringLiteral("FVR") + kausiLoppu.toString(QStringLiteral("yyyyMMdd"));
}

QString saapunutAlvViite(Ymparisto ymparisto, const QDate &kausiLoppu)
{
    return ymparistoEtuliite(ymparisto) + QStringLiteral("GFV") + kausiLoppu.toString(QStringLiteral("yyyyMMdd"));
}

bool harjoitukselleSallittu(bool harjoitus, Ymparisto ymparisto)
{
    return !harjoitus || ymparisto != TUOTANTO;
}

static bool onLuku(const QVariant& arvo)
{
    switch (arvo.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return true;
    default:
        return false;
    }
}

static bool samaArvo(const QVariant& a, const QVariant& b)
{
    if (onLuku(a) && onLuku(b))
        return std::abs(a.toDouble() - b.toDouble()) <= 0.000001;
    if (a.typeId() == QMetaType::QVariantMap || b.typeId() == QMetaType::QVariantMap) {
        const QVariantMap vasen = a.toMap();
        const QVariantMap oikea = b.toMap();
        if (vasen.size() != oikea.size())
            return false;
        for (auto it = vasen.constBegin(); it != vasen.constEnd(); ++it) {
            if (!oikea.contains(it.key()) || !samaArvo(it.value(), oikea.value(it.key())))
                return false;
        }
        return true;
    }
    if (a.typeId() == QMetaType::QVariantList || b.typeId() == QMetaType::QVariantList) {
        const QVariantList vasen = a.toList();
        const QVariantList oikea = b.toList();
        if (vasen.size() != oikea.size())
            return false;
        for (int i = 0; i < vasen.size(); ++i) {
            if (!samaArvo(vasen.at(i), oikea.at(i)))
                return false;
        }
        return true;
    }
    return a == b;
}

static QVariantMap ilmoituksenRunko(const QVariantMap& map)
{
    if (map.contains(QStringLiteral("BusinessId")) || map.contains(QStringLiteral("VATDetails")))
        return map;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        const QVariantMap sisus = it.value().toMap();
        if (sisus.contains(QStringLiteral("BusinessId")) || sisus.contains(QStringLiteral("VATDetails")))
            return sisus;
    }
    return {};
}

bool samaAlvIlmoitus(const QVariantMap& lahetetty, const QVariantMap& haettu)
{
    if (lahetetty.isEmpty() || haettu.isEmpty())
        return false;
    const QVariantMap runko = ilmoituksenRunko(haettu);
    if (runko.isEmpty())
        return false;
    if (!lahetetty.contains(QStringLiteral("BusinessId")) || !lahetetty.contains(QStringLiteral("FilingPeriod")))
        return false;
    if (lahetetty.value(QStringLiteral("BusinessId")).toString() != runko.value(QStringLiteral("BusinessId")).toString())
        return false;
    if (lahetetty.value(QStringLiteral("FilingPeriod")).toString() != runko.value(QStringLiteral("FilingPeriod")).toString())
        return false;

    const QStringList avaimet = {
        QStringLiteral("ReplacementReturn"),
        QStringLiteral("ReplacementReason"),
        QStringLiteral("NoActivity"),
        QStringLiteral("VATDetails")
    };
    for (const QString& avain : avaimet) {
        if (!lahetetty.contains(avain))
            continue;
        if (!runko.contains(avain) || !samaArvo(lahetetty.value(avain), runko.value(avain)))
            return false;
    }
    return true;
}

static bool sailytaAiempiLahetys(const QVariantMap& aiempi)
{
    const QString tila = aiempi.value(QStringLiteral("tila")).toString();
    return tila == QLatin1String("kasittelyssa")
            || tila == QLatin1String("kasitelty")
            || tila == QLatin1String("lahettamassa")
            || tila == QLatin1String("tuntematon");
}

QVariantMap epaonnistuneenLahetyksenVero(const QVariantMap& aiempiVero,
                                         const QVariantMap& uudenYrityksenVero,
                                         const QVariantMap& tapahtuma)
{
    QVariantMap tulos;
    if (sailytaAiempiLahetys(aiempiVero)) {
        tulos = aiempiVero;
    } else {
        tulos = uudenYrityksenVero;
        tulos.insert(QStringLiteral("tila"), QStringLiteral("hylatty"));
    }
    QVariantList lahetykset = tulos.value(QStringLiteral("lahetykset")).toList();
    if (!tapahtuma.isEmpty())
        lahetykset.append(tapahtuma);
    tulos.insert(QStringLiteral("lahetykset"), lahetykset);
    return tulos;
}

QString lahetysVahvistus(int lahetettyId, const QVariantMap &vero)
{
    if (lahetettyId <= 0)
        return QString();
    return QStringLiteral("%1|%2|%3|%4")
            .arg(lahetettyId)
            .arg(vero.value(QStringLiteral("tila")).toString())
            .arg(vero.value(QStringLiteral("yritetty")).toString())
            .arg(vero.value(QStringLiteral("lahetykset")).toList().size());
}

bool onVarmaHttpHylkays(int http, const QString &verkkovirhe)
{
    return verkkovirhe.isEmpty() && http >= 400 && http < 500;
}

QStringList puuttuvatLiitteet(const QStringList &odotetut, const QStringList &tallennetut)
{
    QStringList puuttuvat;
    for (const QString& nimi : odotetut) {
        if (!tallennetut.contains(nimi))
            puuttuvat << nimi;
    }
    return puuttuvat;
}

QStringList tukemattomatKoodit(const QVariantMap &koodit)
{
    QStringList tukemattomat;
    for (const QString& koodi : {QStringLiteral("315"), QStringLiteral("316"), QStringLiteral("317")}) {
        if (koodit.value(koodi).toLongLong() != 0)
            tukemattomat << koodi;
    }
    return tukemattomat;
}

QDate rajattuPvm(const QDate &pvm, const QDate &alku, const QDate &loppu)
{
    QDate paiva = pvm.isValid() ? pvm : alku;
    if (alku.isValid() && paiva < alku)
        paiva = alku;
    if (loppu.isValid() && paiva > loppu)
        paiva = loppu;
    return paiva;
}

}   // namespace Vero
