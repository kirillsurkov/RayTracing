#version 430

#define NULL uint(-1)
#define swap(a, b) (a ^= b, b ^= a, a ^= b)

uniform float u_timer;
uniform mat4 u_viewInv;

layout(local_size_x = 8, local_size_y = 8) in;

layout(rgba32f, binding = 0) uniform image2D outColor;

layout(std430,  binding = 1)  readonly buffer TlasGetAABB               { vec4 tlasGetAABB[];               };
layout(std430,  binding = 2)  readonly buffer TlasGetGeometry           { vec4 tlasGetGeometry[];           };
layout(std430,  binding = 3)  readonly buffer TlasGetChild              { uint tlasGetChild[];              };
layout(std430,  binding = 4)  readonly buffer TlasGetPrimitiveId        { uint tlasGetPrimitiveId[];        };
layout(std430,  binding = 5)  readonly buffer TlasIsLeaf                { uint tlasIsLeaf[];                };
layout(std430,  binding = 6)  readonly buffer TlasGetBlasNodeOffset     { uint tlasGetBlasNodeOffset[];     };
layout(std430,  binding = 7)  readonly buffer TlasGetBlasGeometryOffset { uint tlasGetBlasGeometryOffset[]; };
layout(std430,  binding = 8)  readonly buffer BlasGetAABB               { vec4 blasGetAABB[];               };
layout(std430,  binding = 9)  readonly buffer BlasGetGeometry           { vec4 blasGetGeometry[];           };
layout(std430,  binding = 10) readonly buffer BlasGetChild              { uint blasGetChild[];              };
layout(std430,  binding = 11) readonly buffer BlasGetPrimitiveId        { uint blasGetPrimitiveId[];        };
layout(std430,  binding = 12) readonly buffer BlasIsLeaf                { uint blasIsLeaf[];                };

#define DECLARE_BVH_TRAVERSAL(NAME, GET_CHILD, GET_AABB, IS_LEAF, INTERSECT_FUNCTION) \
    vec4 NAME(in Ray ray, uint skipId, uint nodeOffset, uint geometryOffset, out uint childId) { \
        vec4 bestIsec = vec4(0.0, 0.0, 0.0, 1e10); \
        \
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
                    vec4 isec = INTERSECT_FUNCTION(ray, geometryOffset, GET_CHILD[nodeOffset + leftChild], childId); \
                    if (isec.w >= 0 && isec.w < bestIsec.w && leftChild != skipId) { \
                        bestIsec = isec; \
                    } \
                    leftChild = NULL; \
                } \
            } else leftChild = NULL; \
            \
            if (distRight.x <= distRight.y) { \
                if (IS_LEAF[nodeOffset + rightChild] > 0) { \
                    vec4 isec = INTERSECT_FUNCTION(ray, geometryOffset, GET_CHILD[nodeOffset + rightChild], childId); \
                    if (isec.w >= 0 && isec.w < bestIsec.w && rightChild != skipId) { \
                        bestIsec = isec; \
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
        \
        bestIsec.w = bestIsec.w == 1e10 ? -1.0 : bestIsec.w; \
        return bestIsec; \
    }

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

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

struct Ray {
    vec3 origin;
    vec3 dir;
    vec3 invDir;
};

Ray generateCameraRay(vec2 xy) {
    Ray ray;

    ray.origin = vec3(0.0, 0.0, 0.0);
    ray.dir = normalize(vec3(xy, -5.0));

    ray.origin = (u_viewInv*vec4(ray.origin, 1)).xyz;
    ray.dir    = (u_viewInv*vec4(ray.dir,    0)).xyz;

    ray.invDir = safeInvDir(ray.dir);

    return ray;
}

vec4 triIntersect(in Ray ray, in vec3 v0, in vec3 v1, in vec3 v2) {
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

    return vec4(1.0 - u - v, u, v, t);
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

vec4 intersectBLASLeaf(in Ray ray, uint geometryOffset, uint leafChild, out uint childId) {
    uint index = blasGetPrimitiveId[geometryOffset + leafChild];
    vec3 p1 = blasGetGeometry[(geometryOffset + index) * 3 + 0].xyz;
    vec3 p2 = blasGetGeometry[(geometryOffset + index) * 3 + 1].xyz;
    vec3 p3 = blasGetGeometry[(geometryOffset + index) * 3 + 2].xyz;
    return triIntersect(ray, p1, p2, p3);
}
DECLARE_BVH_TRAVERSAL(intersectBLAS, blasGetChild, blasGetAABB, blasIsLeaf, intersectBLASLeaf)

vec4 intersectTLASLeaf(in Ray ray, uint geometryOffset, uint leafChild, out uint childId) {
    uint index = tlasGetPrimitiveId[leafChild];
    vec3 aabbMin = tlasGetGeometry[index * 2 + 0].xyz;
    vec3 aabbMax = tlasGetGeometry[index * 2 + 1].xyz;
    vec2 isec = aabbIntersect(ray, aabbMin, aabbMax);
    if (isec.x <= isec.y) {
        return intersectBLAS(ray, NULL, tlasGetBlasNodeOffset[index], tlasGetBlasGeometryOffset[index], childId);
    } else {
        return vec4(0.0, 0.0, 0.0, -1.0);
    }
}
DECLARE_BVH_TRAVERSAL(intersectTLAS, tlasGetChild, tlasGetAABB, tlasIsLeaf, intersectTLASLeaf)

vec4 intersectRay(in Ray ray, uint skipId, out uint childId) {
    return intersectTLAS(ray, skipId, 0, 0, childId);
}

float rand(vec2 n) {
    return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453);
}

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dims = imageSize(outColor);
    if (pixelCoords.x >= dims.x || pixelCoords.y >= dims.y) {
        return;
    }

    vec2 xy = vec2(2.0 * float(pixelCoords.x * 2 - dims.x) / float(dims.x), 2.0 * float(pixelCoords.y * 2 - dims.y) / float(dims.y));
    xy.x *= float(dims.x) / float(dims.y);
    Ray ray = generateCameraRay(xy);

    vec4 pixel = vec4(0.0, 0.0, 0.0, 1.0);

    uint childId;
    vec4 isec;
    float rayDist = 0.0;

    isec = intersectRay(ray, NULL, childId);

    for (uint i = 0; i < 1; i++) {
        if (isec.w == -1.0) break;
        pixel.xyz = vec3(isec.w / 1000.0);
    }

    pixel.xyz = pow(pixel.xyz, vec3(1.0 / 2.2));

    imageStore(outColor, pixelCoords, pixel);
}
