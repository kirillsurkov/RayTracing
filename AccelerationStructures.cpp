#include "AccelerationStructures.hpp"

#include <bvh/triangle.hpp>

#include <iostream>

AccelerationStructures::AccelerationStructures() {
}

AccelerationStructures::~AccelerationStructures() {
}

void AccelerationStructures::addBLAS(const std::vector<float>& mesh) {
    std::vector<bvh::Triangle<float>> triangles;
    triangles.reserve(mesh.size() / 4);

    std::uint32_t stride = 3;
    for (std::uint32_t i = 0; i < mesh.size(); i += 3 * 4 * stride) {
        triangles.push_back(bvh::Triangle<float>(
                               bvh::Vector3<float>(mesh[i + 4 * stride * 0], mesh[i + 4 * stride * 0 + 1], mesh[i + 4 * stride * 0 + 2]),
                               bvh::Vector3<float>(mesh[i + 4 * stride * 1], mesh[i + 4 * stride * 1 + 1], mesh[i + 4 * stride * 1 + 2]),
                               bvh::Vector3<float>(mesh[i + 4 * stride * 2], mesh[i + 4 * stride * 2 + 1], mesh[i + 4 * stride * 2 + 2])
                           ));
    }

    auto [bboxes, centers] = bvh::compute_bounding_boxes_and_centers(triangles.data(), triangles.size());
    auto global_bbox = bvh::compute_bounding_boxes_union(bboxes.get(), triangles.size());
    m_blasAabbs.push_back(global_bbox);
    m_blasCenters.push_back(global_bbox.center());

    m_blas.emplace_back();
    BVH& blas = m_blas.back();

    blas.builder.build(global_bbox, bboxes.get(), centers.get(), triangles.size());

    for (std::uint32_t i = 0; i < blas.bvh.node_count; i++) {
        const auto& node = blas.bvh.nodes[i];
        blas.leafs.insert(blas.leafs.end(), {node.is_leaf()});
        blas.children.insert(blas.children.end(), {node.first_child_or_primitive});
        blas.aabbs.insert(blas.aabbs.end(), {node.bounds[0], node.bounds[2], node.bounds[4], 1.0f,
                                             node.bounds[1], node.bounds[3], node.bounds[5], 1.0f});
    }

    blas.geometry.insert(blas.geometry.end(), mesh.begin(), mesh.end());

    blas.primitives.insert(blas.primitives.end(), blas.bvh.primitive_indices.get(), blas.bvh.primitive_indices.get() + triangles.size());
}

void AccelerationStructures::buildTLAS() {
    auto global_bbox = bvh::compute_bounding_boxes_union(m_blasAabbs.data(), m_blasAabbs.size());
    m_tlas.builder.build(global_bbox, m_blasAabbs.data(), m_blasCenters.data(), m_blasAabbs.size());

    for (std::uint32_t i = 0; i < m_tlas.bvh.node_count; i++) {
        const auto& node = m_tlas.bvh.nodes[i];
        m_tlas.leafs.insert(m_tlas.leafs.end(), {node.is_leaf()});
        m_tlas.children.insert(m_tlas.children.end(), {node.first_child_or_primitive});
        m_tlas.aabbs.insert(m_tlas.aabbs.end(), {node.bounds[0], node.bounds[2], node.bounds[4], 1.0f,
                                                 node.bounds[1], node.bounds[3], node.bounds[5], 1.0f});
    }

    for (const auto& bbox : m_blasAabbs) {
        m_tlas.geometry.insert(m_tlas.geometry.end(), {bbox.min[0], bbox.min[1], bbox.min[2], 1.0,
                                                       bbox.max[0], bbox.max[1], bbox.max[2], 1.0});
    }

    m_tlas.primitives.insert(m_tlas.primitives.end(), m_tlas.bvh.primitive_indices.get(), m_tlas.bvh.primitive_indices.get() + m_blasAabbs.size());

    std::uint32_t offsetNode = 0;
    std::uint32_t offsetGeometry = 0;
    for (const auto& blasBVH : m_blas) {
        m_tlasBlasNodeOffsets.push_back(offsetNode);
        offsetNode += blasBVH.bvh.node_count;

        m_tlasBlasGeometryOffsets.push_back(offsetGeometry);
        offsetGeometry += blasBVH.primitives.size();
    }
}
