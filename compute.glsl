#version 430

#define NULLPTR uint(-1)
#define swap(a, b) (a ^= b, b ^= a, a ^= b)

uniform float u_timer;
uniform mat4 u_viewInv;

layout(local_size_x = 8, local_size_y = 8) in;

layout(rgba32f, binding = 0) uniform image2D outColor;

layout(std430, binding = 1) readonly buffer GeometryPos {
    vec4 geometryPos[];
};

layout(std430, binding = 2) readonly buffer GeometryNormal {
    vec4 geometryNormal[];
};

layout(std430, binding = 3) readonly buffer GeometryColor {
    vec4 geometryColor[];
};

layout(std430, binding = 4) readonly buffer BVHLeaf {
    uint bvhLeaf[];
};

layout(std430, binding = 5) readonly buffer BVHAABBMin {
    vec4 bvhAABBMin[];
};

layout(std430, binding = 6) readonly buffer BVHAABBMax {
    vec4 bvhAABBMax[];
};

layout(std430, binding = 7) readonly buffer BVHChild {
    uint bvhChild[];
};

layout(std430, binding = 8) readonly buffer BVHPrimitive {
    uint bvhPrimitive[];
};

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

vec4 leafIntersect(in Ray ray, uint leafIndex) {
    uint index = bvhPrimitive[bvhChild[leafIndex]];
    vec3 p1 = geometryPos[index * 3 + 0].xyz;
    vec3 p2 = geometryPos[index * 3 + 1].xyz;
    vec3 p3 = geometryPos[index * 3 + 2].xyz;
    return triIntersect(ray, p1, p2, p3);
}

shared uint stack[32 * gl_WorkGroupSize.x * gl_WorkGroupSize.y];
vec4 intersectRay(in Ray ray, uint skipId, out uint childId) {
    vec4 bestIsec = vec4(0.0, 0.0, 0.0, 1e10);

    uint stackBase = (gl_WorkGroupSize.x * gl_LocalInvocationID.y + gl_LocalInvocationID.x) * 32;
    uint stackIt = 0;

    stack[stackBase + stackIt++] = NULLPTR;

    uint leftChild = bvhChild[0];
    while (leftChild != NULLPTR) {
        uint rightChild = leftChild + 1;

        vec4 bbMinLeft  = bvhAABBMin[leftChild];
        vec4 bbMaxLeft  = bvhAABBMax[leftChild];
        vec4 bbMinRight = bvhAABBMin[rightChild];
        vec4 bbMaxRight = bvhAABBMax[rightChild];

        vec2 distLeft  = aabbIntersect(ray, bbMinLeft.xyz, bbMaxLeft.xyz);
        vec2 distRight = aabbIntersect(ray, bbMinRight.xyz, bbMaxRight.xyz);

        if (distLeft.x <= distLeft.y) {
            if (bvhLeaf[leftChild] > 0) {
                vec4 isec = leafIntersect(ray, leftChild);
                if (isec.w >= 0 && isec.w < bestIsec.w && leftChild != skipId) {
                    childId = leftChild;
                    bestIsec = isec;
                }
                leftChild = NULLPTR;
            }
        } else leftChild = NULLPTR;

        if (distRight.x <= distRight.y) {
            if (bvhLeaf[rightChild] > 0) {
                vec4 isec = leafIntersect(ray, rightChild);
                if (isec.w >= 0 && isec.w < bestIsec.w && rightChild != skipId) {
                    childId = rightChild;
                    bestIsec = isec;
                }
                rightChild = NULLPTR;
            }
        } else rightChild = NULLPTR;

        if (leftChild != NULLPTR) {
            if (rightChild != NULLPTR) {
                if (distLeft.x > distRight.x) swap(leftChild, rightChild);
                if (stackIt == 32) break;
                stack[stackBase + stackIt++] = bvhChild[rightChild];
            }
            leftChild = bvhChild[leftChild];
        } else if (rightChild != NULLPTR) {
            leftChild = bvhChild[rightChild];
        } else {
            leftChild = stack[stackBase + --stackIt];
        }
    }

    bestIsec.w = bestIsec.w == 1e10 ? -1.0 : bestIsec.w;

    return bestIsec;
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

    const uint lightsN = 3;
    const float radius = 10.0;

    vec3 lights[lightsN];
    vec3 colors[lightsN];
    float strength[lightsN];
    for (int i = 0; i < lightsN; i++) {
        float offset = float(i) / float(lightsN);
        float offsetPos = offset * 2.0 * 3.141592653;
        float offsetColor = offset * 2.0f;
        lights[i] = vec3(radius * sin(u_timer + offsetPos), 10.0, -25.0 - 12.5 * offset * radius + 0 * cos(0*u_timer + offsetPos));
        colors[i] = hsv2rgb(vec3(offsetColor, 0.25f, 1.0f));

        switch (i) {
        case 0: {
            lights[i] = vec3(radius * sin(u_timer + offsetPos), 10.0, -115.0);
            strength[i] = 150.0;
            break;
        }
        case 1: {
            lights[i] = vec3(radius * sin(u_timer + offsetPos), 60.0, -20.0);
            strength[i] = 750.0;
            break;
        }
        case 2: {
            lights[i] = vec3(radius * sin(u_timer + offsetPos), 10.0, radius * cos(u_timer + offsetPos));
            strength[i] = 80.0;
            break;
        }
        }
    }

    vec4 pixel = vec4(0.0, 0.0, 0.0, 1.0);

    uint childId;
    vec4 isec = intersectRay(ray, NULLPTR, childId);
    if (isec.w > 0.0) {
        uint primitiveId = bvhPrimitive[bvhChild[childId]];

        vec3 normal = geometryNormal[primitiveId * 3 + 0].xyz * isec.x +
                      geometryNormal[primitiveId * 3 + 1].xyz * isec.y +
                      geometryNormal[primitiveId * 3 + 2].xyz * isec.z;
        normal = normalize(normal);

        vec4 color = geometryColor[primitiveId];

        pixel.xyz = vec3(0.1);

        for (int i = 0; i < lightsN; i++) {
            uint tmp;

            Ray nextRay;
            nextRay.origin = ray.origin + ray.dir * isec.w;

            vec3 lightToRay = lights[i] - nextRay.origin;

            nextRay.dir = normalize(lightToRay);
            nextRay.invDir = safeInvDir(nextRay.dir);

            vec4 isec2 = intersectRay(nextRay, childId, tmp);

            if (isec2.w == -1.0 || isec2.w*isec2.w >= dot(lightToRay, lightToRay)) {
                float ratio = max(dot(normal, nextRay.dir), 0.0);
                pixel.xyz += colors[i] * strength[i] * ratio / dot(lights[i] - nextRay.origin, lights[i] - nextRay.origin);
            }
        }
    }

    pixel.xyz = pow(pixel.xyz, vec3(1.0 / 2.2));

    imageStore(outColor, pixelCoords, pixel);
}
