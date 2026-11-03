// Derived from fifa99-native (same author), ported for World Cup 98
// Controls page: per-device bindings, SDL3 gamepad hot-plug and live input capture.
#include "keymap.hpp"
#include "mainwindow.hpp"

#include <SDL3/SDL.h>

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace wc98::launcher {

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

std::string guidOf(SDL_JoystickID id) {
    char buf[64];
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), buf, sizeof buf);
    return buf;
}

// Owns the SDL gamepad subsystem; pumps its events from a Qt timer.
class Gamepads {
  public:
    struct Pad {
        SDL_Gamepad* pad;
        std::string name, guid;
    };
    std::function<void()> onHotplug;
    std::function<void(SDL_JoystickID, std::string)> onInput; // input name in settings syntax

    Gamepads() {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"); // the launcher window is a Qt window
        ok_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        if (!ok_) error_ = SDL_GetError();
    }
    ~Gamepads() {
        for (auto& [id, p] : pads_) SDL_CloseGamepad(p.pad);
        if (ok_) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] const std::map<SDL_JoystickID, Pad>& pads() const { return pads_; }

    void poll() {
        if (!ok_) return;
        SDL_Event e;
        bool changed = false;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_GAMEPAD_ADDED:
                if (auto* g = SDL_OpenGamepad(e.gdevice.which)) {
                    pads_[e.gdevice.which] = {g, SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "Gamepad",
                                              guidOf(e.gdevice.which)};
                    changed = true;
                }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (auto it = pads_.find(e.gdevice.which); it != pads_.end()) {
                    SDL_CloseGamepad(it->second.pad);
                    pads_.erase(it);
                    changed = true;
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (onInput)
                    onInput(e.gbutton.which, SDL_GetGamepadStringForButton(SDL_GamepadButton(e.gbutton.button)));
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                auto axis = SDL_GamepadAxis(e.gaxis.axis);
                float v = float(e.gaxis.value) / 32767.0f;
                if (std::fabs(v) < 0.6f || !onInput) break;
                std::string name = SDL_GetGamepadStringForAxis(axis);
                if (axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                    name += v < 0 ? "-" : "+";
                onInput(e.gaxis.which, name);
                break;
            }
            default:
                break;
            }
        }
        if (changed && onHotplug) onHotplug();
    }

  private:
    bool ok_ = false;
    std::string error_;
    std::map<SDL_JoystickID, Pad> pads_;
};

// Modal "press a key/button" dialog. Keyboard capture takes any key, Escape included (it is the
// default Pause key), so cancelling is done with the button or by waiting out the timeout.
class CaptureDialog : public QDialog {
  public:
    CaptureDialog(QWidget* parent, const QString& action, bool keyboard, Gamepads& pads,
                  std::optional<SDL_JoystickID> only)
        : QDialog(parent), keyboard_(keyboard) {
        setWindowTitle("Bind " + action);
        auto* v = new QVBoxLayout(this);
        label_ = new QLabel;
        v->addWidget(label_);
        auto* cancel = new QPushButton("Cancel");
        v->addWidget(cancel);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        cancel->setFocusPolicy(Qt::NoFocus);
        if (!keyboard) {
            pads.onInput = [this, only](SDL_JoystickID id, std::string in) {
                if (only && *only != id) return;
                result = std::move(in);
                accept();
            };
        }
        auto* tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, [this, action] {
            if (--left_ <= 0) return reject();
            label_->setText(QString("<p>%1 for <b>%2</b>…</p><p>Cancels in %3 s.</p>")
                                .arg(keyboard_ ? "Press a key" : "Press a gamepad button or move a stick", action)
                                .arg(left_));
        });
        tick->start(1000);
        label_->setText(QString("<p>%1 for <b>%2</b>…</p><p>Cancels in %3 s.</p>")
                            .arg(keyboard_ ? "Press a key" : "Press a gamepad button or move a stick", action)
                            .arg(left_));
        setFocus();
        clear_ = [&pads] { pads.onInput = nullptr; };
    }
    ~CaptureDialog() override { clear_(); }
    std::string result;

  protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (!keyboard_ || e->isAutoRepeat()) return;
        auto name = sdlKeyName(*e);
        if (name.empty()) {
            label_->setText("That key has no SDL equivalent; try another.");
            return;
        }
        result = name;
        accept();
    }

  private:
    bool keyboard_;
    int left_ = 8;
    QLabel* label_;
    std::function<void()> clear_;
};

} // namespace

