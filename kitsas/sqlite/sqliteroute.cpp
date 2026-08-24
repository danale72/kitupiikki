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
#include "sqliteroute.h"
#include <QJsonDocument>
#include <QSqlRecord>
#include <QDebug>
#include <QSqlError>
#include <QSet>
#include <QMap>

#include "model/euro.h"

SQLiteRoute::SQLiteRoute(SqlModel *model, const QString &polku)
    : model_(model), polku_(polku)
{

}

SQLiteRoute::~SQLiteRoute()
{

}

QVariant SQLiteRoute::route(SQLiteKysely *kysely, const QVariant &data)
{
    QString loppu = kysely->polku().mid( polku().length() );

    if( loppu.startsWith(QChar('/')) )
        loppu = loppu.mid(1);

    QVariant paluu;

    switch (kysely->metodi()) {
    case KpKysely::GET:
        return get(loppu, kysely->urlKysely());
    case KpKysely::POST:
        return post(loppu, data);
    case KpKysely::PUT:
        return  put(loppu, data);
    case KpKysely::PATCH:
        return patch(loppu, data);
    case KpKysely::DELETE:
        return  doDelete(loppu);
    }
    throw SQLiteVirhe("Tuntematon metodi",405);
}

QPair<const QVariant, int> SQLiteRoute::byteArray(SQLiteKysely * /*reititettavaKysely*/, const QByteArray & /*ba*/, const QMap<QString, QString> & /*meta*/)
{
    return qMakePair<const QVariant,int>(QVariant(),0);
}

QVariant SQLiteRoute::get(const QString & polku, const QUrlQuery& /*urlquery*/)
{
    qWarning() << "* Ei reititetty: GET " << polku ;
    return QVariant();
}

QVariant SQLiteRoute::put(const QString & /*polku*/, const QVariant &/*data*/)
{
    return QVariant();
}

QVariant SQLiteRoute::post(const QString &/*polku*/, const QVariant &/*data*/)
{
    return QVariant();
}

QVariant SQLiteRoute::patch(const QString &/*polku*/, const QVariant &/*data*/)
{
    return QVariant();
}

QVariant SQLiteRoute::doDelete(const QString &/*polku*/)
{
    return QVariant();
}

QVariantList SQLiteRoute::resultList(QSqlQuery &kysely)
{
    if( kysely.lastError().type() != QSqlError::NoError) {
        qWarning() << " *SQLVIRHE* "
                  << kysely.lastError().text()
                  << kysely.lastQuery();
    }


    QVariantList lista;
    while( kysely.next()) {
        // Sijoitetaan ensin json-kenttä

        QSqlRecord tietue = kysely.record();
        QVariantMap map = QJsonDocument::fromJson( tietue.value("json").toString().toUtf8() ).toVariant().toMap();

        for(int i=0; i < tietue.count(); i++) {
            QString kenttanimi = tietue.fieldName(i);
            // Jos kenttänimi esim. era_id, tulee era.id
            if( tietue.value(i).toString().isEmpty() ||
                tietue.value(i).toString() == "0")
                continue;   // Ei tyhjiä kenttiä

            if( kenttanimi.contains(QChar('_'))) {
                int viivanpaikka = kenttanimi.indexOf('_');
                QString ryhma = kenttanimi.left(viivanpaikka);
                QString alakentta = kenttanimi.mid(viivanpaikka+1);
                QVariantMap rmap = map.value(ryhma, QVariantMap()).toMap();
                rmap.insert(alakentta, tietue.value(i));
                map.insert(ryhma, rmap);
            }
            else if( kenttanimi.endsWith("snt")) {
                map.insert( kenttanimi.left( kenttanimi.length() - 3 ), Euro( tietue.value(i).toLongLong() ).toString() );
            } else if( kenttanimi == "alvprosentti") {
                map.insert("alvprosentti", QString::number( tietue.value(i).toDouble(), 'f', 2 ));
            }
            else if( kenttanimi != "json") {
                map.insert( kenttanimi, tietue.value(i));
            }
        }
        lista.append(map);
    }
    return lista;
}

