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
#include "verotosite.h"

#include "veroapu.h"

#include "db/kirjanpito.h"
#include "db/kpkysely.h"
#include "db/tilikausimodel.h"
#include "db/tositetyyppimodel.h"
#include "liite/liitteetmodel.h"
#include "model/tosite.h"

#include <QCoreApplication>
#include <QTimer>

#include <memory>

void VeroTosite::etsi(QObject *konteksti, const QString &viite,
                      const std::function<void (const Vero::LoydettyTosite &)> &valmis)
{
    const auto epaonnistui = [valmis] {
        Vero::LoydettyTosite tulos;
        tulos.virhe = true;
        valmis(tulos);
    };

    KpKysely* lista = kpk(QStringLiteral("/tositteet"));
    if (!lista) {
        epaonnistui();
        return;
    }
    lista->lisaaAttribuutti(QStringLiteral("viite"), viite);

    QObject::connect(lista, &KpKysely::vastaus, konteksti, [konteksti, valmis, epaonnistui](QVariant* data) {
        const QVariantList tositteet = data->toList();
        if (tositteet.isEmpty()) {
            valmis(Vero::LoydettyTosite());
            return;
        }
        const int id = tositteet.first().toMap().value("id").toInt();

        KpKysely* tosite = kpk(QString("/tositteet/%1").arg(id));
        if (!tosite) {
            epaonnistui();
            return;
        }
        QObject::connect(tosite, &KpKysely::vastaus, konteksti, [id, valmis](QVariant* tositeData) {
            const QVariantMap map = tositeData->toMap();
            Vero::LoydettyTosite loydetty;
            loydetty.id = id;
            loydetty.vero = map.value(QStringLiteral("vero")).toMap();
            for (const QVariant& liite : map.value(QStringLiteral("liitteet")).toList())
                loydetty.liitteet << liite.toMap().value(QStringLiteral("nimi")).toString();
            valmis(loydetty);
        });
        QObject::connect(tosite, &KpKysely::virhe, konteksti, [epaonnistui] {
            epaonnistui();
        });
        tosite->kysy();
    });
    QObject::connect(lista, &KpKysely::virhe, konteksti, [epaonnistui] {
        epaonnistui();
    });
    lista->kysy();
}

