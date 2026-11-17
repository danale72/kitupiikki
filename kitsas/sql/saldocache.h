/*
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/
#ifndef SALDOCACHE_H
#define SALDOCACHE_H

#include <QHash>
#include <QString>
#include <QVariant>

/**
 * @brief Paikallisen taustajärjestelmän GET /saldot -vastausten välimuisti
 *
 * Saldokysely laskee koosteet koko vientihistoriasta, ja samoja saldoja kysyy moni
 * käyttöliittymän osa (aloitussivu, saldotelakka, tilinvalinta, apurit, raportit).
 * Avaimena on kyselyn polku ja parametrit. Välimuisti tyhjennetään jokaisen
 * muokkaavan kyselyn (POST/PUT/PATCH/DELETE ja liitteet), kirjanpidon vaihtumisen ja
 * kirjanpitoaMuokattu-signaalin yhteydessä.
 */
class SaldoCache
{
public:
    static bool onkoValimuistissa(const QString& polku);

    bool hae(const QString& avain, QVariant* tulos) const;
    void tallenna(const QString& avain, const QVariant& tulos);
    void tyhjenna();
    int koko() const { return valimuisti_.size(); }

    static QString avain(const QString& polku, const QString& kysely);

private:
    static const int MAKSIMIKOKO = 64;
    QHash<QString, QVariant> valimuisti_;
};

#endif // SALDOCACHE_H
