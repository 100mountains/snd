// snd-widget-shots -- headless renderer that draws each SND widget through the
// real OpenGLSurface (the same GL paint path the app uses) into an offscreen
// framebuffer and dumps straight RGBA. A companion Python step encodes PNGs for
// the website gallery, so the site shows the actual pixels SND draws instead of
// hand-rolled canvas mockups.
//
//   snd-widget-shots <outDir>
//
// Writes <outDir>/<id>.rgba per widget: an ASCII "W H\n" header (framebuffer
// pixels, i.e. logical size * SS) then W*H*4 premultiplied RGBA bytes, rows
// top-to-bottom.

#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

#include "snd/icons.h"
#include "snd/ui.h"
#include "snd/ui_draw.h"
#include "snd/ui_paint.h"

// Private GL backend + font atlas (same headers the retained GL window uses).
#include "ui_draw_gl.h"
#include "ui_font_atlas_stb.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace draw = snd::ui::draw;
namespace paint = snd::ui::paint;
using snd::ui::KnobStyle;
using snd::ui::Palette;

namespace {

constexpr int kSupersample = 2; // render at 2x for crisp downscaling on the page

struct Shot {
    std::string id;
    int w = 0;
    int h = 0;
    std::function<void(draw::Surface&, float w, float h)> draw;
};

} // namespace

