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
#ifndef VEROSIVU_H
#define VEROSIVU_H

#include <QDate>

#include "kitupiikkisivu.h"
#include "vero/veropalvelu.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class VeroPalvelu;

/**
 * @brief Verohallinnon asiointi avoinna olevan kirjanpidon puolesta
 *
 * Sivulta haetaan asiakkaalle osoitetut kirjeet ja alv-ilmoitukset sekä
 * lähetetään alv-ilmoitus. Vaihdetut sanomat tallentuvat tositteiksi.
 */
class VeroSivu : public KitupiikkiSivu
{
    Q_OBJECT
public:
    explicit VeroSivu(QWidget *parent = nullptr);

    void siirrySivulle() override;
    QString ohjeSivunNimi() override { return "vero"; }

signals:
    void tositeValittu(int tositeId);

private slots:
    void asetukset();
    void hae();
    void laheta();
    void avaaTosite();
    void paivitaNapit();
    void toimintoValmis(bool ok);
    void kirjanpitoVaihtui();

private:
    void lataaIlmoitukset();
    void paivitaTila(int rivi);
    int valittuRivi() const;
    void kirjaa(const QString& teksti);
    void varmistaJaLaheta(int alvTositeId, const QDate& loppuu, bool korvaava, const QString& syy,
                          Vero::LahetysTila vahvistettuTila, const QString& vahvistus);

    QLabel* otsikko_;
    QTableWidget* taulu_;
    QSpinBox* vuosiSpin_;
    QPushButton* haeNappi_;
    QPushButton* lahetaNappi_;
    QPushButton* tositeNappi_;
    QPushButton* asetusNappi_;
    QPlainTextEdit* loki_;

    VeroPalvelu* palvelu_;
    bool kaynnissa_ = false;
    quint64 sukupolvi_ = 0;     ///< Kasvaa kirjanpidon vaihtuessa, jolloin vanhat vastaukset hylätään
};

#endif // VEROSIVU_H
