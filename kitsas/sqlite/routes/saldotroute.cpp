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
#include "saldotroute.h"

#include "db/kirjanpito.h"

#include <QDebug>

SaldotRoute::SaldotRoute(SqlModel* model) :
    SQLiteRoute(model, "/saldot")
{

}

QVariant SaldotRoute::get(const QString &/*polku*/, const QUrlQuery &urlquery)
{
    QDate pvm = QDate::fromString(urlquery.queryItemValue("pvm"),Qt::ISODate);
    Tilikausi kausi = kp()->tilikaudet()->tilikausiPaivalle(pvm);
    QDate kaudenalku = kausi.alkaa();
    if( urlquery.hasQueryItem("alkupvm"))
        kaudenalku = QDate::fromString( urlquery.queryItemValue("alkupvm"), Qt::ISODate );
    if( urlquery.hasQueryItem("kustannuspaikat"))
        return kustannuspaikat( kaudenalku, pvm, false );
    if( urlquery.hasQueryItem("projektit")) {
        int kuuluu = -1;
        if( urlquery.hasQueryItem("kohdennus"))
            kuuluu = urlquery.queryItemValue("kohdennus").toInt();
        return kustannuspaikat( kaudenalku, pvm, true, kuuluu );
    }

    QSqlQuery kysely(db());
    QVariantMap saldot;


    if( urlquery.hasQueryItem("tili")) {
        Tili* tili = kp()->tilit()->tili(urlquery.queryItemValue("tili").toInt());
        if(tili) {
            QString kysymys = QString("SELECT sum(debetsnt), sum(kreditsnt) "
                                      "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                                      "WHERE Vienti.tili = %1 AND Tosite.tila >= 100 AND ").arg(tili->numero()) +
                   ( tili->onko(TiliLaji::TULOS) ? QString("vienti.pvm BETWEEN '%1' AND '%2'").arg(kaudenalku.toString(Qt::ISODate), pvm.toString(Qt::ISODate))
                                                 : ( urlquery.hasQueryItem("alkusaldot")  ? QString("vienti.pvm < '%1'").arg(pvm.toString(Qt::ISODate))  :  QString("vienti.pvm <= '%1'").arg(pvm.toString(Qt::ISODate)) ));
            kysely.exec(kysymys);
            if(kysely.next()) {
                double saldo = tili->onko(TiliLaji::VASTAAVAA)
                        ? (kysely.value(0).toLongLong() - kysely.value(1).toLongLong()) / 100.0
                        : (kysely.value(1).toLongLong() - kysely.value(0).toLongLong()) / 100.0;
                saldot.insert(urlquery.queryItemValue("tili"), saldo);
            }
        }
        return saldot;
    }




    if( !urlquery.hasQueryItem("tuloslaskelma")) {
        // Tasetilien saldot sekä edellisten ja nykyisen kauden tulos yhdellä kyselyllä:
        // tulostilien osalta erotellaan ehdollisilla summilla kautta edeltävät viennit
        // Ilman tilikautta ei verrata tyhjään päivämäärään: Postgres hylkäisi koko kyselyn
        const QString ennenKautta = kausi.alkaa().isValid()
                ? QString("vienti.pvm < '%1'").arg(kausi.alkaa().toString(Qt::ISODate))
                : QString("1=0");
        QString kysymys = QString("SELECT tili, sum(debetsnt), sum(kreditsnt), "
                                  "sum(CASE WHEN %1 THEN debetsnt END), "
                                  "sum(CASE WHEN %1 THEN kreditsnt END) "
                                  "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id WHERE vienti.pvm ").arg(ennenKautta);
        kysymys += urlquery.hasQueryItem("alkusaldot") ? "<" : "<=";
        kysymys += QString("'%1' ").arg(pvm.toString(Qt::ISODate));
        kysymys += " AND tili IS NOT NULL AND Tosite.tila >= 100 GROUP BY tili ORDER BY tili ";

        qlonglong edellisetSnt = 0;
        qlonglong nykyinenSnt = 0;

        kysely.setForwardOnly(true);
        kysely.exec(kysymys);
        while (kysely.next()) {
            const QString tilistr = kysely.value(0).toString();
            const qlonglong debet = kysely.value(1).toLongLong();
            const qlonglong kredit = kysely.value(2).toLongLong();
            if( tilistr.compare(QLatin1String("3")) < 0 ) {
                // Tasetili (sama ehto kuin SQL:n CAST(tili AS text) < '3')
                if( tilistr.startsWith(QChar('1')))
                    saldot.insert( tilistr, Euro::fromCents(debet - kredit).toString());
                else
                    saldot.insert( tilistr, Euro::fromCents(kredit - debet).toString());
            } else {
                const qlonglong debetEnnen = kysely.value(3).toLongLong();
                const qlonglong kreditEnnen = kysely.value(4).toLongLong();
                edellisetSnt += kreditEnnen - debetEnnen;
                nykyinenSnt += (kredit - kreditEnnen) - (debet - debetEnnen);
            }
        }

        // Edellisten tulos. Tilikartassa ei välttämättä ole tiliä (testikartta):
        // numero 0 ei ole tili, eikä sitä pidä kirjoittaa avaimeksi "0".
        {
            const int edNumero = kp()->tilit()->tiliTyypilla(TiliLaji::EDELLISTENTULOS).numero();
            if( edNumero ) {
                const QString edtili = QString::number(edNumero);
                double saldo = ( qRound64(saldot.value(edtili).toDouble()*100) + edellisetSnt ) / 100.0 ;
                saldot[edtili] = saldo;
            }
        }

        if( !urlquery.hasQueryItem("alkusaldot")) {
            // Nykyisen tulos
            const int tulosNumero = kp()->tilit()->tiliTyypilla(TiliLaji::KAUDENTULOS).numero();
            if( tulosNumero )
                saldot.insert(QString::number(tulosNumero), nykyinenSnt / 100.0 );
        }
    }

    if( !urlquery.hasQueryItem("tase")) {
        Kohdennus kohdennus = kp()->kohdennukset()->kohdennus( urlquery.queryItemValue("kohdennus").toInt() );

        QString kysymys("SELECT tili, SUM(kreditsnt), SUM(debetsnt) FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id ");
        if( kohdennus.tyyppi() == Kohdennus::KUSTANNUSPAIKKA || kohdennus.tyyppi() == Kohdennus::PROJEKTI ) {
            kysymys.append("JOIN Kohdennus ON Vienti.kohdennus=Kohdennus.id");
        } else if( kohdennus.tyyppi() == Kohdennus::MERKKAUS) {
            kysymys.append("LEFT OUTER JOIN Merkkaus ON Vienti.id=Merkkaus.vienti ");
        }


        kysymys.append(" WHERE vienti.pvm ");

        if( urlquery.hasQueryItem("alkusaldot"))
            kysymys += "<";
        else
            kysymys += "<=";
        kysymys += QString(" '%1' ").arg(pvm.toString(Qt::ISODate));

        if( kohdennus.tyyppi() == Kohdennus::KUSTANNUSPAIKKA || kohdennus.tyyppi() == Kohdennus::PROJEKTI ) {
            kysymys += QString(" AND (kohdennus.id=%1 OR kohdennus.kuuluu=%1) ").arg(kohdennus.id());
        } else if( kohdennus.tyyppi() == Kohdennus::MERKKAUS) {
            kysymys += QString(" AND Merkkaus.kohdennus=%1 ").arg(kohdennus.id());
        }

        kysymys += QString(" AND vienti.pvm >= '%1' AND CAST(tili as text) >= '3' AND Tosite.tila >= 100 GROUP BY tili ORDER BY tili")
                .arg(kaudenalku.toString(Qt::ISODate));

        if( !kysely.exec(kysymys) )
            throw SQLiteVirhe(kysely);

        while( kysely.next()) {
            Euro kredit = Euro::fromCents(kysely.value(1).toLongLong());
            Euro debet = Euro::fromCents(kysely.value(2).toLongLong());
            saldot.insert( kysely.value(0).toString(), (kredit-debet).toString());
        }
    }
    return saldot;
}

QVariant SaldotRoute::kustannuspaikat(const QDate &mista, const QDate &mihin, bool projektit, int kuuluu)
{
    QVariantMap kohdennukset;

    QSqlQuery kysely(db());
    QString kysymys;

    if( projektit) {
        kysymys = QString("SELECT kohdennus, tili, SUM(kreditsnt) as ks, SUM(debetsnt) as ds "
                          "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                          "JOIN Kohdennus ON Vienti.kohdennus=Kohdennus.id "
                          "WHERE Tosite.tila >= 100 AND Kohdennus.tyyppi=2 AND CAST(tili as text) >= '3' "
                          "AND Vienti.pvm BETWEEN '%1' AND '%2' %3"
                          "GROUP BY kohdennus,tili ")
                .arg(mista.toString(Qt::ISODate), mihin.toString(Qt::ISODate),
                     kuuluu > -1 ? QString("AND kuuluu=%1 ").arg(kuuluu) : "" );
    }
    else
        kysymys = QString("SELECT kohdennus, kuuluu, tili, SUM(kreditsnt) as ks, SUM(debetsnt) as ds "
                          "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                          "JOIN Kohdennus ON Vienti.kohdennus=Kohdennus.id "
                          "WHERE Tosite.tila >= 100 AND CAST(tili as text) >= '3' "
                          "AND Vienti.pvm BETWEEN '%1' AND '%2' "
                          "GROUP BY kohdennus, kuuluu, tili ")
                .arg(mista.toString(Qt::ISODate)).arg(mihin.toString(Qt::ISODate));

    if( !kysely.exec(kysymys) )
        throw SQLiteVirhe(kysely);

    while( kysely.next()) {
        QString kohdennus = kysely.value("kohdennus").toString();
        if( !projektit && !kysely.value("kuuluu").isNull())
            kohdennus = kysely.value("kuuluu").toString();

        QString tilistr = kysely.value("tili").toString();

        QVariantMap kmap = kohdennukset.value(kohdennus).toMap();
        kmap.insert(tilistr, (  qRound64(kmap.value(tilistr).toDouble() * 100.0 ) +
                                qRound64(kysely.value("ks").toDouble() ) -
                                qRound64(kysely.value("ds").toDouble() )) / 100.0 );

        kohdennukset.insert( kohdennus, kmap);
    }


    return kohdennukset;
}
