#version 430

#define NULL uint(-1)
#define swap(a, b) (a ^= b, b ^= a, a ^= b)

uniform uint u_raysCount;

layout(local_size_x = 64) in;

layout(std430, binding = 13) writeonly buffer IntersectionResult        { vec4 intersectionResult[];        };
layout(std430, binding = 14) readonly  buffer RayBuffer                 { vec4 rayBuffer[];                 };
layout(std430, binding = 1)  readonly  buffer TlasGetAABB               { vec4 tlasGetAABB[];               };
layout(std430, binding = 2)  readonly  buffer TlasGetGeometry           { vec4 tlasGetGeometry[];           };
layout(std430, binding = 3)  readonly  buffer TlasGetChild              { uint tlasGetChild[];              };
layout(std430, binding = 4)  readonly  buffer TlasGetPrimitiveId        { uint tlasGetPrimitiveId[];        };
layout(std430, binding = 5)  readonly  buffer TlasIsLeaf                { uint tlasIsLeaf[];                };
layout(std430, binding = 6)  readonly  buffer TlasGetBlasNodeOffset     { uint tlasGetBlasNodeOffset[];     };
layout(std430, binding = 7)  readonly  buffer TlasGetBlasGeometryOffset { uint tlasGetBlasGeometryOffset[]; };
layout(std430, binding = 8)  readonly  buffer BlasGetAABB               { vec4 blasGetAABB[];               };
layout(std430, binding = 9)  readonly  buffer BlasGetGeometry           { vec4 blasGetGeometry[];           };
layout(std430, binding = 10) readonly  buffer BlasGetChild              { uint blasGetChild[];              };
layout(std430, binding = 11) readonly  buffer BlasGetPrimitiveId        { uint blasGetPrimitiveId[];        };
layout(std430, binding = 12) readonly  buffer BlasIsLeaf                { uint blasIsLeaf[];                };

#define DECLARE_BVH_TRAVERSAL(NAME, GET_CHILD, GET_AABB, IS_LEAF, INTERSECT_FUNCTION, OUT_CHILD) \
    void NAME(in Ray ray, uint skipId, uint nodeOffset, uint geometryOffset, inout Intersection isec) { \
        uint stack[32]; \
        uint stackIt = 0; \
        stack[stackIt++] = NULL; \
        \
        uint leftChild = GET_CHILD[nodeOffset]; \
        while (leftChild != NULL) { \
            uint rightChild = leftChild + 1; \
            \
            vec4 bbMinLeft  = GET_AABB[(nodeOffset + leftChild) * 2 + 0]; \
            vec4 bbMaxLeft  = GET_AABB[(nodeOffset + leftChild) * 2 + 1]; \
            vec4 bbMinRight = GET_AABB[(nodeOffset + rightChild) * 2 + 0]; \
            vec4 bbMaxRight = GET_AABB[(nodeOffset + rightChild) * 2 + 1]; \
            \
            vec2 distLeft  = aabbIntersect(ray, bbMinLeft.xyz, bbMaxLeft.xyz); \
            vec2 distRight = aabbIntersect(ray, bbMinRight.xyz, bbMaxRight.xyz); \
            \
            if (distLeft.x <= distLeft.y) { \
                if (IS_LEAF[nodeOffset + leftChild] > 0) { \
                    Intersection newIsec = isec; \
                    INTERSECT_FUNCTION(ray, geometryOffset, GET_CHILD[nodeOffset + leftChild], newIsec); \
                    if (newIsec.dist >= 0 && newIsec.dist < isec.dist) { \
                        isec = newIsec; \
                        isec.OUT_CHILD = leftChild; \
                    } \
                    leftChild = NULL; \
                } \
            } else leftChild = NULL; \
            \
            if (distRight.x <= distRight.y) { \
                if (IS_LEAF[nodeOffset + rightChild] > 0) { \
                    Intersection newIsec = isec; \
                    INTERSECT_FUNCTION(ray, geometryOffset, GET_CHILD[nodeOffset + rightChild], newIsec); \
                    if (newIsec.dist >= 0 && newIsec.dist < isec.dist) { \
                        isec = newIsec; \
                        isec.OUT_CHILD = rightChild; \
                    } \
                    rightChild = NULL; \
                } \
            } else rightChild = NULL; \
            \
            if (leftChild != NULL) { \
                if (rightChild != NULL) { \
                    if (distLeft.x > distRight.x) swap(leftChild, rightChild); \
                    if (stackIt == 32) break; \
                    stack[stackIt++] = GET_CHILD[nodeOffset + rightChild]; \
                } \
                leftChild = GET_CHILD[nodeOffset + leftChild]; \
            } else if (rightChild != NULL) { \
                leftChild = GET_CHILD[nodeOffset + rightChild]; \
            } else { \
                leftChild = stack[--stackIt]; \
            } \
        } \
    }

struct Ray {
    vec3 origin;
    vec3 dir;
    vec3 invDir;
};

