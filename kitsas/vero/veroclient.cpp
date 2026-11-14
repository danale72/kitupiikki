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
#include "veroclient.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>

namespace Vero {

QVariantMap Vastaus::json() const
{
    QJsonParseError virhe;
    const QJsonDocument doc = QJsonDocument::fromJson(runko, &virhe);
    if (virhe.error != QJsonParseError::NoError || !doc.isObject())
        return QVariantMap();
    return doc.object().toVariantMap();
}

bool onVerkkotasonVirhe(QNetworkReply::NetworkError virhe, int http)
{
    if (virhe == QNetworkReply::NoError)
        return false;
    // Rikkoutunut runko HTTP 200:n jälkeen. HTTP 400 on Qt:ssa samaa enum-ryhmää,
    // mutta se on Verohallinnon hylkäys, kun tilakoodi on 400.
    if (virhe == QNetworkReply::ProtocolFailure)
        return true;
    if (virhe == QNetworkReply::ProtocolUnknownError
            || virhe == QNetworkReply::ProtocolInvalidOperationError)
        return http == 0 || http == 200;
    return virhe < QNetworkReply::ContentAccessDenied;
}

/// Lukee PEM-yksityisavaimen RSA- tai EC-muodossa (Verohallinnon asiakasvarmenteet).
QSslKey lueYksityisavain(const QByteArray& pem)
{
    const QSslKey rsa(pem, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    if (!rsa.isNull())
        return rsa;
    return QSslKey(pem, QSsl::Ec, QSsl::Pem, QSsl::PrivateKey);
}

/// Varmistaa, että yksityisavain sopii varmenteeseen (algoritmi ja pituus).
/// Qt ei tarjoa alustariippumatonta avainmateriaalin vertailua; samanpituinen
/// väärä RSA-avain jää TLS-kättelyn hylättäväksi.
bool avainVastaaVarmennetta(const QSslCertificate& varmenne, const QSslKey& avain)
{
    const QSslKey julkinen = varmenne.publicKey();
    if (julkinen.isNull() || julkinen.algorithm() != avain.algorithm())
        return false;
    // Pituus -1 / 0 = ei tiedossa joillain taustoilla
    if (julkinen.length() > 0 && avain.length() > 0 && julkinen.length() != avain.length())
        return false;
    return true;
}

}   // namespace Vero

VeroClient::VeroClient(QObject *parent) :
    QObject(parent),
    verkko_(new QNetworkAccessManager(this)),
    userAgent_(QStringLiteral("kitsas-vero/1.0"))
{
}

bool VeroClient::asetaVarmenne(const QString &varmennePolku, const QString &avainPolku, QString *virhe)
{
    const auto epaonnistui = [virhe](const QString& selitys) {
        if (virhe)
            *virhe = selitys;
        return false;
    };

    varmenne_ = QSslCertificate();
    avain_ = QSslKey();

    QFile varmenneTiedosto(varmennePolku);
    if (!varmenneTiedosto.open(QIODevice::ReadOnly))
        return epaonnistui(QCoreApplication::translate("Vero", "Varmennetiedostoa %1 ei voi lukea").arg(varmennePolku));
    const QList<QSslCertificate> varmenteet = QSslCertificate::fromData(varmenneTiedosto.readAll(), QSsl::Pem);
    if (varmenteet.isEmpty() || varmenteet.first().isNull())
        return epaonnistui(QCoreApplication::translate("Vero", "Tiedosto %1 ei ole PEM-muotoinen varmenne").arg(varmennePolku));

    QFile avainTiedosto(avainPolku);
    if (!avainTiedosto.open(QIODevice::ReadOnly))
        return epaonnistui(QCoreApplication::translate("Vero", "Avaintiedostoa %1 ei voi lukea").arg(avainPolku));
    const QSslKey avain = Vero::lueYksityisavain(avainTiedosto.readAll());
    if (avain.isNull())
        return epaonnistui(QCoreApplication::translate("Vero", "Tiedosto %1 ei ole PEM-muotoinen RSA- tai EC-yksityisavain").arg(avainPolku));

    const QSslCertificate varmenne = varmenteet.first();
    if (!Vero::avainVastaaVarmennetta(varmenne, avain))
        return epaonnistui(QCoreApplication::translate("Vero", "Yksityisavain ei vastaa varmennetta (algoritmi tai pituus). Tarkista tiedostot."));

    varmenne_ = varmenne;
    avain_ = avain;
    return true;
}

void VeroClient::asetaOhjelma(const QString &id, const QString &avain)
{
    ohjelmaId_ = id;
    ohjelmaAvain_ = avain;
}

void VeroClient::asetaUserAgent(const QString &userAgent)
{
    userAgent_ = userAgent;
}

QString VeroClient::varmenteenYtunnus() const
{
    if (varmenne_.isNull())
        return QString();
    const QStringList cn = varmenne_.subjectInfo(QSslCertificate::CommonName);
    return cn.isEmpty() ? QString() : Vero::normalisoiYtunnus(cn.first());
}

bool VeroClient::onkoTestivarmenne() const
{
    if (varmenne_.isNull())
        return false;
    const QStringList cn = varmenne_.issuerInfo(QSslCertificate::CommonName);
    return !cn.isEmpty() && cn.first().contains(QLatin1String("Test"), Qt::CaseInsensitive);
}

void VeroClient::kutsu(const QUrl &osoite, const QVariantMap &sanoma, const QString &valtuutusToken, bool pdf,
                       const std::function<void (const Vero::Vastaus &)> &valmis)
{
    QUrl url = osoite;
    if (testipalvelin_.isValid() && !testipalvelin_.isEmpty()) {
        url.setScheme(testipalvelin_.scheme());
        url.setHost(testipalvelin_.host());
        url.setPort(testipalvelin_.port());
    }

    QNetworkRequest pyynto(url);
    const Vero::Otsikot otsikot = Vero::otsikot(ohjelmaId_, ohjelmaAvain_, userAgent_, pdf, valtuutusToken);
    for (const auto& otsikko : otsikot)
        pyynto.setRawHeader(otsikko.first, otsikko.second);
    pyynto.setTransferTimeout(60 * 1000);

    if (url.scheme() == QLatin1String("https")) {
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        if (!varmenne_.isNull()) {
            ssl.setLocalCertificate(varmenne_);
            ssl.setPrivateKey(avain_);
        }
        pyynto.setSslConfiguration(ssl);
    }

    const QByteArray runko = QJsonDocument::fromVariant(sanoma).toJson(QJsonDocument::Compact);
    QNetworkReply* vastaus = verkko_->post(pyynto, runko);

    // Asiakas omistaa vastauksen, joten vastauksen käsittely loppuu asiakkaan mukana
    vastaus->setParent(this);
    connect(vastaus, &QNetworkReply::finished, this, [vastaus, valmis] {
        Vero::Vastaus tulos;
        tulos.http = vastaus->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        tulos.runko = vastaus->readAll();
        tulos.sisaltotyyppi = vastaus->header(QNetworkRequest::ContentTypeHeader).toString();
        // Verkkotason virhe (yhteys, aikakatkaisu, SSL, katkennut luku) erotetaan HTTP-virheistä,
        // joiden sanoma on Verohallinnon. QNetworkReply luokittelee HTTP 4xx/5xx sisältövirheiksi
        // (koodit >= ContentAccessDenied), joten ne eivät ylitä rajaa.
        const QNetworkReply::NetworkError virhe = vastaus->error();
        if (Vero::onVerkkotasonVirhe(virhe, tulos.http) || !tulos.http) {
            tulos.verkkovirhe = vastaus->errorString();
            if (tulos.verkkovirhe.isEmpty())
                tulos.verkkovirhe = QString::number(static_cast<int>(virhe));
        }
        vastaus->deleteLater();
        valmis(tulos);
    });
}
