#include "fabric/core/version.hpp"

int main() {
    return fabric::core::version().empty() ? 1 : 0;
}
