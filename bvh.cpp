#include "bvh.hpp"

#include <bvh/triangle.hpp>
#include <bvh/sweep_sah_builder.hpp>

#include <iostream>

BVH::BVH() {
}

BVH::~BVH() {
}

void BVH::build(const std::vector<float>& positions) {
    std::vector<bvh::Triangle<float>> triangles;
    triangles.reserve(positions.size() / 4);

    for (std::uint32_t i = 0; i < positions.size(); i += 12) {
        triangles.push_back(bvh::Triangle<float>(
                               bvh::Vector3<float>(positions[i + 0], positions[i + 1], positions[i + 2]),
                               bvh::Vector3<float>(positions[i + 4], positions[i + 5], positions[i + 6]),
                               bvh::Vector3<float>(positions[i + 8], positions[i + 9], positions[i + 10])
                           ));
    }

    bvh::SweepSahBuilder<bvh::Bvh<float>> builder(m_bvh);
    builder.max_leaf_size = 1;

    auto [bboxes, centers] = bvh::compute_bounding_boxes_and_centers(triangles.data(), triangles.size());
    auto global_bbox = bvh::compute_bounding_boxes_union(bboxes.get(), triangles.size());
    builder.build(global_bbox, bboxes.get(), centers.get(), triangles.size());

    for (std::uint32_t i = 0; i < m_bvh.node_count; i++) {
        const auto& node = m_bvh.nodes[i];
        m_leafs.insert(m_leafs.end(), {node.is_leaf()});
        m_childs.insert(m_childs.end(), {node.first_child_or_primitive});
        m_aabbMins.insert(m_aabbMins.end(), {node.bounds[0], node.bounds[2], node.bounds[4], 1.0f});
        m_aabbMaxs.insert(m_aabbMaxs.end(), {node.bounds[1], node.bounds[3], node.bounds[5], 1.0f});
    }

    m_primitives.resize(triangles.size());
    std::copy(m_bvh.primitive_indices.get(), m_bvh.primitive_indices.get() + triangles.size(), m_primitives.begin());
}
