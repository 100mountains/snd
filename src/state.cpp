// snd::state implementation. Nodes are shared; listeners bubble up the
// parent chain; XML matches JUCE ValueTree's shape for later Murk reads.

#include "snd/state.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>

namespace snd::state {

// ---------------------------------------------------------------------------

struct Tree::Node : std::enable_shared_from_this<Tree::Node> {
    std::string type;
    std::vector<std::pair<std::string, Value>> props; // ordered, small
    std::vector<std::shared_ptr<Node>> children;
    std::weak_ptr<Node> parent;

    struct Entry {
        uint64_t id;
        Listener l;
    };
    std::vector<Entry> listeners;
    static uint64_t nextListenerId;

    Value* find(const std::string& key)
    {
        for (auto& p : props)
            if (p.first == key)
                return &p.second;
        return nullptr;
    }

    // notify this node and every ancestor
    template <typename F> void bubble(F&& fire)
    {
        for (Node* n = this; n;) {
            for (auto& e : n->listeners)
                fire(e.l);
            auto p = n->parent.lock();
            n = p ? p.get() : nullptr;
        }
    }
};

uint64_t Tree::Node::nextListenerId = 1;

// ---------------------------------------------------------------------------

Tree::Tree(std::string type) : node_(std::make_shared<Node>())
{
    node_->type = std::move(type);
}

const std::string& Tree::type() const
{
    static const std::string empty;
    return node_ ? node_->type : empty;
}

bool Tree::hasProperty(const std::string& key) const
{
    return node_ && node_->find(key) != nullptr;
}

Value Tree::get(const std::string& key) const
{
    if (node_)
        if (auto* v = node_->find(key))
            return *v;
    return {};
}

int64_t Tree::getInt(const std::string& key, int64_t fallback) const
{
    auto v = get(key);
    if (auto* i = std::get_if<int64_t>(&v))
        return *i;
    if (auto* d = std::get_if<double>(&v))
        return (int64_t)*d;
    return fallback;
}

double Tree::getDouble(const std::string& key, double fallback) const
{
    auto v = get(key);
    if (auto* d = std::get_if<double>(&v))
        return *d;
    if (auto* i = std::get_if<int64_t>(&v))
        return (double)*i;
    return fallback;
}

bool Tree::getBool(const std::string& key, bool fallback) const
{
    auto v = get(key);
    if (auto* b = std::get_if<bool>(&v))
        return *b;
    if (auto* i = std::get_if<int64_t>(&v))
        return *i != 0;
    return fallback;
}

std::string Tree::getString(const std::string& key, const std::string& fallback) const
{
    auto v = get(key);
    if (auto* s = std::get_if<std::string>(&v))
        return *s;
    return fallback;
}

void Tree::set(const std::string& key, Value v, UndoManager* um)
{
    if (!node_)
        return;
    Value old = get(key);
    bool existed = hasProperty(key);

    if (auto* slot = node_->find(key))
        *slot = v;
    else
        node_->props.emplace_back(key, v);

    if (um) {
        Tree self = *this;
        um->record(
            [self, key, old, existed]() mutable {
                if (existed)
                    self.set(key, old, nullptr);
                else
                    self.removeProperty(key, nullptr);
            },
            [self, key, v]() mutable { self.set(key, v, nullptr); });
    }

    Tree self = *this;
    node_->bubble([&](Listener& l) {
        if (l.onProperty)
            l.onProperty(self, key);
    });
}

void Tree::removeProperty(const std::string& key, UndoManager* um)
{
    if (!node_)
        return;
    auto& props = node_->props;
    auto it = std::find_if(props.begin(), props.end(),
                           [&](auto& p) { return p.first == key; });
    if (it == props.end())
        return;
    Value old = it->second;
    props.erase(it);

    if (um) {
        Tree self = *this;
        um->record([self, key, old]() mutable { self.set(key, old, nullptr); },
                   [self, key]() mutable { self.removeProperty(key, nullptr); });
    }

    Tree self = *this;
    node_->bubble([&](Listener& l) {
        if (l.onProperty)
            l.onProperty(self, key);
    });
}

std::vector<std::string> Tree::propertyNames() const
{
    std::vector<std::string> out;
    if (node_)
        for (auto& p : node_->props)
            out.push_back(p.first);
    return out;
}

size_t Tree::childCount() const { return node_ ? node_->children.size() : 0; }

Tree Tree::child(size_t index) const
{
    if (!node_ || index >= node_->children.size())
        return {};
    return Tree(node_->children[index]);
}

Tree Tree::childWithType(const std::string& type) const
{
    if (node_)
        for (auto& c : node_->children)
            if (c->type == type)
                return Tree(c);
    return {};
}

Tree Tree::getOrCreateChild(const std::string& type, UndoManager* um)
{
    Tree existing = childWithType(type);
    if (existing.valid())
        return existing;
    Tree fresh(type);
    addChild(fresh, -1, um);
    return fresh;
}

void Tree::addChild(Tree child, int index, UndoManager* um)
{
    if (!node_ || !child.node_ || child.node_ == node_)
        return;
    child.node_->parent = node_;
    size_t at = index < 0 || (size_t)index > node_->children.size()
                    ? node_->children.size()
                    : (size_t)index;
    node_->children.insert(node_->children.begin() + at, child.node_);

    if (um) {
        Tree self = *this;
        um->record(
            [self, child]() mutable {
                for (size_t i = 0; i < self.childCount(); ++i)
                    if (self.child(i) == child) {
                        self.removeChild(i, nullptr);
                        break;
                    }
            },
            [self, child, at]() mutable { self.addChild(child, (int)at, nullptr); });
    }

    Tree self = *this;
    node_->bubble([&](Listener& l) {
        if (l.onChildAdded)
            l.onChildAdded(self, child);
    });
}

void Tree::removeChild(size_t index, UndoManager* um)
{
    if (!node_ || index >= node_->children.size())
        return;
    Tree removed(node_->children[index]);
    node_->children.erase(node_->children.begin() + index);
    removed.node_->parent.reset();

    if (um) {
        Tree self = *this;
        um->record(
            [self, removed, index]() mutable {
                self.addChild(removed, (int)index, nullptr);
            },
            [self, index]() mutable { self.removeChild(index, nullptr); });
    }

    Tree self = *this;
    node_->bubble([&](Listener& l) {
        if (l.onChildRemoved)
            l.onChildRemoved(self, removed);
    });
}

Tree Tree::parent() const
{
    if (!node_)
        return {};
    auto p = node_->parent.lock();
    return p ? Tree(p) : Tree();
}

uint64_t Tree::addListener(Listener l)
{
    if (!node_)
        return 0;
    uint64_t id = Node::nextListenerId++;
    node_->listeners.push_back({id, std::move(l)});
    return id;
}

void Tree::removeListener(uint64_t id)
{
    if (!node_)
        return;
    auto& ls = node_->listeners;
    ls.erase(std::remove_if(ls.begin(), ls.end(),
                            [&](auto& e) { return e.id == id; }),
             ls.end());
}

// --- serialization ----------------------------------------------------------

namespace {

// values encode as strings the way ValueTree attributes do
std::string valueToString(const Value& v)
{
    if (auto* i = std::get_if<int64_t>(&v))
        return std::to_string(*i);
    if (auto* d = std::get_if<double>(&v)) {
        char buf[36];
        snprintf(buf, sizeof(buf), "%.17g", *d);
        // keep a decimal marker so the type survives the round-trip
        if (!strpbrk(buf, ".eE"))
            strcat(buf, ".0");
        return buf;
    }
    if (auto* b = std::get_if<bool>(&v))
        return *b ? "1" : "0";
    if (auto* s = std::get_if<std::string>(&v))
        return *s;
    return {};
}

Value stringToValue(const std::string& s)
{
    if (s.empty())
        return s;
    char* end = nullptr;
    long long i = strtoll(s.c_str(), &end, 10);
    if (end && *end == 0)
        return (int64_t)i;
    double d = strtod(s.c_str(), &end);
    if (end && *end == 0)
        return d;
    return s;
}

std::string xmlEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string xmlUnescape(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        if (s.compare(i, 5, "&amp;") == 0) { out += '&'; i += 4; }
        else if (s.compare(i, 4, "&lt;") == 0) { out += '<'; i += 3; }
        else if (s.compare(i, 4, "&gt;") == 0) { out += '>'; i += 3; }
        else if (s.compare(i, 6, "&quot;") == 0) { out += '"'; i += 5; }
        else out += s[i];
    }
    return out;
}

} // namespace