void VeroTosite::tallenna(QObject *konteksti, int tositeId, const Vero::TositeTiedot &tiedot,
                          const Vero::TallennusValmis &valmis)
{
    Tosite* tosite = new Tosite(konteksti);

    // Kirjanpito käyttää samaa yhteysmallia uudelleen, joten osoittimen vertailu ei
    // huomaa kirjan vaihdosta. Vaihdos keskeyttää tallennuksen, jotta vanha tosite-id
    // ei kirjoitu uuteen kirjaan.
    const auto kirjanpitoVaihtui = std::make_shared<bool>(false);
    QObject::connect(kp(), &Kirjanpito::tietokantaVaihtui, tosite, [kirjanpitoVaihtui] {
        *kirjanpitoVaihtui = true;
    });

    // Tosite poistetaan, kun tulos on ilmoitettu. Tuloksen saa ilmoittaa vain kerran.
    const auto ilmoitettu = std::make_shared<bool>(false);
    const auto lopeta = [tosite, valmis, ilmoitettu](int id, const QString& virhe) {
        if (*ilmoitettu)
            return;
        *ilmoitettu = true;
        tosite->deleteLater();
        valmis(id, virhe);
    };

    const auto talleta = [tosite, tiedot, lopeta, kirjanpitoVaihtui] {
        if (*kirjanpitoVaihtui) {
            lopeta(0, QCoreApplication::translate("Vero", "Kirjanpito vaihtui kesken tallennuksen, tositetta ei tallennettu"));
            return;
        }

        // Odotetut nimet tallennetaan tositteelle ennen liitteitä. Jos liite jää
        // puuttumaan, seuraava haku näkee eron eikä ohita tositetta valmiina.
        QVariantMap vero = tiedot.vero;
        QStringList odotetut = vero.contains(QStringLiteral("odotetutLiitteet"))
                ? vero.value(QStringLiteral("odotetutLiitteet")).toStringList()
                : tosite->data(Tosite::VERO).toMap().value(QStringLiteral("odotetutLiitteet")).toStringList();
        for (const Vero::Liitetiedosto& liite : tiedot.liitteet) {
            if (!odotetut.contains(liite.nimi))
                odotetut << liite.nimi;
        }
        if (!odotetut.isEmpty())
            vero.insert(QStringLiteral("odotetutLiitteet"), odotetut);
        // kesken pysyy tositteella, kunnes liitteet on saatu tallennettua
        if (!tiedot.liitteet.isEmpty())
            vero.insert(QStringLiteral("kesken"), true);
        tosite->setData(Tosite::VERO, vero);
        if (!tiedot.viite.isEmpty())
            tosite->asetaViite(tiedot.viite);
        if (!tiedot.kommentti.isEmpty())
            tosite->asetaKommentti(tiedot.kommentti);

        for (const Vero::Liitetiedosto& liite : tiedot.liitteet) {
            if (!tosite->liitteet()->lisaa(liite.sisalto, liite.nimi)) {
                lopeta(0, QCoreApplication::translate("Vero", "Liitteen %1 lisääminen tositteelle epäonnistui").arg(liite.nimi));
                return;
            }
        }

        // Liitteellinen tallennus merkitään kesken, kunnes liitteet ovat kannassa.
        // Merkintä poistetaan vasta seuraavalla kierroksella: Tosite::tallenna() ei
        // lähde liikkeelle saman talletettu-kutsun sisältä, koska edellinen tallennus
        // on silloin vielä käynnissä.
        const auto odottaaLiitteita = std::make_shared<bool>(!tiedot.liitteet.isEmpty());
        QObject::connect(tosite, &Tosite::talletettu, tosite, [tosite, lopeta, odottaaLiitteita, kirjanpitoVaihtui](int id) {
            if (*kirjanpitoVaihtui) {
                lopeta(0, QCoreApplication::translate("Vero", "Kirjanpito vaihtui kesken tallennuksen, tositetta ei tallennettu"));
                return;
            }
            if (*odottaaLiitteita) {
                *odottaaLiitteita = false;
                QTimer::singleShot(0, tosite, [tosite, lopeta, kirjanpitoVaihtui] {
                    if (*kirjanpitoVaihtui) {
                        lopeta(0, QCoreApplication::translate("Vero", "Kirjanpito vaihtui kesken tallennuksen, tositetta ei tallennettu"));
                        return;
                    }
                    QVariantMap vero = tosite->data(Tosite::VERO).toMap();
                    vero.remove(QStringLiteral("kesken"));
                    tosite->setData(Tosite::VERO, vero);
                    tosite->tallenna(Tosite::KIRJANPIDOSSA);
                });
                return;
            }
            lopeta(id, QString());
        });
        // Liitteen tallennusvirhe ei kulje Tosite::tallennusvirhe-signaalin kautta, joten
        // se kuunnellaan erikseen: tosite on tässä vaiheessa jo tallessa, mutta liite puuttuu
        QObject::connect(tosite->liitteet(), &LiitteetModel::tallennusVirhe, tosite, [lopeta](int virhe, const QString& selitys) {
            lopeta(0, QCoreApplication::translate("Vero", "Liitteen tallennus epäonnistui (virhe %1): %2").arg(virhe).arg(selitys));
        });
        QObject::connect(tosite, &Tosite::tallennusvirhe, tosite, [lopeta](int virhe) {
            lopeta(0, QCoreApplication::translate("Vero", "Tositteen tallennus epäonnistui (virhe %1)").arg(virhe));
        });
        tosite->tallenna(Tosite::KIRJANPIDOSSA);
    };

    if (tositeId) {
        // Päivitys: ladataan nykyinen tosite liitteineen ja lisätään sen jatkoksi
        QObject::connect(tosite, &Tosite::ladattu, tosite, talleta, Qt::SingleShotConnection);
        QObject::connect(tosite, &Tosite::latausvirhe, tosite, [lopeta](int virhe) {
            lopeta(0, QCoreApplication::translate("Vero", "Tositteen lataus epäonnistui (virhe %1)").arg(virhe));
        });
        tosite->lataa(tositeId);
    } else {
        const QDate pvm = Vero::rajattuPvm(tiedot.pvm,
                                           kp()->tilikaudet()->kirjanpitoAlkaa(),
                                           kp()->tilikaudet()->kirjanpitoLoppuu());
        tosite->nollaa(pvm, TositeTyyppi::MUISTIO);
        tosite->asetaOtsikko(tiedot.otsikko);
        talleta();
    }
}
