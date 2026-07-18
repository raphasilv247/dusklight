#include "editor.hpp"

#include <RmlUi/Core.h>
#include <fmt/format.h>

#include "bool_button.hpp"
#include "button.hpp"
#include "d/actor/d_a_player.h"
#include "d/d_kankyo.h"
#include "d/d_meter2_info.h"
#include "dusk/map_loader_definitions.h"
#include "number_button.hpp"
#include "pane.hpp"
#include "select_button.hpp"
#include "string_button.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dusk::ui {

Rml::String stage_option_label(const MapEntry& map, bool showInternalNames) {
    return showInternalNames ? fmt::format("{} ({})", map.mapName, map.mapFile) : map.mapName;
}

Rml::String stage_label_for_file(const Rml::String& stageFile, bool showInternalNames) {
    for (const auto& region : gameRegions) {
        for (const auto& map : region.maps) {
            if (stageFile == map.mapFile) {
                return stage_option_label(map, showInternalNames);
            }
        }
    }
    return stageFile;
}

void populate_stage_picker(Pane& pane, std::function<Rml::String()> getStageFile,
    std::function<void(const char*)> setStageFile, bool showInternalNames) {
    pane.clear();
    for (const auto& region : gameRegions) {
        pane.add_section(region.regionName);
        for (const auto& map : region.maps) {
            pane.add_button({
                                .text = stage_option_label(map, showInternalNames),
                                .isSelected =
                                    [getStageFile, stageFile = map.mapFile] {
                                        return getStageFile() == stageFile;
                                    },
                            })
                .on_pressed([setStageFile, stageFile = map.mapFile] {
                    mDoAud_seStartMenu(kSoundItemChange);
                    setStageFile(stageFile);
                });
        }
    }
}

namespace {

bool has_save_data() {
    return dComIfGs_getSaveData() != nullptr;
}

dSv_player_status_a_c* get_player_status() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getPlayer().getPlayerStatusA();
}

dSv_player_status_b_c* get_player_status_b() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getPlayer().getPlayerStatusB();
}

dSv_player_return_place_c* get_player_return_place() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getPlayer().getPlayerReturnPlace();
}

dSv_horse_place_c* get_horse_place() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getPlayer().getHorsePlace();
}

dSv_player_item_c* get_player_item() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getPlayer().getItem();
}

dSv_player_item_record_c* get_player_item_record() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getPlayer().getItemRecord();
}

dSv_player_item_max_c* get_player_item_max() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getPlayer().getItemMax();
}

dSv_fishing_info_c* get_player_fishing_info() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getPlayer().getFishingInfo();
}

dSv_MiniGame_c* get_minigame() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getMiniGame();
}

dSv_player_config_c* get_player_config() {
    if (!has_save_data()) {
        return nullptr;
    }
    return &dComIfGs_getSaveData()->getPlayer().getConfig();
}

template <size_t Size>
Rml::String fixed_string(const char (&value)[Size]) {
    size_t length = 0;
    while (length < Size && value[length] != '\0') {
        ++length;
    }
    return Rml::String(value, length);
}

template <size_t Size>
void set_fixed_string(char (&dest)[Size], const Rml::String& value) {
    std::memset(dest, 0, Size);
    std::memcpy(dest, value.data(), std::min(value.size(), Size - 1));
}

void skip_whitespace(const char*& cursor) {
    while (std::isspace(static_cast<unsigned char>(*cursor))) {
        ++cursor;
    }
}

bool parse_float_token(const char*& cursor, float& parsedValue) {
    skip_whitespace(cursor);
    char* end = nullptr;
    parsedValue = std::strtof(cursor, &end);
    if (end == cursor) {
        return false;
    }
    cursor = end;
    skip_whitespace(cursor);
    return true;
}

bool consume_comma(const char*& cursor) {
    skip_whitespace(cursor);
    if (*cursor != ',') {
        return false;
    }
    ++cursor;
    return true;
}

bool parse_vec3(const Rml::String& value, float& x, float& y, float& z) {
    const char* cursor = value.c_str();
    if (!parse_float_token(cursor, x) || !consume_comma(cursor) || !parse_float_token(cursor, y) ||
        !consume_comma(cursor) || !parse_float_token(cursor, z))
    {
        return false;
    }
    skip_whitespace(cursor);
    return *cursor == '\0';
}

Rml::String get_player_name() {
    if (!has_save_data()) {
        return "";
    }
    return dComIfGs_getPlayerName().buffer;
}

void set_player_name(Rml::String name) {
    dComIfGs_setPlayerName(name.c_str());
}

Rml::String get_horse_name() {
    if (!has_save_data()) {
        return "";
    }
    return dComIfGs_getHorseName().buffer;
}

void set_horse_name(Rml::String name) {
    dComIfGs_setHorseName(name.c_str());
}

enum ItemType {
    ITEMTYPE_DEFAULT_e,
    ITEMTYPE_EQUIP_e,
};

struct itemInfo {
    std::string m_name;
    u8 m_type = ITEMTYPE_DEFAULT_e;
};

