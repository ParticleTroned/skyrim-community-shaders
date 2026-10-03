#include "Features/MeshBlendingPolicy.h"
#include "Utils/BoundedTextRead.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>

namespace
{
	class CountingStreamBuffer final : public std::streambuf
	{
	public:
		explicit CountingStreamBuffer(std::string a_contents) : contents(std::move(a_contents)) {}

		[[nodiscard]] std::size_t BytesConsumed() const noexcept { return offset; }

	protected:
		std::streamsize xsgetn(char* a_destination, std::streamsize a_count) override
		{
			const auto requested = static_cast<std::size_t>(a_count);
			const auto available = contents.size() - offset;
			const auto count = std::min(requested, available);
			std::memcpy(a_destination, contents.data() + offset, count);
			offset += count;
			return static_cast<std::streamsize>(count);
		}

		int_type underflow() override
		{
			return offset < contents.size() ?
			           traits_type::to_int_type(contents[offset]) :
			           traits_type::eof();
		}

	private:
		std::string contents;
		std::size_t offset = 0u;
	};

	bool TestCacheReusePolicy()
	{
		using CSX::MeshBlendingPolicy::CachedClassification;
		using CSX::MeshBlendingPolicy::CanReuseCacheHit;

		return CanReuseCacheHit(CachedClassification::kRejected, true, false, true) &&
		       CanReuseCacheHit(CachedClassification::kAllowedByRule, false, false, true) &&
		       !CanReuseCacheHit(CachedClassification::kAllowedByRule, true, true, true) &&
		       CanReuseCacheHit(CachedClassification::kAutomatic, false, true, true) &&
		       !CanReuseCacheHit(CachedClassification::kAutomatic, false, false, true) &&
		       !CanReuseCacheHit(CachedClassification::kAutomatic, true, true, true);
	}

	bool TestCurrentRuleIdentity()
	{
		using namespace CSX::MeshBlendingPolicy;
		const std::array<NodePathPart, 4> original{
			NodePathPart{ "Root", 0u }, { "Branch", 1u }, { "Parent", 2u }, { "Source", 3u }
		};
		const RuleIdentity cached{ NormalizePath("Data\\Meshes\\Example.nif", true), BuildCanonicalNodePath(original) };
		if (cached.nodePath != "root/branch[1]/parent[2]/source[3]")
			return false;
		const auto reusable = [&](CachedClassification classification, const RuleIdentity& current) {
			return CanReuseCacheHit(classification, false, true,
				!current.nodePath.empty() && cached == current);
		};
		if (!reusable(CachedClassification::kAllowedByRule, cached) ||
			!reusable(CachedClassification::kAutomatic, cached))
			return false;

		// Mutations preserve the source, immediate-parent and root pointer identities.
		for (const auto index : { 0u, 1u, 2u, 3u }) {
			auto renamed = original;
			renamed[index].name = "Denied";
			const RuleIdentity current{ cached.model, BuildCanonicalNodePath(renamed) };
			if (reusable(CachedClassification::kAllowedByRule, current) ||
				reusable(CachedClassification::kAutomatic, current) ||
				!reusable(CachedClassification::kRejected, current))
				return false;
		}
		for (const auto index : { 1u, 2u, 3u }) {
			auto reindexed = original;
			++reindexed[index].parentIndex;
			const RuleIdentity current{ cached.model, BuildCanonicalNodePath(reindexed) };
			if (reusable(CachedClassification::kAllowedByRule, current) ||
				reusable(CachedClassification::kAutomatic, current))
				return false;
		}
		const std::array<NodePathPart, 5> reparented{
			original[0], { "NewAncestor", 4u }, original[1], original[2], original[3]
		};
		const RuleIdentity currentAncestry{ cached.model, BuildCanonicalNodePath(reparented) };
		const RuleIdentity currentModel{ "meshes/denied.nif", cached.nodePath };
		const RuleIdentity unresolved{ cached.model, {} };
		const RuleIdentity normalized{ NormalizePath("meshes/example.nif", true), cached.nodePath };
		return !reusable(CachedClassification::kAllowedByRule, currentAncestry) &&
		       !reusable(CachedClassification::kAutomatic, currentAncestry) &&
		       !reusable(CachedClassification::kAllowedByRule, currentModel) &&
		       !reusable(CachedClassification::kAutomatic, currentModel) &&
		       !reusable(CachedClassification::kAllowedByRule, unresolved) &&
		       !reusable(CachedClassification::kAutomatic, unresolved) &&
		       reusable(CachedClassification::kAllowedByRule, normalized) &&
		       reusable(CachedClassification::kRejected, unresolved);
	}

