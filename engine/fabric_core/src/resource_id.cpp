#include "fabric/core/resource_id.hpp"

namespace fabric::core {

bool ResourceId::is_valid(const std::string_view candidate) noexcept {
    if (candidate.empty() || candidate.size() > 128) {
        return false;
    }

    const auto is_ascii_alphanumeric = [](const char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9');
    };

    if (!is_ascii_alphanumeric(candidate.front()) ||
        !is_ascii_alphanumeric(candidate.back())) {
        return false;
    }

    for (const char character : candidate) {
        if (!is_ascii_alphanumeric(character) && character != '-' &&
            character != '_' && character != '.') {
            return false;
        }
    }
    return true;
}

} // namespace fabric::core
