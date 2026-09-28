#ifndef BRANDMARK_H
#define BRANDMARK_H

#include <QWidget>

// The Bina lockup: mark + lowercase wordmark, painted (crisp at any scale).
// Sits in the corner above the track headers, where Butu keeps its brand.
class BrandMark : public QWidget
{
    Q_OBJECT

public:
    explicit BrandMark(QWidget* parent = nullptr);

    QSize sizeHint() const override { return QSize(120, 26); }

protected:
    void paintEvent(QPaintEvent* event) override;
};

#endif // BRANDMARK_H
