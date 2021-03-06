#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "outputfileoptions_dialog.h"
#include "fancylineedit.h"
#include "lpfparametersdlg.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QProcess>
#include <QObject>
#include <QSettings>
#include <QDebug>
#include <QLineEdit>
#include <QDirIterator>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QCursor>
#include <QInputDialog>
#include <QStringList>

//#define RECURSIVE_DIR_TRAVERSAL
//#define MOCK_CONVERT

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->SamplerateCombo->setCurrentText("44100");

    readSettings();
    applyStylesheet(); // note: no-op if file doesn't exist, or file is factory default (":/ferocious.qss")

    if(ConverterPath.isEmpty()){
        ConverterPath=QDir::currentPath() + "/" + expectedConverter; // attempt to find converter in currentPath
    }

    if(!fileExists(ConverterPath)){
        QString s("Please locate the file: ");
        s.append(expectedConverter);

#if defined (Q_OS_WIN)
        QString filter = "*.exe";
#else
        QString filter = "";
#endif

        ConverterPath=QFileDialog::getOpenFileName(this,
                                                   s,
                                                   QDir::currentPath(),
                                                   filter);

        if(ConverterPath.lastIndexOf(expectedConverter,-1,Qt::CaseInsensitive)==-1){ // safeguard against wrong executable being configured
            ConverterPath.clear();
            QMessageBox::warning(this, tr("Converter Location"),tr("That is not the right program!\n"),QMessageBox::Ok);
        }

    }

    if(!fileExists(ConverterPath)){
        QMessageBox msgBox;
        QString s("The path to the required command-line program (");
        s.append(expectedConverter);
        s.append(") wasn't specified");
        msgBox.setText("Unable to locate converter");
        msgBox.setInformativeText(s);
        msgBox.exec();
        qApp->exit();
    }

    connect(&Converter, &QProcess::readyReadStandardOutput, this, &MainWindow::on_StdoutAvailable);
    connect(&Converter, &QProcess::started, this, &MainWindow::on_ConverterStarted);
    connect(&Converter,
            static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            static_cast<void(MainWindow::*)(int, QProcess::ExitStatus)>(&MainWindow::on_ConverterFinished)
    );

    // turn off the shitty etching on disabled widgets:
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(80, 80, 80));
    pal.setColor(QPalette::Disabled, QPalette::Light, QColor(0, 0, 0, 0));
    QApplication::setPalette(pal);

    // hide context-sensitive widgets:
    ui->AutoBlankCheckBox->setEnabled(ui->DitherCheckBox->isChecked());
    ui->AutoBlankCheckBox->setVisible(ui->DitherCheckBox->isChecked());
    ui->OutfileEdit->hideEditButton();
    ui->progressBar->setVisible(false);
    ui->statusBar->setVisible(false);

    // get converter version:
    getResamplerVersion();


    // retrieve dither profiles and add them to menu:
    populateDitherProfileMenu();


    // set up event filter:
    qApp->installEventFilter(this);

    // Set the separator for Multiple-files:
    MultiFileSeparator = "\n";

}

MainWindow::~MainWindow()
{
    writeSettings();
    delete ui;
}

bool MainWindow::fileExists(const QString& path) {
    QFileInfo fi(path);
    return (fi.exists() && fi.isFile());
}

