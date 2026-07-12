#include "snd/ui_retained_matrix.h"

#include "snd/ui_paint.h"
#include "snd/ui_retained_widgets.h"

#include "ui_draw_imgui.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace snd::ui::retained {

namespace {

draw::Vec2 tl(Rect r) { return {r.x, r.y}; }
draw::Vec2 br(Rect r) { return {r.x + r.w, r.y + r.h}; }

double clampd(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

int clampi(int v, int lo, int hi)
{
    return std::max(lo, std::min(hi, v));
}

bool contains(Rect r, Vec2 p)
{
    return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y &&
           p.y <= r.y + r.h;
}

float fontSize(const draw::FrameContext& ctx)
{
    return ctx.fontSizePx > 0.0f ? ctx.fontSizePx : draw::kUiFontPx;
}

draw::Color alpha(draw::Color color, unsigned a)
{
    return (color & 0x00FFFFFFu) | (static_cast<draw::Color>(a) << 24);
}

draw::Color defaultRowColor(MatrixRowType type)
{
    switch (type) {
    case MatrixRowType::ContinuousBar: return 0xFFE8D25Cu;
    case MatrixRowType::GateCell: return 0xFF58F07Au;
    case MatrixRowType::RouteCell: return 0xFFE58A4Fu;
    case MatrixRowType::Band: return 0xFF6FA7FFu;
    }
    return 0xFFFFFFFFu;
}

void surfaceText(draw::Surface& s, const draw::FrameContext& ctx, draw::Vec2 p,
                 draw::Color color, const std::string& text)
{
    if (text.empty())
        return;
    s.text(ctx.font, fontSize(ctx), p, color, text.c_str());
}

const MatrixRowBand* rowBand(const SequencerMatrixState& state, int row)
{
    if (row < 0)
        return nullptr;
    int visible = 0;
    for (const auto& band : state.rows) {
        if (!band.visible)
            continue;
        if (visible == row)
            return &band;
        ++visible;
    }
    return nullptr;
}

const ContinuousBarRow* continuousFor(const SequencerMatrixState& state,
                                      const NodeId& id)
{
    for (const auto& row : state.continuousRows)
        if (row.rowId == id)
            return &row;
    return nullptr;
}

const GateCellRow* gateFor(const SequencerMatrixState& state, const NodeId& id)
{
    for (const auto& row : state.gateRows)
        if (row.rowId == id)
            return &row;
    return nullptr;
}

const RouteCellRow* routeFor(const SequencerMatrixState& state, const NodeId& id)
{
    for (const auto& row : state.routeRows)
        if (row.rowId == id)
            return &row;
    return nullptr;
}

const BandRow* bandFor(const SequencerMatrixState& state, const NodeId& id)
{
    for (const auto& row : state.bandRows)
        if (row.rowId == id)
            return &row;
    return nullptr;
}

double continuousValueAt(const MatrixGridGeometry& geom, int row, Vec2 p,
                         const ContinuousBarRow& policy)
{
    Rect rr = geom.rowRect(row);
    const double t =
        1.0 - clampd((p.y - rr.y) / std::max(1.0f, rr.h), 0.0, 1.0);
    return policy.min + (policy.max - policy.min) * t;
}

void setContinuous(const ContinuousBarRow& policy, int column, double value)
{
    if (!policy.setValue)
        return;
    double v = clampd(value, std::min(policy.min, policy.max),
                     std::max(policy.min, policy.max));
    policy.setValue(column, v);
}

void setGate(const GateCellRow& policy, int column, bool value)
{
    if (policy.setValue)
        policy.setValue(column, value);
}

void setRoute(const RouteCellRow& policy, int column, int value)
{
    if (!policy.setRoute)
        return;
    const int count = std::max(1, policy.routeCount);
    policy.setRoute(column, clampi(value, 0, count - 1));
}

void writeInterpolatedContinuous(const ContinuousBarRow& policy, int fromColumn,
                                 double fromValue, int toColumn,
                                 double toValue)
{
    const int lo = std::min(fromColumn, toColumn);
    const int hi = std::max(fromColumn, toColumn);
    const int span = std::max(1, hi - lo);
    for (int c = lo; c <= hi; ++c) {
        const double t = static_cast<double>(c - lo) / span;
        const bool reversed = fromColumn > toColumn;
        const double u = reversed ? 1.0 - t : t;
        setContinuous(policy, c, fromValue + (toValue - fromValue) * u);
    }
}

std::string cellSemanticId(const NodeId& base, int row, int column)
{
    return base + ".cell." + std::to_string(row) + "." +
           std::to_string(column);
}

bool parseCellSemanticId(const NodeId& base, const NodeId& id, int& row,
                         int& column)
{
    const std::string prefix = base + ".cell.";
    if (id.rfind(prefix, 0) != 0)
        return false;
    return std::sscanf(id.c_str() + prefix.size(), "%d.%d", &row, &column) ==
           2;
}

std::string valueText(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

Rect fenceRect(const MatrixGridGeometry& geom, const GhostFenceOverlay& fence)
{
    const int rows = geom.rowCount();
    const int cols = geom.columnCount();
    if (rows <= 0 || cols <= 0)
        return {};
    const int r0 = clampi(fence.firstRow, 0, rows - 1);
    const int r1 = clampi(fence.lastRow < 0 ? rows - 1 : fence.lastRow, 0,
                          rows - 1);
    const int c0 = clampi(std::min(fence.firstColumn, fence.lastColumn), 0,
                          cols - 1);
    const int c1 = clampi(std::max(fence.firstColumn, fence.lastColumn), 0,
                          cols - 1);
    Rect a = geom.cellRect(std::min(r0, r1), c0);
    Rect b = geom.cellRect(std::max(r0, r1), c1);
    return {a.x, a.y, b.x + b.w - a.x, b.y + b.h - a.y};
}

void drawMatrixSurface(draw::Surface& s, const SequencerMatrixState& state,
                       const MatrixGridGeometry& geom,
                       const draw::FrameContext& ctx)
{
    const auto& pal = palette();
    Rect b = geom.bounds();
    s.fillRect(tl(b), br(b), alpha(pal.frame, 0xF4));
    s.strokeRect(tl(b), br(b), pal.frameBright, 0.0f, 1.0f);

    for (int row = 0; row < geom.rowCount(); ++row) {
        const MatrixRowBand* bandPtr = rowBand(state, row);
        if (!bandPtr)
            continue;
        const MatrixRowBand& band = *bandPtr;
        Rect rr = geom.rowRect(row);
        Rect gr = geom.gutterRect(row);
        const draw::Color color = defaultRowColor(band.type);
        s.fillRect(tl(gr), br(gr), alpha(pal.frame, 0xAA));
        surfaceText(s, ctx, {gr.x + 5.0f, gr.y + 2.0f}, pal.textDim,
                    band.label);

        for (int col = 0; col < geom.columnCount(); ++col) {
            Rect cr = geom.cellRect(row, col);
            const bool active = col < geom.activeColumnCount();
            s.fillRect(tl(cr), br(cr),
                       active ? alpha(pal.frame, 0x88)
                              : alpha(pal.frame, 0x32));
            if (band.type == MatrixRowType::ContinuousBar) {
                if (const auto* policy = continuousFor(state, band.id)) {
                    const double v = policy->value ? policy->value(col) : 0.0;
                    const double t = clampd((v - policy->min) /
                                                std::max(1e-9, policy->max -
                                                           policy->min),
                                            0.0, 1.0);
                    Rect fill = cr;
                    fill.y = cr.y + cr.h * static_cast<float>(1.0 - t);
                    fill.h = cr.y + cr.h - fill.y;
                    s.fillRect(tl(fill), br(fill),
                               alpha(policy->color ? policy->color : color,
                                     active ? 0xDD : 0x55));
                }
            } else if (band.type == MatrixRowType::GateCell) {
                if (const auto* policy = gateFor(state, band.id)) {
                    const bool on = policy->value && policy->value(col);
                    if (on) {
                        Rect fill{cr.x + 3.0f, cr.y + 3.0f,
                                  std::max(0.0f, cr.w - 6.0f),
                                  std::max(0.0f, cr.h - 6.0f)};
                        s.fillRect(tl(fill), br(fill),
                                   alpha(policy->color ? policy->color : color,
                                         active ? 0xEE : 0x66),
                                   1.0f);
                    }
                }
            } else if (band.type == MatrixRowType::RouteCell) {
                if (const auto* policy = routeFor(state, band.id)) {
                    const int route = policy->route ? policy->route(col) : 0;
                    draw::Color rc =
                        policy->routeColor ? policy->routeColor(route) : color;
                    Rect fill{cr.x + 2.0f, cr.y + 2.0f,
                              std::max(0.0f, cr.w - 4.0f),
                              std::max(0.0f, cr.h - 4.0f)};
                    s.fillRect(tl(fill), br(fill),
                               alpha(rc, active ? 0xD8 : 0x55), 1.0f);
                }
            }
            s.strokeRect(tl(cr), br(cr), alpha(pal.frameBright, 0x55), 0.0f,
                         1.0f);
        }

        if (band.type == MatrixRowType::Band) {
            if (const auto* policy = bandFor(state, band.id)) {
                std::vector<MatrixBandCell> cells =
                    policy->bands ? policy->bands() : std::vector<MatrixBandCell>{};
                for (const auto& cell : cells) {
                    if (cell.column < 0 || cell.column >= geom.columnCount())
                        continue;
                    Rect a = geom.cellRect(row, cell.column);
                    Rect z = geom.cellRect(
                        row, clampi(cell.column + std::max(1, cell.span) - 1, 0,
                                    geom.columnCount() - 1));
                    Rect sr{a.x + 2.0f, rr.y + 2.0f,
                            z.x + z.w - a.x - 4.0f,
                            std::max(0.0f, rr.h - 4.0f)};
                    s.fillRect(tl(sr), br(sr),
                               alpha(cell.color ? cell.color : color, 0xDD),
                               2.0f);
                    surfaceText(s, ctx, {sr.x + 4.0f, sr.y + 2.0f},
                                pal.text, cell.label);
                }
            }
        }
    }

    for (const auto& sel : state.selections) {
        Rect a = geom.cellRect(sel.firstRow, sel.firstColumn);
        Rect z = geom.cellRect(sel.lastRow, sel.lastColumn);
        Rect sr{std::min(a.x, z.x), std::min(a.y, z.y),
                std::abs((z.x + z.w) - a.x), std::abs((z.y + z.h) - a.y)};
        const draw::Color c = sel.color ? sel.color : 0xFF58F07Au;
        s.strokeRect(tl(sr), br(sr), alpha(c, 0xDD), 0.0f, 2.0f);
    }

    for (const auto& fence : state.ghostFences) {
        Rect fr = fenceRect(geom, fence);
        if (fr.w <= 0.0f || fr.h <= 0.0f)
            continue;
        const draw::Color c = fence.color ? fence.color : 0xFF58F07Au;
        s.fillRect(tl(fr), br(fr), alpha(c, 0x18));
        s.strokeRect(tl(fr), br(fr), alpha(c, fence.interactive ? 0xCC : 0x77),
                     0.0f, fence.interactive ? 2.0f : 1.0f);
        surfaceText(s, ctx, {fr.x + 4.0f, fr.y + 2.0f}, alpha(c, 0xEE),
                    fence.label);
    }

    if (state.dragPreview.row >= 0) {
        const int r = clampi(state.dragPreview.row, 0,
                             std::max(0, geom.rowCount() - 1));
        Rect a = geom.cellRect(r, state.dragPreview.firstColumn);
        Rect z = geom.cellRect(r, state.dragPreview.lastColumn);
        draw::Vec2 pts[2] = {{a.x + a.w * 0.5f,
                              a.y + a.h *
                                        static_cast<float>(1.0 -
                                                           state.dragPreview
                                                               .firstValue)},
                             {z.x + z.w * 0.5f,
                              z.y + z.h *
                                        static_cast<float>(1.0 -
                                                           state.dragPreview
                                                               .lastValue)}};
        s.line(pts[0], pts[1],
               state.dragPreview.color ? state.dragPreview.color : 0xFFE8D25Cu,
               2.0f);
    }

    const int playCol = state.playhead.column;
    if (playCol >= 0 && playCol < geom.columnCount()) {
        Rect pr = geom.playheadRect(playCol);
        const draw::Color c =
            state.playhead.color ? state.playhead.color : 0xFFFFFFFFu;
        s.fillRect(tl(pr), br(pr), alpha(c, 0xEE));
        Rect cr = geom.cellRect(0, playCol);
        Rect full{cr.x, geom.contentRect().y, cr.w, geom.contentRect().h};
        s.fillRect(tl(full), br(full), alpha(c, 0x18));
    }

    for (const auto& help : state.helpOverlays) {
        Rect anchor = help.row >= 0 && help.column >= 0
                          ? geom.cellRect(help.row, help.column)
                          : b;
        surfaceText(s, ctx, {anchor.x + 4.0f, anchor.y + 4.0f}, pal.text,
                    help.text);
    }
}

void drawMatrixImGui(ImDrawList& dl, const SequencerMatrixState& state,
                     const MatrixGridGeometry& geom)
{
    draw::ImGuiSurface surface(&dl);
    draw::FrameContext ctx;
    ctx.fontSizePx = ImGui::GetFontSize();
    drawMatrixSurface(surface, state, geom, ctx);
}

void refreshSemantics(Node& node, SequencerMatrixState& state)
{
    node.setSemanticChildren([&state](const Node& n,
                                      std::vector<SemanticNode>& out) {
        MatrixGridGeometry g(state.config, state.rows);
        g.setBounds(n.bounds());
        const int columns = g.columnCount();
        if (columns > 64)
            return;
        for (int row = 0; row < g.rowCount(); ++row) {
            const MatrixRowBand* bandPtr = rowBand(state, row);
            if (!bandPtr)
                continue;
            const MatrixRowBand& band = *bandPtr;
            for (int col = 0; col < columns; ++col) {
                SemanticNode sn;
                sn.id = cellSemanticId(n.id(), row, col);
                sn.parent = n.id();
                sn.bounds = g.cellRect(row, col);
                sn.name = band.label + " step " + std::to_string(col + 1);
                sn.states = stateMask(SemanticState::Focusable);
                if (band.type == MatrixRowType::GateCell) {
                    sn.role = Role::Toggle;
                    if (const auto* policy = gateFor(state, band.id)) {
                        const bool on = policy->value && policy->value(col);
                        sn.value.text = on ? "on" : "off";
                        if (on)
                            sn.states |= SemanticState::Checked;
                    }
                    sn.actions = {Action::Activate};
                } else if (band.type == MatrixRowType::ContinuousBar) {
                    sn.role = Role::Slider;
                    if (const auto* policy = continuousFor(state, band.id)) {
                        const double v = policy->value ? policy->value(col) : 0.0;
                        sn.value = {true, v, policy->min, policy->max,
                                    policy->step, valueText(v)};
                    }
                    sn.actions = {Action::Increment, Action::Decrement,
                                  Action::SetValue};
                } else if (band.type == MatrixRowType::RouteCell) {
                    sn.role = Role::ListItem;
                    if (const auto* policy = routeFor(state, band.id)) {
                        const int v = policy->route ? policy->route(col) : 0;
                        sn.value = {true, static_cast<double>(v), 0.0,
                                    static_cast<double>(
                                        std::max(1, policy->routeCount) - 1),
                                    1.0, std::to_string(v)};
                    }
                    sn.actions = {Action::Activate, Action::Increment,
                                  Action::Decrement};
                } else {
                    sn.role = Role::ListItem;
                }
                out.push_back(std::move(sn));
            }
        }
    });
}

} // namespace

MatrixGridGeometry::MatrixGridGeometry(MatrixGridConfig config,
                                       std::vector<MatrixRowBand> rows)
    : config_(std::move(config)), rows_(std::move(rows))
{
}

void MatrixGridGeometry::setConfig(MatrixGridConfig config)
{
    config_ = std::move(config);
}

void MatrixGridGeometry::setRows(std::vector<MatrixRowBand> rows)
{
    rows_ = std::move(rows);
}

void MatrixGridGeometry::setBounds(Rect bounds)
{
    bounds_ = bounds;
}

int MatrixGridGeometry::columnCount() const
{
    return std::max(0, config_.columns);
}

int MatrixGridGeometry::activeColumnCount() const
{
    return clampi(config_.activeColumns, 0, columnCount());
}

int MatrixGridGeometry::rowCount() const
{
    int count = 0;
    for (const auto& row : rows_)
        if (row.visible)
            ++count;
    return count;
}

int MatrixGridGeometry::rowIndex(const NodeId& rowId) const
{
    int visible = 0;
    for (const auto& row : rows_) {
        if (!row.visible)
            continue;
        if (row.id == rowId)
            return visible;
        ++visible;
    }
    return -1;
}

float MatrixGridGeometry::leftGutterWidth() const
{
    float w = std::max(0.0f, config_.gutterWidth);
    for (const auto& row : rows_) {
        if (!row.visible)
            continue;
        if (row.gutterWidth >= 0.0f)
            w = std::max(w, row.gutterWidth);
    }
    return w;
}

float MatrixGridGeometry::columnWidth(int column) const
{
    if (column >= 0 && column < static_cast<int>(config_.columnWidths.size()))
        return std::max(1.0f, config_.columnWidths[static_cast<std::size_t>(column)]);
    return std::max(1.0f, config_.columnWidth);
}

float MatrixGridGeometry::columnsWidth() const
{
    float w = 0.0f;
    for (int c = 0; c < columnCount(); ++c) {
        if (c > 0)
            w += config_.columnGap;
        w += columnWidth(c);
    }
    return w;
}

Rect MatrixGridGeometry::contentRect() const
{
    const float x = bounds_.x + leftGutterWidth();
    const float y = bounds_.y + std::max(0.0f, config_.topPad) +
                    std::max(0.0f, config_.playheadHeight);
    return {x, y, columnsWidth(),
            std::max(0.0f, bounds_.y + bounds_.h - y -
                               std::max(0.0f, config_.bottomPad))};
}

float MatrixGridGeometry::rowTop(int row) const
{
    float y = contentRect().y;
    int visible = 0;
    for (const auto& band : rows_) {
        if (!band.visible)
            continue;
        if (visible == row)
            return y;
        y += std::max(1.0f, band.height) + config_.rowGap;
        ++visible;
    }
    return y;
}

Rect MatrixGridGeometry::rowRect(int row) const
{
    if (row < 0)
        return {};
    int visible = 0;
    for (const auto& band : rows_) {
        if (!band.visible)
            continue;
        if (visible == row) {
            const float y = rowTop(row);
            return {bounds_.x, y, leftGutterWidth() + columnsWidth(),
                    std::max(1.0f, band.height)};
        }
        ++visible;
    }
    return {};
}

Rect MatrixGridGeometry::gutterRect(int row) const
{
    Rect rr = rowRect(row);
    rr.w = leftGutterWidth();
    return rr;
}

float MatrixGridGeometry::columnLeft(int column) const
{
    float x = bounds_.x + leftGutterWidth();
    for (int c = 0; c < column; ++c)
        x += columnWidth(c) + config_.columnGap;
    return x;
}

Rect MatrixGridGeometry::cellRect(int row, int column) const
{
    if (row < 0 || column < 0 || column >= columnCount())
        return {};
    Rect rr = rowRect(row);
    return {columnLeft(column), rr.y, columnWidth(column), rr.h};
}

Rect MatrixGridGeometry::playheadRect(int column) const
{
    if (column < 0 || column >= columnCount())
        return {};
    const float h = std::max(0.0f, config_.playheadHeight);
    return {columnLeft(column), bounds_.y + std::max(0.0f, config_.topPad),
            columnWidth(column), h};
}

MatrixHit MatrixGridGeometry::hitTest(Vec2 point) const
{
    for (int col = 0; col < columnCount(); ++col) {
        Rect pr = playheadRect(col);
        if (pr.w > 0.0f && pr.h > 0.0f && contains(pr, point))
            return {MatrixHitKind::Playhead, -1, col, {}};
    }
    for (int row = 0; row < rowCount(); ++row) {
        Rect rr = rowRect(row);
        if (!contains(rr, point))
            continue;
        NodeId rowId;
        int visible = 0;
        for (const auto& band : rows_) {
            if (!band.visible)
                continue;
            if (visible == row) {
                rowId = band.id;
                break;
            }
            ++visible;
        }
        if (contains(gutterRect(row), point))
            return {MatrixHitKind::Gutter, row, -1, rowId};
        for (int col = 0; col < columnCount(); ++col)
            if (contains(cellRect(row, col), point))
                return {MatrixHitKind::Cell, row, col, rowId};
        return {MatrixHitKind::Row, row, -1, rowId};
    }
    return {};
}

Vec2 MatrixGridGeometry::preferredSize(
    const MatrixGridConfig& config, const std::vector<MatrixRowBand>& rows)
{
    MatrixGridGeometry geom(config, rows);
    float h = std::max(0.0f, config.topPad) + std::max(0.0f, config.playheadHeight) +
              std::max(0.0f, config.bottomPad);
    int visibleRows = 0;
    for (const auto& row : rows) {
        if (!row.visible)
            continue;
        if (visibleRows > 0)
            h += config.rowGap;
        h += std::max(1.0f, row.height);
        ++visibleRows;
    }
    return {geom.leftGutterWidth() + geom.columnsWidth(), h};
}

namespace widgets {

Node::Ptr sequencerMatrix(NodeId id, std::string name,
                          SequencerMatrixState& state, PaintRenderer* renderer,
                          Vec2 size)
{
    NodeId styleId = id;
    if (size.x <= 0.0f || size.y <= 0.0f)
        size = MatrixGridGeometry::preferredSize(state.config, state.rows);

    auto node = Node::make(std::move(id), Role::Canvas);
    node->setIntrinsicSize(size);
    node->setSize(Length::intrinsic(), Length::intrinsic());
    node->setFocusable(true);
    Semantics sem;
    sem.role = Role::Canvas;
    sem.name = std::move(name);
    sem.description = "Audio editor matrix";
    sem.states = stateMask(SemanticState::Focusable);
    node->setSemantics(sem);
    refreshSemantics(*node, state);

    node->setOnEvent([&state](Node& n, const Event& event) {
        MatrixGridGeometry geom(state.config, state.rows);
        geom.setBounds(n.bounds());

        if (event.type == EventType::MouseUp) {
            state.drag = {};
            n.markDirty();
            return true;
        }

        const bool down = event.type == EventType::MouseDown &&
                          event.button == MouseButton::Left;
        const bool move = event.type == EventType::MouseMove && n.pressed();
        if (!down && !move)
            return false;

        MatrixHit hit = geom.hitTest(event.position);
        if (hit.kind != MatrixHitKind::Cell)
            return false;
        const MatrixRowBand* band = rowBand(state, hit.row);
        if (!band)
            return false;

        if (band->type == MatrixRowType::ContinuousBar) {
            const auto* policy = continuousFor(state, band->id);
            if (!policy)
                return false;
            const double next = continuousValueAt(geom, hit.row, event.position,
                                                  *policy);
            if (down || !state.drag.active || state.drag.row != hit.row ||
                state.drag.type != band->type) {
                setContinuous(*policy, hit.column, next);
                state.drag = {true, band->type, hit.row, hit.column, next};
            } else {
                writeInterpolatedContinuous(*policy, state.drag.column,
                                            state.drag.value, hit.column, next);
                state.drag.column = hit.column;
                state.drag.value = next;
            }
            n.markDirty();
            return true;
        }

        if (band->type == MatrixRowType::GateCell) {
            const auto* policy = gateFor(state, band->id);
            if (!policy)
                return false;
            if (down || !state.drag.active || state.drag.row != hit.row ||
                state.drag.type != band->type) {
                const bool current = policy->value && policy->value(hit.column);
                state.drag = {true, band->type, hit.row, hit.column, 0.0,
                              !current};
            }
            const int lo = std::min(state.drag.column, hit.column);
            const int hi = std::max(state.drag.column, hit.column);
            for (int c = lo; c <= hi; ++c)
                setGate(*policy, c, state.drag.gatePaintValue);
            state.drag.column = hit.column;
            n.markDirty();
            return true;
        }

        if (band->type == MatrixRowType::RouteCell) {
            const auto* policy = routeFor(state, band->id);
            if (!policy)
                return false;
            if (down || !state.drag.active || state.drag.row != hit.row ||
                state.drag.type != band->type) {
                const int current = policy->route ? policy->route(hit.column) : 0;
                const int count = std::max(1, policy->routeCount);
                state.drag = {true, band->type, hit.row, hit.column, 0.0, false,
                              (current + 1) % count};
            }
            const int lo = std::min(state.drag.column, hit.column);
            const int hi = std::max(state.drag.column, hit.column);
            for (int c = lo; c <= hi; ++c)
                setRoute(*policy, c, state.drag.routePaintValue);
            state.drag.column = hit.column;
            n.markDirty();
            return true;
        }

        return false;
    });

    node->setOnSemanticAction([&state](Node& n, const NodeId& semanticId,
                                       Action action, double value) {
        int row = -1;
        int col = -1;
        if (!parseCellSemanticId(n.id(), semanticId, row, col))
            return false;
        const MatrixRowBand* band = rowBand(state, row);
        if (!band)
            return false;
        if (band->type == MatrixRowType::GateCell && action == Action::Activate) {
            if (const auto* policy = gateFor(state, band->id)) {
                const bool current = policy->value && policy->value(col);
                setGate(*policy, col, !current);
                n.markDirty();
                return true;
            }
        } else if (band->type == MatrixRowType::ContinuousBar) {
            if (const auto* policy = continuousFor(state, band->id)) {
                double next = policy->value ? policy->value(col) : policy->min;
                if (action == Action::Increment)
                    next += policy->step;
                else if (action == Action::Decrement)
                    next -= policy->step;
                else if (action == Action::SetValue)
                    next = value;
                else
                    return false;
                setContinuous(*policy, col, next);
                n.markDirty();
                return true;
            }
        } else if (band->type == MatrixRowType::RouteCell) {
            if (const auto* policy = routeFor(state, band->id)) {
                const int current = policy->route ? policy->route(col) : 0;
                const int count = std::max(1, policy->routeCount);
                int next = current;
                if (action == Action::Increment || action == Action::Activate)
                    next = (current + 1) % count;
                else if (action == Action::Decrement)
                    next = (current + count - 1) % count;
                else
                    return false;
                setRoute(*policy, col, next);
                n.markDirty();
                return true;
            }
        }
        return false;
    });

    if (renderer) {
        VisualStyle style;
        style.kind = VisualKind::Canvas;
        style.canvasDraw = [&state](ImDrawList& dl, const Node&, Rect bounds,
                                    const paint::ControlState&) {
            MatrixGridGeometry geom(state.config, state.rows);
            geom.setBounds(bounds);
            drawMatrixImGui(dl, state, geom);
        };
        style.canvasSurfaceDraw =
            [&state](draw::Surface& surface, const Node&, Rect bounds,
                     const paint::ControlState&, const draw::FrameContext& ctx) {
                MatrixGridGeometry geom(state.config, state.rows);
                geom.setBounds(bounds);
                drawMatrixSurface(surface, state, geom, ctx);
            };
        renderer->setStyle(styleId, style);
    }

    return node;
}

} // namespace widgets

} // namespace snd::ui::retained
