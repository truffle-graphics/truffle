#include "truffle/gui/context.hpp"

#include <algorithm>
#include <utility>

namespace truffle::gui {
namespace {

[[nodiscard]] base::Status missing_updater() {
    return base::Status::failure(base::StatusCode::invalid_argument,
                                 "GUI element has no updater");
}

} // namespace

Context::UpdaterEntry* Context::find_updater(ElementId element) noexcept {
    const auto found = std::ranges::find(updaters_, element, &UpdaterEntry::element);
    return found == updaters_.end() ? nullptr : &*found;
}

bool Context::updates_due() const noexcept {
    return std::ranges::any_of(updaters_, [](const UpdaterEntry& entry) {
        return entry.live || entry.pending;
    });
}

base::Status Context::set_updater(ElementId element, Updater update, bool live) {
    if (find(element) == nullptr) {
        return base::Status::failure(base::StatusCode::invalid_argument,
                                     "GUI element handle is not alive");
    }
    if (!update) {
        std::erase_if(updaters_, [element](const UpdaterEntry& entry) {
            return entry.element == element;
        });
        return base::Status::success();
    }
    if (UpdaterEntry* entry = find_updater(element)) {
        entry->update = std::move(update);
        entry->live = live;
        entry->pending = true;
    } else {
        updaters_.push_back({element, std::move(update), live, true, std::nullopt});
    }
    return base::Status::success();
}

base::Status Context::request_update(ElementId element) {
    UpdaterEntry* entry = find_updater(element);
    if (entry == nullptr) {
        return missing_updater();
    }
    entry->pending = true;
    return base::Status::success();
}

base::Status Context::set_update_live(ElementId element, bool live) {
    UpdaterEntry* entry = find_updater(element);
    if (entry == nullptr) {
        return missing_updater();
    }
    entry->live = live;
    return base::Status::success();
}

base::Status Context::request_update_at(ElementId element,
                                        std::optional<Clock::time_point> when) {
    UpdaterEntry* entry = find_updater(element);
    if (entry == nullptr) { return missing_updater(); }
    entry->deadline = when;
    return base::Status::success();
}

base::Status Context::run_updaters(std::size_t& count) {
    if (updaters_.empty()) {
        return base::Status::success();
    }
    // Updaters may add, remove or request updaters; each due one runs once,
    // and requests made while running stay pending for the next frame.
    std::vector<ElementId> due;
    for (const auto& entry : updaters_) {
        if (entry.live || entry.pending || (entry.deadline && *entry.deadline <= time_)) {
            due.push_back(entry.element);
        }
    }
    for (const ElementId element : due) {
        UpdaterEntry* entry = find_updater(element);
        if (entry == nullptr) {
            continue;
        }
        entry->pending = false;
        if (entry->deadline && *entry->deadline <= time_) { entry->deadline.reset(); }
        const Updater update = entry->update; // keeps captured state alive
        auto status = run_element_update(element, update);
        ++count;
        if (!status.ok()) {
            if (UpdaterEntry* failed = find_updater(element)) {
                failed->pending = true;
            }
            return status;
        }
    }
    return base::Status::success();
}

} // namespace truffle::gui
