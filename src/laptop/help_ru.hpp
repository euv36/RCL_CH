#pragma once

#include <string>
#include <vector>

namespace camera_cpp::laptop {

struct HelpSection {
    std::string id;
    std::string title;
    std::vector<std::string> lines;
};

class HelpRu {
public:
    static const std::vector<HelpSection>& sections();
    static std::string combined_text();
};

} // namespace camera_cpp::laptop
