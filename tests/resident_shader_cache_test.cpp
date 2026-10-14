#include "Utils/ResidentShaderCache.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <source_location>
#include <stdexcept>
#include <string>

namespace
{
	std::atomic<std::uint64_t> allocationCount{ 0 };
}

void* operator new(std::size_t a_size)
{
	if (auto* memory = std::malloc(a_size ? a_size : 1)) {
		allocationCount.fetch_add(1, std::memory_order_relaxed);
		return memory;
	}
	throw std::bad_alloc();
}

void* operator new[](std::size_t a_size) { return ::operator new(a_size); }
void operator delete(void* a_memory) noexcept { std::free(a_memory); }
void operator delete[](void* a_memory) noexcept { std::free(a_memory); }
void operator delete(void* a_memory, std::size_t) noexcept { std::free(a_memory); }
void operator delete[](void* a_memory, std::size_t) noexcept { std::free(a_memory); }

namespace
{
	void Require(bool a_condition, const std::source_location& a_location = std::source_location::current())
	{
		if (!a_condition)
			throw std::runtime_error("Resident shader cache check failed at line " + std::to_string(a_location.line()));
	}

	struct Key
	{
		const void* shader;
		std::uint32_t type;
		std::uint32_t descriptor;
		friend bool operator==(const Key&, const Key&) = default;
	};

	struct Shader
	{
		int value;
		unsigned* destroyed = nullptr;
		explicit Shader(int a_value, unsigned* a_destroyed = nullptr) : value(a_value), destroyed(a_destroyed) {}
		~Shader()
		{
			if (destroyed)
				++*destroyed;
		}
	};

	using Cache = Util::ResidentShaderCache<Key, Shader>;

	void PositiveHitsAndExactKeys()
	{
		Cache cache;
		auto owner = std::make_shared<const std::uint8_t>();
		auto shader = std::make_shared<Shader>(1);
		int device, source, otherSource;
		const Key key{ &source, 1, 7 };
		Require(!cache.Find(owner, &device, 1, key));
		cache.Store(owner, &device, 1, key, {});
		Require(!cache.Find(owner, &device, 1, key));
		cache.Store(owner, &device, 1, key, shader);
		Require(cache.Find(owner, &device, 1, key) == shader);
		Require(!cache.Find(owner, &device, 1, { &otherSource, 1, 7 }));
		Require(!cache.Find(owner, &device, 1, { &source, 2, 7 }));
		Require(!cache.Find(owner, &device, 1, { &source, 1, 8 }));
		Require(cache.Find(owner, &device, 1, key) == shader);
	}

	void BoundedEntriesAndPositiveUpdates()
	{
		Util::ResidentShaderCache<Key, Shader, 2> cache;
		auto owner = std::make_shared<const std::uint8_t>();
		auto first = std::make_shared<Shader>(1);
		auto second = std::make_shared<Shader>(2);
		auto replacement = std::make_shared<Shader>(3);
		int device, source;
		const Key firstKey{ &source, 1, 1 }, secondKey{ &source, 1, 2 }, thirdKey{ &source, 1, 3 };
		cache.Store(owner, &device, 1, firstKey, first);
		cache.Store(owner, &device, 1, secondKey, second);
		for (std::uint32_t descriptor = 100; descriptor < 120; ++descriptor) {
			const Key missing{ &source, 1, descriptor };
			Require(!cache.Find(owner, &device, 1, missing));
			cache.Store(owner, &device, 1, missing, {});
		}
		cache.Store(owner, &device, 1, firstKey, {});
		Require(cache.Find(owner, &device, 1, firstKey) == first);
		Require(cache.Find(owner, &device, 1, secondKey) == second);
		cache.Store(owner, &device, 1, secondKey, replacement);
		cache.Store(owner, &device, 1, thirdKey, first);
		Require(!cache.Find(owner, &device, 1, firstKey));
		Require(cache.Find(owner, &device, 1, secondKey) == replacement);
		Require(cache.Find(owner, &device, 1, thirdKey) == first);
	}

