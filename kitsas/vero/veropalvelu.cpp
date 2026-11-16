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
#include "veropalvelu.h"

#include "db/asetusmodel.h"
#include "db/kirjanpito.h"
#include "db/kpkysely.h"
#include "db/tilikausimodel.h"
#include "db/tositetyyppimodel.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSettings>
#include <QUuid>

#include <utility>

namespace {

/// Vero-tositteen sanoman tila-arvot
const QString TILA_LAHETTAMASSA = QStringLiteral("lahettamassa");
const QString TILA_TUNTEMATON = QStringLiteral("tuntematon");
const QString TILA_HYLATTY = QStringLiteral("hylatty");
const QString TILA_KASITTELYSSA = QStringLiteral("kasittelyssa");
const QString TILA_KASITELTY = QStringLiteral("kasitelty");

/// Liite jäi puuttumaan tai tallennus jäi kesken. Vanha tosite ilman odotettuja
/// nimiä ei ole puutteellinen: sitä ei osata täydentää jälkikäteen.
bool liitteetPuutteelliset(const Vero::LoydettyTosite& tosite)
{
    if (!tosite.id)
        return false;
    if (tosite.vero.value(QStringLiteral("kesken")).toBool())
        return true;
    return !Vero::puuttuvatLiitteet(tosite.vero.value(QStringLiteral("odotetutLiitteet")).toStringList(),
                                    tosite.liitteet).isEmpty();
}

/// Testiympäristön tositteiden otsikon loppuun lisättävä merkintä
QString ymparistoLoppu(Vero::Ymparisto ymparisto)
{
    return ymparisto == Vero::TESTI ? QStringLiteral(" [Testi]") : QString();
}

QByteArray kauniisti(const QVariantMap& map)
{
    return QJsonDocument::fromVariant(map).toJson(QJsonDocument::Indented);
}

}   // namespace

struct VeroPalvelu::KirjeHaku {
    QString ytunnus;
    QDate alku;
    QDate loppu;
    int sivuja = 0;
    QSet<QString> nahdyt;
    QList<Vero::Kirje> kirjeet;
};

VeroPalvelu::VeroPalvelu(QObject *parent) :
    QObject(parent),
    asiakas_(new VeroClient(this))
{
    // Vastaus kuuluu aina siihen kirjanpitoon, jonka puolesta kysyttiin
    connect(kp(), &Kirjanpito::tietokantaVaihtui, this, &VeroPalvelu::peruuta);
}

void VeroPalvelu::peruuta()
{
    sukupolvi_++;
    jono_.clear();
    token_.clear();
    if (kaynnissa_) {
        kaynnissa_ = false;
        emit loki(tr("Kirjanpito vaihtui, Vero-toiminto keskeytettiin"));
        emit valmis(false);
    }
}

QString VeroPalvelu::asiakkaanYtunnus()
{
    return Vero::normalisoiYtunnus(kp()->asetukset()->asetus(AsetusModel::Ytunnus));
}

bool VeroPalvelu::alusta(QString *virhe)
{
    const auto epaonnistui = [virhe](const QString& selitys) {
        if (virhe)
            *virhe = selitys;
        return false;
    };

    varoitukset_.clear();
    QSettings* settings = kp()->settings();
    asetukset_ = Vero::Asetukset::lataa(settings, Vero::Asetukset::valittuYmparisto(settings));
    if (!asetukset_.kokonainen())
        return epaonnistui(tr("Vero-yhteyden asetukset puuttuvat (%1). Avaa Vero-sivun asetukset.")
                           .arg(Vero::ymparistoNimi(asetukset_.ymparisto)));

    QString selitys;
    if (!asiakas_->asetaVarmenne(asetukset_.varmenne, asetukset_.avain, &selitys))
        return epaonnistui(selitys);
    asiakas_->asetaOhjelma(asetukset_.ohjelmaId, asetukset_.ohjelmaAvain);

    const QDateTime nyt = QDateTime::currentDateTime();
    const QDateTime vanhenee = asiakas_->varmenne().expiryDate();
    if (vanhenee < nyt)
        return epaonnistui(tr("Asiakasvarmenne on vanhentunut %1").arg(vanhenee.date().toString("dd.MM.yyyy")));
    if (vanhenee < nyt.addDays(30))
        varoitukset_ << tr("Asiakasvarmenne vanhenee %1").arg(vanhenee.date().toString("dd.MM.yyyy"));

    // Testi- ja tuotantovarmenne eivät kelpaa toisen ympäristön palvelimelle
    if (asetukset_.ymparisto == Vero::TUOTANTO && asiakas_->onkoTestivarmenne())
        return epaonnistui(tr("Tuotantoympäristössä on valittu testivarmenne"));
    if (asetukset_.ymparisto == Vero::TESTI && !asiakas_->onkoTestivarmenne())
        varoitukset_ << tr("Testiympäristössä käytetään varmennetta, joka ei näytä testivarmenteelta");

    delegaatti_ = Vero::normalisoiYtunnus(asetukset_.tilitoimisto);
    if (delegaatti_.isEmpty())
        delegaatti_ = asiakas_->varmenteenYtunnus();
    if (delegaatti_.isEmpty())
        return epaonnistui(tr("Tilitoimiston Y-tunnus puuttuu eikä sitä voi päätellä varmenteesta"));

    if (asiakkaanYtunnus().isEmpty())
        return epaonnistui(tr("Kirjanpidon Y-tunnus puuttuu tai on virheellinen (Asetukset / Perustiedot)"));

    // Harjoituskirjan luvut eivät saa lähteä tuotantoon koneelle tallennetuilla tunnuksilla
    if (!Vero::harjoitukselleSallittu(kp()->onkoHarjoitus(), asetukset_.ymparisto))
        return epaonnistui(tr("Harjoituskirjanpidosta ei voi asioida Verohallinnon tuotannossa. "
                              "Vaihda Vero-asetuksissa testiympäristöön tai avaa oikea kirjanpito."));

    return true;
}

void VeroPalvelu::aloita()
{
    sukupolvi_++;       // Aiemmin perutun toiminnon myöhästyneet vastaukset eivät kuulu tähän
    kaynnissa_ = true;
    kaikkiOk_ = true;
    token_.clear();
    jono_.clear();
}

void VeroPalvelu::lisaaTehtava(const Tehtava &tehtava)
{
    jono_.enqueue(tehtava);
}

void VeroPalvelu::ajaSeuraava()
{
    if (jono_.isEmpty()) {
        lopeta(kaikkiOk_);
        return;
    }
    const Tehtava tehtava = jono_.dequeue();
    tehtava([this] { ajaSeuraava(); });
}

void VeroPalvelu::lopeta(bool ok)
{
    kaynnissa_ = false;
    token_.clear();
    emit valmis(ok);
}

void VeroPalvelu::virheella(const QString &teksti)
{
    kaikkiOk_ = false;
    emit loki(teksti);
}

void VeroPalvelu::kutsu(Vero::Rajapinta rajapinta, const QVariantMap &sanoma, bool pdf,
                        const std::function<void (const Vero::Vastaus &)> &valmis, bool vartioi)
{
    const QString valtuutus = rajapinta == Vero::GETTOKEN ? QString() : token_;
    std::function<void(const Vero::Vastaus&)> kasittelija = valmis;
    if (vartioi)
        kasittelija = vartioitu(valmis);
    asiakas_->kutsu(Vero::osoite(asetukset_.ymparisto, rajapinta), sanoma, valtuutus, pdf, kasittelija);
}

void VeroPalvelu::etsi(const QString &viite, const std::function<void (const Vero::LoydettyTosite &)> &valmis)
{
    VeroTosite::etsi(this, viite, vartioitu(valmis));
}

