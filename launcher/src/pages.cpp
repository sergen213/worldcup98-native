// Derived from fifa99-native (same author), ported for World Cup 98
// Game files, Match options, Graphics, Audio, Logs and Troubleshooting pages.
#include "mainwindow.hpp"

#include "wc98/assets/install.hpp"
#include "wc98/assets/manifest.hpp"
#include "wc98/file.hpp"
#include "wc98/formats/options.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSlider>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <format>
#include <cstdlib>
#include <optional>

namespace wc98::launcher {
namespace fs = std::filesystem;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
QString q(std::string_view s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
QString q(const fs::path& p) { return QString::fromStdString(p.string()); }
fs::path p(const QString& s) { return fs::path(s.toStdString()); }

QWidget* scrollPage(QWidget* inner) {
    auto* sa = new QScrollArea;
    sa->setWidget(inner);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    return sa;
}

QLabel* heading(const QString& text) {
    auto* l = new QLabel("<h2>" + text + "</h2>");
    return l;
}

void openFolder(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    QDesktopServices::openUrl(QUrl::fromLocalFile(q(dir)));
}

QPlainTextEdit* monoText() {
    auto* t = new QPlainTextEdit;
    t->setReadOnly(true);
    t->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    t->setLineWrapMode(QPlainTextEdit::NoWrap);
    return t;
}

template <class T, class F, class Done> void runAsync(QObject* owner, F work, Done done) {
    auto* w = new QFutureWatcher<T>(owner);
    QObject::connect(w, &QFutureWatcher<T>::finished, owner, [w, done] {
        done(w->future());
        w->deleteLater();
    });
    w->setFuture(QtConcurrent::run(work));
}

constexpr const char* kGameHelp =
    R"(<p><b>Supplying the game.</b> World Cup 98 Native contains no game content: it needs the data
of your own World Cup 98 (Win95 CD) files.</p>
<ul>
<li><b>CD copy / Windows installation:</b> add the folder that contains <tt>FIFAWC.EXE</tt> plus <tt>AUDIO</tt>,
<tt>FE</tt>, <tt>INGAME</tt> and <tt>COMMON</tt>.</li>
<li><b>ISO or ZIP image:</b> extract it with a standard tool (isoinfo, 7z, file-roller) and add the extracted folder;
this launcher detects images but does not unpack them.</li>
</ul>
<p><i>Validate</i> compares every data file with the known World Cup 98 CD sizes and SHA-256 hashes and lists what each
missing or damaged group of files costs. The game is playable when all required groups are intact. Installer payload
(<tt>SETUP/</tt>, <tt>DIRECTX5/</tt>) and the 1998 renderer plugin DLLs are not part of the required set.</p>)";

} // namespace

// ---- Game files -------------------------------------------------------------------------------

