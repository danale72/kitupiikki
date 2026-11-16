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
#include "asiakkaatroute.h"

AsiakkaatRoute::AsiakkaatRoute(SqlModel *model) :
    SQLiteRoute(model, "/asiakkaat")
{

}

QVariant AsiakkaatRoute::get(const QString &/*polku*/, const QUrlQuery &/*urlquery*/)
{
        QSqlQuery kysely(db());
        // Erien saldot lasketaan kerran ja vain laskujen erille; erääntyneisyys tulee
        // laskun tositteen eräpäivästä. Saldoon lasketaan vain kirjanpidossa olevat viennit.
        kysely.exec("select kumppani.id, kumppani.nimi, sum(summa.debetsnt) as summasnt, sum(avoin.sd) as avd, sum(avoin.sk) as avk, "
                "sum(CASE WHEN summa.erapvm < current_date THEN avoin.sd END) as vad, "
                "sum(CASE WHEN summa.erapvm < current_date THEN avoin.sk END) as vak from "
                "Kumppani JOIN ( select debetsnt, vienti.kumppani, vienti.id as vienti, tosite.erapvm as erapvm FROM "
                "Vienti JOIN Tosite ON vienti.tosite=tosite.id WHERE vienti.tyyppi=202 AND tosite.tila > 0) as summa ON summa.kumppani = kumppani.id "
                "LEFT OUTER JOIN ( select e.eraid as eraid, sum(e.debetsnt) as sd, SUM(e.kreditsnt) AS sk FROM Vienti AS e "
                "JOIN Tosite AS et ON e.tosite=et.id WHERE et.tila >= 100 "
                "AND e.eraid IN (SELECT lv.id FROM Vienti AS lv WHERE lv.tyyppi=202) GROUP BY e.eraid) AS avoin ON avoin.eraid = summa.vienti "
                "group by kumppani.id, kumppani.nimi order by kumppani.nimi ");
        QVariantList lista = resultList(kysely);

        for(int i=0; i < lista.count(); i++) {
            QVariantMap map = lista.at(i).toMap();
            double avd = map.take("avd").toLongLong() / 100.0;
            double avk = map.take("avk").toLongLong() / 100.0;
            map.insert("avoin", avd - avk);
            double evd = map.take("vad").toLongLong() / 100.0;
            double evk = map.take("vak").toLongLong() / 100.0;
            map.insert("eraantynyt", evd - evk);
            lista[i] = map;
        }
        return lista;
}
