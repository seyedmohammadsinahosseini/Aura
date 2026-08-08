#include "FaceSearcher.h"

#include <faiss/IndexFlat.h>

#include <cmath>
#include <stdexcept>

namespace face {

FaceSearcher::FaceSearcher(int dimension) : dimension_(dimension), index_(std::make_unique<faiss::IndexFlatIP>(dimension)) {
}

FaceSearcher::~FaceSearcher() = default;

static std::vector<float> normalize(const std::vector<float>& v) {
    float norm = 0.0f;
    for (float x : v) norm += x * x;
    norm = std::sqrt(norm);
    if (norm < 1e-6f) return v;
    std::vector<float> out(v.size());
    for (size_t i = 0; i < v.size(); ++i) out[i] = v[i] / norm;
    return out;
}

void FaceSearcher::addEmbedding(int64_t personId, const std::vector<float>& emb) {
    if (static_cast<int>(emb.size()) != dimension_) {
        throw std::runtime_error("face searcher embedding dimension mismatch");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto normalized = normalize(emb);
    index_->add(1, normalized.data());
    indexToPersonId_.push_back(personId);
}

void FaceSearcher::rebuildIndex(const std::vector<std::pair<int64_t, std::vector<float>>>& all) {
    std::lock_guard<std::mutex> lock(mutex_);
    index_->reset();
    indexToPersonId_.clear();

    for (const auto& [personId, emb] : all) {
        if (static_cast<int>(emb.size()) != dimension_) {
            continue;
        }
        const auto normalized = normalize(emb);
        index_->add(1, normalized.data());
        indexToPersonId_.push_back(personId);
    }
}

int64_t FaceSearcher::search(const std::vector<float>& query, float& outDistance) {
    if (static_cast<int>(query.size()) != dimension_) {
        throw std::runtime_error("face searcher query dimension mismatch");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (indexToPersonId_.empty()) {
        outDistance = 1.0f;
        return -1;
    }

    const auto normalized = normalize(query);

    std::vector<faiss::idx_t> labels(1, -1);
    std::vector<float> distances(1, 0.0f);
    index_->search(1, normalized.data(), 1, distances.data(), labels.data());

    if (labels[0] < 0 || static_cast<size_t>(labels[0]) >= indexToPersonId_.size()) {
        outDistance = 1.0f;
        return -1;
    }

    // IndexFlatIP with normalized vectors returns cosine similarity in [-1, 1]
    // Convert to distance: 0 = identical, 1 = completely different
    const float similarity = distances[0];
    outDistance = 1.0f - similarity;

    return indexToPersonId_[static_cast<size_t>(labels[0])];
}

size_t FaceSearcher::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return indexToPersonId_.size();
}

}  // namespace face
