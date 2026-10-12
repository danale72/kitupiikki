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
#include "liitteetroute.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

#include <QCryptographicHash>
#include <QDebug>
#include <QNetworkReply>

LiitteetRoute::LiitteetRoute(SqlModel *model) :
    SQLiteRoute(model, "/liitteet")
{

}

QVariant LiitteetRoute::get(const QString &polku, const QUrlQuery &urlquery)
{
    QSqlQuery kysely(db());
    if( polku.isEmpty()) {
        QString kysymys = QString("SELECT tosite.pvm, tosite.sarja, tosite.tunniste, liite.id, liite.nimi, liite.tyyppi "
                                  "FROM Tosite JOIN Liite ON Liite.tosite=Tosite.id WHERE Tosite.tila >= 100 AND "
                                  "Tosite.pvm BETWEEN '%1' AND '%2' ORDER BY sarja, tunniste")
                .arg(urlquery.queryItemValue("alkupvm"), urlquery.queryItemValue("loppupvm"));
        kysely.exec(kysymys);
        return resultList(kysely);

    }
    if( polku.toInt()) {
        if(!kysely.exec(QString("SELECT data FROM Liite WHERE id=%1").arg(polku.toInt()) ) )
            throw SQLiteVirhe(kysely);
    } else {
        QRegularExpression re(R"((\d+)\/(\S+))");
        QRegularExpressionMatch match = re.match(polku);
        const int tosite = match.captured(1).toInt();
        if( tosite ) {
            kysely.prepare("SELECT data FROM Liite WHERE tosite=? AND roolinimi=?");
            kysely.addBindValue(tosite);
        } else {
            // Koko kirjanpidon liite (tilinpäätös, logo, banneri...), ks. kirjanpidonLiite()
            kysely.prepare("SELECT data FROM Liite WHERE (tosite IS NULL OR tosite=0) AND roolinimi=? "
                           "ORDER BY id DESC LIMIT 1");
        }
        kysely.addBindValue(match.captured(2));
        kysely.exec();
    }
    if( kysely.next())
        return kysely.value(0).toByteArray();

    throw SQLiteVirhe("Liitettä ei löydy",QNetworkReply::ContentNotFoundError);
}

QPair<const QVariant, int> LiitteetRoute::byteArray(SQLiteKysely *kysely, const QByteArray &ba, const QMap<QString, QString> &meta)
{
    QString loppu = kysely->polku().mid( polku().length()+1 );
    QRegularExpression re(R"((\d+)/(\S+)?)");
    QRegularExpressionMatch match = re.match(loppu);
    QSqlQuery query(db());
    QVariantMap palautus;

    if( kysely->metodi() == KpKysely::POST) {
        query.prepare("INSERT INTO Liite(nimi,data,tyyppi,sha,tosite) VALUES (?,?,?,?,?)");
        query.addBindValue( meta.value("Filename", QString()) );
        query.addBindValue( ba );
        query.addBindValue( meta.value("Content-type", QString()));
        query.addBindValue( hash( ba) );
        if( loppu.toInt()) {
            query.addBindValue( loppu.toInt() );
        } else {
            // Liite odottamaan tositetta
            query.addBindValue( QVariant());
        }
    } else if( kysely->metodi() == KpKysely::PUT && !match.captured(1).toInt() ) {
        const int id = kirjanpidonLiite(match.captured(2), ba, meta);
        palautus.insert("liite", id);
        palautus.insert("tosite", 0);
        return QPair<const QVariant,int>(palautus, id);
    } else if( kysely->metodi() == KpKysely::PUT)
    {
        query.prepare("INSERT INTO Liite (tosite,nimi,data,tyyppi,sha,roolinimi) VALUES (:tosite, :nimi, :data, :tyyppi, :sha, :roolinimi) "
                      " ON CONFLICT (tosite,roolinimi) DO UPDATE SET nimi=EXCLUDED.nimi, data=EXCLUDED.data, tyyppi=EXCLUDED.tyyppi, sha=EXCLUDED.sha, "
                      " roolinimi=EXCLUDED.roolinimi, luotu=current_timestamp"  );

        query.bindValue(":tosite", match.captured(1).toInt());
        query.bindValue(":nimi", meta.value("Filename", QString()) );
        query.bindValue(":data", ba);
        query.bindValue(":tyyppi", meta.value("Content-type", QString()) );
        query.bindValue(":sha", hash(ba));
        query.bindValue(":roolinimi", match.captured(2));

    }
    if( !query.exec() )
        throw SQLiteVirhe(query);


    palautus.insert("liite", query.lastInsertId());
    if( match.hasMatch() )
        palautus.insert("tosite", match.captured(1).toInt());

    return qMakePair<const QVariant,int>(palautus, query.lastInsertId().toInt());

}

