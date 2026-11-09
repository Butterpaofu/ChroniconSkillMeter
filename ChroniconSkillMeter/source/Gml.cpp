#include "Gml.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace gml {

static YYTKInterface* g_Iface = nullptr;

void Init(YYTKInterface* iface) { g_Iface = iface; }
YYTKInterface* Iface() { return g_Iface; }

RValue NumV(double v) { return RValue(v); }
RValue StrV(const std::string& v) { return RValue(v); }

RValue Call(const char* fn, std::vector<RValue> args) {
    if (!g_Iface || !fn) return RValue();
    return g_Iface->CallBuiltin(fn, std::move(args));
}

double Num(const char* fn, std::vector<RValue> args) {
    return Call(fn, std::move(args)).AsReal();
}

bool Bool(const char* fn, std::vector<RValue> args) {
    return Call(fn, std::move(args)).AsBool();
}

double AsNum(const RValue* v) {
    return v ? v->AsReal() : 0.0;
}

std::string Text(const RValue& v) {
    if (!g_Iface) return {};
    // Going through GML's string() guarantees the result really is a string
    // RValue, so AsString() is safe even for real / instance inputs.
    RValue s = Call("string", {v});
    std::string_view sv = s.AsString(g_Iface);
    if (sv.size() > 4096) return {};
    return std::string(sv);
}

double Asset(const char* name) {
    return Call("asset_get_index", {RValue(name)}).AsReal();
}

double PlayerRef() {
    static double objPlayer = -2.0;
    if (objPlayer == -2.0) objPlayer = Asset("obj_player");
    if (objPlayer < 0.0) return -1.0;

    RValue inst = Call("instance_find", {RValue(objPlayer), RValue(0.0)});
    if (!Call("instance_exists", {inst}).AsBool()) return -1.0;
    return Call("real", {inst}).AsReal();
}

void* FindScript(const char* name) {
    if (!g_Iface || !name) return nullptr;
    YYTK::CScript* script = nullptr;
    if (!Aurie::AurieSuccess(
            g_Iface->GetNamedRoutinePointer(name, reinterpret_cast<PVOID*>(&script))))
        return nullptr;
    if (!script || !script->m_Functions) return nullptr;
    // m_ScriptFunction may be a typed function pointer. A function-pointer to
    // void* cast is not portable C++; copying the pointer bytes is.
    const auto& member = script->m_Functions->m_ScriptFunction;
    static_assert(sizeof(member) == sizeof(void*),
                  "YYToolkit script function pointer has an unexpected size");
    void* target = nullptr;
    std::memcpy(&target, &member, sizeof(target));
    return target;
}

bool HookRaw(Aurie::AurieModule* module, const char* scriptName,
             void* detour, PVOID* original) {
    void* target = FindScript(scriptName);
    if (!target || !detour || !original || !module) return false;
    return Aurie::AurieSuccess(
        Aurie::MmCreateHook(module, scriptName, target, detour, original));
}

// ---------------------------------------------------------------- input

bool KeyPressed(int vk) {
    return Bool("keyboard_check_pressed", {RValue(static_cast<double>(vk))});
}
bool KeyDown(int vk) {
    return Bool("keyboard_check", {RValue(static_cast<double>(vk))});
}
bool MousePressed(int button) {
    return Bool("mouse_check_button_pressed", {RValue(static_cast<double>(button))});
}
bool MouseDown(int button) {
    return Bool("mouse_check_button", {RValue(static_cast<double>(button))});
}
bool WheelUp()   { return Bool("mouse_wheel_up", {}); }
bool WheelDown() { return Bool("mouse_wheel_down", {}); }

// --------------------------------------------------------------- drawing

int Col(int r, int g, int b) {
    return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16);
}

static std::unordered_map<std::string, double>& WidthCache() {
    static std::unordered_map<std::string, double> cache;
    return cache;
}

static void PopUtf8(std::string& s) {
    // Remove one UTF-8 code point from the end (never leave a partial sequence).
    while (!s.empty()) {
        const unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;   // leading byte reached
    }
}

