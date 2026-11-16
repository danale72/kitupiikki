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
#include "verosivu.h"

#include "db/kirjanpito.h"
#include "db/kpkysely.h"
#include "model/euro.h"
#include "veropalvelu.h"
#include "verosetusdialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

enum Sarake { ALKAA, PAATTYY, MAKSETTAVA, TILA, SARAKKEITA };

// Rivin tiedot tallennetaan ensimmäisen sarakkeen kohteeseen
enum Rooli {
    AlvTositeRooli = Qt::UserRole,
    AlkaaRooli,
    PaattyyRooli,
    MuokattuRooli,
    VeroTositeRooli
};

}   // namespace

VeroSivu::VeroSivu(QWidget *parent) :
    KitupiikkiSivu(parent),
    palvelu_(new VeroPalvelu(this))
{
    otsikko_ = new QLabel;
    otsikko_->setWordWrap(true);

    taulu_ = new QTableWidget(0, SARAKKEITA);
    taulu_->setHorizontalHeaderLabels({tr("Alkaa"), tr("Päättyy"), tr("Maksettava vero"), tr("Tila Verohallinnossa")});
    taulu_->setSelectionBehavior(QAbstractItemView::SelectRows);
    taulu_->setSelectionMode(QAbstractItemView::SingleSelection);
    taulu_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    taulu_->verticalHeader()->hide();
    taulu_->horizontalHeader()->setStretchLastSection(true);
    taulu_->setMinimumHeight(180);

    vuosiSpin_ = new QSpinBox;
    vuosiSpin_->setRange(2000, 2100);
    vuosiSpin_->setValue(QDate::currentDate().year());

    haeNappi_ = new QPushButton(tr("Hae Verohallinnolta"));
    haeNappi_->setToolTip(tr("Hakee asiakkaan kirjeet ja annetun vuoden alv-ilmoitukset ja tallentaa ne tositteiksi"));
    lahetaNappi_ = new QPushButton(QIcon(":/pic/verotilitys.png"), tr("Lähetä alv-ilmoitus Verohallintoon"));
    tositeNappi_ = new QPushButton(tr("Näytä lähetetty tosite"));
    asetusNappi_ = new QPushButton(QIcon(":/pic/ratas.png"), tr("Yhteyden asetukset..."));

    loki_ = new QPlainTextEdit;
    loki_->setReadOnly(true);
    loki_->setMaximumBlockCount(2000);

    QHBoxLayout* hakuRivi = new QHBoxLayout;
    hakuRivi->addWidget(new QLabel(tr("Alv-kausien vuosi")));
    hakuRivi->addWidget(vuosiSpin_);
    hakuRivi->addWidget(haeNappi_);
    hakuRivi->addStretch();
    hakuRivi->addWidget(asetusNappi_);

    QHBoxLayout* lahetysRivi = new QHBoxLayout;
    lahetysRivi->addWidget(lahetaNappi_);
    lahetysRivi->addWidget(tositeNappi_);
    lahetysRivi->addStretch();

    QVBoxLayout* asettelu = new QVBoxLayout(this);
    asettelu->addWidget(otsikko_);
    asettelu->addLayout(hakuRivi);
    asettelu->addWidget(new QLabel(tr("Kirjanpitoon tehdyt alv-ilmoitukset")));
    asettelu->addWidget(taulu_, 1);
    asettelu->addLayout(lahetysRivi);
    asettelu->addWidget(new QLabel(tr("Tapahtumat")));
    asettelu->addWidget(loki_, 1);

    connect(asetusNappi_, &QPushButton::clicked, this, &VeroSivu::asetukset);
    connect(haeNappi_, &QPushButton::clicked, this, &VeroSivu::hae);
    connect(lahetaNappi_, &QPushButton::clicked, this, &VeroSivu::laheta);
    connect(tositeNappi_, &QPushButton::clicked, this, &VeroSivu::avaaTosite);
    connect(taulu_, &QTableWidget::itemSelectionChanged, this, &VeroSivu::paivitaNapit);
    connect(taulu_, &QTableWidget::cellDoubleClicked, this, &VeroSivu::avaaTosite);

    connect(palvelu_, &VeroPalvelu::loki, this, &VeroSivu::kirjaa);
    connect(palvelu_, &VeroPalvelu::valmis, this, &VeroSivu::toimintoValmis);
    connect(palvelu_, &VeroPalvelu::kaudetHaettu, this, [this](const QList<Vero::Verokausi>& kaudet) {
        for (const Vero::Verokausi& kausi : kaudet) {
            kirjaa(tr("Kausi %1 – %2: %3").arg(kausi.alkaa.toString("dd.MM.yyyy"),
                                              kausi.loppuu.toString("dd.MM.yyyy"),
                                              kausi.kasitelty() ? tr("käsitelty") : (kausi.tila.isEmpty() ? tr("ei ilmoitusta") : kausi.tila)));
        }
    });

    // Sivun tila kuuluu aina siihen kirjanpitoon, joka oli auki kun tieto pyydettiin
    connect(kp(), &Kirjanpito::tietokantaVaihtui, this, &VeroSivu::kirjanpitoVaihtui);

    paivitaNapit();
}

