#include "vision/distance.hpp"

#include <cmath>

#include "../test_checks.hpp"

using camera_cpp::DistanceEstimator;

int main() {
    DistanceEstimator estimator;
    estimator.reset();

    auto reliable = estimator.update(70.0F, 640.0F, 45.0F, 0.0);
    CAMERA_TEST_CHECK(reliable.reliable);
    CAMERA_TEST_CHECK(std::isfinite(reliable.cm));

    estimator.reset();
    auto far = estimator.update(60.0F, 640.0F, 45.0F, 0.0);
    estimator.reset();
    auto near = estimator.update(70.0F, 640.0F, 45.0F, 0.0);
    CAMERA_TEST_CHECK(far.cm > near.cm);

    estimator.reset();
    auto first = estimator.update(70.0F, 640.0F, 45.0F, 0.0);
    auto held = estimator.update(30.0F, 640.0F, 45.0F, 0.5);
    CAMERA_TEST_CHECK(!held.reliable);
    CAMERA_TEST_CHECK(std::abs(held.cm - first.cm) < 0.001F);

    auto expired = estimator.update(30.0F, 640.0F, 45.0F, 2.1);
    CAMERA_TEST_CHECK(!expired.reliable);
    CAMERA_TEST_CHECK(expired.cm == 0.0F);

    estimator.reset();
    auto recovered = estimator.update(80.0F, 640.0F, 45.0F, 1.0);
    CAMERA_TEST_CHECK(recovered.reliable);

    auto invalid_gate = estimator.update(70.0F, 640.0F, 0.0F, 0.0);
    CAMERA_TEST_CHECK(!invalid_gate.reliable);
    CAMERA_TEST_CHECK(invalid_gate.cm == 0.0F);

    auto invalid_frame = estimator.update(70.0F, NAN, 45.0F, 0.0);
    CAMERA_TEST_CHECK(!invalid_frame.reliable);
    CAMERA_TEST_CHECK(invalid_frame.cm == 0.0F);

    estimator.reset();
    auto clamped = estimator.update(1.0F, 640.0F, 45.0F, 0.0);
    CAMERA_TEST_CHECK(!clamped.reliable);
    CAMERA_TEST_CHECK(clamped.cm == 500.0F);

    return 0;
}