void MainWindow::readSettings()
{
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"JuddSoft","Ferocious");

    settings.beginGroup("Paths");
    MainWindow::ConverterPath = settings.value("ConverterPath", MainWindow::ConverterPath).toString();
    if(ConverterPath.lastIndexOf(expectedConverter,-1,Qt::CaseInsensitive)==-1){ // safeguard against wrong executable being configured
        ConverterPath.clear();
    }
    MainWindow::inFileBrowsePath = settings.value("InputFileBrowsePath", MainWindow::inFileBrowsePath).toString();
    MainWindow::outFileBrowsePath = settings.value("OutputFileBrowsePath", MainWindow::outFileBrowsePath).toString();
    settings.endGroup();

    settings.beginGroup("Ui");
    ui->actionEnable_Tooltips->setChecked(settings.value("EnableToolTips",true).toBool());
    MainWindow::stylesheetFilePath = settings.value("StylesheetPath",":/ferocious.qss").toString();
    settings.endGroup();

    settings.beginGroup("CompressionSettings");
    MainWindow::flacCompressionLevel=settings.value("flacCompressionLevel", 5).toInt(); // flac default compression is 5
    MainWindow::vorbisQualityLevel=settings.value("vorbisQualityLevel", 3.0).toDouble(); // ogg vorbis default quality is 3
    settings.endGroup();

    settings.beginGroup("ConversionSettings");
    MainWindow::bDisableClippingProtection=settings.value("disableClippingProtection",false).toBool();
    ui->actionEnable_Clipping_Protection->setChecked(!bDisableClippingProtection);
    MainWindow::bEnableMultithreading=settings.value("enableMultithreading",false).toBool();
    ui->actionEnable_Multi_Threading->setChecked(bEnableMultithreading);
    settings.endGroup();

    settings.beginGroup("LPFSettings");
    MainWindow::LPFtype = (LPFType)settings.value("LPFtype",1).toInt();
    ui->actionRelaxedLPF->setChecked(false);
    ui->actionStandardLPF->setChecked(false);
    ui->actionSteepLPF->setChecked(false);
    ui->actionCustomLPF->setChecked(false);
    switch(LPFtype) {
    case relaxedLPF:
         ui->actionRelaxedLPF->setChecked(true);
        break;
    case steepLPF:
        ui->actionSteepLPF->setChecked(true);
        break;
    case customLPF:
        ui->actionCustomLPF->setChecked(true);
        ui->actionCustom_Parameters->setVisible(true);
        break;
    default:
         ui->actionStandardLPF->setChecked(true);
    }
    MainWindow::customLpfCutoff = settings.value("customLpfCutoff", 95.45).toDouble();
    MainWindow::customLpfTransition = settings.value("customLpfTransition", 4.55).toDouble();
    settings.endGroup();

    settings.beginGroup("advancedDitherSettings");
    MainWindow::bFixedSeed=settings.value("fixedSeed",false).toBool();
    ui->actionFixed_Seed->setChecked(MainWindow::bFixedSeed);
    MainWindow::seedValue=settings.value("seedValue",0).toInt();
    MainWindow::noiseShape=(NoiseShape)settings.value("noiseShape",noiseShape_standard).toInt();
    MainWindow::ditherProfile=settings.value("ditherProfile",-1).toInt();
    clearNoiseShapingMenu();
    if(ditherProfile == -1 /* none */) {
        if(noiseShape == noiseShape_flatTpdf)
             ui->actionNoiseShapingFlatTpdf->setChecked(true);
        else
            ui->actionNoiseShapingStandard->setChecked(true);
    }
    settings.endGroup();
    filenameGenerator.loadSettings(settings);
}

void MainWindow::writeSettings()
{
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"JuddSoft","Ferocious");

    settings.beginGroup("Paths");
    settings.setValue("ConverterPath", MainWindow::ConverterPath);
    settings.setValue("InputFileBrowsePath",MainWindow::inFileBrowsePath);
    settings.setValue("OutputFileBrowsePath",MainWindow::outFileBrowsePath);
    settings.endGroup();

    settings.beginGroup("Ui");
    settings.setValue("EnableToolTips",ui->actionEnable_Tooltips->isChecked());
    settings.setValue("StylesheetPath",MainWindow::stylesheetFilePath);
    settings.endGroup();

    settings.beginGroup("CompressionSettings");
    settings.setValue("flacCompressionLevel", MainWindow::flacCompressionLevel);
    settings.setValue("vorbisQualityLevel", MainWindow::vorbisQualityLevel);
    settings.endGroup();

    settings.beginGroup("ConversionSettings");
    settings.setValue("disableClippingProtection",MainWindow::bDisableClippingProtection);
    settings.setValue("enableMultithreading",MainWindow::bEnableMultithreading);
    settings.endGroup();

    settings.beginGroup("LPFSettings");
    settings.setValue("LPFtype",MainWindow::LPFtype);
    settings.setValue("customLpfCutoff", MainWindow::customLpfCutoff);
    settings.setValue("customLpfTransition", MainWindow::customLpfTransition);
    settings.endGroup();

    settings.beginGroup("advancedDitherSettings");
    settings.setValue("fixedSeed", MainWindow::bFixedSeed);
    settings.setValue("seedValue", MainWindow::seedValue);
    settings.setValue("noiseShape", MainWindow::noiseShape);
    settings.setValue("ditherProfile", MainWindow::ditherProfile);
    settings.endGroup();

    filenameGenerator.saveSettings(settings);
}

void MainWindow::on_StdoutAvailable()
{
    QString ConverterOutput(Converter.readAll());
    int progress = 0;

    // count backspaces at end of string:
    int backspaces = 0;
    while(ConverterOutput.at(ConverterOutput.length()-1)=='\b'){
        ConverterOutput.chop(1);
        ++backspaces;
    }

    if(backspaces){
        // extract percentage:
        QString whatToChop = ConverterOutput.right(backspaces);
        if(whatToChop.indexOf("%")!=-1){
            progress = whatToChop.replace("%","").toInt();
            ui->progressBar->setValue(progress);
        }
        ConverterOutput.chop(backspaces);
    }

    if(!ConverterOutput.isEmpty()){
        ui->ConverterOutputText->append(ConverterOutput);
    }
}

void MainWindow::on_ConverterStarted()
{
    ui->convertButton->setDisabled(true);
    ui->progressBar->setValue(0);
    if(bShowProgressBar)
        ui->progressBar->setVisible(true);
}

void MainWindow::on_ConverterFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);

    if(!MainWindow::conversionQueue.isEmpty()){
        MainWindow::convertNext();
        ui->progressBar->setValue(0);
    } else{
        ui->progressBar->setVisible(false);
        ui->StatusLabel->setText("Status: Ready");
        ui->convertButton->setEnabled(true);
    }
}

