// Derived from fifa99-native (same author), ported for World Cup 98
#include "mainwindow.hpp"

#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <filesystem>

namespace wc98::launcher {
namespace fs = std::filesystem;

MainWindow::MainWindow() {
    setWindowTitle("World Cup 98 Native Launcher");
    setWindowIcon(QIcon::fromTheme("wc98-native", QIcon(":/icon.png")));
    ctx_.paths = paths();
    try {
        auto r = config::load(ctx_.paths.settings);
        ctx_.settings = std::move(r.settings);
        ctx_.loadWarnings = std::move(r.warnings);
        dirty_ = r.migrated;
    } catch (const std::exception& e) {
        ctx_.loadWarnings.push_back(e.what());
    }
    ctx_.changed = [this] { dirty_ = true; };

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);
    auto* body = new QHBoxLayout;
    nav_ = new QListWidget;
    nav_->setFixedWidth(170);
    nav_->setIconSize({24, 24});
    stack_ = new QStackedWidget;
    body->addWidget(nav_);
    body->addWidget(stack_, 1);
    root->addLayout(body, 1);

    auto* bar = new QHBoxLayout;
    status_ = new QLabel;
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ctx_.status = [this](const QString& s) { status_->setText(s); };
    auto* save = new QPushButton("Save settings");
    auto* playBtn = new QPushButton(QIcon::fromTheme("media-playback-start"), "Play");
    playBtn->setDefault(true);
    playBtn->setMinimumWidth(120);
    bar->addWidget(status_, 1);
    bar->addWidget(save);
    bar->addWidget(playBtn);
    root->addLayout(bar);
    setCentralWidget(central);

    struct P {
        const char* name;
        const char* icon;
        QWidget* (*make)(Context&);
    };
    const P pages[] = {
        {"Game files", "folder-open", makeGameFilesPage},
        {"Match options", "configure", makeOptionsPage},
        {"Controls", "input-gaming", makeControlsPage},
        {"Graphics", "video-display", makeGraphicsPage},
        {"Audio", "audio-volume-high", makeAudioPage},
        {"Logs", "text-x-generic", makeLogsPage},
        {"Troubleshooting", "help-about", makeTroubleshootingPage},
    };
    for (auto& p : pages) {
        nav_->addItem(new QListWidgetItem(QIcon::fromTheme(p.icon), p.name));
        stack_->addWidget(p.make(ctx_));
    }
    connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    nav_->setCurrentRow(0);
    connect(save, &QPushButton::clicked, this, [this] {
        if (saveSettings()) status_->setText("Saved " + QString::fromStdString(ctx_.paths.settings.string()));
    });
    connect(playBtn, &QPushButton::clicked, this, [this] { play(); });

    if (!ctx_.loadWarnings.empty())
        status_->setText(
            QString("Settings: %1 problem(s) fixed with defaults, see Troubleshooting").arg(ctx_.loadWarnings.size()));
    else
        status_->setText("Settings: " + QString::fromStdString(ctx_.paths.settings.string()));
    resize(980, 680);
}

int MainWindow::pageCount() const { return stack_->count(); }
QString MainWindow::pageName(int i) const { return nav_->item(i)->text(); }
void MainWindow::showPage(int i) { nav_->setCurrentRow(i); }

bool MainWindow::saveSettings() {
    try {
        config::save(ctx_.paths.settings, ctx_.settings);
        dirty_ = false;
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Cannot save settings", e.what());
        return false;
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (dirty_ && !saveSettings()) {
        e->ignore();
        return;
    }
    e->accept();
}

void MainWindow::play() {
    if (game_ && game_->state() != QProcess::NotRunning) {
        status_->setText("The game is already running.");
        return;
    }
    if (!saveSettings()) return;
    bool haveData = false;
    for (auto& d : ctx_.settings.game.data) haveData = haveData || fs::is_directory(d);
    if (!haveData) {
        QMessageBox::warning(this, "Game files needed",
                             "No game folder is configured. Add your World Cup 98 CD copy, Windows installation "
                             "folder or extracted ISO image on the Game files page.");
        showPage(0);
        return;
    }
    auto exe = findGame();
    if (exe.empty()) {
        QMessageBox::critical(this, "Game not found",
                              "The wc98-native executable was not found next to the launcher or in PATH.");
        return;
    }
    QDir().mkpath(QString::fromStdString(ctx_.paths.logs.string()));
    auto log = QString::fromStdString((ctx_.paths.logs / "").string()) +
               QDateTime::currentDateTime().toString("'wc98-'yyyyMMdd-HHmmss'.log'");
    delete game_;
    game_ = new QProcess(this);
    game_->setProcessChannelMode(QProcess::MergedChannels);
    game_->setStandardOutputFile(log);
    game_->setWorkingDirectory(QString::fromStdString(ctx_.paths.logs.string()));
    connect(game_, &QProcess::finished, this, [this, log](int code, QProcess::ExitStatus st) {
        status_->setText(st == QProcess::CrashExit
                             ? QString("wc98-native crashed — log: %1").arg(log)
                             : QString("wc98-native exited with code %1 — log: %2").arg(code).arg(log));
        if (ctx_.refreshLogs) ctx_.refreshLogs();
    });
    connect(game_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) { status_->setText("Cannot start wc98-native: " + game_->errorString()); });
    game_->start(QString::fromStdString(exe.string()), {});
    status_->setText("Running " + QString::fromStdString(exe.string()) + " — log: " + log);
}

} // namespace wc98::launcher
