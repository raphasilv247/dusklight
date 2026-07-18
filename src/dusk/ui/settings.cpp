#include "settings.hpp"

#include "aurora/gfx.h"
#include "bool_button.hpp"
#include "controller_config.hpp"
#include "dusk/app_info.hpp"
#include "dusk/audio/DuskAudioSystem.h"
#include "dusk/audio/DuskDsp.hpp"
#include "dusk/android_frame_rate.hpp"
#include "dusk/config.hpp"
#include "dusk/hotkeys.h"
#include "dusk/data.hpp"
#include "dusk/file_select.hpp"
#include "dusk/imgui/ImGuiEngine.hpp"
#include "dusk/io.hpp"
#include "dusk/livesplit.h"
#include "dusk/discord_presence.hpp"
#include "dusk/speedrun.h"
#include "graphics_tuner.hpp"
#include "m_Do/m_Do_main.h"
#include "menu_bar.hpp"
#include "modal.hpp"
#include "number_button.hpp"
#include "menu_bar.hpp"
#include "pane.hpp"
#include "prelaunch.hpp"
#include "touch_controls_editor.hpp"
#include "ui.hpp"

#include <aurora/lib/window.hpp>
#include <SDL3/SDL_filesystem.h>
#include <fmt/format.h>

#if DUSK_ENABLE_SENTRY_NATIVE
#include "dusk/crash_reporting.h"
#endif

#include <algorithm>
#include <filesystem>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_ANDROID) || defined(__ANDROID__) || \
    (defined(__APPLE__) && TARGET_OS_IOS && !TARGET_OS_MACCATALYST)
#define TOUCH_CONTROLS_AVAILABLE true
#else
#define TOUCH_CONTROLS_AVAILABLE false
#endif

namespace dusk::ui {
namespace {

constexpr std::array kLanguageNames = {
    "English",
    "German",
    "French",
    "Spanish",
    "Italian",
};

constexpr std::array kCardFileTypes = {
    "Imagem de Cartão",
    "Pasta GCI",
};

constexpr std::array kFpsOverlayCornerNames = {
    "Superior Esquerdo",
    "Superior Direito",
    "Inferior Esquerdo",
    "Inferior Direito",
};

constexpr std::array kInterpolationModes = {
    "Desativado",
    "Limitado",
    "Ilimitado",
};

constexpr std::array kTouchTargetingLabels = {
    "Híbrido",
    "Segurar",
    "Alternar",
};

constexpr std::array kTouchTargetingDescriptions = {
    "Toque uma vez para travar a mira quando um alvo for encontrado. Toque duas vezes quando nenhum alvo for encontrado para segurar o L.",
    "O L fica pressionado apenas enquanto seu dedo estiver no botão.",
    "Toque no L para mantê-lo pressionado. Toque novamente para soltá-lo.",
};

constexpr std::array kGyroInputModeLabels = {
    "Sensor",
    "Mouse",
};

constexpr std::array kMenuScalingModeLabels = {
    "GameCube",
    "Wii",
    "Dusklight",
};

constexpr std::array kMagicArmorModes = {
    "Normal",
    "Ao Sofrer Dano",
    "Defesa Dupla",
    "Invencível",
    "Cosmético",
};

bool try_parse_backend(std::string_view backend, AuroraBackend& outBackend) {
    if (backend == "auto") {
        outBackend = BACKEND_AUTO;
        return true;
    }
    if (backend == "d3d11") {
        outBackend = BACKEND_D3D11;
        return true;
    }
    if (backend == "d3d12") {
        outBackend = BACKEND_D3D12;
        return true;
    }
    if (backend == "metal") {
        outBackend = BACKEND_METAL;
        return true;
    }
    if (backend == "vulkan") {
        outBackend = BACKEND_VULKAN;
        return true;
    }
    if (backend == "opengl") {
        outBackend = BACKEND_OPENGL;
        return true;
    }
    if (backend == "opengles") {
        outBackend = BACKEND_OPENGLES;
        return true;
    }
    if (backend == "webgpu") {
        outBackend = BACKEND_WEBGPU;
        return true;
    }
    if (backend == "null") {
        outBackend = BACKEND_NULL;
        return true;
    }

    return false;
}

std::string_view backend_name(AuroraBackend backend) {
    switch (backend) {
    default:
        return "Automático";
    case BACKEND_D3D12:
        return "D3D12";
    case BACKEND_D3D11:
        return "D3D11";
    case BACKEND_METAL:
        return "Metal";
    case BACKEND_VULKAN:
        return "Vulkan";
    case BACKEND_OPENGL:
        return "OpenGL";
    case BACKEND_OPENGLES:
        return "OpenGL ES";
    case BACKEND_WEBGPU:
        return "WebGPU";
    case BACKEND_NULL:
        return "Null";
    }
}

std::string_view backend_id(AuroraBackend backend) {
    switch (backend) {
    default:
        return "auto";
    case BACKEND_D3D12:
        return "d3d12";
    case BACKEND_D3D11:
        return "d3d11";
    case BACKEND_METAL:
        return "metal";
    case BACKEND_VULKAN:
        return "vulkan";
    case BACKEND_OPENGL:
        return "opengl";
    case BACKEND_OPENGLES:
        return "opengles";
    case BACKEND_WEBGPU:
        return "webgpu";
    case BACKEND_NULL:
        return "null";
    }
}

std::vector<AuroraBackend> available_backends() {
    std::vector<AuroraBackend> backends;
    backends.emplace_back(BACKEND_AUTO);
    size_t backendCount = 0;
    const AuroraBackend* raw = aurora_get_available_backends(&backendCount);
    for (size_t i = 0; i < backendCount; ++i) {
        // Do not expose NULL
        if (raw[i] != BACKEND_NULL) {
            backends.emplace_back(raw[i]);
        }
    }
    return backends;
}

AuroraBackend configured_backend() {
    AuroraBackend configuredBackend = BACKEND_AUTO;
    const auto configuredId = getSettings().backend.graphicsBackend.getValue();
    if (!try_parse_backend(configuredId, configuredBackend)) {
        configuredBackend = BACKEND_AUTO;
    }
    return configuredBackend;
}

Rml::String configured_data_path_display_name() {
    const auto path = data::abbreviated_path_string(data::configured_data_path());
    if (path.empty()) {
        return "(nenhuma)";
    }

    auto display = display_name_for_path(path);
    if (display.empty()) {
        return path;
    }
    return display;
}

class DataFolderPathText : public Component {
public:
    explicit DataFolderPathText(Rml::Element* parent) : Component(append(parent, "div")) {}