std::map<int, itemInfo> itemMap = {
    {dItemNo_HEART_e, {"Coração"}},
    {dItemNo_GREEN_RUPEE_e, {"Rúpia Verde"}},
    {dItemNo_BLUE_RUPEE_e, {"Rúpia Azul"}},
    {dItemNo_YELLOW_RUPEE_e, {"Rúpia Amarela"}},
    {dItemNo_RED_RUPEE_e, {"Rúpia Vermelha"}},
    {dItemNo_PURPLE_RUPEE_e, {"Rúpia Roxa"}},
    {dItemNo_ORANGE_RUPEE_e, {"Rúpia Laranja"}},
    {dItemNo_SILVER_RUPEE_e, {"Rúpia Prata"}},
    {dItemNo_S_MAGIC_e, {"Magia Pequena"}},
    {dItemNo_L_MAGIC_e, {"Magia Grande"}},
    {dItemNo_BOMB_5_e, {"Bombas (5)"}},
    {dItemNo_BOMB_10_e, {"Bombas (10)"}},
    {dItemNo_BOMB_20_e, {"Bombas (20)"}},
    {dItemNo_BOMB_30_e, {"Bombas (30)"}},
    {dItemNo_ARROW_10_e, {"Flechas (10)"}},
    {dItemNo_ARROW_20_e, {"Flechas (20)"}},
    {dItemNo_ARROW_30_e, {"Flechas (30)"}},
    {dItemNo_ARROW_1_e, {"Flechas (1)"}},
    {dItemNo_PACHINKO_SHOT_e, {"Sementes de Abóbora"}},
    {dItemNo_NOENTRY_19_e, {"Reservado"}},
    {dItemNo_NOENTRY_20_e, {"Reservado"}},
    {dItemNo_NOENTRY_21_e, {"Reservado"}},
    {dItemNo_WATER_BOMB_5_e, {"Bombas Aquáticas (5)"}},
    {dItemNo_WATER_BOMB_10_e, {"Bombas Aquáticas (10)"}},
    {dItemNo_WATER_BOMB_20_e, {"Bombas Aquáticas (20)"}},
    {dItemNo_WATER_BOMB_30_e, {"Bombas Aquáticas (30)"}},
    {dItemNo_BOMB_INSECT_5_e, {"Bomblings (5)"}},
    {dItemNo_BOMB_INSECT_10_e, {"Bomblings (10)"}},
    {dItemNo_BOMB_INSECT_20_e, {"Bomblings (20)"}},
    {dItemNo_BOMB_INSECT_30_e, {"Bomblings (30)"}},
    {dItemNo_RECOVERY_FAILY_e, {"Fada"}},
    {dItemNo_TRIPLE_HEART_e, {"Corações Triplos"}},
    {dItemNo_SMALL_KEY_e, {"Chave Pequena"}},
    {dItemNo_KAKERA_HEART_e, {"Pedaço de Coração"}},
    {dItemNo_UTAWA_HEART_e, {"Contêiner de Coração"}},
    {dItemNo_MAP_e, {"Mapa da Masmorra"}},
    {dItemNo_COMPUS_e, {"Bússola"}},
    {dItemNo_DUNGEON_EXIT_e, {"Ooccoo Sr. (Primeira Vez)", ITEMTYPE_EQUIP_e}},
    {dItemNo_BOSS_KEY_e, {"Chave do Chefe"}},
    {dItemNo_DUNGEON_BACK_e, {"Ooccoo Jr.", ITEMTYPE_EQUIP_e}},
    {dItemNo_SWORD_e, {"Espada de Ordon"}},
    {dItemNo_MASTER_SWORD_e, {"Master Sword"}},
    {dItemNo_WOOD_SHIELD_e, {"Escudo de Ordon"}},
    {dItemNo_SHIELD_e, {"Escudo de Madeira"}},
    {dItemNo_HYLIA_SHIELD_e, {"Escudo Hyliano"}},
    {dItemNo_TKS_LETTER_e, {"Bilhete de Ooccoo", ITEMTYPE_EQUIP_e}},
    {dItemNo_WEAR_CASUAL_e, {"Roupas de Ordon"}},
    {dItemNo_WEAR_KOKIRI_e, {"Roupas do Herói"}},
    {dItemNo_ARMOR_e, {"Armadura Mágica"}},
    {dItemNo_WEAR_ZORA_e, {"Armadura Zora"}},
    {dItemNo_MAGIC_LV1_e, {"Magia Nível 1"}},
    {dItemNo_DUNGEON_EXIT_2_e, {"Ooccoo Sr.", ITEMTYPE_EQUIP_e}},
    {dItemNo_WALLET_LV1_e, {"Carteira"}},
    {dItemNo_WALLET_LV2_e, {"Carteira Grande"}},
    {dItemNo_WALLET_LV3_e, {"Carteira Gigante"}},
    {dItemNo_NOENTRY_55_e, {"Reservado"}},
    {dItemNo_NOENTRY_56_e, {"Reservado"}},
    {dItemNo_NOENTRY_57_e, {"Reservado"}},
    {dItemNo_NOENTRY_58_e, {"Reservado"}},
    {dItemNo_NOENTRY_59_e, {"Reservado"}},
    {dItemNo_NOENTRY_60_e, {"Reservado"}},
    {dItemNo_ZORAS_JEWEL_e, {"Brinco de Coral", ITEMTYPE_EQUIP_e}},
    {dItemNo_HAWK_EYE_e, {"Olho de Falcão", ITEMTYPE_EQUIP_e}},
    {dItemNo_WOOD_STICK_e, {"Espada de Madeira"}},
    {dItemNo_BOOMERANG_e, {"Bumerangue Furacão", ITEMTYPE_EQUIP_e}},
    {dItemNo_SPINNER_e, {"Spinner", ITEMTYPE_EQUIP_e}},
    {dItemNo_IRONBALL_e, {"Bola com Corrente", ITEMTYPE_EQUIP_e}},
    {dItemNo_BOW_e, {"Arco do Herói", ITEMTYPE_EQUIP_e}},
    {dItemNo_HOOKSHOT_e, {"Gancho", ITEMTYPE_EQUIP_e}},
    {dItemNo_HVY_BOOTS_e, {"Botas de Ferro", ITEMTYPE_EQUIP_e}},
    {dItemNo_COPY_ROD_e, {"Cetro Dominador", ITEMTYPE_EQUIP_e}},
    {dItemNo_W_HOOKSHOT_e, {"Ganchos Duplos", ITEMTYPE_EQUIP_e}},
    {dItemNo_KANTERA_e, {"Lanterna", ITEMTYPE_EQUIP_e}},
    {dItemNo_LIGHT_SWORD_e, {"Espada de Luz"}},
    {dItemNo_FISHING_ROD_1_e, {"Vara de Pesca", ITEMTYPE_EQUIP_e}},
    {dItemNo_PACHINKO_e, {"Estilingue", ITEMTYPE_EQUIP_e}},
    {dItemNo_COPY_ROD_2_e, {"Cetro Dominador (Descarregado)"}},
    {dItemNo_NOENTRY_77_e, {"Reservado"}},
    {dItemNo_NOENTRY_78_e, {"Reservado"}},
    {dItemNo_BOMB_BAG_LV2_e, {"Bolsa de Bombas Gigante"}},
    {dItemNo_BOMB_BAG_LV1_e, {"Bolsa de Bombas Vazia", ITEMTYPE_EQUIP_e}},
    {dItemNo_BOMB_IN_BAG_e, {"Bolsa de Bombas"}},
    {dItemNo_NOENTRY_82_e, {"Reservado"}},
    {dItemNo_LIGHT_ARROW_e, {"Flecha de Luz"}},
    {dItemNo_ARROW_LV1_e, {"Aljava"}},
    {dItemNo_ARROW_LV2_e, {"Aljava Grande"}},
    {dItemNo_ARROW_LV3_e, {"Aljava Gigante"}},
    {dItemNo_NOENTRY_87_e, {"Reservado"}},
    {dItemNo_LURE_ROD_e, {"Vara de Pesca (Isca)"}},
    {dItemNo_BOMB_ARROW_e, {"Flecha-Bomba"}},
    {dItemNo_HAWK_ARROW_e, {"Flecha do Falcão"}},
    {dItemNo_BEE_ROD_e, {"Vara de Pesca (Larva de Abelha)", ITEMTYPE_EQUIP_e}},
    {dItemNo_JEWEL_ROD_e, {"Vara de Pesca (Brinco)", ITEMTYPE_EQUIP_e}},
    {dItemNo_WORM_ROD_e, {"Vara de Pesca (Minhoca)", ITEMTYPE_EQUIP_e}},
    {dItemNo_JEWEL_BEE_ROD_e, {"Vara de Pesca (Brinco + Larva de Abelha)", ITEMTYPE_EQUIP_e}},
    {dItemNo_JEWEL_WORM_ROD_e, {"Vara de Pesca (Brinco + Minhoca)", ITEMTYPE_EQUIP_e}},
    {dItemNo_EMPTY_BOTTLE_e, {"Garrafa Vazia", ITEMTYPE_EQUIP_e}},
    {dItemNo_RED_BOTTLE_e, {"Poção Vermelha", ITEMTYPE_EQUIP_e}},
    {dItemNo_GREEN_BOTTLE_e, {"Poção Verde", ITEMTYPE_EQUIP_e}},
    {dItemNo_BLUE_BOTTLE_e, {"Poção Azul", ITEMTYPE_EQUIP_e}},
    {dItemNo_MILK_BOTTLE_e, {"Garrafa de Leite", ITEMTYPE_EQUIP_e}},
    {dItemNo_HALF_MILK_BOTTLE_e, {"Garrafa de Leite pela Metade", ITEMTYPE_EQUIP_e}},
    {dItemNo_OIL_BOTTLE_e, {"Óleo de Lanterna", ITEMTYPE_EQUIP_e}},
    {dItemNo_WATER_BOTTLE_e, {"Garrafa de Água", ITEMTYPE_EQUIP_e}},
    {dItemNo_OIL_BOTTLE_2_e, {"Óleo de Lanterna (Coletado)"}},
    {dItemNo_RED_BOTTLE_2_e, {"Poção Vermelha (Coletada)"}},
    {dItemNo_UGLY_SOUP_e, {"Sopa Ruim", ITEMTYPE_EQUIP_e}},
    {dItemNo_HOT_SPRING_e, {"Água da Fonte Termal", ITEMTYPE_EQUIP_e}},
    {dItemNo_FAIRY_e, {"Fada", ITEMTYPE_EQUIP_e}},
    {dItemNo_HOT_SPRING_2_e, {"Água da Fonte Termal (Loja)"}},
    {dItemNo_OIL2_e, {"Recarga de Lanterna (Coletada)"}},
    {dItemNo_OIL_e, {"Recarga de Lanterna (Loja)"}},
    {dItemNo_NORMAL_BOMB_e, {"Bombas", ITEMTYPE_EQUIP_e}},
    {dItemNo_WATER_BOMB_e, {"Bombas Aquáticas", ITEMTYPE_EQUIP_e}},
    {dItemNo_POKE_BOMB_e, {"Bomblings", ITEMTYPE_EQUIP_e}},
    {dItemNo_FAIRY_DROP_e, {"Lágrimas da Grande Fada", ITEMTYPE_EQUIP_e}},
    {dItemNo_WORM_e, {"Minhoca", ITEMTYPE_EQUIP_e}},
    {dItemNo_DROP_BOTTLE_e, {"Lágrimas da Grande Fada (Jovani)"}},
    {dItemNo_BEE_CHILD_e, {"Larva de Abelha", ITEMTYPE_EQUIP_e}},
    {dItemNo_CHUCHU_RARE_e, {"Geleia de Chu Rara", ITEMTYPE_EQUIP_e}},
    {dItemNo_CHUCHU_RED_e, {"Geleia de Chu Vermelha", ITEMTYPE_EQUIP_e}},
    {dItemNo_CHUCHU_BLUE_e, {"Geleia de Chu Azul", ITEMTYPE_EQUIP_e}},
    {dItemNo_CHUCHU_GREEN_e, {"Geleia de Chu Verde", ITEMTYPE_EQUIP_e}},
    {dItemNo_CHUCHU_YELLOW_e, {"Geleia de Chu Amarela", ITEMTYPE_EQUIP_e}},
    {dItemNo_CHUCHU_PURPLE_e, {"Geleia de Chu Roxa", ITEMTYPE_EQUIP_e}},
    {dItemNo_LV1_SOUP_e, {"Sopa Simples", ITEMTYPE_EQUIP_e}},
    {dItemNo_LV2_SOUP_e, {"Sopa Boa", ITEMTYPE_EQUIP_e}},
    {dItemNo_LV3_SOUP_e, {"Sopa Soberba", ITEMTYPE_EQUIP_e}},
    {dItemNo_LETTER_e, {"Carta de Renado", ITEMTYPE_EQUIP_e}},
    {dItemNo_BILL_e, {"Fatura", ITEMTYPE_EQUIP_e}},
    {dItemNo_WOOD_STATUE_e, {"Estátua de Madeira", ITEMTYPE_EQUIP_e}},
    {dItemNo_IRIAS_PENDANT_e, {"Amuleto de Ilia", ITEMTYPE_EQUIP_e}},
    {dItemNo_HORSE_FLUTE_e, {"Chamado do Cavalo", ITEMTYPE_EQUIP_e}},
    {dItemNo_NOENTRY_133_e, {"Reservado"}},
    {dItemNo_NOENTRY_134_e, {"Reservado"}},
    {dItemNo_NOENTRY_135_e, {"Reservado"}},
    {dItemNo_NOENTRY_136_e, {"Reservado"}},
    {dItemNo_NOENTRY_137_e, {"Reservado"}},
    {dItemNo_NOENTRY_138_e, {"Reservado"}},
    {dItemNo_NOENTRY_139_e, {"Reservado"}},
    {dItemNo_NOENTRY_140_e, {"Reservado"}},
    {dItemNo_NOENTRY_141_e, {"Reservado"}},
    {dItemNo_NOENTRY_142_e, {"Reservado"}},
    {dItemNo_NOENTRY_143_e, {"Reservado"}},
    {dItemNo_RAFRELS_MEMO_e, {"Bilhete de Auru", ITEMTYPE_EQUIP_e}},
    {dItemNo_ASHS_SCRIBBLING_e, {"Esboço de Ashei", ITEMTYPE_EQUIP_e}},
    {dItemNo_NOENTRY_146_e, {"Reservado"}},
    {dItemNo_NOENTRY_147_e, {"Reservado"}},
    {dItemNo_NOENTRY_148_e, {"Reservado"}},
    {dItemNo_NOENTRY_149_e, {"Reservado"}},
    {dItemNo_NOENTRY_150_e, {"Reservado"}},
    {dItemNo_NOENTRY_151_e, {"Reservado"}},
    {dItemNo_NOENTRY_152_e, {"Reservado"}},
    {dItemNo_NOENTRY_153_e, {"Reservado"}},
    {dItemNo_NOENTRY_154_e, {"Reservado"}},
    {dItemNo_NOENTRY_155_e, {"Reservado"}},
    {dItemNo_CHUCHU_YELLOW2_e, {"Recarga de Lanterna (Chu Amarelo)"}},
    {dItemNo_OIL_BOTTLE3_e, {"Óleo de Lanterna (Coro)"}},
    {dItemNo_SHOP_BEE_CHILD_e, {"Larva de Abelha (Loja)"}},
    {dItemNo_CHUCHU_BLACK_e, {"Geleia de Chu Preta", ITEMTYPE_EQUIP_e}},
    {dItemNo_LIGHT_DROP_e, {"Lágrima de Luz"}},
    {dItemNo_DROP_CONTAINER_e, {"Vaso de Luz (Faron)"}},
    {dItemNo_DROP_CONTAINER02_e, {"Vaso de Luz (Eldin)"}},
    {dItemNo_DROP_CONTAINER03_e, {"Vaso de Luz (Lanayru)"}},
    {dItemNo_FILLED_CONTAINER_e, {"Vaso de Luz (Cheio)"}},
    {dItemNo_MIRROR_PIECE_2_e, {"Fragmento do Espelho (Ruínas de Snowpeak)"}},
    {dItemNo_MIRROR_PIECE_3_e, {"Fragmento do Espelho (Templo do Tempo)"}},
    {dItemNo_MIRROR_PIECE_4_e, {"Fragmento do Espelho (Cidade no Céu)"}},
    {dItemNo_NOENTRY_168_e, {"Reservado"}},
    {dItemNo_NOENTRY_169_e, {"Reservado"}},
    {dItemNo_NOENTRY_170_e, {"Reservado"}},
    {dItemNo_NOENTRY_171_e, {"Reservado"}},
    {dItemNo_NOENTRY_172_e, {"Reservado"}},
    {dItemNo_NOENTRY_173_e, {"Reservado"}},
    {dItemNo_NOENTRY_174_e, {"Reservado"}},
    {dItemNo_NOENTRY_175_e, {"Reservado"}},
    {dItemNo_SMELL_YELIA_POUCH_e, {"Aroma de Ilia"}},
    {dItemNo_SMELL_PUMPKIN_e, {"Aroma de Abóbora"}},
    {dItemNo_SMELL_POH_e, {"Aroma de Poe"}},
    {dItemNo_SMELL_FISH_e, {"Aroma de Peixe Fedorento"}},
    {dItemNo_SMELL_CHILDREN_e, {"Aroma da Juventude"}},
    {dItemNo_SMELL_MEDICINE_e, {"Aroma de Remédio"}},
    {dItemNo_NOENTRY_182_e, {"Reservado"}},
    {dItemNo_NOENTRY_183_e, {"Reservado"}},
    {dItemNo_NOENTRY_184_e, {"Reservado"}},
    {dItemNo_NOENTRY_185_e, {"Reservado"}},
    {dItemNo_NOENTRY_186_e, {"Reservado"}},
    {dItemNo_NOENTRY_187_e, {"Reservado"}},
    {dItemNo_NOENTRY_188_e, {"Reservado"}},
    {dItemNo_NOENTRY_189_e, {"Reservado"}},
    {dItemNo_NOENTRY_190_e, {"Reservado"}},
    {dItemNo_NOENTRY_191_e, {"Reservado"}},
    {dItemNo_M_BEETLE_e, {"Besouro (M)"}},
    {dItemNo_F_BEETLE_e, {"Besouro (F)"}},
    {dItemNo_M_BUTTERFLY_e, {"Borboleta (M)"}},
    {dItemNo_F_BUTTERFLY_e, {"Borboleta (F)"}},
    {dItemNo_M_STAG_BEETLE_e, {"Besouro-Cervo (M)"}},
    {dItemNo_F_STAG_BEETLE_e, {"Besouro-Cervo (F)"}},
    {dItemNo_M_GRASSHOPPER_e, {"Gafanhoto (M)"}},
    {dItemNo_F_GRASSHOPPER_e, {"Gafanhoto (F)"}},
    {dItemNo_M_NANAFUSHI_e, {"Inseto-Pau (M)"}},
    {dItemNo_F_NANAFUSHI_e, {"Inseto-Pau (F)"}},
    {dItemNo_M_DANGOMUSHI_e, {"Tatuzinho (M)"}},
    {dItemNo_F_DANGOMUSHI_e, {"Tatuzinho (F)"}},
    {dItemNo_M_MANTIS_e, {"Louva-a-deus (M)"}},
    {dItemNo_F_MANTIS_e, {"Louva-a-deus (F)"}},
    {dItemNo_M_LADYBUG_e, {"Joaninha (M)"}},
    {dItemNo_F_LADYBUG_e, {"Joaninha (F)"}},
    {dItemNo_M_SNAIL_e, {"Caracol (M)"}},
    {dItemNo_F_SNAIL_e, {"Caracol (F)"}},
    {dItemNo_M_DRAGONFLY_e, {"Libélula (M)"}},
    {dItemNo_F_DRAGONFLY_e, {"Libélula (F)"}},
    {dItemNo_M_ANT_e, {"Formiga (M)"}},
    {dItemNo_F_ANT_e, {"Formiga (F)"}},
    {dItemNo_M_MAYFLY_e, {"Efêmera (M)"}},
    {dItemNo_F_MAYFLY_e, {"Efêmera (F)"}},
    {dItemNo_NOENTRY_216_e, {"Reservado"}},
    {dItemNo_NOENTRY_217_e, {"Reservado"}},
    {dItemNo_NOENTRY_218_e, {"Reservado"}},
    {dItemNo_NOENTRY_219_e, {"Reservado"}},
    {dItemNo_NOENTRY_220_e, {"Reservado"}},
    {dItemNo_NOENTRY_221_e, {"Reservado"}},
    {dItemNo_NOENTRY_222_e, {"Reservado"}},
    {dItemNo_NOENTRY_223_e, {"Reservado"}},
    {dItemNo_POU_SPIRIT_e, {"Alma Poe"}},
    {dItemNo_NOENTRY_225_e, {"Reservado"}},
    {dItemNo_NOENTRY_226_e, {"Reservado"}},
    {dItemNo_NOENTRY_227_e, {"Reservado"}},
    {dItemNo_NOENTRY_228_e, {"Reservado"}},
    {dItemNo_NOENTRY_229_e, {"Reservado"}},
    {dItemNo_NOENTRY_230_e, {"Reservado"}},
    {dItemNo_NOENTRY_231_e, {"Reservado"}},
    {dItemNo_NOENTRY_232_e, {"Reservado"}},
    {dItemNo_ANCIENT_DOCUMENT_e, {"Livro Ancestral do Céu", ITEMTYPE_EQUIP_e}},
    {dItemNo_AIR_LETTER_e, {"Livro Ancestral do Céu (Parcial)", ITEMTYPE_EQUIP_e}},
    {dItemNo_ANCIENT_DOCUMENT2_e, {"Livro Ancestral do Céu (Completo)", ITEMTYPE_EQUIP_e}},
    {dItemNo_LV7_DUNGEON_EXIT_e, {"Ooccoo Sr. (Cidade no Céu)"}},
    {dItemNo_LINKS_SAVINGS_e, {"Rúpia Roxa (Poupança de Link)"}},
    {dItemNo_SMALL_KEY2_e, {"Chave Pequena (Portão Norte de Faron)"}},
    {dItemNo_POU_FIRE1_e, {"Fogo Poe 1"}},
    {dItemNo_POU_FIRE2_e, {"Fogo Poe 2"}},
    {dItemNo_POU_FIRE3_e, {"Fogo Poe 3"}},
    {dItemNo_POU_FIRE4_e, {"Fogo Poe 4"}},
    {dItemNo_BOSSRIDER_KEY_e, {"Chaves do Campo de Hyrule"}},
    {dItemNo_TOMATO_PUREE_e, {"Abóbora de Ordon", ITEMTYPE_EQUIP_e}},
    {dItemNo_TASTE_e, {"Queijo de Cabra de Ordon", ITEMTYPE_EQUIP_e}},
    {dItemNo_LV5_BOSS_KEY_e, {"Chave do Quarto"}},
    {dItemNo_SURFBOARD_e, {"Folha Surfista"}},
    {dItemNo_KANTERA2_e, {"Lanterna (Recuperada)"}},
    {dItemNo_L2_KEY_PIECES1_e, {"Fragmento de Chave (1)"}},
    {dItemNo_L2_KEY_PIECES2_e, {"Fragmento de Chave (2)"}},
    {dItemNo_L2_KEY_PIECES3_e, {"Fragmento de Chave (3)"}},
    {dItemNo_KEY_OF_CARAVAN_e, {"Chave do Acampamento Bulblin"}},
    {dItemNo_LV2_BOSS_KEY_e, {"Chave do Chefe das Minas Goron"}},
    {dItemNo_KEY_OF_FILONE_e, {"Chave do Portão Sul de Faron"}},
    {dItemNo_NONE_e, {"Nenhum"}},
};

