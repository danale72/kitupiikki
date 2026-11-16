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
#include "asiakastoimittajalistamodel.h"

#include "db/kirjanpito.h"

AsiakasToimittajaListaModel::AsiakasToimittajaListaModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int AsiakasToimittajaListaModel::rowCount(const QModelIndex &parent) const
{
    // For list models only the root node (an invalid parent) should return the list's size. For all
    // other (valid) parents, rowCount() should return 0 so that it does not become a tree model.
    if (parent.isValid())
        return 0;


    return lista_.count();
}

QVariant AsiakasToimittajaListaModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if( role == Qt::DisplayRole || role == Qt::EditRole)
        return lista_.at(index.row()).nimi;
    if( role == IdRooli)
        return lista_.at(index.row()).id;


    return QVariant();
}

int AsiakasToimittajaListaModel::idAlvTunnuksella(const QString tunnus) const
{
    const_cast<AsiakasToimittajaListaModel*>(this)->varmistaLadattu();
    for(const auto& item : qAsConst( lista_ )) {
        if( item.alvtunnus == tunnus)
            return item.id;
    }
    return 0;
}

int AsiakasToimittajaListaModel::idNimella(const QString &nimi) const
{
    const_cast<AsiakasToimittajaListaModel*>(this)->varmistaLadattu();
    for(const auto& item : qAsConst( lista_ )) {
        if( item.nimi == nimi)
            return item.id;
    }
    return 0;
}

QString AsiakasToimittajaListaModel::nimi(int id) const
{
    const_cast<AsiakasToimittajaListaModel*>(this)->varmistaLadattu();
    for( const auto& item : lista_) {
        if( item.id == id)
            return item.nimi;
    }
    return QString();
}

AsiakasToimittajaListaModel *AsiakasToimittajaListaModel::instanssi()
{
    if( !instanssi__) {
        instanssi__ = new AsiakasToimittajaListaModel;

        connect(kp(), &Kirjanpito::kirjanpitoaMuokattu, instanssi__, &AsiakasToimittajaListaModel::vanhenna);
        connect(kp(), &Kirjanpito::tietokantaVaihtui, instanssi__, &AsiakasToimittajaListaModel::tyhjenna);
    }
    instanssi__->varmistaLadattu();
    return instanssi__;
}

void AsiakasToimittajaListaModel::varmistaLadattu()
{
    if( vanhentunut_ )
        lataa();
}

void AsiakasToimittajaListaModel::vanhenna()
{
    vanhentunut_ = true;
}

void AsiakasToimittajaListaModel::tyhjenna()
{
    beginResetModel();
    lista_.clear();
    vanhentunut_ = true;
    endResetModel();
}

void AsiakasToimittajaListaModel::lataa()
{
    KpKysely* kysely = kpk("/kumppanit");
    if( kysely ) {
        vanhentunut_ = false;
        connect( kysely, &KpKysely::vastaus, this, &AsiakasToimittajaListaModel::saapuu);
        kysely->kysy();
    }
}

void AsiakasToimittajaListaModel::saapuu(QVariant *variant)
{
    beginResetModel();
    lista_.clear();
    QVariantList lista = variant->toList();

    for( const auto& item : qAsConst( lista )) {
        QVariantMap map = item.toMap();
        lista_.append( Item( map.value("id").toInt(), map.value("nimi").toString(),
                             map.value("alvtunnus").toString()) );
    }

    endResetModel();
}

AsiakasToimittajaListaModel::Item::Item(int uId, QString uNimi, QString uAlvtunnus) :
    id(uId), nimi(uNimi), alvtunnus(uAlvtunnus)
{

}

AsiakasToimittajaListaModel* AsiakasToimittajaListaModel::instanssi__ = nullptr;