	void ContextChanges()
	{
		Cache cache;
		auto owner = std::make_shared<const std::uint8_t>();
		auto otherOwner = std::make_shared<const std::uint8_t>();
		auto shader = std::make_shared<Shader>(1);
		int device, otherDevice, source;
		const Key key{ &source, 1, 7 };
		cache.Store(owner, &device, 1, key, shader);
		Require(!cache.Find(owner, &otherDevice, 1, key));
		Require(!cache.Find(owner, &device, 1, key));
		cache.Store(owner, &device, 1, key, shader);
		Require(!cache.Find(owner, &device, 2, key));
		Require(!cache.Find(owner, &device, 1, key));
		cache.Store(owner, &device, 1, key, shader);
		Require(!cache.Find(otherOwner, &device, 1, key));
		Require(!cache.Find(owner, &device, 1, key));
	}

	void TokenOwnershipAndReusedAddress()
	{
		Cache cache;
		auto owner = std::make_shared<std::array<std::uint8_t, 2>>();
		std::shared_ptr<const std::uint8_t> firstToken{ owner, &(*owner)[0] };
		std::shared_ptr<const std::uint8_t> aliasToken{ owner, &(*owner)[1] };
		auto shader = std::make_shared<Shader>(1);
		int device, source;
		const Key key{ &source, 1, 7 };
		cache.Store(firstToken, &device, 1, key, shader);
		Require(firstToken.get() != aliasToken.get());
		Require(cache.Find(aliasToken, &device, 1, key) == shader);

		const std::uint8_t reusedStorage = 0;
		auto makeToken = [&] {
			return std::shared_ptr<const std::uint8_t>{ &reusedStorage, [](const std::uint8_t*) {} };
		};
		auto original = makeToken();
		const auto* address = original.get();
		cache.Store(original, &device, 1, key, shader);
		original.reset();
		auto reused = makeToken();
		Require(reused.get() == address);
		Require(!cache.Find(reused, &device, 1, key));
		cache.Store(reused, &device, 1, key, shader);
		Require(cache.Find(reused, &device, 1, key) == shader);
	}

	void WeakRetirementAndShaderAliases()
	{
		Cache cache;
		auto owner = std::make_shared<const std::uint8_t>();
		std::weak_ptr<const std::uint8_t> observedOwner = owner;
		unsigned destroyed = 0;
		auto shader = std::make_shared<Shader>(1, &destroyed);
		int device, source;
		const Key key{ &source, 1, 7 };
		cache.Store(owner, &device, 1, key, shader);
		Require(owner.use_count() == 1 && shader.use_count() == 1);
		auto retained = cache.Find(owner, &device, 1, key);
		shader.reset();
		Require(destroyed == 0);
		retained.reset();
		Require(destroyed == 1);
		Require(!cache.Find(owner, &device, 1, key));
		owner.reset();
		Require(observedOwner.expired());

		struct ShaderOwner
		{
			Shader first{ 1 }, second{ 2 };
		};
		auto producer = std::make_shared<ShaderOwner>();
		std::shared_ptr<Shader> alias{ producer, &producer->second };
		owner = std::make_shared<const std::uint8_t>();
		cache.Store(owner, &device, 1, key, alias);
		Require(cache.Find(owner, &device, 1, key).get() == &producer->second);
		producer.reset();
		alias.reset();
		Require(!cache.Find(owner, &device, 1, key));
	}

	void CacheOperationsDoNotAllocate()
	{
		auto owner = std::make_shared<const std::uint8_t>();
		auto shader = std::make_shared<Shader>(1);
		int device, otherDevice, source;
		const Key key{ &source, 1, 7 };
		const auto before = allocationCount.load(std::memory_order_relaxed);
		Cache cache;
		cache.Store(owner, &device, 1, key, shader);
		bool correct = true;
		for (std::uint32_t descriptor = 0; descriptor < 100; ++descriptor) {
			correct &= cache.Find(owner, &device, 1, key) == shader;
			cache.Store(owner, &device, 1, { &source, 1, descriptor }, shader);
			cache.Store(owner, &device, 1, key, shader);
		}
		correct &= !cache.Find(owner, &otherDevice, 2, key);
		cache.Store(owner, &otherDevice, 2, key, shader);
		correct &= cache.Find(owner, &otherDevice, 2, key) == shader;
		const auto after = allocationCount.load(std::memory_order_relaxed);
		Require(correct && before == after);
	}
}

int main()
{
	try {
		PositiveHitsAndExactKeys();
		BoundedEntriesAndPositiveUpdates();
		ContextChanges();
		TokenOwnershipAndReusedAddress();
		WeakRetirementAndShaderAliases();
		CacheOperationsDoNotAllocate();
		std::cout << "Resident shader cache checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