Rml::String get_item_name(u8 id) {
    const auto it = itemMap.find(id);
    if (it == itemMap.end()) {
        return fmt::format("Item {}", id);
    }
    return it->second.m_name;
}

Rml::String item_label_for_slot(u8 slot) {
    if (slot == 0xFF) {
        return "Nenhum";
    }
    const auto id = dComIfGs_getSaveData()->getPlayer().getItem().mItems[slot];
    return fmt::format("Slot {0} ({1})", slot, get_item_name(id));
}

struct NamedIndexEntry {
    const char* name;
    u8 index;
};

struct NamedFlagEntry {
    const char* name;
    u16 flag;
};

struct BugSpeciesEntry {
    const char* name;
    u8 maleItem;
    u8 femaleItem;
    u16 maleTurnInFlag;
    u16 femaleTurnInFlag;
};

struct FishSpeciesEntry {
    const char* name;
    u8 index;
};

constexpr std::array<u8, 4> swordEntries = {
    dItemNo_SWORD_e,
    dItemNo_MASTER_SWORD_e,
    dItemNo_WOOD_STICK_e,
    dItemNo_LIGHT_SWORD_e,
};

constexpr std::array<u8, 3> shieldEntries = {
    dItemNo_SHIELD_e,
    dItemNo_WOOD_SHIELD_e,
    dItemNo_HYLIA_SHIELD_e,
};