void VeroSivu::kirjanpitoVaihtui()
{
    sukupolvi_++;
    kaynnissa_ = false;
    taulu_->setRowCount(0);
    loki_->clear();
    paivitaNapit();
}

void VeroSivu::siirrySivulle()
{
    const QString ytunnus = VeroPalvelu::asiakkaanYtunnus();
    otsikko_->setText(tr("<b>%1</b> – Y-tunnus %2 – ympäristö: <b>%3</b>")
                      .arg(kp()->asetukset()->nimi().toHtmlEscaped(),
                           ytunnus.isEmpty() ? tr("puuttuu tai on virheellinen") : ytunnus,
                           Vero::ymparistoNimi(Vero::Asetukset::valittuYmparisto(kp()->settings()))));
    lataaIlmoitukset();
    paivitaNapit();
}

void VeroSivu::kirjaa(const QString &teksti)
{
    loki_->appendPlainText(QString("%1  %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss"), teksti));
}

int VeroSivu::valittuRivi() const
{
    const QList<QTableWidgetSelectionRange> alueet = taulu_->selectedRanges();
    return alueet.isEmpty() ? -1 : alueet.first().topRow();
}

void VeroSivu::paivitaNapit()
{
    const int rivi = valittuRivi();
    haeNappi_->setEnabled(!kaynnissa_);
    asetusNappi_->setEnabled(!kaynnissa_);
    lahetaNappi_->setEnabled(!kaynnissa_ && rivi >= 0 && kp()->asetukset()->onko("AlvVelvollinen"));
    tositeNappi_->setEnabled(rivi >= 0 && taulu_->item(rivi, 0)->data(VeroTositeRooli).toInt() > 0);
}

void VeroSivu::asetukset()
{
    VeroSetusDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
        siirrySivulle();
}

void VeroSivu::lataaIlmoitukset()
{
    KpKysely* kysely = kpk("/alv");
    if (!kysely)
        return;
    const quint64 sukupolvi = sukupolvi_;
    connect(kysely, &KpKysely::vastaus, this, [this, sukupolvi](QVariant* data) {
        if (sukupolvi != sukupolvi_)
            return;
        const QVariantList lista = data->toList();
        taulu_->setRowCount(0);
        for (const QVariant& item : lista) {
            const QVariantMap map = item.toMap();
            const int rivi = taulu_->rowCount();
            taulu_->insertRow(rivi);

            QTableWidgetItem* alkaa = new QTableWidgetItem(map.value("kausialkaa").toDate().toString("dd.MM.yyyy"));
            alkaa->setData(AlvTositeRooli, map.value("id").toInt());
            alkaa->setData(AlkaaRooli, map.value("kausialkaa").toDate());
            alkaa->setData(PaattyyRooli, map.value("kausipaattyy").toDate());
            alkaa->setData(MuokattuRooli, map.value("muokattu").toBool());
            alkaa->setData(VeroTositeRooli, 0);
            taulu_->setItem(rivi, ALKAA, alkaa);
            taulu_->setItem(rivi, PAATTYY, new QTableWidgetItem(map.value("kausipaattyy").toDate().toString("dd.MM.yyyy")));

            QTableWidgetItem* vero = new QTableWidgetItem(Euro::fromString(map.value("maksettava").toString()).display());
            vero->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            taulu_->setItem(rivi, MAKSETTAVA, vero);
            taulu_->setItem(rivi, TILA, new QTableWidgetItem(tr("Tarkistetaan...")));
            paivitaTila(rivi);
        }
        taulu_->resizeColumnsToContents();
        taulu_->horizontalHeader()->setStretchLastSection(true);
        paivitaNapit();
    });
    kysely->kysy();
}