    void update() override {
        const Rml::String rml =
            "<span class=\"data-folder-current\">Pasta de dados atual:<br/>" +
            escape(data::abbreviated_path_string(data::configured_data_path())) + "</span>";
        if (rml != mCurrentRml) {
            mRoot->SetInnerRML(rml);
            mCurrentRml = rml;
        }
        Component::update();
    }

private:
    Rml::String mCurrentRml;
};

void show_data_folder_error_modal(std::string_view message) {
    auto dismiss = [](Modal& modal) {
        mDoAud_seStartMenu(kSoundWindowClose);
        modal.pop();
    };
    push_document(std::make_unique<Modal>(Modal::Props{
        .title = "Pasta de Dados Não Alterada",
        .bodyRml = escape(message),
        .actions =
            {
                ModalAction{
                    .label = "OK",
                    .onPressed = dismiss,
                },
            },
        .onDismiss = dismiss,
        .icon = "warning",
    }));
    if (auto* doc = top_document()) {
        doc->focus();
    }
}

void data_folder_dialog_callback(void*, const char* path, const char* error) {
    if (error != nullptr) {
        show_data_folder_error_modal(error);
        return;
    }
    if (path == nullptr) {
        return;
    }

    std::string dataPathError;
    if (data::set_custom_data_path(path, &dataPathError)) {
        mDoAud_seStartMenu(kSoundItemChange);
        return;
    }

    if (dataPathError.empty()) {
        dataPathError =
            fmt::format("{} não pôde usar a pasta selecionada como sua pasta de dados.", AppName);
    }
    show_data_folder_error_modal(dataPathError);
}

const Rml::String kInternalResolutionHelpText =
    "Configura a resolução usada para renderizar o jogo. Valores mais altos exigem mais do "
    "seu hardware gráfico.";
const Rml::String kShadowResolutionHelpText =
    "Configura a resolução do mapa de sombras. Valores mais altos melhoram a qualidade das sombras, mas aumentam "
    "o uso de GPU e memória.";
const Rml::String kResamplerHelpText =
    "Configura o método de amostragem usado ao escalar a resolução interna para a apresentação final.";
const Rml::String kBloomHelpText =
    "Configura o efeito de bloom em pós-processamento. Classic usa o passe de bloom original; Dusklight usa "
    "um passe de bloom de qualidade superior.";
const Rml::String kBloomBrightnessHelpText =
    "Configura a intensidade do bloom. Valores mais altos fazem as áreas claras brilharem mais intensamente.";
const Rml::String kDepthOfFieldHelpText =
    "Configura o efeito de profundidade de campo em pós-processamento. Classic usa o passe original de profundidade de campo;"
    " Dusklight usa um passe de profundidade de campo de qualidade superior.";
const Rml::String kUnlockFramerateHelpText =
    "<br/>Usa interpolação entre quadros para permitir taxas de quadros mais altas.<br/><br/>Pode introduzir pequenos "
    "artefatos visuais ou falhas de animação.";
const Rml::String kTextureReplacementHelpText =
    "Ativa substituições de textura instaladas.";

int float_setting_percent(ConfigVar<float>& var) {
    return static_cast<int>(var.getValue() * 100.0f + 0.5f);
}

bool gyro_enabled() {
    return getSettings().game.enableGyroAim || getSettings().game.enableGyroRollgoal;
}

Rml::String touch_targeting_label(TouchTargeting targeting) {
    const auto index = static_cast<std::size_t>(targeting);
    if (index >= kTouchTargetingLabels.size()) {
        return "Desconhecido";
    }
    return kTouchTargetingLabels[index];
}

struct ConfigBoolProps {
    Rml::String key;
    Rml::String icon;
    Rml::String helpText;
    std::function<void(bool)> onChange;
    std::function<bool()> isDisabled;
};

SelectButton& config_bool_select(
    Pane& leftPane, Pane& rightPane, ConfigVar<bool>& var, ConfigBoolProps props) {
    auto& button = leftPane.add_child<BoolButton>(BoolButton::Props{
        .key = std::move(props.key),
        .icon = std::move(props.icon),
        .getValue = [&var] { return var.getValue(); },
        .setValue =
            [&var, callback = std::move(props.onChange)](bool value) {
                if (value == var.getValue()) {
                    return;
                }
                var.setValue(value);
                config::save();
                if (callback) {
                    callback(value);
                }
            },
        .isDisabled = std::move(props.isDisabled),
        .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
    });
    leftPane.register_control(
        button, rightPane, [helpText = std::move(props.helpText)](Pane& pane) {
            pane.clear();
            pane.add_rml(helpText);
        });
    return button;
}

void add_speedrun_disabled_option(Pane& leftPane, Pane& rightPane, ConfigVar<bool>& var,
    const Rml::String& key, const Rml::String& helpText) {
    config_bool_select(leftPane, rightPane, var, {
        .key = key,
        .helpText = helpText,
        .isDisabled = [] { return getSettings().game.speedrunMode.getValue(); },
    });
}

SelectButton& config_percent_select(Pane& leftPane, Pane& rightPane, ConfigVar<float>& var,
    Rml::String key, Rml::String helpText, int min, int max, int step = 5,
    std::function<bool()> isDisabled = {}) {
    auto& button = leftPane.add_child<NumberButton>(NumberButton::Props{
        .key = std::move(key),
        .getValue = [&var] { return float_setting_percent(var); },
        .setValue =
            [&var, min, max](int value) {
                var.setValue(std::clamp(value, min, max) / 100.0f);
                config::save();
            },
        .isDisabled = std::move(isDisabled),
        .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
        .min = min,
        .max = max,
        .step = step,
        .suffix = "%",
    });
    leftPane.register_control(button, rightPane, [helpText = std::move(helpText)](Pane& pane) {
        pane.clear();
        pane.add_rml(helpText);
    });
    return button;
}

SelectButton& config_int_select(Pane& leftPane, Pane& rightPane, ConfigVar<int>& var,
    Rml::String key, Rml::String helpText, int min, int max, int step = 5,
    std::function<bool()> isDisabled = {}, std::function<void(int)> onChange = {},
    std::string suffix = "") {
    auto& button = leftPane.add_child<NumberButton>(NumberButton::Props{
        .key = std::move(key),
        .getValue = [&var] { return var.getValue(); },
        .setValue =
            [&var, min, max, callback = std::move(onChange)](int value) {
                const int clampedValue = std::clamp(value, min, max);
                var.setValue(clampedValue);
                config::save();
                if (callback) {
                    callback(clampedValue);
                }
            },
        .isDisabled = std::move(isDisabled),
        .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
        .min = min,
        .max = max,
        .step = step,
        .suffix = suffix,
    });
    leftPane.register_control(button, rightPane, [helpText = std::move(helpText)](Pane& pane) {
        pane.clear();
        pane.add_text(helpText);
    });
    return button;
}

template <typename T>
void graphics_tuner_control(Window& window, Pane& leftPane, Pane& rightPane, ConfigVar<T>& var,
    const GraphicsTunerProps& props) {
    leftPane.register_control(
        leftPane
            .add_select_button({
                .key = props.title,
                .getValue =
                    [&var, option = props.option] {
                        if constexpr (std::is_same_v<T, float>) {
                            return format_graphics_setting_value(
                                option, float_setting_percent(var));
                        } else {
                            return format_graphics_setting_value(
                                option, static_cast<int>(var.getValue()));
                        }
                    },
                .isModified = [&var] { return var.getValue() != var.getDefaultValue(); },
                .submit = false,
            })
            .on_nav_command([&window, props](Rml::Event&, NavCommand cmd) {
                if (cmd == NavCommand::Confirm || cmd == NavCommand::Left ||
                    cmd == NavCommand::Right) {
                    window.push(std::make_unique<GraphicsTuner>(props));
                    return true;
                }
                return false;
            }),
        rightPane, [helpText = props.helpText](Pane& pane) {
            pane.clear();
            pane.add_text(helpText);
        });
}

}  // namespace

SettingsWindow::SettingsWindow(bool prelaunch) : mPrelaunch(prelaunch) {
    if (prelaunch) {
        add_tab("Pré-Início", [this](Rml::Element* content) {
            auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
            auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

            leftPane.register_control(
                leftPane
                    .add_select_button({
                        .key = "Imagem do Disco",
                        .getValue =
                            [] {
                                const auto& path = prelaunch_state().configuredDiscPath;
                                std::string display;
                                if (path.empty()) {
                                    display = "(nenhuma)";
                                } else {
                                    display = display_name_for_path(path);
                                    if (display.empty()) {
                                        display = path;
                                    }
                                }
                                return display;
                            },
                        .isModified =
                            [] {
                                const auto& state = prelaunch_state();
                                const auto& active = state.activeDiscPath;
                                return !active.empty() && state.configuredDiscPath != active;
                            },
                    })
                    .on_pressed([] { open_iso_picker(); }),
                rightPane, [](Pane& pane) {
                    pane.add_rml("Define a imagem de disco que o Dusklight usa para iniciar o jogo.<br/><br/>"
                                 "As alterações exigem reinicialização.");
                });
#if DUSK_CAN_CHANGE_DATA_FOLDER
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = "Pasta de Dados",
                    .getValue = [] { return configured_data_path_display_name(); },
                    .isModified = [] { return data::is_data_path_restart_pending(); },
                }),
                rightPane, [](Pane& pane) {
                    pane.add_text("A pasta de dados é onde o Dusklight armazena configurações, saves, "
                                  "logs, substituições de textura e outros dados do aplicativo.");
                    pane.add_child<DataFolderPathText>();
#if DUSK_CAN_OPEN_DATA_FOLDER
                    pane.add_button("Abrir Pasta de Dados").on_pressed([] {
                        if (data::open_data_path()) {
                            mDoAud_seStartMenu(kSoundClick);
                        }
                    });
#endif
                    pane.add_button("Alterar Pasta de Dados").on_pressed([] {
                        const auto defaultLocation =
                            io::fs_path_to_string(data::configured_data_path());
                        ShowFolderSelect(&data_folder_dialog_callback, nullptr,
                            aurora::window::get_sdl_window(),
                            defaultLocation.empty() ? nullptr : defaultLocation.c_str());
                    });
#if defined(_WIN32)
                    pane.add_button("Modo Portátil").on_pressed([] {
                        if (data::set_portable_data_path()) {
                            mDoAud_seStartMenu(kSoundItemChange);
                        }
                    });
#endif
                    pane.add_button({
                        .text = "Restaurar Padrão",
                        .isDisabled = [] { return data::is_default_data_path(); },
                    }).on_pressed([] {
                        if (data::reset_data_path()) {
                            mDoAud_seStartMenu(kSoundItemChange);
                        }
                    });
                    pane.add_rml("Os dados serão migrados automaticamente na reinicialização.");
                });
#endif
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = "Idioma",
                    .getValue =
                        [] {
                            const auto& state = prelaunch_state();
                            if (!state.configuredDiscCanLaunch || !state.configuredDiscInfo.isPal) {
                                return kLanguageNames[0];
                            }
                            const u8 idx = static_cast<u8>(getSettings().game.language.getValue());
                            return kLanguageNames[idx];
                        },
                    .isDisabled =
                        [] {
                            const auto& state = prelaunch_state();
                            return !state.configuredDiscCanLaunch ||
                                   !state.configuredDiscInfo.isPal;
                        },
                    .isModified =
                        [] {
                            return getSettings().game.language.getValue() !=
                                   prelaunch_state().initialLanguage;
                        },
                }),
                rightPane, [](Pane& pane) {
                    for (int i = 0; i < kLanguageNames.size(); i++) {
                        pane.add_button({
                                            .text = kLanguageNames[i],
                                            .isSelected =
                                                [i] {
                                                    return getSettings().game.language.getValue() ==
                                                           static_cast<GameLanguage>(i);
                                                },
                                        })
                            .on_pressed([i] {
                                mDoAud_seStartMenu(kSoundItemChange);
                                getSettings().game.language.setValue(static_cast<GameLanguage>(i));
                                config::save();
                            });
                    }
                    pane.add_rml("<br/>As alterações exigem reinicialização.");
                });
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = "Backend Gráfico",
                    .getValue = [] { return Rml::String{backend_name(aurora_get_backend())}; },
                    .isModified =
                        [] {
                            return getSettings().backend.graphicsBackend.getValue() !=
                                   prelaunch_state().initialGraphicsBackend;
                        },
                }),
                rightPane, [](Pane& pane) {
                    const auto availableBackends = available_backends();
                    for (const auto backend : availableBackends) {
                        pane
                            .add_button({
                                .text = Rml::String{backend_name(backend)},
                                .isSelected = [backend] { return configured_backend() == backend; },
                            })
                            .on_pressed([backend] {
                                mDoAud_seStartMenu(kSoundItemChange);
                                getSettings().backend.graphicsBackend.setValue(
                                    std::string{backend_id(backend)});
                                config::save();
                            });
                    }
                    pane.add_rml("<br/>As alterações exigem reinicialização.");
                });
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = "Tipo de Arquivo de Save",
                    .getValue =
                        [] {
                            return kCardFileTypes[getSettings().backend.cardFileType.getValue()];
                        },
                    .isModified =
                        [] {
                            return getSettings().backend.cardFileType.getValue() !=
                                   prelaunch_state().initialCardFileType;
                        },
                }),
                rightPane, [](Pane& pane) {
                    for (int i = 0; i < kCardFileTypes.size(); i++) {
                        pane
                            .add_button({
                                .text = kCardFileTypes[i],
                                .isSelected =
                                    [i] {
                                        return getSettings().backend.cardFileType.getValue() == i;
                                    },
                            })
                            .on_pressed([i] {
                                mDoAud_seStartMenu(kSoundItemChange);
                                getSettings().backend.cardFileType.setValue(i);
                                config::save();
                            });
                    }
                });
        });
    }

    add_tab("Vídeo", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Tela");

        leftPane.register_control(leftPane.add_button("Alternar Tela Cheia").on_pressed([] {
            mDoAud_seStartMenu(kSoundItemChange);
            getSettings().video.enableFullscreen.setValue(!getSettings().video.enableFullscreen);
            VISetWindowFullscreen(getSettings().video.enableFullscreen);
            config::save();
        }),
            rightPane, [](Pane& pane) { pane.clear(); });
        leftPane.register_control(leftPane.add_button("Restaurar Tamanho Padrão da Janela").on_pressed([] {
            mDoAud_seStartMenu(kSoundItemChange);
            getSettings().video.enableFullscreen.setValue(false);
            VISetWindowFullscreen(false);
            VISetWindowSize(FB_WIDTH * 2, FB_HEIGHT * 2);
            VICenterWindow();
        }),
            rightPane, [](Pane& pane) { pane.clear(); });
        config_bool_select(leftPane, rightPane, getSettings().video.enableVsync,
            {
                .key = "Ativar VSync",
                .helpText = "Sincroniza a taxa de quadros com a taxa de atualização do seu monitor.",
                .onChange = [](bool value) { aurora_enable_vsync(value); },
            });
        config_bool_select(leftPane, rightPane, getSettings().video.lockAspectRatio,
            {
                .key = "Travar Proporção 4:3",
                .helpText = "Trava a proporção de tela do jogo na original.",
                .onChange =
                    [](bool value) {
                        AuroraSetViewportPolicy(
                            value ? AURORA_VIEWPORT_FIT : AURORA_VIEWPORT_STRETCH);
                    },
            });
        config_bool_select(leftPane, rightPane, getSettings().game.pauseOnFocusLost,
            {
                .key = "Pausar ao Perder o Foco",
                .helpText = "Pausa o jogo quando a janela perde o foco.",
                .isDisabled = [] { return IsMobile || getSettings().game.speedrunMode; },
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Mostrar Contador de FPS",
                .getValue =
                    [] {
                        if (!getSettings().video.enableFpsOverlay.getValue()) {
                            return Rml::String{"Desativado"};
                        }
                        const int idx = getSettings().video.fpsOverlayCorner.getValue();
                        return Rml::String{kFpsOverlayCornerNames[idx]};
                    },
                .isModified =
                    [] {
                        const auto& enable = getSettings().video.enableFpsOverlay;
                        const auto& corner = getSettings().video.fpsOverlayCorner;
                        return enable.getValue() != enable.getDefaultValue() ||
                               (enable.getValue() && corner.getValue() != corner.getDefaultValue());
                    },
            }),
            rightPane, [](Pane& pane) {
                pane.add_button(
                        {
                            .text = "Desativado",
                            .isSelected =
                                [] { return !getSettings().video.enableFpsOverlay.getValue(); },
                        })
                    .on_pressed([] {
                        mDoAud_seStartMenu(kSoundItemChange);
                        getSettings().video.enableFpsOverlay.setValue(false);
                        config::save();
                    });
                for (int i = 0; i < static_cast<int>(kFpsOverlayCornerNames.size()); ++i) {
                    pane.add_button(
                            {
                                .text = kFpsOverlayCornerNames[i],
                                .isSelected =
                                    [i] {
                                        return getSettings().video.enableFpsOverlay.getValue() &&
                                               getSettings().video.fpsOverlayCorner.getValue() == i;
                                    },
                            })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().video.enableFpsOverlay.setValue(true);
                            getSettings().video.fpsOverlayCorner.setValue(i);
                            config::save();
                        });
                }
                pane.add_rml(
                    "<br/>Exibe a taxa de quadros atual em um canto da tela durante o jogo.");
            });
        config_bool_select(leftPane, rightPane, getSettings().video.rememberWindowSize,
            {
                .key = "Lembrar Tamanho da Janela",
                .helpText = "Salva e restaura o tamanho da janela da sessão anterior ao abrir o Dusklight.",
                .onChange =
                    [](bool value) {
                        if (value && !dusk::getSettings().video.enableFullscreen) {
                            const auto windowSize = aurora::window::get_window_size();
                            dusk::getSettings().video.lastWindowWidth.setValue(windowSize.width);
                            dusk::getSettings().video.lastWindowHeight.setValue(windowSize.height);
                            dusk::config::save();
                        }
                    },
                .isDisabled = [] { return IsMobile; },
            });
        leftPane.add_section("Resolução");
        graphics_tuner_control(*this, leftPane, rightPane,
            getSettings().game.internalResolutionScale,
            GraphicsTunerProps{
                .option = GraphicsOption::InternalResolution,
                .title = "Resolução Interna",
                .helpText = kInternalResolutionHelpText,
                .valueMin = 0,
                .valueMax = 12,
                .defaultValue = 0,
            });
        graphics_tuner_control(*this, leftPane, rightPane,
            getSettings().game.shadowResolutionMultiplier,
            GraphicsTunerProps{
                .option = GraphicsOption::ShadowResolution,
                .title = "Resolução de Sombras",
                .helpText = kShadowResolutionHelpText,
                .valueMin = 1,
                .valueMax = 8,
                .defaultValue = 1,
            });
        graphics_tuner_control(*this, leftPane, rightPane, getSettings().game.resampler,
            GraphicsTunerProps{
                .option = GraphicsOption::Resampler,
                .title = "Reamostragem de Saída",
                .helpText = kResamplerHelpText,
                .valueMin = static_cast<int>(Resampler::Bilinear),
                .valueMax = static_cast<int>(Resampler::Area),
                .defaultValue = static_cast<int>(Resampler::Bilinear),
            });

        leftPane.add_section("Pós-Processamento");
        graphics_tuner_control(*this, leftPane, rightPane, getSettings().game.bloomMode,
            GraphicsTunerProps{
                .option = GraphicsOption::BloomMode,
                .title = "Bloom",
                .helpText = kBloomHelpText,
                .valueMin = static_cast<int>(BloomMode::Off),
                .valueMax = static_cast<int>(BloomMode::Dusk),
                .defaultValue = static_cast<int>(BloomMode::Classic),
            });
        graphics_tuner_control(*this, leftPane, rightPane, getSettings().game.bloomMultiplier,
            GraphicsTunerProps{
                .option = GraphicsOption::BloomMultiplier,
                .title = "Brilho do Bloom",
                .helpText = kBloomBrightnessHelpText,
                .valueMin = 0,
                .valueMax = 100,
                .defaultValue = 100,
                .step = 10,
            });
        graphics_tuner_control(*this, leftPane, rightPane, getSettings().game.depthOfFieldMode,
            GraphicsTunerProps{
                .option = GraphicsOption::DepthOfFieldMode,
                .title = "Profundidade de Campo",
                .helpText = kDepthOfFieldHelpText,
                .valueMin = static_cast<int>(DepthOfFieldMode::Off),
                .valueMax = static_cast<int>(DepthOfFieldMode::Dusk),
                .defaultValue = static_cast<int>(DepthOfFieldMode::Classic),
            });

        leftPane.add_section("Renderização");
        graphics_tuner_control(*this, leftPane, rightPane,
            getSettings().game.enableTextureReplacements,
            GraphicsTunerProps{
                .option = GraphicsOption::TextureReplacements,
                .title = "Ativar Substituições de Textura",
                .helpText = kTextureReplacementHelpText,
                .valueMin = static_cast<int>(false),
                .valueMax = static_cast<int>(true),
                .defaultValue = static_cast<int>(false),
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Desbloquear Taxa de Quadros",
                .getValue =
                    [] {
                        return kInterpolationModes[static_cast<u8>(getSettings().game.enableFrameInterpolation.getValue())];
                    },
                .isModified =
                    [] {
                        return getSettings().game.enableFrameInterpolation.getValue() !=
                               getSettings().game.enableFrameInterpolation.getDefaultValue();
                    },
            }),
            rightPane, [](Pane& pane) {
                for (int i = 0; i < kInterpolationModes.size(); i++) {
                    pane.add_button({
                            .text = kInterpolationModes[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.enableFrameInterpolation.getValue() == static_cast<FrameInterpMode>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.enableFrameInterpolation.setValue(static_cast<FrameInterpMode>(i));
                            android::update_surface_frame_rate();
                            config::save();
                        });
                }
                pane.add_rml(kUnlockFramerateHelpText);
            });
        config_int_select(leftPane, rightPane, getSettings().video.maxFrameRate,
            "Limite de Taxa de Quadros", "Limita a taxa de quadros ao valor especificado.", 30, 540, 1,
            [] { return getSettings().game.enableFrameInterpolation.getValue() != FrameInterpMode::Capped; },
            [](int) { android::update_surface_frame_rate(); });
        config_bool_select(leftPane, rightPane, getSettings().game.enableMapBackground,
            {
                .key = "Ativar Sombras do Mini-Mapa",
                .helpText = "Renderiza uma sombra espessa ao redor do mini-mapa. Pode impactar o desempenho."
            });
        config_bool_select(leftPane, rightPane, getSettings().game.disableCutscenePillarboxing,
            {
                .key = "Desativar Pillarboxing em Cutscenes",
                .helpText = "Desativa as barras pretas nas laterais esquerda e direita da tela "
                            "durante algumas cutscenes, principalmente em telas ultra-wide. "
                            "Visuais além do enquadramento original pretendido podem apresentar falhas visuais."
            });
    });

    add_tab("Controles", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        auto addOption = [&](const Rml::String& key, ConfigVar<bool>& value,
                             const Rml::String& helpText, std::function<bool()> isDisabled = {}) {
            config_bool_select(leftPane, rightPane, value,
                {
                    .key = key,
                    .helpText = helpText,
                    .isDisabled = std::move(isDisabled),
                });
        };

        leftPane.add_section("Controles");
        leftPane.register_control(leftPane.add_button("Configurar Controles").on_pressed([this] {
            push(std::make_unique<ControllerConfigWindow>());
        }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text("Abrir configuração de mapeamento de controles.");
            });
        config_bool_select(leftPane, rightPane, getSettings().game.allowBackgroundInput,
            {
                .key = "Permitir Entradas em Segundo Plano",
                .helpText = "Permite entradas mesmo quando a janela do jogo não está em foco.",
                .onChange = [](bool value) { aurora_set_background_input(value); },
            });

#if TOUCH_CONTROLS_AVAILABLE
        leftPane.add_section("Toque");
        addOption("Controles de Toque", getSettings().game.enableTouchControls,
            "Ativa a sobreposição de controles para telas sensíveis ao toque.<br/><br/>Pressione e arraste no lado esquerdo "
            "da tela para mover, e no lado direito da tela para controlar a câmera.");
        auto& customizeTouchLayout = leftPane.add_button(ControlledButton::Props{
            .text = "Personalizar Layout",
            .isDisabled = [] { return !getSettings().game.enableTouchControls; },
        });
        leftPane.register_control(customizeTouchLayout.on_pressed(
                                      [this] { push(std::make_unique<TouchControlsEditor>()); }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text("Abrir o editor de layout dos controles de toque.");
            });
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Mira por Toque",
                                      .getValue =
                                          [] {
                                              return touch_targeting_label(
                                                  getSettings().game.touchTargeting.getValue());
                                          },
                                      .isDisabled =
                                          [] { return !getSettings().game.enableTouchControls; },
                                      .isModified =
                                          [] {
                                              const auto& targeting =
                                                  getSettings().game.touchTargeting;
                                              return targeting.getValue() !=
                                                     targeting.getDefaultValue();
                                          },
                                  }),
            rightPane, [](Pane& pane) {
                pane.clear();
                for (int i = 0; i < static_cast<int>(kTouchTargetingLabels.size()); ++i) {
                    pane.add_button({
                            .text = kTouchTargetingLabels[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.touchTargeting.getValue() ==
                                           static_cast<TouchTargeting>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.touchTargeting.setValue(
                                static_cast<TouchTargeting>(i));
                            config::save();
                        });
                }
                pane.add_rml(fmt::format("<br/>Híbrido: {}<br/>Segurar: {}<br/>Alternar: {}",
                    kTouchTargetingDescriptions[0], kTouchTargetingDescriptions[1],
                    kTouchTargetingDescriptions[2]));
            });
        config_percent_select(leftPane, rightPane, getSettings().game.touchCameraXSensitivity,
            "Sensibilidade X da Câmera por Toque",
            "Ajusta a sensibilidade horizontal da câmera por toque.<br/><br/>Aplica-se apenas à entrada por toque.",
            25, 400, 5, [] { return !getSettings().game.enableTouchControls; });
        config_percent_select(leftPane, rightPane, getSettings().game.touchCameraYSensitivity,
            "Sensibilidade Y da Câmera por Toque",
            "Ajusta a sensibilidade vertical da câmera por toque.<br/><br/>Aplica-se apenas à entrada por toque.", 25,
            400, 5, [] { return !getSettings().game.enableTouchControls; });