constexpr std::array<u8, 5> smellEntries = {
    dItemNo_SMELL_CHILDREN_e,
    dItemNo_SMELL_YELIA_POUCH_e,
    dItemNo_SMELL_POH_e,
    dItemNo_SMELL_FISH_e,
    dItemNo_SMELL_MEDICINE_e,
};

constexpr std::array fusedShadowEntries = {
    NamedIndexEntry{"Templo da Floresta", 0},
    NamedIndexEntry{"Minas Goron", 1},
    NamedIndexEntry{"Templo do Leito do Lago", 2},
};

constexpr std::array mirrorShardEntries = {
    NamedIndexEntry{"Ruínas de Snowpeak", 1},
    NamedIndexEntry{"Templo do Tempo", 2},
    NamedIndexEntry{"Cidade no Céu", 3},
};

constexpr std::array bugSpeciesEntries = {
    BugSpeciesEntry{"Formiga", dItemNo_M_ANT_e, dItemNo_F_ANT_e, dSv_event_flag_c::F_0421,
        dSv_event_flag_c::F_0422},
    BugSpeciesEntry{"Efêmera", dItemNo_M_MAYFLY_e, dItemNo_F_MAYFLY_e, dSv_event_flag_c::F_0423,
        dSv_event_flag_c::F_0424},
    BugSpeciesEntry{"Besouro", dItemNo_M_BEETLE_e, dItemNo_F_BEETLE_e, dSv_event_flag_c::F_0401,
        dSv_event_flag_c::F_0402},
    BugSpeciesEntry{"Louva-a-deus", dItemNo_M_MANTIS_e, dItemNo_F_MANTIS_e, dSv_event_flag_c::F_0413,
        dSv_event_flag_c::F_0414},
    BugSpeciesEntry{"Besouro-Cervo", dItemNo_M_STAG_BEETLE_e, dItemNo_F_STAG_BEETLE_e,
        dSv_event_flag_c::F_0405, dSv_event_flag_c::F_0406},
    BugSpeciesEntry{"Tatuzinho", dItemNo_M_DANGOMUSHI_e, dItemNo_F_DANGOMUSHI_e,
        dSv_event_flag_c::F_0411, dSv_event_flag_c::F_0412},
    BugSpeciesEntry{"Borboleta", dItemNo_M_BUTTERFLY_e, dItemNo_F_BUTTERFLY_e,
        dSv_event_flag_c::F_0403, dSv_event_flag_c::F_0404},
    BugSpeciesEntry{"Joaninha", dItemNo_M_LADYBUG_e, dItemNo_F_LADYBUG_e, dSv_event_flag_c::F_0415,
        dSv_event_flag_c::F_0416},
    BugSpeciesEntry{"Caracol", dItemNo_M_SNAIL_e, dItemNo_F_SNAIL_e, dSv_event_flag_c::F_0417,
        dSv_event_flag_c::F_0418},
    BugSpeciesEntry{"Inseto-Pau", dItemNo_M_NANAFUSHI_e, dItemNo_F_NANAFUSHI_e,
        dSv_event_flag_c::F_0409, dSv_event_flag_c::F_0410},
    BugSpeciesEntry{"Gafanhoto", dItemNo_M_GRASSHOPPER_e, dItemNo_F_GRASSHOPPER_e,
        dSv_event_flag_c::F_0407, dSv_event_flag_c::F_0408},
    BugSpeciesEntry{"Libélula", dItemNo_M_DRAGONFLY_e, dItemNo_F_DRAGONFLY_e,
        dSv_event_flag_c::F_0419, dSv_event_flag_c::F_0420},
};

constexpr std::array<NamedFlagEntry, 7> hiddenSkillEntries = {
    NamedFlagEntry{"Golpe Final", dSv_event_flag_c::F_0339},
    NamedFlagEntry{"Ataque de Escudo", dSv_event_flag_c::F_0338},
    NamedFlagEntry{"Corte pelas Costas", dSv_event_flag_c::F_0340},
    NamedFlagEntry{"Rachador de Elmos", dSv_event_flag_c::F_0341},
    NamedFlagEntry{"Saque Mortal", dSv_event_flag_c::F_0342},
    NamedFlagEntry{"Golpe Saltado", dSv_event_flag_c::F_0343},
    NamedFlagEntry{"Giro Ampliado", dSv_event_flag_c::F_0344},
};

constexpr std::array<const char*, 16> letterSenders = {
    "Renado",
    "Ooccoo 1",
    "Ooccoo 2",
    "O Carteiro",
    "Mercadorias de Kakariko",
    "Barnes 1",
    "Barnes 2",
    "Bombas do Barnes",
    "Malo Mart",
    "Telma",
    "Purlo",
    "De Jr.",
    "Princesa Agitha",
    "Turismo de Lanayru",
    "Shad",
    "Yeta",
};

constexpr std::array<FishSpeciesEntry, 6> fishSpeciesEntries = {
    FishSpeciesEntry{"Bagre de Ordon", 3},
    FishSpeciesEntry{"Guelra-Verde", 5},
    FishSpeciesEntry{"Peixe Fedorento", 4},
    FishSpeciesEntry{"Robalo de Hyrule", 0},
    FishSpeciesEntry{"Lúcio Hyliano", 2},
    FishSpeciesEntry{"Caramujo-Peixe Hyliano", 1},
};

constexpr std::array<const char*, 2> targetTypeNames = {
    "Segurar",
    "Alternar",
};

constexpr std::array<const char*, 3> soundModeNames = {
    "Mono",
    "Estéreo",
    "Surround",
};

struct DefaultInventoryEntry {
    u8 slot;
    u8 item;
};

constexpr std::array<DefaultInventoryEntry, 22> defaultInventory = {
    DefaultInventoryEntry{SLOT_0, dItemNo_BOOMERANG_e},
    DefaultInventoryEntry{SLOT_1, dItemNo_KANTERA_e},
    DefaultInventoryEntry{SLOT_2, dItemNo_SPINNER_e},
    DefaultInventoryEntry{SLOT_3, dItemNo_HVY_BOOTS_e},
    DefaultInventoryEntry{SLOT_4, dItemNo_BOW_e},
    DefaultInventoryEntry{SLOT_5, dItemNo_HAWK_EYE_e},
    DefaultInventoryEntry{SLOT_6, dItemNo_IRONBALL_e},
    DefaultInventoryEntry{SLOT_8, dItemNo_COPY_ROD_e},
    DefaultInventoryEntry{SLOT_9, dItemNo_HOOKSHOT_e},
    DefaultInventoryEntry{SLOT_10, dItemNo_W_HOOKSHOT_e},
    DefaultInventoryEntry{SLOT_11, dItemNo_EMPTY_BOTTLE_e},
    DefaultInventoryEntry{SLOT_12, dItemNo_EMPTY_BOTTLE_e},
    DefaultInventoryEntry{SLOT_13, dItemNo_EMPTY_BOTTLE_e},
    DefaultInventoryEntry{SLOT_14, dItemNo_EMPTY_BOTTLE_e},
    DefaultInventoryEntry{SLOT_15, dItemNo_NORMAL_BOMB_e},
    DefaultInventoryEntry{SLOT_16, dItemNo_WATER_BOMB_e},
    DefaultInventoryEntry{SLOT_17, dItemNo_POKE_BOMB_e},
    DefaultInventoryEntry{SLOT_18, dItemNo_DUNGEON_EXIT_e},
    DefaultInventoryEntry{SLOT_20, dItemNo_FISHING_ROD_1_e},
    DefaultInventoryEntry{SLOT_21, dItemNo_HORSE_FLUTE_e},
    DefaultInventoryEntry{SLOT_22, dItemNo_ANCIENT_DOCUMENT_e},
    DefaultInventoryEntry{SLOT_23, dItemNo_PACHINKO_e},
};

u8 get_slot_default(int slot) {
    for (const auto& entry : defaultInventory) {
        if (entry.slot == slot) {
            return entry.item;
        }
    }
    return dItemNo_NONE_e;
}

void set_item_first_bit(u8 itemNo, bool owned) {
    if (owned) {
        dComIfGs_onItemFirstBit(itemNo);
    } else {
        dComIfGs_offItemFirstBit(itemNo);
    }
}

void toggle_item_first_bit(u8 itemNo) {
    set_item_first_bit(itemNo, !dComIfGs_isItemFirstBit(itemNo));
}

void set_event_bit(u16 flag, bool enabled) {
    if (enabled) {
        dComIfGs_onEventBit(flag);
    } else {
        dComIfGs_offEventBit(flag);
    }
}

void set_letter_get_flag(int index, bool received) {
    if (received) {
        if (dComIfGs_isLetterGetFlag(index)) {
            return;
        }
        dComIfGs_onLetterGetFlag(index);
        const u8 slot = dMeter2Info_getRecieveLetterNum() - 1;
        if (slot < 64) {
            dComIfGs_setGetNumber(slot, static_cast<u8>(index + 1));
        }
    } else {
        if (!dComIfGs_isLetterGetFlag(index)) {
            return;
        }
        auto& info = dComIfGs_getSaveData()->getPlayer().getLetterInfo();
        info.mLetterGetFlags[index >> 5] &= ~(1u << (index & 0x1F));
        for (int slot = 0; slot < 64; ++slot) {
            if (dComIfGs_getGetNumber(slot) != index + 1) {
                continue;
            }
            for (int nextSlot = slot; nextSlot < 63; ++nextSlot) {
                dComIfGs_setGetNumber(nextSlot, dComIfGs_getGetNumber(nextSlot + 1));
            }
            dComIfGs_setGetNumber(63, 0);
            break;
        }
    }
}

