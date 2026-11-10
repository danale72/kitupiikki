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
#ifndef VEROASETUKSET_H
#define VEROASETUKSET_H

#include <QString>

#include "veroapu.h"

class QSettings;

namespace Vero {

/**
 * @brief Tilitoimiston Vero-yhteyden asetukset
 *
 * Asetukset tallennetaan koneelle (QSettings), eivät kirjanpitoon: varmenne,
 * yksityinen avain ja ohjelmatunnisteet kuuluvat tilitoimistolle, eivätkä ne saa
 * päätyä asiakkaan kirjanpidon mukana eteenpäin eivätkä palvelinpuolen
 * Postgres-kantaan. Avaintiedostoa ei kopioida, vain polku tallennetaan.
 * SoftwareKey ja polut ovat selväkielisinä asetustiedostossa — älä jaa sitä
 * äläkä käytä jaettua konetunnusta. Testi- ja tuotantoympäristöllä on omat tunnisteensa.
 */
struct Asetukset {
    Ymparisto ymparisto = TESTI;
    QString varmenne;       ///< Polku asiakasvarmenteeseen (PEM)
    QString avain;          ///< Polku varmenteen yksityiseen avaimeen (PEM)
    QString ohjelmaId;      ///< Vero-SoftwareId
    QString ohjelmaAvain;   ///< Vero-SoftwareKey
    QString tilitoimisto;   ///< Tilitoimiston Y-tunnus (GetToken-valtuutettu), tyhjänä varmenteen CN

    /// Ovatko kaikki yhteyteen tarvittavat tiedot annettu
    bool kokonainen() const;

    /// Valittu ympäristö
    static Ymparisto valittuYmparisto(QSettings* settings);
    static void asetaYmparisto(QSettings* settings, Ymparisto ymparisto);

    /// Annetun ympäristön asetukset
    static Asetukset lataa(QSettings* settings, Ymparisto ymparisto);
    void tallenna(QSettings* settings) const;
};

}   // namespace Vero

#endif // VEROASETUKSET_H