void MainWindow::on_browseInfileButton_clicked()
{   
    QString filenameSpec;
    QStringList fileNames = QFileDialog::getOpenFileNames(this,
        tr("Select Input File(s)"), inFileBrowsePath, tr("Audio Files (*.aif *.aifc *.aiff *.au *.avr *.caf *.dff *.dsf *.flac *.htk *.iff *.mat *.mpc *.oga *.paf *.pvf *.raw *.rf64 *.sd2 *.sds *.sf *.voc *.w64 *.wav *.wve *.xi)"));

    if(fileNames.isEmpty())
        return;

    if(fileNames.count() >=1){
        QDir path(fileNames.first());
        inFileBrowsePath = path.absolutePath(); // record path of this browse session
    }

    if(fileNames.count()==1){ // Single-file mode:
        ui->InfileLabel->setText("Input File:");
        ui->OutfileLabel->setText("Output File:");
        ui->OutfileEdit->setReadOnly(false);
        filenameSpec=fileNames.first();
        if(!filenameSpec.isNull()){

            ui->InfileEdit->setText(QDir::toNativeSeparators(filenameSpec));

            bool bRefreshOutFilename = true;

            // trigger a refresh of outfilename if outfilename is empty and infilename is not empty:
            if(ui->OutfileEdit->text().isEmpty() && !ui->InfileEdit->text().isEmpty())
                bRefreshOutFilename = true;

            // trigger a refresh of outfilename if outfilename has a wildcard and infilename doesn't have a wildcard:
            if((ui->OutfileEdit->text().indexOf("*")>-1) && (ui->InfileEdit->text().indexOf("*")==-1))
                bRefreshOutFilename = true;

            // conditionally auto-generate output filename:
            if(bRefreshOutFilename){
                QString outFileName;
                filenameGenerator.generateOutputFilename(outFileName,ui->InfileEdit->text());
                if(!outFileName.isNull() && !outFileName.isEmpty())
                    ui->OutfileEdit->setText(outFileName);
                    ui->OutfileEdit->update();
            }
        }

    } else { // Multiple-file Mode:

        // Join all the strings together, with MultiFileSeparator in between each string:
        QStringList::iterator it;
        for (it = fileNames.begin(); it != fileNames.end(); ++it){
            filenameSpec += QDir::toNativeSeparators(*it);
            filenameSpec += MultiFileSeparator;
        }

        ui->InfileEdit->setText(filenameSpec);
        QString firstFile =  QDir::toNativeSeparators(fileNames.first()); // get first filename in list (use to generate output filename)

        QString outFilename=firstFile; // use first filename as a basis for generating output filename
        int LastDot = outFilename.lastIndexOf(".");
        int LastSep = outFilename.lastIndexOf(QDir::separator());
        QString s = outFilename.mid(LastSep+1,LastDot-LastSep-1); // get what is between last separator and last '.'
        if(!s.isEmpty() && !s.isNull()){
            outFilename.replace(s,"*"); // replace everything between last separator and file extension with a wildcard ('*'):
        }
        filenameGenerator.generateOutputFilename(outFilename,outFilename); // Generate output filename by applying name-generation rules

        ui->OutfileEdit->setText(outFilename);
        ui->OutfileEdit->update();
        ui->OutfileLabel->setText("Output Files: (filenames auto-generated)");
        ui->OutfileEdit->setReadOnly(true);
        ui->InfileLabel->setText("Input Files:");
    }

    // trigger an update of options if file extension changed:
    ProcessOutfileExtension();
}

void MainWindow::on_convertButton_clicked()
{
    // split the QLineEdit text into a stringlist, using MainWindow::MultiFileSeparator
    QStringList filenames=ui->InfileEdit->text().split(MultiFileSeparator);

    QStringList::const_iterator it;
    for (it = filenames.begin(); it != filenames.end(); ++it){// iterate over the filenames, adding either a single conversion, or wildcard conversion at each iteration:

        QString inFilename=*it;

        if(!inFilename.isEmpty() && !inFilename.isNull()){

            // Search for Wildcards:
            if(ui->InfileEdit->text().lastIndexOf("*") > -1){ // Input Filename has wildcard
                MainWindow::wildcardPushToQueue(inFilename);
            }

            else{ // No Wildcard:
                conversionTask T;
                T.inFilename = inFilename;
                if(filenames.count()>1){ // multi-file mode:
                    filenameGenerator.generateOutputFilename(T.outFilename,inFilename);
                } else { // single-file mode:
                    T.outFilename = ui->OutfileEdit->text();
                }

                MainWindow::conversionQueue.push_back(T);
            }
        }
    }
    MainWindow::convertNext();
}

