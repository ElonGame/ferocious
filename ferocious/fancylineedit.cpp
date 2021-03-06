#include <QToolButton>
#include <QStyle>
#include <QDebug>
#include <QInputDialog>
#include "fancylineedit.h"

FancyLineEdit::FancyLineEdit(QWidget *parent) : QLineEdit(parent)
{
    clearButton = new QToolButton(this);
    clearButton->resize(24,18);
    clearButton->setText("x");
    clearButton->setCursor(Qt::ArrowCursor);

    editButton = new QToolButton(this);
    editButton->resize(24,18);
    editButton->setText("...");
    editButton->setCursor(Qt::ArrowCursor);

    connect(clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(editButton, SIGNAL(clicked()),this, SLOT(on_editButton_Clicked()));
}

FancyLineEdit::~FancyLineEdit()
{

}

void FancyLineEdit::hideEditButton()
{
    editButton->hide();
}

void FancyLineEdit::hideClearButton()
{
    clearButton->hide();
}

void FancyLineEdit::showEditButton()
{
    editButton->show();
}

void FancyLineEdit::showClearButton()
{
    clearButton->show();
}

void FancyLineEdit::resizeEvent(QResizeEvent *)
{

    // put the Clear button on the far-right of the Line Edit, and position 2 pixels down from Line Edit;
    clearButton->move(QPoint(this->width()-clearButton->sizeHint().width(),2));
    clearButton->resize(QSize(clearButton->sizeHint().width()-2,this->height()-4));

    // put the Edit button to the left of the Clear button, and position 2 pixels down from Line Edit;
    editButton->move(QPoint(this->width()-2*editButton->sizeHint().width()+3,2));
    editButton->resize(QSize(editButton->sizeHint().width()-2,this->height()-4));
}

void FancyLineEdit::on_editButton_Clicked()
{
    bool ok=false;
    QString editedText = QInputDialog::getMultiLineText(this, "Edit multiple filenames", "Edit filenames below, and click 'OK' when done.", this->text(), &ok);
    if(ok){
        this->setText(editedText);
    }
}