void VeroPalvelu::haeToken(const TokenValmis &valmis)
{
    const QString ytunnus = asiakkaanYtunnus();
    kutsu(Vero::GETTOKEN,
          QVariantMap{{"DelegateIdentifier", delegaatti_},
                      {"PrincipalIdentifiers", QVariantList{ytunnus}}},
          false,
          [this, ytunnus, valmis](const Vero::Vastaus& vastaus) {
        if (!vastaus.ok()) {
            valmis(QString(), tr("Valtuutuksen haku epäonnistui: %1")
                   .arg(Vero::virheteksti(vastaus.http, vastaus.json(), vastaus.verkkovirhe)));
            return;
        }
        const QString token = Vero::tokenit(vastaus.json()).value(ytunnus);
        if (token.isEmpty()) {
            valmis(QString(), tr("Tilitoimistolla %1 ei ole valtuutusta asiakkaalle %2")
                   .arg(delegaatti_, ytunnus));
            return;
        }
        valmis(token, QString());
    });
}

void VeroPalvelu::hae(int vuosi)
{
    if (kaynnissa_)
        return;
    aloita();

    const QString ytunnus = asiakkaanYtunnus();
    if (ytunnus.isEmpty()) {
        virheella(tr("Kirjanpidon Y-tunnus puuttuu tai on virheellinen"));
        lopeta(false);
        return;
    }

    lisaaTehtava([this, ytunnus, vuosi](Seuraava seuraava) {
        emit loki(tr("Haetaan valtuutus asiakkaalle %1 (%2)").arg(ytunnus, Vero::ymparistoNimi(asetukset_.ymparisto)));
        haeToken([this, ytunnus, vuosi, seuraava](const QString& token, const QString& virhe) {
            if (token.isEmpty()) {
                virheella(virhe);
                jono_.clear();      // Ilman valtuutusta ei voi hakea mitään
                seuraava();
                return;
            }
            token_ = token;

            lisaaTehtava([this, ytunnus](Seuraava s) {
                auto haku = QSharedPointer<KirjeHaku>::create();
                haku->ytunnus = ytunnus;
                haku->alku = kp()->tilikaudet()->kirjanpitoAlkaa();
                haku->loppu = QDate::currentDate();
                haeKirjeSivu(haku, s);
            });
            lisaaTehtava([this, ytunnus, vuosi](Seuraava s) {
                haeKaudet(ytunnus, vuosi, s);
            });
            seuraava();
        });
    });
    ajaSeuraava();
}

void VeroPalvelu::haeKirjeSivu(const QSharedPointer<KirjeHaku> &haku, const Seuraava &seuraava)
{
    // ListLetters palauttaa enintään 100 kirjettä, joten pitkä väli haetaan sivuina
    // siirtämällä loppupäivää vanhimman saadun kirjeen päivään.
    static const int SIVUKOKO = 100;
    static const int ENIMMAISSIVUT = 50;

    emit loki(tr("Haetaan kirjeet väliltä %1 – %2")
              .arg(haku->alku.toString("dd.MM.yyyy"), haku->loppu.toString("dd.MM.yyyy")));

    kutsu(Vero::LIST_LETTERS,
          QVariantMap{{"BusinessId", haku->ytunnus},
                      {"FromDate", haku->alku.toString(Qt::ISODate)},
                      {"ToDate", haku->loppu.toString(Qt::ISODate)},
                      {"NumberOfLetters", SIVUKOKO},
                      {"LetterTypes", QVariantList()}},
          false,
          [this, haku, seuraava](const Vero::Vastaus& vastaus) {
        if (!vastaus.ok()) {
            virheella(tr("Kirjeluettelon haku epäonnistui: %1")
                      .arg(Vero::virheteksti(vastaus.http, vastaus.json(), vastaus.verkkovirhe)));
            seuraava();
            return;
        }

        const QList<Vero::Kirje> sivu = Vero::kirjeet(vastaus.json());
        int uusia = 0;
        for (const Vero::Kirje& kirje : sivu) {
            if (!haku->nahdyt.contains(kirje.numero)) {
                haku->nahdyt.insert(kirje.numero);
                haku->kirjeet.append(kirje);
                uusia++;
            }
        }

        const QDate vanhin = Vero::vanhinPaivays(sivu);
        if (sivu.count() >= SIVUKOKO) {
            if (uusia && vanhin.isValid() && vanhin >= haku->alku && haku->sivuja < ENIMMAISSIVUT) {
                haku->loppu = vanhin;
                haku->sivuja++;
                haeKirjeSivu(haku, seuraava);
                return;
            }
            virheella(tr("Kirjeluettelon sivutus pysähtyi, kaikkia kirjeitä ei ehkä saatu haettua"));
        }

        emit loki(tr("Verohallinnolla on %1 kirjettä").arg(haku->kirjeet.count()));
        for (const Vero::Kirje& kirje : std::as_const(haku->kirjeet)) {
            const QString ytunnus = haku->ytunnus;
            lisaaTehtava([this, ytunnus, kirje](Seuraava s) {
                tallennaKirje(ytunnus, kirje, s);
            });
        }
        seuraava();
    });
}

