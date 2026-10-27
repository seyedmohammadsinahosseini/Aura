#ifndef BACKEND_SERVICES_INFERENCE_INFERENCESERVICE_H
#define BACKEND_SERVICES_INFERENCE_INFERENCESERVICE_H

#include <string>

namespace backend {
namespace services {

class InferenceService {
public:
    bool initialize(const std::string& modelPath);
    bool infer(const std::string& input, std::string& output);
};

} // namespace services
} // namespace backend

#endif // BACKEND_SERVICES_INFERENCE_INFERENCESERVICE_H