QWidget* makeGameFilesPage(Context& c) {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->addWidget(heading("Game files"));

    auto* dataBox = new QGroupBox("Game data (first usable folder is used)");
    auto* dv = new QVBoxLayout(dataBox);
    auto* list = new QListWidget;
    list->setMaximumHeight(140);
    dv->addWidget(list);
    auto* row = new QHBoxLayout;
    auto* addDir = new QPushButton("Add folder…");
    auto* remove = new QPushButton("Remove");
    auto* up = new QPushButton("Move up");
    auto* detect = new QPushButton("Auto-detect");
    for (auto* b : {addDir, remove, up, detect}) row->addWidget(b);
    row->addStretch();
    dv->addLayout(row);
    v->addWidget(dataBox);

    auto* actions = new QHBoxLayout;
    auto* validate = new QPushButton(QIcon::fromTheme("dialog-ok"), "Validate");
    actions->addWidget(validate);
    actions->addStretch();
    v->addLayout(actions);

    auto* summary = new QLabel("Not validated yet.");
    summary->setWordWrap(true);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(summary);
    auto* report = new QTreeWidget;
    report->setHeaderLabels({"Feature", "Status", "Files ok", "Bad", "Without it"});
    report->setRootIsDecorated(false);
    report->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    report->setMinimumHeight(170);
    v->addWidget(report, 1);

    auto* help = new QLabel(QString::fromUtf8(kGameHelp)); // trusted literal markup, no user data
    help->setWordWrap(true);
    v->addWidget(help);

    auto refill = [&c, list] {
        list->clear();
        for (auto& d : c.settings.game.data) {
            auto det = assets::detect(d);
            auto* it = new QListWidgetItem(QString("%1   [%2]").arg(q(d), q(assets::layoutName(det.layout))));
            it->setData(Qt::UserRole, q(d));
            it->setToolTip(q(det.detail));
            list->addItem(it);
        }
        if (list->count()) list->setCurrentRow(0);
    };
    auto addPath = [&c, refill](const fs::path& path) {
        auto& d = c.settings.game.data;
        if (std::ranges::find(d, path) == d.end()) d.push_back(path);
        c.changed();
        refill();
    };
    refill();

    QObject::connect(addDir, &QPushButton::clicked, page, [page, addPath] {
        auto d = QFileDialog::getExistingDirectory(page, "World Cup 98 folder (CD copy, installation or extracted image)");
        if (!d.isEmpty()) addPath(p(d));
    });
    QObject::connect(remove, &QPushButton::clicked, page, [&c, list, refill] {
        int r = list->currentRow();
        if (r < 0) return;
        c.settings.game.data.erase(c.settings.game.data.begin() + r);
        c.changed();
        refill();
    });
    QObject::connect(up, &QPushButton::clicked, page, [&c, list, refill] {
        int r = list->currentRow();
        if (r <= 0) return;
        std::swap(c.settings.game.data[r], c.settings.game.data[r - 1]);
        c.changed();
        refill();
        list->setCurrentRow(r - 1);
    });

    QObject::connect(detect, &QPushButton::clicked, page, [&c, page, detect, addPath] {
        detect->setEnabled(false);
        c.status("Searching ~/Games, removable media and " + q(c.paths.game) + "…");
        runAsync<std::vector<assets::Detection>>(
            page, [] { return assets::searchCommonLocations(); },
            [&c, page, detect, addPath](const QFuture<std::vector<assets::Detection>>& f) {
                detect->setEnabled(true);
                int added = 0;
                for (auto& d : f.result()) {
                    addPath(d.path);
                    ++added;
                }
                c.status(QString("Auto-detect: %1 game location(s) found").arg(added));
                if (!added)
                    QMessageBox::information(
                        page, "Nothing found",
                        "No World Cup 98 data was found in ~/Games, under /run/media or in the launcher's data folder. "
                        "Use Add folder.");
            });
    });

    QObject::connect(validate, &QPushButton::clicked, page, [&c, page, list, validate, summary, report] {
        int r = std::max(0, list->currentRow());
        if (!list->count()) {
            summary->setText("Add a game folder first.");
            return;
        }
        auto path = p(list->item(r)->data(Qt::UserRole).toString());
        validate->setEnabled(false);
        summary->setText("Validating " + q(path) + " (hashing every data file)…");
        report->clear();
        runAsync<assets::Report>(
            page, [path] { return assets::validate(path); },
            [&c, validate, summary, report](const QFuture<assets::Report>& f) {
                validate->setEnabled(true);
                assets::Report rep;
                try {
                    rep = f.result();
                } catch (const std::exception& e) {
                    summary->setText(
                        QString("<b>Validation failed:</b> %1").arg(q(std::string(e.what())).toHtmlEscaped()));
                    return;
                }
                c.missingFeatures.clear();
                for (auto& fs_ : rep.features) {
                    bool ok = fs_.bad == 0;
                    auto* it =
                        new QTreeWidgetItem({q(fs_.feature.name),
                                             ok                     ? "ok"
                                             : fs_.feature.required ? "MISSING (required)"
                                                                    : "missing",
                                             QString::number(fs_.ok), QString::number(fs_.bad), q(fs_.feature.effect)});
                    it->setForeground(1, ok                     ? QColor(0x2e, 0x8b, 0x3e)
                                         : fs_.feature.required ? QColor(0xc0, 0x20, 0x20)
                                                                : QColor(0xb0, 0x70, 0x00));
                    report->addTopLevelItem(it);
                    if (!ok)
                        c.missingFeatures.push_back(
                            std::format("{} ({} bad): {}", fs_.feature.name, fs_.bad, fs_.feature.effect));
                }
                for (int i = 0; i < report->columnCount() - 1; ++i) report->resizeColumnToContents(i);
                summary->setText(
                    QString("<b>%1</b> — %2. Layout: %3.")
                        .arg(q(rep.variant()).toHtmlEscaped(),
                             rep.playable ? "<span style='color:#2e8b3e'>playable</span>"
                                          : "<span style='color:#c02020'>not playable</span>",
                             q(assets::layoutName(rep.source.layout))));
                c.status(rep.playable ? "Game files validated: playable" : "Game files validated: NOT playable");
            });
    });
    return scrollPage(page);
}