void set_max_life(int maxLife) {
    maxLife = std::clamp(maxLife, 15, 100);
    dComIfGs_setMaxLife(static_cast<u8>(maxLife));
    const u16 maxHealth = (dComIfGs_getMaxLife() / 5) * 4;
    if (dComIfGs_getLife() > maxHealth) {
        dComIfGs_setLife(maxHealth);
    }
}

Rml::String max_life_label() {
    const int maxLife = dComIfGs_getMaxLife();
    return fmt::format("{} hearts + {} pieces", maxLife / 5, maxLife % 5);
}

struct ToggleEntry {
    Rml::String text;
    std::function<bool()> isSelected;
    std::function<void(bool)> setSelected;
};

void populate_toggle_group(Pane& pane, const std::vector<ToggleEntry>& entries) {
    pane.clear();
    pane.add_section("Ações");
    pane.add_button("Selecionar Tudo").on_pressed([entries] {
        mDoAud_seStartMenu(kSoundItemChange);
        for (const auto& entry : entries) {
            entry.setSelected(true);
        }
    });
    pane.add_button("Selecionar Nenhum").on_pressed([entries] {
        mDoAud_seStartMenu(kSoundItemChange);
        for (const auto& entry : entries) {
            entry.setSelected(false);
        }
    });

    pane.add_section("Itens");
    for (const auto& entry : entries) {
        pane.add_button({
                            .text = entry.text,
                            .isSelected = entry.isSelected,
                        })
            .on_pressed([isSelected = entry.isSelected, setSelected = entry.setSelected] {
                mDoAud_seStartMenu(kSoundItemChange);
                setSelected(!isSelected());
            });
    }
}

template <size_t Size>
int count_item_first_bits(const std::array<u8, Size>& entries) {
    int count = 0;
    for (const auto item : entries) {
        if (dComIfGs_isItemFirstBit(item)) {
            ++count;
        }
    }
    return count;
}

template <size_t Size>
int count_event_bits(const std::array<NamedFlagEntry, Size>& entries) {
    int count = 0;
    for (const auto& entry : entries) {
        if (dComIfGs_isEventBit(entry.flag)) {
            ++count;
        }
    }
    return count;
}

template <size_t Size>
int count_collect_crystals(const std::array<NamedIndexEntry, Size>& entries) {
    int count = 0;
    for (const auto& entry : entries) {
        if (dComIfGs_isCollectCrystal(entry.index)) {
            ++count;
        }
    }
    return count;
}

template <size_t Size>
int count_collect_mirrors(const std::array<NamedIndexEntry, Size>& entries) {
    int count = 0;
    for (const auto& entry : entries) {
        if (dComIfGs_isCollectMirror(entry.index)) {
            ++count;
        }
    }
    return count;
}

Rml::String count_label(int count, int total) {
    return fmt::format("{} / {}", count, total);
}

int count_clothing() {
    int count = 0;
    if (dComIfGs_isItemFirstBit(dItemNo_WEAR_CASUAL_e)) {
        ++count;
    }
    if (dComIfGs_isCollectClothes(KOKIRI_CLOTHES_FLAG)) {
        ++count;
    }
    if (dComIfGs_isItemFirstBit(dItemNo_WEAR_ZORA_e)) {
        ++count;
    }
    if (dComIfGs_isItemFirstBit(dItemNo_ARMOR_e)) {
        ++count;
    }
    return count;
}

int count_letters() {
    int count = 0;
    for (int index = 0; index < letterSenders.size(); ++index) {
        if (dComIfGs_isLetterGetFlag(index)) {
            ++count;
        }
    }
    return count;
}

Rml::String bug_species_label(const BugSpeciesEntry& bug) {
    int owned = 0;
    int given = 0;
    if (dComIfGs_isItemFirstBit(bug.maleItem)) {
        ++owned;
    }
    if (dComIfGs_isItemFirstBit(bug.femaleItem)) {
        ++owned;
    }
    if (dComIfGs_isEventBit(bug.maleTurnInFlag)) {
        ++given;
    }
    if (dComIfGs_isEventBit(bug.femaleTurnInFlag)) {
        ++given;
    }
    return fmt::format("{} / 2 owned, {} / 2 given", owned, given);
}

Rml::String fish_species_label(const FishSpeciesEntry& fish) {
    return fmt::format(
        "{} caught, {} cm", dComIfGs_getFishNum(fish.index), dComIfGs_getFishSize(fish.index));
}

bool can_edit_item_first_bit(int itemId, const itemInfo& item) {
    return itemId < 254 && item.m_name != "Reservado";
}

void set_all_item_first_bits(bool owned) {
    for (const auto& [itemId, item] : itemMap) {
        if (!can_edit_item_first_bit(itemId, item)) {
            continue;
        }
        set_item_first_bit(static_cast<u8>(itemId), owned);
    }
}

void populate_item_slot_picker(Pane& pane, int slot) {
    pane.clear();
    pane.add_section("Ações");
    pane.add_button(fmt::format("Default ({})", get_item_name(get_slot_default(slot))))
        .on_pressed([slot] {
            mDoAud_seStartMenu(kSoundItemChange);
            dComIfGs_setItem(slot, get_slot_default(slot));
        });

    pane.add_section("Itens");
    pane.add_button(
            {
                .text = "Nenhum",
                .isSelected = [slot] { return get_player_item()->mItems[slot] == dItemNo_NONE_e; },
            })
        .on_pressed([slot] {
            mDoAud_seStartMenu(kSoundItemChange);
            dComIfGs_setItem(slot, dItemNo_NONE_e);
        });
    for (const auto& [itemId, item] : itemMap) {
        if (item.m_type != ITEMTYPE_EQUIP_e) {
            continue;
        }
        pane
            .add_button({
                .text = item.m_name,
                .isSelected = [slot, itemId] { return get_player_item()->mItems[slot] == itemId; },
            })
            .on_pressed([slot, itemId] {
                mDoAud_seStartMenu(kSoundItemChange);
                dComIfGs_setItem(slot, static_cast<u8>(itemId));
            });
    }
}

void populate_item_flag_picker(Pane& pane) {
    pane.clear();
    pane.add_section("Ações");
    pane.add_button("Selecionar Tudo").on_pressed([] {
        mDoAud_seStartMenu(kSoundItemChange);
        set_all_item_first_bits(true);
    });
    pane.add_button("Limpar Nenhum").on_pressed([] {
        mDoAud_seStartMenu(kSoundItemChange);
        set_all_item_first_bits(false);
    });

    pane.add_section("Itens");
    for (const auto& [itemId, item] : itemMap) {
        if (!can_edit_item_first_bit(itemId, item)) {
            continue;
        }
        pane
            .add_button({
                .text = item.m_name,
                .isSelected = [itemId] { return dComIfGs_isItemFirstBit(static_cast<u8>(itemId)); },
            })
            .on_pressed([itemId] {
                mDoAud_seStartMenu(kSoundItemChange);
                toggle_item_first_bit(static_cast<u8>(itemId));
            });
    }
}

void populate_select_item_picker(Pane& pane, u8& selectItemData) {
    pane.clear();
    pane.add_button(
            {
                .text = "Nenhum",
                .isSelected = [&selectItemData] { return selectItemData == dItemNo_NONE_e; },
            })
        .on_pressed([&selectItemData] {
            mDoAud_seStartMenu(kSoundItemChange);
            selectItemData = dItemNo_NONE_e;
        });
    for (int i = 0; i < 24; i++) {
        pane.add_button({
                            .text = item_label_for_slot(i),
                            .isSelected = [i, &selectItemData] { return selectItemData == i; },
                        })
            .on_pressed([i, &selectItemData] {
                mDoAud_seStartMenu(kSoundItemChange);
                selectItemData = i;
            });
    }
}

void populate_select_clothes_picker(Pane& pane) {
    pane.clear();
    const auto addOption = [&pane](u8 id) {
        pane.add_button(
                {
                    .text = get_item_name(id),
                    .isSelected = [id] { return get_player_status()->mSelectEquip[0] == id; },
                })
            .on_pressed([id] {
                mDoAud_seStartMenu(kSoundItemChange);
                dMeter2Info_setCloth(id, false);
                daPy_getPlayerActorClass()->setClothesChange(0);
            });
    };
    addOption(dItemNo_WEAR_CASUAL_e);
    addOption(dItemNo_WEAR_KOKIRI_e);
    addOption(dItemNo_WEAR_ZORA_e);
    addOption(dItemNo_ARMOR_e);
}

template <size_t Size>
void populate_select_equip_picker(Pane& pane, u8& equip, const std::array<u8, Size>& entries) {
    pane.clear();
    const auto addOption = [&pane, &equip](u8 id) {
        pane.add_button({
                            .text = get_item_name(id),
                            .isSelected = [id, &equip] { return equip == id; },
                        })
            .on_pressed([id, &equip] {
                mDoAud_seStartMenu(kSoundItemChange);
                equip = id;
            });
    };
    addOption(dItemNo_NONE_e);
    for (const auto item : entries) {
        addOption(item);
    }
}

static const std::array<Rml::String, 3> walletSizeNames = {
    "Normal",
    "Grande",
    "Gigante",
};

void populate_wallet_picker(Pane& pane) {
    pane.clear();
    for (int i = 0; i < walletSizeNames.size(); ++i) {
        pane.add_button({
                            .text = walletSizeNames[i],
                            .isSelected = [i] { return get_player_status()->getWalletSize() == i; },
                        })
            .on_pressed([i] {
                mDoAud_seStartMenu(kSoundItemChange);
                get_player_status()->setWalletSize(i);
            });
    }
}

static const std::array<Rml::String, 2> formNames = {
    "Humano",
    "Lobo",
};