std::string Tree::toXml() const
{
    if (!node_)
        return {};
    std::ostringstream out;
    std::function<void(const Node&, int)> write = [&](const Node& n, int depth) {
        std::string pad((size_t)depth * 2, ' ');
        out << pad << "<" << n.type;
        for (auto& p : n.props)
            out << " " << p.first << "=\"" << xmlEscape(valueToString(p.second))
                << "\"";
        if (n.children.empty()) {
            out << "/>\n";
            return;
        }
        out << ">\n";
        for (auto& c : n.children)
            write(*c, depth + 1);
        out << pad << "</" << n.type << ">\n";
    };
    write(*node_, 0);
    return out.str();
}

// A deliberately small parser for exactly the XML toXml() writes (which is
// the ValueTree shape). Not a general XML parser.
Tree Tree::fromXml(const std::string& xml)
{
    size_t pos = 0;
    auto skipWs = [&] {
        while (pos < xml.size() && isspace((unsigned char)xml[pos]))
            ++pos;
    };

    std::function<Tree()> parseElement = [&]() -> Tree {
        skipWs();
        if (pos >= xml.size() || xml[pos] != '<')
            return {};
        ++pos;
        size_t nameStart = pos;
        while (pos < xml.size() && !isspace((unsigned char)xml[pos]) &&
               xml[pos] != '>' && xml[pos] != '/')
            ++pos;
        Tree t(xml.substr(nameStart, pos - nameStart));

        // attributes
        while (true) {
            skipWs();
            if (pos >= xml.size())
                return {};
            if (xml[pos] == '/') { // self-closing
                pos += 2; // "/>"
                return t;
            }
            if (xml[pos] == '>') {
                ++pos;
                break;
            }
            size_t keyStart = pos;
            while (pos < xml.size() && xml[pos] != '=')
                ++pos;
            std::string key = xml.substr(keyStart, pos - keyStart);
            pos += 2; // ="
            size_t valStart = pos;
            while (pos < xml.size() && xml[pos] != '"')
                ++pos;
            std::string val = xml.substr(valStart, pos - valStart);
            ++pos;
            t.set(key, stringToValue(xmlUnescape(val)), nullptr);
        }

        // children until the closing tag
        while (true) {
            skipWs();
            if (pos + 1 < xml.size() && xml[pos] == '<' && xml[pos + 1] == '/') {
                while (pos < xml.size() && xml[pos] != '>')
                    ++pos;
                ++pos;
                return t;
            }
            Tree c = parseElement();
            if (!c.valid())
                return {};
            t.addChild(c, -1, nullptr);
        }
    };

    return parseElement();
}

