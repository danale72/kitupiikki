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
#include "tilikaudetroute.h"
#include "db/kirjanpito.h"
#include "db/tositetyyppimodel.h"
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QVariant>
#include <QDebug>
#include <QSqlTableModel>
#include <QSqlRecord>

TilikaudetRoute::TilikaudetRoute(SqlModel *model) :
    SQLiteRoute(model, "/tilikaudet")
{

}

QVariant TilikaudetRoute::get(const QString &polku, const QUrlQuery &/*urlquery*/)
{
    if( QDate::fromString(polku, Qt::ISODate).isValid() )
        return laskelma( kp()->tilikaudet()->tilikausiPaivalle( QDate::fromString(polku, Qt::ISODate) ) );

    QSqlQuery kysely(db());
    kysely.exec("SELECT alkaa,loppuu,json FROM Tilikausi ORDER BY alkaa");

    QVariantList list = resultList(kysely);

    // Kauden luvut kerätään muutamalla ryhmitellyllä kyselyllä (Tilikausi-tauluun liittäen)
    // eikä kuudella kyselyllä jokaista tilikautta kohden
    struct KaudenLuvut {
        qlonglong debet = 0;        // kaikki kauden viennit
        qlonglong kredit = 0;
        qlonglong taseDebet = 0;    // tilit < '2'
        qlonglong taseKredit = 0;
        qlonglong tulosKredit = 0;  // tilit >= '3'
        qlonglong tulosDebet = 0;
        qlonglong lvKredit = 0;     // liikevaihtotilit (CL, CLZ)
        qlonglong lvDebet = 0;
        QVariant viimeinen;
        QDateTime paivitetty;
    };
    QHash<QDate, KaudenLuvut> luvut;

    kysely.setForwardOnly(true);
    kysely.exec("SELECT k.alkaa, SUM(debetsnt), SUM(kreditsnt), "
                "SUM(CASE WHEN CAST(Vienti.tili AS text) < '2' THEN debetsnt END), "
                "SUM(CASE WHEN CAST(Vienti.tili AS text) < '2' THEN kreditsnt END), "
                "SUM(CASE WHEN CAST(Vienti.tili AS text) >= '3' THEN kreditsnt END), "
                "SUM(CASE WHEN CAST(Vienti.tili AS text) >= '3' THEN debetsnt END), "
                "SUM(CASE WHEN CAST(Vienti.tili AS text) >= '3' AND (Tili.tyyppi='CL' OR Tili.tyyppi='CLZ') THEN kreditsnt END), "
                "SUM(CASE WHEN CAST(Vienti.tili AS text) >= '3' AND (Tili.tyyppi='CL' OR Tili.tyyppi='CLZ') THEN debetsnt END) "
                "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                "JOIN Tilikausi AS k ON Vienti.pvm BETWEEN k.alkaa AND k.loppuu "
                "LEFT OUTER JOIN Tili ON Vienti.tili=Tili.numero "
                "WHERE Tosite.tila >= 100 GROUP BY k.alkaa");
    while( kysely.next()) {
        KaudenLuvut& l = luvut[ kysely.value(0).toDate() ];
        l.debet = kysely.value(1).toLongLong();
        l.kredit = kysely.value(2).toLongLong();
        l.taseDebet = kysely.value(3).toLongLong();
        l.taseKredit = kysely.value(4).toLongLong();
        l.tulosKredit = kysely.value(5).toLongLong();
        l.tulosDebet = kysely.value(6).toLongLong();
        l.lvKredit = kysely.value(7).toLongLong();
        l.lvDebet = kysely.value(8).toLongLong();
    }

    kysely.exec("SELECT k.alkaa, MAX(Tosite.pvm) FROM Tosite JOIN Tilikausi AS k ON Tosite.pvm BETWEEN k.alkaa AND k.loppuu "
                "WHERE Tosite.tila >= 100 GROUP BY k.alkaa");
    while( kysely.next())
        luvut[ kysely.value(0).toDate() ].viimeinen = kysely.value(1);

    kysely.exec("SELECT k.alkaa, MAX(Tositeloki.aika) FROM Tositeloki JOIN Tosite ON Tositeloki.tosite=Tosite.id "
                "JOIN Tilikausi AS k ON Tosite.pvm BETWEEN k.alkaa AND k.loppuu GROUP BY k.alkaa");
    while( kysely.next())
        luvut[ kysely.value(0).toDate() ].paivitetty = kysely.value(1).toDateTime();

    // Täsmäys ja tase ovat kertymiä: mukaan myös viennit, jotka eivät osu millekään tilikaudelle
    QList<QPair<QDate, KaudenLuvut>> kaudenUlkopuoliset;
    kysely.exec("SELECT Vienti.pvm, SUM(debetsnt), SUM(kreditsnt), "
                "SUM(CASE WHEN CAST(Vienti.tili AS text) < '2' THEN debetsnt END), "
                "SUM(CASE WHEN CAST(Vienti.tili AS text) < '2' THEN kreditsnt END) "
                "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id WHERE Tosite.tila >= 100 "
                "AND NOT EXISTS (SELECT 1 FROM Tilikausi AS k WHERE Vienti.pvm BETWEEN k.alkaa AND k.loppuu) "
                "GROUP BY Vienti.pvm");
    while( kysely.next()) {
        KaudenLuvut l;
        l.debet = kysely.value(1).toLongLong();
        l.kredit = kysely.value(2).toLongLong();
        l.taseDebet = kysely.value(3).toLongLong();
        l.taseKredit = kysely.value(4).toLongLong();
        kaudenUlkopuoliset.append(qMakePair(kysely.value(0).toDate(), l));
    }

    qlonglong debetKertyma = 0;
    qlonglong kreditKertyma = 0;
    qlonglong taseDebetKertyma = 0;
    qlonglong taseKreditKertyma = 0;
    for(int i=0; i < list.count(); i++){
        QVariantMap map = list.at(i).toMap();
        const QDate alkaa = map.value("alkaa").toDate();
        const QDate loppuu = map.value("loppuu").toDate();
        const KaudenLuvut l = luvut.value(alkaa);

        debetKertyma += l.debet;
        kreditKertyma += l.kredit;
        taseDebetKertyma += l.taseDebet;
        taseKreditKertyma += l.taseKredit;
        // Kaudettomat viennit lasketaan kertymään kerran, viimeistään sen kauden kohdalla, jonka loppuun mennessä ne on kirjattu
        for(auto iter = kaudenUlkopuoliset.begin(); iter != kaudenUlkopuoliset.end(); ) {
            if( iter->first <= loppuu ) {
                debetKertyma += iter->second.debet;
                kreditKertyma += iter->second.kredit;
                taseDebetKertyma += iter->second.taseDebet;
                taseKreditKertyma += iter->second.taseKredit;
                iter = kaudenUlkopuoliset.erase(iter);
            } else {
                ++iter;
            }
        }

        // Kirjanpidon täsmääminen
        if( debetKertyma != kreditKertyma)
            map.insert("virhe",true);

        map.insert("tase", QString::number( qAbs(taseKreditKertyma - taseDebetKertyma) / 100.0,'f',2));
        map.insert("tulos", QString::number((l.tulosKredit - l.tulosDebet) / 100.0, 'f', 2));
        map.insert("liikevaihto", QString::number((l.lvKredit - l.lvDebet) / 100.0, 'f', 2));

        // Kauden viimeinen tosite
        map.insert("viimeinen", l.viimeinen);

        // Tilikauden päivitys
        QDateTime paivitetty = l.paivitetty;
        paivitetty.setTimeSpec(Qt::UTC);
        map.insert("paivitetty", paivitetty.toLocalTime());

        list[i] = map;
    }

    return list;
}