void VeroPalvelu::tallennaKirje(const QString &ytunnus, const Vero::Kirje &kirje, const Seuraava &seuraava)
{
    const QString viite = Vero::kirjeViite(asetukset_.ymparisto, kirje.numero);

    etsi(viite, [this, ytunnus, kirje, viite, seuraava](const Vero::LoydettyTosite& loydetty) {
        if (loydetty.virhe) {   // Tuplatallennuksen välttämiseksi ei jatketa, jos tilaa ei tiedetä
            virheella(tr("Kirjeen %1 olemassaoloa ei voitu tarkistaa kirjanpidosta").arg(kirje.numero));
            seuraava();
            return;
        }
        // Valmis kirje ohitetaan. Puuttuva liite haetaan uudelleen samalle tositteelle:
        // pelkkä olemassaolo ei riitä, koska liitteen tallennus on voinut epäonnistua.
        if (loydetty.id && !liitteetPuutteelliset(loydetty)) {
            seuraava();
            return;
        }

        const auto haeJaTallenna = [this, ytunnus, kirje, viite, seuraava](int aiempiId, const QString& varaus) {
            const quint64 sukupolvi = sukupolvi_;
            const auto vapauta = [this, sukupolvi, viite, varaus] {
                vapautaVaraus(sukupolvi, viite, varaus);
            };
            kutsu(Vero::GET_LETTER,
                  QVariantMap{{"BusinessId", ytunnus},
                              {"LetterNumber", kirje.numero},
                              {"SetReadInOmaVero", false}},
                  true,
                  [this, kirje, viite, aiempiId, vapauta, seuraava](const Vero::Vastaus& vastaus) {
                if (!vastaus.ok() || !vastaus.runko.startsWith("%PDF")) {
                    vapauta();
                    virheella(tr("Kirjeen %1 haku epäonnistui: %2").arg(kirje.numero,
                              Vero::virheteksti(vastaus.http, vastaus.json(), vastaus.verkkovirhe)));
                    seuraava();
                    return;
                }

                const QString nimi = kirje.nimiFi.isEmpty() ? kirje.nimiEn : kirje.nimiFi;
                Vero::TositeTiedot tiedot;
                tiedot.viite = viite;
                tiedot.pvm = kirje.paivays;
                tiedot.otsikko = tr("Verohallinnon kirje: %1").arg(nimi.isEmpty() ? kirje.numero : nimi) + ymparistoLoppu(asetukset_.ymparisto);
                tiedot.vero = QVariantMap{
                    {"suunta", "saapunut"},
                    {"ymparisto", Vero::ymparistoAvain(asetukset_.ymparisto)},
                    {"tyyppi", "kirje"},
                    {"kirjenumero", kirje.numero},
                    {"kirjetyyppi", kirje.tyyppi},
                    {"nimi", nimi},
                    {"paivays", kirje.paivays.toString(Qt::ISODate)},
                    {"haettu", QDateTime::currentDateTime().toString(Qt::ISODate)}};
                tiedot.liitteet.append({QString("%1.pdf").arg(viite), vastaus.runko});

                tallennaTosite(aiempiId, tiedot, [this, kirje, aiempiId, vapauta, seuraava](bool ok) {
                    vapauta();
                    if (ok)
                        emit loki(aiempiId
                                  ? tr("Täydennetty kirjeen %1 puuttuva liite").arg(kirje.numero)
                                  : tr("Tallennettu kirje %1").arg(kirje.numero));
                    seuraava();
                });
            });
        };

        // Varaus myös täydennykselle: kaksi yhteyttä ei liitä samaa PDF:ää,
        // eikä täydennys osu kesken olevaan ensitalletukseen. Liitelista luetaan
        // uudelleen lukon jälkeen.
        varaaLahetys(viite, [this, kirje, viite, haeJaTallenna, seuraava](const QString& tunnus) {
            if (tunnus.isEmpty()) {
                emit loki(tr("Kirjettä %1 tallennetaan toisesta yhteydestä").arg(kirje.numero));
                seuraava();
                return;
            }
            const quint64 sukupolvi = sukupolvi_;
            const auto vapauta = [this, sukupolvi, viite, tunnus] {
                vapautaVaraus(sukupolvi, viite, tunnus);
            };
            etsi(viite, [this, kirje, viite, tunnus, haeJaTallenna, seuraava, vapauta](const Vero::LoydettyTosite& tuore) {
                if (tuore.virhe) {
                    vapauta();
                    virheella(tr("Kirjeen %1 olemassaoloa ei voitu tarkistaa kirjanpidosta").arg(kirje.numero));
                    seuraava();
                    return;
                }
                if (tuore.id && !liitteetPuutteelliset(tuore)) {
                    vapauta();
                    seuraava();
                    return;
                }
                // Liite on tallella, mutta tosite jäi kesken-merkinnälle. Uutta hakua ei tarvita.
                if (tuore.id && tuore.liitteet.contains(viite + QStringLiteral(".pdf"))) {
                    Vero::TositeTiedot tiedot;
                    tiedot.vero = tuore.vero;
                    tiedot.vero.remove(QStringLiteral("kesken"));
                    tallennaTosite(tuore.id, tiedot, [vapauta, seuraava](bool) {
                        vapauta();
                        seuraava();
                    });
                    return;
                }
                haeJaTallenna(tuore.id, tunnus);
            });
        });
    });
}

void VeroPalvelu::haeKaudet(const QString &ytunnus, int vuosi, const Seuraava &seuraava)
{
    emit loki(tr("Haetaan alv-kaudet vuodelle %1").arg(vuosi));

    kutsu(Vero::GET_VAT_PERIODS,
          QVariantMap{{"BusinessId", ytunnus}, {"FilingYear", vuosi}},
          false,
          [this, ytunnus, seuraava](const Vero::Vastaus& vastaus) {
        if (!vastaus.ok()) {
            virheella(tr("Alv-kausien haku epäonnistui: %1")
                      .arg(Vero::virheteksti(vastaus.http, vastaus.json(), vastaus.verkkovirhe)));
            seuraava();
            return;
        }

        const QList<Vero::Verokausi> kaudet = Vero::verokaudet(vastaus.json());
        emit kaudetHaettu(kaudet);

        for (const Vero::Verokausi& kausi : kaudet) {
            // Vain Verohallinnon käsittelemät ilmoitukset: vastaanotettu ilmoitus ei vielä
            // ole käsitelty (FileVATReturn on eräajo)
            if (kausi.kasitelty()) {
                lisaaTehtava([this, ytunnus, kausi](Seuraava s) {
                    kasitteleKausi(ytunnus, kausi, s);
                });
            }
        }
        seuraava();
    });
}

