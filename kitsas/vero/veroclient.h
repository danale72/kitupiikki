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
#ifndef VEROCLIENT_H
#define VEROCLIENT_H

#include <QNetworkReply>
#include <QObject>
#include <QSslCertificate>
#include <QSslKey>
#include <QUrl>
#include <QVariantMap>

#include <functional>

#include "veroapu.h"

class QNetworkAccessManager;

namespace Vero {

/**
 * @brief Verohallinnon vastaus
 *
 * http == 0 tarkoittaa, että vastausta ei saatu lainkaan. Verkkovirhe voi tulla myös
 * HTTP-otsikoiden jälkeen (aikakatkaisu tai yhteyden katkeaminen vastausta luettaessa),
 * jolloin runko on epätäydellinen. Silloin verkkovirhe on asetettu eikä vastausta saa
 * hyväksyä, vaikka HTTP-tila olisi 200. Verohallinnon omat virheet (HTTP 4xx/5xx)
 * eivät ole verkkovirheitä: niissä http on asetettu ja runko luettavissa.
 */
struct Vastaus {
    int http = 0;
    QByteArray runko;
    QString sisaltotyyppi;
    QString verkkovirhe;

    bool ok() const { return http == 200 && verkkovirhe.isEmpty(); }
    QVariantMap json() const;
};

/**
 * @brief Onko QNetworkReply-virhe verkkotason virhe, ei Verohallinnon HTTP-vastaus
 *
 * HTTP 4xx/5xx ovat sisältö- ja palvelinvirheitä. Qt kuvaa HTTP 400:n koodilla
 * ProtocolInvalidOperationError, joten pelkkä protokollakoodi ei riitä.
 * ProtocolFailure HTTP 200 -otsikoiden jälkeen on verkkovirhe: runkoon ei saa luottaa.
 */
bool onVerkkotasonVirhe(QNetworkReply::NetworkError virhe, int http);

}   // namespace Vero

/**
 * @brief Vero API -asiakas: kutsut tehdään asiakasvarmenteella (mTLS)
 *
 * Asiakas ei tunne kirjanpitoa. Varmenne, avain ja ohjelmatunnisteet annetaan
 * ulkopuolelta (asetuksista), eikä niitä tallenneta mihinkään.
 */
class VeroClient : public QObject
{
    Q_OBJECT
public:
    explicit VeroClient(QObject *parent = nullptr);

    /**
     * @brief Lataa asiakasvarmenteen ja sen yksityisen avaimen PEM-tiedostoista
     * @param virhe Virheen selitys, jos lataus epäonnistuu
     */
    bool asetaVarmenne(const QString& varmennePolku, const QString& avainPolku, QString* virhe = nullptr);
    void asetaOhjelma(const QString& id, const QString& avain);
    void asetaUserAgent(const QString& userAgent);

    QSslCertificate varmenne() const { return varmenne_; }
    /// Varmenteen CN eli tilitoimiston (valtuutetun) Y-tunnus
    QString varmenteenYtunnus() const;
    /// Varmenne on testiympäristön varmenne, jos sen myöntäjän nimessä on "Test"
    bool onkoTestivarmenne() const;

    /**
     * @brief Ohjaa kaikki kutsut toiseen palvelimeen (vain testejä varten)
     *
     * Korvaa osoitteen skeeman, palvelimen ja portin, polku säilyy.
     */
    void asetaTestipalvelin(const QUrl& osoite) { testipalvelin_ = osoite; }

    /**
     * @brief POST-kutsu JSON-sanomalla
     * @param valmis Kutsutaan kerran vastauksen saavuttua tai virheen jälkeen
     */
    void kutsu(const QUrl& osoite, const QVariantMap& sanoma, const QString& valtuutusToken, bool pdf,
               const std::function<void(const Vero::Vastaus&)>& valmis);

private:
    QNetworkAccessManager* verkko_;
    QSslCertificate varmenne_;
    QSslKey avain_;
    QString ohjelmaId_;
    QString ohjelmaAvain_;
    QString userAgent_;
    QUrl testipalvelin_;
};

#endif // VEROCLIENT_H
