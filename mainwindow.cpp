#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "initialtour.h"
#include "QSettings"

const std::string KeyFirstUse = "nuevo";

QSettings settings("Nodes");
bool nuevo = settings.value(KeyFirstUse, true).toBool();

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    if(nuevo) {
        initialTour *open = new initialTour(this);
        open->setAttribute(Qt::WA_DeleteOnClose);
        open->show();
        settings.setValue(KeyFirstUse, false);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}
