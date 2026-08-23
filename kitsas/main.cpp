/*
   Copyright (C) 2017 Arto Hyvättinen

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

#include <QApplication>
#include <QGuiApplication>
#include <QSplashScreen>
#include <QIcon>
#include <QFontDatabase>
#include <QFont>

#include "db/kirjanpito.h"
#include "sqlite/sqlitemodel.h"

#include "kitupiikkiikkuna.h"
#include "versio.h"
#include "kieli/kielet.h"

#include <QDebug>

#include <QStyle>
#include <QStyleFactory>
#include <QSettings>

#include <QFileInfo>
#include <QCommandLineParser>
#include <QtEnvironmentVariables>

#include "aloitussivu/tervetulodialogi.h"
#include "maaritys/ulkoasumaaritys.h"
#include "pilvi/pilvimodel.h"

#include "tools/kitsaslokimodel.h"
#include "aloitussivu/toffeelogin.h"

#include "laskutus/laskunuusinta.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationVersion(KITSAS_VERSIO);
#if defined(KITSAS_PG_BUILD)
    a.setOrganizationDomain("kitsas-pg.fi");
    a.setOrganizationName("Kitsas oy (PG-testi)");
#else
    a.setOrganizationDomain("kitsas.fi");
    a.setOrganizationName("Kitsas oy");
#endif

#if defined (Q_OS_LINUX)
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS","--disable-gpu");
#endif

    KitsasLokiModel::alusta();  

#if defined (Q_OS_WIN) || defined (Q_OS_MACX)
    a.setStyle(QStyleFactory::create("Fusion"));
#else
    // #120 GNOME-ongelmien takia ei käytetä Linuxissa natiiveja dialogeja
    a.setAttribute(Qt::AA_DontUseNativeDialogs);
#endif

    // Pakotetaan vaalea teema käyttöjärjestelmän tummasta tilasta riippumatta.
    // Fusion-tyylin standardPalette() seuraa Qt 6.5+:ssa järjestelmän
    // tumma/vaalea-asetusta, joten paletti rakennetaan käsin kiinteillä väreillä.
    // Lähtöväristä (napin väri) johdetaan Light/Dark/Mid/Midlight/Shadow-sävyt
    // automaattisesti, jotta esim. palette(mid):iin nojaava työkalupalkin tyyli
    // ei jää oletusarvoisesti mustaksi.
    QPalette vaaleaPaletti( QColor(239, 239, 239) );
    vaaleaPaletti.setColor(QPalette::Window, QColor(239, 239, 239));
    vaaleaPaletti.setColor(QPalette::WindowText, QColor(0, 0, 0));
    vaaleaPaletti.setColor(QPalette::Base, QColor(255, 255, 255));
    vaaleaPaletti.setColor(QPalette::AlternateBase, QColor(247, 247, 247));
    vaaleaPaletti.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
    vaaleaPaletti.setColor(QPalette::ToolTipText, QColor(0, 0, 0));
    vaaleaPaletti.setColor(QPalette::Text, QColor(0, 0, 0));
    vaaleaPaletti.setColor(QPalette::Button, QColor(239, 239, 239));
    vaaleaPaletti.setColor(QPalette::ButtonText, QColor(0, 0, 0));
    vaaleaPaletti.setColor(QPalette::BrightText, QColor(255, 0, 0));
    vaaleaPaletti.setColor(QPalette::Link, QColor(0, 0, 255));
    vaaleaPaletti.setColor(QPalette::Highlight, QColor(48, 140, 198));
    vaaleaPaletti.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    vaaleaPaletti.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    vaaleaPaletti.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    vaaleaPaletti.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    vaaleaPaletti.setColor(QPalette::Disabled, QPalette::Base, QColor(239, 239, 239));
    a.setPalette(vaaleaPaletti);

    QCommandLineParser parser;
    parser.addOptions({
                          {"api",
                           "Pilvipalvelun osoite",
                           "url",
                           KITSAS_API},
                          {"log",
                          "Lokitiedosto",
                          "tiedostopolku",
                          QString()},
                          {"pro",
                          "Kirjautuminen suoraan pilveen"},
                           {"demo",
                           "Demo-tila"},
                            {"noweb","Käytä aina ulkoista selainta"}
                      });
    parser.addVersionOption();
    parser.process(a);

#if defined(KITSAS_PG_BUILD)
    a.setApplicationName("Kitsas PG");
#else
    a.setApplicationName( parser.isSet("pro") || PRO_VERSIO ? "Kitsas Pro" : "Kitsas");
#endif
#ifndef Q_OS_MACX
    a.setWindowIcon( parser.isSet("pro") || PRO_VERSIO ? QIcon(":/pic/propossu-64.png") : QIcon(":/pic/Possu64.png") );
#endif


    Kielet::alustaKielet(":/tr/tulkki.json");

    PilviModel::asetaPilviLoginOsoite( parser.value("api") );
    KitsasLokiModel::setLoggingToFile( parser.value("log") );
    a.setProperty("noweb", parser.isSet("noweb"));

    QStringList argumentit = qApp->arguments();

    // Windowsin asentamattomalla versiolla
    // asetukset kirjoitetaan kitupiikki.ini -tiedostoon

#if defined  (Q_OS_WIN) && defined (KITSAS_PORTABLE)

    QFileInfo info(argumentit.at(0));
    QString polku = info.absoluteDir().absolutePath();

    Kirjanpito kirjanpito(polku);
#else
    Kirjanpito kirjanpito;
#endif

    Kirjanpito::asetaInstanssi(&kirjanpito);

#if defined (Q_OS_WIN)
    // Kierretään Qt:n bugi resurssitiedostosta ladattujen fonttien käytössä
    // PDF-tiedostoa luotaessa kopioimalla fontti ensin tilapäistiedostoon

    QTemporaryDir dir;

    if (dir.isValid())    {

        dir.setAutoRemove(true);

        QString tempPolku = dir.path();

        QString sansPolku = tempPolku + "/FreeSans.ttf";
        QFile::copy(":/aloitus/FreeSans.ttf",sansPolku);
        QFontDatabase::addApplicationFont(sansPolku);

        QString monoPolku = tempPolku + "/FreeMono.ttf";
        QFile::copy(":/aloitus/FreeMono.ttf", monoPolku);
        QFontDatabase::addApplicationFont(monoPolku);

        QString code128Polku = tempPolku + "/code128_XL.ttf";
        QFile::copy(":/lasku/code128_XL.ttf", code128Polku);
        QFontDatabase::addApplicationFont(code128Polku);
    }

    else

    {
        QFontDatabase::addApplicationFont(":/aloitus/FreeSans.ttf");
        QFontDatabase::addApplicationFont(":/aloitus/FreeMono.ttf");
        QFontDatabase::addApplicationFont(":/lasku/code128_XL.ttf");
    }
#else

    QFontDatabase::addApplicationFont(":/aloitus/FreeSans.ttf");
    QFontDatabase::addApplicationFont(":/aloitus/FreeMono.ttf");
    QFontDatabase::addApplicationFont(":/lasku/code128_XL.ttf");

#endif

    // Fonttimääritykset
    UlkoasuMaaritys::oletusfontti__ = a.font();
    QString fonttinimi = kp()->settings()->value("Fontti").toString();
    if( !fonttinimi.isEmpty()) {
        a.setFont( QFont( fonttinimi, kp()->settings()->value("FonttiKoko").toInt()) );
    }

    if( parser.isSet("pro") ||  PRO_VERSIO ) {
        PilviKayttaja::asetaVersioMoodi(PilviKayttaja::PRO);
        ToffeeLogin loginDlg;
        if(loginDlg.keyExec() != QDialog::Accepted) {
            return 0;
        }        
    } else if( kp()->settings()->value("ViimeksiVersiolla").toString() != a.applicationVersion()  ) {
        TervetuloDialogi tervetuloa;
        if( tervetuloa.exec() != QDialog::Accepted)
            return 0;
        kp()->settings()->setValue("ViimeksiVersiolla", a.applicationVersion());
    }
    a.setProperty("demo", parser.isSet("demo"));

    QSplashScreen *splash = new QSplashScreen;
    splash->setPixmap( QPixmap(":/pic/splash_" + Kielet::instanssi()->uiKieli() + ".png"));
    splash->show();

    KitupiikkiIkkuna ikkuna;
    ikkuna.show();

    // Avaa argumenttina olevan tiedostonnimen
    if( !parser.positionalArguments().isEmpty() && QFile(parser.positionalArguments().value(0)).exists())
        kirjanpito.sqlite()->avaaTiedosto( parser.positionalArguments().value(0) );

    new LaskunUusinta(&kirjanpito);

    splash->finish( &ikkuna );
    delete splash;

    return a.exec();
}
