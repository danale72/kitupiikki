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

#include "selausmodel.h"

#include <QSqlQuery>
#include <QSqlError>
#include "db/kirjanpito.h"
#include "db/tositetyyppimodel.h"
#include "tositeselausmodel.h"

#include "model/eramap.h"

#include "sqlite/sqlitemodel.h"
#include <QDebug>
#include <QPalette>


SelausModel::SelausModel(QObject *parent) :
    QAbstractTableModel(parent)
{

}

int SelausModel::rowCount(const QModelIndex & /* parent */) const
{
    return rivit_.count();
}

int SelausModel::columnCount(const QModelIndex & /* parent */) const
{
    return 9;
}

QVariant SelausModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if( role == Qt::TextAlignmentRole)
        return QVariant( Qt::AlignCenter | Qt::AlignVCenter);
    else if( role != Qt::DisplayRole)
        return QVariant();
    else if( orientation == Qt::Horizontal )
    {
        switch (section)
        {
        case TOSITE:
            return tr("Tosite");
        case PVM :
            return tr("Pvm");
        case TILI:
            return tr("Tili");
        case DEBET :
            return tr("Debet");
        case KREDIT:
            return tr("Kredit");
        case ALV:
            return tr("Alv");
        case KOHDENNUS:
            return tr("Kohdennus");
        case KUMPPANI:
            return tr("Asiakas/Toimittaja");
        case SELITE:
            return tr("Selite");
        }
    }

    return QVariant( section + 1  );
}

QVariant SelausModel::data(const QModelIndex &index, int role) const
{
    if( !index.isValid())
        return QVariant();

    return rivit_.at(index.row()).data(index.column(), role, index.row() % 2 == 1);
}

