#include <ppl.h>
#include <ppltasks.h>
#include <agents.h>
using namespace concurrency;

// A task that completes after `timeout` ms, using a timer + call agent.
task<void> complete_after(unsigned int timeout)
{
    task_completion_event<void> tce;

    auto fire_once = new timer<int>(timeout, 0, nullptr, false); // non-repeating
    auto callback = new call<int>([tce](int) { tce.set(); });

    fire_once->link_target(callback);
    fire_once->start();

    task<void> event_set(tce);
    return event_set.then([callback, fire_once]() {
        delete callback;
        delete fire_once;
    });
}

// Races `t` against a timeout. If the timer wins, cancels `cts`
// (your task body must check the token and bail out to actually stop).
template<typename T>
task<T> cancel_after_timeout(task<T> t, cancellation_token_source cts, unsigned int timeout)
{
    task<bool> success_task = t.then([](T) { return true; });
    task<bool> failure_task = complete_after(timeout).then([] { return false; });

    return (failure_task || success_task).then([t, cts](bool success) {
        if (!success) cts.cancel();
        return t; // rethrows task_canceled when you .get() it if it timed out
    });
}