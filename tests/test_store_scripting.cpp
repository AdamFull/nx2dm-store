#include "framework/nxtest.h"

#include "engine/stub_platform.h"

#include "app/engine.h"
#include "core/foundation/core/scope_guard.h"
#include "core/foundation/platform/filesystem.h"
#include "script/luau/luau_backend.h"
#include "script/luau/luau_bindings.h"
#include "script/script_host.h"
#include "store/store_scripting.h"
#include "store/store_service.h"

namespace {

using namespace nxm::store;
namespace script = nxe::script;

struct Exposed {
  nxe::Engine engine{nxe::Game{}};
  nxe::ModuleContext ctx{engine};
  script::Host host;
  nx::vector<script::Host::ServiceInfo> services;

  Exposed() {
    expose_store_services(host, ctx);
    services = host.services();
  }
};

} // namespace

TEST_CASE("store scripting: every service is exposed as script-services.json "
          "declares it") {
  const Exposed exposed;
  const auto manifest = nx::fs::file_read_text(
      nx::fs::path_view(NX_MODULE_SERVICES_MANIFEST));
  REQUIRE(manifest);

  nx::string error;
  if (!script::luau_manifest_agrees(manifest.value(), exposed.services, error))
    FAIL(error.c_str());
}

namespace {

nx::vector<nx::string> strings(std::initializer_list<nx::string_view> items) {
  nx::vector<nx::string> out;
  for (const nx::string_view item : items)
    out.emplace_back(item);
  return out;
}

struct FakeCore final : StoreCore {
  bool is_owned(nx::string_view) const override { return true; }
  nx::vector<nx::string> owned_dlc_ids() const override {
    return strings({"dlc_a", "dlc_b"});
  }
  nx::string_view store_name() const noexcept override { return "fake"; }
};

struct FakeIap final : StoreIap {
  nx::vector<StoreProduct> products() const override {
    return {{nx::string("gems"), nx::string("Gems"), nx::string("$1")},
            {nx::string("coins"), nx::string("Coins"), nx::string("$2")}};
  }
  bool purchase(nx::string_view) override { return false; }
  bool purchase_pending() const override { return false; }
  nx::string_view purchase_error() const override { return {}; }
};

struct FakeAchievements final : StoreAchievements {
  bool unlock(nx::string_view) override { return true; }
  bool is_unlocked(nx::string_view) const override { return false; }
  nx::vector<nx::string> achievement_ids() const override {
    return strings({"first_win"});
  }
  bool set_stat(nx::string_view, f64) override { return true; }
  f64 stat(nx::string_view) const override { return 0.0; }
};

struct FakeCloudSaves final : StoreCloudSaves {
  bool write(nx::string_view, nx::string_view) override { return true; }
  nx::string read(nx::string_view) const override { return {}; }
  bool exists(nx::string_view) const override { return false; }
  bool remove(nx::string_view) override { return true; }
  nx::vector<nx::string> keys() const override {
    return strings({"slot1", "slot2", "slot3"});
  }
  u64 bytes_used() const override { return 0; }
  u64 bytes_total() const override { return 0; }
};

struct FakePresence final : StorePresence {
  bool set_status(nx::string_view) override { return true; }
  nx::string_view own_name() const override { return "me"; }
  usize friend_count() const override { return 2; }
  nx::vector<nx::string> friend_names() const override {
    return strings({"ann", "bob"});
  }
};

constexpr nxe::ModuleService FAKE_SERVICES[] = {
    {.id = kCoreService, .version = {1, 0, 0}},
    {.id = kIapService, .version = {1, 0, 0}},
    {.id = kAchievementsService, .version = {1, 0, 0}},
    {.id = kCloudSavesService, .version = {1, 0, 0}},
    {.id = kPresenceService, .version = {1, 0, 0}},
};

/// A store backend with fixed data, registered the way a real one is.
class FakeStoreModule final : public nxe::Module {
public:
  [[nodiscard]] nxe::ModuleDescriptor descriptor() const noexcept override {
    nxe::ModuleDescriptor out{};
    out.id = "store_fake";
    out.version = {1, 0, 0};
    out.provided_services = FAKE_SERVICES;
    return out;
  }