void SelausModel::lataaSqlite(SQLiteModel* sqlite, const QDate &alkaa, const QDate &loppuu, int tili)
{

    QString tilistr = tili > 0 ? QString(" AND Vienti.tili=%1").arg(tili) : "";
    const QString ehto = QString("Tosite.tila >= 100 AND Vienti.pvm BETWEEN '%1' AND '%2' %3")
            .arg(alkaa.toString(Qt::ISODate), loppuu.toString(Qt::ISODate), tilistr);

    QString kysymys = QString("SELECT vienti.id AS id, vienti.pvm as pvm, vienti.tili as tili, debetsnt, kreditsnt, alvprosentti, alvkoodi, "
                    "selite, vienti.kohdennus as kohdennus, eraid, vienti.tosite as tosite_id, tosite.pvm as tosite_pvm, tosite.tunniste as tosite_tunniste,"
                    "tosite.tyyppi as tosite_tyyppi, tosite.sarja as tosite_sarja, "
                    "kumppani.nimi as kumppani_nimi, liitteita "
                    "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                    "LEFT OUTER JOIN Kumppani ON Vienti.kumppani=kumppani.id "
                    "LEFT OUTER JOIN (SELECT tosite, COUNT(id) AS liitteita FROM Liite GROUP BY tosite) AS lq ON Tosite.id=lq.tosite "
                    "WHERE %1 ORDER BY pvm").arg(ehto);

    // Lisätiedot haetaan koko selaukselle kerralla samalla rajauksella
    SelausApu apu;
    QSqlQuery apukysely( sqlite->tietokanta() );
    apukysely.setForwardOnly(true);
    const QString valitut = QString("SELECT Vienti.id FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id WHERE %1").arg(ehto);
    const QString valitutErat = QString("SELECT DISTINCT Vienti.eraid FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                                        "WHERE %1 AND Vienti.eraid IS NOT NULL").arg(ehto);

    if( kp()->kohdennukset()->merkkauksia()) {
        apukysely.exec(QString("SELECT vienti, kohdennus FROM Merkkaus WHERE vienti IN (%1)").arg(valitut));
        while( apukysely.next())
            apu.merkkaukset[ apukysely.value(0).toInt() ].append(
                        kp()->kohdennukset()->kohdennus(apukysely.value(1).toInt()).nimi());
    }

    apukysely.exec(QString("SELECT Vienti.id, Tosite.tyyppi, Tosite.tunniste, Tosite.sarja, Tosite.pvm "
                           "FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id WHERE Vienti.id IN (%1)").arg(valitutErat));
    while( apukysely.next()) {
        SelausApu::Era era;
        era.tyyppi = apukysely.value(1).toInt();
        era.tunniste = apukysely.value(2).toInt();
        era.sarja = apukysely.value(3).toString();
        era.pvm = apukysely.value(4).toDate();
        apu.erat.insert( apukysely.value(0).toInt(), era);
    }

    apukysely.exec(QString("SELECT Vienti.eraid FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id "
                           "WHERE Vienti.eraid IN (%1) AND Tosite.tila >= 100 "
                           "GROUP BY Vienti.eraid HAVING COALESCE(SUM(debetsnt),0) <> COALESCE(SUM(kreditsnt),0)").arg(valitutErat));
    while( apukysely.next())
        apu.avoimet.insert( apukysely.value(0).toInt() );

    beginResetModel();

    kaytetytTilit_.clear();
    rivit_.clear();

    QSqlQuery kysely( sqlite->tietokanta() );
    kysely.setForwardOnly(true);
    kysely.exec(kysymys);

    while(kysely.next()) {
        SelausRivi rivi(kysely, samakausi_, apu);
        rivit_.append(rivi);
        kaytetytTilit_.insert(rivi.getTili());
    }


    endResetModel();

}

int SelausModel::tili(int rivi) const
{
    return rivit_[rivi].getTili();
}

QString SelausModel::etsiTeksti(int rivi) const
{
    return rivit_[rivi].getEtsi();
}

QList<int> SelausModel::tiliLista() const
{
    QList<int> lista = kaytetytTilit_.values();
    std::sort(lista.begin(), lista.end());
    return lista;
}


void SelausModel::lataa(const QDate &alkaa, const QDate &loppuu, int tili)
{
    samakausi_ = kp()->tilikausiPaivalle(alkaa).alkaa() == kp()->tilikausiPaivalle(loppuu).alkaa();
    tiliselaus_ = tili;

    if( kp()->yhteysModel()) {

        SQLiteModel* sqlite = qobject_cast<SQLiteModel*>( kp()->yhteysModel() );
        if( sqlite ) {
            lataaSqlite(sqlite, alkaa, loppuu, tili);
        } else if( !ladataan_ && kp()->yhteysModel()->onkoOikeutta(YhteysModel::TOSITE_SELAUS | YhteysModel::RAPORTIT | YhteysModel::TILINPAATOS)){
            KpKysely *kysely = kpk("/viennit");
            if(kysely) {
                kysely->lisaaAttribuutti("alkupvm", alkaa);
                kysely->lisaaAttribuutti("loppupvm", loppuu);
                if(tili > -1)
                    kysely->lisaaAttribuutti("tili", tili);
                connect( kysely, &KpKysely::vastaus, this, &SelausModel::tietoSaapuu);
                ladataan_ = true;

                kysely->kysy();
            }
        }
    }
    return;
}

void SelausModel::tietoSaapuu(QVariant *var)
{

    beginResetModel();
    kaytetytTilit_.clear();

    QVariantList lista = var->toList();
    rivit_.clear();


    for(const auto& item : qAsConst( lista ))
    {
        QVariantMap map = item.toMap();
        SelausRivi rivi(map, samakausi_);
        rivit_.append(rivi);
        kaytetytTilit_.insert(rivi.getTili());
    }

    endResetModel();
    ladataan_ = false;
}

SelausRivi::SelausRivi(const QVariantMap &data, bool samakausi)
{
    QVariantMap tosite = data.value("tosite").toMap();

    vientiId = data.value("id").toInt();
    tositeId = tosite.value("id").toInt();
    tositeTunnus = kp()->tositeTunnus(tosite.value("tunniste").toInt(),
                                      tosite.value("pvm").toDate(),
                                      tosite.value("sarja").toString(),
                                      samakausi);
    vertailuTunnus = kp()->tositeTunnus(tosite.value("tunniste").toInt(),
                                        tosite.value("pvm").toDate(),
                                        tosite.value("sarja").toString(),
                                        samakausi,
                                        true);
    pvm = data.value("pvm").toDate();
    tositeTyyppi = tosite.value("tyyppi").toInt();
    tili = data.value("tili").toInt();
    debet = data.value("debet").toString();
    kredit = data.value("kredit").toString();
    alvKoodi = data.value("alvkoodi").toInt();
    alvProsentti = data.value("alvprosentti").toDouble();
    kumppani = data.value("kumppani").toMap().value("nimi").toString();
    selite = data.value("selite").toString();
    liitteita = data.value("liitteita").toInt();

    etsi = tositeTunnus + " " + kumppani + " " + selite;

    Kohdennus kohdennusObj =  kp()->kohdennukset()->kohdennus( data.value("kohdennus").toInt() );
    kohdennustyyppi = kohdennusObj.tyyppi();
    kohdennuskuvake = kohdennusObj.tyyppiKuvake();

    if(kohdennustyyppi)
       kohdennus = kohdennusObj.nimi();

    if( data.contains("merkkaukset")) {
        QStringList tagit;
        for( const auto& merkkausVar : data.value("merkkaukset").toList()) {
            tagit.append( kp()->kohdennukset()->kohdennus(merkkausVar.toInt()).nimi() );
        }
        if( !kohdennus.isEmpty())
            kohdennus.append(" ");
        kohdennus.append( tagit.join(", "));
    }
    EraMap era = data.value("era").toMap();
    if( era.eratyyppi() == EraMap::Lasku) {
        kohdennuskuvake = era.saldo() ? kp()->tositeTyypit()->kuvake( era.tositetyyppi() ) : QIcon(":/pic/ok.png");
        if( era.id() != data.value("id").toInt()) {
           if(!kohdennus.isEmpty())
                kohdennus.append(" ");
            kohdennus.append( kp()->tositeTunnus(era.tunniste(),
                                                 era.pvm(),
                                                 era.sarja(),
                                                 kp()->tilikaudet()->tilikausiPaivalle(era.pvm()).alkaa() == kp()->tilikaudet()->tilikausiPaivalle(pvm).alkaa() ));
        }
    } else if( era.eratyyppi() == EraMap::Asiakas) {
        kohdennuskuvake = QIcon(":/pic/mies.png");
    } else if( era.eratyyppi() == EraMap::Huoneisto) {
        kohdennuskuvake = QIcon(":/pic/talo.png");
        if(!kohdennus.isEmpty())
             kohdennus.append(" ");
        kohdennus.append(era.huoneistoNimi());
    }    
    viimeistele();
}

SelausRivi::SelausRivi(QSqlQuery &data, bool samakausi, const SelausApu &apu)
{
    vientiId = data.value("id").toInt();
    tositeId = data.value("tosite_id").toInt();
    tositeTunnus = kp()->tositeTunnus(data.value("tosite_tunniste").toInt(),
                                      data.value("tosite_pvm").toDate(),
                                      data.value("tosite_sarja").toString(),
                                      samakausi);
    vertailuTunnus = kp()->tositeTunnus(data.value("tosite_tunniste").toInt(),
                                        data.value("tosite_pvm").toDate(),
                                        data.value("tosite_sarja").toString(),
                                        samakausi,
                                        true);
    pvm = data.value("pvm").toDate();
    tositeTyyppi = data.value("tosite_tyyppi").toInt();
    tili = data.value("tili").toInt();
    debet = data.value("debetsnt").toLongLong();
    kredit = data.value("kreditsnt").toLongLong();
    alvKoodi = data.value("alvkoodi").toInt();
    alvProsentti = data.value("alvprosentti").toDouble();
    kumppani = data.value("kumppani_nimi").toString();
    selite = data.value("selite").toString();
    liitteita = data.value("liitteita").toInt();

    etsi = tositeTunnus + " " + kumppani + " " + selite;

    Kohdennus kohdennusObj = kp()->kohdennukset()->kohdennus(data.value("kohdennus").toInt());
    kohdennustyyppi = kohdennusObj.tyyppi();
    kohdennuskuvake = kohdennusObj.tyyppiKuvake();
    if(kohdennustyyppi)
       kohdennus = kohdennusObj.nimi();


    const QStringList tagit = apu.merkkaukset.value(vientiId);
    if( !tagit.isEmpty()) {
        if(!kohdennus.isEmpty())
            kohdennus.append(" ");
        kohdennus.append(tagit.join(", "));
    }

    int eraid = data.value("eraid").toInt();
    if( eraid) {

        if( eraid != vientiId) {
            auto era = apu.erat.constFind(eraid);
            if( era != apu.erat.constEnd()) {
                if(!kohdennus.isEmpty())
                    kohdennus.append(" ");
                kohdennuskuvake = kp()->tositeTyypit()->kuvake(era->tyyppi);
                kohdennus.append( kp()->tositeTunnus(era->tunniste,
                                                     era->pvm,
                                                     era->sarja,
                                                     kp()->tilikaudet()->tilikausiPaivalle(era->pvm).alkaa() == kp()->tilikaudet()->tilikausiPaivalle(pvm).alkaa() ));
            }
        }

        // Maksettu, kun erän saldo on nolla
        if( !apu.avoimet.contains(eraid))
            kohdennuskuvake = QIcon(":/pic/ok.png");
    }
    viimeistele();
}

void SelausRivi::viimeistele()
{
    pvmJarjestys = QString("%1 %2").arg( pvm.toString(Qt::ISODate), vertailuTunnus );
    if( tili)
        tiliTeksti = QString("%1 %2").arg(tili).arg(kp()->tilit()->nimi(tili));
}

QVariant SelausRivi::data(int sarake, int role, bool alternateColor) const
{
    if( role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch (sarake)
        {
            case SelausModel::TOSITE:
                return role == Qt::EditRole ? vertailuTunnus : tositeTunnus;
            case SelausModel::PVM:
            if( role == Qt::DisplayRole)
                return pvm;
            else
                return pvmJarjestys;

            case SelausModel::TILI:
            {
                if( role==Qt::EditRole) {
                    return tili;
                } else if(tili) {
                    return tiliTeksti;
                } else {
                    return QVariant();
                }

            }

            case SelausModel::DEBET:
            {
                if( role == Qt::EditRole)
                    return debet.cents();
                else
                    return debet.display(false);
            }

            case SelausModel::KREDIT:
            {

                if( role == Qt::EditRole)
                    return kredit.cents();
                else
                    return kredit.display(false);
            }

            case SelausModel::ALV:
            {
                if(alvProsentti)
                    return QString("%1 %").arg(alvProsentti,0,'f',1);
                else
                    return QString();
            }

            case SelausModel::SELITE:
                if( selite == kumppani )
                    return QVariant();
                else
                    return selite;

            case SelausModel::KUMPPANI: return kumppani;

            case SelausModel::KOHDENNUS : return kohdennus;

        }
    } else if( role == Qt::TextAlignmentRole)
    {
        if( sarake==SelausModel::KREDIT || sarake == SelausModel::DEBET || sarake == SelausModel::ALV)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        else
            return QVariant( Qt::AlignLeft | Qt::AlignVCenter);

    }
    else if( role == Qt::UserRole)
    {
        // UserRolena on tositeid, jotta selauksesta pääsee helposti tositteeseen
        return tositeId;
    } else if( role == SelausModel::EtsiRooli) {
        return etsi;
    }
    else if( role == Qt::DecorationRole && sarake == SelausModel::KOHDENNUS )
    {
        return QIcon(kohdennuskuvake);
    }
    else if( role == Qt::DecorationRole && sarake == SelausModel::PVM)
    {
        return kp()->tositeTyypit()->kuvake(tositeTyyppi);
    }
    else if( role == Qt::DecorationRole && sarake==SelausModel::TOSITE )
    {
        if( !tili)
            return QIcon(":/pic/oranssi.png");
        else if(  liitteita )
            return QIcon(":/pic/liite.png");
        else
            return QIcon(":/pic/tyhja.png");
    }
    else if( role == Qt::DecorationRole && sarake==SelausModel::ALV) {
        return kp()->alvTyypit()->kuvakeKoodilla(alvKoodi);
    } else if( role == TositeSelausModel::TositeTyyppiRooli) {
        return tositeTyyppi;
    } else if(role == SelausModel::TiliRooli) {
        return tili;
    } else if(role == Qt::BackgroundRole && !tili) {
        if( QPalette().base().color().lightness() > 128) {
            return alternateColor ? QBrush(QColor(255, 200, 77)) : QBrush(QColor(255,209,102));
        } else {
            return alternateColor ? QBrush(QColor(204, 41, 0)) : QBrush(QColor(255,51,0));
        }

    }


    return QVariant();
}