bool Tree::equivalent(const Tree& other) const
{
    if (!node_ || !other.node_)
        return node_ == other.node_;
    if (node_->type != other.node_->type ||
        node_->props != other.node_->props ||
        node_->children.size() != other.node_->children.size())
        return false;
    for (size_t i = 0; i < node_->children.size(); ++i)
        if (!child(i).equivalent(other.child(i)))
            return false;
    return true;
}

Tree Tree::clone() const
{
    if (!node_)
        return {};
    Tree copy(node_->type);
    copy.node_->props = node_->props;
    for (auto& c : node_->children)
        copy.addChild(Tree(c).clone(), -1, nullptr);
    return copy;
}

// --- UndoManager -------------------------------------------------------------

struct UndoManager::Impl {
    struct Action {
        std::function<void()> undo, redo;
    };
    std::vector<std::vector<Action>> undoStack;
    std::vector<std::vector<Action>> redoStack;
    bool applying = false;
};

UndoManager::UndoManager() : impl(new Impl) {}
UndoManager::~UndoManager() = default;

void UndoManager::beginTransaction()
{
    if (impl->undoStack.empty() || !impl->undoStack.back().empty())
        impl->undoStack.emplace_back();
}

void UndoManager::record(std::function<void()> undoFn, std::function<void()> redoFn)
{
    if (impl->applying)
        return;
    if (impl->undoStack.empty())
        impl->undoStack.emplace_back();
    impl->undoStack.back().push_back({std::move(undoFn), std::move(redoFn)});
    impl->redoStack.clear();
}

bool UndoManager::undo()
{
    while (!impl->undoStack.empty() && impl->undoStack.back().empty())
        impl->undoStack.pop_back();
    if (impl->undoStack.empty())
        return false;
    auto txn = std::move(impl->undoStack.back());
    impl->undoStack.pop_back();
    impl->applying = true;
    for (auto it = txn.rbegin(); it != txn.rend(); ++it)
        it->undo();
    impl->applying = false;
    impl->redoStack.push_back(std::move(txn));
    return true;
}

bool UndoManager::redo()
{
    if (impl->redoStack.empty())
        return false;
    auto txn = std::move(impl->redoStack.back());
    impl->redoStack.pop_back();
    impl->applying = true;
    for (auto& a : txn)
        a.redo();
    impl->applying = false;
    impl->undoStack.push_back(std::move(txn));
    return true;
}

bool UndoManager::canUndo() const
{
    for (auto& t : impl->undoStack)
        if (!t.empty())
            return true;
    return false;
}

bool UndoManager::canRedo() const { return !impl->redoStack.empty(); }

void UndoManager::clear()
{
    impl->undoStack.clear();
    impl->redoStack.clear();
}

} // namespace snd::state
