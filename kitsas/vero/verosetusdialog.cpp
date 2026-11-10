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
#include "verosetusdialog.h"

#include "db/kirjanpito.h"
#include "veroclient.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

VeroSetusDialog::VeroSetusDialog(QWidget *parent) :
    QDialog(parent)
{
    setWindowTitle(tr("Verohallinnon yhteyden asetukset"));
    setMinimumWidth(560);

    ymparistoCombo_ = new QComboBox;
    ymparistoCombo_->addItem(Vero::ymparistoNimi(Vero::TESTI), Vero::TESTI);
    ymparistoCombo_->addItem(Vero::ymparistoNimi(Vero::TUOTANTO), Vero::TUOTANTO);

    varmenneEdit_ = new QLineEdit;
    avainEdit_ = new QLineEdit;
    ohjelmaIdEdit_ = new QLineEdit;
    ohjelmaAvainEdit_ = new QLineEdit;
    ohjelmaAvainEdit_->setEchoMode(QLineEdit::Password);
    tilitoimistoEdit_ = new QLineEdit;
    tilitoimistoEdit_->setPlaceholderText(tr("Tyhjänä käytetään varmenteen Y-tunnusta"));
    varmenneInfo_ = new QLabel;
    varmenneInfo_->setWordWrap(true);

    const auto tiedostoRivi = [](QLineEdit* edit, QPushButton* nappi) {
        QWidget* rivi = new QWidget;
        QHBoxLayout* asettelu = new QHBoxLayout(rivi);
        asettelu->setContentsMargins(0, 0, 0, 0);
        asettelu->addWidget(edit);
        asettelu->addWidget(nappi);
        return rivi;
    };
    QPushButton* varmenneNappi = new QPushButton(tr("Valitse..."));
    QPushButton* avainNappi = new QPushButton(tr("Valitse..."));

    QFormLayout* lomake = new QFormLayout;
    lomake->addRow(tr("Ympäristö"), ymparistoCombo_);
    lomake->addRow(tr("Asiakasvarmenne (PEM)"), tiedostoRivi(varmenneEdit_, varmenneNappi));
    lomake->addRow(QString(), varmenneInfo_);
    lomake->addRow(tr("Varmenteen yksityinen avain (PEM)"), tiedostoRivi(avainEdit_, avainNappi));
    lomake->addRow(tr("Ohjelman tunniste (Vero-SoftwareId)"), ohjelmaIdEdit_);
    lomake->addRow(tr("Ohjelman avain (Vero-SoftwareKey)"), ohjelmaAvainEdit_);
    lomake->addRow(tr("Tilitoimiston Y-tunnus"), tilitoimistoEdit_);

    QLabel* ohje = new QLabel(tr("Asetukset tallennetaan vain tälle koneelle (ohjelman avain selväkielisenä). "
                                 "Ne eivät tallennu kirjanpitoon eivätkä kulje sen mukana. "
                                 "Testi- ja tuotantoympäristöllä on omat varmenteensa ja tunnisteensa."));
    ohje->setWordWrap(true);

    QDialogButtonBox* napit = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(napit, &QDialogButtonBox::accepted, this, &VeroSetusDialog::accept);
    connect(napit, &QDialogButtonBox::rejected, this, &VeroSetusDialog::reject);

    QVBoxLayout* asettelu = new QVBoxLayout(this);
    asettelu->addLayout(lomake);
    asettelu->addWidget(ohje);
    asettelu->addWidget(napit);

    connect(varmenneNappi, &QPushButton::clicked, this, &VeroSetusDialog::valitseVarmenne);
    connect(avainNappi, &QPushButton::clicked, this, &VeroSetusDialog::valitseAvain);
    connect(varmenneEdit_, &QLineEdit::editingFinished, this, &VeroSetusDialog::paivitaVarmenteenTiedot);
    connect(avainEdit_, &QLineEdit::editingFinished, this, &VeroSetusDialog::paivitaVarmenteenTiedot);

    for (const Vero::Ymparisto ymparisto : {Vero::TESTI, Vero::TUOTANTO})
        luonnokset_.insert(ymparisto, Vero::Asetukset::lataa(kp()->settings(), ymparisto));

    naytettava_ = Vero::Asetukset::valittuYmparisto(kp()->settings());
    ymparistoCombo_->setCurrentIndex(naytettava_ == Vero::TUOTANTO ? 1 : 0);
    lataaKentat(naytettava_);

    // Yhdistetään vasta alkutilan jälkeen, ettei alustus kirjoita tyhjiä kenttiä luonnoksiin
    connect(ymparistoCombo_, &QComboBox::currentIndexChanged, this, &VeroSetusDialog::ymparistoVaihtui);
}

Vero::Ymparisto VeroSetusDialog::valittu() const
{
    return static_cast<Vero::Ymparisto>(ymparistoCombo_->currentData().toInt());
}

