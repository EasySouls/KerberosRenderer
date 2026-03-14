#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include <cstdint>

namespace Kerberos::Physics
{
	/**
	* Layer that objects can be in, determines which other objects it can collide with
	* Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
	* layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics simulation
	* but only if you do collision testing).
	*/
	namespace Layers
	{
		static constexpr JPH::ObjectLayer NON_MOVING = 0;
		static constexpr JPH::ObjectLayer MOVING = 1;
		static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
	};


	/**
	 * Class that determines if two object layers can collide
	 */
	class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(const JPH::ObjectLayer inObject1, const JPH::ObjectLayer inObject2) const override
		{
			switch (inObject1)
			{
				case Layers::NON_MOVING:
					return inObject2 == Layers::MOVING; /// Non moving only collides with moving
				case Layers::MOVING:
					return true;						/// Moving collides with everything
				default:
					JPH_ASSERT(false);
					return false;
			}
		}
	};

	/**
	* Each broadphase layer results in a separate bounding volume tree in the broad phase.You at least want to have
	* a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
	* You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
	* many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
	* your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
	*/
	namespace BroadPhaseLayers
	{
		static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
		static constexpr JPH::BroadPhaseLayer MOVING(1);
		static constexpr uint32_t NUM_LAYERS(2);
	};


	/**
	 * BroadPhaseLayerInterface implementation.
	 * This defines a mapping between object and broadphase layers.
	 */
	class BroadPhaseLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
	{
	public:
		BroadPhaseLayerInterfaceImpl()
		{
			/// Create a mapping table from object to broad phase layer
			m_ObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
			m_ObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
		}

		std::uint32_t GetNumBroadPhaseLayers() const override
		{
			return BroadPhaseLayers::NUM_LAYERS;
		}

		JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer inLayer) const override
		{
			JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
			return m_ObjectToBroadPhase[inLayer];
		}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		virtual const char* GetBroadPhaseLayerName(const JPH::BroadPhaseLayer inLayer) const override
		{
			switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer))
			{
				case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING):	
					return "NON_MOVING";
				case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING):		
					return "MOVING";
				default:
					JPH_ASSERT(false); 
					return "INVALID";
			}
		}
#endif

	private:
		JPH::BroadPhaseLayer m_ObjectToBroadPhase[Layers::NUM_LAYERS];
	};

	/// Class that determines if an object layer can collide with a broadphase layer
	class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(const JPH::ObjectLayer inLayer1, const JPH::BroadPhaseLayer inLayer2) const override
		{
			switch (inLayer1)
			{
				case Layers::NON_MOVING:
					return inLayer2 == BroadPhaseLayers::MOVING;
				case Layers::MOVING:
					return true;
				default:
					JPH_ASSERT(false);
					return false;
			}
		}
	};
}