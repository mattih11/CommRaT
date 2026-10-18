#include <commrat/module/helpers/periodic_deadline.hpp>

#include <cassert>

int main() {
    commrat::detail::PeriodicDeadline deadline;
    constexpr auto period = corerat::Milliseconds(10);
    constexpr corerat::Timestamp epoch = 1'000'000'000;
    deadline.start(epoch);
    assert(deadline.advance(epoch + 1'000'000, period) == epoch + 10'000'000);

    for (unsigned tick = 1; tick < 10000; ++tick) {
        const auto wake = epoch + tick * 10'000'000ULL + 300'000;
        deadline.start(wake);
        assert(deadline.advance(wake + 1'000'000, period)
            == epoch + (tick + 1) * 10'000'000ULL);
    }

    const auto delayed = epoch + 100'016'000'000ULL;
    assert(deadline.advance(delayed, period) == epoch + 100'010'000'000ULL);
    assert(deadline.advance(delayed + 1'000'000, period)
        == epoch + 100'020'000'000ULL);

    constexpr auto overrun = epoch + 101'000'000'000ULL;
    assert(deadline.advance(overrun, period) == overrun);
    assert(deadline.advance(overrun + 1'000'000, period)
        == overrun + 10'000'000);

    deadline.reset();
    deadline.start(overrun + 1'000'000'000);
    assert(deadline.advance(overrun + 1'001'000'000, period)
        == overrun + 1'010'000'000);
}