void VeroPalvelu::kasitteleKausi(const QString &ytunnus, const Vero::Verokausi &kausi, const Seuraava &seuraava)
{
    const Vero::Ymparisto ymparisto = asetukset_.ymparisto;
    const QString saapunutViite = Vero::saapunutAlvViite(ymparisto, kausi.loppuu);
    const QString lahetettyViite = Vero::lahetettyAlvViite(ymparisto, kausi.loppuu);
    const QString kausiTeksti = kausi.loppuu.toString("dd.MM.yyyy");

    // 2. Lähetetty tosite päivitetään vain lähetyksen varauksen takana. Muuten toinen
    // yhteys ehtii tallentaa uuden yrityksen, ja tämä kirjoitus korvaisi sen vanhalla kuvalla.
    // Jonossa oleva, keskeytynyt tai tulokseltaan tuntematon yritys merkitään käsitellyksi
    // vain, kun haettu ilmoitus vastaa juuri sitä sanomaa eikä varausta pidä toinen lähetys.
    const auto merkitseLahetetty = [this, lahetettyViite, kausiTeksti, seuraava](const QVariantMap& haettu) {
        varaaLahetys(lahetettyViite, [this, lahetettyViite, kausiTeksti, seuraava, haettu](const QString& tunnus) {
            if (tunnus.isEmpty()) {
                emit loki(tr("Kauden %1 lähetystä käsitellään toisesta yhteydestä, joten tilaa ei muutettu").arg(kausiTeksti));
                seuraava();
                return;
            }
            const quint64 sukupolvi = sukupolvi_;
            const auto vapauta = [this, sukupolvi, lahetettyViite, tunnus] {
                vapautaVaraus(sukupolvi, lahetettyViite, tunnus);
            };
            const auto valmis = [vapauta, seuraava] {
                vapauta();
                seuraava();
            };
            etsi(lahetettyViite, [this, kausiTeksti, haettu, valmis](const Vero::LoydettyTosite& lahetetty) {
            if (lahetetty.virhe) {
                virheella(tr("Kauden %1 lähetetyn ilmoituksen tilaa ei voitu päivittää").arg(kausiTeksti));
                valmis();
                return;
            }
            if (!lahetetty.id) {
                valmis();
                return;
            }
            const QString tila = lahetetty.vero.value(QStringLiteral("tila")).toString();
            const bool vastaa = Vero::samaAlvIlmoitus(lahetetty.vero.value(QStringLiteral("sanoma")).toMap(), haettu);
            const bool odottaa = tila == TILA_KASITTELYSSA || tila == TILA_TUNTEMATON || tila == TILA_LAHETTAMASSA;
            const bool merkitse = odottaa && vastaa;
            const bool puute = liitteetPuutteelliset(lahetetty);
            if (odottaa && !haettu.isEmpty() && !vastaa)
                emit loki(tr("Kauden %1 käsiteltyä ilmoitusta ei voitu yhdistää tähän lähetykseen, joten tilaa ei muutettu").arg(kausiTeksti));
            if (!merkitse && !puute) {
                valmis();
                return;
            }
            Vero::TositeTiedot tiedot;
            tiedot.vero = lahetetty.vero;
            if (merkitse) {
                tiedot.vero.insert(QStringLiteral("tila"), TILA_KASITELTY);
                tiedot.vero.insert(QStringLiteral("kasitelty"), QDateTime::currentDateTime().toString(Qt::ISODate));
            }
            // Puuttuvat liitteet kootaan tositteelle jo tallennetusta sanomasta, koska
            // lähetettyä ilmoitusta ei haeta Verohallinnolta uudelleen
            if (puute) {
                QStringList nimet = lahetetty.liitteet;
                const QString kausi = lahetetty.vero.value(QStringLiteral("kausipaattyy")).toString();
                const auto lisaa = [&](const QString& avain, const QString& nimi) {
                    if (!lahetetty.vero.contains(avain) || nimet.contains(nimi))
                        return;
                    tiedot.liitteet.append({nimi, kauniisti(lahetetty.vero.value(avain).toMap())});
                    nimet << nimi;
                };
                lisaa(QStringLiteral("sanoma"), QStringLiteral("FileVATReturn-%1.json").arg(kausi));
                lisaa(QStringLiteral("vastaus"), QStringLiteral("FileVATReturn-vastaus-%1.json").arg(kausi));
                // nimet sisältää tallella olevat ja nyt lisättävät. Aiemmin kadonneita
                // nimiä ei jätetä odotuslistaan, jos niitä ei voi enää tuottaa.
                tiedot.vero.insert(QStringLiteral("odotetutLiitteet"), nimet);
                tiedot.vero.remove(QStringLiteral("kesken"));
            }
            tallennaTosite(lahetetty.id, tiedot, [this, kausiTeksti, merkitse, valmis](bool ok) {
                if (ok && merkitse)
                    emit loki(tr("Verohallinto on käsitellyt kauden %1 alv-ilmoituksen").arg(kausiTeksti));
                valmis();
            });
            });
        }, false);
    };

    // 1. Verohallinnon tallentama ilmoitus haetaan aina: se on voinut muuttua sen jälkeen,
    // kun se viimeksi haettiin (korvaava ilmoitus tehty OmaVerossa tai toisella ohjelmalla).
    const auto haeIlmoitus = [this, ytunnus, kausi, kausiTeksti, saapunutViite, ymparisto, merkitseLahetetty, seuraava]
            (const Vero::LoydettyTosite& loydetty, const QString& varaus) {
        const quint64 sukupolvi = sukupolvi_;
        const auto vapauta = [this, sukupolvi, saapunutViite, varaus] {
            vapautaVaraus(sukupolvi, saapunutViite, varaus);
        };

        kutsu(Vero::GET_FILED_VAT_RETURN,
              QVariantMap{{"BusinessId", ytunnus},
                          {"FilingPeriod", kausi.loppuu.toString(Qt::ISODate)}},
              false,
              [this, kausi, kausiTeksti, saapunutViite, ymparisto, loydetty, vapauta, merkitseLahetetty, seuraava](const Vero::Vastaus& vastaus) {
            const QVariantMap uusiVastaus = vastaus.json();
            if (!vastaus.ok() || uusiVastaus.isEmpty()) {
                vapauta();
                virheella(tr("Kauden %1 ilmoituksen haku epäonnistui: %2").arg(kausiTeksti,
                          Vero::virheteksti(vastaus.http, uusiVastaus, vastaus.verkkovirhe)));
                seuraava();
                return;
            }

            const auto vertailtava = [](const QVariantMap& map) {
                return QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact);
            };
            const QDateTime nyt = QDateTime::currentDateTime();

            const bool samaVastaus = loydetty.id
                    && vertailtava(loydetty.vero.value(QStringLiteral("vastaus")).toMap()) == vertailtava(uusiVastaus);
            // Sama sisältö päivitetään silti, jos edellinen liite jäi puuttumaan
            if (samaVastaus && !liitteetPuutteelliset(loydetty)) {
                vapauta();
                merkitseLahetetty(uusiVastaus);
                return;
            }

            const QString liitteenNimi = QStringLiteral("GetFiledVATReturn-%1-%2.json")
                    .arg(kausi.loppuu.toString(Qt::ISODate), nyt.toString(QStringLiteral("yyyyMMddhhmmss")));

            Vero::TositeTiedot tiedot;
            if (loydetty.id && samaVastaus) {
                // Sisältö ei muuttunut, mutta liite puuttuu: täydennetään sama tosite
                tiedot.vero = loydetty.vero;
                tiedot.vero.remove(QStringLiteral("kesken"));
                QStringList nimet = loydetty.liitteet;
                nimet << liitteenNimi;
                tiedot.vero.insert(QStringLiteral("odotetutLiitteet"), nimet);
            } else if (loydetty.id) {
                // Ilmoitus on muuttunut: aiempi vastaus jää tositteen historiaan
                tiedot.vero = loydetty.vero;
                QVariantList aiemmat = loydetty.vero.value("aiemmat").toList();
                aiemmat.append(QVariantMap{{"haettu", loydetty.vero.value("haettu")},
                                           {"vastaus", loydetty.vero.value("vastaus")}});
                tiedot.vero.insert("aiemmat", aiemmat);
                tiedot.vero.insert("haettu", nyt.toString(Qt::ISODate));
                tiedot.vero.insert("vastaus", uusiVastaus);
            } else {
                tiedot.viite = saapunutViite;
                tiedot.pvm = QDate::currentDate();
                tiedot.otsikko = tr("Verohallinnon käsittelemä alv-ilmoitus %1").arg(kausiTeksti) + ymparistoLoppu(ymparisto);
                tiedot.vero = QVariantMap{
                    {"suunta", "saapunut"},
                    {"ymparisto", Vero::ymparistoAvain(ymparisto)},
                    {"tyyppi", "alv"},
                    {"kausialkaa", kausi.alkaa.toString(Qt::ISODate)},
                    {"kausipaattyy", kausi.loppuu.toString(Qt::ISODate)},
                    {"tila", TILA_KASITELTY},
                    {"haettu", nyt.toString(Qt::ISODate)},
                    {"vastaus", uusiVastaus}};
            }
            tiedot.liitteet.append({liitteenNimi, vastaus.runko});

            const bool paivitys = loydetty.id != 0;
            tallennaTosite(loydetty.id, tiedot, [this, kausiTeksti, paivitys, vapauta, merkitseLahetetty, uusiVastaus](bool ok) {
                vapauta();
                if (ok)
                    emit loki(paivitys
                              ? tr("Päivitetty Verohallinnon tieto kauden %1 alv-ilmoituksesta").arg(kausiTeksti)
                              : tr("Tallennettu Verohallinnon tieto kauden %1 alv-ilmoituksesta").arg(kausiTeksti));
                merkitseLahetetty(uusiVastaus);
            });
        });
    };

    etsi(saapunutViite, [this, kausiTeksti, saapunutViite, seuraava, haeIlmoitus, merkitseLahetetty]
         (const Vero::LoydettyTosite& loydetty) {
        if (loydetty.virhe) {
            virheella(tr("Kauden %1 tositetta ei voitu tarkistaa kirjanpidosta").arg(kausiTeksti));
            seuraava();
            return;
        }
        // Olemassa oleva tosite päivitetään varauksen takana ja luetaan uudelleen
        // lukon jälkeen. Muuten hidas vastaus kirjoittaa vanhan kuvan uudemman päälle.
        // Uusi luodaan vain varauksen kanssa, jotta toinen yhteys ei tee samaa viitettä.
        if (loydetty.id) {
            varaaLahetys(saapunutViite, [this, kausiTeksti, saapunutViite, haeIlmoitus, merkitseLahetetty, seuraava]
                         (const QString& tunnus) {
                if (tunnus.isEmpty()) {
                    emit loki(tr("Kauden %1 ilmoitusta päivitetään toisesta yhteydestä").arg(kausiTeksti));
                    merkitseLahetetty(QVariantMap());
                    return;
                }
                const quint64 sukupolvi = sukupolvi_;
                etsi(saapunutViite, [this, kausiTeksti, saapunutViite, tunnus, sukupolvi, haeIlmoitus, seuraava]
                     (const Vero::LoydettyTosite& tuore) {
                    if (tuore.virhe) {
                        vapautaVaraus(sukupolvi, saapunutViite, tunnus);
                        virheella(tr("Kauden %1 tositetta ei voitu tarkistaa kirjanpidosta").arg(kausiTeksti));
                        seuraava();
                        return;
                    }
                    haeIlmoitus(tuore, tunnus);
                });
            }, false);
            return;
        }
        varaaUusi(saapunutViite, [this, kausiTeksti, haeIlmoitus, merkitseLahetetty, seuraava](const UudenTositteenVaraus& varaus) {
            if (varaus.loydetty.virhe) {
                virheella(tr("Kauden %1 tositetta ei voitu tarkistaa kirjanpidosta").arg(kausiTeksti));
                seuraava();
                return;
            }
            if (varaus.estetty) {
                emit loki(tr("Kauden %1 ilmoitusta tallennetaan toisesta yhteydestä").arg(kausiTeksti));
                merkitseLahetetty(QVariantMap());
                return;
            }
            haeIlmoitus(varaus.loydetty, varaus.tunnus);
        });
    });
}

