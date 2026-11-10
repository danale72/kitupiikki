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
#ifndef VEROTOSITE_H
#define VEROTOSITE_H

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

namespace Vero {

struct Liitetiedosto {
    QString nimi;
    QByteArray sisalto;
};

/**
 * @brief Verohallinnolta saapuneen tai sille lähetetyn sanoman tiedot tositteena
 *
 * Tosite on muistiotyyppinen (ei vientejä). Sanoman tiedot ovat tositteen
 * json-sarakkeessa avaimen "vero" alla, ja tositteen viite on sanoman
 * tunniste, jolla sama sanoma löytyy uudelleen ilman skeeman muutoksia.
 */
struct TositeTiedot {
    QString viite;
    QDate pvm;
    QString otsikko;
    QString kommentti;
    QVariantMap vero;
    QList<Liitetiedosto> liitteet;
};

/// Tositteen tallennuksen tulos: id on 0 ja virhe selitys, jos tallennus epäonnistui
typedef std::function<void(int id, const QString& virhe)> TallennusValmis;

/// Kirjanpidossa olevan tositteen sanomatiedot: id on 0 ja virhe false, jos tositetta ei löytynyt
struct LoydettyTosite {
    int id = 0;
    QVariantMap vero;
    /// Tositteen liitteiden nimet. Niitä verrataan vero.odotetutLiitteet-listaan,
    /// jotta puuttuva liite huomataan ja haetaan uudelleen.
    QStringList liitteet;
    /// Haku epäonnistui. Silloin id on 0, mutta se EI tarkoita että tositetta ei ole:
    /// lähetys- tai tallennustoimintoa ei saa jatkaa, koska kaksoiskappale voisi syntyä.
    bool virhe = false;
};

}   // namespace Vero

/**
 * @brief Vero-tositteiden luonti, päivitys ja haku
 *
 * Kaikki kutsut ovat asynkronisia ja kulkevat Kitsaan normaalin Tosite- ja
 * KpKysely-rajapinnan kautta, joten sama koodi toimii SQLite- ja Postgres-kirjanpidossa.
 */
class VeroTosite
{
public:
    /// Hakee viitteellä tositteen, jonka tila on kirjanpidossa. Ei löydy: id 0.
    static void etsi(QObject* konteksti, const QString& viite,
                     const std::function<void(const Vero::LoydettyTosite&)>& valmis);

    /**
     * @brief Luo uuden tositteen tai päivittää olemassa olevan
     *
     * Päivityksessä tositteen päivämäärää ja otsikkoa ei muuteta, "vero"-tiedot
     * korvataan ja liitteet lisätään aiempien jatkoksi, jolloin lähetysten
     * historia säilyy.
     *
     * @param tositeId 0 luo uuden tositteen
     */
    static void tallenna(QObject* konteksti, int tositeId, const Vero::TositeTiedot& tiedot,
                         const Vero::TallennusValmis& valmis);
};

#endif // VEROTOSITE_H