// ---- Match options ------------------------------------------------------------------------------
// The original option block (USER/OPTIONS.SAV, docs/reverse-engineering/OPTIONS_SAV_WC98.md): the
// named pass-7 fields are exposed as raw numeric values (byte-level label semantics are a documented
// hypothesis until the M7 Wine oracle) and mirrored into settings.ini [gameplay] entries.

namespace {

struct OptField {
    const char* key;   // settings.ini [gameplay] key (parse lower-cases keys — keep these lowercase)
    const char* label; // UI label (field-cluster names from the pass-7 consumer map)
    int max;
};
constexpr std::array kOptFields{
    OptField{"stadium", "Stadium code (+0x08)", 0xffff},
    OptField{"daynight", "Day/night mode (+0x0a)", 0xff},
    OptField{"gamespeed", "Game speed (+0x14)", 2147483647},
    OptField{"detailfields", "Detail: fields (+0x18)", 2147483647},
    OptField{"detailstadium", "Detail: stadium (+0x1c)", 2147483647},
    OptField{"halflength", "Half length (+0x24)", 2147483647},
    OptField{"clockstyle", "Clock style (+0x28)", 2147483647},
    OptField{"volumeblock", "Volume block (+0x60)", 2147483647},
    OptField{"cameramode0", "Camera mode, view 0 (+0x64)", 2147483647},
    OptField{"cameramode1", "Camera mode, view 1 (+0x68)", 2147483647},
    OptField{"cameramode2", "Camera mode, view 2 (+0x6c)", 2147483647},
    OptField{"cameramode3", "Camera mode, view 3 (+0x70)", 2147483647},
    OptField{"cameramode4", "Camera mode, view 4 (+0x74)", 2147483647},
    OptField{"cameramode5", "Camera mode, view 5 (+0x78)", 2147483647},
};

std::uint64_t gameplayGet(const config::Settings& s, std::string_view key) {
    for (auto& e : s.gameplay)
        if (e.key == key) return strtoull(e.value.c_str(), nullptr, 10);
    return 0;
}
void gameplaySet(config::Settings& s, std::string_view key, std::uint64_t v) {
    for (auto& e : s.gameplay)
        if (e.key == key) { e.value = std::to_string(v); return; }
    s.gameplay.push_back({std::string(key), std::to_string(v)});
}

// First configured game tree's save block, when present.
fs::path optionsSavPath(const config::Settings& s) {
    for (auto& d : s.game.data)
        if (assets::detect(d).layout == assets::Layout::GameTree)
            if (auto f = findCaseInsensitive(d, "USER/OPTIONS.SAV"); !f.empty() && fs::is_regular_file(f)) return f;
    return {};
}

void applyOptionsToFields(const formats::OptionsSave& o, const std::vector<QSpinBox*>& spins, config::Settings& s) {
    const int vals[] = {int(o.stadium),   int(o.dayNight),  int(o.gameSpeed),     int(o.detailFields),
                        int(o.detailStadium), int(o.halfLength), int(o.clockStyle), int(o.volumeBlock),
                        int(o.cameraModes[0]), int(o.cameraModes[1]), int(o.cameraModes[2]),
                        int(o.cameraModes[3]), int(o.cameraModes[4]), int(o.cameraModes[5])};
    for (std::size_t i = 0; i < kOptFields.size(); ++i) {
        spins[i]->setValue(vals[i]); // spin signals write through to [gameplay] as usual
        gameplaySet(s, kOptFields[i].key, std::uint64_t(vals[i]));
    }
    gameplaySet(s, "lang", o.lang);
}

void readFieldsIntoOptions(const std::vector<QSpinBox*>& spins, formats::OptionsSave& o) {
    o.stadium = std::uint16_t(spins[0]->value());
    o.dayNight = std::uint8_t(spins[1]->value());
    o.gameSpeed = std::uint32_t(spins[2]->value());
    o.detailFields = std::uint32_t(spins[3]->value());
    o.detailStadium = std::uint32_t(spins[4]->value());
    o.halfLength = std::uint32_t(spins[5]->value());
    o.clockStyle = std::uint32_t(spins[6]->value());
    o.volumeBlock = std::uint32_t(spins[7]->value());
    for (int i = 0; i < 6; ++i) o.cameraModes[i] = std::uint32_t(spins[8 + i]->value());
}

} // namespace

