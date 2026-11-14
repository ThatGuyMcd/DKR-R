#pragma once
#include <chrono>
#include <stdexcept>

namespace dkr::profile {
// Initial attempt plus five retries. Validation, permission and quota errors
// need user action and are never resubmitted automatically.
template <class Error, class Request, class Wait, class Current>
auto profile_request(Request request, Wait wait, Current current) {
    for (unsigned retries = 0;; ++retries) {
        if (!current())
            throw std::runtime_error("The signed-in account changed. Reopen your profile.");
        try {
            return request();
        } catch (const Error& error) {
            const bool transient =
                error.status == 0 || error.status == 408 || (error.status >= 500 && error.status <= 599);
            if (!transient || retries == 5)
                throw;
            wait(std::chrono::milliseconds(250U << retries));
        }
    }
}
} // namespace dkr::profile