struct Intersection {
    uint tlasNodeId;
    uint blasNodeId;
    vec2 barycentric;
    float dist;
};

vec3 safeInvDir(vec3 d) {
    float dirx = d.x;
    float diry = d.y;
    float dirz = d.z;
    float ooeps = 1e-5;
    vec3 invdir;
    invdir.x = 1.0 / (abs(dirx) > ooeps ? dirx : (dirx < 0 ? -ooeps : ooeps));
    invdir.y = 1.0 / (abs(diry) > ooeps ? diry : (diry < 0 ? -ooeps : ooeps));
    invdir.z = 1.0 / (abs(dirz) > ooeps ? dirz : (dirz < 0 ? -ooeps : ooeps));
    return invdir;
}

void triIntersect(in Ray ray, in vec3 v0, in vec3 v1, in vec3 v2, inout Intersection isec) {
    vec3 v1v0 = v1 - v0;
    vec3 v2v0 = v2 - v0;
    vec3 rov0 = ray.origin - v0;

    vec3  n = cross(v1v0, v2v0);
    vec3  q = cross(rov0, ray.dir);
    float d = 1.0 / dot(ray.dir, n);
    float u = d*dot(-q, v2v0);
    float v = d*dot( q, v1v0);
    float t = d*dot(-n, rov0);

    if (u < 0.0 || v < 0.0 || (u + v) > 1.0) t = -1.0;

    isec.dist = t;
    isec.barycentric = vec2(u, v);
}

vec2 aabbIntersect(in Ray ray, vec3 boxMin, vec3 boxMax) {
    vec3 tMin = (boxMin - ray.origin) * ray.invDir;
    vec3 tMax = (boxMax - ray.origin) * ray.invDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);
    return vec2(tNear, tFar);
};

void intersectBLASLeaf(in Ray ray, uint geometryOffset, uint leafChild, inout Intersection isec) {
    uint index = blasGetPrimitiveId[geometryOffset + leafChild];
    uint stride = 3;
    vec3 p1 = blasGetGeometry[((geometryOffset + index) * 3 + 0) * stride + 0].xyz;
    vec3 p2 = blasGetGeometry[((geometryOffset + index) * 3 + 1) * stride + 0].xyz;
    vec3 p3 = blasGetGeometry[((geometryOffset + index) * 3 + 2) * stride + 0].xyz;
    triIntersect(ray, p1, p2, p3, isec);
}
DECLARE_BVH_TRAVERSAL(intersectBLAS, blasGetChild, blasGetAABB, blasIsLeaf, intersectBLASLeaf, blasNodeId)

void intersectTLASLeaf(in Ray ray, uint geometryOffset, uint leafChild, inout Intersection isec) {
    uint index = tlasGetPrimitiveId[leafChild];
    vec3 aabbMin = tlasGetGeometry[index * 2 + 0].xyz;
    vec3 aabbMax = tlasGetGeometry[index * 2 + 1].xyz;
    vec2 isecAABB = aabbIntersect(ray, aabbMin, aabbMax);
    if (isecAABB.x <= isecAABB.y) {
        intersectBLAS(ray, NULL, tlasGetBlasNodeOffset[index], tlasGetBlasGeometryOffset[index], isec);
    }
}
DECLARE_BVH_TRAVERSAL(intersectTLAS, tlasGetChild, tlasGetAABB, tlasIsLeaf, intersectTLASLeaf, tlasNodeId)

Intersection intersectRay(in Ray ray) {
    Intersection isec;
    isec.dist = 1e10;
    isec.tlasNodeId = NULL;
    isec.blasNodeId = NULL;
    intersectTLAS(ray, NULL, 0, 0, isec);
    isec.dist = (isec.dist == 1e10 ? -1.0 : isec.dist);
    return isec;
}

void main() {
    uint rayId = uint(gl_GlobalInvocationID.x);
    if (rayId >= u_raysCount) {
        return;
    }

    vec4 rayData1 = rayBuffer[rayId * 2 + 0];
    vec4 rayData2 = rayBuffer[rayId * 2 + 1];

    Ray ray;
    ray.origin = rayData1.xyz;
    ray.dir    = rayData2.xyz;
    ray.invDir = safeInvDir(ray.dir);

    Intersection isec = intersectRay(ray);

    intersectionResult[rayId] = vec4(uintBitsToFloat(uvec2(isec.tlasNodeId, isec.blasNodeId)), isec.barycentric);

    //imageStore(outColor, floatBitsToInt(vec2(rayData1.w, rayData2.w)), vec4(vec3(isec.barycentric, 0.0), 1.0));
    //imageStore(outColor, floatBitsToInt(vec2(rayData1.w, rayData2.w)), vec4(floatBitsToInt(vec2(rayData1.w, rayData2.w)) / vec2(800.0, 600.0), 0.0, 1.0));

    //imageStore(outColor, floatBitsToInt(vec2(rayData1.w, rayData2.w)), vec4(ray.origin, 1.0));
}
