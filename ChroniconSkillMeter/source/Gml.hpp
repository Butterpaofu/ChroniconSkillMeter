#pragma once
// -----------------------------------------------------------------------------
// Gml.hpp - thin, defensive wrapper around YYToolkit's GameMaker bindings.
//
// Everything the mod needs from the engine goes through here, so the rest of
// the code stays engine-agnostic and this is the only file to audit when the
// game updates.
// -----------------------------------------------------------------------------
#include <YYToolkit/Shared.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gml {

using RValue = YYTK::RValue;
using CInstance = YYTK::CInstance;
using YYTKInterface = YYTK::YYTKInterface;
using ScriptFn = YYTK::PFUNC_YYGMLScript;

void Init(YYTKInterface* iface);
YYTKInterface* Iface();

RValue Call(const char* fn, std::vector<RValue> args = {});
double Num(const char* fn, std::vector<RValue> args = {});
bool Bool(const char* fn, std::vector<RValue> args = {});

RValue NumV(double v);
RValue StrV(const std::string& v);
double AsNum(const RValue* v);
std::string Text(const RValue& v);        // any RValue -> std::string (via GML string())

double Asset(const char* name);
double PlayerRef();                       // -1 when no player instance exists

void* FindScript(const char* name);
bool HookRaw(Aurie::AurieModule* module, const char* scriptName,
             void* detour, PVOID* original);

// Accepts a plain function pointer without a non-portable function->void* cast.
template <typename Fn>
bool Hook(Aurie::AurieModule* module, const char* scriptName,
          Fn detour, PVOID* original) {
    static_assert(sizeof(Fn) == sizeof(void*),
                  "hook detour must be a plain function pointer");
    void* p = nullptr;
    std::memcpy(&p, &detour, sizeof(p));
    return HookRaw(module, scriptName, p, original);
}

// --- input ------------------------------------------------------------------
bool KeyPressed(int vk);                  // Windows VK_* codes
bool KeyDown(int vk);
bool MousePressed(int button = 1);        // 1 = mb_left, 2 = mb_right
bool MouseDown(int button = 1);
bool WheelUp();
bool WheelDown();

// --- drawing ----------------------------------------------------------------
// GameMaker stores colours as 0xBBGGRR.
int Col(int r, int g, int b);

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool Contains(double px, double py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

// Draws in a virtual 960x540 coordinate space, letterboxed and scaled to the
// current GUI size - the same approach Chronicon's own HUD uses.
// Construction saves the draw state; destruction restores it.
class Canvas {
public:
    Canvas();
    ~Canvas();

    double S = 1, OX = 0, OY = 0, W = 960, H = 540;

    double X(double x) const;
    double Y(double y) const;
    // Text width in the virtual 960x540 space (scale applied, GUI scale not).
    double Width(const std::string& text, double scale = 1) const;
    std::string Fit(const std::string& text, double width, double scale = 1) const;

    void Fill(const Rect& r, int color, double alpha = 1.0) const;
    void Outline(const Rect& r, int color) const;
    void Box(const Rect& r, int fill, int border = -1, double alpha = 1.0) const;

    // align: 0 = left, 1 = centre, 2 = right
    void Text(double x, double y, const std::string& text, int color,
              double scale = 1.0, int align = 0, double maxWidth = 0) const;

private:
    RValue savedColor_, savedAlpha_, savedFont_, savedH_, savedV_;
    bool valid_ = false;
};

} // namespace gml