QWidget* makeControlsPage(Context& c) {
    auto* page = new QWidget;
    auto pads = std::make_shared<Gamepads>();
    auto* v = new QVBoxLayout(page);
    v->addWidget(new QLabel("<h2>Controls</h2>"));

    auto* top = new QHBoxLayout;
    auto* device = new QComboBox;
    device->setMinimumWidth(360);
    auto* padState = new QLabel;
    top->addWidget(new QLabel("Device:"));
    top->addWidget(device, 1);
    top->addWidget(padState);
    v->addLayout(top);

    auto* table = new QTableWidget(int(config::kActionCount), 2);
    table->setHorizontalHeaderLabels({"Action", "Inputs"});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    v->addWidget(table, 1);

    auto* row = new QHBoxLayout;
    auto* bind = new QPushButton("Bind…");
    bind->setToolTip("Replace the inputs of the selected action (double-click a row does the same)");
    auto* add = new QPushButton("Add input…");
    auto* clear = new QPushButton("Clear");
    auto* reset = new QPushButton("Reset device to defaults");
    for (auto* b : {bind, add, clear, reset}) row->addWidget(b);
    row->addStretch();
    v->addLayout(row);
    auto* help = new QLabel(
        "Keyboard defaults follow the original manual (arrows, S D W A Q E, Ctrl/Alt skill modes, Space IGM, "
        "C V X Z tactics, Esc pause). Gamepad defaults map the Gravis Gamepad Pro layout: face buttons pass/shoot/"
        "speed burst/lob, L1 evade, R1 through pass, L2/R2 skill modes, Select IGM, Start pause. "
        "\"Gamepad (any)\" applies to every pad without its own bindings.");
    help->setWordWrap(true);
    v->addWidget(help);

    auto currentId = [device] { return device->currentData().toString().toStdString(); };
    auto fillTable = [&c, table, currentId] {
        const auto* b = c.settings.bindings(currentId());
        for (std::size_t i = 0; i < config::kActionCount; ++i) {
            table->setItem(int(i), 0, new QTableWidgetItem(q(std::string(config::actionLabel(config::Action(i))))));
            QStringList ins;
            if (b)
                for (auto& in : b->actions[i]) ins << q(in);
            table->setItem(int(i), 1, new QTableWidgetItem(ins.isEmpty() ? QString("—") : ins.join(", ")));
        }
        table->resizeColumnToContents(0);
    };
    Gamepads* pp = pads.get(); // pads->onHotplug must not own `pads`
    auto fillDevices = [&c, pp, device, padState, fillTable] {
        auto* pads = pp;
        auto keep = device->currentData().toString();
        device->blockSignals(true);
        device->clear();
        device->addItem("Keyboard", "keyboard");
        device->addItem("Gamepad (any)", "gamepad");
        for (auto& [id, p] : pads->pads())
            device->addItem(QString("%1 — connected [%2]").arg(q(p.name), q(p.guid)), q("gamepad." + p.guid));
        for (auto& d : c.settings.controls) // configured pads that are not plugged in
            if (device->findData(q(d.id)) < 0) device->addItem(q(d.id) + " (not connected)", q(d.id));
        device->setCurrentIndex(std::max(0, device->findData(keep)));
        device->blockSignals(false);
        padState->setText(pads->ok() ? QString("%1 gamepad(s) connected").arg(pads->pads().size())
                                     : q("SDL gamepad: " + pads->error()));
        fillTable();
    };
    pads->onHotplug = fillDevices;
    fillDevices();
    auto* timer = new QTimer(page);
    QObject::connect(timer, &QTimer::timeout, page, [pads] { pads->poll(); });
    timer->start(16);
    QObject::connect(device, &QComboBox::currentIndexChanged, page, [fillTable](int) { fillTable(); });

    auto capture = [&c, page, pads, table, currentId, fillTable](bool append) {
        int r = table->currentRow();
        if (r < 0) return;
        auto id = currentId();
        bool keyboard = id == "keyboard";
        std::optional<SDL_JoystickID> only;
        if (id.starts_with("gamepad."))
            for (auto& [jid, p] : pads->pads())
                if ("gamepad." + p.guid == id) only = jid;
        CaptureDialog dlg(page, table->item(r, 0)->text(), keyboard, *pads, only);
        if (dlg.exec() != QDialog::Accepted || dlg.result.empty()) return;
        auto& b = c.settings.bindingsFor(id).actions[std::size_t(r)];
        if (!append) b.clear();
        if (std::ranges::find(b, dlg.result) == b.end()) b.push_back(dlg.result);
        c.changed();
        fillTable();
        table->selectRow(r);
        c.status(QString("%1: %2").arg(table->item(r, 0)->text(), q(dlg.result)));
    };
    QObject::connect(bind, &QPushButton::clicked, page, [capture] { capture(false); });
    QObject::connect(add, &QPushButton::clicked, page, [capture] { capture(true); });
    QObject::connect(table, &QTableWidget::cellDoubleClicked, page, [capture](int, int) { capture(false); });
    QObject::connect(clear, &QPushButton::clicked, page, [&c, table, currentId, fillTable] {
        int r = table->currentRow();
        if (r < 0) return;
        c.settings.bindingsFor(currentId()).actions[std::size_t(r)].clear();
        c.changed();
        fillTable();
        table->selectRow(r);
    });
    QObject::connect(reset, &QPushButton::clicked, page, [&c, currentId, fillTable] {
        auto id = currentId();
        auto def = id == "keyboard" ? config::defaultKeyboard() : config::defaultGamepad();
        c.settings.bindingsFor(id).actions = def.actions;
        c.changed();
        fillTable();
    });
    table->selectRow(0);
    return page;
}

} // namespace wc98::launcher