QVariant TilikaudetRoute::put(const QString &polku, const QVariant &data)
{
    QVariantMap map = data.toMap();

    QDate vanhaAlkaa = QDate::fromString(polku, Qt::ISODate);
    QDate loppuu = map.take("loppuu").toDate();
    QDate uusiAlkaa = map.take("alkaa").toDate();

    if(vanhaAlkaa == uusiAlkaa) {
        QSqlQuery kysely(db());
        kysely.prepare("INSERT INTO Tilikausi (alkaa,loppuu,json) VALUES (?,?,?) "
                       "ON CONFLICT (alkaa) DO UPDATE SET loppuu=EXCLUDED.loppuu, json=EXCLUDED.json");
        kysely.addBindValue(uusiAlkaa);
        kysely.addBindValue(loppuu);
        kysely.addBindValue( mapToJson(map) );
        kysely.exec();
    } else {
        QSqlTableModel model( nullptr, db() );
        model.setTable("Tilikausi");
        model.setFilter(QString("alkaa = '%1'").arg(vanhaAlkaa.toString(Qt::ISODate)));
        model.select();
        if(model.rowCount() == 1) {
            QSqlRecord record = model.record(0);
            record.setValue("alkaa", uusiAlkaa);
            record.setValue("loppuu", loppuu);
            record.setValue("json", mapToJson(map));
            model.setRecord(0, record);
            model.submitAll();
        }
    }

    return QVariant();
}