int main(int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : "widgetshots";

    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(64, 64, "widget-shots", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    draw::StbFontAtlas fonts;
    std::string fontError;
    if (!fonts.build(&fontError)) {
        fprintf(stderr, "font atlas build failed: %s\n", fontError.c_str());
        return 1;
    }
    draw::OpenGLSurface surface(fonts);
    if (!surface.init()) {
        fprintf(stderr, "surface init failed\n");
        return 1;
    }
    fonts.upload();
    paint::loadTransportIcons(); // crisp Lucide transport SVGs for drawVectorIconButton

    const Palette& pal = snd::ui::palette();
    const draw::FontRef uiFont = fonts.defaultFontRef();
    const float fs = 13.0f;

    // ---- widget table (data-w ids mirror site/index.html) ------------------
    std::vector<Shot> shots;
    auto reg = [&](const char* id, int w, int h,
                   std::function<void(draw::Surface&, float, float)> fn) {
        shots.push_back({id, w, h, std::move(fn)});
    };

    // Knobs: the five styles + a bipolar ring + a disabled ring.
    auto knob = [&](KnobStyle style, float frac, bool bipolar, bool disabled) {
        return [style, frac, bipolar, disabled, &pal](draw::Surface& s, float w,
                                                       float h) {
            paint::ControlState st;
            st.disabled = disabled;
            float sz = (w < h ? w : h) - 8.0f;
            paint::drawKnob(s, {4.0f, 4.0f}, sz, frac, style, pal, st, bipolar, 0);
        };
    };
    reg("ring", 72, 72, knob(KnobStyle::Ring, 0.62f, false, false));
    reg("seq", 72, 72, knob(KnobStyle::Seq, 0.62f, false, false));
    reg("synth", 72, 72, knob(KnobStyle::Synth, 0.62f, false, false));
    reg("davies", 72, 72, knob(KnobStyle::Davies, 0.62f, false, false));
    reg("nxd", 72, 72, knob(KnobStyle::Nxd, 0.62f, false, false));
    reg("ringbi", 72, 72, knob(KnobStyle::Ring, 0.7f, true, false));
    reg("ringdis", 72, 72, knob(KnobStyle::Ring, 0.4f, false, true));

    // Faders at four levels.
    auto fader = [&](float v) {
        return [v, &pal](draw::Surface& s, float w, float h) {
            paint::ControlState st;
            paint::drawFader(s, {0.0f, 0.0f}, {w, h}, v, pal, st);
        };
    };
    reg("fader0", 30, 120, fader(0.2f));
    reg("fader1", 30, 120, fader(0.5f));
    reg("fader2", 30, 120, fader(0.8f));
    reg("fader3", 30, 120, fader(1.0f));

    // Toggle: off / on.
    auto toggle = [&](float anim) {
        return [anim, &pal](draw::Surface& s, float w, float h) {
            paint::ControlState st;
            paint::drawToggle(s, {0.0f, 0.0f}, w, h, anim, pal, st);
        };
    };
    reg("toggleoff", 46, 24, toggle(0.0f));
    reg("toggleon", 46, 24, toggle(1.0f));
    reg("toggle", 46, 24, toggle(1.0f));

    // LEDs: on / off / half-lit (blink mid-state).
    auto led = [&](bool on, float level, draw::Color col) {
        return [on, level, col, &pal](draw::Surface& s, float w, float h) {
            paint::ControlState st;
            (void)level;
            paint::drawLed(s, {w * 0.5f, h * 0.5f}, 10.0f, on, pal, st, col);
        };
    };
    reg("ledon", 34, 34, led(true, 1.0f, 0));
    reg("ledoff", 34, 34, led(false, 0.0f, 0));
    reg("ledblink", 34, 34, led(true, 0.5f, snd::ui::draw::rgba(255, 80, 70, 255)));

    // Segmented control + a two-option A/B + a disabled one.
    auto seg = [&](std::vector<std::string> labels, int sel, bool disabled) {
        return [labels, sel, disabled, &pal, uiFont, fs](draw::Surface& s, float w,
                                                         float h) {
            paint::ControlState st;
            st.disabled = disabled;
            std::vector<const char*> cptr;
            for (auto& l : labels)
                cptr.push_back(l.c_str());
            paint::drawSegmented(s, uiFont, fs, {0.0f, 0.0f}, {w, h}, cptr.data(),
                                 (int)cptr.size(), sel, -1, pal, st);
        };
    };
    reg("seg", 216, 26, seg({"Mono", "Stereo", "M/S"}, 1, false));
    reg("segab", 110, 26, seg({"A", "B"}, 0, false));
    reg("segdis", 216, 26, seg({"Mono", "Stereo", "M/S"}, 1, true));

    // Cycle button.
    reg("cyc", 110, 26, [&pal, uiFont, fs](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        paint::drawCycleButton(s, uiFont, fs, {0.0f, 0.0f}, {w, h}, "1/16", 2, 4, pal, st);
    });

    // Section header.
    reg("header", 260, 20, [&pal, uiFont](draw::Surface& s, float w, float h) {
        (void)h;
        paint::drawSectionHeader(s, uiFont, {0.0f, 4.0f}, "OSCILLATOR", 12.0f, w, pal);
    });

    // Pattern grid + a taller sequencer matrix.
    auto grid = [&](int rows, int steps, int playhead) {
        return [rows, steps, playhead, &pal](draw::Surface& s, float w, float h) {
            paint::ControlState st;
            std::vector<char> cells(rows * steps, 0);
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < steps; ++c)
                    if (((c + r) % 4) == 0)
                        cells[r * steps + c] = 1;
            std::vector<unsigned char> b(cells.begin(), cells.end());
            paint::drawPatternGrid(s, {0.0f, 0.0f}, {w, h},
                                   reinterpret_cast<const bool*>(b.data()), rows, steps,
                                   playhead, pal, st);
        };
    };
    reg("grid", 360, 120, grid(4, 16, 4));
    reg("matrix", 520, 152, grid(8, 16, 6));

    // XY pad.
    reg("xy", 150, 150, [&pal](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        paint::drawXYPad(s, {0.0f, 0.0f}, {w, h}, 0.62f, 0.4f, pal, st);
    });

    // Keyboard.
    reg("keys", 420, 80, [&pal](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        paint::drawKeyboard(s, {0.0f, 0.0f}, {w, h}, 48, 2, -1, nullptr, pal, st);
    });

    // Envelope.
    reg("env", 360, 150, [&pal](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        std::vector<snd::ui::EnvPoint> pts = {
            {0.0f, 0.0f}, {0.25f, 1.0f}, {0.6f, 0.4f}, {1.0f, 0.0f}};
        paint::drawEnvelope(s, {0.0f, 0.0f}, {w, h}, pts, nullptr, -1, -1, -1, -1, pal, st);
    });

    // Modulation-ring knobs: a knob body plus the mod overlay at three depths.
    auto modknob = [&](float frac, float depth, float live) {
        return [frac, depth, live, &pal](draw::Surface& s, float w, float h) {
            paint::ControlState st;
            float sz = (w < h ? w : h) - 8.0f;
            paint::drawKnob(s, {4.0f, 4.0f}, sz, frac, KnobStyle::Synth, pal, st, false, 0);
            snd::ui::KnobMod mod;
            mod.depth = depth;
            mod.value = live;
            paint::drawKnobModRing(s, {4.0f, 4.0f}, sz, frac, mod, pal, 0);
        };
    };
    reg("modknob", 72, 72, modknob(0.5f, 0.28f, 0.72f));
    reg("modknob2", 72, 72, modknob(0.62f, -0.22f, 0.4f));
    reg("modknob3", 72, 72, modknob(0.35f, 0.4f, 0.7f));

    // Randomise window: a knob with the ghost-fence overlay.
    reg("ghostk", 86, 86, [&pal](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        float sz = (w < h ? w : h) - 12.0f;
        paint::drawKnob(s, {6.0f, 6.0f}, sz, 0.55f, KnobStyle::Davies, pal, st, false, 0);
        paint::KnobWindow win;
        win.lo = 0.3f;
        win.hi = 0.8f;
        paint::drawKnobWindow(s, {6.0f, 6.0f}, sz, win, pal, 0, 0, 1.0f);
    });

    // Randomise window as a combo bracket: a combo face + the index window.
    reg("ghostc", 150, 46, [&pal, uiFont, fs](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        draw::Vec2 mn{2.0f, 4.0f};
        draw::Vec2 mx{w - 2.0f, h - 12.0f};
        s.fillRect(mn, mx, pal.frame, 5.0f);
        s.strokeRect(mn, mx, pal.frameBright, 5.0f, 1.0f);
        s.text(uiFont, fs, {mn.x + 10.0f, mn.y + 6.0f}, pal.text, "Saw");
        paint::KnobWindow win;
        win.lo = 0.35f;
        win.hi = 0.7f;
        paint::drawComboWindow(s, mn, {mx.x - mn.x, mx.y - mn.y}, win, pal, 0, 0, 1.0f);
    });

    // Gradient knob: a knob body with a two-colour gradient value arc.
    reg("gradknob", 72, 72, [&pal](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        float sz = (w < h ? w : h) - 8.0f;
        paint::drawKnob(s, {4.0f, 4.0f}, sz, 0.68f, KnobStyle::Synth, pal, st, false, 0);
        draw::Vec2 c{4.0f + sz * 0.5f, 4.0f + sz * 0.5f};
        float r = sz * 0.5f - 4.0f;
        paint::drawGradientArc(s, c, r, paint::knobAngle(0.0f), paint::knobAngle(0.68f),
                               snd::ui::draw::rgba(90, 200, 255, 255),
                               snd::ui::draw::rgba(240, 120, 90, 255), 4.0f, 48);
    });

    // LED buttons: tactile key + status LED ring, three states.
    auto ledButtonShot = [&](const char* glyph, float level, draw::Color col,
                             draw::FontRef lucide) {
        return [glyph, level, col, lucide, &pal](draw::Surface& s, float w, float h) {
            paint::ControlState st;
            paint::drawLedButton(s, lucide, {0.0f, 0.0f}, {w, h}, glyph, level, pal, st,
                                 false, col, 0);
        };
    };
    const draw::FontRef lucide = fonts.iconFontLucideRef();
    reg("ledrec", 46, 40,
        ledButtonShot(ICON_LC_MIC, 1.0f, snd::ui::draw::rgba(255, 80, 70, 255), lucide));
    reg("ledmon", 46, 40,
        ledButtonShot(ICON_LC_HEADPHONES, 1.0f, snd::ui::draw::rgba(80, 220, 120, 255),
                      lucide));
    reg("ledarm", 46, 40,
        ledButtonShot(ICON_LC_CIRCLE, 0.55f, snd::ui::draw::rgba(240, 190, 90, 255),
                      lucide));

    // Velocity grid: a pattern grid whose ON cells fill by "velocity".
    reg("velgrid", 240, 96, [&pal](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        const int rows = 4, steps = 8;
        std::vector<unsigned char> cells(rows * steps, 0);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < steps; ++c)
                if (((c + r) % 3) == 0)
                    cells[r * steps + c] = 1;
        paint::PatternCellPainter painter = [rows, steps, &pal](
                                                const paint::PatternCellPaintArgs& a) {
            if (!a.on)
                return;
            float vel = 0.35f + 0.65f * ((float)(a.step % 4) / 3.0f);
            draw::Color top = paint::withAlpha(pal.accent, (uint32_t)(vel * 255.0f));
            float midY = a.cellMax.y - (a.cellMax.y - a.cellMin.y) * vel;
            a.surface->fillRect({a.cellMin.x, midY}, {a.cellMax.x, a.cellMax.y}, top, 2.0f);
        };
        paint::drawPatternGrid(s, {0.0f, 0.0f}, {w, h},
                               reinterpret_cast<const bool*>(cells.data()), rows, steps, -1,
                               pal, st, painter);
    });

    // XY puck.
    reg("puck", 96, 96, [&pal](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        paint::drawXYPad(s, {0.0f, 0.0f}, {w, h}, 0.35f, 0.68f, pal, st);
    });

    // Custom paint: a gradient-filled disc with an accent arc (painter-hook look).
    reg("custom", 72, 72, [&pal](draw::Surface& s, float w, float h) {
        (void)pal;
        draw::Vec2 c{w * 0.5f, h * 0.5f};
        float r = (w < h ? w : h) * 0.5f - 6.0f;
        s.fillCircle(c, r, snd::ui::draw::rgba(40, 44, 64, 255), 48);
        s.strokeCircle(c, r, snd::ui::draw::rgba(90, 100, 140, 255), 48, 1.5f);
        paint::drawGradientArc(s, c, r - 5.0f, paint::knobAngle(0.0f), paint::knobAngle(0.75f),
                               snd::ui::draw::rgba(240, 190, 90, 255),
                               snd::ui::draw::rgba(230, 90, 140, 255), 5.0f, 48);
        ImVec2 tip = paint::dirAt(paint::knobAngle(0.75f));
        s.line(c, {c.x + tip.x * (r - 8.0f), c.y + tip.y * (r - 8.0f)},
               snd::ui::draw::rgba(255, 255, 255, 255), 2.0f);
    });

    // A second, narrower cycle button.
    reg("cyc2", 74, 26, [&pal, uiFont, fs](draw::Surface& s, float w, float h) {
        paint::ControlState st;
        paint::drawCycleButton(s, uiFont, fs, {0.0f, 0.0f}, {w, h}, "x2", 1, 3, pal, st);
    });

    // Meters: vertical L / R + a horizontal one.
    auto meter = [&](float shown, float peak) {
        return [shown, peak, &pal](draw::Surface& s, float w, float h) {
            paint::drawMeter(s, {0.0f, 0.0f}, {w, h}, shown, peak, -48.0f, pal);
        };
    };
    reg("meterL", 12, 120, meter(0.72f, 0.9f));
    reg("meterR", 12, 120, meter(0.6f, 0.82f));
    reg("meterH", 220, 14, meter(0.68f, 0.86f));

    // Vector transport / scope icons (the Icon enum), drawn as house icon buttons.
    auto vicon = [&](snd::ui::Icon icon, bool active) {
        return [icon, active, &pal](draw::Surface& s, float w, float h) {
            paint::ControlState st;
            paint::drawVectorIconButton(s, {0.0f, 0.0f}, {w, h}, icon, 0, pal, st, active,
                                        0, 0);
        };
    };
    reg("viPlay", 38, 38, vicon(snd::ui::Icon::Play, false));
    reg("viStop", 38, 38, vicon(snd::ui::Icon::Stop, false));
    reg("viRecord", 38, 38, vicon(snd::ui::Icon::Record, true));
    reg("viStart", 38, 38, vicon(snd::ui::Icon::SkipToStart, false));
    reg("viEnd", 38, 38, vicon(snd::ui::Icon::SkipToEnd, false));
    reg("viLoop", 38, 38, vicon(snd::ui::Icon::Loop, true));
    reg("viFollow", 38, 38, vicon(snd::ui::Icon::Follow, false));
    reg("viWaveform", 38, 38, vicon(snd::ui::Icon::Waveform, false));
    reg("viSpectrum", 38, 38, vicon(snd::ui::Icon::Spectrum, false));

    // Graph surface: backdrop + two module boxes + a cable, in two looks.
    auto graph = [&](paint::GraphSurfaceStyle::Backdrop backdrop) {
        return [backdrop, &pal, uiFont, fs](draw::Surface& s, float w, float h) {
            paint::ControlState st;
            paint::GraphSurfaceStyle style = paint::graphSkinStyle(paint::GraphSkin::Neo);
            style.backdrop = backdrop;
            style.backdropFill = paint::graphBackdropFill(backdrop);
            s.pushClip({0.0f, 0.0f}, {w, h}, true);
            paint::drawGraphGrid(s, {0.0f, 0.0f}, {w, h}, {0.0f, 0.0f}, 1.0f, pal, st, style,
                                 0.0);
            paint::drawCable(s, {150.0f, 74.0f}, {228.0f, 120.0f}, pal, st, 0, 2.6f, style,
                             1.0f);
            paint::drawModuleBox(s, uiFont, fs, {26.0f, 34.0f}, {120.0f, 66.0f},
                                 "Oscillator", pal, st, false, false, style, 24.0f, 0.0);
            paint::drawModuleBox(s, uiFont, fs, {228.0f, 88.0f}, {120.0f, 66.0f}, "Filter",
                                 pal, st, false, false, style, 24.0f, 0.0);
            s.popClip();
        };
    };
    reg("graphNeo", 360, 190, graph(paint::GraphSurfaceStyle::Backdrop::Grid));
    reg("graphAurora", 360, 190, graph(paint::GraphSurfaceStyle::Backdrop::Aurora));

    // The bob3 chrome row, for comparing UI text faces (SND_UI_FONT): transport
    // keys, the numeric readouts that count live, and the page tabs.
    reg("chromerow", 560, 26, [&pal, uiFont, fs](draw::Surface& s, float w, float h) {
        (void)w;
        paint::ControlState st;
        paint::OutlineButtonStyle cs;
        cs.fill = snd::ui::draw::rgba(10, 13, 18);
        cs.border = snd::ui::draw::rgba(64, 73, 88);
        cs.text = snd::ui::draw::rgba(226, 231, 239);
        const float bh = 18.0f;
        const float y = (h - bh) * 0.5f;
        float x = 2.0f;
        auto key = [&](snd::ui::Icon ic) {
            paint::drawTransportButton(s, ic, {x, y}, {32.0f, bh}, pal, st, cs, 1.6f);
            x += 36.0f;
        };
        key(snd::ui::Icon::Settings);
        key(snd::ui::Icon::Record);
        key(snd::ui::Icon::Play);
        key(snd::ui::Icon::Loop);
        auto box = [&](const char* text, float bw) {
            paint::drawOutlineButton(s, uiFont, fs, {x, y}, {bw, bh}, text, pal, st, cs);
            x += bw + 4.0f;
        };
        box("120", 42.0f);
        box("4/4", 42.0f);
        box("1.1", 42.0f);
        x += 6.0f;
        const char* tabs[] = {"Graph", "Murk", "Perform", "Mixer", "SEQ"};
        const float tabW[] = {48.0f, 44.0f, 62.0f, 52.0f, 40.0f};
        for (int i = 0; i < 5; ++i) {
            paint::ControlState ts;
            ts.selected = i == 0;
            paint::drawOutlineButton(s, uiFont, fs, {x, y}, {tabW[i], bh}, tabs[i], pal,
                                     ts, cs);
            x += tabW[i] + 4.0f;
        }
    });

    // A synth-panel specimen: mixed-case prose and the ALL-CAPS short labels a
    // module panel is actually built from, so a face can be judged on the words
    // it will really have to render.
    reg("specimen", 540, 26, [&pal, uiFont, fs](draw::Surface& s, float w, float h) {
        (void)w;
        const float y = (h - fs) * 0.5f;
        s.text(uiFont, fs, {4.0f, y}, snd::ui::draw::rgba(226, 231, 239),
               "bob BOB - Filter cutoff 12.4 kHz, resonance 0.62");
        s.text(uiFont, fs, {320.0f, y}, snd::ui::draw::rgba(226, 231, 239),
               "OSC LFO ENV VCA CUTOFF RES ATTACK");
    });

    // A label strip for contact sheets: SND_SHOT_LABEL rendered flush left.
    // Rendered in a separate run with a known-legible face, so a row stays
    // identifiable even when the face under test is too faint to read itself.
    if (const char* labelText = std::getenv("SND_SHOT_LABEL")) {
        std::string label = labelText;
        reg("label", 230, 26, [label, &pal, uiFont](draw::Surface& s, float w, float h) {
            (void)w;
            s.text(uiFont, 13.0f, {4.0f, (h - 13.0f) * 0.5f},
                   snd::ui::draw::rgba(210, 216, 228), label.c_str());
        });
    }

    // ---- render each shot to an FBO and dump RGBA -------------------------
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);

    int ok = 0;
    for (const Shot& shot : shots) {
        const int fbW = shot.w * kSupersample;
        const int fbH = shot.h * kSupersample;

        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbW, fbH, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "FBO incomplete for %s\n", shot.id.c_str());
            continue;
        }

        glViewport(0, 0, fbW, fbH);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        surface.beginFrame({(float)shot.w, (float)shot.h}, fbW, fbH);
        shot.draw(surface, (float)shot.w, (float)shot.h);
        surface.endFrame();

        std::vector<unsigned char> px((size_t)fbW * fbH * 4);
        glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

        // GL reads bottom-up; write rows top-to-bottom.
        std::string path = outDir + "/" + shot.id + ".rgba";
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) {
            fprintf(stderr, "cannot write %s\n", path.c_str());
            continue;
        }
        fprintf(f, "%d %d\n", fbW, fbH);
        const size_t rowBytes = (size_t)fbW * 4;
        for (int y = fbH - 1; y >= 0; --y)
            fwrite(px.data() + (size_t)y * rowBytes, 1, rowBytes, f);
        fclose(f);
        ++ok;
    }

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    paint::releaseTransportIcons();
    surface.destroyGl();
    fonts.destroyGl();
    glfwDestroyWindow(window);
    glfwTerminate();

    printf("rendered %d/%zu widget shots to %s\n", ok, shots.size(), outDir.c_str());
    return ok == (int)shots.size() ? 0 : 2;
}
