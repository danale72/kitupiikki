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
#ifndef VEROPALVELU_H
#define VEROPALVELU_H

#include <QDate>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QSharedPointer>
#include <QStringList>

#include <functional>
#include <utility>

#include "veroapu.h"
#include "veroasetukset.h"
#include "veroclient.h"
#include "verotosite.h"

namespace Vero {

/// Alv-ilmoituksen tila Verohallintoon nähden, tositteiden perusteella
enum class LahetysTila {
    EILAHETETTY,
    KASITTELYSSA,   ///< Verohallinto on vastaanottanut, mutta ei vielä käsitellyt (HTTP 200)
    KASITELTY,      ///< Verohallinto on käsitellyt ilmoituksen
    /// Lähetys tallennettiin, mutta vastausta ei saatu (yhteys katkesi tai ohjelma
    /// sulkeutui kesken). Ilmoitus on voitu vastaanottaa: uutta alkuperäistä
    /// ilmoitusta ei saa lähettää ennen kuin käyttäjä on tarkistanut tilanteen.
    TARKISTETTAVA
};

/// Kauden alv-ilmoituksen tila kirjanpidon tositteiden mukaan
struct LahetysTietoa {
    LahetysTila tila = LahetysTila::EILAHETETTY;
    int lahetettyId = 0;    ///< Tällä ohjelmalla lähetetyn ilmoituksen tosite (0 = ei ole)
    int saapunutId = 0;     ///< Verohallinnolta haetun käsitellyn ilmoituksen tosite (0 = ei ole)
    QVariantMap vero;       ///< Lähetetyn, tai jos sitä ei ole, saapuneen tositteen vero-tiedot
    /// Lähetetyn tositteen tilannekuva. Sama tila eri yrityksellä antaa eri arvon.
    QString vahvistus;
    /// Tilaa ei voitu selvittää (tietokantahaku epäonnistui). Muut kentät eivät ole luotettavia.
    bool virhe = false;
};

}   // namespace Vero

/**
 * @brief Verohallinnon rajapintojen käyttö avoinna olevan kirjanpidon puolesta
 *
 * Hakee asiakkaalle osoitetut kirjeet ja alv-ilmoitukset sekä lähettää alv-ilmoituksen.
 * Kaikki vaihdetut sanomat tallennetaan kirjanpitoon muistiotositteina (ks. VeroTosite),
 * joten mitään ei tallenneta kirjanpitoon tietokannan rakennetta muuttamatta.
 *
 * Operaatiot ajetaan yksi kerrallaan; valmis() lähetetään, kun operaatio on päättynyt.
 */
class VeroPalvelu : public QObject
{
    Q_OBJECT
public:
    explicit VeroPalvelu(QObject *parent = nullptr);

    VeroClient* asiakas() const { return asiakas_; }

    /**
     * @brief Lataa asetukset, varmenteen ja tarkistaa että yhteys voidaan muodostaa
     * @param virhe Selitys, jos yhteyttä ei voi käyttää
     */
    bool alusta(QString* virhe);
    /// alusta()-kutsun varoitukset, esim. varmenteen vanheneminen
    QStringList varoitukset() const { return varoitukset_; }

    Vero::Ymparisto ymparisto() const { return asetukset_.ymparisto; }
    /// Avoinna olevan kirjanpidon Y-tunnus normalisoituna, tyhjä jos puuttuu tai on virheellinen
    static QString asiakkaanYtunnus();

    bool kaynnissa() const { return kaynnissa_; }

    /**
     * @brief Hakee Verohallinnolta kirjeet ja annetun vuoden alv-ilmoitukset
     *
     * Uudet kirjeet ja käsitellyt alv-ilmoitukset tallennetaan tositteiksi.
     * Lähetettyjen alv-ilmoitusten tila päivitetään, kun Verohallinto on käsitellyt ne.
     */
    void hae(int vuosi);

    /**
     * @brief Lähettää alv-ilmoituksen Verohallinnolle (FileVATReturn)
     *
     * Verohallinnon HTTP 200 -vastaus tarkoittaa vain, että ilmoitus on vastaanotettu
     * jonoon (eräajo); käsittelyn tulos nähdään vasta myöhemmin hae()-kutsulla.
     *
     * Jos kauden ilmoitus on jo lähetetty, sen tositetta päivitetään ja uusi sanoma
     * liitetään aiempien jatkoksi.
     *
     * @param alvTositeId Kirjanpidon alv-laskelmatosite, jonka koodit ilmoitetaan
     * @param vahvistettuTila Tila, jonka käyttäjä näki vahvistaessaan lähetyksen.
     *        Lähetys tehdään vain, jos lukon jälkeen luettu tila on edelleen sama.
     * @param vahvistus Lähetetyn tositteen tilannekuva vahvistushetkellä.
     *        Sama tilakoodi uudella yrityksellä ei riitä lähetykseen.
     */
    void lahetaAlv(int alvTositeId, const QDate& loppuu, bool korvaava, const QString& korvauksenSyy,
                   Vero::LahetysTila vahvistettuTila, const QString& vahvistus);

