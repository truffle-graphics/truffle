#include "truffle/gui/dock/layout.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <set>
#include <span>
#include <utility>

namespace truffle::gui::dock {
namespace {

[[nodiscard]] base::Status invalid(std::string message) {
    return base::Status::failure(base::StatusCode::invalid_argument, std::move(message));
}

bool path_to(const DockNode& node, std::string_view panel, std::vector<std::size_t>& path) {
    if (node.kind == DockNode::Kind::tabs) {
        return std::ranges::find(node.panels, panel) != node.panels.end();
    }
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        path.push_back(i);
        if (path_to(node.children[i], panel, path)) { return true; }
        path.pop_back();
    }
    return false;
}

DockNode& at(DockNode& root, std::span<const std::size_t> path) {
    DockNode* node = &root;
    for (const auto index : path) { node = &node->children[index]; }
    return *node;
}

void renormalize(std::vector<float>& fractions) {
    float sum = 0;
    for (const float f : fractions) { sum += f; }
    if (sum > 0) {
        for (float& f : fractions) { f /= sum; }
    }
}

// Collapses single-child splits and merges a split into a parent of the same
// axis, so equal layouts have one shape.
void normalize(DockNode& node) {
    if (node.kind != DockNode::Kind::split) { return; }
    std::vector<DockNode> children;
    std::vector<float> fractions;
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        DockNode& child = node.children[i];
        normalize(child);
        if (child.kind == DockNode::Kind::split && child.axis == node.axis) {
            for (std::size_t j = 0; j < child.children.size(); ++j) {
                children.push_back(std::move(child.children[j]));
                fractions.push_back(node.fractions[i] * child.fractions[j]);
            }
        } else {
            children.push_back(std::move(child));
            fractions.push_back(node.fractions[i]);
        }
    }
    if (children.size() == 1) {
        DockNode only = std::move(children.front());
        node = std::move(only);
        return;
    }
    renormalize(fractions);
    node.children = std::move(children);
    node.fractions = std::move(fractions);
}

base::Status validate_node(const DockNode& node, bool root, std::set<std::string, std::less<>>& seen,
                           int depth) {
    if (depth > 64) { return invalid("GUI dock layout is nested too deeply"); }
    if (node.kind == DockNode::Kind::tabs) {
        if (node.panels.empty()) {
            return root ? base::Status::success() : invalid("GUI dock tabs need a panel");
        }
        if (node.active >= node.panels.size()) {
            return invalid("GUI dock active tab is out of range");
        }
        for (const auto& panel : node.panels) {
            if (panel.empty()) { return invalid("GUI dock panel ids must not be empty"); }
            if (!seen.insert(panel).second) {
                return invalid("GUI dock panel '" + panel + "' appears twice");
            }
        }
        return base::Status::success();
    }
    if (node.children.size() < 2 || node.fractions.size() != node.children.size()) {
        return invalid("GUI dock splits need two or more children with one share each");
    }
    float sum = 0;
    for (const float f : node.fractions) {
        if (!std::isfinite(f) || !(f > 0)) { return invalid("GUI dock split shares must be positive"); }
        sum += f;
    }
    if (std::abs(sum - 1.0F) > 1e-3F) { return invalid("GUI dock split shares must sum to 1"); }
    for (const auto& child : node.children) {
        if (auto status = validate_node(child, false, seen, depth + 1); !status.ok()) { return status; }
    }
    return base::Status::success();
}

void collect(const DockNode& node, std::vector<std::string>& out) {
    if (node.kind == DockNode::Kind::tabs) {
        out.insert(out.end(), node.panels.begin(), node.panels.end());
        return;
    }
    for (const auto& child : node.children) { collect(child, out); }
}

void quote(std::string& out, std::string_view text) {
    out += '"';
    for (const char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; }
        out += c;
    }
    out += '"';
}

