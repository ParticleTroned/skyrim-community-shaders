#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <immintrin.h>
#include <limits>

namespace RE
{
	struct NiPoint3
	{
		float x = 0, y = 0, z = 0;
		NiPoint3 operator*(float scale) const { return { x * scale, y * scale, z * scale }; }
		NiPoint3 operator+(const NiPoint3& other) const { return { x + other.x, y + other.y, z + other.z }; }
	};
	struct hkVector4
	{
		__m128 quad = _mm_setzero_ps();
		hkVector4() = default;
		hkVector4(float x, float y, float z, float w) : quad(_mm_setr_ps(x, y, z, w)) {}
		explicit hkVector4(__m128 value) : quad(value) {}
	};
	struct hkTransform
	{
		struct
		{
			hkVector4 col0{ 1, 0, 0, 0 };
			hkVector4 col1{ 0, 1, 0, 0 };
			hkVector4 col2{ 0, 0, 1, 0 };
		} rotation;
		hkVector4 translation;
	};
	struct bhkWorld
	{
		static inline float scale = 1;
		static float GetWorldScaleInverse() { return scale; }
	};
	struct hkReferencedObject
	{
		virtual ~hkReferencedObject() = default;
	};
	enum class hkpShapeType
	{
		kCapsule,
		kSphere,
		kBox,
		kCylinder,
		kConvexVertices,
		kTriangle,
		kList
	};
	struct hkpShape : hkReferencedObject
	{
		hkpShapeType type = hkpShapeType::kSphere;
		NiPoint3 center;
		NiPoint3 halfExtents{ 2, 2, 2 };
		mutable int projectionCalls = 0;
		float GetMaximumProjection(const hkVector4& direction) const
		{
			++projectionCalls;
			alignas(16) float axis[4];
			_mm_store_ps(axis, direction.quad);
			// Axis support projections include the local center with the direction's sign.
			return center.x * axis[0] + center.y * axis[1] + center.z * axis[2] +
			       halfExtents.x * std::abs(axis[0]) + halfExtents.y * std::abs(axis[1]) + halfExtents.z * std::abs(axis[2]);
		}
	};
	struct hkpCapsuleShape : hkpShape
	{
		hkpCapsuleShape() { type = hkpShapeType::kCapsule; }
		hkVector4 vertexA{ 0, 0, -2, 0 };
		hkVector4 vertexB{ 0, 0, 2, 0 };
		float radius = 1;
	};
	struct hkpListShape : hkpShape
	{
		hkpListShape() { type = hkpShapeType::kList; }
	};
	struct hkpRigidBody : hkReferencedObject
	{
		struct
		{
			const hkpShape* shape = nullptr;
			const hkpShape* GetShape() const { return shape; }
		} collidable;
	};
	template <class T>
	struct Borrowed
	{
		T* value = nullptr;
		T* get() const { return value; }
	};
	struct bhkRigidBody;
	struct bhkEntity
	{
		virtual ~bhkEntity() = default;
		virtual bhkRigidBody* AsBhkRigidBody() { return nullptr; }
	};
	struct bhkRigidBody : bhkEntity
	{
		Borrowed<hkReferencedObject> referencedObject;
		hkTransform transform;
		hkVector4 massCenter{ 100, 200, 300, 0 };
		int transformCalls = 0;
		int massCenterCalls = 0;
		bhkRigidBody* AsBhkRigidBody() override { return this; }
		void GetTransform(hkTransform& output)
		{
			++transformCalls;
			output = transform;
		}
		void GetCenterOfMassWorld(hkVector4& output)
		{
			++massCenterCalls;
			output = massCenter;
		}
	};
	struct bhkBlendCollisionObject;
	struct bhkNiCollisionObject
	{
		virtual ~bhkNiCollisionObject() = default;
		virtual bhkBlendCollisionObject* AsBhkBlendCollisionObject() { return nullptr; }
		Borrowed<bhkEntity> body;
	};
	struct bhkBlendCollisionObject : bhkNiCollisionObject
	{
		bhkBlendCollisionObject* AsBhkBlendCollisionObject() override { return this; }
	};
}

template <class To, class From>
To skyrim_cast(From* value)
{
	return dynamic_cast<To>(value);
}

namespace Util
{
	bool ExtractShapeBound(const RE::hkpShape* shape, float& radius);
}