    /**
     * @brief Kauden alv-ilmoituksen tila tositteiden perusteella
     *
     * Käsitelty ilmoitus on käsitelty myös silloin, kun se on tehty muualla (OmaVero, toinen
     * ohjelma) ja vain haettu tähän kirjanpitoon: silloin lähetettyId on 0 mutta saapunutId
     * ja tila KASITELTY, ja uusi lähetys on korvaava ilmoitus. Tila päivittyy vain haetuista
     * tiedoista, joten tuoreinta tilaa varten kannattaa ensin hakea Verohallinnolta.
     */
    static void lahetyksenTila(QObject* konteksti, Vero::Ymparisto ymparisto, const QDate& loppuu,
                               const std::function<void(const Vero::LahetysTietoa&)>& valmis);

public slots:
    /// Keskeyttää käynnissä olevan toiminnon, esim. kun kirjanpito vaihtuu
    void peruuta();

signals:
    /// Rivi käyttäjälle näytettävään lokiin
    void loki(const QString& teksti);
    /// Verohallinnon ilmoittamat verokaudet (GetVATPeriods)
    void kaudetHaettu(const QList<Vero::Verokausi>& kaudet);
    /// Kirjanpitoon on lisätty tai päivitetty tosite
    void tositeTallennettu(int tositeId);
    /// Operaatio on päättynyt; ok on false, jos jokin vaihe epäonnistui
    void valmis(bool ok);

private:
    typedef std::function<void()> Seuraava;
    typedef std::function<void(Seuraava)> Tehtava;
    typedef std::function<void(const QString& token, const QString& virhe)> TokenValmis;

    void aloita();
    void lisaaTehtava(const Tehtava& tehtava);
    void ajaSeuraava();
    void lopeta(bool ok);
    void virheella(const QString& teksti);

    void haeToken(const TokenValmis& valmis);
    void kutsu(Vero::Rajapinta rajapinta, const QVariantMap& sanoma, bool pdf,
               const std::function<void(const Vero::Vastaus&)>& valmis, bool vartioi = true);
    void etsi(const QString& viite, const std::function<void(const Vero::LoydettyTosite&)>& valmis);

    /// Varaa kauden lähetyksen tälle yhteydelle. Tyhjä tunnus: varaus on toisella yhteydessä.
    /// korvaa=false ei ota vanhentunuttakaan varausta, joten käynnissä olevaa lähetystä ei ohiteta.
    void varaaLahetys(const QString& viite, const std::function<void(const QString& tunnus)>& valmis,
                      bool korvaa = true);

    /// Uuden Vero-tositteen luonti niin, että toinen yhteys ei tee samaa viitettä
    struct UudenTositteenVaraus {
        Vero::LoydettyTosite loydetty;
        QString tunnus;     ///< Varaus, joka pitää vapauttaa tallennuksen jälkeen. Tyhjä, jos ei ole meillä.
        bool estetty = false;  ///< Toinen yhteys luo tositetta, eikä sitä vielä näy
    };
    void varaaUusi(const QString& viite, const std::function<void(const UudenTositteenVaraus&)>& valmis);
    void vahvistaVaraus(const QString& viite, const QString& tunnus, const std::function<void(bool oma)>& valmis);
    /// Siirtää oman varauksen aikaleimaa eteenpäin. Epäonnistuu, jos tunnus ei enää omista riviä.
    void uudistaVaraus(const QString& viite, const QString& tunnus, const std::function<void(bool oma)>& valmis);
    /// Vapauttaa varauksen vain, jos kirjanpito ei ole ehtinyt vaihtua
    void vapautaVaraus(quint64 sukupolvi, const QString& viite, const QString& tunnus);

    /**
     * @brief Estää vanhentuneen kutsun jatkumisen
     *
     * Palautettu funktio ei tee mitään, jos toiminto on sillä välin peruttu (kirjanpito
     * vaihtui), jotta yhden asiakkaan vastaus ei koskaan tallennu toisen asiakkaan kirjanpitoon.
     */
    template<typename F>
    auto vartioitu(F toiminto) {
        const quint64 sukupolvi = sukupolvi_;
        return [this, sukupolvi, toiminto](auto&&... argumentit) {
            if (sukupolvi == sukupolvi_)
                toiminto(std::forward<decltype(argumentit)>(argumentit)...);
        };
    }

    struct KirjeHaku;
    void haeKirjeSivu(const QSharedPointer<KirjeHaku>& haku, const Seuraava& seuraava);
    void tallennaKirje(const QString& ytunnus, const Vero::Kirje& kirje, const Seuraava& seuraava);

    void haeKaudet(const QString& ytunnus, int vuosi, const Seuraava& seuraava);
    void kasitteleKausi(const QString& ytunnus, const Vero::Verokausi& kausi, const Seuraava& seuraava);

    void tallennaLahetetty(int alvTositeId, const QVariantMap& alv, const QDate& loppuu,
                           bool korvaava, const QString& korvauksenSyy,
                           const QVariantMap& sanoma, const Vero::Vastaus& vastaus,
                           const Vero::LoydettyTosite& aiempi, const Seuraava& seuraava);
    void tallennaTosite(int tositeId, const Vero::TositeTiedot& tiedot,
                        const std::function<void(bool ok)>& valmis);

    Vero::Asetukset asetukset_;
    VeroClient* asiakas_;
    QStringList varoitukset_;

    QString delegaatti_;
    QString token_;
    quint64 sukupolvi_ = 0;     ///< Kasvaa, kun käynnissä oleva toiminto perutaan
    QQueue<Tehtava> jono_;
    bool kaynnissa_ = false;
    bool kaikkiOk_ = true;
};

#endif // VEROPALVELU_H
