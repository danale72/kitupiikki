/*
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/
#include "saldocache.h"

bool SaldoCache::onkoValimuistissa(const QString &polku)
{
    return polku == QLatin1String("/saldot");
}

bool SaldoCache::hae(const QString &avain, QVariant *tulos) const
{
    auto iter = valimuisti_.constFind(avain);
    if( iter == valimuisti_.constEnd())
        return false;
    *tulos = iter.value();
    return true;
}

void SaldoCache::tallenna(const QString &avain, const QVariant &tulos)
{
    if( valimuisti_.size() >= MAKSIMIKOKO )
        valimuisti_.clear();
    valimuisti_.insert(avain, tulos);
}

void SaldoCache::tyhjenna()
{
    valimuisti_.clear();
}

QString SaldoCache::avain(const QString &polku, const QString &kysely)
{
    return polku + QLatin1Char('?') + kysely;
}
