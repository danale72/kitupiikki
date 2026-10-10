/*
   Copyright (C) 2019 Arto Hyvättinen
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/
#ifndef SQLALUSTAJA_H
#define SQLALUSTAJA_H

#include <QSqlDatabase>
#include <QVariantMap>

class QProgressDialog;
class QSqlQuery;

/**
 * @brief Yhteiset SQL-taustajärjestelmän alustustoiminnot
 */
class SqlAlustaja
{
public:
    static bool suoritaSqlResurssi(QSqlDatabase db, const QString& resurssi);
    /**
     * @brief Kirjoittaa velhon init-datan (asetukset, tilikartta, tilikaudet)
     * @return false heti ensimmäisestä epäonnistuneesta lisäyksestä; syy
     *         virhetekstiin. Kutsujan on peruttava tapahtuma.
     */
    static bool kirjoitaInit(QSqlDatabase db, const QVariantMap& initMap, QProgressDialog *progress = nullptr,
                             QString *virheteksti = nullptr);

private:
    static QString json(const QVariant& var);
    static bool virhe(const QSqlQuery& kysely, const QString& kohde, QString *virheteksti);
    static bool aseta(QSqlDatabase db, const QString& avain, const QVariant& arvo, QString *virheteksti);
    static bool kirjoitaAsetukset(QSqlDatabase db, const QVariantMap& asetukset, QString *virheteksti);
    static bool kirjoitaTilit(QSqlDatabase db, const QVariantList& tililista, QString *virheteksti);
    static bool kirjoitaTilikaudet(QSqlDatabase db, const QVariantList& kausilista, QString *virheteksti);
    static bool kirjoitaAvausTosite(QSqlDatabase db, const QDate& tilinavauspaiva, QString *virheteksti);
};

#endif // SQLALUSTAJA_H