#endif

        leftPane.add_section("Câmera");
        addOption("Câmera Livre", getSettings().game.freeCamera,
            "Ativa o controle livre de câmera, permitindo controlar a câmera totalmente com o C-Stick.");
        config_percent_select(leftPane, rightPane, getSettings().game.freeCameraXSensitivity,
            "Sensibilidade X da Câmera Livre",
            "Ajusta a sensibilidade horizontal da câmera livre.<br/><br/>Aplica-se apenas ao analógico de controle.",
            50, 200, 5, [] { return !getSettings().game.freeCamera; });
        config_percent_select(leftPane, rightPane, getSettings().game.freeCameraYSensitivity,
            "Sensibilidade Y da Câmera Livre",
            "Ajusta a sensibilidade vertical da câmera livre.<br/><br/>Aplica-se apenas ao analógico de controle.",
            50, 200, 5, [] { return !getSettings().game.freeCamera; });
        addOption("Inverter Eixo X da Câmera", getSettings().game.invertCameraXAxis,
            "Inverte o movimento horizontal da câmera.<br/><br/>Aplica-se apenas ao analógico de controle.");
        addOption("Inverter Eixo Y da Câmera", getSettings().game.invertCameraYAxis,
            "Inverte o movimento vertical da câmera.<br/><br/>Aplica-se apenas ao analógico de controle.",
            [] { return !getSettings().game.freeCamera; });
        addOption("Inverter Eixo X em Primeira Pessoa", getSettings().game.invertFirstPersonXAxis,
            "Inverte o movimento horizontal ao mirar com itens ou na câmera em primeira pessoa.<br/><br/>Aplica-se apenas ao analógico de controle.");
        addOption("Inverter Eixo Y em Primeira Pessoa", getSettings().game.invertFirstPersonYAxis,
            "Inverte o movimento vertical enquanto mira com itens ou na câmera em primeira pessoa.<br/><br/>Aplica-se apenas ao analógico de controle.");

        leftPane.add_section("Giroscópio");
        addOption("Mira por Giroscópio", getSettings().game.enableGyroAim,
            "Ativa os controles de giroscópio no modo de observação, ao mirar com o falcão e ao mirar "
            "itens compatíveis.<br/><br/>Os itens compatíveis incluem Estilingue, Bumerangue Furacão, "
            "Arco do Herói, Gancho(s), Bola com Corrente e Cetro Dominador.");
        addOption("Giroscópio no Rollgoal", getSettings().game.enableGyroRollgoal,
            "Ativa os controles de giroscópio para o Rollgoal na cabana da Hena.");
        config_percent_select(leftPane, rightPane, getSettings().game.gyroSensitivityY,
            "Sensibilidade de Inclinação do Giroscópio", "Controla a sensibilidade vertical da mira por giroscópio.", 25, 400, 5,
            [] { return !gyro_enabled(); });
        config_percent_select(leftPane, rightPane, getSettings().game.gyroSensitivityX,
            "Sensibilidade de Guinada do Giroscópio", "Controla a sensibilidade horizontal da mira por giroscópio.", 25, 400, 5,
            [] { return !gyro_enabled(); });
        config_percent_select(leftPane, rightPane, getSettings().game.gyroSensitivityRollgoal,
            "Sensibilidade do Rollgoal", "Controla a intensidade com que a entrada do giroscópio inclina a mesa do Rollgoal.",
            25, 400, 5,
            [] { return !getSettings().game.enableGyroRollgoal; });
        config_percent_select(leftPane, rightPane, getSettings().game.gyroDeadband, "Zona Morta do Giroscópio",
            "Ignora pequenos movimentos do giroscópio para reduzir drift e tremulação.", 0, 50, 1,
            [] { return !gyro_enabled(); });
        config_percent_select(leftPane, rightPane, getSettings().game.gyroSmoothing,
            "Suavização do Giroscópio", "Valores mais altos suavizam a entrada do giroscópio ao longo do tempo.", 0, 100, 1,
            [] { return !gyro_enabled(); });
        addOption("Inverter Inclinação do Giroscópio", getSettings().game.gyroInvertPitch,
            "Inverte a mira vertical por giroscópio.", [] { return !gyro_enabled(); });
        addOption("Inverter Guinada do Giroscópio", getSettings().game.gyroInvertYaw,
            "Inverte a mira horizontal por giroscópio.", [] { return !gyro_enabled(); });

        leftPane.add_section("Mouse");
        addOption("Mira por Mouse", getSettings().game.enableMouseAim,
            "Ativa a entrada por mouse no modo de observação, ao mirar com o falcão e ao mirar "
            "itens compatíveis.<br/><br/>Os itens compatíveis incluem Estilingue, Bumerangue Furacão, "
            "Arco do Herói, Gancho(s), Bola com Corrente e Cetro Dominador.");
        addOption("Câmera por Mouse", getSettings().game.enableMouseCamera,
            "Ativa a entrada por mouse para controlar a câmera em terceira pessoa.");
        config_percent_select(leftPane, rightPane, getSettings().game.mouseAimSensitivity,
            "Sensibilidade da Mira por Mouse", "Controla a sensibilidade da mira por mouse.", 25, 400, 5,
            [] { return !getSettings().game.enableMouseAim; });
        config_percent_select(leftPane, rightPane, getSettings().game.mouseCameraSensitivity,
            "Sensibilidade da Câmera por Mouse", "Controla a sensibilidade da câmera por mouse.", 25, 400, 5,
            [] { return !getSettings().game.enableMouseCamera; });
        addOption("Inverter Eixo Y do Mouse", getSettings().game.invertMouseY,
            "Inverte o controle vertical do mouse tanto para mira quanto para câmera.",
            [] { return !getSettings().game.enableMouseAim || !getSettings().game.enableMouseCamera; });

        leftPane.add_section("Jogabilidade");
        addOption("Mouse/Toque nos Menus", getSettings().game.enableMenuPointer,
            "Ativa a entrada por mouse e toque para os menus do jogo compatíveis.");
        addOption("Inverter Eixo X ao Voar/Nadar", getSettings().game.invertAirSwimX,
            "Inverte o movimento horizontal ao voar ou nadar.");
        addOption("Inverter Eixo Y ao Voar/Nadar", getSettings().game.invertAirSwimY,
            "Inverte o movimento vertical ao voar ou nadar.");
        addOption("Trocar Entrada de Seleção Direta", getSettings().game.swapDirectSelect,
            "Troca os controles de Seleção Direta na roda de itens, tornando a Seleção Direta o padrão e segurar o L para rolar a roda.");

        leftPane.add_section("Ferramentas");
        addOption("Tecla de Turbo", getSettings().game.enableTurboKeybind,
            "Segure Tab para aumentar a velocidade do jogo em até 4x.",
            [] { return getSettings().game.speedrunMode.getValue(); });
        addOption("Tecla de Reinício (" + Rml::String{hotkeys::DO_RESET} + ")",
            getSettings().game.enableResetKeybind,
            "Pressione " + Rml::String{hotkeys::DO_RESET} + " para reiniciar o jogo.");
    });

    add_tab("Áudio", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        // TODO: Individual sliders for Main Music, Sub Music, Sound Effects, and Fanfare.
        leftPane.add_section("Volume");
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Volume Principal",
                .getValue = [] { return getSettings().audio.masterVolume.getValue(); },
                .setValue =
                    [](int value) {
                        getSettings().audio.masterVolume.setValue(value);
                        config::save();
                        audio::SetMasterVolume(audio::MasterVolumeToLinear(value / 100.0f));
                    },
                .isModified =
                    [] {
                        return getSettings().audio.masterVolume.getValue() !=
                               getSettings().audio.masterVolume.getDefaultValue();
                    },
                .max = 100,
                .suffix = "%",
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text("Ajusta o volume de todos os sons do jogo.");
            });

        leftPane.add_section("Efeitos");
        config_bool_select(leftPane, rightPane, getSettings().audio.enableReverb,
            {
                .key = "Ativar Reverberação",
                .helpText = "Ativa o efeito de reverberação no áudio do jogo.",
                .onChange = [](bool value) { audio::SetEnableReverb(value); },
            });
        config_bool_select(leftPane, rightPane, getSettings().audio.enableHrtf,
            {
                .key = "Ativar Som Espacial",
                .helpText =
                    "Emula som surround via HRTF. Recomendado apenas para uso com fones de ouvido!",
                .onChange = [](bool value) { audio::EnableHrtf = value; },
            });
        config_bool_select(leftPane, rightPane, getSettings().audio.menuSounds,
            {
                .key = "Sons do Menu do Dusklight",
                .helpText = "Reproduz efeitos sonoros ao navegar pelo menu do Dusklight.",
            });

        leftPane.add_section("Ajustes");
        config_bool_select(leftPane, rightPane, getSettings().game.noLowHpSound,
            {
                .key = "Sem Som de HP Baixo",
                .helpText = "Desativa o som de aviso sonoro quando a vida está baixa.",
            });
        config_bool_select(leftPane, rightPane, getSettings().game.midnasLamentNonStop,
            {
                .key = "Lamento de Midna Contínuo",
                .helpText = "Impede a música dos inimigos enquanto o Lamento de Midna está tocando.",
            });
    });

    add_tab("Jogabilidade", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        auto addOption = [&](const Rml::String& key, ConfigVar<bool>& value,
                             const Rml::String& helpText) {
            config_bool_select(leftPane, rightPane, value,
                {
                    .key = key,
                    .helpText = helpText,
                });
        };
        auto addSpeedrunDisabledOption = [&](const Rml::String& key, ConfigVar<bool>& value,
                                             const Rml::String& helpText) {
            add_speedrun_disabled_option(leftPane, rightPane, value, key, helpText);
        };

        leftPane.add_section("Geral");
        addOption("Modo Espelhado", getSettings().game.enableMirrorMode,
            "Espelha o mundo horizontalmente, igualando à versão de Wii do jogo.");
        addOption("HUD Mínima", getSettings().game.minimalHUD,
            "Desativa os elementos da HUD principal do jogo.<br/>Útil para uma experiência mais "
            "imersiva.");
        config_percent_select(leftPane, rightPane, getSettings().game.hudScale,
            "Escala da HUD",
            "Ajusta o tamanho da HUD de jogabilidade (corações, botões, mini-mapa, etc.). Não afeta caixas de diálogo ou menus.",
            50, 200, 5,
            [] { return getSettings().game.minimalHUD.getValue(); });
        addOption("Restaurar Bugs da Wii 1.0", getSettings().game.restoreWiiGlitches,
            "Restaura bugs corrigidos da versão Wii USA 1.0, a primeira versão lançada.");
        addOption("Ativar Rotação da Boneca de Link", getSettings().game.enableLinkDollRotation,
            "Ativa a rotação de Link no menu de coleção com o C-Stick.");
        addOption("Ocultar Marcadores de Estátua de Coruja", getSettings().game.removeQuestMapMarkers,
            "Remove os marcadores de Estátua de Coruja concluídos do mapa e do Mini-Mapa.");

        leftPane.add_section("Dificuldade");
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Multiplicador de Dano",
                .getValue = [] { return getSettings().game.damageMultiplier.getValue(); },
                .setValue =
                    [](int value) {
                        getSettings().game.damageMultiplier.setValue(value);
                        config::save();
                    },
                .isDisabled = [] { return getSettings().game.speedrunMode.getValue(); },
                .isModified =
                    [] {
                        return getSettings().game.damageMultiplier.getValue() !=
                               getSettings().game.damageMultiplier.getDefaultValue();
                    },
                .min = 1,
                .max = 8,
                .suffix = "×",
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_text("Multiplica o dano recebido.");
            });
        addSpeedrunDisabledOption(
            "Morte Instantânea", getSettings().game.instantDeath, "Qualquer golpe irá te matar instantaneamente.");
        addSpeedrunDisabledOption("Sem Queda de Corações", getSettings().game.noHeartDrops,
            "Corações nunca cairão de inimigos, vasos e outros locais diversos.");

        leftPane.add_section("Qualidade de Vida");
        addOption("Carteiras Maiores", getSettings().game.biggerWallets,
            "Os tamanhos de carteira são como na versão HD. (500, 1000, 2000)");
        addOption("Desativar Cutscenes de Rúpias", getSettings().game.disableRupeeCutscenes,
            "Rúpias não reproduzirão cutscenes depois que você as coletar pela primeira vez.");
        addOption("Escalada Mais Rápida", getSettings().game.fastClimbing,
            "Escalada mais rápida em escadas e vinhas como na versão HD.");
        addOption("Lágrimas de Luz Mais Rápidas", getSettings().game.fastTears,
            "As Lágrimas de Luz deixadas pelos Insetos das Sombras saem mais rápido como na versão HD.");
        addSpeedrunDisabledOption("Salvamento Automático", getSettings().game.autoSave,
            "Salva automaticamente o jogo ao ir para uma nova área ou abrir a porta de uma masmorra.");
        addOption("Saves Instantâneos", getSettings().game.instantSaves,
            "Pula o atraso ao gravar no Cartão de Memória.");
        addOption("Segurar B para Texto Instantâneo", getSettings().game.instantText,
            "Faz o texto rolar imediatamente ao segurar B.");
        addOption("Sem Animação de Erro ao Escalar", getSettings().game.noMissClimbing,
            "Impede que Link reproduza uma animação de esforço ao se agarrar a beiradas ou "
            "escalar vinhas.");
        addOption("Sem Devolução de Rúpias", getSettings().game.noReturnRupees,
            "Sempre coleta Rúpias mesmo se sua Carteira estiver muito cheia.");
        addOption("Sem Recuo da Espada", getSettings().game.noSwordRecoil,
            "Link não recuará quando sua espada atingir paredes.");
        addOption("Sem 2º Peixe para a Gata", getSettings().game.no2ndFishForCat,
            "Pula a necessidade de pegar um segundo peixe para a gata da Sera.");
        addOption("Pesca por Botão", getSettings().game.buttonFishing,
            "Permite pescar com a Vara de Pesca usando o botão ao qual o item está atribuído.");
        addOption("Mostrar Contagem de Poe no Mapa", getSettings().game.enhancedMapMenus,
            "Exibe o número de Almas Poe coletadas/total de uma região no mapa.");
        addSpeedrunDisabledOption("Canção do Sol (R+X)", getSettings().game.sunsSong,
            "Permite que Wolf Link uive e mude o período do dia.");
        addOption("Transformação Rápida (R+Y)", getSettings().game.enableQuickTransform,
            "Transforma-se instantaneamente ao pressionar R e Y simultaneamente.");

        leftPane.add_section("Speedrun");
        config_bool_select(leftPane, rightPane, getSettings().game.speedrunMode,
            {
                .key = "Modo Speedrun",
                .helpText =
                    "Ativa opções de speedrun enquanto restringe certos modificadores de jogabilidade.",
                .onChange =
                    [](bool enabled) {
                        if (enabled) {
                            resetForSpeedrunMode();
                        } else {
                            restoreFromSpeedrunMode();
                            if (getSettings().game.liveSplitEnabled) {
                                speedrun::disconnectLiveSplit();
                            }
                        }
                        MenuBar::rebuild();
                    },
            });
        config_bool_select(leftPane, rightPane, getSettings().game.liveSplitEnabled,
            {
                .key = "Conexão com LiveSplit",
                .helpText = "Conecta ao servidor do LiveSplit em localhost:16834. Para funcionar, você deve clicar com o botão direito no LiveSplit e ativar Control -> Start TCP Server."
                " Para ver o IGT no LiveSplit, você deve mudar sua comparação para Game Time.",
                .onChange =
                    [](bool enabled) {
                        if (enabled) {
                            speedrun::connectLiveSplit();
                        } else {
                            speedrun::disconnectLiveSplit();
                        }
                    },
                .isDisabled = [] { return IsMobile || !getSettings().game.speedrunMode; },
            });
        config_bool_select(leftPane, rightPane, getSettings().game.showSpeedrunRTATimer,
            {
                .key = "Mostrar RTA",
                .helpText = "Exibe o cronômetro RTA. O IGT está sempre visível.",
                .isDisabled = [] { return !getSettings().game.speedrunMode; },
            });
    });

    add_tab("Trapaças", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        auto addCheat = [&](const Rml::String& key, ConfigVar<bool>& value,
                            const Rml::String& helpText) {
            add_speedrun_disabled_option(leftPane, rightPane, value, key, helpText);
        };

        leftPane.add_section("Recursos");
        addCheat("Corações Infinitos", getSettings().game.infiniteHearts, "Mantém sua vida sempre cheia.");
        addCheat(
            "Flechas Infinitas", getSettings().game.infiniteArrows, "Mantém sua contagem de flechas sempre cheia.");
        addCheat("Sementes Infinitas", getSettings().game.infiniteSeeds, "Mantém suas munições de estilingue (sementes) sempre cheias.");
        addCheat("Bombas Infinitas", getSettings().game.infiniteBombs, "Mantém todas as bolsas de bombas sempre cheias.");
        addCheat("Óleo Infinito", getSettings().game.infiniteOil, "Mantém o óleo da sua lanterna sempre cheio.");
        addCheat("Oxigênio Infinito", getSettings().game.infiniteOxygen,
            "Mantém seu medidor de oxigênio subaquático sempre cheio.");
        addCheat(
            "Rúpias Infinitas", getSettings().game.infiniteRupees, "Mantém sua contagem de rúpias sempre cheia.");
        addCheat("Sem Temporizador de Itens", getSettings().game.enableIndefiniteItemDrops,
            "Itens dropados como rúpias e corações nunca desaparecerão após caírem.");

        leftPane.add_section("Habilidades");
        addCheat(
            "Salto Lunar (R+A)", getSettings().game.moonJump, "Segure R e A para subir no ar.");
        addCheat("Super Gancho", getSettings().game.superClawshot,
            "Estende o comportamento do Gancho além das regras normais do jogo.");
        addCheat("Giro Ampliado Sempre Disponível", getSettings().game.alwaysGreatspin,
            "Permite o ataque Giro Ampliado sem exigir vida cheia.");
        addCheat("Botas de Ferro Rápidas", getSettings().game.enableFastIronBoots,
            "Acelera o movimento enquanto pesado, incluindo o uso das Botas de Ferro, segurar a Bola com Corrente, usar a Armadura Mágica sem rúpias, etc.");
        addCheat("Pode Transformar em Qualquer Lugar", getSettings().game.canTransformAnywhere,
            "Permite transformar-se mesmo que NPCs estejam olhando.");
        addCheat("Rolamento Rápido", getSettings().game.fastRoll,
            "Torna a animação e o movimento de rolamento de Link duas vezes mais rápidos.");
        addCheat("Spinner Rápido", getSettings().game.fastSpinner,
            "Acelera o movimento do Spinner enquanto segura R.");
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Comportamento da Armadura Mágica",
                .getValue =
                    [] {
                        return kMagicArmorModes[static_cast<u8>(getSettings().game.armorRupeeDrain.getValue())];
                    },
                .isDisabled = [] { return getSettings().game.speedrunMode.getValue(); },
                .isModified =
                    [] {
                        return getSettings().game.armorRupeeDrain.getValue() !=
                               getSettings().game.armorRupeeDrain.getDefaultValue();
                    },
            }),
            rightPane, [](Pane& pane) {
                for (int i = 0; i < kMagicArmorModes.size(); i++) {
                    pane.add_button({
                            .text = kMagicArmorModes[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.armorRupeeDrain.getValue() == static_cast<MagicArmorMode>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.armorRupeeDrain.setValue(static_cast<MagicArmorMode>(i));
                            config::save();
                        });
                }
                pane.add_rml(
                    "<br/>Controla o comportamento da Armadura Mágica.");
            });
        addCheat("Inimigos Invencíveis", getSettings().game.invincibleEnemies,
            "Impede que inimigos recebam dano.");
    });

    add_tab("Interface", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Dusklight");
#if DUSK_CAN_OPEN_DATA_FOLDER
        leftPane.register_control(
            leftPane.add_button("Abrir Pasta de Dados").on_pressed([] {
                mDoAud_seStartMenu(kSoundClick);
                data::open_data_path();
            }),
            rightPane, [](Pane& pane) {
                pane.add_text(
                    "Abre a pasta onde o Dusklight armazena configurações, saves, logs, substituições de "
                    "textura e outros dados do aplicativo.");
            });
#endif
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Notificações",
                .getValue = [] {
                    const bool ach = getSettings().game.enableAchievementToasts.getValue();
                    const bool ctl = getSettings().game.enableControllerToasts.getValue();
                    if (!ach && !ctl) {
                        return Rml::String{"Desativado"};
                    }
                    if (ach && ctl) {
                        return Rml::String{"Todas"};
                    }
                    return Rml::String{"Algumas"};
                },
                .isModified = [] {
                    const auto& ach = getSettings().game.enableAchievementToasts;
                    const auto& ctl = getSettings().game.enableControllerToasts;
                    return ach.getValue() != ach.getDefaultValue() || ctl.getValue() != ctl.getDefaultValue();
                },
            }),
            rightPane, [](Pane& pane) {
                pane.clear();
                pane.add_button("Selecionar Todas").on_pressed([] {
                    mDoAud_seStartMenu(kSoundItemChange);
                    getSettings().game.enableAchievementToasts.setValue(true);
                    getSettings().game.enableControllerToasts.setValue(true);
                    config::save();
                });
                pane.add_button("Selecionar Nenhuma").on_pressed([] {
                    mDoAud_seStartMenu(kSoundItemChange);
                    getSettings().game.enableAchievementToasts.setValue(false);
                    getSettings().game.enableControllerToasts.setValue(false);
                    config::save();
                });

                pane.add_section("Tipos");
                pane.add_button(
                    {
                        .text = "Conquistas",
                        .isSelected =
                        [] {
                            return getSettings().game.enableAchievementToasts.getValue();
                        },
                    })
                    .on_pressed([] {
                        mDoAud_seStartMenu(kSoundItemChange);
                        auto& v = getSettings().game.enableAchievementToasts;
                        v.setValue(!v.getValue());
                        config::save();
                    });
                pane.add_button(
                    {
                        .text = "Dispositivo Ausente",
                        .isSelected =
                            [] { return getSettings().game.enableControllerToasts.getValue(); },
                    })
                    .on_pressed([] {
                        mDoAud_seStartMenu(kSoundItemChange);
                        auto& v = getSettings().game.enableControllerToasts;
                        v.setValue(!v.getValue());
                        config::save();
                    });
                pane.add_rml("<br/>Escolha quais notificações podem ser exibidas.");
            });
