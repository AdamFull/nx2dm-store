#include "framework/nxtest.h"

#include "core/app/engine.h"
#include "core/script/script_host.h"
#include "store/store_scripting.h"

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
      {"store_owned_dlc_count", "()->(number)"},
      {"store_owned_dlc_id", "(number)->(string)"},
      {"store_unlock_achievement", "(string)->(boolean)"},
      {"store_is_achievement_unlocked", "(string)->(boolean)"},
      {"store_refresh_achievement_ids", "()->(boolean)"},
      {"store_refresh_stat", "(string)->(boolean)"},
      {"store_achievement_count", "()->(number)"},
      {"store_achievement_id", "(number)->(string)"},
      {"store_set_stat", "(string,number)->(boolean)"},
      {"store_stat", "(string)->(number)"},
      {"store_set_product_id", "(string)->(boolean)"},
      {"store_clear_product_ids", "()->(boolean)"},
      {"store_refresh_products", "()->(boolean)"},
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
      {"store_cloud_save_key_count", "()->(number)"},
      {"store_cloud_save_key", "(number)->(string)"},
      {"store_cloud_bytes_used", "()->(number)"},
      {"store_cloud_bytes_total", "()->(number)"},
      {"store_set_presence", "(string)->(boolean)"},
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