// wildcardPushtoQueue() - expand wildcard in filespec, and push matching filenames into queue:
void MainWindow::wildcardPushToQueue(const QString& inFilename){
    int inLastSepIndex = inFilename.lastIndexOf(QDir::separator());     // position of last separator in Infile
    int outLastSepIndex = ui->OutfileEdit->text().lastIndexOf(QDir::separator());   // position of last separator in Outfile

    QString inDir;
    QString outDir;
    QString tail;

    if(inLastSepIndex >-1){
        tail = inFilename.right(inFilename.length()-inLastSepIndex-1); // isolate everything after last separator

        // get input directory:
        inDir = inFilename.left(inLastSepIndex); // isolate directory

        // strip any wildcards out of directory name:
        inDir.replace(QString("*"),QString(""));

        // append slash to the end of Windows drive letters:
        if(inDir.length()==2 && inDir.right(1)==":")
            inDir += "\\";

    } else { // No separators in input Directory
        tail = inFilename;
        inDir = "";
    }

    if(outLastSepIndex >-1){
        // get output directory:
        outDir = ui->OutfileEdit->text().left(outLastSepIndex); // isolate directory

        // strip any wildcards out of directory name:
        outDir.replace(QString("*"),QString(""));
    }
    else
        outDir="";

    QString regexString(tail);// for building a regular expression to match against filenames in the input directory.

    // convert file-system symbols to regex symbols:
    regexString.replace(QString("."),QString("\\.")); // . => \\.
    regexString.replace(QString("*"),QString(".+"));  // * => .+
    QRegularExpression regex(regexString);

    // set up a FilenameGenerator for generating output file names:
    FilenameGenerator O(filenameGenerator); // initialize to default settings, as a fallback position.

    // initialize output directory:
    O.outputDirectory = QDir::toNativeSeparators(outDir);
    O.useSpecificOutputDirectory = true;

    // initialize output file extension:
    int outLastDot = ui->OutfileEdit->text().lastIndexOf(".");
    if(outLastDot > -1){
        O.fileExt = ui->OutfileEdit->text().right(ui->OutfileEdit->text().length()-outLastDot-1); // get file extension from file nam
        if(O.fileExt.lastIndexOf("*")>-1){ // outfile extension has a wildcard in it
            O.useSpecificFileExt = false;   // use source file extension
        }else{
            O.useSpecificFileExt = true;    // use file extension of outfile name
        }
    }else{ // outfile name has no file extension
        O.useSpecificFileExt = false; // use source file extension
    }

    // initialize output file suffix:
    // (use whatever is between last '*' and '.')
    int outLastStarBeforeDot = ui->OutfileEdit->text().left(outLastDot).lastIndexOf("*");
    if(outLastStarBeforeDot > -1){
        O.Suffix = ui->OutfileEdit->text().mid(outLastStarBeforeDot+1,outLastDot-outLastStarBeforeDot-1); // get what is between last '*' and last '.'
        O.appendSuffix = true;
    } else { // no Suffix
        O.Suffix="";
        O.appendSuffix = false;
    }

    // traverse input directory

#ifdef RECURSIVE_DIR_TRAVERSAL
    QDirIterator it(inDir, QDir::Files, QDirIterator::Subdirectories);
#else
    QDirIterator it(inDir, QDir::Files); // all files in inDir
#endif

    while (it.hasNext()) {

        QString nextFilename=QDir::toNativeSeparators(it.next());
        QRegularExpressionMatch match = regex.match(nextFilename);

        if (!match.hasMatch())
            continue; // no match ? move on to next file ...

        conversionTask T;
        T.inFilename = QDir::toNativeSeparators(nextFilename);
        O.generateOutputFilename(T.outFilename, T.inFilename);

        MainWindow::conversionQueue.push_back(T);
     }
}

// convertNext() - take the next conversion task from the front of the queue, convert it, then remove it from queue.
void MainWindow::convertNext(){
    if(!conversionQueue.empty()){
        conversionTask& nextTask = MainWindow::conversionQueue.first();
        ui->StatusLabel->setText("Status: processing "+nextTask.inFilename);
        ui->progressBar->setFormat("Status: processing "+nextTask.inFilename);
        this->repaint();
        MainWindow::convert(nextTask.outFilename,nextTask.inFilename);
        conversionQueue.removeFirst();
    }
}