void VeroSetusDialog::lataaKentat(Vero::Ymparisto ymparisto)
{
    const Vero::Asetukset asetukset = luonnokset_.value(ymparisto);
    varmenneEdit_->setText(asetukset.varmenne);
    avainEdit_->setText(asetukset.avain);
    ohjelmaIdEdit_->setText(asetukset.ohjelmaId);
    ohjelmaAvainEdit_->setText(asetukset.ohjelmaAvain);
    tilitoimistoEdit_->setText(asetukset.tilitoimisto);
    naytettava_ = ymparisto;
    paivitaVarmenteenTiedot();
}

void VeroSetusDialog::talletaLuonnos()
{
    Vero::Asetukset luonnos;
    luonnos.ymparisto = naytettava_;
    luonnos.varmenne = varmenneEdit_->text().trimmed();
    luonnos.avain = avainEdit_->text().trimmed();
    luonnos.ohjelmaId = ohjelmaIdEdit_->text().trimmed();
    luonnos.ohjelmaAvain = ohjelmaAvainEdit_->text().trimmed();
    luonnos.tilitoimisto = tilitoimistoEdit_->text().trimmed();
    luonnokset_.insert(naytettava_, luonnos);
}

void VeroSetusDialog::ymparistoVaihtui()
{
    // Edellisen ympäristön kentät jäävät muistiin luonnoksena, mutta eivät asetuksiin
    talletaLuonnos();
    lataaKentat(valittu());
}

void VeroSetusDialog::valitseVarmenne()
{
    const QString polku = QFileDialog::getOpenFileName(this, tr("Valitse asiakasvarmenne"),
                                                       varmenneEdit_->text(),
                                                       tr("PEM-tiedostot (*.pem *.crt *.cer);;Kaikki tiedostot (*)"));
    if (!polku.isEmpty()) {
        varmenneEdit_->setText(polku);
        paivitaVarmenteenTiedot();
    }
}

void VeroSetusDialog::valitseAvain()
{
    const QString polku = QFileDialog::getOpenFileName(this, tr("Valitse yksityinen avain"),
                                                       avainEdit_->text(),
                                                       tr("PEM-tiedostot (*.pem *.key);;Kaikki tiedostot (*)"));
    if (!polku.isEmpty()) {
        avainEdit_->setText(polku);
        paivitaVarmenteenTiedot();
    }
}

void VeroSetusDialog::paivitaVarmenteenTiedot()
{
    if (varmenneEdit_->text().trimmed().isEmpty() || avainEdit_->text().trimmed().isEmpty()) {
        varmenneInfo_->clear();
        return;
    }

    VeroClient asiakas;
    QString virhe;
    if (!asiakas.asetaVarmenne(varmenneEdit_->text().trimmed(), avainEdit_->text().trimmed(), &virhe)) {
        varmenneInfo_->setText(QString("<span style=\"color: red\">%1</span>").arg(virhe.toHtmlEscaped()));
        return;
    }

    const QDateTime vanhenee = asiakas.varmenne().expiryDate();
    const bool vanhentunut = vanhenee < QDateTime::currentDateTime();
    QString teksti = tr("Y-tunnus %1, %2, voimassa %3 asti")
            .arg(asiakas.varmenteenYtunnus().isEmpty() ? tr("(ei Y-tunnus)") : asiakas.varmenteenYtunnus(),
                 asiakas.onkoTestivarmenne() ? tr("testivarmenne") : tr("tuotantovarmenne"),
                 vanhenee.date().toString("dd.MM.yyyy"));
    if (vanhentunut)
        teksti = QString("<span style=\"color: red\">%1 – %2</span>").arg(teksti.toHtmlEscaped(), tr("VANHENTUNUT"));
    else
        teksti = teksti.toHtmlEscaped();
    varmenneInfo_->setText(teksti);
}

void VeroSetusDialog::accept()
{
    talletaLuonnos();

    // Molempien ympäristöjen Y-tunnukset tarkistetaan, koska molemmat tallennetaan
    for (const Vero::Ymparisto ymparisto : {Vero::TESTI, Vero::TUOTANTO}) {
        const QString tilitoimisto = luonnokset_.value(ymparisto).tilitoimisto;
        if (!tilitoimisto.isEmpty() && Vero::normalisoiYtunnus(tilitoimisto).isEmpty()) {
            if (ymparisto != valittu())
                ymparistoCombo_->setCurrentIndex(ymparisto == Vero::TUOTANTO ? 1 : 0);
            QMessageBox::warning(this, tr("Virheellinen Y-tunnus"),
                                 tr("Tilitoimiston Y-tunnus ei ole kelvollinen (%1).")
                                 .arg(Vero::ymparistoNimi(ymparisto)));
            return;
        }
    }

    for (const Vero::Ymparisto ymparisto : {Vero::TESTI, Vero::TUOTANTO}) {
        Vero::Asetukset asetukset = luonnokset_.value(ymparisto);
        asetukset.ymparisto = ymparisto;
        if (!asetukset.tilitoimisto.isEmpty())
            asetukset.tilitoimisto = Vero::normalisoiYtunnus(asetukset.tilitoimisto);
        asetukset.tallenna(kp()->settings());
    }
    Vero::Asetukset::asetaYmparisto(kp()->settings(), valittu());

    QDialog::accept();
}
