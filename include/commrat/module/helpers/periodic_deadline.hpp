#pragma once

#include <corerat/platform/timestamp.hpp>

namespace commrat::detail {

class PeriodicDeadline {
public:
    void reset() noexcept {
        armed_ = false;
    }

    void start(corerat::Timestamp now) noexcept {
        if (!armed_) {
            deadline_ = now;
            armed_ = true;
        }
    }

    [[nodiscard]] corerat::Timestamp advance(
        corerat::Timestamp now, corerat::Duration period) noexcept {
        deadline_ += static_cast<corerat::Timestamp>(period.count_ns());
        // Bound catch-up to one period after a long stall.
        if (now > deadline_
            && now - deadline_ > static_cast<corerat::Timestamp>(period.count_ns())) {
            deadline_ = now;
        }
        return deadline_;
    }

private:
    corerat::Timestamp deadline_{0};
    bool armed_{false};
};

} // namespace commrat::detail
