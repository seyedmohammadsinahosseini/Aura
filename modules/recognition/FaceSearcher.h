#ifndef FACE_CPP_FACESEARCHER_H
#define FACE_CPP_FACESEARCHER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace faiss {
class IndexFlatIP;
}

namespace face {

class FaceSearcher {
public:
    explicit FaceSearcher(int dimension = 512);
    ~FaceSearcher();

    void addEmbedding(int64_t personId, const std::vector<float>& emb);
    void rebuildIndex(const std::vector<std::pair<int64_t, std::vector<float>>>& all);
    int64_t search(const std::vector<float>& query, float& outDistance);
    size_t size() const;

private:
    int dimension_{512};
    std::unique_ptr<faiss::IndexFlatIP> index_{};
    std::vector<int64_t> indexToPersonId_{};
    mutable std::mutex mutex_{};
};

} // namespace face

#endif // FACE_CPP_FACESEARCHER_H
