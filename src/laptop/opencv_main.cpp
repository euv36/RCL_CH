#include "laptop/opencv_app.hpp"

#include <string>

int main(int argc, char** argv) {
    camera_cpp::laptop::OpenCVApp app;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] != nullptr ? argv[i] : "";
        if (arg == "--source" && i + 1 < argc && argv[i + 1] != nullptr) {
            app.state().camera_source_text = argv[++i];
        } else if (arg.rfind("--source=", 0) == 0) {
            app.state().camera_source_text = arg.substr(9);
        }
    }
    return app.run();
}