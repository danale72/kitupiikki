/*
   Copyright (C) 2019 Arto Hyvättinen

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
#include "asetuksetroute.h"

#include <QRegularExpression>
#include <QSqlError>

namespace {

/// Tuore varaus pidetään, jotta käynnissä oleva lähetys (aikakatkaisu 60 s) ehtii valmistua.
/// Tätä vanhempi varaus on jäänyt kesken kaatuneesta yhteydestä ja sen saa ottaa.
const int VARAUS_VANHENEE_SEKUNTEINA = 180;

bool kelpaaTunniste(const QString& teksti, int eninta)
{
    static const QRegularExpression sallittu(QStringLiteral("^[A-Za-z0-9-]+$"));
    return !teksti.isEmpty() && teksti.length() <= eninta && sallittu.match(teksti).hasMatch();
}

/// Varauksen ikä sekunteina tietokannan kellosta, tai -1 jos varausta ei ole.
/// SQLite ja Postgres ilmaisevat erotuksen eri funktioilla; tulos on sekunteja molemmissa.
int varauksenIka(const QSqlDatabase& db, const QString& avain, QString* tunnus)
{
    QSqlQuery kysely(db);
    if (db.driverName().contains(QLatin1String("PSQL"), Qt::CaseInsensitive))
        kysely.prepare("SELECT arvo, EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - muokattu)) "
                       "FROM Asetus WHERE avain=?");
    else
        kysely.prepare("SELECT arvo, CAST((julianday('now') - julianday(muokattu)) * 86400 AS INTEGER) "
                       "FROM Asetus WHERE avain=?");
    kysely.addBindValue(avain);
    if (!kysely.exec() || !kysely.next())
        return -1;
    if (tunnus)
        *tunnus = kysely.value(0).toString();
    return kysely.value(1).toInt();
}

}   // namespace

AsetuksetRoute::AsetuksetRoute(SqlModel *model) :
    SQLiteRoute(model, "/asetukset")
{

}

QString AsetuksetRoute::varausAvain(const QString &viite)
{
    return QStringLiteral("VeroLahetys/") + viite;
}

QVariant AsetuksetRoute::get(const QString &polku, const QUrlQuery &urlquery)
{
    if (polku.startsWith(QLatin1String("verovaraus")))
        return haeVaraus(polku.section('/', 1), urlquery.queryItemValue(QStringLiteral("tunnus")));

    QVariantMap asetukset;
    QSqlQuery kysely(db());
    kysely.exec("SELECT avain,arvo FROM Asetus");
    while( kysely.next()) {
        const QString avain = kysely.value(0).toString();
        // Lähetysvarauksen tunnus ei kuulu asetusten yleiseen lukemiseen
        if (avain.startsWith(QLatin1String("VeroLahetys/")))
            continue;
        asetukset.insert(avain, kysely.value(1));
    }
    return asetukset;
}

QVariant AsetuksetRoute::post(const QString &polku, const QVariant &data)
{
    if (polku == QLatin1String("verovaraus"))
        return varaaLahetys(data);
    return QVariant();
}

QVariant AsetuksetRoute::haeVaraus(const QString &viite, const QString &tunnus)
{
    if (!kelpaaTunniste(viite, 64))
        return QVariantMap();
    QString nykyinen;
    const int ika = varauksenIka(db(), varausAvain(viite), &nykyinen);
    if (ika < 0)
        return QVariantMap();
    // Tunnusta ei palauteta: sen tietävä yhteys voi vapauttaa toisen varauksen.
    // Omistus tarkistetaan vertaamalla pyytäjän tunnusta rivin arvoon.
    QVariantMap tulos{{QStringLiteral("ika"), ika}};
    if (kelpaaTunniste(tunnus, 64))
        tulos.insert(QStringLiteral("oma"), nykyinen == tunnus);
    return tulos;
}

QVariant AsetuksetRoute::varaaLahetys(const QVariant &data)
{
    const QVariantMap pyynto = data.toMap();
    const QString viite = pyynto.value(QStringLiteral("viite")).toString();
    const QString tunnus = pyynto.value(QStringLiteral("tunnus")).toString();
    const bool korvaa = pyynto.value(QStringLiteral("korvaa")).toBool();
    if (!kelpaaTunniste(viite, 64) || !kelpaaTunniste(tunnus, 64))
        return QVariantMap{{QStringLiteral("varattu"), false}};

    const QString avain = varausAvain(viite);
    QSqlQuery lisays(db());
    lisays.prepare("INSERT INTO Asetus (avain, arvo, muokattu) VALUES (?,?,current_timestamp)");
    lisays.addBindValue(avain);
    lisays.addBindValue(tunnus);
    if (lisays.exec())
        return QVariantMap{{QStringLiteral("varattu"), true}};

    // Pääavain esti toisen varauksen. Vanhentuneen saa ottaa vain, jos pyytäjä sallii
    // korvaamisen: käynnissä olevaa lähetystä ei ohiteta.
    const QString virhe = lisays.lastError().text();
    const bool ristiriita = lisays.lastError().nativeErrorCode() == QLatin1String("23505")
            || virhe.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive);
    if (!ristiriita) {
        qWarning() << "Vero-lähetyksen varaus epäonnistui:" << virhe;
        return QVariantMap{{QStringLiteral("varattu"), false}};
    }

    QString nykyinen;
    const int ika = varauksenIka(db(), avain, &nykyinen);
    // Sama tunnus uudistaa oman varauksen. Hidas token- tai POST-kutsu ei saa
    // vanheta alta, eikä toinen yhteys saa ottaa vuokraa kesken kirjoituksen.
    if (nykyinen == tunnus) {
        QSqlQuery paivitys(db());
        paivitys.prepare("UPDATE Asetus SET muokattu=current_timestamp WHERE avain=? AND arvo=?");
        paivitys.addBindValue(avain);
        paivitys.addBindValue(tunnus);
        if (!paivitys.exec() || paivitys.numRowsAffected() < 1)
            return QVariantMap{{QStringLiteral("varattu"), false}, {QStringLiteral("ika"), ika}};
        return QVariantMap{{QStringLiteral("varattu"), true}, {QStringLiteral("ika"), 0}};
    }
    if (!korvaa || nykyinen.isEmpty())
        return QVariantMap{{QStringLiteral("varattu"), false}, {QStringLiteral("ika"), ika}};

    // Ikäraja on samassa UPDATE-lauseessa kuin tunnuksen vaihto, jotta toisen
    // yhteyden uudistus ehtii estää vanhentuneen rivin varastamisen (TOCTOU).
    const bool postgres = db().driverName().contains(QLatin1String("PSQL"), Qt::CaseInsensitive);
    QSqlQuery paivitys(db());
    if (postgres)
        paivitys.prepare(QStringLiteral(
            "UPDATE Asetus SET arvo=?, muokattu=current_timestamp "
            "WHERE avain=? AND arvo=? "
            "AND EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - muokattu)) >= ?"));
    else
        paivitys.prepare(QStringLiteral(
            "UPDATE Asetus SET arvo=?, muokattu=current_timestamp "
            "WHERE avain=? AND arvo=? "
            "AND CAST((julianday('now') - julianday(muokattu)) * 86400 AS INTEGER) >= ?"));
    paivitys.addBindValue(tunnus);
    paivitys.addBindValue(avain);
    paivitys.addBindValue(nykyinen);
    paivitys.addBindValue(VARAUS_VANHENEE_SEKUNTEINA);
    if (!paivitys.exec() || paivitys.numRowsAffected() < 1) {
        const int tuoreIka = varauksenIka(db(), avain, nullptr);
        return QVariantMap{{QStringLiteral("varattu"), false},
                           {QStringLiteral("ika"), tuoreIka >= 0 ? tuoreIka : ika}};
    }
    return QVariantMap{{QStringLiteral("varattu"), true}, {QStringLiteral("ika"), ika}};
}

QVariant AsetuksetRoute::doDelete(const QString &polku)
{
    if (!polku.startsWith(QLatin1String("verovaraus/")))
        return QVariant();
    const QStringList osat = polku.split('/');
    if (osat.count() != 3)
        return QVariantMap{{QStringLiteral("vapautettu"), false}};
    return vapautaVaraus(osat.at(1), osat.at(2));
}

QVariant AsetuksetRoute::vapautaVaraus(const QString &viite, const QString &tunnus)
{
    if (!kelpaaTunniste(viite, 64) || !kelpaaTunniste(tunnus, 64))
        return QVariantMap{{QStringLiteral("vapautettu"), false}};

    QSqlQuery poisto(db());
    poisto.prepare("DELETE FROM Asetus WHERE avain=? AND arvo=?");
    poisto.addBindValue(varausAvain(viite));
    poisto.addBindValue(tunnus);
    const bool onnistui = poisto.exec() && poisto.numRowsAffected() > 0;
    return QVariantMap{{QStringLiteral("vapautettu"), onnistui}};
}

QVariant AsetuksetRoute::patch(const QString &/*polku*/, const QVariant &data)
{
    QSqlQuery lisaaja(db());
    lisaaja.prepare("INSERT INTO Asetus (avain,arvo,muokattu) VALUES (?,?,current_timestamp) "
                    "ON CONFLICT (avain) DO UPDATE SET arvo = EXCLUDED.arvo, muokattu=current_timestamp ");
    QSqlQuery poistaja(db());

    QVariantMap map = data.toMap();
    QMapIterator<QString,QVariant> iter(map);
    while(iter.hasNext()) {
        iter.next();
        if( iter.value().isNull()) {
            poistaja.exec(QString("DELETE FROM asetus WHERE avain = '%1'").arg(iter.key()));
        } else {
            lisaaja.addBindValue(iter.key());
            lisaaja.addBindValue(iter.value());
            lisaaja.exec();
        }
    }
    return QVariant();
}