QWidget* makeOptionsPage(Context& c) {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->addWidget(heading("Match options"));

    auto* info = new QLabel(
        "The 1998 option block (<tt>USER/OPTIONS.SAV</tt>): named fields per the pass-7 consumer map, shown as raw "
        "values — byte-level label semantics are a documented hypothesis until the Wine oracle (see "
        "<tt>docs/reverse-engineering/OPTIONS_SAV_WC98.md</tt>). Values are kept in settings.ini <tt>[gameplay]</tt> "
        "and passed through to the game untouched.");
    info->setWordWrap(true);
    v->addWidget(info);

    auto* form = new QFormLayout;
    std::vector<QSpinBox*> spins;
    for (auto& f : kOptFields) {
        auto* spin = new QSpinBox;
        spin->setRange(0, f.max);
        spin->setValue(int(gameplayGet(c.settings, f.key)));
        QObject::connect(spin, &QSpinBox::valueChanged, page, [&c, key = std::string(f.key), spin](int x) {
            gameplaySet(c.settings, key, std::uint64_t(x));
            c.changed();
        });
        form->addRow(q(std::string_view(f.label)), spin);
        spins.push_back(spin);
    }
    auto* lang = new QComboBox;
    for (int i = 0; i < 8; ++i) lang->addItem(QString::number(i)); // loader range-checks [0,8)
    {
        std::uint64_t l = gameplayGet(c.settings, "lang");
        lang->setCurrentIndex(int(l < 8 ? l : 0));
    }
    QObject::connect(lang, &QComboBox::currentIndexChanged, page, [&c, lang](int) {
        gameplaySet(c.settings, "lang", std::uint64_t(lang->currentData().isNull() ? lang->currentIndex()
                                                                                   : lang->currentData().toInt()));
        c.changed();
    });
    form->addRow("Language byte (+0x23):", lang);
    v->addLayout(form);

    auto* row = new QHBoxLayout;
    auto* importBtn = new QPushButton(QIcon::fromTheme("document-open"), "Import from OPTIONS.SAV…");
    auto* exportBtn = new QPushButton(QIcon::fromTheme("document-save"), "Export to OPTIONS.SAV…");
    row->addWidget(importBtn);
    row->addWidget(exportBtn);
    row->addStretch();
    v->addLayout(row);

    QObject::connect(importBtn, &QPushButton::clicked, page, [&c, page, spins, lang] {
        auto start = optionsSavPath(c.settings);
        auto f = QFileDialog::getOpenFileName(page, "World Cup 98 OPTIONS.SAV", q(start), "Save files (*.sav *.SAV)");
        if (f.isEmpty()) return;
        try {
            auto o = formats::OptionsSave::parse(readFile(p(f)));
            applyOptionsToFields(o, spins, c.settings);
            lang->setCurrentIndex(int(o.lang < 8 ? o.lang : 0));
            c.changed();
            c.status("Imported " + f + QString(" (%1 option bytes)").arg(o.raw.size()));
        } catch (const std::exception& e) {
            QMessageBox::critical(page, "Cannot import", QString("%1\n%2").arg(f, q(std::string(e.what()))));
        }
    });
    QObject::connect(exportBtn, &QPushButton::clicked, page, [&c, page, spins, lang] {
        auto def = optionsSavPath(c.settings);
        if (def.empty()) def = fs::path("options.sav");
        auto f = QFileDialog::getSaveFileName(page, "Write World Cup 98 OPTIONS.SAV", q(def),
                                              "Save files (*.sav *.SAV)");
        if (f.isEmpty()) return;
        try {
            // Start from the existing block when it parses, so every byte this page does not name
            // survives verbatim (the original saves the whole live DGROUP image).
            std::optional<std::vector<std::uint8_t>> prev;
            std::error_code ec;
            if (fs::is_regular_file(p(f), ec))
                try {
                    prev = formats::OptionsSave::parse(readFile(p(f))).raw;
                } catch (const std::exception&) {
                }
            auto o = prev ? formats::OptionsSave::parse(*prev) : formats::OptionsSave::defaults();
            readFieldsIntoOptions(spins, o);
            o.lang = std::uint8_t(lang->currentIndex());
            o.syncToRaw();
            wc98::writeFile(p(f), o.serialize());
            c.status("Exported " + f);
        } catch (const std::exception& e) {
            QMessageBox::critical(page, "Cannot export", QString("%1\n%2").arg(f, q(std::string(e.what()))));
        }
    });

    auto* note = new QLabel(
        "<b>Audio volumes</b> live on the Audio page (the port's mixer); the original +0x60 volume block above is "
        "the 1998 game's own fader state. <i>Import</i> fills this page from a 1998 save, <i>Export</i> writes one "
        "for Wine/reference testing.");
    note->setWordWrap(true);
    v->addWidget(note);
    v->addStretch();
    return scrollPage(page);
}

