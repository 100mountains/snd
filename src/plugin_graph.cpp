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
    std::vector<float> samples;
    size_t pos = 0;

    void resize(uint32_t samples)
    {
        this->samples.assign(samples, 0.0f);
        pos = 0;
    }

    // push one sample per channel, pop the delayed one
    inline float tick(float input)
    {
        if (samples.empty())
            return input;
        const float output = samples[pos];
        samples[pos] = input;
        pos = (pos + 1) % samples.size();
        return output;
    }
};

} // namespace

struct Graph::Impl {
    struct Node {
        std::unique_ptr<Instance> instance; // null for kInput/kOutput
        std::vector<std::vector<float>> sum; // delay-aligned input channels
        std::vector<std::vector<float>> out; // output channels
        std::vector<const float*> inputPointers;
        std::vector<float*> outputPointers;
        midi::Buffer midiIn, midiOut;
        control::Buffer controlIn, controlOut;
        uint32_t accLatency = 0;
    };

    struct Edge {
        int from = -1, to = -1;
        GraphEdgeType type = GraphEdgeType::Audio;
        GraphAudioRoute route;
        std::vector<DelayLine> delay;
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

    uint32_t inputChannels(int id) const
    {
        const auto& node = nodes.at(id);
        return node.instance ? node.instance->inputChannels() : 2;
    }

    uint32_t outputChannels(int id) const
    {
        const auto& node = nodes.at(id);
        return node.instance ? node.instance->outputChannels() : 2;
    }

    bool hasEdge(int from, int to, GraphEdgeType type,
                 GraphAudioRoute route = {}) const
    {
        for (auto& e : edges)
            if (e.from == from && e.to == to && e.type == type &&
                (type != GraphEdgeType::Audio ||
                 (e.route.sourceChannel == route.sourceChannel &&
                  e.route.destinationChannel == route.destinationChannel &&
                  e.route.channels == route.channels)))
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
    if (type == GraphEdgeType::Audio) {
        if (from == to || !impl->nodes.count(from) || !impl->nodes.count(to) ||
            to == kInput || from == kOutput)
            return false;
        const uint32_t channels =
            std::min<uint32_t>(2, std::min(impl->outputChannels(from),
                                           impl->inputChannels(to)));
        return channels > 0 && connectAudio(from, to, {0, 0, channels});
    }
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

bool Graph::connectAudio(int from, int to, GraphAudioRoute route)
{
    if (from == to || !impl->nodes.count(from) || !impl->nodes.count(to) ||
        to == kInput || from == kOutput || route.channels == 0 ||
        route.sourceChannel + route.channels > impl->outputChannels(from) ||
        route.destinationChannel + route.channels > impl->inputChannels(to) ||
        impl->hasEdge(from, to, GraphEdgeType::Audio, route))
        return false;
    Impl::Edge edge;
    edge.from = from;
    edge.to = to;
    edge.type = GraphEdgeType::Audio;
    edge.route = route;
    edge.delay.resize(route.channels);
    impl->edges.push_back(std::move(edge));
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

bool Graph::disconnectAudio(int from, int to, GraphAudioRoute route)
{
    auto& edges = impl->edges;
    const auto before = edges.size();
    edges.erase(std::remove_if(edges.begin(), edges.end(),
                               [&](const auto& edge) {
                                   return edge.from == from && edge.to == to &&
                                          edge.type == GraphEdgeType::Audio &&
                                          edge.route.sourceChannel == route.sourceChannel &&
                                          edge.route.destinationChannel == route.destinationChannel &&
                                          edge.route.channels == route.channels;
                               }),
                edges.end());
    impl->prepared = false;
    return edges.size() != before;
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
        n.sum.assign(impl->inputChannels(id),
                     std::vector<float>(maxBlockFrames, 0.0f));
        n.out.assign(impl->outputChannels(id),
                     std::vector<float>(maxBlockFrames, 0.0f));
        n.inputPointers.clear();
        n.outputPointers.clear();
        n.inputPointers.reserve(n.sum.size());
        n.outputPointers.reserve(n.out.size());
        for (const auto& channel : n.sum)
            n.inputPointers.push_back(channel.data());
        for (auto& channel : n.out)
            n.outputPointers.push_back(channel.data());
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
                for (auto& channelDelay : e.delay)
                    channelDelay.resize(e.delaySamples);
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
        for (auto& channel : n.sum)
            std::memset(channel.data(), 0, sizeof(float) * frames);
        for (auto& channel : n.out)
            std::memset(channel.data(), 0, sizeof(float) * frames);
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
                for (uint32_t channel = 0; channel < e.route.channels; ++channel) {
                    auto& source = src.out[e.route.sourceChannel + channel];
                    auto& destination = n.sum[e.route.destinationChannel + channel];
                    auto& delay = e.delay[channel];
                    for (uint32_t frame = 0; frame < frames; ++frame)
                        destination[frame] += delay.tick(source[frame]);
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
            for (uint32_t channel = 0; channel < n.out.size(); ++channel)
                if (in && in[channel])
                    std::memcpy(n.out[channel].data(), in[channel],
                                sizeof(float) * frames);
            for (const auto& event : midiIn)
                ok = n.midiOut.push_back(event) && ok;
        } else if (n.instance) {
            ok = n.instance->processEvents(
                     n.inputPointers.empty() ? nullptr : n.inputPointers.data(),
                     static_cast<uint32_t>(n.inputPointers.size()),
                     n.outputPointers.empty() ? nullptr : n.outputPointers.data(),
                     static_cast<uint32_t>(n.outputPointers.size()), frames, n.midiIn,
                     &n.midiOut, n.controlIn, &n.controlOut) && ok;
        } else {
            // pass-through (kOutput and any utility nodes)
            const auto channels = std::min(n.sum.size(), n.out.size());
            for (std::size_t channel = 0; channel < channels; ++channel)
                std::memcpy(n.out[channel].data(), n.sum[channel].data(),
                            sizeof(float) * frames);
            for (const auto& event : n.midiIn)
                ok = n.midiOut.push_back(event) && ok;
            for (const auto& event : n.controlIn)
                ok = n.controlOut.push_back(event) && ok;
        }
    }

    auto& o = impl->nodes[kOutput];
    if (out && out[0] && out[1]) {
        std::memcpy(out[0], o.out[0].data(), sizeof(float) * frames);
        std::memcpy(out[1], o.out[1].data(), sizeof(float) * frames);
    }
    if (midiOut)
        for (const auto& event : o.midiOut)
            ok = midiOut->push_back(event) && ok;
    return ok;
}

} // namespace snd::plugin
