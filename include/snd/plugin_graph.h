// snd::plugin::Graph -- a typed audio/MIDI/control processing graph with
// automatic audio latency compensation (the AudioProcessorGraph replacement).
// Parallel paths that merge are delay-aligned using each node's reported
// latencySamples(), so a phase-linear EQ in one branch no longer combs
// against the dry branch.
//
// Build/connect/prepare on the main thread while not processing; process()
// from one thread at a time.
#pragma once

#include "snd/plugin_host.h"

#include <memory>

namespace snd::plugin {

enum class GraphEdgeType {
    Audio,
    Midi,
    Control,
};

class Graph {
public:
    Graph();
    ~Graph();
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

    // The graph's audio entry and exit points.
    static constexpr int kInput = 0;
    static constexpr int kOutput = 1;

    // Takes ownership; returns the node id (>= 2).
    int addNode(std::unique_ptr<Instance> instance);
    bool removeNode(int node); // disconnects its edges too
    Instance* instance(int node) const;

    bool connect(int from, int to, GraphEdgeType type = GraphEdgeType::Audio);
    bool disconnect(int from, int to, GraphEdgeType type = GraphEdgeType::Audio);

    // Prepares every node, orders the graph, and computes the compensation
    // delays. False on a cycle or a node that failed to prepare.
    bool prepare(double sampleRate, uint32_t maxBlockFrames);
    void unprepare();

    // Total input->output delay after compensation (valid after prepare()).
    uint32_t latencySamples() const;

    // Stereo in/out. frames <= maxBlockFrames.
    bool process(const float* const* in, float* const* out, uint32_t frames);
    bool processEvents(const float* const* in, float* const* out, uint32_t frames,
                       const midi::Buffer& midiIn, midi::Buffer* midiOut = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace snd::plugin