#include "actor_shape_bounds_under_test.h"

namespace
{
	int failures = 0;
	void Check(bool condition, const char* message)
	{
		if (!condition) {
			std::fprintf(stderr, "%s\n", message);
			++failures;
		}
	}
	bool Near(float actual, float expected)
	{
		return std::isfinite(actual) && std::abs(actual - expected) < 0.0001f;
	}
	struct Fixture
	{
		RE::hkpCapsuleShape shape;
		RE::hkpRigidBody rigid;
		RE::bhkRigidBody wrapper;
		RE::bhkNiCollisionObject object;
		Fixture()
		{
			RE::bhkWorld::scale = 1;
			rigid.collidable.shape = &shape;
			wrapper.referencedObject.value = &rigid;
			object.body.value = &wrapper;
		}
	};
	void ExpectRejected(RE::bhkNiCollisionObject* object)
	{
		RE::NiPoint3 center{ 17, 19, 23 };
		float radius = 29;
		Check(!Util::GetShapeBound(object, center, radius), "invalid collision accepted");
		Check(center.x == 17 && center.y == 19 && center.z == 23 && radius == 29, "failed extraction changed outputs");
	}
	void ExpectCapsuleRejected(Fixture& fixture)
	{
		ExpectRejected(&fixture.object);
		float radius = 31;
		Check(!Util::ExtractShapeBound(&fixture.shape, radius) && radius == 31, "invalid capsule radius accepted or published");
	}
	void TransformedCapsule()
	{
		Fixture f;
		f.shape.vertexA = { 1, 2, 3, 0 };
		f.shape.vertexB = { 1, 2, 7, 0 };
		f.wrapper.transform.rotation.col0 = { 0, 1, 0, 0 };
		f.wrapper.transform.rotation.col1 = { -1, 0, 0, 0 };
		f.wrapper.transform.translation = { 10, 20, 30, 0 };
		RE::bhkWorld::scale = 2;
		RE::NiPoint3 center;
		float radius = 0;
		Check(Util::GetShapeBound(&f.object, center, radius), "transformed capsule rejected");
		Check(Near(center.x, 16) && Near(center.y, 42) && Near(center.z, 70) && Near(radius, 6), "rotation, translation, scale or midpoint is wrong");
		Check(f.wrapper.transformCalls == 1 && f.wrapper.massCenterCalls == 0 && f.shape.projectionCalls == 0, "capsule used center of mass or projections");
		std::swap(f.shape.vertexA, f.shape.vertexB);
		Check(Util::GetShapeBound(&f.object, center, radius) && Near(center.z, 70) && Near(radius, 6), "endpoint order changes bounds");
	}
	void CapsuleGeometry()
	{
		Fixture f;
		f.shape.vertexA = { 0, 0, 0, 0 };
		f.shape.vertexB = { 6, 8, 0, 0 };
		f.shape.radius = 2;
		RE::NiPoint3 center;
		float radius = 0;
		Check(Util::GetShapeBound(&f.object, center, radius) && Near(center.x, 3) && Near(center.y, 4) && Near(radius, 7), "diagonal capsule does not enclose its endpoints");
		Check(Util::ExtractShapeBound(&f.shape, radius) && Near(radius, 7), "radius entry points disagree");
		f.shape.vertexB = f.shape.vertexA;
		Check(Util::GetShapeBound(&f.object, center, radius) && Near(radius, 2), "coincident endpoints rejected");
		const float limit = std::numeric_limits<float>::max();
		f.shape.vertexA = { -limit, 0, 0, 0 };
		f.shape.vertexB = { limit, 0, 0, 0 };
		Check(Util::GetShapeBound(&f.object, center, radius) && center.x == 0 && radius == limit, "representable bound overflowed intermediate arithmetic");
		RE::bhkWorld::scale = 2;
		ExpectCapsuleRejected(f);
	}
	void InvalidCapsules()
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		for (float invalid : { nan, inf, -inf }) {
			for (int endpoint = 0; endpoint < 2; ++endpoint) {
				for (int axis = 0; axis < 3; ++axis) {
					Fixture f;
					alignas(16) float components[4]{};
					components[axis] = invalid;
					(endpoint == 0 ? f.shape.vertexA : f.shape.vertexB).quad = _mm_load_ps(components);
					ExpectCapsuleRejected(f);
				}
			}
		}
		for (float invalid : { 0.0f, -1.0f, nan, inf, -inf }) {
			Fixture radiusCase;
			radiusCase.shape.radius = invalid;
			ExpectCapsuleRejected(radiusCase);
			Fixture scaleCase;
			RE::bhkWorld::scale = invalid;
			ExpectCapsuleRejected(scaleCase);
		}
		Fixture underflow;
		underflow.shape.vertexA = underflow.shape.vertexB;
		underflow.shape.radius = std::numeric_limits<float>::min();
		RE::bhkWorld::scale = std::numeric_limits<float>::min();
		ExpectCapsuleRejected(underflow);
		Fixture translation;
		translation.wrapper.transform.translation = { 0, nan, 0, 0 };
		ExpectRejected(&translation.object);
		Fixture rotation;
		rotation.wrapper.transform.rotation.col0 = { inf, 0, 0, 0 };
		ExpectRejected(&rotation.object);
	}
	void MissingAndUnsupportedShapes()
	{
		ExpectRejected(nullptr);
		Fixture f;
		f.object.body.value = nullptr;
		ExpectRejected(&f.object);
		RE::bhkEntity entity;
		f.object.body.value = &entity;
		ExpectRejected(&f.object);
		f.object.body.value = &f.wrapper;
		f.wrapper.referencedObject.value = nullptr;
		ExpectRejected(&f.object);
		f.wrapper.referencedObject.value = &f.shape;
		ExpectRejected(&f.object);
		f.wrapper.referencedObject.value = &f.rigid;
		f.rigid.collidable.shape = nullptr;
		ExpectRejected(&f.object);
		RE::hkpListShape list;
		f.rigid.collidable.shape = &list;
		ExpectRejected(&f.object);
		float radius = 31;
		Check(!Util::ExtractShapeBound(&list, radius) && radius == 31, "unsupported list radius accepted or published");
		Check(list.projectionCalls == 0 && f.wrapper.massCenterCalls == 0 && f.wrapper.transformCalls == 0, "rejected shape reached Havok accessors");
	}
	void BlendCollisionLists()
	{
		Fixture f;
		RE::hkpListShape list;
		list.center = { 1, 2, 3 };
		list.halfExtents = { 2, 3, 6 };
		f.rigid.collidable.shape = &list;
		RE::bhkBlendCollisionObject blend;
		blend.body.value = &f.wrapper;
		f.wrapper.transform.rotation.col0 = { 0, 1, 0, 0 };
		f.wrapper.transform.rotation.col1 = { -1, 0, 0, 0 };
		f.wrapper.transform.translation = { 10, 20, 30, 0 };
		RE::bhkWorld::scale = 2;
		RE::NiPoint3 center;
		float radius = 0;
		Check(Util::GetShapeBound(&blend, center, radius), "blend collision list rejected");
		Check(Near(center.x, 16) && Near(center.y, 42) && Near(center.z, 66) && Near(radius, 14),
			"blend collision list has incorrect transformed bounds");
		Check(list.projectionCalls == 6 && f.wrapper.transformCalls == 1 && f.wrapper.massCenterCalls == 0,
			"blend collision list did not use projected geometry");

		list.projectionCalls = 0;
		ExpectRejected(&f.object);
		radius = 31;
		Check(!Util::ExtractShapeBound(&list, radius) && radius == 31, "context-free list radius accepted or published");
		Check(list.projectionCalls == 0, "unsupported list reached projections");

		for (float invalid : { -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
			list.halfExtents = { 2, invalid, 6 };
			ExpectRejected(&blend);
		}
		list.halfExtents = {};
		ExpectRejected(&blend);
		f.rigid.collidable.shape = nullptr;
		ExpectRejected(&blend);
		blend.body.value = nullptr;
		ExpectRejected(&blend);
	}
	void NonCapsuleFallback()
	{
		Fixture f;
		RE::hkpShape sphere;
		sphere.center = { 1, 2, 3 };
		f.rigid.collidable.shape = &sphere;
		f.wrapper.transform.rotation.col0 = { 0, 1, 0, 0 };
		f.wrapper.transform.rotation.col1 = { -1, 0, 0, 0 };
		f.wrapper.transform.translation = { 10, 20, 30, 0 };
		RE::bhkWorld::scale = 3;
		RE::NiPoint3 center;
		float radius = 0;
		Check(Util::GetShapeBound(&f.object, center, radius), "fallback rejected");
		Check(Near(center.x, 24) && Near(center.y, 63) && Near(center.z, 99) && Near(radius, 6), "non-capsule bound is not centered on transformed geometry");
		Check(f.wrapper.massCenterCalls == 0 && f.wrapper.transformCalls == 1 && sphere.projectionCalls == 6, "non-capsule bound used center of mass or repeated projections");
		f.wrapper.transform.translation = { std::numeric_limits<float>::quiet_NaN(), 0, 0, 0 };
		ExpectRejected(&f.object);
	}
	void NonCapsuleGeometry()
	{
		struct Case
		{
			RE::hkpShapeType type;
			RE::NiPoint3 halfExtents;
			float radius;
		};
		const std::array<Case, 5> cases{ {
			{ RE::hkpShapeType::kSphere, { 2, 2, 2 }, 2 },
			{ RE::hkpShapeType::kBox, { 2, 3, 6 }, 7 },
			{ RE::hkpShapeType::kCylinder, { 2, 2, 6 }, std::sqrt(44.0f) },
			{ RE::hkpShapeType::kConvexVertices, { 2, 3, 6 }, 7 },
			{ RE::hkpShapeType::kTriangle, { 2, 3, 6 }, 7 },
		} };
		for (const auto& test : cases)
			for (const RE::NiPoint3 offset : { RE::NiPoint3{}, RE::NiPoint3{ 17, -19, 23 } })
				for (float scale : { 1.0f, 3.0f }) {
					RE::hkpShape shape;
					shape.type = test.type;
					shape.center = offset;
					shape.halfExtents = test.halfExtents;
					RE::bhkWorld::scale = scale;
					float radius = 0;
					Check(Util::ExtractShapeBound(&shape, radius) && Near(radius, test.radius * scale),
						"non-capsule radius depends on local offset or loses its extents");
				}
	}
	void InvalidNonCapsules()
	{
		Fixture f;
		RE::hkpShape shape;
		f.rigid.collidable.shape = &shape;
		const auto rejected = [&] {
			ExpectRejected(&f.object);
			float radius = 31;
			Check(!Util::ExtractShapeBound(&shape, radius) && radius == 31, "invalid projected radius accepted or published");
		};
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		for (float invalid : { 0.0f, -1.0f, nan, inf, -inf }) {
			RE::bhkWorld::scale = invalid;
			rejected();
		}
		RE::bhkWorld::scale = 1;
		for (float invalid : { -1.0f, nan, inf, -inf }) {
			shape.halfExtents = { 2, invalid, 2 };
			rejected();
		}
		shape.halfExtents = {};
		rejected();
		shape.halfExtents = { 2, 2, 2 };
		shape.center = { nan, 0, 0 };
		rejected();
		shape.center = {};
		shape.halfExtents = { std::numeric_limits<float>::min(), 0, 0 };
		RE::bhkWorld::scale = std::numeric_limits<float>::min();
		rejected();
	}
	void LargeNonCapsules()
	{
		RE::hkpShape shape;
		const float limit = std::numeric_limits<float>::max();
		shape.halfExtents = { limit, limit, limit };
		RE::bhkWorld::scale = 1;
		float radius = 0;
		Check(Util::ExtractShapeBound(&shape, radius) && radius == limit, "representable sphere radius overflowed projections");
		shape.type = RE::hkpShapeType::kBox;
		shape.halfExtents = { limit * .5f, limit * .5f, limit * .5f };
		Check(Util::ExtractShapeBound(&shape, radius) && std::abs(double(radius) / limit - std::sqrt(3.0) * .5) < 1e-6,
			"representable box radius overflowed intermediate arithmetic");
		RE::bhkWorld::scale = 2;
		radius = 31;
		Check(!Util::ExtractShapeBound(&shape, radius) && radius == 31, "unrepresentable projected radius was published");
	}
}

int main()
{
	TransformedCapsule();
	CapsuleGeometry();
	InvalidCapsules();
	MissingAndUnsupportedShapes();
	BlendCollisionLists();
	NonCapsuleFallback();
	NonCapsuleGeometry();
	InvalidNonCapsules();
	LargeNonCapsules();
	return failures == 0 ? 0 : 1;
}