void write(std::string& out, const DockNode& node) {
    if (node.kind == DockNode::Kind::tabs) {
        out += "(tabs " + std::to_string(node.active);
        for (const auto& panel : node.panels) {
            out += ' ';
            quote(out, panel);
        }
        out += ')';
        return;
    }
    out += node.axis == Axis::horizontal ? "(split h" : "(split v";
    for (const float f : node.fractions) {
        out += ' ' + std::to_string(static_cast<long>(std::lround(f * 10000.0F)));
    }
    for (const auto& child : node.children) {
        out += ' ';
        write(out, child);
    }
    out += ')';
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    [[nodiscard]] bool at_end() {
        skip();
        return pos_ == text_.size();
    }
    [[nodiscard]] char peek() {
        skip();
        return pos_ < text_.size() ? text_[pos_] : '\0';
    }
    [[nodiscard]] bool consume(char c) {
        if (peek() != c) { return false; }
        ++pos_;
        return true;
    }
    [[nodiscard]] std::string_view word() {
        skip();
        const auto start = pos_;
        while (pos_ < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[pos_])) ||
                                       text_[pos_] == '-')) {
            ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }
    [[nodiscard]] bool number(std::uint32_t& value) {
        const auto digits = word();
        const auto* end = digits.data() + digits.size();
        const auto [ptr, error] = std::from_chars(digits.data(), end, value);
        return !digits.empty() && error == std::errc{} && ptr == end;
    }
    [[nodiscard]] bool string(std::string& value) {
        if (!consume('"')) { return false; }
        value.clear();
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') { return true; }
            if (c == '\\') {
                if (pos_ == text_.size()) { return false; }
                c = text_[pos_++];
            }
            value += c;
        }
        return false;
    }

    base::Result<DockNode> node(int depth = 0) {
        if (depth > 64) { return invalid("GUI dock layout is nested too deeply"); }
        if (!consume('(')) { return invalid("GUI dock layout expected '('"); }
        DockNode node;
        const auto kind = word();
        if (kind == "tabs") {
            std::uint32_t active = 0;
            if (!number(active)) { return invalid("GUI dock tabs need an active index"); }
            node.active = active;
            std::string panel;
            while (peek() == '"') {
                if (!string(panel)) { return invalid("GUI dock layout has an unterminated string"); }
                node.panels.push_back(panel);
            }
        } else if (kind == "split") {
            node.kind = DockNode::Kind::split;
            const auto axis = word();
            if (axis != "h" && axis != "v") { return invalid("GUI dock split axis must be h or v"); }
            node.axis = axis == "h" ? Axis::horizontal : Axis::vertical;
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                std::uint32_t share = 0;
                if (!number(share)) { return invalid("GUI dock split share is not a number"); }
                node.fractions.push_back(static_cast<float>(share));
            }
            while (peek() == '(') {
                auto child = this->node(depth + 1);
                if (!child.ok()) { return child.status(); }
                node.children.push_back(std::move(child).value());
            }
            renormalize(node.fractions);
        } else {
            return invalid("GUI dock layout expected tabs or split");
        }
        if (!consume(')')) { return invalid("GUI dock layout expected ')'"); }
        return node;
    }

private:
    void skip() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) { ++pos_; }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

DockNode DockNode::tabs(std::vector<std::string> panels, std::size_t active) {
    DockNode node;
    node.panels = std::move(panels);
    node.active = active;
    return node;
}

DockNode DockNode::split(Axis axis, std::vector<DockNode> children, std::vector<float> fractions) {
    DockNode node;
    node.kind = Kind::split;
    node.axis = axis;
    if (fractions.empty() && !children.empty()) {
        fractions.assign(children.size(), 1.0F / static_cast<float>(children.size()));
    }
    node.fractions = std::move(fractions);
    node.children = std::move(children);
    return node;
}

base::Status validate(const DockNode& layout) {
    std::set<std::string, std::less<>> seen;
    return validate_node(layout, true, seen, 0);
}

bool contains(const DockNode& layout, std::string_view panel) {
    std::vector<std::size_t> path;
    return path_to(layout, panel, path);
}

std::vector<std::string> panel_ids(const DockNode& layout) {
    std::vector<std::string> out;
    collect(layout, out);
    return out;
}

base::Status insert_panel(DockNode& layout, std::string_view panel, std::string_view anchor,
                          DropSide side) {
    if (panel.empty()) { return invalid("GUI dock panel ids must not be empty"); }
    if (contains(layout, panel)) { return invalid("GUI dock panel is already docked"); }
    if (layout.empty()) {
        layout = DockNode::tabs({std::string(panel)});
        return base::Status::success();
    }
    std::vector<std::size_t> path;
    if (!path_to(layout, anchor, path)) { return invalid("GUI dock anchor panel is not docked"); }
    DockNode& target = at(layout, path);
    if (side == DropSide::center) {
        target.panels.emplace_back(panel);
        target.active = target.panels.size() - 1;
        return base::Status::success();
    }
    const Axis axis = side == DropSide::left || side == DropSide::right ? Axis::horizontal
                                                                         : Axis::vertical;
    const bool before = side == DropSide::left || side == DropSide::top;
    DockNode added = DockNode::tabs({std::string(panel)});
    if (!path.empty()) {
        DockNode& parent = at(layout, std::span(path).first(path.size() - 1));
        if (parent.axis == axis) {
            const auto index = path.back();
            const float half = parent.fractions[index] / 2;
            parent.fractions[index] = half;
            const auto position = static_cast<std::ptrdiff_t>(before ? index : index + 1);
            parent.children.insert(parent.children.begin() + position, std::move(added));
            parent.fractions.insert(parent.fractions.begin() + position, half);
            return base::Status::success();
        }
    }
    DockNode old = std::move(target);
    std::vector<DockNode> children;
    children.push_back(before ? std::move(added) : std::move(old));
    children.push_back(before ? std::move(old) : std::move(added));
    target = DockNode::split(axis, std::move(children));
    return base::Status::success();
}

