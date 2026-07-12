// snd::plugin::Graph implementation: topological order + per-edge ring
// delays sized so every path into a merge point carries equal latency.

#include "snd/plugin_graph.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace snd::plugin {

namespace {

struct DelayLine {
    std::vector<float> l, r;
    size_t pos = 0;

    void resize(uint32_t samples)
    {
        l.assign(samples, 0.0f);
        r.assign(samples, 0.0f);
        pos = 0;
    }

    // push one sample per channel, pop the delayed one
    inline void tick(float inL, float inR, float& outL, float& outR)
    {
        if (l.empty()) {
            outL = inL;
            outR = inR;
            return;
        }
        outL = l[pos];
        outR = r[pos];
        l[pos] = inL;
        r[pos] = inR;
        pos = (pos + 1) % l.size();
    }
};

} // namespace

struct Graph::Impl {
    struct Node {
        std::unique_ptr<Instance> instance; // null for kInput/kOutput
        std::vector<float> sumL, sumR;      // summed (delay-aligned) inputs
        std::vector<float> outL, outR;      // this node's output
        midi::Buffer midiIn, midiOut;
        control::Buffer controlIn, controlOut;
        uint32_t accLatency = 0;
    };

    struct Edge {
        int from = -1, to = -1;
        GraphEdgeType type = GraphEdgeType::Audio;
        DelayLine delay;
        uint32_t delaySamples = 0;
    };

    std::map<int, Node> nodes;
    std::vector<Edge> edges;
    std::vector<int> order; // topological, prepared
    midi::Buffer emptyMidi;
    int nextId = 2;
    uint32_t maxBlock = 0;
    uint32_t totalLatency = 0;
    bool prepared = false;

    bool hasEdge(int from, int to, GraphEdgeType type) const
    {
        for (auto& e : edges)
            if (e.from == from && e.to == to && e.type == type)
                return true;
        return false;
    }

    bool topoSort()
    {
        order.clear();
        std::map<int, int> indegree;
        for (auto& [id, n] : nodes)
            indegree[id] = 0;
        for (auto& e : edges)
            ++indegree[e.to];

        std::vector<int> ready;
        for (auto& [id, deg] : indegree)
            if (deg == 0)
                ready.push_back(id);

        while (!ready.empty()) {
            int id = ready.back();
            ready.pop_back();
            order.push_back(id);
            for (auto& e : edges)
                if (e.from == id && --indegree[e.to] == 0)
                    ready.push_back(e.to);
        }
        return order.size() == nodes.size(); // false = cycle
    }
};

Graph::Graph() : impl(new Impl)
{
    impl->nodes[kInput];  // pass-through endpoints
    impl->nodes[kOutput];
}

Graph::~Graph() { unprepare(); }

int Graph::addNode(std::unique_ptr<Instance> instance)
{
    if (!instance)
        return -1;
    int id = impl->nextId++;
    impl->nodes[id].instance = std::move(instance);
    impl->prepared = false;
    return id;
}

bool Graph::removeNode(int node)
{
    if (node == kInput || node == kOutput || !impl->nodes.count(node))
        return false;
    impl->edges.erase(std::remove_if(impl->edges.begin(), impl->edges.end(),
                                     [&](auto& e) {
                                         return e.from == node || e.to == node;
                                     }),
                      impl->edges.end());
    impl->nodes.erase(node);
    impl->prepared = false;
    return true;
}

Instance* Graph::instance(int node) const
{
    auto it = impl->nodes.find(node);
    return it == impl->nodes.end() ? nullptr : it->second.instance.get();
}

bool Graph::connect(int from, int to, GraphEdgeType type)
{
    if (from == to || !impl->nodes.count(from) || !impl->nodes.count(to) ||
        to == kInput || from == kOutput || impl->hasEdge(from, to, type))
        return false;
    Impl::Edge e;
    e.from = from;
    e.to = to;
    e.type = type;
    impl->edges.push_back(std::move(e));
    impl->prepared = false;
    return true;
}

bool Graph::disconnect(int from, int to, GraphEdgeType type)
{
    auto& es = impl->edges;
    size_t before = es.size();
    es.erase(std::remove_if(es.begin(), es.end(),
                            [&](auto& e) {
                                return e.from == from && e.to == to && e.type == type;
                            }),
             es.end());
    impl->prepared = false;
    return es.size() != before;
}

