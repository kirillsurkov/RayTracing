#pragma once

#include <bvh/bvh.hpp>
#include <bvh/sweep_sah_builder.hpp>

class AccelerationStructures {
public:
    struct BVH {
        bvh::Bvh<float>                       bvh;
        bvh::SweepSahBuilder<bvh::Bvh<float>> builder;
        std::vector<float>                    aabbs;
        std::vector<float>                    geometry;
        std::vector<std::uint32_t>            children;
        std::vector<std::uint32_t>            primitives;
        std::vector<std::uint32_t>            leafs;

        BVH() : builder(bvh) {
            builder.max_leaf_size = 1;
        }
    };

private:
    BVH                                  m_tlas;
    std::vector<std::uint32_t>           m_tlasBlasNodeOffsets;
    std::vector<std::uint32_t>           m_tlasBlasGeometryOffsets;
    std::vector<BVH>                     m_blas;
    std::vector<bvh::BoundingBox<float>> m_blasAabbs;
    std::vector<bvh::Vector3<float>>     m_blasCenters;

public:
    AccelerationStructures();
    ~AccelerationStructures();

    void addBLAS(const std::vector<float>& triangles);
    void buildTLAS();

    const BVH& getTLAS()              const { return m_tlas; }
    const std::vector<BVH>& getBLAS() const { return m_blas; }

    const std::vector<std::uint32_t>& getTlasBlasNodeOffsets() const { return m_tlasBlasNodeOffsets; }
    const std::vector<std::uint32_t>& getTlasBlasGeometryOffsets() const { return m_tlasBlasGeometryOffsets; }
};
