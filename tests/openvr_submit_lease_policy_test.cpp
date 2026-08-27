#include "Features/VR/OpenVRSubmitLeasePolicy.h"

#include <cstdlib>

namespace
{
	void Require(bool a_condition)
	{
		if (!a_condition)
			std::abort();
	}

	void TestPayloadSelection()
	{
		using OpenVRSubmitLeasePolicy::PayloadKind;
		using OpenVRSubmitLeasePolicy::SelectPayloadKind;

		Require(SelectPayloadKind(false, false) == PayloadKind::Texture);
		Require(SelectPayloadKind(true, false) == PayloadKind::TextureWithPose);
		Require(SelectPayloadKind(false, true) == PayloadKind::TextureWithDepth);
		Require(SelectPayloadKind(true, true) == PayloadKind::TextureWithPoseAndDepth);
	}

	void TestPublicationRequiresExactGenerationAndDevice()
	{
		using OpenVRSubmitLeasePolicy::CanPublish;
		using OpenVRSubmitLeasePolicy::PublicationLease;

		const PublicationLease lease{
			.generation = 17,
			.deviceIdentity = 0x1234,
			.colorTextureRetained = true,
		};
		Require(CanPublish(lease, 17, 0x1234));
		Require(!CanPublish(lease, 18, 0x1234));
		Require(!CanPublish(lease, 17, 0x5678));
	}

	void TestRequiredLifetimesAreRetained()
	{
		using OpenVRSubmitLeasePolicy::CanPublish;
		using OpenVRSubmitLeasePolicy::PublicationLease;

		PublicationLease lease{
			.generation = 0,
			.deviceIdentity = 0x1234,
			.colorTextureRetained = true,
			.depthTextureRequired = true,
		};
		Require(!CanPublish(lease, 0, 0x1234));

		lease.depthTextureRetained = true;
		Require(CanPublish(lease, 0, 0x1234));

		lease.colorTextureRetained = false;
		Require(!CanPublish(lease, 0, 0x1234));
	}
}

int main()
{
	TestPayloadSelection();
	TestPublicationRequiresExactGenerationAndDevice();
	TestRequiredLifetimesAreRetained();
	return 0;
}
