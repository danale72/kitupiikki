/*
   Copyright (C) 2019 Arto Hyvättinen
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/
#ifndef SQLMODEL_H
#define SQLMODEL_H

#include "db/yhteysmodel.h"
#include "sql/saldocache.h"

#include <QSqlDatabase>
#include <QPair>
#include <QStringList>

class SQLiteRoute;
class SQLiteKysely;

/**
 * @brief Yhteinen kanta paikallisille SQL-taustajärjestelmille (SQLite, PostgreSQL)
 *
 * Omistaa QSqlDatabase-yhteyden ja reitittää KpKyselyt samoille route-luokille.
 * SQLite-kohtaiset PRAGMA- ja tiedostotoiminnot jäävät SQLiteModeliin.
 */
class SqlModel : public YhteysModel
{
    Q_OBJECT

public:
    /**
     * @brief Käytössä oleva tietokantaversio
     *
     * Jos yritetään avata uudempaa, tulee virhe
     */
    static const int TIETOKANTAVERSIO = 24;

    ~SqlModel() override;

    KpKysely* kysely(const QString& polku = QString(),
                     KpKysely::Metodi metodi = KpKysely::GET) override;

    void sulje() override;

    QSqlDatabase tietokanta() const { return tietokanta_; }

    qlonglong oikeudet() const override;

    void reitita(SQLiteKysely *reititettavakysely, const QVariant& data);
    void reitita(SQLiteKysely* reititettavakysely, const QByteArray &ba, const QMap<QString,QString> &meta);

    virtual qint64 tietokannanKoko() const;

    /** Kirjoitetaanko jokainen kysely lokiin (qInfo). Oletuksena vain, jos ympäristömuuttuja
     *  KITSAS_KYSELYLOKI on asetettu: lokitus maksaa joka kyselyllä. */
    static void asetaKyselyLoki(bool lokitetaanko) { lokitaKyselyt__ = lokitetaanko; }

    /** Kaavioversion 24 jälkeen lisätyt indeksit (nimi -> CREATE INDEX -lause), samat kuin luo.sql:ssä */
    static QList<QPair<QString,QString>> lisaIndeksit();
    /** Kaaviosta poistetut tarpeettomat indeksit (PK/UNIQUE kattaa ne jo) */
    static QStringList poistetutIndeksit();

public slots:
    void tyhjennaValimuisti();

protected:
    SqlModel(const QString& ajuri, const QString& yhteysnimi, QObject *parent = nullptr);

    void lisaaRoute(SQLiteRoute *route);
    void varmistaUid();
    void merkitseAvatuksi();
    /**
     * @brief Luo puuttuvat lisäindeksit ja poistaa tarpeettomat avattaessa
     *
     * Ei nosta kaavioversiota: vanhemmat ohjelmaversiot avaavat kannan yhä.
     * Epäonnistuminen (esim. Postgresissa ei omistajan oikeuksia) ei estä avaamista.
     */
    void varmistaIndeksit();

    QSqlDatabase tietokanta_;

private:
    QList<SQLiteRoute*> routes_;
    SaldoCache saldoCache_;

    static inline bool lokitaKyselyt__ = qEnvironmentVariableIsSet("KITSAS_KYSELYLOKI");
};

#endif // SQLMODEL_H
