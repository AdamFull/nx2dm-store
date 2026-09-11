#include "store/store_scripting.h"

#include "store/store_service.h"

#include "core/app/module_context.h"
#include "core/script/script_host.h"

#include <memory>

namespace nxm::store {
namespace {

/// Products fetched by the most recent `store_refresh_products()`, read back
/// through index-based getters - Luau bindings only pass scalars, the same
/// reason modio's own scripting layer keeps a shared result slot (see
/// modio_scripting.cpp's AsyncOp) instead of returning a struct.
struct ProductCache {
  nx::vector<StoreProduct> products;
};

/// Only nx::string_view has a script value_traits specialization
/// (script_expose.h) - an owning nx::string returned by value from
/// StoreCloudSaves::read() has nowhere to live once the call returns, so it
/// is stashed here and the view into it handed back instead, the same
/// reason modio's own bindings keep AsyncOp::text alive across the call.
struct CloudSaveCache {
  nx::string last_read;
};

}

void expose_store_services(nxe::script::Host &host, nxe::ModuleContext &ctx) {
  host.expose_as("store_name", [&ctx]() -> nx::string_view {
    StoreCore *const core = ctx.services().find<StoreCore>(kCoreService);
    return core == nullptr ? nx::string_view{} : core->store_name();
  });

  host.expose_as("store_is_owned", [&ctx](const nx::string_view dlc_id) {
    StoreCore *const core = ctx.services().find<StoreCore>(kCoreService);
    return core != nullptr && core->is_owned(dlc_id);
  });

  // -- Achievements -------------------------------------------------------

  host.expose_as("store_unlock_achievement", [&ctx](const nx::string_view id) {
    StoreAchievements *const achievements =
        ctx.services().find<StoreAchievements>(kAchievementsService);
    return achievements != nullptr && achievements->unlock(id);
  });

  host.expose_as("store_is_achievement_unlocked",
                 [&ctx](const nx::string_view id) {
                   const StoreAchievements *const achievements =
                       ctx.services().find<StoreAchievements>(
                           kAchievementsService);
                   return achievements != nullptr &&
                          achievements->is_unlocked(id);
                 });

  // -- IAP ------------------------------------------------------------

  const auto products = std::make_shared<ProductCache>();

  host.expose_as("store_refresh_products", [&ctx, products]() {
    StoreIap *const iap = ctx.services().find<StoreIap>(kIapService);
    if (iap == nullptr)
      return false;
    products->products = iap->products();
    return true;
  });

  host.expose_as("store_product_count", [products]() {
    return static_cast<f64>(products->products.size());
  });

  host.expose_as("store_product_id",
                 [products](const f64 index) -> nx::string_view {
                   const usize i = nx::cast<usize>(index);
                   return i < products->products.size()
                              ? products->products[i].id.view()
                              : nx::string_view{};
                 });

  host.expose_as("store_product_title",
                 [products](const f64 index) -> nx::string_view {
                   const usize i = nx::cast<usize>(index);
                   return i < products->products.size()
                              ? products->products[i].title.view()
                              : nx::string_view{};
                 });

  host.expose_as("store_product_price",
                 [products](const f64 index) -> nx::string_view {
                   const usize i = nx::cast<usize>(index);
                   return i < products->products.size()
                              ? products->products[i].price_display.view()
                              : nx::string_view{};
                 });

  host.expose_as("store_purchase", [&ctx](const nx::string_view product_id) {
    StoreIap *const iap = ctx.services().find<StoreIap>(kIapService);
    return iap != nullptr && iap->purchase(product_id);
  });

  host.expose_as("store_purchase_pending", [&ctx]() {
    const StoreIap *const iap = ctx.services().find<StoreIap>(kIapService);
    return iap != nullptr && iap->purchase_pending();
  });

  host.expose_as("store_purchase_error", [&ctx]() -> nx::string_view {
    const StoreIap *const iap = ctx.services().find<StoreIap>(kIapService);
    return iap == nullptr ? nx::string_view{} : iap->purchase_error();
  });

  // -- Cloud saves ----------------------------------------------------

  const auto cloud_saves = std::make_shared<CloudSaveCache>();

  host.expose_as("store_cloud_save_write",
                 [&ctx](const nx::string_view key,
                        const nx::string_view value) {
                   StoreCloudSaves *const saves =
                       ctx.services().find<StoreCloudSaves>(kCloudSavesService);
                   return saves != nullptr && saves->write(key, value);
                 });

  host.expose_as(
      "store_cloud_save_read",
      [&ctx, cloud_saves](const nx::string_view key) -> nx::string_view {
        const StoreCloudSaves *const saves =
            ctx.services().find<StoreCloudSaves>(kCloudSavesService);
        cloud_saves->last_read = saves == nullptr ? nx::string{} : saves->read(key);
        return cloud_saves->last_read.view();
      });

  // -- Presence ---------------------------------------------------------

  host.expose_as("store_set_presence", [&ctx](const nx::string_view text) {
    StorePresence *const presence =
        ctx.services().find<StorePresence>(kPresenceService);
    return presence != nullptr && presence->set_status(text);
  });

  host.expose_as("store_friend_count", [&ctx]() {
    const StorePresence *const presence =
        ctx.services().find<StorePresence>(kPresenceService);
    return presence == nullptr ? 0.0 : static_cast<f64>(presence->friend_count());
  });
}

}