QVariant TilikaudetRoute::doDelete(const QString &polku)
{
    QDate alkaa = QDate::fromString(polku, Qt::ISODate);
    db().exec(QString("DELETE FROM Tilikausi WHERE alkaa='%1'").arg(alkaa.toString(Qt::ISODate)));
    return QVariant();
}

QVariant TilikaudetRoute::post(const QString &polku, const QVariant &data)
{
    if( polku !="numerointi")
        return QVariant();

    QVariantMap map = data.toMap();
    QString alkaa = map.value("alkaa").toDate().toString(Qt::ISODate);
    QString loppuu = map.value("loppuu").toDate().toString(Qt::ISODate);

    QDate alku = map.value("alkaa").toDate();
    QDate kausialkaa = kp()->tilikaudet()->tilikausiPaivalle(alku).alkaa();
    bool alusta = alku == kausialkaa;

    QMap<QString,int> numerot;

    QSqlQuery query(db());

    if( !alusta) {

    query.exec(QString("SELECT sarja, MAX(tunniste) FROM Tosite "
               "WHERE pvm < '%1' AND pvm >= '%2' AND tila >= 100 "
                "GROUP BY sarja").arg(alkaa).arg(kausialkaa.toString(Qt::ISODate)));
    while( query.next()) {
        int min = query.value(1).toInt() + 1;
        numerot.insert(query.value(0).toString(), min);
    }
    }


    query.exec(QString("SELECT id, sarja FROM Tosite "
               "WHERE pvm BETWEEN '%1' AND '%2' AND tila >= 100 "
                "ORDER BY sarja, pvm, id").arg(alkaa).arg(loppuu));

    QList<QPair<int,int>> uudet;    // id, tunniste
    while( query.next()) {
        int id = query.value(0).toInt();
        QString sarja = query.value(1).toString();

        int tunniste =  numerot.contains(sarja) ? numerot.value(sarja) : 1;
        numerot[sarja] = tunniste + 1;
        uudet.append(qMakePair(id, tunniste));
    }

    // Päivitetään useita tositteita kerralla, ettei jokaisesta tarvita omaa kyselyä
    // (etäpalvelimella jokainen kysely on verkkokierros)
    const int erakoko = 500;
    db().transaction();
    QSqlQuery update(db());
    for(int alku = 0; alku < uudet.count(); alku += erakoko) {
        QStringList tapaukset;
        QStringList idt;
        for(int i = alku; i < qMin(alku + erakoko, uudet.count()); i++) {
            tapaukset << QString("WHEN %1 THEN %2").arg(uudet.at(i).first).arg(uudet.at(i).second);
            idt << QString::number(uudet.at(i).first);
        }
        if( !update.exec(QString("UPDATE Tosite SET tunniste = CASE id %1 END WHERE id IN (%2)")
                         .arg(tapaukset.join(' '), idt.join(',')))) {
            db().rollback();
            throw SQLiteVirhe(update);
        }
    }
    db().commit();
    return QVariant();
}