  bool on_register(nxe::ModuleContext &ctx) override {
    nxe::ServiceRegistrar registrar = ctx.service_registrar();
    StoreCore &core = m_core;
    StoreIap &iap = m_iap;
    StoreAchievements &achievements = m_achievements;
    StoreCloudSaves &saves = m_saves;
    StorePresence &presence = m_presence;
    return registrar.provide(kCoreService, FAKE_SERVICES[0].version, core) &&
           registrar.provide(kIapService, FAKE_SERVICES[1].version, iap) &&
           registrar.provide(kAchievementsService, FAKE_SERVICES[2].version,
                             achievements) &&
           registrar.provide(kCloudSavesService, FAKE_SERVICES[3].version,
                             saves) &&
           registrar.provide(kPresenceService, FAKE_SERVICES[4].version,
                             presence);
  }

private:
  FakeCore m_core;
  FakeIap m_iap;
  FakeAchievements m_achievements;
  FakeCloudSaves m_saves;
  FakePresence m_presence;
};

void quiet_configure(nxe::EngineConfig &config) {
  config.calibrate = false;
  config.action_map = {};
  config.schedule = {};
  config.frame_file = {};
  config.ui_styles = {};
  config.physics = false;
  config.physics_rules = {};
  config.audio = false;
  config.imgui = false;
}

} // namespace

// The lists are the same refreshed data the index getters read, whole.
TEST_CASE("store scripting: a refreshed list comes back whole") {
  nxe::test::StubPlatform platform;
  FakeStoreModule fake;
  nxe::Engine engine{nxe::Game{.configure = quiet_configure}};
  engine.add_module(&fake);
  nxe::rt::AppConfig config;
  engine.configure(config);
  if (!engine.on_create(platform))
    SKIP("no usable RHI device");
  const nx::scope_guard destroy([&] { engine.on_destroy(); });
  nxe::ModuleContext ctx{engine};

  script::Host host;
  REQUIRE(host.set_backend(script::luau_backend()));
  expose_store_services(host, ctx);
  REQUIRE(host.bind());
  const nx::string_view source = R"(
local function same(list, count, at)
  assert(#list == count(), "length")
  for i = 1, #list do
    assert(list[i] == at(i - 1), "item " .. i)
  end
end

assert(host.store_refresh_owned_dlc())
same(host.store_owned_dlcs(), host.store_owned_dlc_count, host.store_owned_dlc_id)
assert(host.store_refresh_achievement_ids())
same(host.store_achievements(), host.store_achievement_count,
     host.store_achievement_id)
assert(host.store_refresh_cloud_save_keys())
same(host.store_cloud_save_keys(), host.store_cloud_save_key_count,
     host.store_cloud_save_key)
assert(host.store_refresh_friend_names())
same(host.store_friends(), host.store_friend_count, host.store_friend_name)

assert(host.store_refresh_products())
local products = host.store_products()
assert(#products == host.store_product_count() and #products == 2, "products")
for i, product in products do
  assert(product.id == host.store_product_id(i - 1), "id")
  assert(product.title == host.store_product_title(i - 1), "title")
  assert(product.price == host.store_product_price(i - 1), "price")
end
assert(products[2].title == "Coins", "record content")
return {}
)";
  CHECK(host.load("store_lists",
                  {reinterpret_cast<const std::byte *>(source.data()),
                   source.size()}));
}

TEST_CASE("store scripting: the module hands them over on its own") {
  std::unique_ptr<nxe::Module> found;
  for (const nxe::ModuleFactory factory : nxe::enabled_module_factories()) {
    std::unique_ptr<nxe::Module> module = factory();
    if (module != nullptr && module->name() == "store")
      found = std::move(module);
  }
  REQUIRE(found != nullptr);

  nxe::Engine engine{nxe::Game{}};
  nxe::ModuleContext ctx{engine};
  script::Host host;
  found->on_expose_scripts(host, ctx);

  script::Host direct;
  expose_store_services(direct, ctx);
  CHECK(host.exposed_count() == direct.exposed_count());
  CHECK(host.exposed_count() > 0u);
}
