#include "model.hpp"

pxa::Result<void> Model::add(int amount) {
    if (amount <= 0 || count.get() > INT32_MAX - amount)
        return std::unexpected(pxa::Error::invalid_argument);
    count.set(count.get() + amount);
    return {};
}