void VeroSivu::paivitaTila(int rivi)
{
    const QDate loppuu = taulu_->item(rivi, ALKAA)->data(PaattyyRooli).toDate();

    const quint64 sukupolvi = sukupolvi_;
    VeroPalvelu::lahetyksenTila(this, Vero::Asetukset::valittuYmparisto(kp()->settings()), loppuu,
                                [this, loppuu, sukupolvi](const Vero::LahetysTietoa& tieto) {
        if (sukupolvi != sukupolvi_)
            return;
        const Vero::LahetysTila tila = tieto.tila;
        const QVariantMap& vero = tieto.vero;
        const int tositeId = tieto.lahetettyId ? tieto.lahetettyId : tieto.saapunutId;
        // Taulu on voitu ladata uudelleen, joten rivi etsitään kauden mukaan
        for (int rivi = 0; rivi < taulu_->rowCount(); rivi++) {
            QTableWidgetItem* alkaa = taulu_->item(rivi, ALKAA);
            if (!alkaa || alkaa->data(PaattyyRooli).toDate() != loppuu)
                continue;

            QString teksti;
            if (tieto.virhe) {
                taulu_->item(rivi, TILA)->setText(tr("Tilaa ei voitu lukea kirjanpidosta"));
                alkaa->setData(VeroTositeRooli, 0);
                continue;
            }
            switch (tila) {
            case Vero::LahetysTila::EILAHETETTY:
                teksti = tr("Ei lähetetty");
                break;
            case Vero::LahetysTila::KASITTELYSSA:
                teksti = tr("Käsittelyssä (lähetetty %1)")
                        .arg(vero.value("lahetetty").toDateTime().toString("dd.MM.yyyy hh:mm"));
                break;
            case Vero::LahetysTila::KASITELTY:
                // Oma yritys voi olla yhä jonossa, jos haettu ilmoitus on eri sanoma.
                if (tieto.lahetettyId && vero.value(QStringLiteral("tila")).toString() == QLatin1String("kasittelyssa"))
                    teksti = tr("Kausi on jo ilmoitettu, oma lähetys ei täsmää");
                else
                    teksti = tieto.lahetettyId ? tr("Käsitelty") : tr("Käsitelty (haettu Verohallinnolta)");
                break;
            case Vero::LahetysTila::TARKISTETTAVA:
                teksti = tr("Lähetyksen tulos ei ole tiedossa");
                break;
            }
            if (tila == Vero::LahetysTila::EILAHETETTY && vero.value(QStringLiteral("tila")).toString() == QLatin1String("hylatty"))
                teksti = tr("Hylätty, ei vastaanotettu");
            taulu_->item(rivi, TILA)->setText(teksti);
            alkaa->setData(VeroTositeRooli, tositeId);
        }
        taulu_->resizeColumnsToContents();
        taulu_->horizontalHeader()->setStretchLastSection(true);
        paivitaNapit();
    });
}

void VeroSivu::hae()
{
    QString virhe;
    if (!palvelu_->alusta(&virhe)) {
        QMessageBox::warning(this, tr("Verohallinnon yhteys"), virhe);
        return;
    }
    for (const QString& varoitus : palvelu_->varoitukset())
        kirjaa(varoitus);

    kaynnissa_ = true;
    paivitaNapit();
    palvelu_->hae(vuosiSpin_->value());
}