void VeroPalvelu::varaaLahetys(const QString &viite, const std::function<void (const QString &)> &valmis, bool korvaa)
{
    const QString tunnus = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const quint64 sukupolvi = sukupolvi_;
    KpKysely* kysely = kpk(QStringLiteral("/asetukset/verovaraus"), KpKysely::POST);
    if (!kysely) {
        valmis(QString());
        return;
    }
    connect(kysely, &KpKysely::virhe, this, [this, sukupolvi, valmis] {
        if (sukupolvi == sukupolvi_)
            valmis(QString());
    });
    connect(kysely, &KpKysely::vastaus, this, [this, sukupolvi, valmis, tunnus](QVariant* data) {
        if (sukupolvi != sukupolvi_)
            return;
        valmis(data->toMap().value(QStringLiteral("varattu")).toBool() ? tunnus : QString());
    });
    kysely->kysy(QVariantMap{{QStringLiteral("viite"), viite},
                             {QStringLiteral("tunnus"), tunnus},
                             {QStringLiteral("korvaa"), korvaa}});
}

void VeroPalvelu::varaaUusi(const QString &viite, const std::function<void (const UudenTositteenVaraus &)> &valmis)
{
    // Sama varaus kuin lähetyksessä: viite on pääavain, joten kahdesta yhtäaikaisesta
    // hausta vain toinen luo tositteen. Toinen jatkaa sillä tositteella, jos se ehti syntyä.
    varaaLahetys(viite, [this, viite, valmis](const QString& tunnus) {
        const quint64 sukupolvi = sukupolvi_;
        etsi(viite, [this, sukupolvi, viite, tunnus, valmis](const Vero::LoydettyTosite& loydetty) {
            UudenTositteenVaraus tulos;
            tulos.loydetty = loydetty;
            if (loydetty.virhe || loydetty.id) {
                // Varausta ei tarvita: haku epäonnistui tai tosite on jo olemassa
                if (!tunnus.isEmpty())
                    vapautaVaraus(sukupolvi, viite, tunnus);
            } else if (tunnus.isEmpty()) {
                tulos.estetty = true;   // Toinen yhteys pitää varausta, tositetta ei vielä näy
            } else {
                tulos.tunnus = tunnus;  // Tämä yhteys luo tositteen
            }
            valmis(tulos);
        });
    });
}

void VeroPalvelu::vahvistaVaraus(const QString &viite, const QString &tunnus, const std::function<void (bool)> &valmis)
{
    const quint64 sukupolvi = sukupolvi_;
    // Omistus kysytään tunnuksella; vastaus ei palauta rivin tunnusta (ei voi varastaa GET:llä).
    KpKysely* kysely = kpk(QStringLiteral("/asetukset/verovaraus/%1").arg(viite));
    if (!kysely) {
        valmis(false);
        return;
    }
    kysely->lisaaAttribuutti(QStringLiteral("tunnus"), tunnus);
    connect(kysely, &KpKysely::virhe, this, [this, sukupolvi, valmis] {
        if (sukupolvi == sukupolvi_)
            valmis(false);
    });
    connect(kysely, &KpKysely::vastaus, this, [this, sukupolvi, valmis](QVariant* data) {
        if (sukupolvi != sukupolvi_)
            return;
        valmis(data->toMap().value(QStringLiteral("oma")).toBool());
    });
    kysely->kysy();
}

void VeroPalvelu::uudistaVaraus(const QString &viite, const QString &tunnus, const std::function<void (bool)> &valmis)
{
    const quint64 sukupolvi = sukupolvi_;
    KpKysely* kysely = kpk(QStringLiteral("/asetukset/verovaraus"), KpKysely::POST);
    if (!kysely) {
        valmis(false);
        return;
    }
    connect(kysely, &KpKysely::virhe, this, [this, sukupolvi, valmis] {
        if (sukupolvi == sukupolvi_)
            valmis(false);
    });
    connect(kysely, &KpKysely::vastaus, this, [this, sukupolvi, valmis](QVariant* data) {
        if (sukupolvi != sukupolvi_)
            return;
        valmis(data->toMap().value(QStringLiteral("varattu")).toBool());
    });
    kysely->kysy(QVariantMap{{QStringLiteral("viite"), viite},
                             {QStringLiteral("tunnus"), tunnus},
                             {QStringLiteral("korvaa"), true}});
}

void VeroPalvelu::vapautaVaraus(quint64 sukupolvi, const QString &viite, const QString &tunnus)
{
    // Vapautus osuu siihen kirjaan, jossa varaus tehtiin. Kirjan vaihduttua kpk()
    // osoittaisi jo toiseen kirjaan, joten varausta ei kosketa.
    if (sukupolvi != sukupolvi_ || tunnus.isEmpty())
        return;
    KpKysely* kysely = kpk(QStringLiteral("/asetukset/verovaraus/%1/%2").arg(viite, tunnus), KpKysely::DELETE);
    if (kysely)
        kysely->kysy();
}

