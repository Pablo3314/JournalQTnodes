#ifndef INITIALTOUR_H
#define INITIALTOUR_H

#include <QDialog>

namespace Ui {
class initialTour;
}

class initialTour : public QDialog
{
    Q_OBJECT

public:
    explicit initialTour(QWidget *parent = nullptr);
    ~initialTour();

private:
    Ui::initialTour *ui;
};

#endif // INITIALTOUR_H
