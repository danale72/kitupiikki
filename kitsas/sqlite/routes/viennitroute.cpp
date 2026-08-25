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
#include "viennitroute.h"
#include "tositeroute.h"
#include "db/kirjanpito.h"
#include "model/tosite.h"

#include <QSet>
#include <QMap>

ViennitRoute::ViennitRoute(SqlModel* model) :
    SQLiteRoute(model,"/viennit")
{

}

QVariant ViennitRoute::get(const QString &polku, const QUrlQuery &urlquery)
{
    if( polku.toInt())
        return vienti(polku.toInt());

    QStringList ehdot;
    ehdot.append(QString("tila >= %1").arg(Tosite::KIRJANPIDOSSA));

    if( urlquery.hasQueryItem("alkupvm"))
        ehdot.append(QString("vienti.pvm >= '%1'").arg(urlquery.queryItemValue("alkupvm")));
    if( urlquery.hasQueryItem("loppupvm"))
        ehdot.append(QString("vienti.pvm <= '%1'").arg(urlquery.queryItemValue("loppupvm")));
    if( urlquery.hasQueryItem("tili"))
        ehdot.append(QString("tili=%1").arg(urlquery.queryItemValue("tili")));
    if( urlquery.hasQueryItem("era"))
        ehdot.append(QString("eraid=%1").arg(urlquery.queryItemValue("era")));


    // TODO Kohdennus ja merkkaus

    QString jarjestys;
    if( urlquery.queryItemValue("jarjestys")=="tili")
        jarjestys = "Vienti.tili,";
    else if( urlquery.queryItemValue("jarjestys") == "tosite")
        jarjestys = "Tosite.sarja,Tosite.tunniste,";
    else if( urlquery.queryItemValue("jarjestys") == "laji")
        jarjestys = "Tosite.tyyppi,";
    else if( urlquery.queryItemValue("jarjestys")=="laji,tosite")
        jarjestys = "Tosite.tyyppi,Tosite.sarja,Tosite.tunniste,";


    QString kysymys("SELECT vienti.id AS id, vienti.pvm as pvm, vienti.tili as tili, debetsnt, kreditsnt, alvkoodi, alvprosentti, arkistotunnus, "
                    "selite, vienti.kohdennus as kohdennus, eraid as era_id, vienti.tosite as tosite_id, tosite.pvm as tosite_pvm, tosite.tunniste as tosite_tunniste,"
                    "tosite.tyyppi as tosite_tyyppi, tosite.sarja as tosite_sarja, kumppani.id as kumppani_id, "
                    "kumppani.nimi as kumppani_nimi, vienti.tyyppi as tyyppi,"
                    "CAST( (SELECT COUNT(liite.id) FROM Liite WHERE liite.tosite=tosite.id) AS int) AS liitteita "
                    "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id ");

    if( urlquery.hasQueryItem("kohdennus")) {
        Kohdennus kohdennus = kp()->kohdennukset()->kohdennus( urlquery.queryItemValue("kohdennus").toInt() );
        if( kohdennus.tyyppi() == Kohdennus::PROJEKTI )
            ehdot.append(QString("kohdennus=%1").arg(kohdennus.id()));
        else if( kohdennus.tyyppi() == Kohdennus::MERKKAUS ) {
            kysymys.append("JOIN Merkkaus ON Vienti.id=Merkkaus.vienti ");
            ehdot.append(QString("merkkaus.kohdennus=%1").arg(kohdennus.id()));
        } else {
            kysymys.append("JOIN Kohdennus ON Vienti.kohdennus=Kohdennus.id ");
            ehdot.append(QString("(Kohdennus.id=%1 OR Kohdennus.kuuluu=%1)").arg(kohdennus.id()));
        }
    }

    kysymys.append("LEFT OUTER JOIN Kumppani ON Vienti.kumppani=kumppani.id "
                    "WHERE ");
    kysymys.append( ehdot.join(" AND ") );
    kysymys.append(" ORDER BY " + jarjestys + "Vienti.pvm, Tosite.sarja, Tosite.tunniste, Vienti.rivi ");

    QSqlQuery kysely(db());
    kysely.exec(kysymys);

    QVariantList viennit = resultList(kysely);

    if( urlquery.hasQueryItem("vastatilit"))
        taydennaVastatilit(viennit);

    taydennaEratJaMerkkaukset(viennit);
    return viennit;

}

QVariant ViennitRoute::vienti(int id)
{
    QString kysymys(QString("SELECT vienti.id, pvm, tili, selite, debetsnt, kreditsnt, kumppani.nimi as kumppani "
                            "FROM Vienti LEFT OUTER JOIN Kumppani ON Vienti.kumppani=Kumppani.id WHERE Vienti.id=%1").arg(id));
    QSqlQuery kysely( db());
    kysely.exec(kysymys);

    return resultMap(kysely);
}

void ViennitRoute::taydennaVastatilit(QVariantList &lista)
{
    // Haetaan kaikkien listan tositteiden tilit yhdellä kyselyllä sen sijaan,
    // että jokaiselle viennille tehtäisiin oma kyselynsä.
    QSet<int> tositeIdt;
    for(const QVariant& v : lista) {
        int tositeId = v.toMap().value("tosite").toMap().value("id").toInt();
        if( tositeId )
            tositeIdt.insert(tositeId);
    }
    if( tositeIdt.isEmpty())
        return;

    QStringList idlist;
    for(int id : tositeIdt)
        idlist << QString::number(id);

    QMap<int, QVariantList> tilitTositteella;
    QSqlQuery kysely(db());
    kysely.exec(QString("SELECT tosite, tili FROM Vienti WHERE tosite IN (%1)").arg(idlist.join(',')));
    while(kysely.next()) {
        if( kysely.value(1).isNull())
            continue;   // NULL-tiliä ei näytetä vastatilinä, kuten alkuperäinen tili<>... -ehtokaan ei näyttänyt
        int tositeId = kysely.value(0).toInt();
        int tili = kysely.value(1).toInt();
        if( !tilitTositteella[tositeId].contains(tili))
            tilitTositteella[tositeId].append(tili);
    }

    for(int i=0; i < lista.count(); i++) {
        QVariantMap vienti = lista[i].toMap();
        int tili = vienti.value("tili").toInt();
        int tositeId = vienti.value("tosite").toMap().value("id").toInt();

        QVariantList vastatilit;
        for(const QVariant& v : tilitTositteella.value(tositeId)) {
            if( v.toInt() != tili)
                vastatilit.append(v);
        }
        if(!vastatilit.isEmpty()) {
            vienti.insert("vastatilit", vastatilit);
            lista[i] = vienti;
        }
    }

}
