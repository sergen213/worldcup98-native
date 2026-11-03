// Derived from fifa99-native (same author), ported for World Cup 98
#pragma once
#include "common.hpp"

#include "wc98/config/settings.hpp"

#include <QMainWindow>

#include <functional>
#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QProcess;
class QStackedWidget;

namespace wc98::launcher {

// State shared by the pages.
struct Context {
    config::Settings settings;
    std::vector<std::string> loadWarnings;
    Paths paths;
    std::vector<std::string> missingFeatures; // from the last validation, for Troubleshooting
    std::function<void()> changed;            // a page edited `settings`
    std::function<void(const QString&)> status;
    std::function<void()> refreshLogs;
};

class MainWindow : public QMainWindow {
  public:
    MainWindow();
    [[nodiscard]] int pageCount() const;
    [[nodiscard]] QString pageName(int i) const;
    void showPage(int i);
    bool saveSettings();

  protected:
    void closeEvent(QCloseEvent* e) override;

  private:
    void play();

    Context ctx_;
    bool dirty_ = false;
    QListWidget* nav_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* status_ = nullptr;
    QProcess* game_ = nullptr;
};

// Pages (pages.cpp / controls.cpp).
QWidget* makeGameFilesPage(Context& c);
QWidget* makeOptionsPage(Context& c);
QWidget* makeGraphicsPage(Context& c);
QWidget* makeAudioPage(Context& c);
QWidget* makeControlsPage(Context& c);
QWidget* makeLogsPage(Context& c);
QWidget* makeTroubleshootingPage(Context& c);

} // namespace wc98::launcher