QVariantMap SQLiteRoute::resultMap(QSqlQuery &kysely)
{
    // Kyselyssä voi olla vain yksi rivi
    QVariantList lista = resultList(kysely);
    if( lista.isEmpty())
        return QVariantMap();
    else
        return lista.first().toMap();
}

QString SQLiteRoute::mapToJson(const QVariantMap &map)
{
    // Must return QString, not QByteArray: Qt's QPSQL driver binds QByteArray
    // parameters as Postgres bytea (hex-escaped), which corrupts/rejects json(b) columns.
    return QString::fromUtf8(QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact));
}

QSqlDatabase SQLiteRoute::db()
{
    return model_->tietokanta();
}


void SQLiteRoute::taydennaEratJaMerkkaukset(QVariantList &vientilista)
{
    // Kerätään ensin tarvittavat viennin ja erän id:t, jotta erä- ja merkkaustiedot
    // voidaan hakea yhdellä kyselyllä riviä kohti sen sijaan, että jokaiselle
    // viennille tehtäisiin omat kyselynsä (kallista etäkäytössä olevalla tietokannalla).
    QSet<int> vientiIdt;
    QSet<int> eraIdt;
    for(const QVariant &v : vientilista) {
        QVariantMap map = v.toMap();
        int id = map.value("id").toInt();
        if( id )
            vientiIdt.insert(id);
        if( map.contains("era")) {
            int eraid = map.value("era").toMap().value("id").toInt();
            if( eraid )
                eraIdt.insert(eraid);
        }
    }

    QSqlQuery kysely(db());

    QMap<int, QVariantMap> eratiedot;
    QMap<int, qlonglong> eraDebetit;
    QMap<int, qlonglong> eraKreditit;

    if( !eraIdt.isEmpty()) {
        QStringList idlist;
        for(int id : eraIdt)
            idlist << QString::number(id);
        const QString idehto = idlist.join(',');

        kysely.exec(QString("SELECT Vienti.id as id, Tosite.tunniste as tunniste, Tosite.sarja as sarja, Tosite.pvm as pvm, Tosite.tyyppi as tositetyyppi "
                            "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                            "WHERE Vienti.id IN (%1)")
                    .arg(idehto));
        for(const QVariant& v : resultList(kysely)) {
            QVariantMap m = v.toMap();
            eratiedot.insert( m.value("id").toInt(), m);
        }

        kysely.exec(QString("SELECT eraid, SUM(debetsnt) as debetit, SUM(kreditsnt) as kreditit "
                            "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                            "WHERE eraid IN (%1) AND Tosite.tila >= 100 "
                            "GROUP BY eraid")
                    .arg(idehto));
        while( kysely.next()) {
            int eraid = kysely.value(0).toInt();
            eraDebetit.insert(eraid, kysely.value(1).toLongLong());
            eraKreditit.insert(eraid, kysely.value(2).toLongLong());
        }
    }

    QMap<int, QVariantList> merkkaukset;
    if( !vientiIdt.isEmpty()) {
        QStringList idlist;
        for(int id : vientiIdt)
            idlist << QString::number(id);

        kysely.exec(QString("SELECT vienti, kohdennus FROM Merkkaus WHERE vienti IN (%1)").arg(idlist.join(',')));
        while( kysely.next())
            merkkaukset[ kysely.value(0).toInt() ].append( kysely.value(1).toInt() );
    }

    for(int i=0; i < vientilista.count(); i++) {
        QVariantMap map = vientilista.at(i).toMap();

        if( map.contains("era")) {
            int eraid = map.value("era").toMap().value("id").toInt();
            if( eraid ) {
                if( !eratiedot.contains(eraid)) {
                    map.remove("era");
                } else {
                    QVariantMap eramap = eratiedot.value(eraid);
                    eramap.insert("saldo", (eraDebetit.value(eraid,0) - eraKreditit.value(eraid,0)) / 100.0);
                    map.insert("era", eramap);
                }
            }
        }

        const QVariantList& rivinMerkkaukset = merkkaukset.value( map.value("id").toInt() );
        if( rivinMerkkaukset.count())
            map.insert("merkkaukset", rivinMerkkaukset);

        vientilista[i] = map;
    }
}
