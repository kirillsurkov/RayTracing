#version 430

#define NULL uint(-1)
#define M_PI 3.141592653

uniform uint u_iteration;
uniform uint u_raysCount;
uniform float u_timer;

layout(local_size_x = 64) in;

layout(rgba32f, binding = 0) uniform image2D outColor;

layout(std430,  binding = 16) writeonly buffer NextRays                  { vec4 nextRays[];                  };
layout(std430,  binding = 15)           buffer Counter                   { uint counter;                     };
layout(std430,  binding = 13) readonly  buffer IntersectionBuffer        { vec4 intersectionBuffer[];        };
layout(std430,  binding = 14) readonly  buffer RayBuffer                 { vec4 rayBuffer[];                 };
layout(std430,  binding = 1)  readonly  buffer TlasGetAABB               { vec4 tlasGetAABB[];               };
layout(std430,  binding = 2)  readonly  buffer TlasGetGeometry           { vec4 tlasGetGeometry[];           };
layout(std430,  binding = 3)  readonly  buffer TlasGetChild              { uint tlasGetChild[];              };
layout(std430,  binding = 4)  readonly  buffer TlasGetPrimitiveId        { uint tlasGetPrimitiveId[];        };
layout(std430,  binding = 5)  readonly  buffer TlasIsLeaf                { uint tlasIsLeaf[];                };
layout(std430,  binding = 6)  readonly  buffer TlasGetBlasNodeOffset     { uint tlasGetBlasNodeOffset[];     };
layout(std430,  binding = 7)  readonly  buffer TlasGetBlasGeometryOffset { uint tlasGetBlasGeometryOffset[]; };
layout(std430,  binding = 8)  readonly  buffer BlasGetAABB               { vec4 blasGetAABB[];               };
layout(std430,  binding = 9)  readonly  buffer BlasGetGeometry           { vec4 blasGetGeometry[];           };
layout(std430,  binding = 10) readonly  buffer BlasGetChild              { uint blasGetChild[];              };
layout(std430,  binding = 11) readonly  buffer BlasGetPrimitiveId        { uint blasGetPrimitiveId[];        };
layout(std430,  binding = 12) readonly  buffer BlasIsLeaf                { uint blasIsLeaf[];                };

struct Ray {
    vec3 origin;
    vec3 dir;
};

struct Intersection {
    uint tlasNodeId;
    uint blasNodeId;
    vec2 barycentric;
};

float rand(float n){return fract(sin(n) * 43758.5453123);}

float noise(float p){
    float fl = floor(p);
    float fc = fract(p);
    return mix(rand(fl), rand(fl + 1.0), fc);
}

float ggxNormalDistribution(float NdotH, float roughness) {
    float a2 = roughness * roughness;
    float d = ((NdotH * a2 - NdotH) * NdotH + 1);
    return a2 / (d * d * M_PI);
}

float schlickMaskingTerm(float NdotL, float NdotV, float roughness) {
    float k = roughness*roughness / 2;

    float g_v = NdotV / (NdotV*(1 - k) + k);
    float g_l = NdotL / (NdotL*(1 - k) + k);
    return g_v * g_l;
}

vec3 schlickFresnel(vec3 f0, float lDotH) {
    return f0 + (vec3(1.0f, 1.0f, 1.0f) - f0) * pow(1.0f - lDotH, 5.0f);
}

vec3 getPerpendicularVector(vec3 u) {
    vec3 a = abs(u);
    uint xm = ((a.x - a.y)<0 && (a.x - a.z)<0) ? 1 : 0;
    uint ym = (a.y - a.z)<0 ? (1 ^ xm) : 0;
    uint zm = 1 ^ (xm | ym);
    return cross(u, vec3(xm, ym, zm));
}

vec3 getGGXMicrofacet(float roughness, vec3 hitNorm) {
    float seed = rand(float(gl_GlobalInvocationID.x) / float(u_raysCount) + float(u_iteration));

    vec2 randVal = vec2(rand(seed + 0.1), rand(seed + 0.2));

    vec3 B = getPerpendicularVector(hitNorm);
    vec3 T = cross(B, hitNorm);

    float a2 = roughness * roughness;
    float cosThetaH = sqrt(max(0.0f, (1.0-randVal.x)/((a2-1.0)*randVal.x+1) ));
    float sinThetaH = sqrt(max(0.0f, 1.0f - cosThetaH * cosThetaH));
    float phiH = randVal.y * M_PI * 2.0f;

    return T * (sinThetaH * cos(phiH)) +
       B * (sinThetaH * sin(phiH)) +
       hitNorm * cosThetaH;
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

    vec4 isecData = intersectionBuffer[rayId];
    Intersection isec;
    isec.tlasNodeId = floatBitsToUint(isecData.x);
    isec.blasNodeId = floatBitsToUint(isecData.y);
    isec.barycentric = isecData.zw;

    float light = 0.0;
    if (isec.tlasNodeId != NULL && isec.blasNodeId != NULL) {
        uint tlasIndex = tlasGetPrimitiveId[tlasGetChild[isec.tlasNodeId]];
        uint blasNodeOffset = tlasGetBlasNodeOffset[tlasIndex];
        uint blasGeometryOffset = tlasGetBlasGeometryOffset[tlasIndex];
        uint blasIndex = blasGetPrimitiveId[blasGeometryOffset + blasGetChild[blasNodeOffset + isec.blasNodeId]];

        uint stride = 3;

        vec3 p1 = blasGetGeometry[((blasGeometryOffset + blasIndex) * 3 + 0) * stride + 0].xyz;
        vec3 p2 = blasGetGeometry[((blasGeometryOffset + blasIndex) * 3 + 1) * stride + 0].xyz;
        vec3 p3 = blasGetGeometry[((blasGeometryOffset + blasIndex) * 3 + 2) * stride + 0].xyz;

        vec3 n1 = blasGetGeometry[((blasGeometryOffset + blasIndex) * 3 + 0) * stride + 1].xyz;
        vec3 n2 = blasGetGeometry[((blasGeometryOffset + blasIndex) * 3 + 1) * stride + 1].xyz;
        vec3 n3 = blasGetGeometry[((blasGeometryOffset + blasIndex) * 3 + 2) * stride + 1].xyz;

        bool emissive = blasGetGeometry[((blasGeometryOffset + blasIndex) * 3 + 0) * stride + 2].x > 0.0;

        vec3 normal = (1.0 - isec.barycentric.x - isec.barycentric.y) * n1 +
                                                  isec.barycentric.x  * n2 +
                                                  isec.barycentric.y  * n3;
        normal = normalize(normal);

        vec3 pos = (1.0 - isec.barycentric.x - isec.barycentric.y) * p1 +
                                               isec.barycentric.x  * p2 +
                                               isec.barycentric.y  * p3;

        //light = (emissive ? 1.0 : 0.1) * length(pos - ray.origin) / 20;

        if (emissive) {
            light = 1.0;
        } else {
            uint offset = atomicAdd(counter, 1);

            vec3 newRayDirection = reflect(rayData2.xyz, getGGXMicrofacet(0.1, normal));

            rayData1.xyz = pos + normal * 0.001;
            //rayData2.xyz = reflect(rayData2.xyz, normal);
            rayData2.xyz = newRayDirection;

            nextRays[offset * 2 + 0] = rayData1;
            nextRays[offset * 2 + 1] = rayData2;
        }
    } else {
        //light = 1.0;
    }

    imageStore(outColor, floatBitsToInt(vec2(rayData1.w, rayData2.w)), vec4(vec3(light), 1.0));
}
