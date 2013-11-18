#include "exporterdialog.h"
#include "ui_exporterdialog.h"

ExporterDialog::ExporterDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ExporterDialog)
{
    ui->setupUi(this);
}

ExporterDialog::~ExporterDialog()
{
    delete ui;
}

void ExporterDialog::setFormat(DltExporter::DltExportFormat exportFormat)
{
    if(exportFormat == DltExporter::FormatDlt)
        ui->radioButtonDlt->setChecked(true);
    else if(exportFormat == DltExporter::FormatAscii)
        ui->radioButtonAscii->setChecked(true);
    else if(exportFormat == DltExporter::FormatCsv)
        ui->radioButtonCsv->setChecked(true);
}

DltExporter::DltExportFormat ExporterDialog::getFormat()
{
    if(ui->radioButtonDlt->isChecked())
        return DltExporter::FormatDlt;
    if(ui->radioButtonAscii->isChecked())
        return DltExporter::FormatAscii;
    if(ui->radioButtonCsv->isChecked())
        return DltExporter::FormatCsv;
    return DltExporter::FormatDlt;
}

void ExporterDialog::setSelection(DltExporter::DltExportSelection exportSelection)
{
    if(exportSelection == DltExporter::SelectionAll)
        ui->radioButtonAll->setChecked(true);
    else if(exportSelection == DltExporter::SelectionFiltered)
        ui->radioButtonFiltered->setChecked(true);
    else if(exportSelection == DltExporter::SelectionSelected)
        ui->radioButtonSelection->setChecked(true);
}

DltExporter::DltExportSelection ExporterDialog::getSelection()
{
    if(ui->radioButtonAll->isChecked())
        return DltExporter::SelectionAll;
    if(ui->radioButtonFiltered->isChecked())
        return DltExporter::SelectionFiltered;
    if(ui->radioButtonSelection->isChecked())
        return DltExporter::SelectionSelected;
    return DltExporter::SelectionAll;
}
