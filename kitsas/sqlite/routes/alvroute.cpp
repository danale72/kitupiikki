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
#include "alvroute.h"
#include "db/tositetyyppimodel.h"

#include <QDate>
#include <QJsonDocument>

AlvRoute::AlvRoute(SqlModel *model)
    : SQLiteRoute(model, "/alv")
{

}

QVariant AlvRoute::get(const QString &/*polku*/, const QUrlQuery &urlquery)
{
    QSqlQuery kysely(db());

    kysely.exec( QString("SELECT id, json FROM Tosite WHERE tyyppi=%1 AND tila>=100 "
                         "ORDER BY pvm DESC").arg(TositeTyyppi::ALVLASKELMA));

    QVariantList kyselyntulos = resultList(kysely);
    QVariantList vastaus;

    // Sormenjäljet lasketaan vain pyydettäessä (alv-sivulla), koska ne vaativat
    // koosteen kaikkien ilmoitettujen kausien kirjauksista
    const bool tarkista = urlquery.hasQueryItem("tarkista");
    QList<QPair<QDate,QDate>> tarkistettavat;
    if( tarkista ) {
        for(const QVariant& item : qAsConst( kyselyntulos )) {
            const QVariantMap vmap = item.toMap().value("alv").toMap();
            if( !vmap.value("muokattu").toBool() && vmap.contains("sormenjalki"))
                tarkistettavat.append(qMakePair(vmap.value("kausialkaa").toDate(), vmap.value("kausipaattyy").toDate()));
        }
    }
    const QList<QVariantMap> jaljet = sormenjaljet(db(), tarkistettavat);
    int jalkiIndeksi = 0;

    for(const QVariant& item : qAsConst( kyselyntulos )) {
        QVariantMap map = item.toMap();
        QVariantMap vmap = map.value("alv").toMap();

        // Muokattu: kauden alv-kirjaukset poikkeavat ilmoitusta laadittaessa tallennetusta
        // sormenjäljestä. Tarkistettu: ilmoituksella on sormenjälki tai muokattu-merkintä;
        // vanhoille ilmoituksille AlvIlmoitustenModel tekee tarkistuksen kertaalleen.
        bool tarkistettu = true;
        bool muokattu = vmap.value("muokattu").toBool();
        if( !muokattu && vmap.contains("sormenjalki")) {
            if( tarkista )
                muokattu = !samaSormenjalki( vmap.value("sormenjalki").toMap(), jaljet.value(jalkiIndeksi++));
        } else if( !muokattu ) {
            tarkistettu = false;
        }
        vmap.remove("sormenjalki");
        if( tarkistettu )
            vmap.remove("koodit");
        vmap.insert("muokattu", muokattu);
        vmap.insert("tarkistettu", tarkistettu);
        vmap.insert("id", map.value("id"));
        vastaus.append(vmap);
    }
    return vastaus;
}

QVariant AlvRoute::put(const QString &polku, const QVariant &data)
{
    // Vanhan, sormenjäljettömän ilmoituksen kertatarkistuksen tulos:
    // "ok" tallentaa nykyisen sormenjäljen, "muokattu" merkitsee ilmoituksen muokatuksi
    const int tositeId = polku.toInt();
    const QString tarkistus = data.toMap().value("tarkistus").toString();

    QSqlQuery kysely(db());
    kysely.exec( QString("SELECT json FROM Tosite WHERE id=%1 AND tyyppi=%2")
                 .arg(tositeId).arg(TositeTyyppi::ALVLASKELMA));
    if( !kysely.next())
        return QVariant();

    QVariantMap map = QJsonDocument::fromJson( kysely.value(0).toString().toUtf8() ).toVariant().toMap();
    QVariantMap alv = map.value("alv").toMap();
    if( tarkistus == "ok") {
        alv.insert("sormenjalki", sormenjalki(db(), alv.value("kausialkaa").toDate(), alv.value("kausipaattyy").toDate()));
        alv.remove("muokattu");
    } else if( tarkistus == "muokattu") {
        alv.insert("muokattu", true);
    } else {
        return QVariant();
    }
    map.insert("alv", alv);

    kysely.prepare("UPDATE Tosite SET json=? WHERE id=?");
    kysely.addBindValue( mapToJson(map) );
    kysely.addBindValue( tositeId );
    if( !kysely.exec())
        throw SQLiteVirhe(kysely);

    return QVariant();
}