// convert() - convert file infn to outfn, using current parameters
void MainWindow::convert(const QString &outfn, const QString& infn)
{
    QStringList args;

    // format args: Main
    args << "-i" << infn << "-o" << outfn << "-r" << ui->SamplerateCombo->currentText();

    // format args: Bit Format
    if(ui->BitDepthCheckBox->isChecked()){
        args << "-b" << ui->BitDepthCombo->currentText();
    }

    // format args: Normalization
    if(ui->NormalizeCheckBox->isChecked()){
        double NormalizeAmount=ui->NormalizeAmountEdit->text().toDouble();
        if((NormalizeAmount>0.0) && (NormalizeAmount<=1.0)){
            args << "-n" << QString::number(NormalizeAmount);
        }
    }

    // format args: Double Precision
    if(ui->DoublePrecisionCheckBox->isChecked())
        args << "--doubleprecision";

    // format args: Dithering
    if(ui->DitherCheckBox->isChecked()){

        if(!ui->DitherAmountEdit->text().isEmpty()){
            double DitherAmount=ui->DitherAmountEdit->text().toDouble();
            if((DitherAmount>0.0) && (DitherAmount<=8.0)){
                args << "--dither" << QString::number(DitherAmount);
            }
        }else{
            args << "--dither";
        }

        if(ui->AutoBlankCheckBox->isChecked())
            args << "--autoblank";
    }

    // format args: dither profile:
    if(MainWindow::ditherProfile != -1){
        args << "--ns" << QString::number(MainWindow::ditherProfile);
    }

    // format args: noise-shaping
    else if(MainWindow::noiseShape == noiseShape_flatTpdf){
        args << "--flat-tpdf";
    }

    // format args: seed
    if(MainWindow::bFixedSeed){
        args << "--seed" << QString::number(MainWindow::seedValue);
    }

    // format args: Minimum Phase
    if(ui->minPhase_radioBtn->isChecked()){
        args << "--minphase";
    }

    // format compression levels for compressed formats:
    int extidx = outfn.lastIndexOf(".");
    if(extidx > -1){ // filename must have a "." to contain a file extension ...
        QString ext = outfn.right(outfn.length()-extidx-1); // get file extension from file name

        if(ext.toLower()=="flac")// format args: flac compression
            args << "--flacCompression" << QString::number(MainWindow::flacCompressionLevel);

        if(ext.toLower()=="oga") // format args: vorbis compression
            args << "--vorbisQuality" << QString::number(MainWindow::vorbisQualityLevel);
    }

    // format args: --noClippingProtection
    if(bDisableClippingProtection){
        args << "--noClippingProtection";
    }

    // format args: --mt
    if(bEnableMultithreading){
        args << "--mt";
    }

    // format args: LPF type:
    switch(LPFtype) {
    case relaxedLPF:
        args << "--relaxedLPF";
        break;
    case steepLPF:
        args << "--steepLPF";
        break;
    case customLPF:
        args << "--lpf-cutoff" << QString::number(customLpfCutoff);
        args << "--lpf-transition" << QString::number(customLpfTransition);
        break;
    default:
        break;
    }

#ifndef MOCK_CONVERT
    Converter.setProcessChannelMode(QProcess::MergedChannels);
    Converter.start(ConverterPath,args);
#else
    qDebug() << ConverterPath << " " << args;
    QTimer::singleShot(200, [this] {
        on_ConverterFinished(0, QProcess::NormalExit);
    });
#endif
}

void MainWindow::on_InfileEdit_editingFinished()
{
    QString inFilename = ui->InfileEdit->text();

    if(inFilename.isEmpty()){

        // reset to single file mode:
        ui->InfileLabel->setText("Input File:");
        ui->OutfileLabel->setText("Output File:");
        ui->OutfileEdit->setReadOnly(false);

        // reset outFilename
        ui->OutfileEdit->clear();

        return;
    }

    bool bRefreshOutfileEdit = true; // control whether to always update output filename

    // look for Wildcard in filename, before file extension
    if(inFilename.indexOf("*")>-1){ // inFilename has wildcard
        int InLastDot =inFilename.lastIndexOf(".");
        if(InLastDot > -1){
            int InLastStarBeforeDot = inFilename.left(InLastDot).lastIndexOf("*");
            if(InLastStarBeforeDot > -1){ // Wilcard in Filename; trigger a refresh:
                bRefreshOutfileEdit = true;
              }
        }
    }

    else{ // inFilename does not have a wildcard
        if(ui->OutfileEdit->text().indexOf("*")>-1){ // outfilename does have a wildcard
            bRefreshOutfileEdit = true; // trigger a refresh
        }
    }

    if(ui->OutfileEdit->text().isEmpty() && !ui->InfileEdit->text().isEmpty())
        bRefreshOutfileEdit = true;

    QString outFilename;

    if(inFilename.right(1)==MultiFileSeparator){
        inFilename=inFilename.left(inFilename.size()-1); // Trim Multifile separator off the end
        ui->InfileEdit->setText(inFilename);
    }

    if(inFilename.indexOf(MultiFileSeparator)==-1){ // Single-file mode:

        ui->InfileLabel->setText("Input File:");
        ui->OutfileLabel->setText("Output File:");
        ui->OutfileEdit->setReadOnly(false);

        if(bRefreshOutfileEdit){
            filenameGenerator.generateOutputFilename(outFilename,ui->InfileEdit->text());
            if(!outFilename.isNull() && !outFilename.isEmpty())
                ui->OutfileEdit->setText(outFilename);
            ui->OutfileEdit->update();
        }
    }

    else { // multi-file mode:

        QString outFilename=inFilename.left(inFilename.indexOf(MultiFileSeparator)); // use first filename as a basis for generating output filename
        int LastDot = outFilename.lastIndexOf(".");
        int LastSep = outFilename.lastIndexOf(QDir::separator());
        QString s = outFilename.mid(LastSep+1,LastDot-LastSep-1); // get what is between last separator and last '.'
        if(!s.isEmpty() && !s.isNull()){
            outFilename.replace(s,"*"); // replace everything between last separator and file extension with a wildcard ('*'):
        }
        filenameGenerator.generateOutputFilename(outFilename,outFilename); // Generate output filename by applying name-generation rules
        ui->OutfileEdit->setText(outFilename);
        ui->OutfileLabel->setText("Output Files: (filenames auto-generated)");
        ui->OutfileEdit->setReadOnly(true);
        ui->OutfileEdit->update();
        ui->InfileLabel->setText("Input Files:");
    }

    // trigger an update of options if output file extension changed:
    ProcessOutfileExtension();
}

