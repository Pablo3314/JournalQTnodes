#include "initialtour.h"
#include "ui_initialtour.h"

initialTour::initialTour(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::initialTour)
{
    ui->setupUi(this);
}

initialTour::~initialTour()
{
    delete ui;
}
