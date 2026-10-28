#pragma once

#include "Vulkan.hpp"
#include "Core/UUID.hpp"

#include <span>

namespace Kerberos {

struct BoneMatrix
{
    float R0[4];
    float R1[4];
    float R2[4];
};
static_assert(sizeof(BoneMatrix) == 48);

struct GPUSkeletonInfo
{
    uint32_t FirstJoint; // into parents[] / rests[]
    uint32_t JointCount;
    uint32_t FirstIBM; // into ibms[]
    uint32_t Flags;
    uint32_t _pad[4];
};
static_assert(sizeof(GPUSkeletonInfo) == 32);

struct GPUJointRest
{
    float T[4]; // translation xyz
    float R[4]; // quat xyzw
    float S[4]; // scale xyz
};
static_assert(sizeof(GPUJointRest) == 48);

// uint32_t parents[];                       // 0xFFFFFFFF = root
// BoneMatrix ibms[];                        // 48 B each

// ---- Skeleton instance (per skeleton-root entity) ----
struct GPUSkeletonInstanceStatic
{
    uint32_t SkeletonAsset;
    uint32_t FirstPose; // same slice base for localPose/pose/skinMatrix
    uint32_t JointCount;
    uint32_t Flags;
};
static_assert(sizeof(GPUSkeletonInstanceStatic) == 16);

struct GPUAnimState
{
    uint32_t ClipIndex;
    uint32_t Flags; // Playing | Loop | HasOverride
    float Time;
    float Speed;
    uint32_t _reserved[4]; // clip2 / blend weight later
};
static_assert(sizeof(GPUAnimState) == 32);

struct GPUClipInfo
{
    uint32_t FirstTrack;
    uint32_t TrackCount;
    uint32_t FirstJointMap; // into jointTrackMaps[]
    uint32_t JointCount;    // must match skeleton layout
    float Duration;
    uint32_t Flags;
    uint32_t _pad[2];
};
static_assert(sizeof(GPUClipInfo) == 32);

struct GPUJointTrackMap
{
    uint32_t Track[3]; // relative to FirstTrack; 0xFFFFFFFF none
    uint32_t _pad;
};
static_assert(sizeof(GPUJointTrackMap) == 16);

struct GPUTrack
{
    uint32_t FirstKey;
    uint32_t KeyCount;
    uint32_t Path;          // 0=T, 1=R, 2=S
    uint32_t Interpolation; // 0=linear, 1=step, 2=cubic
};
static_assert(sizeof(GPUTrack) == 16);

// float times[];                            // 4 B/key (SoA)
// float4 values[];                          // 16 B/key (SoA)

struct GPUSkinMeshInfo
{
    uint32_t FirstVertex; // into sourceVertices[]
    uint32_t VertexCount;
    uint32_t FirstIndex; // into indices[]
    uint32_t IndexCount;
    uint32_t Flags;
    uint32_t _pad[3];
};
static_assert(sizeof(GPUSkinMeshInfo) == 32);

struct GPUSkinVertex
{
    float Position[3];
    float Normal[3];
    float Tangent[4];
    float TexCoord[2];
    uint32_t Joints01, Joints23;   // 48,52  u16x2 packed
    uint32_t Weights01, Weights23; // 56,60  unorm16x2 packed
};
static_assert(sizeof(GPUSkinVertex) == 64);

// ---- Mesh instance (per SkinnedMeshComponent) ----
struct GPUSkinInstanceInfo
{
    uint32_t MeshAsset;        // GPUSkinMeshInfo
    uint32_t SkeletonInstance; // GPUSkeletonInstanceStatic
    uint32_t OutputByteOffset; // into SVB
    uint32_t EntityID;
    uint32_t Flags;
    uint32_t MaterialIndex_or_pad;
    uint32_t _pad[2];
};
static_assert(sizeof(GPUSkinInstanceInfo) == 32);

struct EvalPush
{
    uint32_t firstJoint;
    uint32_t firstPose;
    uint32_t jointCount;
    uint32_t clipIndex;
    float time;
    uint32_t flags;
    uint32_t _pad[2];
};
static_assert(sizeof(EvalPush) == 32);

struct PrecomputePush
{
    uint32_t firstPose;
    uint32_t firstIBM;
    uint32_t jointCount;
    uint32_t _pad;
};
static_assert(sizeof(PrecomputePush) == 16);

struct SkinningPushConstant
{
    uint32_t sourceFirst;
    uint32_t outputFirst;
    uint32_t vertexCount;
    uint32_t outputByteOffset;
    uint32_t firstSkinMatrix;
    uint32_t jointCount;
    uint32_t _pad[2];
};
static_assert(sizeof(SkinningPushConstant) == 32);

using SkeletonInstanceId = uint32_t;

struct SkeletonFrame
{
    SkeletonInstanceId Id;
    UUID Entity = UUID::Invalid();
    uint32_t FirstSkinMatrix; // in the skinMatrix buffer
    uint32_t JointCount;
    uint64_t SkinMatrixVersion;
};

struct SkinningFrameSnapshot
{
    vk::Buffer SkinMatrixBuffer;
    std::span<const SkeletonFrame> Skeletons;
};

using MeshInstanceId = uint32_t;

struct SkinnedDrawView
{
    MeshInstanceId Id;
    vk::Buffer VertexBuffer;
    vk::DeviceSize VertexOffsetBytes;
    uint32_t VertexCount;
    vk::Buffer IndexBuffer;
    vk::DeviceSize IndexOffsetBytes;
    uint32_t IndexCount;
    uint64_t SkinVersion;
};

}