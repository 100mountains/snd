// Retained audio-editor matrix primitives for sequencers, tap tables, and
// band editors. Geometry is shared by painting, hit-testing, and semantics.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "snd/ui_draw.h"
#include "snd/ui_retained.h"

namespace snd::ui::retained {

class PaintRenderer;

enum class MatrixRowType {
    ContinuousBar,
    GateCell,
    RouteCell,
    Band,
};

struct MatrixRowBand {
    NodeId id;
    std::string label;
    MatrixRowType type = MatrixRowType::GateCell;
    float height = 18.0f;
    float gutterWidth = -1.0f;
    bool visible = true;
};

struct MatrixGridConfig {
    int columns = 16;
    int activeColumns = 16;
    float columnWidth = 18.0f;
    std::vector<float> columnWidths;
    float columnGap = 1.0f;
    float rowGap = 2.0f;
    float gutterWidth = 44.0f;
    float topPad = 2.0f;
    float bottomPad = 2.0f;
    float playheadHeight = 5.0f;
};

enum class MatrixHitKind {
    None,
    Gutter,
    Row,
    Cell,
    Playhead,
};

struct MatrixHit {
    MatrixHitKind kind = MatrixHitKind::None;
    int row = -1;
    int column = -1;
    NodeId rowId;
};

class MatrixGridGeometry {
public:
    MatrixGridGeometry() = default;
    MatrixGridGeometry(MatrixGridConfig config, std::vector<MatrixRowBand> rows);

    void setConfig(MatrixGridConfig config);
    void setRows(std::vector<MatrixRowBand> rows);
    void setBounds(Rect bounds);

    const MatrixGridConfig& config() const { return config_; }
    const std::vector<MatrixRowBand>& rows() const { return rows_; }
    Rect bounds() const { return bounds_; }

    int columnCount() const;
    int activeColumnCount() const;
    int rowCount() const;
    int rowIndex(const NodeId& rowId) const;
    float leftGutterWidth() const;
    float columnWidth(int column) const;
    float columnsWidth() const;
    Rect contentRect() const;
    Rect rowRect(int row) const;
    Rect gutterRect(int row) const;
    Rect cellRect(int row, int column) const;
    Rect playheadRect(int column) const;
    MatrixHit hitTest(Vec2 point) const;

    static Vec2 preferredSize(const MatrixGridConfig& config,
                              const std::vector<MatrixRowBand>& rows);

private:
    float rowTop(int row) const;
    float columnLeft(int column) const;

    MatrixGridConfig config_;
    std::vector<MatrixRowBand> rows_;
    Rect bounds_;
};

struct ContinuousBarRow {
    NodeId rowId;
    std::function<double(int column)> value;
    std::function<void(int column, double value)> setValue;
    double min = 0.0;
    double max = 1.0;
    double step = 0.01;
    draw::Color color = 0;
};

struct GateCellRow {
    NodeId rowId;
    std::function<bool(int column)> value;
    std::function<void(int column, bool value)> setValue;
    draw::Color color = 0;
};

struct RouteCellRow {
    NodeId rowId;
    std::function<int(int column)> route;
    std::function<void(int column, int route)> setRoute;
    std::function<draw::Color(int route)> routeColor;
    int routeCount = 4;
};

struct MatrixBandCell {
    int column = 0;
    int span = 1;
    std::string label;
    draw::Color color = 0;
};

struct BandRow {
    NodeId rowId;
    std::function<std::vector<MatrixBandCell>()> bands;
};

struct GhostFenceOverlay {
    NodeId id;
    std::string label;
    int firstColumn = 0;
    int lastColumn = 0;
    int firstRow = 0;
    int lastRow = -1;
    bool interactive = false;
    draw::Color color = 0;
};

struct HelpOverlay {
    std::string text;
    int row = -1;
    int column = -1;
};

struct PlayheadOverlay {
    int column = -1;
    draw::Color color = 0;
};

struct SelectionOverlay {
    int firstRow = 0;
    int lastRow = 0;
    int firstColumn = 0;
    int lastColumn = 0;
    draw::Color color = 0;
};

struct DragPreviewOverlay {
    int row = -1;
    int firstColumn = 0;
    int lastColumn = 0;
    double firstValue = 0.0;
    double lastValue = 0.0;
    draw::Color color = 0;
};

struct SequencerMatrixDrag {
    bool active = false;
    MatrixRowType type = MatrixRowType::GateCell;
    int row = -1;
    int column = -1;
    double value = 0.0;
    bool gatePaintValue = false;
    int routePaintValue = 0;
};

struct SequencerMatrixState {
    MatrixGridConfig config;
    std::vector<MatrixRowBand> rows;
    std::vector<ContinuousBarRow> continuousRows;
    std::vector<GateCellRow> gateRows;
    std::vector<RouteCellRow> routeRows;
    std::vector<BandRow> bandRows;
    std::vector<GhostFenceOverlay> ghostFences;
    std::vector<HelpOverlay> helpOverlays;
    std::vector<SelectionOverlay> selections;
    DragPreviewOverlay dragPreview;
    PlayheadOverlay playhead;
    SequencerMatrixDrag drag;
};

namespace widgets {

Node::Ptr sequencerMatrix(NodeId id, std::string name,
                          SequencerMatrixState& state,
                          PaintRenderer* renderer = nullptr,
                          Vec2 size = {});

} // namespace widgets

} // namespace snd::ui::retained