base::Status remove_panel(DockNode& layout, std::string_view panel) {
    std::vector<std::size_t> path;
    if (!path_to(layout, panel, path)) { return invalid("GUI dock panel is not docked"); }
    DockNode& tabs = at(layout, path);
    const auto index = static_cast<std::size_t>(std::ranges::find(tabs.panels, panel) -
                                                tabs.panels.begin());
    tabs.panels.erase(tabs.panels.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < tabs.active || tabs.active >= tabs.panels.size()) {
        tabs.active = tabs.active > 0 ? tabs.active - 1 : 0;
    }
    if (tabs.panels.empty() && !path.empty()) {
        DockNode& parent = at(layout, std::span(path).first(path.size() - 1));
        const auto child = static_cast<std::ptrdiff_t>(path.back());
        parent.children.erase(parent.children.begin() + child);
        parent.fractions.erase(parent.fractions.begin() + child);
        renormalize(parent.fractions);
    }
    normalize(layout);
    return base::Status::success();
}

base::Status move_panel(DockNode& layout, std::string_view panel, std::string_view anchor,
                        DropSide side) {
    if (panel == anchor) { return invalid("GUI dock panel cannot be docked beside itself"); }
    if (!contains(layout, panel)) { return invalid("GUI dock panel is not docked"); }
    if (!contains(layout, anchor)) { return invalid("GUI dock anchor panel is not docked"); }
    DockNode next = layout;
    if (auto status = remove_panel(next, panel); !status.ok()) { return status; }
    if (auto status = insert_panel(next, panel, anchor, side); !status.ok()) { return status; }
    normalize(next);
    layout = std::move(next);
    return base::Status::success();
}

base::Status activate_panel(DockNode& layout, std::string_view panel) {
    std::vector<std::size_t> path;
    if (!path_to(layout, panel, path)) { return invalid("GUI dock panel is not docked"); }
    DockNode& tabs = at(layout, path);
    tabs.active = static_cast<std::size_t>(std::ranges::find(tabs.panels, panel) -
                                           tabs.panels.begin());
    return base::Status::success();
}

std::string serialize(const DockNode& layout) {
    std::string out;
    write(out, layout);
    return out;
}

base::Result<DockNode> parse(std::string_view text) {
    Parser parser(text);
    auto node = parser.node();
    if (!node.ok()) { return node.status(); }
    if (!parser.at_end()) { return invalid("GUI dock layout has trailing text"); }
    if (auto status = validate(node.value()); !status.ok()) { return status; }
    return node;
}

base::Status DockPresets::set(std::string name, DockNode layout) {
    if (name.empty()) { return invalid("GUI dock preset names must not be empty"); }
    if (auto status = validate(layout); !status.ok()) { return status; }
    presets_.insert_or_assign(std::move(name), std::move(layout));
    return base::Status::success();
}

const DockNode* DockPresets::find(std::string_view name) const noexcept {
    const auto found = presets_.find(name);
    return found == presets_.end() ? nullptr : &found->second;
}

bool DockPresets::erase(std::string_view name) {
    const auto found = presets_.find(name);
    if (found == presets_.end()) { return false; }
    presets_.erase(found);
    return true;
}

std::vector<std::string> DockPresets::names() const {
    std::vector<std::string> out;
    for (const auto& entry : presets_) { out.push_back(entry.first); }
    return out;
}

std::string DockPresets::serialize() const {
    std::string out = "dock-presets 1\n";
    for (const auto& [name, layout] : presets_) {
        out += "preset ";
        quote(out, name);
        out += ' ';
        write(out, layout);
        out += '\n';
    }
    return out;
}

base::Result<DockPresets> DockPresets::parse(std::string_view text) {
    Parser parser(text);
    std::uint32_t version = 0;
    if (parser.word() != "dock-presets" || !parser.number(version) || version != 1) {
        return invalid("GUI dock presets must start with 'dock-presets 1'");
    }
    DockPresets presets;
    std::string name;
    while (!parser.at_end()) {
        if (parser.word() != "preset" || !parser.string(name)) {
            return invalid("GUI dock presets expected 'preset \"name\"'");
        }
        auto layout = parser.node();
        if (!layout.ok()) { return layout.status(); }
        if (presets.find(name) != nullptr) { return invalid("GUI dock preset '" + name + "' appears twice"); }
        if (auto status = presets.set(name, std::move(layout).value()); !status.ok()) { return status; }
    }
    return presets;
}

} // namespace truffle::gui::dock
