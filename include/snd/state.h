// snd::state -- a small tree of named nodes with typed properties, listeners,
// undo, and XML (de)serialization. The ValueTree-shaped state model used by
// bob/Murk compatibility work: the XML shape matches JUCE's ValueTree
// writeToXml output (type = element name, properties = attributes, children =
// child elements) so historical Murk state files can be read or converted when
// needed.
//
// Main-thread only, like ValueTree. Tree is a lightweight reference; copies
// share the same node.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace snd::state {

using Value = std::variant<std::monostate, int64_t, double, bool, std::string>;

class UndoManager;

class Tree {
public:
    Tree() = default; // invalid tree
    explicit Tree(std::string type);

    bool valid() const { return node_ != nullptr; }
    const std::string& type() const;

    // --- properties ---------------------------------------------------------
    bool hasProperty(const std::string& key) const;
    Value get(const std::string& key) const;
    int64_t getInt(const std::string& key, int64_t fallback = 0) const;
    double getDouble(const std::string& key, double fallback = 0.0) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    std::string getString(const std::string& key,
                          const std::string& fallback = {}) const;

    // set with um != nullptr to make the change undoable
    void set(const std::string& key, Value v, UndoManager* um = nullptr);
    void removeProperty(const std::string& key, UndoManager* um = nullptr);
    std::vector<std::string> propertyNames() const;

    // --- children -----------------------------------------------------------
    size_t childCount() const;
    Tree child(size_t index) const;
    Tree childWithType(const std::string& type) const; // first match, or invalid
    Tree getOrCreateChild(const std::string& type, UndoManager* um = nullptr);
    void addChild(Tree child, int index = -1, UndoManager* um = nullptr);
    void removeChild(size_t index, UndoManager* um = nullptr);
    Tree parent() const;

    // --- listeners ----------------------------------------------------------
    // Fire for changes to this node or anything below it. The returned id
    // unregisters via removeListener.
    struct Listener {
        std::function<void(Tree node, const std::string& key)> onProperty;
        std::function<void(Tree parent, Tree child)> onChildAdded;
        std::function<void(Tree parent, Tree child)> onChildRemoved;
    };
    uint64_t addListener(Listener l);
    void removeListener(uint64_t id);

    // --- serialization ------------------------------------------------------
    std::string toXml() const;                     // ValueTree-shaped XML
    static Tree fromXml(const std::string& xml);   // invalid tree on parse error

    // deep comparison / deep copy
    bool equivalent(const Tree& other) const;
    Tree clone() const;

    bool operator==(const Tree& o) const { return node_ == o.node_; }

private:
    friend class UndoManager;
    struct Node;
    explicit Tree(std::shared_ptr<Node> n) : node_(std::move(n)) {}
    std::shared_ptr<Node> node_;
};

// Undo/redo over tree edits. Group edits with beginTransaction(); every
// set/add/remove that passed this manager becomes one undoable step.
class UndoManager {
public:
    UndoManager();
    ~UndoManager();

    void beginTransaction();
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    void clear();

    // used by Tree
    void record(std::function<void()> undoFn, std::function<void()> redoFn);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace snd::state
