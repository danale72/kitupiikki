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
#ifndef VEROSETUSDIALOG_H
#define VEROSETUSDIALOG_H

#include <QDialog>
#include <QMap>

#include "veroasetukset.h"

class QComboBox;
class QLabel;
class QLineEdit;

/**
 * @brief Tilitoimiston Vero-yhteyden asetusten muokkaus
 *
 * Asetukset tallennetaan tälle koneelle, ei kirjanpitoon.
 */
class VeroSetusDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VeroSetusDialog(QWidget *parent = nullptr);

    void accept() override;

private slots:
    void ymparistoVaihtui();
    void valitseVarmenne();
    void valitseAvain();
    void paivitaVarmenteenTiedot();

private:
    void lataaKentat(Vero::Ymparisto ymparisto);
    void talletaLuonnos();
    Vero::Ymparisto valittu() const;

    QComboBox* ymparistoCombo_;
    QLineEdit* varmenneEdit_;
    QLineEdit* avainEdit_;
    QLineEdit* ohjelmaIdEdit_;
    QLineEdit* ohjelmaAvainEdit_;
    QLineEdit* tilitoimistoEdit_;
    QLabel* varmenneInfo_;

    Vero::Ymparisto naytettava_ = Vero::TESTI;
    /// Molempien ympäristöjen muokattavat arvot. Asetuksiin kirjoitetaan vasta OK:lla,
    /// joten Peruuta hylkää kaiken, myös ympäristön vaihdon jälkeen tehdyt muutokset.
    QMap<int, Vero::Asetukset> luonnokset_;
};

#endif // VEROSETUSDIALOG_H
