#include "engine2/ContactSolver.hpp"

#include <glm/vec3.hpp>

#include <cmath>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {
    if(!ok) throw std::runtime_error(message);
}
bool near(float x,float y,float tolerance=0.0001f) {
    return std::abs(x-y)<tolerance;
}
}

int main() {
    using namespace engine2;
    const glm::vec3 normal(1,0,0);

    // The first impact of a 3m/s sphere rebounds at 2.4m/s.
    // Iterating the SAME constraint must not cancel that bounce.
    glm::vec3 moving(3,0,0),fixed(0,0,0);
    ContactImpulseState bounce;
    ContactMaterial elastic{0.8f,0,0};
    for (int pass=0;pass<8;++pass)
        solveContactImpulse(moving,fixed,1.0f,0.0f,
                            normal,elastic,elastic,bounce);
    check(near(moving.x,-2.4f) && near(fixed.x,0) &&
          near(bounce.accumulatedNormal,5.4f),
          "Iterative normal impulses must preserve restitution");

    // Static friction can cancel the whole tangential closing velocity.
    moving={2,1,0};
    fixed={0,0,0};
    ContactImpulseState sticky;
    ContactMaterial stickyMaterial{0.0f,1.0f,0.7f};
    for (int i=0;i<8;++i)
        solveContactImpulse(moving,fixed,1,0,normal,
                            stickyMaterial,stickyMaterial,sticky);
    check(near(moving.x,0) && near(moving.y,0) &&
          near(glm::length(sticky.accumulatedTangent),1.0f),
          "Static friction should cancel small tangential slip");

    // Dynamic friction opposes large tangential motion and is bounded
    // by the Coulomb cone; it must not create energy.
    moving={2,6,0};
    fixed={0,0,0};
    ContactImpulseState sliding;
    ContactMaterial dynamic{0,0.5f,0.25f};
    for (int i=0;i<8;++i)
        solveContactImpulse(moving,fixed,1,0,normal,
                            dynamic,dynamic,sliding);
    check(near(moving.x,0) && near(moving.y,5.5f) &&
          near(glm::length(sliding.accumulatedTangent),0.5f),
          "Dynamic friction cone clamp is incorrect");

    // Preserving legacy scenes: zero friction leaves transverse velocity.
    moving={2,1,0};
    fixed={0,0,0};
    ContactImpulseState frictionless;
    for (int i=0;i<8;++i)
        solveContactImpulse(moving,fixed,1,0,normal,
                            {0,0,0},{0,0,0},frictionless);
    check(near(moving.y,1),"Default material must remain frictionless");

    // Floor normal impulse from gravity cancels horizontal motion if
    // inside the static cone; otherwise applies dynamic slowing.
    glm::vec3 slow(0.1f,-0.2f,0);
    solveFloorImpact(slow,{0,1,0.5f});
    check(near(slow.x,0) && near(slow.y,0),
          "Static floor friction did not stop a slow body");
    glm::vec3 fast(2,-0.2f,0);
    solveFloorImpact(fast,{0,1,0.5f});
    check(near(fast.x,1.9f) && near(fast.y,0),
          "Dynamic floor friction did not slow a sliding body");
    glm::vec3 rebound(2,-1,0);
    solveFloorImpact(rebound,{0.8f,0,0});
    check(near(rebound.y,0.8f) && near(rebound.x,2),
          "Floor restitution changed for legacy frictionless bodies");
    return 0;
}
