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

/// The same index-into-a-cached-list shape as ProductCache, reused for every
/// other list-returning neutral method (owned DLC ids, achievement ids,
/// cloud save keys, friend names) - each gets its own instance below, only
/// ever populated by its own refresh function.
struct StringListCache {
  nx::vector<nx::string> items;
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

  const auto owned_dlc = std::make_shared<StringListCache>();

  host.expose_as("store_refresh_owned_dlc", [&ctx, owned_dlc]() {
    StoreCore *const core = ctx.services().find<StoreCore>(kCoreService);
    if (core == nullptr)
      return false;
    core->refresh_ownership();
    owned_dlc->items = core->owned_dlc_ids();
    return true;
  });

  /// Refreshes ownership of exactly one DLC id - the only path that reaches
  /// a real result on GOG, whose SDK has no bulk ownership query (see
  /// store::StoreCore::refresh_ownership()). A harmless bulk refresh on
  /// every other backend, which ignores the id.
  host.expose_as("store_refresh_dlc_ownership",
                 [&ctx](const nx::string_view dlc_id) {
                   StoreCore *const core =
                       ctx.services().find<StoreCore>(kCoreService);
                   if (core == nullptr)
                     return false;
                   core->refresh_ownership(dlc_id);
                   return true;
                 });

  host.expose_as("store_owned_dlc_count", [owned_dlc]() {
    return static_cast<f64>(owned_dlc->items.size());
  });

  host.expose_as("store_owned_dlc_id",
                 [owned_dlc](const f64 index) -> nx::string_view {
                   const usize i = nx::cast<usize>(index);
                   return i < owned_dlc->items.size() ? owned_dlc->items[i].view()
                                                       : nx::string_view{};
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

  const auto achievement_ids = std::make_shared<StringListCache>();

  host.expose_as("store_refresh_achievement_ids", [&ctx, achievement_ids]() {
    StoreAchievements *const achievements =
        ctx.services().find<StoreAchievements>(kAchievementsService);
    if (achievements == nullptr)
      return false;
    achievements->refresh();
    achievement_ids->items = achievements->achievement_ids();
    return true;
  });

  /// Refreshes exactly one numeric stat - the only path that reaches a real
  /// result on Stove, whose SDK has no bulk stat query (see
  /// store::StoreAchievements::refresh()). A harmless bulk refresh on every
  /// other backend, which ignores the id.
  host.expose_as("store_refresh_stat", [&ctx](const nx::string_view id) {
    StoreAchievements *const achievements =
        ctx.services().find<StoreAchievements>(kAchievementsService);
    if (achievements == nullptr)
      return false;
    achievements->refresh({nx::string(id)});
    return true;
  });

  host.expose_as("store_achievement_count", [achievement_ids]() {
    return static_cast<f64>(achievement_ids->items.size());
  });

  host.expose_as("store_achievement_id",
                 [achievement_ids](const f64 index) -> nx::string_view {
                   const usize i = nx::cast<usize>(index);
                   return i < achievement_ids->items.size()
                              ? achievement_ids->items[i].view()
                              : nx::string_view{};
                 });

  host.expose_as("store_set_stat",
                 [&ctx](const nx::string_view id, const f64 value) {
                   StoreAchievements *const achievements =
                       ctx.services().find<StoreAchievements>(
                           kAchievementsService);
                   return achievements != nullptr &&
                          achievements->set_stat(id, value);
                 });

  host.expose_as("store_stat", [&ctx](const nx::string_view id) {
    const StoreAchievements *const achievements =
        ctx.services().find<StoreAchievements>(kAchievementsService);
    return achievements == nullptr ? 0.0 : achievements->stat(id);
  });

  // -- IAP ------------------------------------------------------------

  const auto products = std::make_shared<ProductCache>();
  const auto pending_product_ids = std::make_shared<nx::vector<nx::string>>();

  /// Backends with no "list everything" catalogue query (Play Billing, HMS
  /// IAP, Samsung IAP, StoreKit) need their product ids named up front - a
  /// game builds that list here before calling store_refresh_products().
  /// Ignored by every backend with a real catalogue query.
  host.expose_as("store_set_product_id",
                 [pending_product_ids](const nx::string_view id) {
                   pending_product_ids->emplace_back(id);
                   return true;
                 });
  host.expose_as("store_clear_product_ids", [pending_product_ids]() {
    pending_product_ids->clear();
    return true;
  });

  host.expose_as("store_refresh_products", [&ctx, products, pending_product_ids]() {
    StoreIap *const iap = ctx.services().find<StoreIap>(kIapService);
    if (iap == nullptr)
      return false;
    iap->refresh_products(*pending_product_ids);
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

  host.expose_as("store_cloud_save_exists", [&ctx](const nx::string_view key) {
    const StoreCloudSaves *const saves =
        ctx.services().find<StoreCloudSaves>(kCloudSavesService);
    return saves != nullptr && saves->exists(key);
  });

  host.expose_as("store_cloud_save_remove", [&ctx](const nx::string_view key) {
    StoreCloudSaves *const saves =
        ctx.services().find<StoreCloudSaves>(kCloudSavesService);
    return saves != nullptr && saves->remove(key);
  });

  const auto cloud_save_keys = std::make_shared<StringListCache>();

  host.expose_as("store_refresh_cloud_save_keys", [&ctx, cloud_save_keys]() {
    StoreCloudSaves *const saves =
        ctx.services().find<StoreCloudSaves>(kCloudSavesService);
    if (saves == nullptr)
      return false;
    saves->refresh_keys();
    cloud_save_keys->items = saves->keys();
    return true;
  });

  host.expose_as("store_cloud_save_key_count", [cloud_save_keys]() {
    return static_cast<f64>(cloud_save_keys->items.size());
  });

  host.expose_as("store_cloud_save_key",
                 [cloud_save_keys](const f64 index) -> nx::string_view {
                   const usize i = nx::cast<usize>(index);
                   return i < cloud_save_keys->items.size()
                              ? cloud_save_keys->items[i].view()
                              : nx::string_view{};
                 });

  host.expose_as("store_cloud_bytes_used", [&ctx]() {
    const StoreCloudSaves *const saves =
        ctx.services().find<StoreCloudSaves>(kCloudSavesService);
    return saves == nullptr ? 0.0 : static_cast<f64>(saves->bytes_used());
  });

  host.expose_as("store_cloud_bytes_total", [&ctx]() {
    const StoreCloudSaves *const saves =
        ctx.services().find<StoreCloudSaves>(kCloudSavesService);
    return saves == nullptr ? 0.0 : static_cast<f64>(saves->bytes_total());
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

  host.expose_as("store_own_name", [&ctx]() -> nx::string_view {
    const StorePresence *const presence =
        ctx.services().find<StorePresence>(kPresenceService);
    return presence == nullptr ? nx::string_view{} : presence->own_name();
  });

  const auto friend_names = std::make_shared<StringListCache>();

  host.expose_as("store_refresh_friend_names", [&ctx, friend_names]() {
    StorePresence *const presence =
        ctx.services().find<StorePresence>(kPresenceService);
    if (presence == nullptr)
      return false;
    presence->refresh();
    friend_names->items = presence->friend_names();
    return true;
  });

  host.expose_as("store_friend_name",
                 [friend_names](const f64 index) -> nx::string_view {
                   const usize i = nx::cast<usize>(index);
                   return i < friend_names->items.size()
                              ? friend_names->items[i].view()
                              : nx::string_view{};
                 });
}

}