bool Graph::prepare(double sampleRate, uint32_t maxBlockFrames)
{
    unprepare();
    if (!impl->topoSort())
        return false;

    impl->maxBlock = maxBlockFrames;
    std::vector<Instance*> preparedInstances;
    for (auto& [id, n] : impl->nodes) {
        if (n.instance) {
            if (!n.instance->prepare(sampleRate, maxBlockFrames)) {
                for (auto* prepared : preparedInstances)
                    prepared->unprepare();
                return false;
            }
            preparedInstances.push_back(n.instance.get());
        }
        n.sumL.assign(maxBlockFrames, 0.0f);
        n.sumR.assign(maxBlockFrames, 0.0f);
        n.outL.assign(maxBlockFrames, 0.0f);
        n.outR.assign(maxBlockFrames, 0.0f);
        n.accLatency = 0;
    }

    // Latency walk in topological order: every merge point waits for its
    // slowest input; faster edges get a ring delay to match.
    for (int id : impl->order) {
        auto& n = impl->nodes[id];
        uint32_t maxIn = 0;
        for (auto& e : impl->edges)
            if (e.to == id && e.type == GraphEdgeType::Audio)
                maxIn = std::max(maxIn, impl->nodes[e.from].accLatency);
        for (auto& e : impl->edges)
            if (e.to == id && e.type == GraphEdgeType::Audio) {
                e.delaySamples = maxIn - impl->nodes[e.from].accLatency;
                e.delay.resize(e.delaySamples);
            }
        uint32_t own = n.instance ? n.instance->latencySamples() : 0;
        n.accLatency = maxIn + own;
    }
    impl->totalLatency = impl->nodes[kOutput].accLatency;
    impl->prepared = true;
    return true;
}

void Graph::unprepare()
{
    if (!impl->prepared)
        return;
    for (auto& [id, n] : impl->nodes)
        if (n.instance)
            n.instance->unprepare();
    impl->prepared = false;
}

uint32_t Graph::latencySamples() const { return impl->totalLatency; }

bool Graph::process(const float* const* in, float* const* out, uint32_t frames)
{
    return processEvents(in, out, frames, impl->emptyMidi, nullptr);
}

bool Graph::processEvents(const float* const* in, float* const* out,
                          uint32_t frames, const midi::Buffer& midiIn,
                          midi::Buffer* midiOut)
{
    if (!impl->prepared || frames == 0 || frames > impl->maxBlock)
        return false;

    for (auto& [id, n] : impl->nodes) {
        std::memset(n.sumL.data(), 0, sizeof(float) * frames);
        std::memset(n.sumR.data(), 0, sizeof(float) * frames);
        n.midiIn.clear();
        n.midiOut.clear();
        n.controlIn.clear();
        n.controlOut.clear();
    }
    if (midiOut)
        midiOut->clear();

    bool ok = true;
    for (int id : impl->order) {
        auto& n = impl->nodes[id];

        // gather delay-aligned inputs
        for (auto& e : impl->edges) {
            if (e.to != id)
                continue;
            auto& src = impl->nodes[e.from];
            if (e.type == GraphEdgeType::Audio) {
                for (uint32_t f = 0; f < frames; ++f) {
                    float dl, dr;
                    e.delay.tick(src.outL[f], src.outR[f], dl, dr);
                    n.sumL[f] += dl;
                    n.sumR[f] += dr;
                }
            } else if (e.type == GraphEdgeType::Midi) {
                for (const auto& event : src.midiOut)
                    ok = n.midiIn.push_back(event) && ok;
            } else {
                for (const auto& event : src.controlOut)
                    ok = n.controlIn.push_back(event) && ok;
            }
        }
        std::sort(n.midiIn.begin(), n.midiIn.end(),
                  [](const midi::Message& a, const midi::Message& b) {
                      return a.frame < b.frame;
                  });
        std::sort(n.controlIn.begin(), n.controlIn.end(),
                  [](const control::Event& a, const control::Event& b) {
                      return a.frame < b.frame;
                  });

        if (id == kInput) {
            if (in && in[0] && in[1]) {
                std::memcpy(n.outL.data(), in[0], sizeof(float) * frames);
                std::memcpy(n.outR.data(), in[1], sizeof(float) * frames);
            } else {
                std::memset(n.outL.data(), 0, sizeof(float) * frames);
                std::memset(n.outR.data(), 0, sizeof(float) * frames);
            }
            for (const auto& event : midiIn)
                ok = n.midiOut.push_back(event) && ok;
        } else if (n.instance) {
            const float* ins[2] = {n.sumL.data(), n.sumR.data()};
            float* outs[2] = {n.outL.data(), n.outR.data()};
            ok = n.instance->processEvents(ins, 2, outs, 2, frames, n.midiIn,
                                           &n.midiOut, n.controlIn,
                                           &n.controlOut) && ok;
        } else {
            // pass-through (kOutput and any utility nodes)
            std::memcpy(n.outL.data(), n.sumL.data(), sizeof(float) * frames);
            std::memcpy(n.outR.data(), n.sumR.data(), sizeof(float) * frames);
            for (const auto& event : n.midiIn)
                ok = n.midiOut.push_back(event) && ok;
            for (const auto& event : n.controlIn)
                ok = n.controlOut.push_back(event) && ok;
        }
    }

    auto& o = impl->nodes[kOutput];
    if (out && out[0] && out[1]) {
        std::memcpy(out[0], o.outL.data(), sizeof(float) * frames);
        std::memcpy(out[1], o.outR.data(), sizeof(float) * frames);
    }
    if (midiOut)
        for (const auto& event : o.midiOut)
            ok = midiOut->push_back(event) && ok;
    return ok;
}

} // namespace snd::plugin