void VeroPalvelu::lahetaAlv(int alvTositeId, const QDate &loppuu, bool korvaava, const QString &korvauksenSyy,
                           Vero::LahetysTila vahvistettuTila, const QString &vahvistus)
{
    if (kaynnissa_)
        return;
    aloita();

    const QString ytunnus = asiakkaanYtunnus();
    if (ytunnus.isEmpty()) {
        virheella(tr("Kirjanpidon Y-tunnus puuttuu tai on virheellinen"));
        lopeta(false);
        return;
    }
    const quint64 sukupolvi = sukupolvi_;

    // 1. Ilmoitettavat luvut kirjanpidon alv-laskelmasta
    lisaaTehtava([this, sukupolvi, ytunnus, alvTositeId, loppuu, korvaava, korvauksenSyy, vahvistettuTila, vahvistus](Seuraava seuraava) {
        KpKysely* kysely = kpk(QString("/tositteet/%1").arg(alvTositeId));
        if (!kysely) {
            virheella(tr("Alv-laskelmaa ei voi lukea"));
            jono_.clear();
            seuraava();
            return;
        }
        connect(kysely, &KpKysely::virhe, this, [this, sukupolvi, seuraava] {
            if (sukupolvi != sukupolvi_)
                return;
            virheella(tr("Alv-laskelman lataus epäonnistui"));
            jono_.clear();
            seuraava();
        });
        connect(kysely, &KpKysely::vastaus, this,
                [this, sukupolvi, ytunnus, alvTositeId, loppuu, korvaava, korvauksenSyy, seuraava, vahvistettuTila, vahvistus](QVariant* data) {
            if (sukupolvi != sukupolvi_)
                return;
            const QVariantMap tosite = data->toMap();
            const QVariantMap alv = tosite.value("alv").toMap();
            if (tosite.value("tyyppi").toInt() != TositeTyyppi::ALVLASKELMA ||
                alv.value("kausipaattyy").toDate() != loppuu) {
                virheella(tr("Tosite ei ole kauden %1 alv-laskelma").arg(loppuu.toString("dd.MM.yyyy")));
                jono_.clear();
                seuraava();
                return;
            }

            // Alarajahuojennusta ei osata ilmoittaa: ilmoitus ei saa lähteä ilman sitä
            const QStringList tukemattomat = Vero::tukemattomatKoodit(alv.value("koodit").toMap());
            if (!tukemattomat.isEmpty()) {
                virheella(tr("Alv-laskelmassa on alarajahuojennusta (koodit %1), jota ei voi ilmoittaa "
                             "tällä toiminnolla. Ilmoitus on tehtävä OmaVerossa.").arg(tukemattomat.join(", ")));
                jono_.clear();
                seuraava();
                return;
            }

            const QVariantMap sanoma = Vero::fileVatReturn(
                        ytunnus, loppuu, alv.value("koodit").toMap(),
                        kp()->asetukset()->asetus(AsetusModel::VeroYhteysHenkilo),
                        kp()->asetukset()->asetus(AsetusModel::VeroYhteysPuhelin),
                        korvaava, korvauksenSyy,
                        kp()->onkoMaksuperusteinenAlv(loppuu));

            const QStringList virheet = Vero::validoiFileVatReturn(sanoma);
            if (!virheet.isEmpty()) {
                for (const QString& virhe : virheet)
                    virheella(tr("Ilmoitus ei kelpaa: %1").arg(virhe));
                jono_.clear();
                seuraava();
                return;
            }

            // 2. Lähetysvaraus ennen tilan lukua ja POST-kutsua. Varaus on tietokannan
            // pääavain, joten toinen saman kirjan yhteys ei pääse lähettämään samaa kautta
            // samaan aikaan. Tila luetaan vasta lukon jälkeen: lukkoa edeltävä luku ehtisi
            // vanheta, jos toinen yhteys lähettää ja vapauttaa lukon sillä välin.
            // Yritys tallennetaan tositteelle ennen lähetystä: jos vastaus
            // jää tulematta, tila on "tarkistettava" eikä "ei lähetetty".
            const QString viite = Vero::lahetettyAlvViite(asetukset_.ymparisto, loppuu);
            varaaLahetys(viite, [this, ytunnus, loppuu, korvaava, korvauksenSyy, sanoma, alvTositeId, alv, viite, seuraava, vahvistettuTila, vahvistus]
                         (const QString& tunnus) {
                if (tunnus.isEmpty()) {
                    virheella(tr("Tämän kauden ilmoitusta lähetetään juuri toisesta yhteydessä, ilmoitusta ei lähetetty"));
                    jono_.clear();
                    seuraava();
                    return;
                }
                const quint64 varausSukupolvi = sukupolvi_;
                const auto vapauta = [this, varausSukupolvi, viite, tunnus] {
                    vapautaVaraus(varausSukupolvi, viite, tunnus);
                };

                lahetyksenTila(this, asetukset_.ymparisto, loppuu,
                               [this, ytunnus, loppuu, korvaava, korvauksenSyy, sanoma, alvTositeId, alv, viite, tunnus, seuraava, vahvistettuTila, vahvistus, vapauta]
                               (const Vero::LahetysTietoa& nytTila) {
                    if (nytTila.virhe) {
                        vapauta();
                        virheella(tr("Aiemman lähetyksen tietoja ei voitu lukea kirjanpidosta, ilmoitusta ei lähetetty"));
                        jono_.clear();
                        seuraava();
                        return;
                    }
                    if (nytTila.tila != vahvistettuTila || nytTila.vahvistus != vahvistus) {
                        vapauta();
                        virheella(tr("Kauden tila muuttui vahvistuksen jälkeen. Tarkista tila ja lähetä uudelleen."));
                        jono_.clear();
                        seuraava();
                        return;
                    }
                    // Vain tällä ohjelmalla lähetetty tosite on aiempi yritys. Pelkkä
                    // Verohallinnolta haettu ilmoitus ei ole lähetystosite.
                    Vero::LoydettyTosite aiempi;
                    aiempi.id = nytTila.lahetettyId;
                    if (aiempi.id)
                        aiempi.vero = nytTila.vero;

                    const QDateTime nyt = QDateTime::currentDateTime();
                    QVariantList lahetykset = aiempi.vero.value(QStringLiteral("lahetykset")).toList();
                    lahetykset.append(QVariantMap{{QStringLiteral("aika"), nyt.toString(Qt::ISODate)},
                                                 {QStringLiteral("korvaava"), korvaava},
                                                 {QStringLiteral("korvauksenSyy"), korvauksenSyy},
                                                 {QStringLiteral("http"), 0},
                                                 {QStringLiteral("tulos"), TILA_LAHETTAMASSA}});

                    Vero::TositeTiedot varaus;
                    varaus.viite = viite;
                    varaus.pvm = QDate::currentDate();
                    varaus.otsikko = tr("Alv-ilmoitus Verohallintoon %1 – %2")
                            .arg(alv.value(QStringLiteral("kausialkaa")).toDate().toString(QStringLiteral("dd.MM.yyyy")),
                                 loppuu.toString(QStringLiteral("dd.MM.yyyy")))
                            + ymparistoLoppu(asetukset_.ymparisto);
                    varaus.vero = QVariantMap{
                        {QStringLiteral("suunta"), QStringLiteral("lahetetty")},
                        {QStringLiteral("ymparisto"), Vero::ymparistoAvain(asetukset_.ymparisto)},
                        {QStringLiteral("tyyppi"), QStringLiteral("alv")},
                        {QStringLiteral("kausialkaa"), alv.value(QStringLiteral("kausialkaa")).toDate().toString(Qt::ISODate)},
                        {QStringLiteral("kausipaattyy"), loppuu.toString(Qt::ISODate)},
                        {QStringLiteral("alvtosite"), alvTositeId},
                        {QStringLiteral("tila"), TILA_LAHETTAMASSA},
                        {QStringLiteral("yritetty"), nyt.toString(Qt::ISODate)},
                        {QStringLiteral("korvaava"), korvaava},
                        {QStringLiteral("korvauksenSyy"), korvauksenSyy},
                        {QStringLiteral("lahetykset"), lahetykset},
                        {QStringLiteral("sanoma"), sanoma}};

                    VeroTosite::tallenna(this, aiempi.id, varaus, vartioitu(
                        [this, vapauta, ytunnus, loppuu, korvaava, korvauksenSyy, sanoma, alvTositeId, alv, viite, tunnus, varaus, aiempi, seuraava]
                        (int varausId, const QString& virhe) {
                        if (!varausId) {
                            vapauta();
                            virheella(virhe.isEmpty()
                                      ? tr("Lähetysvarausta ei voitu tallentaa, ilmoitusta ei lähetetty")
                                      : virhe);
                            seuraava();
                            return;
                        }

                        vahvistaVaraus(viite, tunnus, [this, vapauta, ytunnus, loppuu, korvaava, korvauksenSyy, sanoma,
                                                       alvTositeId, alv, varausId, varaus, aiempi, seuraava, viite, tunnus](bool oma) {
                            if (!oma) {
                                vapauta();
                                virheella(tr("Lähetysvaraus ehti siirtyä toiselle yhteydelle, ilmoitusta ei lähetetty"));
                                seuraava();
                                return;
                            }

                            Vero::LoydettyTosite pohja;
                            pohja.id = varausId;
                            pohja.vero = varaus.vero;

                            haeToken([this, vapauta, loppuu, korvaava, korvauksenSyy, sanoma, alvTositeId, alv, pohja, aiempi, seuraava, viite, tunnus]
                                     (const QString& token, const QString& virhe) {
                                if (token.isEmpty()) {
                                    // POST ei lähtenyt. Kirjoitetaan vain, jos varaus on yhä meillä.
                                    vahvistaVaraus(viite, tunnus, [this, vapauta, seuraava, virhe, aiempi, pohja, sanoma, korvaava, korvauksenSyy](bool oma) {
                                        if (!oma) {
                                            virheella(virhe);
                                            seuraava();
                                            return;
                                        }
                                        // Vastaanotettua, jonossa olevaa tai tulokseltaan tuntematonta
                                        // ilmoitusta ei korvata tällä yrityksellä. Sanoma jää historiariviin.
                                        const QVariantMap tapahtuma{
                                            {QStringLiteral("aika"), QDateTime::currentDateTime().toString(Qt::ISODate)},
                                            {QStringLiteral("korvaava"), korvaava},
                                            {QStringLiteral("korvauksenSyy"), korvauksenSyy},
                                            {QStringLiteral("http"), 0},
                                            {QStringLiteral("tulos"), QStringLiteral("peruttu")},
                                            {QStringLiteral("sanoma"), sanoma}};
                                        Vero::TositeTiedot peru;
                                        peru.vero = Vero::epaonnistuneenLahetyksenVero(
                                                    aiempi.id ? aiempi.vero : QVariantMap(), pohja.vero, tapahtuma);
                                        virheella(virhe);
                                        tallennaTosite(pohja.id, peru, [vapauta, seuraava](bool) {
                                            vapauta();
                                            seuraava();
                                        });
                                    });
                                    return;
                                }
                                token_ = token;

                                // Vuokra uusitaan juuri ennen POST-kutsua. Token-haku voi kestää
                                // yli 180 sekuntia, vaikka siirtymän aikakatkaisu on 60 sekuntia joutilaisuutta.
                                uudistaVaraus(viite, tunnus, [this, vapauta, loppuu, korvaava, korvauksenSyy, sanoma, alvTositeId, alv, pohja, aiempi, seuraava, viite, tunnus]
                                              (bool oma) {
                                    if (!oma) {
                                        vapauta();
                                        virheella(tr("Lähetysvaraus ehti siirtyä toiselle yhteydelle, ilmoitusta ei lähetetty"));
                                        seuraava();
                                        return;
                                    }

                                const quint64 lahetysSukupolvi = sukupolvi_;
                                emit loki(tr("Lähetetään alv-ilmoitus kaudelta %1").arg(loppuu.toString("dd.MM.yyyy")));
                                // vartioi = false: jos kirja vaihtuu kesken POST-kutsun, lähteneestä
                                // ilmoituksesta kerrotaan. Tallennus uuteen kirjaan estetään erikseen.
                                kutsu(Vero::FILE_VAT_RETURN, sanoma, false,
                                      [this, vapauta, lahetysSukupolvi, loppuu, korvaava, korvauksenSyy, sanoma, alvTositeId, alv, pohja, aiempi, seuraava, viite, tunnus]
                                      (const Vero::Vastaus& vastaus) {
                                    if (lahetysSukupolvi != sukupolvi_) {
                                        if (vastaus.ok())
                                            emit loki(tr("Alv-ilmoitus kaudelta %1 ehti lähteä Verohallintoon, mutta kirjanpito vaihtui. "
                                                         "Tarkista tila OmaVerosta.").arg(loppuu.toString("dd.MM.yyyy")));
                                        return;
                                    }

                                    const Seuraava jalkeen = [vapauta, seuraava] {
                                        vapauta();
                                        seuraava();
                                    };
                                    // Vastaus tallennetaan vain, jos varaus on yhä meillä.
                                    // Uusittu vuokra kattaa POSTin, mutta toinen yhteys on voinut ehtiä väliin.
                                    vahvistaVaraus(viite, tunnus, [this, jalkeen, loppuu, korvaava, korvauksenSyy, sanoma, alvTositeId, alv, pohja, aiempi, vastaus](bool yhdaOma) {
                                    if (!yhdaOma) {
                                        emit loki(tr("Alv-ilmoitus kaudelta %1 ehti lähteä, mutta lähetysvaraus siirtyi toiselle yhteydelle. "
                                                     "Tarkista tila OmaVerosta.").arg(loppuu.toString("dd.MM.yyyy")));
                                        jalkeen();
                                        return;
                                    }
                                    // Verkkovirhe, katkennut HTTP 200 ja 5xx eivät todista hylkäystä:
                                    // palvelin on voinut ottaa ilmoituksen vastaan. Vain 4xx on varma hylkäys.
                                    if (!vastaus.ok()) {
                                        const bool verkkovirhe = !Vero::onVarmaHttpHylkays(vastaus.http, vastaus.verkkovirhe);
                                        Vero::TositeTiedot tiedot;
                                        if (verkkovirhe) {
                                            // POST on voinut mennä perille, joten yritys jää tuntemattomaksi.
                                            Vero::LoydettyTosite tulos = pohja;
                                            tulos.vero.insert(QStringLiteral("tila"), TILA_TUNTEMATON);
                                            QVariantList tapahtumat = tulos.vero.value(QStringLiteral("lahetykset")).toList();
                                            tapahtumat.append(QVariantMap{
                                                {QStringLiteral("aika"), QDateTime::currentDateTime().toString(Qt::ISODate)},
                                                {QStringLiteral("korvaava"), korvaava},
                                                {QStringLiteral("http"), vastaus.http},
                                                {QStringLiteral("tulos"), TILA_TUNTEMATON}});
                                            tulos.vero.insert(QStringLiteral("lahetykset"), tapahtumat);
                                            tiedot.vero = tulos.vero;
                                            virheella(vastaus.http >= 500
                                                      ? tr("Verohallinnon palvelin vastasi virheellä (HTTP %1). Ilmoitus on voitu vastaanottaa. "
                                                           "Tarkista OmaVero ennen uutta lähetystä.").arg(vastaus.http)
                                                      : tr("Yhteys katkesi lähetyksen aikana. Ilmoitus on voitu vastaanottaa. "
                                                           "Tarkista OmaVero ennen uutta lähetystä."));
                                            tallennaTosite(tulos.id, tiedot, [jalkeen](bool) { jalkeen(); });
                                        } else {
                                            // Verohallinto hylkäsi tämän yrityksen. Aiemmin vastaanotettu
                                            // ilmoitus säilyy; hylkäys kirjataan vain historiariviksi.
                                            QVariantMap tapahtuma{
                                                {QStringLiteral("aika"), QDateTime::currentDateTime().toString(Qt::ISODate)},
                                                {QStringLiteral("korvaava"), korvaava},
                                                {QStringLiteral("korvauksenSyy"), korvauksenSyy},
                                                {QStringLiteral("http"), vastaus.http},
                                                {QStringLiteral("tulos"), TILA_HYLATTY},
                                                {QStringLiteral("sanoma"), sanoma}};
                                            if (!vastaus.json().isEmpty())
                                                tapahtuma.insert(QStringLiteral("virhevastaus"), vastaus.json());
                                            tiedot.vero = Vero::epaonnistuneenLahetyksenVero(
                                                        aiempi.id ? aiempi.vero : QVariantMap(), pohja.vero, tapahtuma);
                                            if (tiedot.vero.value(QStringLiteral("tila")).toString() == TILA_HYLATTY
                                                    && !vastaus.json().isEmpty())
                                                tiedot.vero.insert(QStringLiteral("vastaus"), vastaus.json());
                                            virheella(tr("Alv-ilmoituksen lähetys epäonnistui: %1")
                                                      .arg(Vero::virheteksti(vastaus.http, vastaus.json(), vastaus.verkkovirhe)));
                                            tallennaTosite(pohja.id, tiedot, [jalkeen](bool) { jalkeen(); });
                                        }
                                        return;
                                    }

                                    emit loki(tr("Verohallinto vastaanotti ilmoituksen. Se on käsittelyjonossa, "
                                                 "käsittelyn tulos näkyy myöhemmin haettaessa."));
                                    tallennaLahetetty(alvTositeId, alv, loppuu, korvaava, korvauksenSyy, sanoma, vastaus, pohja, jalkeen);
                                    });
                                }, false);
                                });
                            });
                        });
                    }));
                });
            });
        });
        kysely->kysy();
    });
    ajaSeuraava();
}

