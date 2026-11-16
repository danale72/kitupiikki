#include "pdfrenderview.h"
#include "liite/liitteetmodel.h"
#include "qevent.h"
#include "qmimedata.h"
#include <QGraphicsPixmapItem>
#include <QScrollBar>
#include <QTimer>

PdfRenderView::PdfRenderView(QWidget *parent) :
    QGraphicsView(parent),
    kokoAjastin_(new QTimer(this))
{
    setScene(new QGraphicsScene(this));

    // Koon muuttuessa sivut asetellaan uudelleen vasta, kun koko on hetken pysynyt samana
    kokoAjastin_->setSingleShot(true);
    kokoAjastin_->setInterval(120);
    connect( kokoAjastin_, &QTimer::timeout, this, [this] {
        if( doc_ && doc_->status() == QPdfDocument::Status::Ready) asettele();
    });

    connect( verticalScrollBar(), &QScrollBar::valueChanged, this, &PdfRenderView::renderoiNakyvat);
    connect( horizontalScrollBar(), &QScrollBar::valueChanged, this, &PdfRenderView::renderoiNakyvat);
}

QPdfDocument *PdfRenderView::document()
{
    return doc_;
}

void PdfRenderView::setZoom(QPdfView::ZoomMode mode, qreal factor)
{
    zoomMode_ = mode;
    zoomFactor_ = factor;
    if( doc_) statusChanged( doc_->status());
}

void PdfRenderView::setDocument(QPdfDocument *document)
{
    doc_ = document;
    connect( doc_, &QPdfDocument::statusChanged, this, &PdfRenderView::statusChanged);
}

void PdfRenderView::statusChanged(QPdfDocument::Status status)
{
    if( status == QPdfDocument::Status::Unloading) return;

    scene()->setBackgroundBrush(QBrush(Qt::gray));
    scene()->clear();
    sivut_.clear();
    kuvat_.clear();
    if( !doc_ ) return;

    if( status == QPdfDocument::Status::Ready)
        asettele();
}

void PdfRenderView::asettele()
{
    scene()->clear();
    sivut_.clear();
    kuvat_.clear();
    if( !doc_ || doc_->status() != QPdfDocument::Status::Ready) return;

    double ypos = 0;
    double leveys = 1.0;

    for(int page = 0; page < doc_->pageCount(); page++) {
        QSizeF pointSize = doc_->pagePointSize(page);

        QSize size;

        if( zoomMode_ == QPdfView::ZoomMode::FitInView ) {
            size = pointSize.toSize().scaled( viewport()->width() - 20, viewport()->height() - 20, Qt::KeepAspectRatio );
        } else if( zoomMode_ == QPdfView::ZoomMode::FitToWidth) {
            const int areaWidth = viewport()->width() - 20;
            size = QSize(  areaWidth, pointSize.height() / pointSize.width() * areaWidth );
        } else {
            size = pointSize.toSize() * zoomFactor_;
        }
        if( size.width() < 1 || size.height() < 1)
            size = QSize(1, 1);

        const QRectF rect(0, ypos, size.width(), size.height());
        scene()->addRect(rect, QPen(QBrush(Qt::black), 2.00), QBrush(Qt::white) );
        sivut_.append(rect);
        kuvat_.append(nullptr);

        ypos += size.height() + 10.0;
        leveys = std::max(leveys, 1.0 * size.width());
    }
    scene()->setSceneRect(0, 0, leveys, ypos );
    renderoiNakyvat();
}

void PdfRenderView::renderoiNakyvat()
{
    if( !doc_ || doc_->status() != QPdfDocument::Status::Ready || sivut_.isEmpty()) return;

    // Näkyvät sivut sekä puolen näkymän verran ylä- ja alapuolelta
    QRectF nakyva = mapToScene(viewport()->rect()).boundingRect();
    nakyva.adjust(0, -nakyva.height() / 2, 0, nakyva.height() / 2);

    QPdfDocumentRenderOptions options;
    options.setRenderFlags( QPdfDocumentRenderOptions::RenderFlag::Annotations);

    for(int page = 0; page < sivut_.count(); page++) {
        if( kuvat_.at(page) || !sivut_.at(page).intersects(nakyva))
            continue;
        const QRectF& rect = sivut_.at(page);
        QImage image = doc_->render(page, rect.size().toSize(), options);
        QGraphicsPixmapItem *item = scene()->addPixmap(QPixmap::fromImage(image, Qt::DiffuseAlphaDither));
        item->setPos(rect.topLeft());
        kuvat_[page] = item;
    }
}

void PdfRenderView::dragEnterEvent(QDragEnterEvent *event)
{
    event->ignore();
}

void PdfRenderView::dragMoveEvent(QDragMoveEvent *event)
{
    event->ignore();
}

void PdfRenderView::dropEvent(QDropEvent *event)
{
    event->ignore();
}

void PdfRenderView::resizeEvent(QResizeEvent * event)
{
    QGraphicsView::resizeEvent(event);
    if( doc_ && zoomMode_ != QPdfView::ZoomMode::Custom)
        kokoAjastin_->start();
    else
        renderoiNakyvat();
}
