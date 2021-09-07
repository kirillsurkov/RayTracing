#pragma once

#include <bvh/bvh.hpp>

class BVH {
public:
    struct AABB {
        float x0;
        float y0;
        float z0;
        float x1;
        float y1;
        float z1;
    };

private:
    bvh::Bvh<float>            m_bvh;
    std::vector<std::uint32_t> m_leafs;
    std::vector<std::float_t>  m_aabbMins;
    std::vector<std::float_t>  m_aabbMaxs;
    std::vector<std::uint32_t> m_childs;
    std::vector<std::uint32_t> m_primitives;

public:
    BVH();
    ~BVH();

    void build(const std::vector<float>& positions);
    const std::vector<std::uint32_t>& getLeafs()      const { return m_leafs; }
    const std::vector<std::float_t>&  getAABBMins()   const { return m_aabbMins; }
    const std::vector<std::float_t>&  getAABBMaxs()   const { return m_aabbMaxs; }
    const std::vector<std::uint32_t>& getChilds()     const { return m_childs; }
    const std::vector<std::uint32_t>& getPrimitives() const { return m_primitives; }
};
