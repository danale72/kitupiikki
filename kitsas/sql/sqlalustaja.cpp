/*
   Copyright (C) 2019 Arto Hyvättinen
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/
#include "sqlalustaja.h"

#include "db/tositetyyppimodel.h"
#include "model/tosite.h"

#include <QApplication>
#include <QDate>
#include <QFile>
#include <QJsonDocument>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QTextStream>

QString SqlAlustaja::json(const QVariant &var)
{
    return QString::fromUtf8( QJsonDocument::fromVariant(var).toJson(QJsonDocument::Compact) );
}

bool SqlAlustaja::suoritaSqlResurssi(QSqlDatabase db, const QString &resurssi)
{
    QSqlQuery query(db);

    QFile sqltiedosto(resurssi);
    if( !sqltiedosto.open(QIODevice::ReadOnly) ) {
        QMessageBox::critical(nullptr, QObject::tr("Kirjanpidon luominen epäonnistui"),
                              QObject::tr("SQL-resurssia %1 ei voitu avata.").arg(resurssi));
        return false;
    }
    QTextStream in(&sqltiedosto);

    QString sqluonti = in.readAll();
    sqluonti.replace("\n","");
    sqluonti.replace("\r","");
    QStringList sqlista = sqluonti.split(";");

    for(const QString& kysely : sqlista)
    {
        if(!kysely.isEmpty() &&  !query.exec(kysely))
        {
            qWarning() << "SQL-lause " << kysely << " epäonnistui ";
            QMessageBox::critical(nullptr, QObject::tr("Kirjanpidon luominen epäonnistui"),
                                  QObject::tr("Virhe tietokantaa luotaessa: %1 (%2)").arg(query.lastError().text(), kysely) );
            return false;
        }
    }
    return true;
}

// Huom! Alustuksen aikana EI saa kutsua qApp->processEvents():ia. Tapahtumasilmukan
// pyöriessä käyttäjä voi esim. valita aloitussivulta toisen asiakkaan, jolloin
// PostgresModel avaa jaetun yhteyden uudelleen toiseen tietokantaan ja loput
// alustuksen kyselyt kirjoittuvat väärän asiakkaan kirjanpitoon.

bool SqlAlustaja::virhe(const QSqlQuery &kysely, const QString &kohde, QString *virheteksti)
{
    const QString teksti = QStringLiteral("%1: %2").arg(kohde, kysely.lastError().text());
    qWarning() << "SqlAlustaja:" << teksti;
    if( virheteksti )
        *virheteksti = teksti;
    return false;
}

bool SqlAlustaja::aseta(QSqlDatabase db, const QString &avain, const QVariant &arvo, QString *virheteksti)
{
    QSqlQuery asetusKysely(db);
    asetusKysely.prepare("INSERT INTO Asetus(avain,arvo) VALUES(?,?)");
    asetusKysely.addBindValue(avain);
    if( arvo.toString().isEmpty()) {
        asetusKysely.addBindValue( json(arvo) );
    } else {
        asetusKysely.addBindValue(arvo);
    }
    if( !asetusKysely.exec() )
        return virhe(asetusKysely, QStringLiteral("Asetus %1").arg(avain), virheteksti);
    return true;
}

bool SqlAlustaja::kirjoitaAsetukset(QSqlDatabase db, const QVariantMap &asetukset, QString *virheteksti)
{
    QMapIterator<QString,QVariant> iter(asetukset);
    while( iter.hasNext() ) {
        iter.next();
        if( !aseta( db, iter.key(), iter.value(), virheteksti ) )
            return false;
    }
    return true;
}

bool SqlAlustaja::kirjoitaTilit(QSqlDatabase db, const QVariantList &tililista, QString *virheteksti)
{
    QSqlQuery otsikkoKysely(db);
    otsikkoKysely.prepare("INSERT INTO Otsikko(numero,taso,json) VALUES (?,?,?)");
    QSqlQuery tiliKysely(db);
    tiliKysely.prepare("INSERT INTO Tili(numero,tyyppi,iban,json) VALUES(?,?,?,?)");

    // Tilikarttatiedostossa voi olla sama tili tai otsikko kahdesti (esim.
    // yritys.kitsaskartta: otsikko 262/H4). Aiemmin jälkimmäinen kaatui
    // hiljaa avainrikkeeseen, joten säilytetään sama "ensimmäinen voittaa"
    // -käytös ohittamalla kaksoiskappaleet, eikä keskeytetä koko alustusta.
    QSet<QPair<int,int>> otsikot;
    QSet<int> tilit;

    for(const QVariant& var : tililista) {
        QVariantMap map = var.toMap();
        int numero = map.take("numero").toInt();
        QString tyyppi = map.take("tyyppi").toString();
        if( tyyppi.startsWith(QChar('H'))) {
            const int taso = tyyppi.mid(1).toInt();
            if( otsikot.contains(qMakePair(numero, taso)) ) {
                qWarning() << "SqlAlustaja: tilikartassa kaksinkertainen otsikko" << numero << tyyppi << "- ohitetaan";
                continue;
            }
            otsikot.insert(qMakePair(numero, taso));
            otsikkoKysely.addBindValue(numero);
            otsikkoKysely.addBindValue( taso );
            otsikkoKysely.addBindValue( json(map) );
            if( !otsikkoKysely.exec() )
                return virhe(otsikkoKysely, QStringLiteral("Otsikko %1 (%2)").arg(numero).arg(tyyppi), virheteksti);
        } else {
            if( tilit.contains(numero) ) {
                qWarning() << "SqlAlustaja: tilikartassa kaksinkertainen tili" << numero << "- ohitetaan";
                continue;
            }
            tilit.insert(numero);
            tiliKysely.addBindValue(numero);
            tiliKysely.addBindValue(tyyppi);
            tiliKysely.addBindValue( map.take("iban"));
            tiliKysely.addBindValue( json(map) );
            if( !tiliKysely.exec() )
                return virhe(tiliKysely, QStringLiteral("Tili %1").arg(numero), virheteksti);
        }
    }
    return true;
}

bool SqlAlustaja::kirjoitaAvausTosite(QSqlDatabase db, const QDate &tilinavauspaiva, QString *virheteksti)
{
    QSqlQuery avauskysely(db);
    avauskysely.prepare("INSERT INTO Tosite (pvm,tyyppi,tila,tunniste,otsikko) "
                        "VALUES (?,?,?,1,'Tilinavaus') ");
    avauskysely.addBindValue(tilinavauspaiva);
    avauskysely.addBindValue(TositeTyyppi::TILINAVAUS);
    avauskysely.addBindValue(Tosite::KIRJANPIDOSSA);
    if( !avauskysely.exec() )
        return virhe(avauskysely, QStringLiteral("Tilinavaustosite"), virheteksti);
    return true;
}

bool SqlAlustaja::kirjoitaTilikaudet(QSqlDatabase db, const QVariantList &kausilista, QString *virheteksti)
{
    QSqlQuery tilikausiKysely(db);
    tilikausiKysely.prepare("INSERT INTO Tilikausi(alkaa,loppuu,json) VALUES (?,?,?)");

    if( kausilista.count() > 1 &&
        !kirjoitaAvausTosite( db, kausilista.first().toMap().value("loppuu").toDate(), virheteksti ) )
        return false;

    for( const QVariant& var : kausilista) {
        QVariantMap map = var.toMap();
        const QDate alkaa = map.take("alkaa").toDate();
        const QDate loppuu = map.take("loppuu").toDate();
        tilikausiKysely.addBindValue( alkaa );
        tilikausiKysely.addBindValue( loppuu );
        tilikausiKysely.addBindValue( json(map) );
        if( !tilikausiKysely.exec() )
            return virhe(tilikausiKysely, QStringLiteral("Tilikausi %1 - %2")
                         .arg(alkaa.toString(Qt::ISODate), loppuu.toString(Qt::ISODate)), virheteksti);
    }
    return true;
}

bool SqlAlustaja::kirjoitaInit(QSqlDatabase db, const QVariantMap &initMap, QProgressDialog *progress, QString *virheteksti)
{
    const QVariantMap asetukset = initMap.value("asetukset").toMap();
    if( !kirjoitaAsetukset( db, asetukset, virheteksti) ||
        !kirjoitaTilit( db, initMap.value("tilit").toList(), virheteksti) ||
        !kirjoitaTilikaudet( db, initMap.value("tilikaudet").toList(), virheteksti ) )
        return false;
    // Vakiotilikartat asettavat LaskuSeuraavaId:n itse; oletus vain jos puuttuu
    // (aiemmin tämä lisäys kaatui hiljaa avainrikkeeseen).
    if( !asetukset.contains("LaskuSeuraavaId") &&
        !aseta(db, "LaskuSeuraavaId", 100, virheteksti) )
        return false;
    if( progress )
        progress->setValue(8);
    return true;
}
