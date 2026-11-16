#ifndef PDFRENDERVIEW_H
#define PDFRENDERVIEW_H

#include <QGraphicsView>
#include <QPdfDocument>
#include <QGraphicsScene>
#include <QPdfView>
#include <QVector>

class LiitteetModel;
class QGraphicsPixmapItem;
class QTimer;

class PdfRenderView : public QGraphicsView
{
    Q_OBJECT
public:
    PdfRenderView(QWidget* parent = nullptr);
    QPdfDocument *document();    

    void setZoom(QPdfView::ZoomMode mode, qreal factor);
    void setDocument(QPdfDocument *document);

protected:
    void statusChanged(QPdfDocument::Status status);

    /** Asettelee sivujen paikat; sivut renderöidään vasta näkyviin tullessaan */
    void asettele();
    void renderoiNakyvat();

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    void resizeEvent(QResizeEvent* event) override;

protected:
    QPdfDocument* doc_ = nullptr;
    QPdfView::ZoomMode zoomMode_ = QPdfView::ZoomMode::FitToWidth;
    qreal zoomFactor_ = 1.00;

    QVector<QRectF> sivut_;
    QVector<QGraphicsPixmapItem*> kuvat_;
    QTimer* kokoAjastin_ = nullptr;
};

#endif // PDFRENDERVIEW_H
