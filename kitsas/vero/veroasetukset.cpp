/*
   Copyright (C) 2026 Kitsas contributors

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
#include "veroasetukset.h"

#include <QSettings>

namespace Vero {

static QString asetusAvain(Ymparisto ymparisto, const QString& nimi)
{
    return QStringLiteral("Vero/%1/%2").arg(ymparistoAvain(ymparisto), nimi);
}

bool Asetukset::kokonainen() const
{
    return !varmenne.isEmpty() && !avain.isEmpty() && !ohjelmaId.isEmpty() && !ohjelmaAvain.isEmpty();
}

Ymparisto Asetukset::valittuYmparisto(QSettings *settings)
{
    return ymparistoAvaimesta(settings->value(QStringLiteral("Vero/Ymparisto")).toString());
}

void Asetukset::asetaYmparisto(QSettings *settings, Ymparisto ymparisto)
{
    settings->setValue(QStringLiteral("Vero/Ymparisto"), ymparistoAvain(ymparisto));
}

Asetukset Asetukset::lataa(QSettings *settings, Ymparisto ymparisto)
{
    Asetukset asetukset;
    asetukset.ymparisto = ymparisto;
    asetukset.varmenne = settings->value(asetusAvain(ymparisto, QStringLiteral("Varmenne"))).toString();
    asetukset.avain = settings->value(asetusAvain(ymparisto, QStringLiteral("Avain"))).toString();
    asetukset.ohjelmaId = settings->value(asetusAvain(ymparisto, QStringLiteral("OhjelmaId"))).toString();
    asetukset.ohjelmaAvain = settings->value(asetusAvain(ymparisto, QStringLiteral("OhjelmaAvain"))).toString();
    asetukset.tilitoimisto = settings->value(asetusAvain(ymparisto, QStringLiteral("Tilitoimisto"))).toString();
    return asetukset;
}

void Asetukset::tallenna(QSettings *settings) const
{
    settings->setValue(asetusAvain(ymparisto, QStringLiteral("Varmenne")), varmenne);
    settings->setValue(asetusAvain(ymparisto, QStringLiteral("Avain")), this->avain);
    settings->setValue(asetusAvain(ymparisto, QStringLiteral("OhjelmaId")), ohjelmaId);
    settings->setValue(asetusAvain(ymparisto, QStringLiteral("OhjelmaAvain")), ohjelmaAvain);
    settings->setValue(asetusAvain(ymparisto, QStringLiteral("Tilitoimisto")), tilitoimisto);
}

}   // namespace Vero