// ---- Graphics ---------------------------------------------------------------------------------

QWidget* makeGraphicsPage(Context& c) {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->addWidget(heading("Graphics"));
    auto* form = new QFormLayout;
    v->addLayout(form);

    auto& g = c.settings.graphics;

    auto enumCombo = [&](auto& field, std::initializer_list<std::pair<const char*, int>> items) {
        auto* cb = new QComboBox;
        for (auto& [label, val] : items) cb->addItem(label, val);
        cb->setCurrentIndex(std::max(0, cb->findData(int(field))));
        QObject::connect(cb, &QComboBox::currentIndexChanged, cb, [&c, &field, cb](int) {
            field = std::remove_reference_t<decltype(field)>(cb->currentData().toInt());
            c.changed();
        });
        return cb;
    };
    using config::Presentation, config::Aspect, config::WindowMode, config::TextureFilter;
    form->addRow(
        "Presentation:",
        enumCombo(g.presentation, {{"Original (640×480, pixel-exact 4:3)", int(Presentation::Original)},
                                   {"Enhanced (native resolution, MSAA, widescreen)", int(Presentation::Enhanced)}}));
    auto* res = new QComboBox;
    res->setEditable(true);
    res->addItem("desktop");
    for (auto r : {"640x480", "800x600", "1024x768", "1280x960", "1600x1200", "1280x720", "1920x1080", "2560x1440",
                   "3840x2160", "2560x1080", "3440x1440"})
        res->addItem(r);
    res->setCurrentText(g.width ? QString("%1x%2").arg(g.width).arg(g.height) : "desktop");
    QObject::connect(res, &QComboBox::currentTextChanged, res, [&c, &g](const QString& t) {
        auto parts = t.toLower().split('x');
        bool ok1 = false, ok2 = false;
        if (parts.size() == 2) {
            uint w = parts[0].trimmed().toUInt(&ok1), h = parts[1].trimmed().toUInt(&ok2);
            if (ok1 && ok2 && w >= 320 && h >= 240) g.width = w, g.height = h, c.changed();
        } else if (t == "desktop")
            g.width = g.height = 0, c.changed();
    });
    form->addRow("Resolution:", res);
    form->addRow("Aspect ratio:", enumCombo(g.aspect, {{"Auto (follow the window)", int(Aspect::Auto)},
                                                       {"4:3 (original, pillarboxed)", int(Aspect::R4x3)},
                                                       {"16:9", int(Aspect::R16x9)},
                                                       {"16:10", int(Aspect::R16x10)},
                                                       {"21:9", int(Aspect::R21x9)}}));
    form->addRow("Window:", enumCombo(g.window, {{"Windowed", int(WindowMode::Windowed)},
                                                 {"Borderless", int(WindowMode::Borderless)},
                                                 {"Fullscreen", int(WindowMode::Fullscreen)}}));
    auto check = [&](const char* label, bool& field) {
        auto* cb = new QCheckBox(label);
        cb->setChecked(field);
        QObject::connect(cb, &QCheckBox::toggled, cb, [&c, &field](bool on) { field = on, c.changed(); });
        return cb;
    };
    form->addRow("", check("Vertical sync", g.vsync));
    auto* msaa = new QComboBox;
    for (int s : {1, 2, 4, 8}) msaa->addItem(s == 1 ? QString("Off") : QString("%1×").arg(s), s);
    msaa->setCurrentIndex(std::max(0, msaa->findData(int(g.msaa))));
    QObject::connect(msaa, &QComboBox::currentIndexChanged, msaa,
                     [&c, &g, msaa](int) { g.msaa = msaa->currentData().toUInt(), c.changed(); });
    form->addRow("Anti-aliasing (enhanced):", msaa);
    form->addRow("Texture filtering:", enumCombo(g.filter, {{"Nearest (original look)", int(TextureFilter::Nearest)},
                                                            {"Linear", int(TextureFilter::Linear)},
                                                            {"Anisotropic", int(TextureFilter::Anisotropic)}}));
    auto* dev = new QComboBox;
    dev->addItem("Automatic", "");
    auto vk = queryVulkan();
    for (auto& gpu : vk.gpus)
        dev->addItem(QString("%1 (%2, %3)")
                         .arg(q(gpu.name), q(gpu.type), q(gpu.driverName.empty() ? gpu.driverVersion : gpu.driverName)),
                     q(gpu.name));
    if (!g.device.empty() && dev->findData(q(g.device)) < 0) dev->addItem(q(g.device) + " (not present)", q(g.device));
    dev->setCurrentIndex(std::max(0, dev->findData(q(g.device))));
    QObject::connect(dev, &QComboBox::currentIndexChanged, dev,
                     [&c, &g, dev](int) { g.device = dev->currentData().toString().toStdString(), c.changed(); });
    form->addRow("Vulkan device:", dev);
    form->addRow("", check("Vulkan validation layers (debugging; slower)", g.validation));
    if (!vk.loader) {
        auto* warn =
            new QLabel("<span style='color:#c02020'>Vulkan is unavailable: " + q(vk.error).toHtmlEscaped() + "</span>");
        warn->setWordWrap(true);
        v->addWidget(warn);
    }
    auto* note =
        new QLabel("The original presentation always renders the 3D scene at 640×480 and scales it pixel-exactly; "
                   "aspect, anti-aliasing and filtering apply to the enhanced presentation.");
    note->setWordWrap(true);
    v->addWidget(note);
    v->addStretch();
    return scrollPage(page);
}