QVariant TilikaudetRoute::laskelma(const Tilikausi &kausi)
{
    QSqlQuery kysely( db() );
    QVariantMap ulos;


    // Laaditaan poisto- ja jaksotuslaskelmat

    // Tarkistetaan, onko poistoja jo kirjattu
    kysely.exec(QString("SELECT id FROM Tosite WHERE pvm = '%1' "
                        "AND tyyppi=%2 AND tila >= 100 LIMIT 1")
                .arg(kausi.paattyy().toString(Qt::ISODate))
                .arg( TositeTyyppi::POISTOLASKELMA ));
    if( kysely.next() ) {
        ulos.insert("poistot","kirjattu");
    } else {
        QVariantList poistot;

        // Poistolaskelmaa ei ole vielä kirjattu, joten lasketaan poistot

        // Ensin menojäännöspoistot

        kysely.exec( QString("select tili.numero, sum(debetsnt), sum(kreditsnt), coalesce (kohdennus, 0) from vienti "
                             "join tili on vienti.tili=tili.numero join tosite on vienti.tosite=tosite.id "
                             "where tili.tyyppi='APM' and vienti.pvm <= '%1' and tosite.tila >= 100 group by tili.numero, kohdennus order by tili.numero, kohdennus")
                     .arg(kausi.paattyy().toString(Qt::ISODate)));
        while( kysely.next()) {
            Tili* tili = kp()->tilit()->tili(kysely.value(0).toInt());
            if( !tili)
                continue;

            qlonglong summa = kysely.value(1).toLongLong() -  kysely.value(2).toLongLong();
            int poistoprosentti = tili->luku("menojaannospoisto");
            qlonglong poisto = poistoprosentti * summa / 100;

            QVariantMap map;
            map.insert("tili", tili->numero());
            map.insert("ennen", Euro::fromCents(summa));
            map.insert("poisto", Euro::fromCents(poisto));
            map.insert("kohdennus", kysely.value(3).toInt());
            poistot.append(map);

        }
        // Laaditaan laskelma APT-eristä
        // Ladataan avoimet erät
        QSqlQuery apukysely( db() );
        kysely.exec(QString("select tili.numero, vienti.eraid as eraid, sum(debetsnt), sum(kreditsnt) from vienti "
                            "join tili on vienti.tili=tili.numero join tosite on vienti.tosite=tosite.id "
                            "where tili.tyyppi='APT' and vienti.pvm <= '%1' and tosite.tila >= 100 group by tili.numero, vienti.eraid "
                            "order by tili.numero, vienti.eraid").arg(kausi.paattyy().toString(Qt::ISODate)) );

        while(kysely.next()) {
            int eraid = kysely.value("eraid").toInt();

            apukysely.exec(QString("select debetsnt,kreditsnt,selite,vienti.json,vienti.pvm, coalesce (kohdennus, 0), tosite.tyyppi from vienti join tosite on vienti.tosite=tosite.id  where vienti.id=%1").arg(eraid));
            if( apukysely.next()) {
                QVariantMap jsonmap = QJsonDocument::fromJson( apukysely.value(3).toByteArray() ).toVariant().toMap();
                
                int poistokk = jsonmap.value("tasaerapoisto").toInt();
                
                if (poistokk < 1)
                    continue;
                
                qlonglong alkusumma = apukysely.value(0).toLongLong() - apukysely.value(1).toLongLong();
        
                qlonglong saldo = kysely.value(2).toLongLong() - kysely.value(3).toLongLong();
                QDate hankintapaiva = apukysely.value(4).toDate();

                int kuukauttaKulunut = kausi.paattyy().year() * 12 + kausi.paattyy().month() -
                                       hankintapaiva.year() * 12 - hankintapaiva.month() + (apukysely.value(6).toInt() == TositeTyyppi::TILINAVAUS ? 0 : 1 );
                qlonglong laskennallinenpoisto = alkusumma * kuukauttaKulunut / poistokk;
                qlonglong nytpoistettavaa = laskennallinenpoisto - alkusumma + saldo;
                qlonglong poisto = nytpoistettavaa > saldo ?  saldo : nytpoistettavaa;


                if( poisto > 0) {
                    QVariantMap map;
                    map.insert("eraid", eraid);
                    map.insert("tili", kysely.value(0).toInt());
                    map.insert("nimike", apukysely.value(2).toString());
                    map.insert("pvm", hankintapaiva);
                    map.insert("ennen", Euro::fromCents(saldo));
                    map.insert("poisto", Euro::fromCents(poisto));
                    map.insert("poistoaika", poistokk );
                    map.insert("kohdennus", apukysely.value(5).toInt());
                    poistot.append(map);
                }
            }

        }
        ulos.insert("poistot", poistot);
    }


    // Tarkistetaan, onko jaksotukset jo kirjattu
    kysely.exec(QString("SELECT id FROM Tosite WHERE pvm = '%1' "
                        "AND tyyppi=%2 AND tila >= 100 LIMIT 1")
                .arg(kausi.paattyy().toString(Qt::ISODate))
                .arg( TositeTyyppi::JAKSOTUS ));
    if( kysely.next()) {
        ulos.insert("jaksotukset","kirjattu");
    } else {
        QVariantList jaksotukset;
        // Haetaan jaksotettavat viennit
        kysely.exec(QString("select debetsnt,kreditsnt,tili,selite,jaksoalkaa,jaksoloppuu, kohdennus, tosite.pvm, tosite.sarja, tosite.tunniste, vienti.pvm from vienti "
                            "join tosite on vienti.tosite=tosite.id "
                            "where jaksoalkaa is not null and tosite.tila >= 100 "
                            "AND vienti.pvm >= '%1' AND jaksoalkaa IS NOT NULL ORDER BY tili, vienti.id")
                    .arg(kausi.alkaa().toString(Qt::ISODate)));
        while( kysely.next()) {
            qlonglong debet = kysely.value(0).toLongLong();
            qlonglong kredit = kysely.value(1).toLongLong();
            int tili = kysely.value(2).toInt();
            QString selite = kysely.value(3).toString();
            QDate alkaa = kysely.value(4).toDate();
            QDate loppuu = kysely.value(5).toDate();
            QDate vientipvm = kysely.value(10).toDate();

            Euro euro = Euro::fromCents( debet - kredit );
            Euro jaksotus = laskeJaksotus(kausi.paattyy(), vientipvm, alkaa, loppuu, euro);
            if( jaksotus) {
                QVariantMap map;
                map.insert("tili", tili);
                if( jaksotus > Euro::Zero)
                    map.insert("debet", jaksotus.toString());
                else
                    map.insert("kredit", jaksotus.abs().toString());
                map.insert("selite", selite);

                if (vientipvm <= kausi.paattyy())
                {
                map.insert("jaksoalkaa", alkaa);
                if( loppuu.isValid())
                    map.insert("jaksoloppuu", loppuu);
                }

                map.insert("kohdennus", kysely.value(6).toInt());
                map.insert("pvm", kysely.value(7).toDate());
                map.insert("sarja", kysely.value(8));
                map.insert("tunniste", kysely.value(9));
                jaksotukset.append(map);
            }
        }
        ulos.insert("jaksotukset", jaksotukset);

        // Negatiivinen alv-velka jaksotetaan alv-saamiseksi tilinpäätökseen
        kysely.exec(QString("SELECT sum(debetsnt), sum(kreditsnt) FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                            "WHERE tili=%1 AND tila>=100 AND vienti.pvm <= '%2'")
                    .arg(kp()->tilit()->tiliTyypilla(TiliLaji::VEROVELKA).numero())
                    .arg(kausi.paattyy().toString(Qt::ISODate)));        
        if( kysely.next() ) {
            qlonglong velka = kysely.value(0).toLongLong() - kysely.value(1).toLongLong();
            if( velka > 0)
                ulos.insert("verosaaminen", Euro::fromCents(velka));
        }
    }

    verolaskelma( kausi, ulos);
    yksityistilit( kausi, ulos);

    return ulos;
}

void TilikaudetRoute::verolaskelma(const Tilikausi &kausi, QVariantMap &ulos)
{
    QSqlQuery kysely(db());
    // Tarkistetaan, onko tulovero jo kirjattu
    kysely.exec(QString("SELECT id FROM Tosite WHERE pvm = '%1' "
                        "AND tyyppi=%2 AND tila >= 100 LIMIT 1")
                .arg(kausi.paattyy().toString(Qt::ISODate))
                .arg( TositeTyyppi::TULOVERO ));
    if( kysely.next()) {
        ulos.insert("tulovero","kirjattu");
    } else {
        QVariantMap vmap;
        // Tulot, Vähennykset ja Ennakkot
        // TULOT, tilityypit C ja CL
        kysely.exec(QString("SELECT sum(kreditsnt), sum(debetsnt) FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                    "JOIN Tili ON Vienti.tili=Tili.numero "
                    "WHERE Tosite.tila >= 100 AND (Tili.tyyppi='C' OR Tili.tyyppi='CL') AND "
                    "Vienti.pvm BETWEEN '%1' AND '%2'").arg(kausi.alkaa().toString(Qt::ISODate))
                                                       .arg(kausi.paattyy().toString(Qt::ISODate)));
        if( kysely.next() )
            vmap.insert("tulo", (Euro::fromCents(kysely.value(0).toLongLong()) - Euro::fromCents(kysely.value(1).toLongLong())));

        // VÄHENNYKSET
        qlonglong vahennykset = 0;
        kysely.exec(QString("SELECT sum(kreditsnt), sum(debetsnt) FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                    "JOIN Tili ON Vienti.tili=Tili.numero "
                    "WHERE Tosite.tila >= 100 AND (Tili.tyyppi='D' or Tili.tyyppi='DP') AND "
                    "Vienti.pvm BETWEEN '%1' AND '%2'").arg(kausi.alkaa().toString(Qt::ISODate))
                                                       .arg(kausi.paattyy().toString(Qt::ISODate)));
        if( kysely.next() ) {
            vahennykset = kysely.value(1).toLongLong() - kysely.value(0).toLongLong() ;
            vmap.insert("taysivahennys", Euro::fromCents(vahennykset));
        }

        // 50% VÄHENNYKSET
        kysely.exec(QString("SELECT sum(kreditsnt), sum(debetsnt) FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                    "JOIN Tili ON Vienti.tili=Tili.numero "
                    "WHERE Tosite.tila >= 100 AND Tili.tyyppi='DH' AND "
                    "Vienti.pvm BETWEEN '%1' AND '%2'").arg(kausi.alkaa().toString(Qt::ISODate))
                                                       .arg(kausi.paattyy().toString(Qt::ISODate)));
        if( kysely.next() ) {
            const qlonglong puolivahennys = ( kysely.value(1).toLongLong() - kysely.value(0).toLongLong());
            vahennykset += puolivahennys / 2;
            vmap.insert("puolivahennys", Euro::fromCents(puolivahennys));
        }
        vmap.insert("vahennys", Euro::fromCents(vahennykset));

        // ENNAKOT
        kysely.exec(QString("SELECT sum(kreditsnt), sum(debetsnt) FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                    "JOIN Tili ON Vienti.tili=Tili.numero "
                    "WHERE Tosite.tila >= 100 AND Tili.tyyppi='DVE' AND "
                    "Vienti.pvm BETWEEN '%1' AND '%2'").arg(kausi.alkaa().toString(Qt::ISODate))
                                                       .arg(kausi.paattyy().toString(Qt::ISODate)));
        if( kysely.next() )
            vmap.insert("ennakko",(Euro::fromCents(kysely.value(1).toLongLong() - kysely.value(0).toLongLong())));

        ulos.insert("tulovero", vmap);
    }
}

void TilikaudetRoute::yksityistilit(const Tilikausi &kausi, QVariantMap &ulos)
{
    QSqlQuery kysely(db());
    // Tarkistetaan, onko tulovero jo kirjattu
    kysely.exec(QString("SELECT id FROM Tosite WHERE pvm = '%1' "
                        "AND tyyppi=%2 AND tila >= 100 LIMIT 1")
                    .arg(kausi.paattyy().addDays(1).toString(Qt::ISODate))
                    .arg( TositeTyyppi::YKSITYISTILIEN_PAATTAMINEN ));
    if( kysely.next()) {
        ulos.insert("yksityistilit","kirjattu");
    } else {
        QVariantMap yksityistiliMap;
        QVariantMap tiliMap;

        kysely.exec(QString("SELECT tili, SUM(COALESCE(kreditsnt,0)) - SUM(COALESCE(debetsnt,0)) AS saldo FROM Vienti "
            "JOIN Tosite ON Vienti.tosite=Tosite.id JOIN Tili ON Vienti.tili=Tili.numero "
            "WHERE Tosite.tila >= 100 AND Tili.tyyppi='BY' AND Vienti.pvm <= '%1' "
            "GROUP BY tili")
                        .arg( kausi.paattyy().toString(Qt::ISODate)));
        while( kysely.next() ) {
            tiliMap.insert( kysely.value(0).toString(), Euro::fromCents(kysely.value(1).toLongLong()).toString() );
        }
        yksityistiliMap.insert("tilit", tiliMap);

        kysely.exec(QString("SELECT SUM(COALESCE(kreditsnt,0)) - SUM(COALESCE(debetsnt,0)) AS tulos FROM "
            "Vienti JOIN Tosite ON Vienti.tosite=Tosite.id WHERE CAST(tili AS TEXT) >= '3' AND Vienti.pvm BETWEEN '%1' AND '%2' AND tila >= 100")
                .arg(kausi.alkaa().toString(Qt::ISODate), kausi.paattyy().toString(Qt::ISODate)));
        if(kysely.next()) {
            yksityistiliMap.insert("tulos", Euro::fromCents(kysely.value(0).toLongLong()));
        }
        ulos.insert("yksityistilit", yksityistiliMap);
    }
}

Euro TilikaudetRoute::laskeJaksotus(const QDate &kausipaattyy, const QDate &pvm, const QDate &jaksoalkaa, const QDate &jaksopaattyy, const Euro &euro)
{

     int ennen = jaksoalkaa.daysTo(kausipaattyy) + 1;
     int jalkeen = kausipaattyy.daysTo(jaksopaattyy);

    if( pvm > kausipaattyy) {
        if( jaksoalkaa > kausipaattyy) return Euro::Zero;
        if( !jaksopaattyy.isValid() || jaksopaattyy <= kausipaattyy) return euro;

        double osa = (1.0 * ennen) / (ennen + jalkeen);

        return Euro::fromCents(qRound64( euro.cents() * osa));
    } else {
        if( jaksopaattyy.isValid() && jaksopaattyy <= kausipaattyy) return Euro::Zero;
        if( !jaksopaattyy.isValid() && jaksoalkaa <= kausipaattyy) return Euro::Zero;
        if( !jaksopaattyy.isValid() || jaksoalkaa > kausipaattyy) return Euro::Zero-euro;

        double osa = (1.0 * jalkeen) / (ennen+jalkeen);

        return Euro::Zero - Euro::fromCents(qRound64( (euro.cents()) * osa));
    }
}
