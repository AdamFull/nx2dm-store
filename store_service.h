#pragma once

#include "core/foundation/core/foundation.h"
#include "core/foundation/strings/utf8_string.h"

namespace nxm::store {

// Five independently-optional services - a backend module (store_steam,
// store_eos, ...) registers only the ones its SDK actually has. A store
// without cloud saves or presence (Stove's SDK ships neither) simply never
// registers StoreCloudSaves/StorePresence, and
// nxe::ServiceProvider::find<T>() already returns null for an absent
// service - the same shape nxe::ModuleServiceDependency::optional already
// uses elsewhere, not a new capability-flag system. See modules/store/README.md.

inline constexpr nx::string_view kCoreService = "store.core";
inline constexpr nx::string_view kIapService = "store.iap";
inline constexpr nx::string_view kAchievementsService = "store.achievements";
inline constexpr nx::string_view kCloudSavesService = "store.cloud_saves";
inline constexpr nx::string_view kPresenceService = "store.presence";

/// Entitlement/ownership checks. The one service every backend provides.
class StoreCore {
public:
  virtual ~StoreCore() = default;

  /// The signed-in user owns the base app, or - when `dlc_id` is set - that
  /// DLC specifically.
  [[nodiscard]] virtual bool is_owned(nx::string_view dlc_id = {}) const = 0;

  /// DLC ids the signed-in user actually owns - not every DLC the app has
  /// (a backend's DLC catalogue can include unowned, purchasable entries).
  [[nodiscard]] virtual nx::vector<nx::string> owned_dlc_ids() const = 0;

  [[nodiscard]] virtual nx::string_view store_name() const noexcept = 0;
};

struct StoreProduct {
  nx::string id;
  nx::string title;
  nx::string price_display;
};

/// Product listing and the purchase flow. Purchasing is async: `purchase()`
/// only begins one; poll `purchase_pending()`/`purchase_error()` for the
/// result, the same shared-slot idiom modio's own script bindings already
/// use for async operations.
class StoreIap {
public:
  virtual ~StoreIap() = default;

  [[nodiscard]] virtual nx::vector<StoreProduct> products() const = 0;
  virtual bool purchase(nx::string_view product_id) = 0;
  [[nodiscard]] virtual bool purchase_pending() const = 0;
  /// Empty when the most recently finished purchase succeeded (or none has
  /// run yet).
  [[nodiscard]] virtual nx::string_view purchase_error() const = 0;
};

class StoreAchievements {
public:
  virtual ~StoreAchievements() = default;

  virtual bool unlock(nx::string_view id) = 0;
  [[nodiscard]] virtual bool is_unlocked(nx::string_view id) const = 0;
  [[nodiscard]] virtual nx::vector<nx::string> achievement_ids() const = 0;

  /// Numeric stats, not achievement flags - e.g. "enemies_killed". A backend
  /// maps this onto whatever numeric-stat storage its SDK has (Steam's own
  /// stats are declared int or float on its backend; callers here never need
  /// to know which - an implementation tries both).
  virtual bool set_stat(nx::string_view id, f64 value) = 0;
  [[nodiscard]] virtual f64 stat(nx::string_view id) const = 0;
};

/// Small key/value cloud saves - not a file API. A backend maps this onto
/// whatever its SDK actually offers (Steam Cloud, EOS Player Data Storage,
/// ...).
class StoreCloudSaves {
public:
  virtual ~StoreCloudSaves() = default;

  virtual bool write(nx::string_view key, nx::string_view value) = 0;
  [[nodiscard]] virtual nx::string read(nx::string_view key) const = 0;
  [[nodiscard]] virtual bool exists(nx::string_view key) const = 0;
  virtual bool remove(nx::string_view key) = 0;
  [[nodiscard]] virtual nx::vector<nx::string> keys() const = 0;
  [[nodiscard]] virtual u64 bytes_used() const = 0;
  [[nodiscard]] virtual u64 bytes_total() const = 0;
};

/// Rich presence + friends.
class StorePresence {
public:
  virtual ~StorePresence() = default;

  virtual bool set_status(nx::string_view text) = 0;
  [[nodiscard]] virtual nx::string_view own_name() const = 0;
  [[nodiscard]] virtual usize friend_count() const = 0;
  [[nodiscard]] virtual nx::vector<nx::string> friend_names() const = 0;
};

}