void populate_form_picker(Pane& pane) {
    pane.clear();
    for (int i = 0; i < formNames.size(); ++i) {
        pane.add_button(
                {
                    .text = formNames[i],
                    .isSelected = [i] { return get_player_status()->getTransformStatus() == i; },
                })
            .on_pressed([i] {
                mDoAud_seStartMenu(kSoundItemChange);
                get_player_status()->setTransformStatus(i);
            });
    }
}

void add_toggle_button(Pane& pane, ToggleEntry entry) {
    auto isSelected = std::move(entry.isSelected);
    auto setSelected = std::move(entry.setSelected);
    pane.add_button({
                        .text = entry.text,
                        .isSelected = isSelected,
                    })
        .on_pressed([isSelected, setSelected] {
            mDoAud_seStartMenu(kSoundItemChange);
            setSelected(!isSelected());
        });
}

template <size_t Size>
std::vector<ToggleEntry> item_toggle_entries(const std::array<u8, Size>& entries) {
    std::vector<ToggleEntry> toggles;
    toggles.reserve(entries.size());
    for (const auto item : entries) {
        toggles.push_back({
            .text = get_item_name(item),
            .isSelected = [item] { return dComIfGs_isItemFirstBit(item); },
            .setSelected = [item](bool selected) { set_item_first_bit(item, selected); },
        });
    }
    return toggles;
}

template <size_t Size>
std::vector<ToggleEntry> event_toggle_entries(const std::array<NamedFlagEntry, Size>& entries) {
    std::vector<ToggleEntry> toggles;
    toggles.reserve(entries.size());
    for (const auto& [name, flag] : entries) {
        toggles.push_back({
            .text = name,
            .isSelected = [flag] { return dComIfGs_isEventBit(flag); },
            .setSelected = [flag](bool selected) { set_event_bit(flag, selected); },
        });
    }
    return toggles;
}

template <size_t Size>
std::vector<ToggleEntry> collect_crystal_toggle_entries(
    const std::array<NamedIndexEntry, Size>& entries) {
    std::vector<ToggleEntry> toggles;
    toggles.reserve(entries.size());
    for (const auto& [name, index] : entries) {
        toggles.push_back({
            .text = name,
            .isSelected = [index] { return dComIfGs_isCollectCrystal(index); },
            .setSelected =
                [index](bool selected) {
                    if (selected) {
                        dComIfGs_onCollectCrystal(index);
                    } else {
                        dComIfGs_offCollectCrystal(index);
                    }
                },
        });
    }
    return toggles;
}

template <size_t Size>
std::vector<ToggleEntry> collect_mirror_toggle_entries(
    const std::array<NamedIndexEntry, Size>& entries) {
    std::vector<ToggleEntry> toggles;
    toggles.reserve(entries.size());
    for (const auto& [name, index] : entries) {
        toggles.push_back({
            .text = name,
            .isSelected = [index] { return dComIfGs_isCollectMirror(index); },
            .setSelected =
                [index](bool selected) {
                    if (selected) {
                        dComIfGs_onCollectMirror(index);
                    } else {
                        dComIfGs_offCollectMirror(index);
                    }
                },
        });
    }
    return toggles;
}

void populate_collect_clothes_picker(Pane& pane) {
    populate_toggle_group(pane,
        {
            ToggleEntry{
                .text = "Roupas de Ordon",
                .isSelected = [] { return dComIfGs_isItemFirstBit(dItemNo_WEAR_CASUAL_e); },
                .setSelected =
                    [](bool selected) { set_item_first_bit(dItemNo_WEAR_CASUAL_e, selected); },
            },
            ToggleEntry{
                .text = "Hero's Clothes",
                .isSelected = [] { return dComIfGs_isCollectClothes(KOKIRI_CLOTHES_FLAG); },
                .setSelected =
                    [](bool selected) {
                        if (selected) {
                            dComIfGs_setCollectClothes(KOKIRI_CLOTHES_FLAG);
                        } else {
                            dComIfGs_offCollectClothes(KOKIRI_CLOTHES_FLAG);
                        }
                    },
            },
            ToggleEntry{
                .text = "Armadura Zora",
                .isSelected = [] { return dComIfGs_isItemFirstBit(dItemNo_WEAR_ZORA_e); },
                .setSelected =
                    [](bool selected) { set_item_first_bit(dItemNo_WEAR_ZORA_e, selected); },
            },
            ToggleEntry{
                .text = "Armadura Mágica",
                .isSelected = [] { return dComIfGs_isItemFirstBit(dItemNo_ARMOR_e); },
                .setSelected = [](bool selected) { set_item_first_bit(dItemNo_ARMOR_e, selected); },
            },
        });
}

void populate_poe_souls_picker(Pane& pane) {
    pane.clear();
    pane.add_section("Ações");
    pane.add_button("All 60").on_pressed([] {
        mDoAud_seStartMenu(kSoundItemChange);
        dComIfGs_setPohSpiritNum(60);
    });
    pane.add_button("Limpar").on_pressed([] {
        mDoAud_seStartMenu(kSoundItemChange);
        dComIfGs_setPohSpiritNum(0);
    });

    pane.add_section("Valor");
    pane.add_child<NumberButton>(NumberButton::Props{
        .key = "Coletado",
        .getValue = [] { return dComIfGs_getPohSpiritNum(); },
        .setValue =
            [](int value) { dComIfGs_setPohSpiritNum(static_cast<u8>(std::clamp(value, 0, 60))); },
        .max = 60,
    });
}

void populate_max_life_picker(Pane& pane) {
    pane.clear();
    pane.add_section("Ações");
    pane.add_button("3 Hearts").on_pressed([] {
        mDoAud_seStartMenu(kSoundItemChange);
        dComIfGs_setMaxLife(15);
        dComIfGs_setLife(12);
    });
    pane.add_button("20 Hearts").on_pressed([] {
        mDoAud_seStartMenu(kSoundItemChange);
        dComIfGs_setMaxLife(100);
        dComIfGs_setLife(80);
    });

    pane.add_section("Valor");
    pane.add_child<NumberButton>(NumberButton::Props{
        .key = "Vida Máxima",
        .getValue = [] { return dComIfGs_getMaxLife(); },
        .setValue = [](int value) { set_max_life(value); },
        .min = 15,
        .max = 100,
    });
}

void populate_bug_species_picker(Pane& pane, const BugSpeciesEntry& bug) {
    pane.clear();
    pane.add_section("Possuído");
    add_toggle_button(
        pane, {
                  .text = fmt::format("Male {}", bug.name),
                  .isSelected = [item = bug.maleItem] { return dComIfGs_isItemFirstBit(item); },
                  .setSelected = [item = bug.maleItem](
                                     bool selected) { set_item_first_bit(item, selected); },
              });
    add_toggle_button(
        pane, {
                  .text = fmt::format("Female {}", bug.name),
                  .isSelected = [item = bug.femaleItem] { return dComIfGs_isItemFirstBit(item); },
                  .setSelected = [item = bug.femaleItem](
                                     bool selected) { set_item_first_bit(item, selected); },
              });

    pane.add_section("Dado à Agitha");
    add_toggle_button(
        pane, {
                  .text = fmt::format("Male {}", bug.name),
                  .isSelected = [flag = bug.maleTurnInFlag] { return dComIfGs_isEventBit(flag); },
                  .setSelected = [flag = bug.maleTurnInFlag](
                                     bool selected) { set_event_bit(flag, selected); },
              });
    add_toggle_button(
        pane, {
                  .text = fmt::format("Female {}", bug.name),
                  .isSelected = [flag = bug.femaleTurnInFlag] { return dComIfGs_isEventBit(flag); },
                  .setSelected = [flag = bug.femaleTurnInFlag](
                                     bool selected) { set_event_bit(flag, selected); },
              });
}

void populate_letters_picker(Pane& pane) {
    std::vector<ToggleEntry> toggles;
    toggles.reserve(letterSenders.size());
    for (int index = 0; index < letterSenders.size(); ++index) {
        toggles.push_back({
            .text = letterSenders[index],
            .isSelected = [index] { return dComIfGs_isLetterGetFlag(index); },
            .setSelected = [index](bool selected) { set_letter_get_flag(index, selected); },
        });
    }
    populate_toggle_group(pane, toggles);
}

void populate_fish_species_picker(Pane& pane, const FishSpeciesEntry& fish) {
    pane.clear();
    pane.add_section(fish.name);
    pane.add_child<NumberButton>(NumberButton::Props{
        .key = "Capturado",
        .getValue = [index = fish.index] { return dComIfGs_getFishNum(index); },
        .setValue =
            [index = fish.index](int value) {
                get_player_fishing_info()->mFishCount[index] =
                    static_cast<u16>(std::clamp(value, 0, 999));
            },
        .max = 999,
    });
    pane.add_child<NumberButton>(NumberButton::Props{
        .key = "Maior",
        .getValue = [index = fish.index] { return dComIfGs_getFishSize(index); },
        .setValue =
            [index = fish.index](int value) {
                dComIfGs_setFishSize(index, static_cast<u8>(std::clamp(value, 0, 255)));
            },
        .max = 255,
    });
}

Rml::String target_type_label() {
    const auto type = get_player_config()->getAttentionType();
    if (type >= targetTypeNames.size()) {
        return fmt::format("Unknown ({})", type);
    }
    return targetTypeNames[type];
}

Rml::String sound_mode_label() {
    const auto mode = get_player_config()->getSound();
    if (mode >= soundModeNames.size()) {
        return fmt::format("Unknown ({})", mode);
    }
    return soundModeNames[mode];
}

void populate_target_type_picker(Pane& pane) {
    pane.clear();
    for (u8 type = 0; type < targetTypeNames.size(); ++type) {
        pane
            .add_button({
                .text = targetTypeNames[type],
                .isSelected = [type] { return get_player_config()->getAttentionType() == type; },
            })
            .on_pressed([type] {
                mDoAud_seStartMenu(kSoundItemChange);
                get_player_config()->setAttentionType(type);
            });
    }
}

