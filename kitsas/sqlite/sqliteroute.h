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
#ifndef SQLITEROUTE_H
#define SQLITEROUTE_H

#include "sql/sqlmodel.h"
#include "sqlitekysely.h"

#include <QSqlDatabase>
#include <QSqlQuery>

#include <exception>
#include <functional>

class SQLiteRoute
{
public:
    SQLiteRoute(SqlModel *model, const QString& polku);
    virtual ~SQLiteRoute();

    QVariant route(SQLiteKysely* kysely, const QVariant& data);
    virtual QPair<const QVariant,int> byteArray(SQLiteKysely* kysely, const QByteArray &ba, const QMap<QString,QString> &meta);

    QString polku() const { return polku_;}

    /** Tyhjentää reitin omat välimuistit (kirjanpito vaihtuu tai sitä muokataan) */
    virtual void tyhjennaValimuisti() {}

protected:

    virtual QVariant get(const QString& polku, const QUrlQuery& urlquery = QUrlQuery());
    virtual QVariant put(const QString& polku, const QVariant& data);
    virtual QVariant post(const QString& polku, const QVariant& data);
    virtual QVariant patch(const QString& polku, const QVariant& data);
    virtual QVariant doDelete(const QString& polku);

    QVariantList resultList(QSqlQuery& kysely);
    /** resultListin tyhjän kentän ehto: NULL, tyhjä tai "0" */
    static bool onTyhja(const QVariant& arvo);
    QVariantMap resultMap(QSqlQuery& kysely);
    static QString mapToJson(const QVariantMap& map);

    void taydennaEratJaMerkkaukset(QVariantList& vientilista);

    /**
     * @brief Tekee toisistaan riippumattomat kirjoitukset yhdessä tapahtumassa
     *
     * Yksi tapahtuma säästää SQLitessä levyn synkronoinnit ja Postgresissa verkkokierrokset.
     * Jos jokin kirjoitus epäonnistuu (kirjoitukset palauttaa false), tapahtuma perutaan ja
     * kirjoitukset tehdään uudelleen ilman tapahtumaa, jolloin yksittäinen virhe ei estä
     * muita kirjoituksia: Postgres keskeyttäisi muuten koko tapahtuman ensimmäiseen virheeseen.
     */
    void kirjoitaTapahtumana(const std::function<bool()>& kirjoitukset);

protected:
    QSqlDatabase db();
    SqlModel *model_;
    QString polku_;
    /** Käsiteltävän kyselyn parametrit myös muille kuin GET-pyynnöille */
    QUrlQuery urlKysely_;
};

#endif // SQLITEROUTE_H
