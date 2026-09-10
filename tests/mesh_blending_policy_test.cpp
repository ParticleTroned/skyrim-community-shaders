#include "Features/MeshBlendingPolicy.h"
#include "Utils/BoundedTextRead.h"

#include <algorithm>
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

		return CanReuseCacheHit(CachedClassification::kRejected, true, false) &&
		       CanReuseCacheHit(CachedClassification::kAllowedByRule, false, false) &&
		       !CanReuseCacheHit(CachedClassification::kAllowedByRule, true, true) &&
		       CanReuseCacheHit(CachedClassification::kAutomatic, false, true) &&
		       !CanReuseCacheHit(CachedClassification::kAutomatic, false, false) &&
		       !CanReuseCacheHit(CachedClassification::kAutomatic, true, true);
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
	               TestLandscapeSelectorPolicy() &&
	               TestCanonicalOverridePolicy() &&
	               TestCanonicalLandscapePolicy() &&
	               TestBoundedTextRead() ?
	           0 :
	           1;
}
