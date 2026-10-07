/*
   Copyright (C) 2019 Arto Hyvättinen

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
#include "tervetulodialogi.h"
#include "ui_tervetuloa.h"
#include "kieli/kielet.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTextStream>

TervetuloDialogi::TervetuloDialogi() :
    ui(new Ui::TervetuloDlg)
{
    ui->setupUi(this);

#ifndef Q_OS_LINUX
    ui->valikkoonCheck->setVisible(false);
#endif

    if( Kielet::instanssi()->uiKieli() == "sv")
        ui->svKieli->setChecked(true);
    else if( Kielet::instanssi()->uiKieli() == "en")
        ui->enKieli->setChecked(true);
    else
        ui->fiKieli->setChecked(true);

    paivitaVersio();
    connect(ui->fiKieli, &QRadioButton::toggled, this, &TervetuloDialogi::kieliVaihtui );
    connect(ui->svKieli, &QRadioButton::toggled, this, &TervetuloDialogi::kieliVaihtui );
    connect(ui->enKieli, &QRadioButton::toggled, this, &TervetuloDialogi::kieliVaihtui );
}

void TervetuloDialogi::accept()
{
#ifdef Q_OS_LINUX
    // Ohjelman lisääminen käynnistysvalikkoon Linuxilla
    if( ui->valikkoonCheck->isChecked())
        linuxKaynnistysValikkoon();
#endif
    QDialog::accept();
}

void TervetuloDialogi::kieliVaihtui()
{
    Kielet::instanssi()->valitseUiKieli( valittuKieli() );
    ui->retranslateUi(this);
    paivitaVersio();
}


void TervetuloDialogi::paivitaVersio()
{
    ui->versioLabel->setText(tr("Versio %1").arg( qApp->applicationVersion() ));
}

QString TervetuloDialogi::valittuKieli() const
{
    if( ui->svKieli->isChecked())
        return "sv";
    else if( ui->enKieli->isChecked())
        return "en";
    else
        return "fi";
}

void TervetuloDialogi::linuxKaynnistysValikkoon()
{
    const QString nimi = QCoreApplication::applicationName();
    const QString desktopPolku = QDir::home().absoluteFilePath(".local/share/applications/" + nimi + ".desktop");
    const QString kuvakePolku = QDir::home().absoluteFilePath(".local/share/icons/" + nimi + ".png");

    // Poistetaan vanha, jotta päivittyisi
    QFile::remove( QDir::home().absoluteFilePath(".local/share/applications/Kitsas.desktop") );
    QFile::remove( QDir::home().absoluteFilePath(".local/share/applications/Kiswas.desktop") );
    QFile::remove( desktopPolku );
    // Kopioidaan kuvake
    QDir::home().mkpath( ".local/share/icons" );
    QFile::remove( kuvakePolku );
    QFile::copy(":/pic/Possu64.png", kuvakePolku);

    // Lisätään mimetyyppi
    QFile mime( QDir::home().absoluteFilePath(".local/share/mime/application/kitsas.xml"));
    mime.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QTextStream mout(&mime);    
    mout << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    mout << "<mime-type xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\" type=\"application/kitsas\">\n";
    mout << "<comment xml:lang=\"en\">Kitsas accounting database</comment>\n";
    mout << "<comment xml:lang=\"en\">Kitsaan kirjanpito</comment>\n";
    mout << "  <glob pattern=\"*.kitsas\"/>\n";
    mout << "</mime-type>";
    mout.flush();
    mime.close();


    // Lisätään työpöytätiedosto
    QFile desktop( desktopPolku );
    desktop.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QTextStream out(&desktop);    

    out << "[Desktop Entry]\nVersion=1.0\nType=Application\nName=" << nimi << " " << qApp->applicationVersion() << "\n";
    out << "Icon=" << kuvakePolku << "\n";
    out << "Exec=" << qApp->applicationFilePath() << "\n";
    out << "TryExec=" << qApp->applicationFilePath() << "\n";
    out << "GenericName=Kirjanpito\n";
    out << tr("Comment=Avoimen lähdekoodin kirjanpitäjä\n");
    out << "Categories=Office;Finance;Qt;\nMimeType=application/kitsas\nTerminal=false";
}
