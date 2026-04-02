#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "initialtour.h"
#include "canvaswidget.h"

#include <QSettings>
#include <QResizeEvent>

const std::string KeyFirstUse = "nuevo";

QSettings settings("Nodes", "Nodes");
bool nuevo = settings.value(QString::fromStdString(KeyFirstUse), true).toBool();

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    if (nuevo) {
        initialTour *open = new initialTour(this);
        open->setAttribute(Qt::WA_DeleteOnClose);
        open->show();
        settings.setValue(QString::fromStdString(KeyFirstUse), false);
    }

    canvas = new CanvasWidget(ui->centralwidget);
    canvas->setGeometry(ui->centralwidget->rect());
    canvas->lower();
    canvas->show();

    resize(1280, 800);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (canvas && ui && ui->centralwidget) {
        canvas->setGeometry(ui->centralwidget->rect());
    }
}