#if DUSK_ENABLE_SENTRY_NATIVE
        auto& crashReporting = leftPane.add_child<BoolButton>(BoolButton::Props{
            .key = "Relatório de Falhas",
            .getValue =
                [] { return crash_reporting::get_consent() == crash_reporting::Consent::Given; },
            .setValue = [](bool enabled) { crash_reporting::set_consent(enabled); },
            .isDisabled =
                [] {
                    return crash_reporting::get_consent() == crash_reporting::Consent::Unavailable;
                },
            .isModified = [] { return false; },
        });
        leftPane.register_control(crashReporting, rightPane, [](Pane& pane) {
            pane.clear();
            pane.add_rml("O Dusklight pode enviar relatórios de falhas automaticamente para os desenvolvedores. Os relatórios "
                         "de falhas contêm o seguinte:<br/>• Versão do sistema operacional<br/>• Arquitetura "
                         "da CPU<br/>• Modelo da GPU e versão do driver<br/>• Caminhos de arquivo (podem "
                         "incluir o nome de usuário da conta)<br/>• Rastreamento de pilha (stack trace)");
        });
#endif
        config_bool_select(leftPane, rightPane, getSettings().backend.skipPreLaunchUI,
            {
                .key = "Pular Menu Principal do Dusklight",
                .helpText = "Ao iniciar o Dusklight, pula o menu principal e inicia direto o "
                            "jogo se uma imagem de disco estiver disponível.",
            });
        config_bool_select(leftPane, rightPane, getSettings().backend.checkForUpdates,
            {
                .key = "Verificar Atualizações",
                .helpText = "Verifica os lançamentos no GitHub em busca de uma nova versão do Dusklight ao iniciar.<br/><br/>"
                            "Nenhuma informação pessoal é transmitida ou coletada.",
            });
