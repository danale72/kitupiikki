#include "cacheliite.h"

#include <QImage>
#include <QPainter>

CacheLiite::CacheLiite()
{

}

CacheLiite::CacheLiite(LiiteTila tila) :
    tila_{tila}
{

}

void CacheLiite::setData(const QByteArray &data)
{
    data_ = data;        
    tila_ = HAETTU;    
    thumb_ = QPixmap();
    thumbLaskettu_ = false;

}

void CacheLiite::asetaSeuraava(CacheLiite *seuraava)
{
    seuraava_ = seuraava;
}

void CacheLiite::asetaEdellinen(CacheLiite *edellinen)
{
    edellinen_ = edellinen;
}


void CacheLiite::lukitse()
{
    lukko_++;
}

void CacheLiite::vapauta()
{
    if(lukko_) lukko_--;
    if(!lukko_ && tila_ == KELVOTON) {
        data_.clear();
    }
}

bool CacheLiite::kaytettavissa() const
{
    return tila_ == HAETTU;
}

void CacheLiite::tyhjenna()
{
    data_.clear();    
    tila_ = TYHJENNETTY;
    thumb_ = QPixmap();
    thumbLaskettu_ = false;
}

QByteArray *CacheLiite::dataPtr()
{
    if( kaytettavissa() && lukossa())
        return &data_;
    else
        return nullptr;
}

QPixmap CacheLiite::thumb() const
{    
    if( tila_ != HAETTU)
        return QPixmap();
    if( thumbLaskettu_ )
        return thumb_;

    thumbLaskettu_ = true;
    // PDF:stä ei saa kuvaa QImagella; näytetään oletuskuvake
    if( data_.startsWith("%PDF"))
        return thumb_;

    QImage image = QImage::fromData(data_);
    if( image.isNull())
        return thumb_;
    image = image.scaled(64,64,Qt::KeepAspectRatio);
    QImage filled(image.size(), QImage::Format_RGB32);
    filled.fill(Qt::white);
    QPainter p(&filled);
    p.drawImage(image.rect(), image);
    p.end();
    thumb_ = QPixmap::fromImage(filled);
    return thumb_;
}
