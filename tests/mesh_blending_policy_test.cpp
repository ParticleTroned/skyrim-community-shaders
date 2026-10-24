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
		       CanReuseCacheHit(CachedClassification::kDetectedByRule, false, true, true) &&
		       !CanReuseCacheHit(CachedClassification::kDetectedByRule, false, false, true) &&
		       !CanReuseCacheHit(CachedClassification::kDetectedByRule, true, true, true) &&
		       !CanReuseCacheHit(CachedClassification::kDetectedByRule, false, true, false) &&
		       CanReuseCacheHit(CachedClassification::kAutomatic, false, true, true) &&
		       !CanReuseCacheHit(CachedClassification::kAutomatic, false, false, true) &&
		       !CanReuseCacheHit(CachedClassification::kAutomatic, true, true, true);
	}

	bool TestGeneratedRuleTrust()
	{
		using namespace CSX::MeshBlendingPolicy;
		CompiledRules rules;
		rules.AddDetected("Data\\Meshes\\Example.nif", "Root/Source[1]");
		rules.AddDetected("meshes/*.nif", "Root/Source[1]");
		rules.AddDetected("meshes/example.nif", "*");
		rules.AddDetected("/", "Root/Source[1]");
		rules.BuildNodeIndex();
		if (rules.detected.size() != 1u || rules.authored.Size() != 0u ||
			!rules.HasAllowRules() || !rules.HasAnyRules() || !rules.NeedsNodePath("meshes/example.nif") ||
			rules.Match("meshes/example.nif", "root/source[1]") != RuleMatch::kDetected ||
			rules.Match("meshes/example.nif", "root/other[1]") != RuleMatch::kNone ||
			rules.Match("meshes/other.nif", "root/source[1]") != RuleMatch::kNone)
			return false;

		rules.authored.Add("meshes/example.nif", {});
		rules.BuildNodeIndex();
		if (rules.Match("meshes/example.nif", "root/source[1]") != RuleMatch::kAuthored)
			return false;
		rules.denied.Add("meshes/*.nif", "*/source[?]");
		rules.BuildNodeIndex();
		return rules.Match("meshes/example.nif", "root/source[1]") == RuleMatch::kDeny &&
		       rules.Match("meshes/example.nif", "root/other[1]") == RuleMatch::kAuthored &&
		       rules.NeedsNodePath("meshes/other.nif") &&
		       !rules.denied.Matches("meshes/example.nif", {});
	}

	bool TestStreamingIdentity()
	{
		using namespace CSX::MeshBlendingPolicy;
		const std::array<NodePathPart, 4> original{
			NodePathPart{ "Root", 0u }, { "Branch", 1u }, { "Parent", 2u }, { "Source", 3u }
		};
		const RuleIdentity cached{ "meshes/example.nif", BuildNodeCacheIdentity(original) };
		if (!MatchesRuleIdentity("Data\\Meshes\\Example.NIF", original, cached) ||
			MatchesRuleIdentity("meshes/other.nif", original, cached) ||
			MatchesRuleIdentity("meshes/example.nif", {}, cached))
			return false;
		for (std::size_t index = 0u; index < original.size(); ++index) {
			auto renamed = original;
			renamed[index].name = "Other";
			if (MatchesRuleIdentity(cached.model, renamed, cached))
				return false;
			if (index != 0u) {
				auto reindexed = original;
				++reindexed[index].parentIndex;
				if (MatchesRuleIdentity(cached.model, reindexed, cached))
					return false;
			}
		}
		std::string model = "meshes/example.nif";
		std::string name = "Source";
		auto mutablePath = original;
		mutablePath.back().name = name;
		if (!MatchesRuleIdentity(model, mutablePath, cached))
			return false;
		name[0] = 'X';
		if (MatchesRuleIdentity(model, mutablePath, cached))
			return false;
		name[0] = 'S';
		model[7] = 'X';
		return !MatchesRuleIdentity(model, mutablePath, cached);
	}

	bool TestNormalizedPathComparison()
	{
		using namespace CSX::MeshBlendingPolicy;
		const std::array<std::pair<std::string_view, std::string_view>, 10> cases{
			std::pair{ "Data\\\\Meshes//Example.NIF/", "meshes/example.nif" },
			{ "././DATA///Meshes//Example.NIF", "meshes/example.nif" },
			{ "/./Example.nif", "meshes/./example.nif" },
			{ "Example.nif", "meshes/example.nif" },
			{ "data/", "" }, { "meshes/", "" }, { "./", "" }, { "////", "" },
			{ "././/Example.nif", "meshes/example.nif" },
			{ "Data/Data/Example.nif", "meshes/data/example.nif" }
		};
		for (const auto& [input, expected] : cases) {
			if (NormalizePath(input, true) != expected || !MatchesNormalizedModelPath(input, expected) ||
				MatchesNormalizedModelPath(input, std::string(expected) + "x"))
				return false;
			if (!expected.empty() && MatchesNormalizedModelPath(input, expected.substr(0u, expected.size() - 1u)))
				return false;
		}
		return true;
	}

	bool TestLiveReceiverRoute()
	{
		using CSX::MeshBlendingPolicy::ResolveLiveRoute;
		struct Object
		{
			std::array<Object*, 8> children{};
			Object* parent = nullptr;
			std::uint32_t parentIndex = 0u;
			bool culled = false;
		};
		Object root;
		Object branch;
		Object receiver;
		Object replacement;
		root.children[7] = &branch;
		branch.parent = &root;
		branch.parentIndex = 7u;
		branch.children[5] = &receiver;
		receiver.parent = &branch;
		receiver.parentIndex = 5u;
		const std::array route{ 7u, 5u };
		std::size_t visited = 0u;
		const auto child = [&](Object* parent, std::uint32_t index) -> Object* {
			++visited;
			if (index >= parent->children.size())
				return nullptr;
			auto* current = parent->children[index];
			return current && current->parent == parent && current->parentIndex == index ? current : nullptr;
		};
		const auto visible = [](const Object* current) { return !current->culled; };
		if (ResolveLiveRoute(&root, route, &receiver, child) != &receiver || visited != route.size())
			return false;
		for (auto* object : { &root, &branch, &receiver }) {
			object->culled = true;
			visited = 0u;
			if (ResolveLiveRoute(&root, route, &receiver, child, visible) || (object == &root && visited != 0u))
				return false;
			object->culled = false;
			if (ResolveLiveRoute(&root, route, &receiver, child, visible) != &receiver)
				return false;
		}
		branch.children[5] = nullptr;
		if (ResolveLiveRoute(&root, route, &receiver, child))
			return false;
		branch.children[5] = &replacement;
		replacement.parent = &branch;
		replacement.parentIndex = 5u;
		if (ResolveLiveRoute(&root, route, &receiver, child))
			return false;
		branch.children[5] = &receiver;
		receiver.parentIndex = 4u;
		if (ResolveLiveRoute(&root, route, &receiver, child))
			return false;
		receiver.parentIndex = 5u;
		receiver.parent = &root;
		if (ResolveLiveRoute(&root, route, &receiver, child))
			return false;
		receiver.parent = &branch;
		const auto* staleAddress = reinterpret_cast<const Object*>(static_cast<std::uintptr_t>(1u));
		return !ResolveLiveRoute(&root, route, staleAddress, child) &&
		       !ResolveLiveRoute(&root, route, staleAddress, child, visible) &&
		       !ResolveLiveRoute(&root, std::array{ 8u }, &receiver, child) &&
		       !ResolveLiveRoute(static_cast<Object*>(nullptr), route, &receiver, child);
	}

	bool TestBoundedOwnerLookup()
	{
		using CSX::MeshBlendingPolicy::ResolveBoundedOwner;
		struct Object
		{
			Object* parent = nullptr;
			int* owner = nullptr;
			bool linked = true;
		};
		std::array<Object, 65> chain{};
		for (std::size_t index = 0u; index + 1u < chain.size(); ++index)
			chain[index].parent = &chain[index + 1u];
		std::size_t visited = 0u;
		std::size_t parentVisits = 0u;
		const auto ownUserData = [&](const Object* object) {
			++visited;
			return object->owner;
		};
		const auto linkedParent = [&](Object* object) {
			++parentVisits;
			return object->linked ? object->parent : nullptr;
		};
		int owner = 1;
		int nearerOwner = 2;
		chain[63].owner = &owner;
		if (ResolveBoundedOwner(chain.data(), 64u, ownUserData, linkedParent) != &owner || visited != 64u || parentVisits != 63u)
			return false;
		visited = 0u;
		parentVisits = 0u;
		chain[63].owner = nullptr;
		chain[64].owner = &owner;
		if (ResolveBoundedOwner(chain.data(), 64u, ownUserData, linkedParent) || visited != 64u || parentVisits != 63u)
			return false;
		visited = 0u;
		parentVisits = 0u;
		chain[5].owner = &nearerOwner;
		if (ResolveBoundedOwner(chain.data(), 64u, ownUserData, linkedParent) != &nearerOwner || visited != 6u || parentVisits != 5u)
			return false;
		chain[5].owner = nullptr;
		chain[32].linked = false;
		visited = 0u;
		parentVisits = 0u;
		if (ResolveBoundedOwner(chain.data(), 64u, ownUserData, linkedParent) || visited != 33u || parentVisits != 33u)
			return false;
		Object cycle;
		cycle.parent = &cycle;
		visited = 0u;
		parentVisits = 0u;
		if (ResolveBoundedOwner(&cycle, 64u, ownUserData, linkedParent) || visited != 64u || parentVisits != 63u)
			return false;
		visited = 0u;
		parentVisits = 0u;
		return !ResolveBoundedOwner(chain.data(), 0u, ownUserData, linkedParent) &&
		       !ResolveBoundedOwner(static_cast<Object*>(nullptr), 64u, ownUserData, linkedParent) && visited == 0u && parentVisits == 0u;
	}

	bool TestCurrentRuleIdentity()
	{
		using namespace CSX::MeshBlendingPolicy;
		const std::array<NodePathPart, 4> original{
			NodePathPart{ "Root", 0u }, { "Branch", 1u }, { "Parent", 2u }, { "Source", 3u }
		};
		const RuleIdentity cached{ NormalizePath("Data\\Meshes\\Example.nif", true), BuildNodeCacheIdentity(original) };
		if (BuildCanonicalNodePath(original) != "root/branch[1]/parent[2]/source[3]" || cached.nodeIdentity.empty())
			return false;
		const auto reusable = [&](CachedClassification classification, const RuleIdentity& current) {
			return CanReuseCacheHit(classification, false, true,
				!current.nodeIdentity.empty() && cached == current);
		};
		if (!reusable(CachedClassification::kAllowedByRule, cached) ||
			!reusable(CachedClassification::kAutomatic, cached))
			return false;

		// Mutations preserve the source, immediate-parent and root pointer identities.
		for (const auto index : { 0u, 1u, 2u, 3u }) {
			auto renamed = original;
			renamed[index].name = "Denied";
			const RuleIdentity current{ cached.model, BuildNodeCacheIdentity(renamed) };
			if (reusable(CachedClassification::kAllowedByRule, current) ||
				reusable(CachedClassification::kAutomatic, current) ||
				!reusable(CachedClassification::kRejected, current))
				return false;
		}
		for (const auto index : { 1u, 2u, 3u }) {
			auto reindexed = original;
			++reindexed[index].parentIndex;
			const RuleIdentity current{ cached.model, BuildNodeCacheIdentity(reindexed) };
			if (reusable(CachedClassification::kAllowedByRule, current) ||
				reusable(CachedClassification::kAutomatic, current))
				return false;
		}
		const std::array<NodePathPart, 5> reparented{
			original[0], { "NewAncestor", 4u }, original[1], original[2], original[3]
		};
		const RuleIdentity currentAncestry{ cached.model, BuildNodeCacheIdentity(reparented) };
		const RuleIdentity currentModel{ "meshes/denied.nif", cached.nodeIdentity };
		const RuleIdentity unresolved{ cached.model, {} };
		const RuleIdentity normalized{ NormalizePath("meshes/example.nif", true), cached.nodeIdentity };
		return !reusable(CachedClassification::kAllowedByRule, currentAncestry) &&
		       !reusable(CachedClassification::kAutomatic, currentAncestry) &&
		       !reusable(CachedClassification::kAllowedByRule, currentModel) &&
		       !reusable(CachedClassification::kAutomatic, currentModel) &&
		       !reusable(CachedClassification::kAllowedByRule, unresolved) &&
		       !reusable(CachedClassification::kAutomatic, unresolved) &&
		       reusable(CachedClassification::kAllowedByRule, normalized) &&
		       reusable(CachedClassification::kRejected, unresolved);
	}

	bool TestAmbiguousNodeIdentity()
	{
		using namespace CSX::MeshBlendingPolicy;
		const std::array<NodePathPart, 2> ordinary{ NodePathPart{ "Root", 0u }, { "Child", 1u } };
		auto folded = ordinary;
		folded[0].name = "root";
		folded[1].name = "child";
		if (BuildNodeCacheIdentity(ordinary) != BuildNodeCacheIdentity(folded) ||
			BuildCanonicalNodePath(ordinary) != "root/child[1]" ||
			!BuildNodeCacheIdentity({}).empty())
			return false;
		const std::array<NodePathPart, 1> emptyRoot{ NodePathPart{ "", 0u } };
		const std::array<NodePathPart, 1> literalEmptyMarker{ NodePathPart{ "#", 0u } };
		if (BuildNodeCacheIdentity(emptyRoot) == BuildNodeCacheIdentity(literalEmptyMarker) ||
			BuildCanonicalNodePath(emptyRoot) != "#" ||
			!BuildCanonicalNodePath(literalEmptyMarker).empty())
			return false;
		const std::array<NodePathPart, 1> literalHierarchy{ NodePathPart{ "Root/Child[1]", 0u } };
		if (BuildNodeCacheIdentity(ordinary) == BuildNodeCacheIdentity(literalHierarchy) ||
			!BuildCanonicalNodePath(literalHierarchy).empty())
			return false;
		const auto allowed = RuleIdentity{ "meshes/example.nif", BuildNodeCacheIdentity(ordinary) };
		for (const std::string_view name : { "", "#", "Child[1]", "Child/Leaf", "Child\\Leaf", "*", "?", ".", "..", "4:name:1:" }) {
			auto renamed = ordinary;
			renamed[1].name = name;
			const auto current = RuleIdentity{ allowed.model, BuildNodeCacheIdentity(renamed) };
			if (current.nodeIdentity.empty() || current == allowed ||
				CanReuseCacheHit(CachedClassification::kAllowedByRule, false, true, current == allowed) ||
				CanReuseCacheHit(CachedClassification::kAutomatic, false, true, current == allowed))
				return false;
			if (!name.empty() && name != "4:name:1:" && !BuildCanonicalNodePath(renamed).empty())
				return false;
		}
		auto rawPercent = ordinary;
		rawPercent[1].name = "Child%2fLeaf";
		auto rawSlash = ordinary;
		rawSlash[1].name = "Child/Leaf";
		return BuildNodeCacheIdentity(rawPercent) != BuildNodeCacheIdentity(rawSlash) &&
		       BuildCanonicalNodePath(rawPercent) == "root/child%2fleaf[1]" &&
		       !CanMatchNodeSelector("*", {}) && !CanMatchNodeSelector("#", {}) &&
		       CanMatchNodeSelector({}, {}) && CanMatchNodeSelector("*", "root/child[1]");
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

constexpr bool TestDescriptorIsolation()
{
	using namespace CSX::MeshBlendingPolicy;
	static_assert(EnableMask == 0x00800000u);
	static_assert((EnableMask & 0x007FFFFFu) == 0u);
	for (const auto other : { 0u, 1u << 9, 1u << 10, 0x007FFFFFu, 0xFF7FFFFFu }) {
		std::uint32_t shader = other | EnableMask;
		std::uint32_t feature = 0xFFFFFFFFu;
		ClearDrawDescriptors(shader, feature);
		if (shader != other || feature != 0xFFFFFFFFu)
			return false;
		ClearDrawDescriptors(shader, feature);
		if (shader != other || feature != 0xFFFFFFFFu)
			return false;
	}
	return true;
}
static_assert(TestDescriptorIsolation());

int main()
{
	return TestDescriptorIsolation() && TestCacheReusePolicy() &&
	               TestGeneratedRuleTrust() &&
	               TestStreamingIdentity() &&
	               TestNormalizedPathComparison() &&
	               TestLiveReceiverRoute() &&
	               TestBoundedOwnerLookup() &&
	               TestCurrentRuleIdentity() &&
	               TestAmbiguousNodeIdentity() &&
	               TestCanonicalOverridePolicy() &&
	               TestBoundedTextRead() ?
	           0 :
	           1;
}