#ifdef DUSK_DISCORD
        config_bool_select(leftPane, rightPane, getSettings().game.enableDiscordPresence,
            {
                .key = "Ativar Discord Rich Presence",
                .helpText = "Permite que o Dusklight se integre ao Discord Rich Presence. Isso permite que o Discord mostre seu status em jogo.",
                .onChange = [](bool enabled) {
                    if (enabled) {
                        dusk::discord::initialize();
                    } else {
                        dusk::discord::shutdown();
                    }
                },
            });
#endif
        config_bool_select(leftPane, rightPane, getSettings().backend.enableAdvancedSettings,
            {
                .key = "Ativar Configurações Avançadas",
                .icon = "warning",
                .helpText = "Mostra configurações avançadas e ferramentas de depuração com "
                            "Shift+F1.<br/><br/><icon class=\"warning\"/> AVISO: Ferramentas de depuração "
                            "podem facilmente corromper seu jogo. Não use em um save que você usa normalmente!",
                .onChange = [](bool) { MenuBar::rebuild(); },
                .isDisabled = [] { return getSettings().game.speedrunMode.getValue(); },
            });
        config_bool_select(leftPane, rightPane, getSettings().game.showInputViewer,
            {
                .key = "Mostrar Visualizador de Entradas",
                .helpText = "Exibe uma sobreposição de entradas do controle durante o jogo.",
            });
        config_bool_select(leftPane, rightPane, getSettings().game.showInputViewerGyro,
            {
                .key = "Mostrar Visualizador de Entrada do Giroscópio",
                .helpText = "Mostra os valores do sensor de giroscópio no visualizador de entradas.",
                .isDisabled = [] { return !getSettings().game.showInputViewer; },
            });
        leftPane.add_section("Jogo");
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Modo de Escala de Menu",
                .getValue =
                    [] {
                        return kMenuScalingModeLabels[static_cast<u8>(
                            getSettings().game.menuScalingMode.getValue())];
                    },
                .isModified =
                    [] {
                        const auto& mode = getSettings().game.menuScalingMode;
                        return mode.getValue() != mode.getDefaultValue();
                    },
            }),
            rightPane, [](Pane& pane) {
                for (int i = 0; i < static_cast<int>(kMenuScalingModeLabels.size()); ++i) {
                    pane
                        .add_button({
                            .text = kMenuScalingModeLabels[i],
                            .isSelected =
                                [i] {
                                    return getSettings().game.menuScalingMode.getValue() ==
                                           static_cast<MenuScaling>(i);
                                },
                        })
                        .on_pressed([i] {
                            mDoAud_seStartMenu(kSoundItemChange);
                            getSettings().game.menuScalingMode.setValue(
                                static_cast<MenuScaling>(i));
                            config::save();
                        });
                }
                pane.add_rml("<br/>Altera como os menus de Coleção e Seleção de Arquivo se ajustam à sua "
                             "proporção de tela.");
            });
        config_bool_select(leftPane, rightPane, getSettings().game.hideTvSettingsScreen,
            {
                .key = "Pular Tela de Configurações de TV",
                .helpText = "Pula a tela de calibração de TV exibida ao carregar um save.",
            });
        add_speedrun_disabled_option(leftPane, rightPane, getSettings().game.recordingMode,
            "Modo de Gravação",
            "Desativa a HUD do jogo e toda a música de fundo.<br/><br/>Útil para gravar vídeos.");
    });
}

void SettingsWindow::update() {
    if (mPrelaunch && top_document() == this) {
        try_push_verification_modal(*this);
    }

    Window::update();
}

void SettingsWindow::hide(bool close) {
    config::save();
    Window::hide(close);
}

}  // namespace dusk::ui