QVariantMap AlvRoute::sormenjalki(QSqlDatabase db, const QDate &alkaa, const QDate &paattyy)
{
    QVariantMap jalki;
    if( !alkaa.isValid() || !paattyy.isValid())
        return jalki;

    QSqlQuery kysely(db);
    kysely.exec( QString("SELECT Vienti.alvkoodi, Vienti.alvprosentti, "
                         "SUM(COALESCE(Vienti.debetsnt,0)) - SUM(COALESCE(Vienti.kreditsnt,0)) "
                         "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                         "WHERE Tosite.tila >= 100 AND Tosite.tyyppi <> %1 AND Vienti.alvkoodi > 0 "
                         "AND Vienti.pvm BETWEEN '%2' AND '%3' "
                         "GROUP BY Vienti.alvkoodi, Vienti.alvprosentti")
                 .arg(TositeTyyppi::ALVLASKELMA)
                 .arg(alkaa.toString(Qt::ISODate), paattyy.toString(Qt::ISODate)));
    while( kysely.next()) {
        const qlonglong summa = kysely.value(2).toLongLong();
        if( !summa )
            continue;
        // Avaimena koodi/prosentin sadasosat: SQLite palauttaa numericin reaalilukuna, Postgres merkkijonona
        const QString avain = QString("%1/%2").arg(kysely.value(0).toInt())
                                              .arg(qRound(kysely.value(1).toDouble() * 100));
        jalki.insert(avain, jalki.value(avain).toLongLong() + summa);
    }
    return jalki;
}

QList<QVariantMap> AlvRoute::sormenjaljet(QSqlDatabase db, const QList<QPair<QDate, QDate> > &kaudet)
{
    QList<QVariantMap> jaljet;
    QDate alku;
    QDate loppu;
    for(const auto& kausi : kaudet) {
        jaljet.append(QVariantMap());
        if( !kausi.first.isValid() || !kausi.second.isValid())
            continue;
        if( !alku.isValid() || kausi.first < alku)
            alku = kausi.first;
        if( !loppu.isValid() || kausi.second > loppu)
            loppu = kausi.second;
    }
    if( !alku.isValid())
        return jaljet;

    // Yksi kooste päivittäin kaikkien kausien ajalta, jaetaan kausille täällä
    QSqlQuery kysely(db);
    kysely.setForwardOnly(true);
    kysely.exec( QString("SELECT Vienti.pvm, Vienti.alvkoodi, Vienti.alvprosentti, "
                         "SUM(COALESCE(Vienti.debetsnt,0)) - SUM(COALESCE(Vienti.kreditsnt,0)) "
                         "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                         "WHERE Tosite.tila >= 100 AND Tosite.tyyppi <> %1 AND Vienti.alvkoodi > 0 "
                         "AND Vienti.pvm BETWEEN '%2' AND '%3' "
                         "GROUP BY Vienti.pvm, Vienti.alvkoodi, Vienti.alvprosentti")
                 .arg(TositeTyyppi::ALVLASKELMA)
                 .arg(alku.toString(Qt::ISODate), loppu.toString(Qt::ISODate)));
    while( kysely.next()) {
        const qlonglong summa = kysely.value(3).toLongLong();
        if( !summa )
            continue;
        const QDate pvm = kysely.value(0).toDate();
        const QString avain = QString("%1/%2").arg(kysely.value(1).toInt())
                                              .arg(qRound(kysely.value(2).toDouble() * 100));
        for(int i=0; i < kaudet.count(); i++) {
            if( pvm >= kaudet.at(i).first && pvm <= kaudet.at(i).second) {
                QVariantMap& jalki = jaljet[i];
                jalki.insert(avain, jalki.value(avain).toLongLong() + summa);
            }
        }
    }
    // Nollasummat pois, kuten sormenjalki() tekee
    for(QVariantMap& jalki : jaljet) {
        for(auto iter = jalki.begin(); iter != jalki.end(); ) {
            if( iter.value().toLongLong() == 0)
                iter = jalki.erase(iter);
            else
                ++iter;
        }
    }
    return jaljet;
}

bool AlvRoute::samaSormenjalki(const QVariantMap &a, const QVariantMap &b)
{
    QStringList avaimet = a.keys() + b.keys();
    for(const QString& avain : qAsConst(avaimet)) {
        if( a.value(avain).toLongLong() != b.value(avain).toLongLong())
            return false;
    }
    return true;
}
