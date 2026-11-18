#include "engine2/Exposure.hpp"

#include <cmath>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b, float tolerance = 1.e-4f) {
    return std::abs(a - b) < tolerance;
}
}

int main() {
    using engine2::adaptAutoExposure;
    using engine2::targetAutoExposure;

    check(near(targetAutoExposure(std::log(0.18f), 0.18f), 1.0f),
          "Middle gray must produce neutral exposure");
    check(near(targetAutoExposure(std::log(0.09f), 0.18f), 2.0f),
          "Dark scenes must raise exposure");
    check(near(targetAutoExposure(std::log(0.72f), 0.18f), 0.25f),
          "Bright scenes must lower exposure");
    check(near(targetAutoExposure(-100.f, 0.18f), 16.0f) &&
          near(targetAutoExposure(100.f, 0.18f), 0.05f),
          "Metering must respect minimum and maximum exposure");
    check(near(adaptAutoExposure(1.0f, 4.0f, 3.0f, 0.0f), 1.0f),
          "Zero delta must freeze adaptation");
    const float half = adaptAutoExposure(1.0f, 4.0f, 3.0f, 0.5f);
    const float quarter1 = adaptAutoExposure(1.0f, 4.0f, 3.0f, 0.25f);
    const float quarter2 = adaptAutoExposure(quarter1, 4.0f, 3.0f, 0.25f);
    check(near(half, quarter2), "Adaptation must be framerate independent");
    check(half > 1.0f && half < 4.0f,
          "Exposure must approach target without overshooting");
    bool rejected = false;
    try { (void)targetAutoExposure(0.0f, -1.0f); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Invalid key value was accepted");
    return 0;
}