void MainWindow::on_browseOutfileButton_clicked()
{
    QString path = ui->OutfileEdit->text().isEmpty() ? outFileBrowsePath : ui->OutfileEdit->text(); // if OutfileEdit is populated, use that. Otherwise, use last output file browse path

    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Select Output File"), path, tr("Audio Files (*.aiff *.au *.avr *.caf *.dff *.dsf *.flac *.htk *.iff *.mat *.mpc *.oga *.paf *.pvf *.raw *.rf64 *.sd2 *.sds *.sf *.voc *.w64 *.wav *.wve *.xi)"));

    if(!fileName.isNull()){
        QDir path(fileName);
         outFileBrowsePath = path.absolutePath(); // remember this browse session (Unix separators)
       //  outFileBrowsePath = QDir::toNativeSeparators(path.absolutePath()); // remember this browse session (native separators)
        ui->OutfileEdit->setText(QDir::toNativeSeparators(fileName));
        PopulateBitFormats(ui->OutfileEdit->text());

        // trigger an update of options if file extension changed:
        ProcessOutfileExtension();
    }
}

void MainWindow::on_NormalizeCheckBox_clicked()
{
    ui->NormalizeAmountEdit->setEnabled(ui->NormalizeCheckBox->isChecked());
}

void MainWindow::on_NormalizeAmountEdit_editingFinished()
{
    double NormalizeAmount = ui->NormalizeAmountEdit->text().toDouble();
    if(NormalizeAmount <0.0 || NormalizeAmount >1.0)
        ui->NormalizeAmountEdit->setText("1.00");
}

void MainWindow::on_BitDepthCheckBox_clicked()
{
    ui->BitDepthCombo->setEnabled(ui->BitDepthCheckBox->isChecked());
}

// Launch external process, and populate QComboBox using output from the process:
void MainWindow::PopulateBitFormats(const QString& fileName)
{
    QProcess ConverterQuery;
    ui->BitDepthCombo->clear();
    int extidx = fileName.lastIndexOf(".");
    if(extidx > -1){
        QString ext = fileName.right(fileName.length()-extidx-1); // get file extension from file name
        ConverterQuery.start(ConverterPath, QStringList() << "--listsubformats" << ext); // ask converter for a list of subformats for the given file extension

        if (!ConverterQuery.waitForFinished())
            return;

        ConverterQuery.setReadChannel(QProcess::StandardOutput);
        while(ConverterQuery.canReadLine()){
            QString line = QString::fromLocal8Bit(ConverterQuery.readLine());
            ui->BitDepthCombo->addItem(line.simplified());
        }
    }
}

// Query Converter for version number:
void MainWindow::getResamplerVersion()
{
    QString v;
    QProcess ConverterQuery;

    ConverterQuery.start(ConverterPath, QStringList() << "--version"); // ask converter for its version number

    if (!ConverterQuery.waitForFinished())
        return;

    ConverterQuery.setReadChannel(QProcess::StandardOutput);
    while(ConverterQuery.canReadLine()){
        v += (QString::fromLocal8Bit(ConverterQuery.readLine())).simplified();

        // split the version number into components:
        QStringList ResamplerVersionNumbers = v.split(".");

        // set various options accoring to resampler version:
        int vB=ResamplerVersionNumbers[1].toInt(); // 2nd number
        bShowProgressBar = (vB >=1 )? true : false; // (no progress output on ReSampler versions prior to 1.1.0)
        ResamplerVersion=v;
    }
}

void MainWindow::on_OutfileEdit_editingFinished()
{
   ProcessOutfileExtension(); // trigger an update of options if user changed the file extension
}

// ProcessoutFileExtension() - analyze extension of outfile and update subformats dropdown accordingly
void MainWindow::ProcessOutfileExtension()
{

    QString fileName=ui->OutfileEdit->text();
    int extidx = fileName.lastIndexOf(".");
    if(extidx > -1){ // filename must have a "." to contain a file extension ...
        QString ext = fileName.right(fileName.length()-extidx-1); // get file extension from file name

        // if user has changed the extension (ie type) of the filename, then repopulate subformats combobox:
        if(ext != lastOutputFileExt){
            PopulateBitFormats(fileName);
            lastOutputFileExt=ext;
        }
    }
}

void MainWindow::on_DitherCheckBox_clicked()
{
    ui->DitherAmountEdit->setEnabled(ui->DitherCheckBox->isChecked());
    ui->AutoBlankCheckBox->setEnabled(ui->DitherCheckBox->isChecked());
    ui->AutoBlankCheckBox->setVisible(ui->DitherCheckBox->isChecked());
}