void VeroPalvelu::tallennaLahetetty(int alvTositeId, const QVariantMap &alv, const QDate &loppuu,
                                    bool korvaava, const QString &korvauksenSyy,
                                    const QVariantMap &sanoma, const Vero::Vastaus &vastaus,
                                    const Vero::LoydettyTosite &aiempi, const Seuraava &seuraava)
{
    const Vero::Ymparisto ymparisto = asetukset_.ymparisto;
    const QString viite = Vero::lahetettyAlvViite(ymparisto, loppuu);
    const QDateTime nyt = QDateTime::currentDateTime();

    // Aiemmat lähetykset säilyvät tositteen historiassa
    QVariantList lahetykset = aiempi.vero.value("lahetykset").toList();
    lahetykset.append(QVariantMap{
                          {"aika", nyt.toString(Qt::ISODate)},
                          {"korvaava", korvaava},
                          {"korvauksenSyy", korvauksenSyy},
                          {"http", vastaus.http}});

    Vero::TositeTiedot tiedot;
    tiedot.viite = viite;
    tiedot.pvm = QDate::currentDate();
    tiedot.otsikko = tr("Alv-ilmoitus Verohallintoon %1 – %2")
            .arg(alv.value("kausialkaa").toDate().toString("dd.MM.yyyy"), loppuu.toString("dd.MM.yyyy"))
            + ymparistoLoppu(ymparisto);
    tiedot.vero = QVariantMap{
        {"suunta", "lahetetty"},
        {"ymparisto", Vero::ymparistoAvain(ymparisto)},
        {"tyyppi", "alv"},
        {"kausialkaa", alv.value("kausialkaa").toDate().toString(Qt::ISODate)},
        {"kausipaattyy", loppuu.toString(Qt::ISODate)},
        {"alvtosite", alvTositeId},
        {"tila", TILA_KASITTELYSSA},
        {"lahetetty", nyt.toString(Qt::ISODate)},
        {"korvaava", korvaava},
        {"korvauksenSyy", korvauksenSyy},
        {"lahetykset", lahetykset},
        {"sanoma", sanoma},
        {"vastaus", vastaus.json()}};

    const QString aika = nyt.toString("yyyyMMddhhmmss");
    tiedot.liitteet.append({QString("FileVATReturn-%1-%2.json").arg(loppuu.toString(Qt::ISODate), aika),
                            kauniisti(sanoma)});
    tiedot.liitteet.append({QString("FileVATReturn-vastaus-%1-%2.json").arg(loppuu.toString(Qt::ISODate), aika),
                            vastaus.runko.isEmpty() ? QByteArray("{}") : vastaus.runko});

    tallennaTosite(aiempi.id, tiedot, [this, seuraava](bool ok) {
        if (ok)
            emit loki(tr("Lähetetty ilmoitus tallennettu tositteeksi"));
        else
            emit loki(tr("Ilmoitus lähti, mutta tositteen tallennus epäonnistui! Tarkista Vero-sivun tila ennen uutta lähetystä."));
        seuraava();
    });
}