// ---- Audio ------------------------------------------------------------------------------------

QWidget* makeAudioPage(Context& c) {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->addWidget(heading("Audio"));
    auto* form = new QFormLayout;
    v->addLayout(form);
    auto& a = c.settings.audio;
    auto slider = [&](const char* label, int& field) {
        auto* row = new QHBoxLayout;
        auto* s = new QSlider(Qt::Horizontal);
        s->setRange(0, 100);
        s->setValue(field);
        auto* val = new QLabel(QString("%1%").arg(field));
        val->setMinimumWidth(40);
        QObject::connect(s, &QSlider::valueChanged, s,
                         [&c, &field, val](int x) { field = x, val->setText(QString("%1%").arg(x)), c.changed(); });
        row->addWidget(s, 1);
        row->addWidget(val);
        form->addRow(label, row);
    };
    slider("Master:", a.master);
    slider("Music:", a.music);
    slider("Sound effects:", a.sfx);
    slider("Commentary:", a.commentary);
    slider("Crowd:", a.crowd);
    auto* dev = new QComboBox;
    dev->addItem("System default", "");
    for (auto& d : openalDevices()) dev->addItem(q(d), q(d));
    if (!a.device.empty() && dev->findData(q(a.device)) < 0) dev->addItem(q(a.device) + " (not present)", q(a.device));
    dev->setCurrentIndex(std::max(0, dev->findData(q(a.device))));
    QObject::connect(dev, &QComboBox::currentIndexChanged, dev,
                     [&c, &a, dev](int) { a.device = dev->currentData().toString().toStdString(), c.changed(); });
    form->addRow("Output device:", dev);
    auto* note = new QLabel("Commentary, music and crowd need the corresponding AUDIO/ files from the CD; Validate on "
                            "the Game files page shows which are present.");
    note->setWordWrap(true);
    v->addWidget(note);
    v->addStretch();
    return scrollPage(page);
}

// ---- Logs -------------------------------------------------------------------------------------

