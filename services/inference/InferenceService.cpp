#include "InferenceService.h"

namespace backend {
namespace services {

bool InferenceService::initialize(const std::string& modelPath) {
    (void)modelPath;
    return true;
}

bool InferenceService::infer(const std::string& input, std::string& output) {
    (void)input;
    output.clear();
    return true;
}

} // namespace services
} // namespace backend