int LiitteetRoute::kirjanpidonLiite(const QString &roolinimi, const QByteArray &ba, const QMap<QString, QString> &meta)
{
    // Koko kirjanpitoon liittyvät liitteet (polku /liitteet/0/<roolinimi>) tallennetaan
    // tosite=NULL -rivinä. Aiemmin käytetty tosite=0 rikkoo liite_tosite_fkey-viiteavaimen
    // sekä Postgresissa että SQLitessä (PRAGMA foreign_keys = ON), koska tositetta 0 ei ole.
    // NULL-arvot eivät törmää UNIQUE(tosite,roolinimi) -rajoitteeseen, joten ON CONFLICT
    // ei toimi: päivitetään ensin olemassa oleva rivi (myös vanha tosite=0 -rivi muunnetaan
    // samalla NULL:ksi) ja lisätään uusi vain, jos päivitettävää ei ollut.
    QSqlQuery query(db());
    query.prepare("UPDATE Liite SET tosite=NULL, nimi=?, data=?, tyyppi=?, sha=?, luotu=current_timestamp "
                  "WHERE (tosite IS NULL OR tosite=0) AND roolinimi=?");
    query.addBindValue( meta.value("Filename", QString()) );
    query.addBindValue( ba );
    query.addBindValue( meta.value("Content-type", QString()) );
    query.addBindValue( hash(ba) );
    query.addBindValue( roolinimi );
    if( !query.exec() )
        throw SQLiteVirhe(query);

    if( query.numRowsAffected() > 0) {
        query.prepare("SELECT id FROM Liite WHERE tosite IS NULL AND roolinimi=? ORDER BY id DESC LIMIT 1");
        query.addBindValue( roolinimi );
        if( !query.exec() )
            throw SQLiteVirhe(query);
        return query.next() ? query.value(0).toInt() : 0;
    }

    query.prepare("INSERT INTO Liite (tosite,nimi,data,tyyppi,sha,roolinimi) VALUES (NULL,?,?,?,?,?)");
    query.addBindValue( meta.value("Filename", QString()) );
    query.addBindValue( ba );
    query.addBindValue( meta.value("Content-type", QString()) );
    query.addBindValue( hash(ba) );
    query.addBindValue( roolinimi );
    if( !query.exec() )
        throw SQLiteVirhe(query);
    return query.lastInsertId().toInt();
}

QVariant LiitteetRoute::doDelete(const QString &polku)
{
    int id = polku.toInt();
    if( id ) {
        QSqlQuery kysely( db());
        kysely.exec( QString("DELETE FROM Liite WHERE id=%1").arg(id));
    }
    return QVariant();
}

QString LiitteetRoute::hash(const QByteArray &ba)
{
    // Must return QString, not QByteArray: QPSQL binds QByteArray as bytea,
    // which corrupts the Liite.sha TEXT column (same issue as mapToJson).
    QCryptographicHash laskin(QCryptographicHash::Sha256);
    laskin.addData(ba);
    return QString::fromLatin1(laskin.result().toHex());
}