QWidget* makeLogsPage(Context& c) {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->addWidget(heading("Logs"));
    auto* row = new QHBoxLayout;
    auto* files = new QComboBox;
    files->setMinimumWidth(320);
    auto* refreshBtn = new QPushButton("Refresh");
    auto* copy = new QPushButton("Copy");
    auto* open = new QPushButton("Open folder");
    row->addWidget(files, 1);
    row->addWidget(refreshBtn);
    row->addWidget(copy);
    row->addWidget(open);
    v->addLayout(row);
    auto* text = monoText();
    v->addWidget(text, 1);

    auto show = [text, files] {
        QFile f(files->currentData().toString());
        if (!f.open(QIODevice::ReadOnly)) {
            text->setPlainText(files->count() ? "Cannot read " + f.fileName()
                                              : "No logs yet. Logs are written when the game is started with Play.");
            return;
        }
        constexpr qint64 kMax = 2 << 20; // the tail of very long logs
        if (f.size() > kMax) f.seek(f.size() - kMax);
        text->setPlainText(QString::fromUtf8(f.readAll()));
        text->moveCursor(QTextCursor::End);
    };
    auto refresh = [&c, files, show] {
        files->blockSignals(true);
        files->clear();
        std::vector<std::pair<fs::file_time_type, fs::path>> logs;
        std::error_code ec;
        if (fs::is_directory(c.paths.logs, ec))
            for (auto& e : fs::directory_iterator(c.paths.logs, ec))
                if (e.is_regular_file() && e.path().extension() == ".log")
                    logs.emplace_back(e.last_write_time(), e.path());
        std::ranges::sort(logs, std::greater{});
        for (auto& [t, f] : logs) files->addItem(q(f.filename()), q(f));
        files->blockSignals(false);
        show();
    };
    c.refreshLogs = refresh;
    refresh();
    QObject::connect(files, &QComboBox::currentIndexChanged, page, [show](int) { show(); });
    QObject::connect(refreshBtn, &QPushButton::clicked, page, refresh);
    QObject::connect(copy, &QPushButton::clicked, page, [&c, text] {
        QApplication::clipboard()->setText(text->toPlainText());
        c.status("Log copied to the clipboard");
    });
    QObject::connect(open, &QPushButton::clicked, page, [&c] { openFolder(c.paths.logs); });
    return page;
}

// ---- Troubleshooting --------------------------------------------------------------------------

QWidget* makeTroubleshootingPage(Context& c) {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->addWidget(heading("Troubleshooting"));
    auto* text = monoText();
    v->addWidget(text, 1);
    auto* row = new QHBoxLayout;
    auto* refresh = new QPushButton("Refresh");
    auto* copy = new QPushButton(QIcon::fromTheme("edit-copy"), "Copy diagnostics");
    row->addStretch();
    row->addWidget(refresh);
    row->addWidget(copy);
    v->addLayout(row);
    auto tips = new QLabel(
        "<b>Common problems.</b> <i>No Vulkan device</i>: install your GPU's Vulkan driver (Mesa RADV/ANV, NVIDIA). "
        "<i>Not playable</i>: validate on Game files; required groups must be intact. <i>ISO listed but game does not "
        "start</i>: extract the image with a standard tool and add that folder. Include the diagnostics and the latest "
        "log when reporting a bug.");
    tips->setWordWrap(true);
    v->addWidget(tips);

    auto fill = [&c, text] {
        auto s = diagnostics();
        s += std::format("Qt {}\n", qVersion());
        if (!c.loadWarnings.empty()) {
            s += "Settings problems at startup (defaults used):\n";
            for (auto& w : c.loadWarnings) s += "  " + w + "\n";
        }
        if (!c.missingFeatures.empty()) {
            s += "Missing features (last validation):\n";
            for (auto& m : c.missingFeatures) s += "  " + m + "\n";
        } else
            s += "Missing features: validate on the Game files page to check.\n";
        text->setPlainText(q(s));
    };
    fill();
    QObject::connect(refresh, &QPushButton::clicked, page, fill);
    QObject::connect(copy, &QPushButton::clicked, page, [&c, text, fill] {
        fill();
        QApplication::clipboard()->setText(text->toPlainText());
        c.status("Diagnostics copied to the clipboard");
    });
    return page;
}

} // namespace wc98::launcher
