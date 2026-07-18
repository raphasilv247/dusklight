#if DUSK_ENABLE_SENTRY_NATIVE

#include "reporting.hpp"

#include "button.hpp"
#include "dusk/crash_reporting.h"
#include "ui.hpp"

#include <dolphin/gx/GXAurora.h>

namespace dusk::ui {

CrashReportWindow::CrashReportWindow() : WindowSmall("modal", "modal-dialog") {
    mDialog->SetClass("modal-dialog", true);

    auto* header = append(mDialog, "div");
    header->SetClass("modal-header", true);

    auto* title = append(header, "div");
    title->SetClass("modal-title", true);
    title->SetInnerRML("Enviar Relatórios de Falha");

    auto* headIcon = append(header, "icon");
    headIcon->SetClass("question-mark", true);

    auto* intro = append(mDialog, "div");
    intro->SetClass("modal-body", true);
    intro->SetInnerRML(
        "O Dusklight pode enviar relatórios de falhas automaticamente para os desenvolvedores. Os relatórios de falhas contêm o "
        "seguinte:"
        "<br/>• Versão do sistema operacional<br/>• Arquitetura da CPU<br/>• Modelo da GPU e versão do driver"
        "<br/>• Caminhos de arquivo (podem incluir o nome de usuário da conta)<br/>• Rastreamento de pilha (stack trace)<br/><br/>"
        "Isso pode ser alterado no menu de Configurações a qualquer momento.");

    auto* grid = append(mDialog, "div");
    grid->SetClass("preset-grid", true);

    struct OptionInfo {
        const char* name;
        const char* desc;
        void (*apply)();
    };

    static constexpr OptionInfo kOptions[] = {
        {"Ativar",
            "Envia relatórios de falhas para os desenvolvedores do Dusklight. Os relatórios incluirão as informações descritas "
            "acima.",
            [] { crash_reporting::set_consent(true); }},
        {"Desativar",
            "Não envia relatórios de falhas. Isso pode dificultar a resolução de problemas que você "
            "encontrar.",
            [] { crash_reporting::set_consent(false); }},
    };

    for (const auto& option : kOptions) {
        auto* col = append(grid, "div");
        col->SetClass("preset-col", true);

        auto btn = std::make_unique<Button>(col, Rml::String(option.name));
        btn->on_nav_command([this, apply = option.apply](Rml::Event&, NavCommand cmd) {
            if (cmd == NavCommand::Confirm) {
                apply();
                hide(true);
                return true;
            }
            return false;
        });
        mButtons.push_back(std::move(btn));

        auto* desc = append(col, "div");
        desc->SetClass("preset-desc", true);
        desc->SetInnerRML(option.desc);
    }
}

bool CrashReportWindow::focus() {
    if (!mButtons.empty()) {
        return mButtons.back()->focus();
    }
    return false;
}

bool CrashReportWindow::handle_nav_command(Rml::Event& event, NavCommand cmd) {
    if (cmd == NavCommand::Cancel || cmd == NavCommand::Menu) {
        return true;
    }
    int direction = 0;
    if (cmd == NavCommand::Left) {
        direction = -1;
    } else if (cmd == NavCommand::Right) {
        direction = 1;
    } else {
        return false;
    }
    auto* target = event.GetTargetElement();
    for (int i = 0; i < static_cast<int>(mButtons.size()); ++i) {
        if (mButtons[i]->contains(target)) {
            const int next = i + direction;
            if (next >= 0 && next < static_cast<int>(mButtons.size())) {
                if (mButtons[next]->focus()) {
                    mDoAud_seStartMenu(kSoundItemFocus);
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

}  // namespace dusk::ui

#endif