void MainWindow::on_DitherAmountEdit_editingFinished()
{
    double DitherAmount = ui->DitherAmountEdit->text().toDouble();
    if(DitherAmount <0.0 || DitherAmount >8.0)
        ui->DitherAmountEdit->setText("1.0");
}

void MainWindow::on_actionConverter_Location_triggered()
{
    QString s("Please locate the file: ");
    s.append(expectedConverter);

#if defined (Q_OS_WIN)
    QString filter = "*.exe";
#else
    QString filter = "";
#endif

    QString cp =QFileDialog::getOpenFileName(this, s, ConverterPath,  filter);

    if(!cp.isNull()){
        ConverterPath = cp;
        if(ConverterPath.lastIndexOf(expectedConverter,-1,Qt::CaseInsensitive)==-1){ // safeguard against wrong executable being configured
            ConverterPath.clear();
            QMessageBox::warning(this, tr("Converter Location"),tr("That is not the right program!\n"),QMessageBox::Ok);
        } else {
            // get converter version:
            getResamplerVersion();
        }
    }
}

void MainWindow::on_actionOutput_File_Options_triggered()
{
    OutputFileOptions_Dialog D(filenameGenerator);
    D.exec();
    on_InfileEdit_editingFinished(); // trigger change of output file if relevant
}

void MainWindow::on_actionAbout_triggered()
{
    QString info("Ferocious File Conversion\n By J.Niemann\n\n");

    info += "GUI Version: " + QString(APP_VERSION) + "\n";
    info += "Converter Vesion: " + ResamplerVersion + "\n";

    QMessageBox msgBox;
    msgBox.setText("About");
    msgBox.setInformativeText(info);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setIconPixmap(QPixmap(":/images/sine_sweep-32x32-buttonized.png"));
    msgBox.exec();
}

void MainWindow::on_actionAbout_Qt_triggered()
{
    QApplication::aboutQt();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::ToolTip) // Intercept tooltip event
        return (!ui->actionEnable_Tooltips->isChecked());

    else
        return QMainWindow::eventFilter(obj, event);	// pass control to base class' eventFilter
}

void MainWindow::on_actionFlac_triggered()
{
    QInputDialog D;
    D.setInputMode(QInputDialog::IntInput);
    D.setWindowTitle(tr("flac compression level"));
    D.setLabelText(tr("compression level (0-8):"));
    D.setIntMinimum(0);
    D.setIntMaximum(8);
    D.setIntValue(MainWindow::flacCompressionLevel);
    D.setIntStep(1);

    if(D.exec()==QDialog::Accepted)
        MainWindow::flacCompressionLevel = D.intValue();
}

void MainWindow::on_actionOgg_Vorbis_triggered()
{
    QInputDialog D;
    D.setInputMode(QInputDialog::DoubleInput);
    D.setWindowTitle( tr("vorbis quality level"));
    D.setLabelText(tr("quality level (-1 to 10):"));
    D.setDoubleRange(-1.0,10.0);
    D.setDoubleValue(MainWindow::vorbisQualityLevel);
    D.setDoubleDecimals(2);

    if(D.exec()==QDialog::Accepted)
        MainWindow::vorbisQualityLevel = D.doubleValue();
}

void MainWindow::on_actionEnable_Clipping_Protection_triggered()
{
    bDisableClippingProtection = !ui->actionEnable_Clipping_Protection->isChecked();
    qDebug() << bDisableClippingProtection;
}

void MainWindow::applyStylesheet() {

    if(stylesheetFilePath == ":/ferocious.qss") {
        // factory default
        qDebug() << "using factory default theme";
        return;
    }

    if(!fileExists(stylesheetFilePath)) {
        qDebug() << "stylesheet " << stylesheetFilePath << " doesn't exist";
        return;
    }

    QApplication* a = qApp;

    // retrieve and apply Stylesheet:
    QFile ss(stylesheetFilePath);
    if(ss.open(QIODevice::ReadOnly | QIODevice::Text)){
        a->setStyleSheet(ss.readAll());
        ss.close();
    }else{
        qDebug() << "Couldn't open stylesheet resource " << stylesheetFilePath;
    }

}

void MainWindow::on_actionTheme_triggered()
{
    stylesheetFilePath = QFileDialog::getOpenFileName(this,"Choose a Stylesheet",QDir::currentPath(),tr("Style Sheets (*.qss *.css)"));
    applyStylesheet();
}

void MainWindow::on_actionRelaxedLPF_triggered()
{
    LPFtype = relaxedLPF;
    ui->actionRelaxedLPF->setChecked(true);
    ui->actionStandardLPF->setChecked(false);
    ui->actionSteepLPF->setChecked(false);
    ui->actionCustomLPF->setChecked(false);
    ui->actionCustom_Parameters->setVisible(false);
}

void MainWindow::on_actionStandardLPF_triggered()
{
    LPFtype = standardLPF;
    ui->actionRelaxedLPF->setChecked(false);
    ui->actionStandardLPF->setChecked(true);
    ui->actionSteepLPF->setChecked(false);
    ui->actionCustomLPF->setChecked(false);
    ui->actionCustom_Parameters->setVisible(false);
}