void VeroSivu::laheta()
{
    const int rivi = valittuRivi();
    if (rivi < 0)
        return;

    QTableWidgetItem* item = taulu_->item(rivi, ALKAA);
    const int alvTositeId = item->data(AlvTositeRooli).toInt();
    const QDate alkaa = item->data(AlkaaRooli).toDate();
    const QDate loppuu = item->data(PaattyyRooli).toDate();

    QString virhe;
    if (!palvelu_->alusta(&virhe)) {
        QMessageBox::warning(this, tr("Verohallinnon yhteys"), virhe);
        return;
    }
    for (const QString& varoitus : palvelu_->varoitukset())
        kirjaa(varoitus);

    // Tuoreus luetaan uudelleen. Taulun MuokattuRooli on lataushetken välimuisti,
    // joten toisen yhteyden kirjaukset eivät näy siinä.
    const quint64 sukupolvi = sukupolvi_;
    KpKysely* tuoreus = kpk(QStringLiteral("/alv"));
    if (!tuoreus) {
        QMessageBox::warning(this, tr("Verohallinnon yhteys"),
                             tr("Alv-laskelman tuoreutta ei voitu tarkistaa, ilmoitusta ei lähetetty."));
        return;
    }
    connect(tuoreus, &KpKysely::virhe, this, [this, sukupolvi](int, const QString&) {
        if (sukupolvi != sukupolvi_)
            return;
        QMessageBox::warning(this, tr("Verohallinnon yhteys"),
                             tr("Alv-laskelman tuoreutta ei voitu tarkistaa, ilmoitusta ei lähetetty."));
    });
    connect(tuoreus, &KpKysely::vastaus, this, [this, sukupolvi, alvTositeId, alkaa, loppuu](QVariant* data) {
        if (sukupolvi != sukupolvi_)
            return;
        bool loytyi = false;
        bool muokattu = false;
        for (const QVariant& rivi : data->toList()) {
            const QVariantMap map = rivi.toMap();
            if (map.value(QStringLiteral("id")).toInt() != alvTositeId)
                continue;
            loytyi = true;
            muokattu = map.value(QStringLiteral("muokattu")).toBool();
            break;
        }
        if (!loytyi) {
            QMessageBox::warning(this, tr("Verohallinnon yhteys"),
                                 tr("Alv-laskelmaa ei löydy, ilmoitusta ei lähetetty."));
            return;
        }
        if (muokattu &&
            QMessageBox::question(this, tr("Alv-laskelma on vanhentunut"),
                                  tr("Kauden alv-kirjauksia on muutettu alv-laskelman tekemisen jälkeen, joten "
                                     "ilmoitettavat luvut eivät vastaa nykyistä kirjanpitoa.\n\n"
                                     "Tee ensin alv-laskelma uudelleen ALV-sivulla. Lähetetäänkö silti?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;

    // Tilan tarkistus tositteista juuri ennen lähetystä: toinen lähetys kuuluu aina varmistaa
    VeroPalvelu::lahetyksenTila(this, palvelu_->ymparisto(), loppuu,
                                [this, alvTositeId, alkaa, loppuu, sukupolvi](const Vero::LahetysTietoa& tieto) {
        if (sukupolvi != sukupolvi_)
            return;
        if (tieto.virhe) {
            // Tilaa ei tiedetä, joten ei voida varmistaa ettei ilmoitusta ole jo lähetetty
            QMessageBox::warning(this, tr("Verohallinnon yhteys"),
                                 tr("Aiemman lähetyksen tilaa ei voitu lukea kirjanpidosta, joten ilmoitusta ei lähetetty."));
            return;
        }
        const Vero::LahetysTila tila = tieto.tila;
        bool korvaava = false;
        QString syy;

        if (tila == Vero::LahetysTila::TARKISTETTAVA) {
            QMessageBox kysymys(this);
            kysymys.setIcon(QMessageBox::Warning);
            kysymys.setWindowTitle(tr("Edellisen lähetyksen tulos ei ole tiedossa"));
            kysymys.setText(tr("Kauden %1 – %2 alv-ilmoituksen lähetyksestä ei saatu vastausta. "
                               "Ilmoitus on voitu silti vastaanottaa.")
                           .arg(alkaa.toString("dd.MM.yyyy"), loppuu.toString("dd.MM.yyyy")));
            kysymys.setInformativeText(tr("Tarkista OmaVero ennen uutta lähetystä. Jos ilmoitus näkyy siellä, "
                                          "älä lähetä uudelleen vaan hae tiedot Verohallinnolta. "
                                          "Jos ilmoitusta ei ole vastaanotettu, sen voi lähettää uudelleen."));
            kysymys.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            kysymys.button(QMessageBox::Yes)->setText(tr("Ei vastaanotettu, lähetä uudelleen"));
            kysymys.button(QMessageBox::No)->setText(tr("Peruuta"));
            kysymys.setDefaultButton(QMessageBox::No);
            if (kysymys.exec() != QMessageBox::Yes)
                return;
            // Epävarma yritys saattaa itse olla korvaava. Uusi lähetys käyttää samaa
            // lippua ja syytä, koska alkuperäinen käsitelty ilmoitus on edelleen voimassa.
            korvaava = tieto.vero.value(QStringLiteral("korvaava")).toBool();
            syy = tieto.vero.value(QStringLiteral("korvauksenSyy")).toString();
            varmistaJaLaheta(alvTositeId, loppuu, korvaava, syy, Vero::LahetysTila::TARKISTETTAVA, tieto.vahvistus);
            return;
        }

        if (tila != Vero::LahetysTila::EILAHETETTY) {
            // Tilanteessa, jossa ilmoitus on tehty muualla ja vain haettu tänne, ei ole omaa lähetysaikaa
            const QString lahetetty = tieto.lahetettyId ?
                        tr("lähetetty %1").arg(tieto.vero.value("lahetetty").toDateTime().toString("dd.MM.yyyy hh:mm")) :
                        tr("haettu Verohallinnolta %1").arg(tieto.vero.value("haettu").toDateTime().toString("dd.MM.yyyy hh:mm"));
            const QString tilateksti = tila == Vero::LahetysTila::KASITELTY ?
                        tr("Verohallinto on jo käsitellyt sen.") :
                        tr("Se on vielä Verohallinnon käsittelyjonossa.");

            if (QMessageBox::question(this, tr("Ilmoitus on jo lähetetty"),
                                      tr("Kauden %1 – %2 alv-ilmoitus on jo tehty (%3). %4\n\n"
                                         "Haluatko korvata sen?")
                                      .arg(alkaa.toString("dd.MM.yyyy"), loppuu.toString("dd.MM.yyyy"), lahetetty, tilateksti),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;

            // Käsitelty ilmoitus, ja myös jonossa oleva korvaava ilmoitus, on korvattava
            // uudella korvaavalla ilmoituksella: alkuperäinen käsitelty ilmoitus on yhä voimassa.
            const bool jonossaKorvaava = tila == Vero::LahetysTila::KASITTELYSSA
                    && tieto.vero.value(QStringLiteral("korvaava")).toBool();
            if (tila == Vero::LahetysTila::KASITELTY || jonossaKorvaava) {
                int valittu = Vero::korvauksenSyyt.indexOf(tieto.vero.value(QStringLiteral("korvauksenSyy")).toString());
                if (valittu < 0)
                    valittu = 0;
                bool ok = false;
                syy = QInputDialog::getItem(this, tr("Korvaavan ilmoituksen syy"),
                                            tr("Korvauksen syy (Verohallinnon koodi)"),
                                            Vero::korvauksenSyyt, valittu, false, &ok);
                if (!ok)
                    return;
                korvaava = true;
            }
        }
        varmistaJaLaheta(alvTositeId, loppuu, korvaava, syy, tila, tieto.vahvistus);
    });
    });
    tuoreus->kysy();
}

void VeroSivu::varmistaJaLaheta(int alvTositeId, const QDate &loppuu, bool korvaava, const QString &syy,
                                Vero::LahetysTila vahvistettuTila, const QString &vahvistus)
{
    const bool tuotanto = palvelu_->ymparisto() == Vero::TUOTANTO;

    QMessageBox kysymys(this);
    kysymys.setIcon(tuotanto ? QMessageBox::Warning : QMessageBox::Question);
    kysymys.setWindowTitle(tr("Lähetä alv-ilmoitus"));
    kysymys.setText(tr("Lähetetäänkö kauden päättyen %1 alv-ilmoitus Verohallintoon?").arg(loppuu.toString("dd.MM.yyyy")));
    kysymys.setInformativeText(tr("Asiakas: %1 (%2)\nYmpäristö: %3%4\n\n"
                                  "Verohallinto käsittelee ilmoitukset eräajona. Onnistunut lähetys tarkoittaa, että "
                                  "ilmoitus on vastaanotettu, ei vielä käsitelty.")
                               .arg(kp()->asetukset()->nimi(), VeroPalvelu::asiakkaanYtunnus(),
                                    Vero::ymparistoNimi(palvelu_->ymparisto()),
                                    korvaava ? tr("\nKorvaava ilmoitus, syy: %1").arg(syy) : QString()));
    kysymys.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    kysymys.setDefaultButton(QMessageBox::No);
    if (kysymys.exec() != QMessageBox::Yes)
        return;

    kaynnissa_ = true;
    paivitaNapit();
    palvelu_->lahetaAlv(alvTositeId, loppuu, korvaava, syy, vahvistettuTila, vahvistus);
}

void VeroSivu::avaaTosite()
{
    const int rivi = valittuRivi();
    if (rivi < 0)
        return;
    const int tositeId = taulu_->item(rivi, ALKAA)->data(VeroTositeRooli).toInt();
    if (tositeId > 0)
        emit tositeValittu(tositeId);
}

void VeroSivu::toimintoValmis(bool ok)
{
    kaynnissa_ = false;
    kirjaa(ok ? tr("Valmis.") : tr("Valmis, mutta kaikki vaiheet eivät onnistuneet."));
    lataaIlmoitukset();
    paivitaNapit();
}
