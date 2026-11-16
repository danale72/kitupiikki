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
#ifndef VEROAPU_H
#define VEROAPU_H

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

/**
 * @brief Verohallinnon rajapinnan (Vero API) apufunktiot
 *
 * Tämä tiedosto ei riipu kirjanpidosta (kp()) eikä tietokannasta, jotta
 * kaikki rajapinnan muoto-, osoite- ja muunnoslogiikka voidaan testata
 * yksikkötesteillä.
 */
namespace Vero {

enum Ymparisto { TESTI = 0, TUOTANTO = 1 };

enum Rajapinta {
    GETTOKEN,
    LIST_LETTERS,
    GET_LETTER,
    GET_VAT_PERIODS,
    GET_FILED_VAT_RETURN,
    FILE_VAT_RETURN
};

/// Ympäristön tunniste asetuksissa: "testi" tai "tuotanto"
QString ymparistoAvain(Ymparisto ymparisto);
Ymparisto ymparistoAvaimesta(const QString& avain);
QString ymparistoNimi(Ymparisto ymparisto);

/// Rajapintakutsun osoite
QUrl osoite(Ymparisto ymparisto, Rajapinta rajapinta);

/**
 * @brief Y-tunnus muodossa 1234567-8
 * @param teksti Y-tunnus tai alv-tunnus (FI12345678), välilyönnit sallitaan
 * @return Normalisoitu Y-tunnus tai tyhjä, jos tunnus ei ole kelvollinen
 */
QString normalisoiYtunnus(const QString& teksti);

typedef QList<QPair<QByteArray,QByteArray>> Otsikot;

/// Kutsujen HTTP-otsikot. Valtuutustoken lisätään vain, jos se on annettu.
Otsikot otsikot(const QString& ohjelmaId, const QString& ohjelmaAvain, const QString& userAgent,
                bool pdf = false, const QString& valtuutusToken = QString());

// ---- ALV-ilmoituksen (FileVATReturn v2) muodostaminen kirjanpidon alv-koodeista

/// Mikä FileVATReturn-kenttä vastaa alv-ilmoituksen kooditunnusta (301-320)
struct KoodiKentta {
    int koodi;
    const char* ryhma;   ///< "VATDetails"-alainen ryhmä tai tyhjä (suoraan VATDetails-alla)
    const char* kentta;
};
const QList<KoodiKentta>& koodiKentat();

/// Onko kausilla toimintaa eli onko jokin koodeista 301-320 nollasta poikkeava
bool onkoToimintaa(const QVariantMap& koodit);

/**
 * @param koodit Tositteen alv-koodit sentteinä (alv/koodit), avaimena koodin numero
 * @param kausiLoppu Alv-kauden viimeinen päivä
 */
QVariantMap fileVatReturn(const QString& ytunnus, const QDate& kausiLoppu, const QVariantMap& koodit,
                          const QString& yhteyshenkilo, const QString& puhelin,
                          bool korvaava = false, const QString& korvauksenSyy = QString(),
                          bool maksuperusteinen = false);

/// Palauttaa listan virheistä, tyhjä lista tarkoittaa että sanoman voi lähettää
QStringList validoiFileVatReturn(const QVariantMap& sanoma);

extern const QStringList korvauksenSyyt;   ///< CLC, LGL, TXA, LAW

// ---- Vastausten tulkinta

struct Kirje {
    QString numero;
    QString tyyppi;
    QString nimiFi;
    QString nimiEn;
    QDate paivays;
};
QList<Kirje> kirjeet(const QVariantMap& vastaus);
/// Vanhin päivä kirjeluettelossa, tai tyhjä päivä jos luettelo on tyhjä
QDate vanhinPaivays(const QList<Kirje>& kirjeet);

struct Verokausi {
    QDate alkaa;
    QDate loppuu;
    QDate erapaiva;
    QString tila;      ///< Verohallinnon tila, esim. "Processed"
    QString tiheys;    ///< VATMON, VATQTR, VATYEAR
    bool kasitelty() const { return tila == QLatin1String("Processed"); }
};
QList<Verokausi> verokaudet(const QVariantMap& vastaus);

/// GetToken-vastauksen tokenit: asiakkaan Y-tunnus -> token
QMap<QString,QString> tokenit(const QVariantMap& vastaus);

/// Käyttäjälle näytettävä virheteksti epäonnistuneesta kutsusta
QString virheteksti(int http, const QVariantMap& runko, const QString& verkkovirhe = QString());

// ---- Tositteiden tunnisteet (tallennetaan tositteen viite-kenttään)
// Viite-kenttä on ainoa indeksoitavissa oleva kenttä, jonka avulla jo haettu
// tai lähetetty sanoma tunnistetaan ilman tietokannan skeeman muutosta.

// Testi- ja tuotantoympäristön sanomat ovat samassa kirjanpidossa mahdollisesti rinnakkain,
// joten ympäristö kuuluu tunnisteeseen: tuotannon tunnisteet ovat FVR/GFV/VKI-alkuisia ja
// testiympäristön tunnisteissa on lisäksi etuliite "T" (TFVR/TGFV/TVKI). Testisanoma ei
// siten koskaan näytä tuotantosanomalta.

QString kirjeViite(Ymparisto ymparisto, const QString& kirjenumero);
QString lahetettyAlvViite(Ymparisto ymparisto, const QDate& kausiLoppu);
QString saapunutAlvViite(Ymparisto ymparisto, const QDate& kausiLoppu);

/**
 * @brief Alv-koodit, joita FileVATReturn-sanomaan ei vielä osata muodostaa
 *
 * Alarajahuojennuksen koodit 315-317 voivat esiintyä vanhoissa alv-laskelmissa, mutta
 * niitä ei viedä sanomaan (VATRelief). Jos laskelmassa on niille nollasta poikkeava
 * summa, ilmoitusta ei saa lähettää, koska huojennus jäisi ilmoittamatta.
 *
 * @return Nollasta poikkeavat tukemattomat koodit numeroina, tyhjä jos ilmoituksen voi lähettää
 */
QStringList tukemattomatKoodit(const QVariantMap& koodit);

/// Nimet, jotka odotettiin liitteiksi mutta joita tositteella ei ole
QStringList puuttuvatLiitteet(const QStringList& odotetut, const QStringList& tallennetut);

/// Harjoituskirjanpito saa käyttää vain testiympäristöä, ei tuotantoa
bool harjoitukselleSallittu(bool harjoitus, Ymparisto ymparisto);

/**
 * @brief Onko haettu käsitelty ilmoitus sama kuin tällä ohjelmalla lähetetty sanoma
 *
 * Vertaa vakaita kenttiä (BusinessId, FilingPeriod, ReplacementReturn, NoActivity, VATDetails).
 * Haetun vastauksen ylimääräiset kentät eivät estä osumaa. Tyhjä tai tunnistamaton vastaus
 * ei ole osuma: tilaa ei saa silloin päätellä käsitellyksi.
 */
bool samaAlvIlmoitus(const QVariantMap& lahetetty, const QVariantMap& haettu);

/**
 * @brief Epäonnistuneen yrityksen jälkeen tositteelle jäävä vero-kartta
 *
 * Jos aiempi lähetys on jo vastaanotettu, jonossa, kesken tai tulokseltaan tuntematon,
 * sen tila, sanoma ja vastaus säilyvät. Hylätty yritys kirjataan vain lahetykset-listaan.
 * Jos aiempaa sellaista lähetystä ei ole, uusi yritys jää tilaan hylätty.
 *
 * @param aiempiVero Aiemman lähetetyn tositteen vero-kartta, tai tyhjä jos tositetta ei ole
 * @param uudenYrityksenVero Juuri yritetty sanoma (lähettämässä-varaus)
 * @param tapahtuma Yrityksen kirjaus (aika, http, tulos); tyhjä kartta ei lisää riviä
 */
QVariantMap epaonnistuneenLahetyksenVero(const QVariantMap& aiempiVero,
                                         const QVariantMap& uudenYrityksenVero,
                                         const QVariantMap& tapahtuma);

/**
 * @brief Lähetetyn tositteen tilannekuva
 *
 * Muuttuu, kun sama tila saa uuden yrityksen (yritetty-aika tai lahetykset-lista).
 * Tyhjä, jos lähetettyä tositetta ei ole. Kahden vahvistuksen vertailu huomaa
 * yrityksen, jonka tilakoodi pysyi samana.
 */
QString lahetysVahvistus(int lahetettyId, const QVariantMap& vero);

/**
 * @brief Onko HTTP-vastaus varma hylkäys
 *
 * 4xx tarkoittaa, ettei ilmoitusta otettu vastaan. 5xx ja verkkovirhe voivat tulla
 * sen jälkeen, kun palvelin on jo vastaanottanut POSTin, joten ne eivät ole hylkäys.
 */
bool onVarmaHttpHylkays(int http, const QString& verkkovirhe);

/// Tositteen päivämäärä rajataan kirjanpidon alkuun ja loppuun,
/// jotta tosite voidaan aina tallentaa
QDate rajattuPvm(const QDate& pvm, const QDate& alku, const QDate& loppu);

}   // namespace Vero

#endif // VEROAPU_H