void populate_sound_mode_picker(Pane& pane) {
    pane.clear();
    for (u8 mode = 0; mode < soundModeNames.size(); ++mode) {
        pane.add_button(
                {
                    .text = soundModeNames[mode],
                    .isSelected = [mode] { return get_player_config()->getSound() == mode; },
                })
            .on_pressed([mode] {
                mDoAud_seStartMenu(kSoundItemChange);
                get_player_config()->setSound(mode);
            });
    }
}

constexpr float kDaytimeUnitsPerHour = 15.0f;

float daytime_from_clock(int hour, int minute) {
    hour = std::clamp(hour, 0, 23);
    minute = std::clamp(minute, 0, 59);
    return (hour * kDaytimeUnitsPerHour) + (minute / 60.0f * kDaytimeUnitsPerHour);
}

void set_clock_time(int hour, int minute) {
    if (auto* statusB = get_player_status_b()) {
        statusB->setTime(daytime_from_clock(hour, minute));
    }
}

}  // namespace

EditorWindow::EditorWindow() {
    add_tab("Status do Jogador", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Jogador");
        leftPane.register_control(leftPane.add_child<StringButton>(StringButton::Props{
                                      .key = "Nome do Jogador",
                                      .getValue = get_player_name,
                                      .setValue = set_player_name,
                                      .maxLength = 16,
                                  }),
            rightPane, {});
        leftPane.register_control(leftPane.add_child<StringButton>(StringButton::Props{
                                      .key = "Nome do Cavalo",
                                      .getValue = get_horse_name,
                                      .setValue = set_horse_name,
                                      .maxLength = 16,
                                  }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Vida Máxima",
                .getValue = [] { return get_player_status()->getMaxLife(); },
                .setValue = [](int value) { return get_player_status()->setMaxLife(value); },
                .max = UINT16_MAX,  // TODO: actual max
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Vida",
                .getValue = [] { return get_player_status()->getLife(); },
                .setValue = [](int value) { return get_player_status()->setLife(value); },
                .max = UINT16_MAX,  // TODO: actual max
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Rúpias",
                .getValue = [] { return get_player_status()->getRupee(); },
                .setValue = [](int value) { return get_player_status()->setRupee(value); },
                .max = get_player_status()->getRupeeMax(),
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Óleo Máximo",
                .getValue = [] { return get_player_status()->getMaxOil(); },
                .setValue = [](int value) { return get_player_status()->setMaxOil(value); },
                .max = UINT16_MAX,  // TODO: actual max
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Óleo",
                .getValue = [] { return get_player_status()->getOil(); },
                .setValue = [](int value) { return get_player_status()->setOil(value); },
                .max = UINT16_MAX,  // TODO: actual max
            }),
            rightPane, {});

        leftPane.add_section("Equipamento");
        const auto genSelectItemComboBox = [&leftPane, &rightPane](
                                               const Rml::String& label, u8& selectItemData) {
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = label,
                    .getValue = [&selectItemData] { return item_label_for_slot(selectItemData); },
                }),
                rightPane, [&selectItemData](Pane& pane) {
                    populate_select_item_picker(pane, selectItemData);
                });
        };
        genSelectItemComboBox("Equipar X", get_player_status()->mSelectItem[0]);
        genSelectItemComboBox("Equipar Y", get_player_status()->mSelectItem[1]);
        genSelectItemComboBox("Equipar Combo X", get_player_status()->mMixItem[0]);
        genSelectItemComboBox("Equipar Combo Y", get_player_status()->mMixItem[1]);

        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Roupas",
                .getValue = [] { return get_item_name(get_player_status()->mSelectEquip[0]); },
            }),
            rightPane, [](Pane& pane) { populate_select_clothes_picker(pane); });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Espada",
                .getValue = [] { return get_item_name(get_player_status()->mSelectEquip[1]); },
            }),
            rightPane, [](Pane& pane) {
                populate_select_equip_picker(
                    pane, get_player_status()->mSelectEquip[1], swordEntries);
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Escudo",
                .getValue = [] { return get_item_name(get_player_status()->mSelectEquip[2]); },
            }),
            rightPane, [](Pane& pane) {
                populate_select_equip_picker(
                    pane, get_player_status()->mSelectEquip[2], shieldEntries);
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Aroma",
                .getValue = [] { return get_item_name(get_player_status()->mSelectEquip[3]); },
            }),
            rightPane, [](Pane& pane) {
                populate_select_equip_picker(
                    pane, get_player_status()->mSelectEquip[3], smellEntries);
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Tamanho da Carteira",
                .getValue = [] { return walletSizeNames[get_player_status()->getWalletSize()]; },
            }),
            rightPane, [](Pane& pane) { populate_wallet_picker(pane); });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Forma",
                .getValue = [] { return formNames[get_player_status()->getTransformStatus()]; },
            }),
            rightPane, [](Pane& pane) { populate_form_picker(pane); });

        leftPane.add_section("Mundo");
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Dia",
                .getValue = [] { return get_player_status_b()->getDate(); },
                .setValue =
                    [](int value) { get_player_status_b()->setDate(static_cast<u16>(value)); },
                .max = UINT16_MAX,
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Hora",
                .getValue = [] { return dKy_getdaytime_hour(); },
                .setValue = [](int value) { set_clock_time(value, dKy_getdaytime_minute()); },
                .max = 23,
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Minuto",
                .getValue = [] { return dKy_getdaytime_minute(); },
                .setValue = [](int value) { set_clock_time(dKy_getdaytime_hour(), value); },
                .max = 59,
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Nível de Transformação",
                .getValue =
                    [] {
                        return std::popcount(static_cast<unsigned>(
                            get_player_status_b()->mTransformLevelFlag & 0xF));
                    },
                .setValue =
                    [](int value) {
                        get_player_status_b()->mTransformLevelFlag =
                            static_cast<u8>((1u << value) - 1u);
                    },
                .max = 4,
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Nível de Purificação do Crepúsculo",
                .getValue =
                    [] {
                        return std::popcount(static_cast<unsigned>(
                            get_player_status_b()->mDarkClearLevelFlag & 0x7));
                    },
                .setValue =
                    [](int value) {
                        get_player_status_b()->mDarkClearLevelFlag =
                            static_cast<u8>((1u << value) - 1u);
                    },
                .max = 3,
            }),
            rightPane, {});
    });

    add_tab("Localização", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Local de Save");
        leftPane
            .register_control(leftPane.add_select_button({
                                  .key = "Fase",
                                  .getValue =
                                      [] {
                                          return stage_label_for_file(
                                              fixed_string(get_player_return_place()->mName));
                                      },
                              }),
                rightPane,
                [](Pane& pane) {
                    populate_stage_picker(
                        pane, [] { return fixed_string(get_player_return_place()->mName); },
                        [](const char* stageFile) {
                            set_fixed_string(
                                get_player_return_place()->mName, Rml::String(stageFile));
                        });
                })
            .set_disabled(true);
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Sala",
                .getValue = [] { return get_player_return_place()->mRoomNo; },
                .setValue =
                    [](int value) { get_player_return_place()->mRoomNo = static_cast<s8>(value); },
                .min = std::numeric_limits<s8>::min(),
                .max = std::numeric_limits<s8>::max(),
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "ID de Spawn",
                .getValue = [] { return get_player_return_place()->mPlayerStatus; },
                .setValue =
                    [](int value) {
                        get_player_return_place()->mPlayerStatus = static_cast<u8>(value);
                    },
                .max = std::numeric_limits<u8>::max(),
            }),
            rightPane, {});

        leftPane.add_section("Localização do Cavalo");
        leftPane.register_control(leftPane.add_child<StringButton>(StringButton::Props{
                                      .key = "Posição do Cavalo",
                                      .getValue =
                                          [] {
                                              const auto* horsePlace = get_horse_place();
                                              return fmt::format("{}, {}, {}",
                                                  static_cast<float>(horsePlace->mPos.x),
                                                  static_cast<float>(horsePlace->mPos.y),
                                                  static_cast<float>(horsePlace->mPos.z));
                                          },
                                      .setValue =
                                          [](Rml::String value) {
                                              float x = 0.0f;
                                              float y = 0.0f;
                                              float z = 0.0f;
                                              if (parse_vec3(value, x, y, z)) {
                                                  auto* horsePlace = get_horse_place();
                                                  horsePlace->mPos.x = x;
                                                  horsePlace->mPos.y = y;
                                                  horsePlace->mPos.z = z;
                                              }
                                          },
                                  }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Ângulo do Cavalo",
                .getValue = [] { return get_horse_place()->mAngleY; },
                .setValue = [](int value) { get_horse_place()->mAngleY = static_cast<s16>(value); },
                .min = std::numeric_limits<s16>::min(),
                .max = std::numeric_limits<s16>::max(),
            }),
            rightPane, {});
        leftPane
            .register_control(
                leftPane.add_select_button({
                    .key = "Fase do Cavalo",
                    .getValue =
                        [] { return stage_label_for_file(fixed_string(get_horse_place()->mName)); },
                }),
                rightPane,
                [](Pane& pane) {
                    populate_stage_picker(
                        pane, [] { return fixed_string(get_horse_place()->mName); },
                        [](const char* stageFile) {
                            set_fixed_string(get_horse_place()->mName, Rml::String(stageFile));
                        });
                })
            .set_disabled(true);
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Sala do Cavalo",
                .getValue = [] { return get_horse_place()->mRoomNo; },
                .setValue = [](int value) { get_horse_place()->mRoomNo = static_cast<s8>(value); },
                .min = std::numeric_limits<s8>::min(),
                .max = std::numeric_limits<s8>::max(),
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "ID de Spawn do Cavalo",
                .getValue = [] { return get_horse_place()->mSpawnId; },
                .setValue = [](int value) { get_horse_place()->mSpawnId = static_cast<u8>(value); },
                .max = std::numeric_limits<u8>::max(),
            }),
            rightPane, {});
    });

    add_tab("Inventário", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Roda de Itens");
        leftPane.register_control(leftPane.add_button("Padrão para Tudo").on_pressed([&rightPane] {
            mDoAud_seStartMenu(kSoundItemChange);
            for (int slot = 0; slot < 24; ++slot) {
                dComIfGs_setItem(slot, get_slot_default(slot));
            }
            rightPane.clear();
        }),
            rightPane, {});
        leftPane.register_control(leftPane.add_button("Limpar Tudo").on_pressed([&rightPane] {
            mDoAud_seStartMenu(kSoundItemChange);
            for (int slot = 0; slot < 24; ++slot) {
                dComIfGs_setItem(slot, dItemNo_NONE_e);
            }
            rightPane.clear();
        }),
            rightPane, {});
        for (int slot = 0; slot < 24; ++slot) {
            leftPane.register_control(
                leftPane.add_select_button({
                    .key = fmt::format("Slot {0:02d}", slot),
                    .getValue = [slot] { return get_item_name(get_player_item()->mItems[slot]); },
                }),
                rightPane, [slot](Pane& pane) { populate_item_slot_picker(pane, slot); });
        }

        leftPane.add_section("Quantidades");
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Quantidade de Flechas",
                .getValue = [] { return get_player_item_record()->mArrowNum; },
                .setValue =
                    [](int value) { get_player_item_record()->mArrowNum = static_cast<u8>(value); },
                .max = std::numeric_limits<u8>::max(),
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Quantidade do Estilingue",
                .getValue = [] { return get_player_item_record()->mPachinkoNum; },
                .setValue =
                    [](int value) {
                        get_player_item_record()->mPachinkoNum = static_cast<u8>(value);
                    },
                .max = std::numeric_limits<u8>::max(),
            }),
            rightPane, {});
        for (int bag = 0; bag < 3; ++bag) {
            leftPane.register_control(
                leftPane.add_child<NumberButton>(NumberButton::Props{
                    .key = fmt::format("Bomb Bag {} Amount", bag + 1),
                    .getValue = [bag] { return get_player_item_record()->mBombNum[bag]; },
                    .setValue =
                        [bag](int value) {
                            get_player_item_record()->mBombNum[bag] = static_cast<u8>(value);
                        },
                    .max = std::numeric_limits<u8>::max(),
                }),
                rightPane, {});
        }
        for (int bottle = 0; bottle < 4; ++bottle) {
            leftPane.register_control(
                leftPane.add_child<NumberButton>(NumberButton::Props{
                    .key = fmt::format("Bottle {} Amount", bottle + 1),
                    .getValue = [bottle] { return get_player_item_record()->mBottleNum[bottle]; },
                    .setValue =
                        [bottle](int value) {
                            get_player_item_record()->mBottleNum[bottle] = static_cast<u8>(value);
                        },
                    .max = std::numeric_limits<u8>::max(),
                }),
                rightPane, {});
        }

        leftPane.add_section("Capacidades");
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Máximo de Flechas",
                .getValue = [] { return get_player_item_max()->mItemMax[0]; },
                .setValue =
                    [](int value) { get_player_item_max()->mItemMax[0] = static_cast<u8>(value); },
                .max = std::numeric_limits<u8>::max(),
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Máximo de Bombas Normais",
                .getValue = [] { return get_player_item_max()->mItemMax[1]; },
                .setValue =
                    [](int value) { get_player_item_max()->mItemMax[1] = static_cast<u8>(value); },
                .max = std::numeric_limits<u8>::max(),
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Máximo de Bombas Aquáticas",
                .getValue = [] { return get_player_item_max()->mItemMax[2]; },
                .setValue =
                    [](int value) { get_player_item_max()->mItemMax[2] = static_cast<u8>(value); },
                .max = std::numeric_limits<u8>::max(),
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Máximo de Bomblings",
                .getValue = [] { return get_player_item_max()->mItemMax[3]; },
                .setValue =
                    [](int value) { get_player_item_max()->mItemMax[3] = static_cast<u8>(value); },
                .max = std::numeric_limits<u8>::max(),
            }),
            rightPane, {});

        leftPane.add_section("Flags");
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Itens Obtidos",
                                      .getValue = [] { return "Editar"; },
                                  }),
            rightPane, [](Pane& pane) { populate_item_flag_picker(pane); });
    });
    add_tab("Coleção", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Equipamento");
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Espadas",
                .getValue =
                    [] {
                        return count_label(
                            count_item_first_bits(swordEntries), swordEntries.size());
                    },
            }),
            rightPane,
            [](Pane& pane) { populate_toggle_group(pane, item_toggle_entries(swordEntries)); });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Escudos",
                .getValue =
                    [] {
                        return count_label(
                            count_item_first_bits(shieldEntries), shieldEntries.size());
                    },
            }),
            rightPane,
            [](Pane& pane) { populate_toggle_group(pane, item_toggle_entries(shieldEntries)); });
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Roupas",
                                      .getValue = [] { return count_label(count_clothing(), 4); },
                                  }),
            rightPane, [](Pane& pane) { populate_collect_clothes_picker(pane); });

        leftPane.add_section("Itens-Chave");
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Sombras Fundidas",
                .getValue =
                    [] {
                        return count_label(
                            count_collect_crystals(fusedShadowEntries), fusedShadowEntries.size());
                    },
            }),
            rightPane, [](Pane& pane) {
                populate_toggle_group(pane, collect_crystal_toggle_entries(fusedShadowEntries));
            });
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Fragmentos do Espelho",
                .getValue =
                    [] {
                        return count_label(
                            count_collect_mirrors(mirrorShardEntries), mirrorShardEntries.size());
                    },
            }),
            rightPane, [](Pane& pane) {
                populate_toggle_group(pane, collect_mirror_toggle_entries(mirrorShardEntries));
            });

        leftPane.add_section("Health & Souls");
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Almas Poe",
                .getValue = [] { return fmt::format("{} / 60", dComIfGs_getPohSpiritNum()); },
            }),
            rightPane, [](Pane& pane) { populate_poe_souls_picker(pane); });
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Vida Máxima",
                                      .getValue = [] { return max_life_label(); },
                                  }),
            rightPane, [](Pane& pane) { populate_max_life_picker(pane); });

        leftPane.add_section("Insetos Dourados");
        for (const auto& bug : bugSpeciesEntries) {
            leftPane.register_control(leftPane.add_select_button({
                                          .key = bug.name,
                                          .getValue = [bug] { return bug_species_label(bug); },
                                      }),
                rightPane, [bug](Pane& pane) { populate_bug_species_picker(pane, bug); });
        }

        leftPane.add_section("Habilidades");
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Habilidades Ocultas",
                .getValue =
                    [] {
                        return count_label(
                            count_event_bits(hiddenSkillEntries), hiddenSkillEntries.size());
                    },
            }),
            rightPane, [](Pane& pane) {
                populate_toggle_group(pane, event_toggle_entries(hiddenSkillEntries));
            });

        leftPane.add_section("Registros");
        leftPane.register_control(
            leftPane.add_select_button({
                .key = "Cartas do Carteiro",
                .getValue = [] { return count_label(count_letters(), letterSenders.size()); },
            }),
            rightPane, [](Pane& pane) { populate_letters_picker(pane); });

        leftPane.add_section("Registro de Pesca");
        for (const auto& fish : fishSpeciesEntries) {
            leftPane.register_control(leftPane.add_select_button({
                                          .key = fish.name,
                                          .getValue = [fish] { return fish_species_label(fish); },
                                      }),
                rightPane, [fish](Pane& pane) { populate_fish_species_picker(pane, fish); });
        }
    });

    //add_tab("Flags", [this](Rml::Element* content) {
    //    // TODO
    //});

    add_tab("Minijogo", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Recordes");
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Tempo do Jogo STAR (ms)",
                .getValue =
                    [] {
                        return static_cast<int>(std::min<u32>(
                            get_minigame()->getHookGameTime(), std::numeric_limits<int>::max()));
                    },
                .setValue =
                    [](int value) {
                        get_minigame()->setHookGameTime(static_cast<u32>(std::max(0, value)));
                    },
                .max = std::numeric_limits<int>::max(),
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Tempo da Corrida de Snowboard (ms)",
                .getValue =
                    [] {
                        return static_cast<int>(std::min<u32>(
                            get_minigame()->getRaceGameTime(), std::numeric_limits<int>::max()));
                    },
                .setValue =
                    [](int value) {
                        get_minigame()->setRaceGameTime(static_cast<u32>(std::max(0, value)));
                    },
                .max = std::numeric_limits<int>::max(),
            }),
            rightPane, {});
        leftPane.register_control(
            leftPane.add_child<NumberButton>(NumberButton::Props{
                .key = "Pontuação de Fruit-Pop-Flight",
                .getValue =
                    [] {
                        return static_cast<int>(std::min<u32>(
                            get_minigame()->getBalloonScore(), std::numeric_limits<int>::max()));
                    },
                .setValue =
                    [](int value) {
                        get_minigame()->setBalloonScore(static_cast<u32>(std::max(0, value)));
                    },
                .max = std::numeric_limits<int>::max(),
            }),
            rightPane, {});
    });

    add_tab("Configuração", [this](Rml::Element* content) {
        auto& leftPane = add_child<Pane>(content, Pane::Type::Controlled);
        auto& rightPane = add_child<Pane>(content, Pane::Type::Uncontrolled);

        leftPane.add_section("Opções");
        leftPane.register_control(
            leftPane.add_child<BoolButton>(BoolButton::Props{
                .key = "Ativar Vibração",
                .getValue = [] { return get_player_config()->getVibration() != 0; },
                .setValue = [](bool value) { get_player_config()->setVibration(value); },
            }),
            rightPane, {});
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Tipo de Mira",
                                      .getValue = [] { return target_type_label(); },
                                  }),
            rightPane, [](Pane& pane) { populate_target_type_picker(pane); });
        leftPane.register_control(leftPane.add_select_button({
                                      .key = "Som",
                                      .getValue = [] { return sound_mode_label(); },
                                  }),
            rightPane, [](Pane& pane) { populate_sound_mode_picker(pane); });
    });
}

}  // namespace dusk::ui