void MainWindow::on_actionSteepLPF_triggered()
{
    LPFtype = steepLPF;
    ui->actionRelaxedLPF->setChecked(false);
    ui->actionStandardLPF->setChecked(false);
    ui->actionSteepLPF->setChecked(true);
    ui->actionCustomLPF->setChecked(false);
    ui->actionCustom_Parameters->setVisible(false);
}

void MainWindow::on_actionCustomLPF_triggered()
{
    LPFtype = customLPF;
    ui->actionRelaxedLPF->setChecked(false);
    ui->actionStandardLPF->setChecked(false);
    ui->actionSteepLPF->setChecked(false);
    if(ui->actionCustomLPF->isChecked()) {
        getCustomLpfParameters();
    }
    ui->actionCustomLPF->setChecked(true);
    ui->actionCustom_Parameters->setVisible(true);
}

void MainWindow::on_actionFixed_Seed_triggered()
{
    bFixedSeed = ui->actionFixed_Seed->isChecked();
}

void MainWindow::on_actionSeed_Value_triggered()
{
    QInputDialog D;
    D.setInputMode(QInputDialog::IntInput);
    D.setWindowTitle(tr("Choose Seed for Random Number Generator"));
    D.setLabelText(tr("Seed (-2,147,483,648 to 2,147,483,647):"));
    D.setIntMinimum(-2147483647 - 1); // note: compiler warning if you initialize with -2147483648 (because it tries to start with 2147483648 and then apply a unary minus)
    D.setIntMaximum(2147483647);
    D.setIntValue(MainWindow::seedValue);
    D.setIntStep(1);

    if(D.exec()==QDialog::Accepted)
        MainWindow::seedValue = D.intValue();
}

void MainWindow::on_actionEnable_Multi_Threading_triggered(bool checked)
{
    MainWindow::bEnableMultithreading = checked;
}

/*
note regarding Noise Shaping and Dither Profiles:
"Noise Shaping" refers to either standard or flat-tpdf
The two menu items "Standard" and "Flat TPDF" are always present.
"Dither Profile" refers to dither profiles to be issued with the --ns option
Dither Profiles were introduced much later in the development of ReSampler.
The additional "dither profiles" are only added to the menu if the version of ReSampler being used
has the capability.
*/

void MainWindow::on_actionNoiseShapingStandard_triggered()
{
    MainWindow::noiseShape = noiseShape_standard;
    clearNoiseShapingMenu();
    ui->actionNoiseShapingStandard->setChecked(true);
    MainWindow::ditherProfile = -1; // none
}

void MainWindow::on_actionNoiseShapingFlatTpdf_triggered()
{
    MainWindow::noiseShape = noiseShape_flatTpdf;
    clearNoiseShapingMenu();
    ui->actionNoiseShapingFlatTpdf->setChecked(true);
    MainWindow::ditherProfile = -1; // none
}

void MainWindow::on_action_DitherProfile_triggered(QAction* action, int id)
{
    clearNoiseShapingMenu();
    action->setChecked(true);
    ditherProfile = id;
}

void MainWindow::clearNoiseShapingMenu()
{
    QList<QAction*> nsActions = ui->menuNoise_Shaping->actions();
    for(int i=0; i<nsActions.count(); ++i )
    {
        nsActions[i]->setChecked(false);
    }
}

void MainWindow::populateDitherProfileMenu()
{

    QList<int> ignoreList;
    ignoreList << 0 << 6; // dither profiles to not add to menu (flat, standard)

    QMenu* nsMenu = ui->menuNoise_Shaping;

    // Launch external process, and populate Menu using output from the process:
    QProcess ConverterQuery;
    ConverterQuery.start(ConverterPath, QStringList() << "--showDitherProfiles");
    if (!ConverterQuery.waitForFinished() || (ConverterQuery.exitCode() != 0)) {
        // note: earlier versions of ReSampler that don't understand --showDitherProfiles
        // are expected to return exitCode of 1
        return;
    }

    ConverterQuery.setReadChannel(QProcess::StandardOutput);
    while(ConverterQuery.canReadLine()) {
        QString line = QString::fromLocal8Bit(ConverterQuery.readLine());
        QStringList fields = line.split(":");
        int id = fields.at(0).toInt();
        if(ignoreList.indexOf(id) == -1){
            QString label = fields.at(1).simplified();
            QAction* action = nsMenu->addAction(label);
            action->setCheckable(true);
            action->setChecked(id == ditherProfile);
            connect(action, &QAction::triggered, this, [=]() {
                this->on_action_DitherProfile_triggered(action, id);
            });
        }
    }
}

void MainWindow::on_actionCustom_Parameters_triggered()
{
    getCustomLpfParameters();
}

void MainWindow::getCustomLpfParameters() {
    auto d = new lpfParametersDlg(this);
    d->setValues(customLpfCutoff, customLpfTransition);
    d->setWindowTitle("Custom LPF Parameters");
    connect(d,&QDialog::accepted,[this, d]{
        auto v = d->getValues();
        customLpfCutoff = v.first;
        customLpfTransition = v.second;
    });
    d->exec();
}
