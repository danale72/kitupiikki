/*
   Copyright (C) 2019 Arto Hyvättinen
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/
#include "sqlmodel.h"

#include "db/kirjanpito.h"
#include "sqlite/sqlitekysely.h"
#include "sqlite/sqliteroute.h"

#include "sqlite/routes/initroute.h"
#include "sqlite/routes/tositeroute.h"
#include "sqlite/routes/viennitroute.h"
#include "sqlite/routes/kumppanitroute.h"
#include "sqlite/routes/liitteetroute.h"
#include "sqlite/routes/asetuksetroute.h"
#include "sqlite/routes/tilikaudetroute.h"
#include "sqlite/routes/saldotroute.h"
#include "sqlite/routes/asiakkaatroute.h"
#include "sqlite/routes/budjettiroute.h"
#include "sqlite/routes/eraroute.h"
#include "sqlite/routes/myyntilaskutroute.h"
#include "sqlite/routes/ostolaskutroute.h"
#include "sqlite/routes/toimittajatroute.h"
#include "sqlite/routes/kohdennusroute.h"
#include "sqlite/routes/tuotteetroute.h"
#include "sqlite/routes/tilitroute.h"
#include "sqlite/routes/alvroute.h"
#include "sqlite/routes/ryhmatroute.h"
#include "sqlite/routes/tuontitulkki.h"
#include "sqlite/routes/inforoute.h"
#include "sqlite/routes/vakioviiteroute.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QDebug>

SqlModel::SqlModel(const QString &ajuri, const QString &yhteysnimi, QObject *parent)
    : YhteysModel(parent)
{
    tietokanta_ = QSqlDatabase::addDatabase(ajuri, yhteysnimi);

    // Varmuuden vuoksi myös muualla kuin reittien kautta tehdyt muutokset tyhjentävät välimuistin.
    // Kirjanpito luo mallit konstruktorissaan, jolloin kp() ei vielä ole käytettävissä.
    Kirjanpito* kirjanpito = qobject_cast<Kirjanpito*>(parent);
    if( !kirjanpito )
        kirjanpito = kp();
    if( kirjanpito ) {
        connect( kirjanpito, &Kirjanpito::kirjanpitoaMuokattu, this, &SqlModel::tyhjennaValimuisti);
        connect( kirjanpito, &Kirjanpito::tietokantaVaihtui, this, &SqlModel::tyhjennaValimuisti);
    }

    lisaaRoute(new TositeRoute(this));
    lisaaRoute(new ViennitRoute(this));
    lisaaRoute(new KumppanitRoute(this));
    lisaaRoute(new LiitteetRoute(this));
    lisaaRoute(new InitRoute(this));
    lisaaRoute(new SaldotRoute(this));
    lisaaRoute(new TilikaudetRoute(this));
    lisaaRoute(new AsetuksetRoute(this));
    lisaaRoute(new AsiakkaatRoute(this));
    lisaaRoute(new BudjettiRoute(this));
    lisaaRoute(new EraRoute(this));
    lisaaRoute(new MyyntilaskutRoute(this));
    lisaaRoute(new OstolaskutRoute(this));
    lisaaRoute(new ToimittajatRoute(this));
    lisaaRoute(new KohdennusRoute(this));
    lisaaRoute(new TuotteetRoute(this));
    lisaaRoute(new TilitRoute(this));
    lisaaRoute(new RyhmatRoute(this));
    lisaaRoute(new TuontiTulkki(this));
    lisaaRoute(new AlvRoute(this));
    lisaaRoute(new VakioviiteRoute(this));
    lisaaRoute(new InfoRoute(this));
}

SqlModel::~SqlModel()
{
    for( auto route : routes_)
        delete route;
}

KpKysely *SqlModel::kysely(const QString &polku, KpKysely::Metodi metodi)
{
    return new SQLiteKysely(this, metodi, polku);
}

void SqlModel::tyhjennaValimuisti()
{
    saldoCache_.tyhjenna();
    for( SQLiteRoute* route : routes_)
        route->tyhjennaValimuisti();
}

void SqlModel::sulje()
{
    tyhjennaValimuisti();
    if( tietokanta_.isOpen()) {
        // Poistetaan vain tositetta odottamaan jääneet nimettömät liitteet: roolinimellinen
        // tosite=NULL -rivi on koko kirjanpidon liite (tilinpäätös, logo...), ks. LiitteetRoute
        tietokanta_.exec("DELETE FROM Liite WHERE tosite IS NULL AND roolinimi IS NULL");
        tietokanta_.close();
    }
}

qlonglong SqlModel::oikeudet() const
{
    return TOSITE_SELAUS |
            TOSITE_LUONNOS |
            TOSITE_MUOKKAUS |
            LASKU_SELAUS |
            LASKU_LAATIMINEN |
            LASKU_LAHETTAMINEN |
            ALV_ILMOITUS |
            BUDJETTI |
            TILINPAATOS |
            ASETUKSET |
            TUOTTEET |
            RYHMAT |
            RAPORTIT;
}

void SqlModel::reitita(SQLiteKysely* reititettavakysely, const QVariant &data)
{
    const QString& polku = reititettavakysely->polku();
    if( lokitaKyselyt__ )
        qInfo() << polku + " " + reititettavakysely->urlKysely().toString();

    QString valimuistiAvain;
    if( reititettavakysely->metodi() == KpKysely::GET ) {
        if( SaldoCache::onkoValimuistissa(polku) ) {
            valimuistiAvain = SaldoCache::avain(polku, reititettavakysely->urlKysely().toString());
            QVariant tulos;
            if( saldoCache_.hae(valimuistiAvain, &tulos)) {
                reititettavakysely->vastaa(tulos);
                return;
            }
        }
    } else {
        saldoCache_.tyhjenna();
    }

    for( SQLiteRoute* route : routes_) {
        if( polku.startsWith( route->polku() ) ) {
            const QVariant tulos = route->route( reititettavakysely, data);
            if( !valimuistiAvain.isEmpty())
                saldoCache_.tallenna(valimuistiAvain, tulos);
            reititettavakysely->vastaa(tulos);
            return;
        }
    }
    qWarning() << " *** Kyselyä " << reititettavakysely->polku() << " ei reititetty ***";
    emit reititettavakysely->virhe(404);
}

void SqlModel::reitita(SQLiteKysely *reititettavakysely, const QByteArray &ba, const QMap<QString, QString> &meta)
{
    saldoCache_.tyhjenna();
    for( SQLiteRoute* route : routes_) {
        if( reititettavakysely->polku().startsWith( route->polku() ) ) {
            reititettavakysely->vastaaLisayksesta( route->byteArray(reititettavakysely, ba, meta) );
            return;
        }
    }
    emit reititettavakysely->virhe(404);
}

qint64 SqlModel::tietokannanKoko() const
{
    return 0;
}

void SqlModel::lisaaRoute(SQLiteRoute *route)
{
    routes_.append(route);
}

void SqlModel::varmistaUid()
{
    QSqlQuery query( tietokanta_ );
    query.exec("SELECT Arvo FROM Asetus WHERE Avain='UID'");
    if(!query.next())
        query.exec(QString("INSERT INTO Asetus(Avain,Arvo) VALUES('UID','%1')").arg(Kirjanpito::satujono(16)));
}

void SqlModel::merkitseAvatuksi()
{
    tietokanta_.exec("UPDATE Asetus SET arvo=CURRENT_TIMESTAMP WHERE avain='Avattu'");
}

QList<QPair<QString, QString> > SqlModel::lisaIndeksit()
{
    return {
        { "vienti_eraid", "CREATE INDEX IF NOT EXISTS vienti_eraid ON Vienti (eraid)" },
        { "vienti_kumppani_tyyppi_tili", "CREATE INDEX IF NOT EXISTS vienti_kumppani_tyyppi_tili ON Vienti (kumppani, tyyppi, tili)" },
        { "vienti_tyyppi", "CREATE INDEX IF NOT EXISTS vienti_tyyppi ON Vienti (tyyppi)" },
        { "tosite_viite", "CREATE INDEX IF NOT EXISTS tosite_viite ON Tosite (viite)" },
        { "tosite_kumppani", "CREATE INDEX IF NOT EXISTS tosite_kumppani ON Tosite (kumppani)" },
        { "tosite_sarja_pvm", "CREATE INDEX IF NOT EXISTS tosite_sarja_pvm ON Tosite (sarja, pvm)" },
        { "tosite_tila_sarja", "CREATE INDEX IF NOT EXISTS tosite_tila_sarja ON Tosite (tila, sarja)" },
        { "kumppaniiban_kumppani", "CREATE INDEX IF NOT EXISTS kumppaniiban_kumppani ON KumppaniIban (kumppani)" },
        { "merkkaus_kohdennus", "CREATE INDEX IF NOT EXISTS merkkaus_kohdennus ON Merkkaus (kohdennus)" },
    };
}

QStringList SqlModel::poistetutIndeksit()
{
    // liite_tosite: UNIQUE(tosite, roolinimi); rivi_tosite: PRIMARY KEY(tosite, rivi)
    return { "liite_tosite", "rivi_tosite" };
}

void SqlModel::varmistaIndeksit()
{
    // Haetaan ensin olemassa olevat indeksit, jottei jokaisesta tarvitse tehdä omaa
    // kyselyään (etäpalvelimella jokainen kysely on verkkokierros)
    const bool postgres = tietokanta_.driverName() == QLatin1String("QPSQL");
    QSet<QString> olemassa;
    {
        // Luettelokysely on lopetettava ennen DROP/CREATE-lauseita: SQLite
        // pitää keskeneräistä lausetta skeeman lukkona.
        QSqlQuery kysely( tietokanta_ );
        if( postgres )
            kysely.exec("SELECT indexname FROM pg_indexes WHERE schemaname = current_schema()");
        else
            kysely.exec("SELECT name FROM sqlite_master WHERE type='index'");
        if( kysely.lastError().isValid())
            return;

        while( kysely.next())
            olemassa.insert( kysely.value(0).toString().toLower() );
    }

    QSqlQuery ddl( tietokanta_ );
    for(const auto& indeksi : lisaIndeksit()) {
        if( olemassa.contains(indeksi.first))
            continue;
        if( !ddl.exec(indeksi.second))
            qWarning() << "Indeksin" << indeksi.first << "luonti epäonnistui:" << ddl.lastError().text();
    }
    for(const QString& nimi : poistetutIndeksit()) {
        if( olemassa.contains(nimi) && !ddl.exec(QString("DROP INDEX IF EXISTS %1").arg(nimi)))
            qWarning() << "Indeksin" << nimi << "poisto epäonnistui:" << ddl.lastError().text();
    }
}
