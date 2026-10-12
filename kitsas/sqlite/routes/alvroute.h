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
#ifndef ALVROUTE_H
#define ALVROUTE_H

#include "../sqliteroute.h"

class AlvRoute : public SQLiteRoute
{
public:
    AlvRoute(SqlModel *model);

    QVariant get(const QString &polku, const QUrlQuery &urlquery = QUrlQuery()) override;
    QVariant put(const QString &polku, const QVariant &data) override;

    /**
     * @brief Kauden alv-kirjausten sormenjälki
     *
     * Kauden alv-koodillisten vientien nettosumma (debet - kredit) alv-koodeittain ja
     * -prosenteittain, alv-ilmoitustositteita lukuun ottamatta. Tallennetaan alv-ilmoitukseen
     * sen laatimishetkellä; jos sormenjälki myöhemmin muuttuu, on kauden alv-kirjauksia
     * muokattu ilmoituksen antamisen jälkeen.
     */
    static QVariantMap sormenjalki(QSqlDatabase db, const QDate& alkaa, const QDate& paattyy);
    static bool samaSormenjalki(const QVariantMap& a, const QVariantMap& b);
};

#endif // ALVROUTE_H