	bool TestLandscapeSelectorPolicy()
	{
		using CSX::MeshBlendingPolicy::HasLandscapeSelector;
		using CSX::MeshBlendingPolicy::TrimAsciiSpaces;

		return !HasLandscapeSelector({}, {}, {}) &&
		       !HasLandscapeSelector("   ", " ", "  ") &&
		       HasLandscapeSelector(" 0x1~Plugin.esp ", {}, {}) &&
		       HasLandscapeSelector({}, " EditorID ", {}) &&
		       HasLandscapeSelector({}, {}, " textures/example.dds ") &&
		       TrimAsciiSpaces("  value  ") == "value" &&
		       TrimAsciiSpaces("   ").empty();
	}

	bool TestCanonicalOverridePolicy()
	{
		using CSX::MeshBlendingPolicy::CanonicalizeOverrideSelectors;

		const auto valid = CanonicalizeOverrideSelectors(
			"Data//Meshes/Architecture/Test.NIF/",
			"Root///Child/");
		if (!valid.IsExactPair() || valid.model != "meshes/architecture/test.nif" ||
			valid.nodePath != "root/child") {
			return false;
		}

		for (const std::string_view model : { "/", "./", "data/", "meshes/", "////" }) {
			const auto canonical = CanonicalizeOverrideSelectors(model, "Root/Child");
			if (!canonical.ModelCollapsed() || canonical.IsExactPair()) {
				return false;
			}
		}
		for (const std::string_view node : { "/", "./", "////" }) {
			const auto canonical = CanonicalizeOverrideSelectors("meshes/example.nif", node);
			if (!canonical.NodePathCollapsed() || canonical.IsExactPair()) {
				return false;
			}
		}

		const auto wildcard = CanonicalizeOverrideSelectors("meshes/*.nif", "Root/Child");
		const auto otherModel = CanonicalizeOverrideSelectors("meshes/other.nif", "Root/Child");
		const auto otherNode = CanonicalizeOverrideSelectors("meshes/example.nif", "Root/Other");
		const auto stable = CanonicalizeOverrideSelectors(valid.model, valid.nodePath);
		return !wildcard.IsExactPair() &&
		       std::pair{ valid.model, valid.nodePath } != std::pair{ otherModel.model, otherModel.nodePath } &&
		       std::pair{ valid.model, valid.nodePath } != std::pair{ otherNode.model, otherNode.nodePath } &&
		       valid.model == stable.model && valid.nodePath == stable.nodePath;
	}

	bool TestCanonicalLandscapePolicy()
	{
		using CSX::MeshBlendingPolicy::CanonicalizeLandscapeSelectors;

		for (const std::string_view diffuse : { "/", "./", "data/", "textures/", "////" }) {
			const auto canonical = CanonicalizeLandscapeSelectors({}, {}, diffuse);
			if (!canonical.DiffuseCollapsed() || canonical.HasSelector()) {
				return false;
			}
		}

		const auto valid = CanonicalizeLandscapeSelectors(
			" 0x1~Plugin.esp ",
			" EditorID ",
			" Data//Textures/Landscape/Test.DDS/ ");
		const auto stable = CanonicalizeLandscapeSelectors(valid.form, valid.editorID, valid.diffuse);
		return valid.HasSelector() && valid.form == "0x1~Plugin.esp" &&
		       valid.editorID == "EditorID" && valid.diffuse == "textures/landscape/test.dds" &&
		       valid.form == stable.form && valid.editorID == stable.editorID && valid.diffuse == stable.diffuse;
	}

	bool TestBoundedTextRead()
	{
		using Util::BoundedTextRead::Read;
		using Util::BoundedTextRead::Result;

		std::string output;
		CountingStreamBuffer exactBuffer("1234");
		std::istream exact(&exactBuffer);
		if (Read(exact, 4u, output) != Result::Success || output != "1234" ||
			exactBuffer.BytesConsumed() != 4u)
			return false;

		CountingStreamBuffer oversizedBuffer("12345");
		std::istream oversized(&oversizedBuffer);
		if (Read(oversized, 4u, output) != Result::LimitExceeded ||
			oversizedBuffer.BytesConsumed() != 5u)
			return false;

		std::istringstream empty("");
		if (Read(empty, 0u, output) != Result::Success || !output.empty())
			return false;

		const std::string multiChunk(8192u, 'x');
		std::istringstream exactMultiChunk(multiChunk);
		if (Read(exactMultiChunk, multiChunk.size(), output) != Result::Success ||
			output != multiChunk)
			return false;

		std::istringstream broken("content");
		broken.setstate(std::ios::badbit);
		return Read(broken, 32u, output) == Result::ReadError;
	}
}

int main()
{
	return TestCacheReusePolicy() &&
	               TestCurrentRuleIdentity() &&
	               TestLandscapeSelectorPolicy() &&
	               TestCanonicalOverridePolicy() &&
	               TestCanonicalLandscapePolicy() &&
	               TestBoundedTextRead() ?
	           0 :
	           1;
}