Canvas::Canvas() {
    if (!g_Iface) return;
    savedColor_ = Call("draw_get_color");
    savedAlpha_ = Call("draw_get_alpha");
    savedFont_  = Call("draw_get_font");
    savedH_     = Call("draw_get_halign");
    savedV_     = Call("draw_get_valign");
    valid_ = true;

    W = Num("display_get_gui_width");
    H = Num("display_get_gui_height");
    if (!(W > 0) || !(H > 0)) { W = 960; H = 540; }
    S = (std::max)(0.25, (std::min)(W / 960.0, H / 540.0));
    OX = (W - 960.0 * S) / 2.0;
    OY = (H - 540.0 * S) / 2.0;

    // Chronicon ships both of these fonts; fall back to the HUD's current one.
    static double font = Asset("font_small_GUI_latin");
    if (std::isfinite(font) && font >= 0.0) Call("draw_set_font", {RValue(font)});
    Call("draw_set_alpha", {RValue(1.0)});
    Call("draw_set_halign", {RValue(0.0)});
    Call("draw_set_valign", {RValue(0.0)});
}

Canvas::~Canvas() {
    if (!valid_ || !g_Iface) return;
    Call("draw_set_color",  {savedColor_});
    Call("draw_set_alpha",  {savedAlpha_});
    Call("draw_set_font",   {savedFont_});
    Call("draw_set_halign", {savedH_});
    Call("draw_set_valign", {savedV_});
}

double Canvas::X(double x) const { return std::round(OX + x * S); }
double Canvas::Y(double y) const { return std::round(OY + y * S); }

double Canvas::Width(const std::string& text, double scale) const {
    auto& cache = WidthCache();
    auto it = cache.find(text);
    double w = 0.0;
    if (it != cache.end()) {
        w = it->second;
    } else {
        w = Num("string_width", {RValue(text)});
        if (cache.size() > 2048) cache.clear();
        cache.emplace(text, w);
    }
    // Width in the virtual 960x540 space (not in physical GUI pixels).
    return w * scale;
}

std::string Canvas::Fit(const std::string& text, double width, double scale) const {
    std::string out = text;
    if (Width(out, scale) <= width) return out;
    while (!out.empty() && Width(out + "...", scale) > width) PopUtf8(out);
    return out.empty() ? "" : out + "...";
}

void Canvas::Fill(const Rect& r, int color, double alpha) const {
    Call("draw_set_alpha", {RValue(alpha)});
    Call("draw_set_color", {RValue(static_cast<double>(color))});
    Call("draw_rectangle", {RValue(X(r.x)), RValue(Y(r.y)),
                            RValue(X(r.x + r.w) - 1), RValue(Y(r.y + r.h) - 1),
                            RValue(false)});
    Call("draw_set_alpha", {RValue(1.0)});
}

void Canvas::Outline(const Rect& r, int color) const {
    Call("draw_set_color", {RValue(static_cast<double>(color))});
    Call("draw_rectangle", {RValue(X(r.x)), RValue(Y(r.y)),
                            RValue(X(r.x + r.w) - 1), RValue(Y(r.y + r.h) - 1),
                            RValue(true)});
}

void Canvas::Box(const Rect& r, int fill, int border, double alpha) const {
    Fill(r, fill, alpha);
    if (border >= 0) Outline(r, border);
}

void Canvas::Text(double x, double y, const std::string& text, int color,
                  double scale, int align, double maxWidth) const {
    std::string s = maxWidth > 0 ? Fit(text, maxWidth, scale) : text;
    double px = x;
    if (align == 1) px -= Width(s, scale) / 2.0;
    else if (align == 2) px -= Width(s, scale);

    Call("draw_set_color", {RValue(static_cast<double>(color))});
    // font_small_GUI_latin has a ~4px top bearing at scale 1.
    Call("draw_text_transformed", {RValue(X(px)), RValue(Y(y - 4.0 * scale)),
                                   RValue(s), RValue(S * scale), RValue(S * scale),
                                   RValue(0.0)});
}

} // namespace gml