void VeroPalvelu::lahetyksenTila(QObject *konteksti, Vero::Ymparisto ymparisto, const QDate &loppuu,
                                 const std::function<void (const Vero::LahetysTietoa &)> &valmis)
{
    const QString saapunutViite = Vero::saapunutAlvViite(ymparisto, loppuu);

    VeroTosite::etsi(konteksti, Vero::lahetettyAlvViite(ymparisto, loppuu),
                     [konteksti, saapunutViite, valmis](const Vero::LoydettyTosite& lahetetty) {
        Vero::LahetysTietoa tieto;
        if (lahetetty.virhe) {
            tieto.virhe = true;
            valmis(tieto);
            return;
        }
        if (lahetetty.id) {
            const QString tila = lahetetty.vero.value(QStringLiteral("tila")).toString();
            tieto.lahetettyId = lahetetty.id;
            tieto.vero = lahetetty.vero;
            tieto.vahvistus = Vero::lahetysVahvistus(lahetetty.id, lahetetty.vero);
            if (tila == TILA_KASITELTY) {
                tieto.tila = Vero::LahetysTila::KASITELTY;
                valmis(tieto);
                return;
            }
            if (tila == TILA_LAHETTAMASSA || tila == TILA_TUNTEMATON) {
                tieto.tila = Vero::LahetysTila::TARKISTETTAVA;
                valmis(tieto);
                return;
            }
            if (tila == TILA_KASITTELYSSA) {
                // Oma lähetys jää käsittelyyn. Haettu ilmoitus katsotaan silti:
                // jos kausi on jo ilmoitettu (eri sanoma, esim. OmaVerossa),
                // seuraava lähetys on korvaava. Oman tositteen tilaa ei muuteta tässä.
                tieto.tila = Vero::LahetysTila::KASITTELYSSA;
            }
            // Hylätty lähetys ei ole vastaanotettu. Alla katsotaan, onko kausi
            // kuitenkin ilmoitettu muuta kautta (haettu käsitelty ilmoitus).
        }

        // Ei lähetettyä ilmoitusta, mutta Verohallinnolta haettu käsitelty ilmoitus tarkoittaa,
        // että kausi on jo ilmoitettu (esim. OmaVerossa): uusi ilmoitus olisi korvaava
        VeroTosite::etsi(konteksti, saapunutViite, [valmis, tieto](const Vero::LoydettyTosite& saapunut) {
            Vero::LahetysTietoa tulos = tieto;
            if (saapunut.virhe) {
                tulos.virhe = true;
            } else if (saapunut.id) {
                tulos.tila = Vero::LahetysTila::KASITELTY;
                tulos.saapunutId = saapunut.id;
                if (!tieto.lahetettyId)
                    tulos.vero = saapunut.vero;
            }
            valmis(tulos);
        });
    });
}

void VeroPalvelu::tallennaTosite(int tositeId, const Vero::TositeTiedot &tiedot,
                                 const std::function<void (bool)> &valmis)
{
    VeroTosite::tallenna(this, tositeId, tiedot, vartioitu([this, valmis](int id, const QString& virhe) {
        if (id) {
            emit tositeTallennettu(id);
            valmis(true);
        } else {
            virheella(virhe);
            valmis(false);
        }
    }));
}
