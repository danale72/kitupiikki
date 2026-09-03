/*
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/
#ifndef SQLITETUOJA_H
#define SQLITETUOJA_H

#include <QSqlDatabase>
#include <QString>

/**
 * @brief Tuo aiemman SQLite-kirjanpitotiedoston (.kitsas) sisällön tyhjään,
 * juuri Kitsas-kaaviolla alustettuun Postgres-tietokantaan.
 *
 * Lähdetiedoston pitää olla täsmälleen nykyistä tietokantaversiota
 * (SqlModel::TIETOKANTAVERSIO) - vanhempi tiedosto pitää ensin avata ja
 * päivittää tavallisesti Kitsaassa.
 */
class SqliteTuoja
{
public:
    static bool tuo(QSqlDatabase postgres, const QString& sqlitePolku, bool ilmoitaVirheesta = true);
};

#endif // SQLITETUOJA_H
