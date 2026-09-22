#include "framework/nxtest.h"

#include "engine/stub_platform.h"

#include "app/engine.h"
#include "core/foundation/core/scope_guard.h"
#include "script/luau/luau_backend.h"
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

  [[nodiscard]] const script::Host::ServiceInfo *
  find(const nx::string_view name) const {
    for (const script::Host::ServiceInfo &one : services)
      if (one.name == name)
        return &one;
    return nullptr;
  }
};

} // namespace

// This is the one place a mismatch between what store_scripting.cpp actually
// registers and the shape a script is told about would show up - see
// test_modio_scripting.cpp's identical role for modio. No backend is
// registered in this harness (ctx.services().find<T>() returns null for
// every neutral service), so every callable here just exercises its own
// "no backend" refusal path, not real backend behavior.
TEST_CASE("store scripting: every service is exposed with the shape a "
          "script is told about") {
  const Exposed exposed;

  static constexpr struct {
    nx::string_view name;
    nx::string_view signature;
  } WANT[] = {
      {"store_name", "()->(string)"},
      {"store_is_owned", "(string)->(boolean)"},
      {"store_refresh_owned_dlc", "()->(boolean)"},
      {"store_refresh_dlc_ownership", "(string)->(boolean)"},
      {"store_owned_dlcs", "()->({string})"},
      {"store_owned_dlc_count", "()->(number)"},
      {"store_owned_dlc_id", "(number)->(string)"},
      {"store_unlock_achievement", "(string)->(boolean)"},
      {"store_is_achievement_unlocked", "(string)->(boolean)"},
      {"store_refresh_achievement_ids", "()->(boolean)"},
      {"store_refresh_stat", "(string)->(boolean)"},
      {"store_achievements", "()->({string})"},
      {"store_achievement_count", "()->(number)"},
      {"store_achievement_id", "(number)->(string)"},
      {"store_set_stat", "(string,number)->(boolean)"},
      {"store_stat", "(string)->(number)"},
      {"store_set_product_id", "(string)->(boolean)"},
      {"store_clear_product_ids", "()->(boolean)"},
      {"store_refresh_products", "()->(boolean)"},
      {"store_products", "()->({{id: string, title: string, price: string}})"},
      {"store_product_count", "()->(number)"},
      {"store_product_id", "(number)->(string)"},
      {"store_product_title", "(number)->(string)"},
      {"store_product_price", "(number)->(string)"},
      {"store_purchase", "(string)->(boolean)"},
      {"store_purchase_pending", "()->(boolean)"},
      {"store_purchase_error", "()->(string)"},
      {"store_cloud_save_write", "(string,string)->(boolean)"},
      {"store_cloud_save_read", "(string)->(string)"},
      {"store_cloud_save_exists", "(string)->(boolean)"},
      {"store_cloud_save_remove", "(string)->(boolean)"},
      {"store_refresh_cloud_save_keys", "()->(boolean)"},
      {"store_cloud_save_keys", "()->({string})"},
      {"store_cloud_save_key_count", "()->(number)"},
      {"store_cloud_save_key", "(number)->(string)"},
      {"store_cloud_bytes_used", "()->(number)"},
      {"store_cloud_bytes_total", "()->(number)"},
      {"store_set_presence", "(string)->(boolean)"},
      {"store_friends", "()->({string})"},
      {"store_friend_count", "()->(number)"},
      {"store_own_name", "()->(string)"},
      {"store_refresh_friend_names", "()->(boolean)"},
      {"store_friend_name", "(number)->(string)"},
  };

  CHECK(exposed.services.size() == nx::array_size(WANT));
  for (const auto &want : WANT) {
    const script::Host::ServiceInfo *const found = exposed.find(want.name);
    REQUIRE(found != nullptr);
    CHECK(found->signature == want.signature);
  }
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
