# store module

The neutral storefront interface. `modules/store` itself talks to no
storefront - it only declares five independently-optional services
(`store_service.h`) and exposes the neutral `host.store_*` Luau surface
(`store_scripting.cpp`) that forwards to whichever backend module is active.
A storefront integration is a separate backend module - `modules/store_steam`,
`modules/store_egs`, `modules/store_gog`, `modules/store_stove`,
`modules/store_microsoft`, `modules/store_google_play` and
`modules/store_app_gallery` are built and verified so far;
`modules/store_galaxy_store` and `modules/store_app_store` are built too but
**unverified** (no compile check - see their own paragraphs below). Amazon
Appstore was deliberately dropped rather than left "still to come" - every
other mobile store this repository targets is now covered.

The five show the same neutral interface accommodating structurally
different SDKs: Steam's calls are mostly synchronous (a local client cache),
while every single EOS call is asynchronous (a plain C completion callback
resolved by `EOS_Platform_Tick`, even for a "simple" read) - `store_egs`
handles this with small per-service caches populated by each query's
callback, with the neutral interface's synchronous methods reading whatever
is cached so far. EOS also splits its own API in two by auth method: a
silent, headless device-id login unlocks achievements/stats/cloud saves/
leaderboards, but entitlements/IAP/presence/friends need a real Epic account
session that has no headless equivalent - `store_egs` attempts the closest
thing (silently reusing a previously cached Epic login, if any) and
degrades those four to empty/`false` when it's unavailable, the same
absent-service shape Stove's missing cloud-saves/presence already
established. GOG Galaxy sits between the two: most calls are synchronous
local-cache reads like Steam's (`IStorage`, `IFriends`'s own persona/friend
accessors, `IStats`'s Get/Set once signed in), but DLC ownership, the
stats/achievements cache load, and the friends list are still listener-based
like EOS's - and unlike either, Galaxy has no per-call return-value error
signaling at all, so `store_gog` checks a thread-local `galaxy::api::
GetError()` after every call that can fail. Its sign-in is also the
narrowest of the three: `IUser::SignInGalaxy()` requires a real, locally
installed and running GOG Galaxy Client - there is no device-id-style
headless option the way EOS has.

STOVE is the outlier of the first four: every one of its callbacks is a **plain C
function pointer with no userdata parameter at all** (contrast EOS's
trailing `void* ClientData` or GOG's listener-object dispatch), so
`store_stove` routes each callback through a static "current instance"
pointer per service class instead - safe only because exactly one store
backend module is ever active in a process at a time, the same invariant
`order_modules()` already enforces below. STOVE also assumes a launcher
already authenticated the user before the game process started (there is no
Login call in its SDK at all, only read-only accessors for the
already-signed-in session), and its achievements are entirely
server-computed from a Stat crossing a goal value the SDK never names -
`StoveAchievements::unlock()` has no honest implementation against this SDK
and always refuses, even though the rest of that service (numeric stats,
reading unlock status, listing achievement ids) works normally. This is a
different kind of gap than a whole service being absent (see below): one
method degraded within an otherwise-real service, the same way
`store_gog`'s `bytes_used()`/`bytes_total()` always report 0 for a missing
quota API while the rest of `StoreCloudSaves` works.

`store_microsoft` is the structural outlier of all five, in three ways at
once. First, it needs no vendor SDK and no per-project config file at all -
the C++/WinRT projection headers for `Windows.Services.Store` already ship
inside the installed Windows SDK, and `StoreContext::GetDefault()` takes no
parameters, resolving everything from the process's own package identity
instead of a developer-supplied id/secret. Second, it has no per-frame pump:
`IAsyncOperation<T>::Completed()` resolves on its own WinRT thread-pool
thread, not from an explicit `RunCallbacks()`-style call this module would
otherwise have to schedule every frame like the other four all do - so its
two services guard their cached state with a mutex instead of relying on
"only ever touched from the game's own tick." Third, and most
fundamentally, its one real precondition is neither a running client nor a
signed-in session but **package identity itself**: `Windows.Services.Store`
flatly refuses to do anything inside an ordinary unpackaged Win32 exe
(`APPMODEL_ERROR_NO_PACKAGE`) - exactly what `nx2d.exe` is, with no MSIX
packaging step anywhere in this project - so `store_microsoft` is
structurally idle in every build this repository can currently produce.
That's a real, verifiable guard condition rather than a hypothetical one:
`GetCurrentPackageFullName()` (a synchronous, side-effect-free Win32 call,
no WinRT involved) is cheap enough that its own test asserts it for real
rather than only asserting the guard path never gets exercised.

`store_google_play` is the first mobile backend, and the first with **no
native/NDK API of its own at all** - Google Play Billing is pure Java
(Kotlin, in this repo's case), confirmed absent from Google's own
documentation and every forum thread on the question. That makes it the
worked example for every other Android mobile store (Huawei App Gallery,
Samsung Galaxy Store - neither ships a native SDK either): a hand-written
shim
(`modules/store_google_play/android/java/com/nx2d/runtime/
NxGooglePlayBilling.kt`) wraps the vendor's Java/Kotlin API as a set of
`@JvmStatic` functions the C++ side calls via JNI, and reports results back
through a matching set of JNI-exported C++ functions the shim calls
directly - the first Java-calls-C++ direction in this codebase (every
existing JNI caller, e.g. haptics, only goes the other way). The JNI
plumbing itself - class/method lookup, string and array marshalling - lives
in `engine/core/foundation/platform/android_jni.h` (`nx::android`), moved
and expanded out of what used to be a runtime-only, haptics-specific
helper, precisely so the next three mobile backends can reuse it rather
than re-inventing it. A module contributes Kotlin/Java sources the same
conditional way it contributes C++ (`android/app/build.gradle.kts`'s
per-module source-dir loop), and its own Gradle dependencies through its
own `modules/store_google_play/android/build.gradle.kts` - see "Android
Gradle wiring" below for the mechanism every mobile backend uses.
`store_google_play`'s shim is never referenced from any
unconditionally-compiled file, so a build without the module carries none
of Billing Library's code, permissions, or dependency weight. Like
`store_gog`, it can't check base-app ownership (the Play
Store itself already gates who can install/run the APK) and like
`store_stove`'s achievements, one operation has no honest mapping: Play
Billing's consumable/durable split doesn't exist in `StoreIap::purchase()`,
so every product is treated as a durable entitlement (`acknowledgePurchase`,
never `consumeAsync`) - a consumable-currency product isn't served by this
backend.

`store_app_gallery` is the second mobile backend, following
`store_google_play`'s recipe exactly - HMS IAP Kit has no native/NDK API
either, so `NxHuaweiIap.kt` is a second Kotlin shim sharing the same
`nx::android` JNI toolkit and `@JvmStatic`/`external fun` boundary shape as
`NxGooglePlayBilling.kt`, and it never registers
`store.achievements`/`store.cloud_saves`/`store.presence` for the same
"the SDK simply doesn't have it" reason (HUAWEI Game Service is a separate
product). Two things about it are genuinely bigger than Google Play's
integration, though. First, HMS IAP Kit's purchase flow (`createPurchaseIntent()`)
*and* its up-front environment check (`isEnvReady()`, which can require
signing into a HUAWEI ID the device has none of) both resolve through
`Activity.onActivityResult()` - Play Billing needs none of that, so
supporting it meant adding a small, module-agnostic extension point to the
always-compiled `NxActivity.java`
(`NxActivity.ActivityResultHandler`/`registerActivityResultHandler()`):
`NxActivity` itself never references `store_app_gallery` by name, a module
self-registers a handler the first time its platform layer initializes, and
a build without the module carries no dispatch overhead at all (an empty
handler list). Second, HMS IAP Kit needs the Huawei AGConnect Gradle plugin
plus a per-app `agconnect-services.json` credential file, mirroring how a
desktop backend needs a vendored SDK even though this one is (like Google
Play) a plain public Maven dependency with no partner-gated download - see
"Android Gradle wiring" below for exactly how that gets threaded through,
entirely from this module's own files. Like
`store_google_play`, it can't check base-app ownership (AppGallery itself
already gates install) and treats every product as a durable, non-consumable
entitlement, the same honest `StoreIap::purchase()` scope limit.

`store_galaxy_store` is the third mobile backend, and the first one this
repository **cannot compile-verify at all**: Samsung IAP has no native/NDK
API either, so `NxSamsungIap.kt` is a third Kotlin shim sharing the same
`nx::android` toolkit and `@JvmStatic`/`external fun` boundary shape as the
other two, and it never registers
`store.achievements`/`store.cloud_saves`/`store.presence` for the same "the
SDK simply doesn't have it" reason - but unlike Google Play Billing and HMS
IAP, which are both plain Maven dependencies, Samsung distributes the SDK
only as a downloadable `.aar` gated behind a Samsung Developer account
login (see "SDK delivery convention" below), so there is no way to fetch it
without one. Every method signature and VO field name here was read from
Samsung's own official IAP programming guide and codelab, not guessed, but
none of it has been run through a real compiler - the same honest
"write it now, unverified" gap the user explicitly signed off on for this
backend. Structurally it's the simplest of the three: `startPayment()`
takes no Activity/request-code pair at all, resolving purely through
`OnPaymentListener` - `IapHelper` manages launching and binding to the
Galaxy Store checkout UI internally, so unlike `store_app_gallery` this
backend needs no `NxActivity.ActivityResultHandler` registration, the same
pure-listener shape Google Play Billing has. Like the other two mobile
backends it can't check base-app ownership (Galaxy Store itself already
gates install) and treats every product as a durable entitlement,
acknowledging every purchase via `acknowledgePurchases()` - both right
after a fresh purchase and for every `AcknowledgedStatus.NOT_ACKNOWLEDGED`
entry a `getOwnedList()` re-query turns up, mirroring
`NxGooglePlayBilling.kt`'s own `acknowledgeIfNeeded()`.

`store_app_store` is the fourth mobile backend and structurally the odd one
out: it targets iOS, not Android, so there is no JNI shim at all -
Objective-C++ (`.mm`) lets its two files call StoreKit directly and be
called back by it directly, the first mobile backend in this family with
no cross-language shim layer of any kind. It uses classic StoreKit
(`SKPaymentQueue`/`SKProduct`/`SKPaymentTransactionObserver`), not the
newer Swift-only StoreKit 2 - the async/await product and transaction APIs
StoreKit 2 adds have no Objective-C bridge at all, and this engine is
C++/Objective-C++, not Swift, the same reason every other major C++ game
engine's iOS IAP integration still goes through classic StoreKit today.
`AppStoreCore`/`AppStoreIap` mirror the other three mobile backends'
eventually-consistent cache shape exactly, just populated from
`SKProductsRequestDelegate`/`SKPaymentTransactionObserver` callbacks
instead of JNI-exported functions, and the same static-current-instance
dispatch convention is kept purely for consistency across the family, not
because Objective-C++ actually needs it (a delegate object could just as
easily hold a raw C++ pointer to the instance it belongs to). It never
registers `store.achievements`/`store.cloud_saves`/`store.presence` for the
same "the SDK simply doesn't have it" reason (Game Center is a separate,
unrelated product); can't check base-app ownership (the App Store already
gates install); and has no restore-purchases API distinct from the
transaction observer itself - `refresh_ownership()` calls
`SKPaymentQueue.restoreCompletedTransactions`, and every restored
transaction arrives through the *same* `updatedTransactions:` callback a
fresh purchase does, distinguished only by `transactionState`. Every
non-consumable purchase is finished (`finishTransaction:`) immediately,
whether fresh or restored - StoreKit, like the other three mobile
backends' own equivalent step, never redelivers a finished transaction
again. This backend is unverified for a different reason than
`store_galaxy_store`: not a missing vendor file, but no Mac/Xcode
anywhere in this environment - it has never been compiled, only written
against Apple's own StoreKit reference documentation and this repository's
existing `.mm` precedent (`engine/core/runtime/sdl/sdl_haptics_ios.mm`,
`engine/core/foundation/vfs/native_async_io_dispatch.mm`) for how
Objective-C++ is already written elsewhere in this codebase. Amazon
Appstore, the remaining mobile storefront, was deliberately not built -
StoreKit's own `.mm`-only pattern above is the template if it's ever
picked back up.

This module never names a concrete store, the same discipline the scripting
backends already keep for language neutrality - `grep`ping this module for a
store name should never turn up a hit.

## The five services

Declared in `store_service.h`, registered by a backend through
`ModuleContext::service_registrar().provide(id, version, service)` and read
through `ModuleContext::services().find<T>(id)` - the same mechanism
`modules/example`, `modules/spine` and `modules/live2d` already use for their
own services.

| id | interface | required? |
|---|---|---|
| `store.core` | `StoreCore` - ownership/entitlement checks, owned DLC listing | every backend provides this |
| `store.iap` | `StoreIap` - product listing + purchase flow | optional |
| `store.achievements` | `StoreAchievements` - achievements + numeric stats | optional |
| `store.cloud_saves` | `StoreCloudSaves` - key/value saves, listing, quota | optional |
| `store.presence` | `StorePresence` - rich presence + a real friends list | optional |

"Optional" means exactly what `ServiceProvider::find<T>` already does for any
absent service: it returns `nullptr`. A backend whose SDK has no cloud-save
or presence subsystem (STOVE, for one - its SDK genuinely has neither, only
a bare `Base_GetCloudSavingPath()` path string with no read/write/list API
attached, and zero friends/presence hits anywhere) simply never registers
`StoreCloudSaves`/`StorePresence`, and every neutral Luau function above
degrades to a safe `false`/empty return. There is no separate capability-flag
API to check first. `store_gog` is the same shape for a different service:
the vendored GOG Galaxy SDK has no purchase/checkout API at all (confirmed
absent from every header - only entitlement checks,
`IApps::IsDlcOwned()`/`IsDlcInstalled()`, which `store.core` already covers),
so it never registers `StoreIap` either. `store_microsoft` registers only
`store.core`/`store.iap` for the same reason: `Windows.Services.Store` has
no achievements/stats/cloud-save/friends API of its own at all (Xbox's
equivalents live in the separate, much larger Xbox Live/GDK SDK).

### Refresh: triggering the query behind a cache

Every getter above except on `store_steam` (whose SDK reads are already
synchronous local-client calls) is backed by a small cache that a backend's
own async SDK query populates - `EgsCore::owned_dlc_ids()`, say, only ever
returns what the last `EOS_Ecom_QueryEntitlements` round trip found. Each of
the five interfaces therefore also declares a `refresh_*` method - a
non-pure virtual with a default no-op body, so a fully synchronous backend
(or a future one) needs no override at all:

- `StoreCore::refresh_ownership(dlc_id = {})` - most backends bulk-refresh
  everything regardless of `dlc_id` (Play Billing/HMS/Samsung IAP/StoreKit/
  Stove/EOS); GOG Galaxy has no bulk query and only checks the one DLC named
  (an empty id is a no-op there).
- `StoreIap::refresh_products(product_ids = {})` - backends with no "list
  everything" query (Play Billing, HMS IAP, Samsung IAP, StoreKit) need
  `product_ids` up front; a backend with a real catalogue query ignores it.
- `StoreAchievements::refresh(stat_ids = {})` - most backends refresh
  definitions/unlock-state/stats together in one shot; Stove has no bulk
  stat query and only refreshes the stats named in `stat_ids` (achievement
  ids/unlock state still refresh in bulk there regardless).
- `StoreCloudSaves::refresh_keys()` / `StorePresence::refresh()` - no ids
  needed, every backend that overrides these bulk-refreshes.

`store/store_scripting.cpp`'s five `store_refresh_*` Luau bindings
(`store_refresh_owned_dlc`, `store_refresh_products`,
`store_refresh_achievement_ids`, `store_refresh_cloud_save_keys`,
`store_refresh_friend_names`) each call the matching `refresh_*` method
before taking their snapshot - a script that never calls one of these gets
`false`/empty forever from the getters on every backend except Steam, the
same way any consumer of an async SDK would if it never triggered the query
in the first place. `store_refresh_dlc_ownership(dlc_id)`,
`store_set_product_id(id)`/`store_clear_product_ids()`, and
`store_refresh_stat(id)` are the three small additions a script needs to
reach the id-scoped paths above (GOG's single-DLC check, the four
mobile-shaped backends' product lookup, and Stove's single-stat refresh) -
harmless bulk refreshes when called against a backend that doesn't need
them.

A backend can also expose capabilities that don't belong in this neutral
interface at all - Steam's Workshop, leaderboards, and explicit overlay
control (`modules/store_steam/store_steam_workshop.h`,
`store_steam_leaderboards.h`, `store_steam_overlay.h`) are the worked
example. These aren't registered through `ServiceRegistry` (nothing outside
that one backend module will ever look them up) and get their own
`host.store_steam_*` Luau surface from a separate scripting file
(`store_steam_scripting.h/.cpp`), keeping `store/store_scripting.cpp` itself
strictly neutral.

`store_egs` has the same shape for a second real capability set, grounded in
the vendored EOS SDK's own `eos_leaderboards.h`/`eos_ui.h`/`eos_mods.h`
(`store_egs_leaderboards.h`, `store_egs_overlay.h`, `store_egs_mods.h`,
exposed via `store_egs_scripting.h/.cpp`'s `host.store_egs_*` surface) - two
real scope differences from Steam's version worth calling out rather than
treating as gaps. First, EOS Leaderboards has no score-upload API at all: a
leaderboard is tied to a stat name server-side, so submitting a score is
just calling `store_set_stat(stat_name, value)` (the neutral achievements
surface, `EOS_Stats_IngestStat`) - `EgsLeaderboards` only covers the read
side (`EOS_Leaderboards_QueryLeaderboardRanks`, simpler than Steam's own
find-then-download flow since EOS queries by leaderboard id directly and
each returned record already carries rank/score/display name together, no
separate friends lookup needed). Second, EOS Mods only manages
Epic-launcher-installed mods (install/uninstall/update/enumerate) - there is
no publish/upload API the way Steam Workshop has one; installing or
updating a mod needs its full `EOS_Mod_Identifier` (namespace/item/artifact
id + title + version), not a bare id, so `EgsMods` discovers mods through
`EnumerateMods`+`CopyModInfo` first and every mutating call takes an index
into that cached list, the same shape `store/store_scripting.cpp`'s own
`ProductCache` already established for exactly this reason. The
social-overlay control (`EgsOverlay`) is the closest one-to-one match to
Steam's - `ShowBlockPlayer`/`ShowReportPlayer`/`ShowNativeProfile` resolve a
friends-list index to the `EOS_EpicAccountId` they need internally
(`EgsPresence::friend_id_at()`), the same index-not-raw-handle shape
`store_steam_overlay.h`'s own `open_to_friend()` already uses.

`store_stove` has one too, of a completely different shape: PC Bang
detection (`store_stove_pcbang.h`, exposed via `store_stove_scripting.h/.cpp`'s
`host.store_stove_*` surface) - a real Korean-market storefront concept with
no equivalent anywhere else in this family. Korean internet-cafe ("PC bang")
venues get special in-game benefits when the SDK detects the machine is
one. `PCBang_UserLogin` registers two callbacks at once: one fires once with
the login result, the other fires repeatedly on the SDK's own 4-minute
timer with refreshed benefits - there's no separate "refresh" call from the
game side, it's push-based. `PCBang_CheckPCBangStatus` is a second,
independent query that doesn't require a prior login. `StovePCBang`
translates the SDK's four-way `PCBangPremium` code into the two booleans a
game actually needs (`is_pc_bang()`, `is_premium()`) rather than exposing
the raw enum value to Luau.

`store_microsoft`'s own extra is smaller still: a rate-and-review prompt
(`store_microsoft_rate_review.h`, `StoreContext::
RequestRateAndReviewAppAsync`, verified against the real Windows SDK's
C++/WinRT projection headers rather than guessed) that shows the native
Store rating dialog and reports back whether the user actually changed
their rating. `MicrosoftRateReview` follows the exact same mutex-guarded-
cache shape `MicrosoftCore`/`MicrosoftIap` already use, and translates the
SDK's four-way `StoreRateAndReviewStatus` into `succeeded()`/
`canceled_by_user()` the same way `StovePCBang`/`EgsMods` already avoid
leaking a raw enum to Luau.

`order_modules()` already rejects two modules that both declare the same
`provided_services` entry, so if a build somehow enabled two store backends
at once, startup fails loudly (`"exported by both"`) instead of silently
picking one - nothing new needed for "only one store is active."

## Build exclusion

A backend is a normal module: `-DNX_MODULE_STORE_<NAME>=ON`, off by default.
For a project, `python nx.py modules <project> --add store --add store_steam`
adds them to `project.json`'s `"modules"` list, which `nx.py build --project`
now derives `-DNX_MODULE_<NAME>=ON`/`OFF` from **for every known module,
explicitly** - so a Steam-only project structurally cannot end up with any
other backend compiled in, with no manual flag to remember.

## Config convention

Each backend owns its own INI file, matching `modules/modio`'s config
convention exactly: `nx::ini::parse` + `nx::vfs::read_text` against
`/config/store_<name>.ini` (authored at `assets/config/store_<name>.ini`,
passed through the cook pipeline byte-for-byte), read automatically in
`on_attach`. One file per backend, not one shared file, so a project that
only ships to Steam never has to know EOS's config schema, and can
`.gitignore` just the backends whose credentials are genuinely sensitive.
`store_microsoft`, `store_google_play`, `store_app_gallery`,
`store_galaxy_store` and `store_app_store` are the exceptions - none has a
config file, because none has developer-supplied credentials to configure
at *runtime* in the first place: `StoreContext::GetDefault()`,
`BillingClient.newBuilder(context)`, `Iap.getIapClient(activity)`,
`IapHelper.getInstance(context)` and `SKPaymentQueue.defaultQueue` all take
no per-developer id/secret, resolving everything from the process's own
package/bundle identity (and, for `store_google_play`/`store_app_gallery`/
`store_galaxy_store`/`store_app_store`, the installed Play Store/
AppGallery/Galaxy Store client or signed-in Apple ID) instead.
`store_app_gallery`'s one credential, `agconnect-services.json`, is a
**build-time** artifact instead - see "Android Gradle wiring" below;
`store_galaxy_store`'s equivalent is the vendored `.aar` itself - see "SDK
delivery convention" below.

## SDK delivery convention

A storefront SDK is developer-provided and **never committed** - vendors
either gate the download behind a partner account (Steamworks, EOS, GOG
Galaxy, Stove, and now Samsung IAP too) or there's nothing to vendor at all
(Microsoft Store's `Windows.Services.Store` and Apple's StoreKit both ship
with their respective platform SDKs already; Google Play Billing and HMS
IAP are plain public Maven dependencies, no partner account or download
either). Each desktop
backend that needs a vendored SDK follows `modules/live2d`'s precedent for
a big vendor SDK with its own idiosyncratic shape:

- `modules/store_<name>/third_party/` is gitignored per-module.
- A small `NxStore<Name>.cmake` resolves the extracted SDK root
  (`cmake/NxModules.cmake`'s `_nx_resolve_vendor_root`) and builds an
  `IMPORTED SHARED` target for the vendor's DLL/`.so`
  (`_nx_imported_shared_library`, which also stages the runtime file next to
  the built executable).
- A missing SDK is a `FATAL_ERROR` naming the vendor's download page, not a
  silent skip.
- Before that resolution even runs, the module's own `CMakeLists.txt` checks
  it's actually being built for a platform this SDK was ever vendored for
  and `return()`s (a `STATUS` "module skipped" message, not a `FATAL_ERROR`)
  otherwise - `nx_module()`'s own `PLATFORMS` gate runs too late to help,
  since a project's module list isn't per-platform (a project also
  targeting Android still passes `-DNX_MODULE_STORE_GOG=ON` for a
  Windows-only backend, for instance).

`cmake/NxModules.cmake`'s `nx_module_prebuilt()` is a *different* mechanism -
a portable, multi-toolset **static**-lib convention meant for a developer's
own small prebuilt middleware, not for a vendor's own DLL layout. Storefront
backends don't use it.

A mobile backend whose SDK is a plain Maven dependency (`store_google_play`,
`store_app_gallery`) skips CMake-side vendoring entirely: a hand-written
JNI shim replaces `NxStore<Name>.cmake` on the C++ side, and its Gradle
dependency lives in the module's own `android/build.gradle.kts` - see
`store_google_play`'s own paragraph above, "Android Gradle wiring" below,
and `engine/core/foundation/platform/android_jni.h` for the reusable JNI
toolkit every mobile backend shares.

`store_galaxy_store` needs a **third** vendoring shape, for the one SDK in
this family that's both mobile (no native/NDK API, same JNI-shim
architecture as the other two) *and* partner-gated (a real file the
developer must supply, same as the desktop backends): the Samsung IAP
`.aar` goes under `modules/store_galaxy_store/third_party/` - gitignored
via that module's own `.gitignore`, following the same `/third_party/`
pattern as `store_steam`'s/`store_gog`'s own `.gitignore` files, just
holding a `.aar` instead of a native SDK tree - and the module's own
`android/build.gradle.kts` picks it up as a local `fileTree(...)`
dependency rather than a Maven coordinate. A missing `.aar` is a
`GradleException` naming where to place it, not a silent skip or a
confusing Kotlin "unresolved reference" compile error - the same bar
`NxStore<Name>.cmake`'s own `FATAL_ERROR` sets for a missing desktop SDK,
and the same bar `store_app_gallery`'s missing-`agconnect-services.json`
check sets below.

## Android Gradle wiring

A module owns its own Android-side Gradle dependencies, exactly like it
owns its own `CMakeLists.txt` - `android/app/build.gradle.kts` (the shared,
engine-owned `:app` module every project points at) never names a specific
backend. A module that needs extra Gradle dependencies, a vendor `.aar`,
or to apply a plugin ships `modules/<name>/android/build.gradle.kts`;
`android/app/build.gradle.kts` applies it generically for every enabled
module that has one (`apply(from = ...)`, right after `nxModules` is
known), publishing `nxProjectDir`/`nxEngineRoot` via `extra` first so a
module's script can reach them (a plain Kotlin `val` in one `.gradle.kts`
file isn't visible from another). This is *not* a separate Gradle
subproject - the applied script runs in `:app`'s own `Project` context
(same compilation unit, same manifest, same `namespace`), so
`dependencies{}`/`apply(plugin = ...)` inside it behave exactly as if
written directly in `:app`'s own build script. One real wrinkle: Gradle
doesn't generate type-safe `implementation`/`api`/... accessors for a
script applied this way, so a module's `dependencies{}` block adds
configurations by string name (`"implementation"(...)`, not the typed
`implementation(...)` function) - `store_google_play`'s
`android/build.gradle.kts` is the simplest worked example (one
`"implementation"(...)` line, nothing else).

`store_app_gallery` is the one backend whose Gradle plugin has no
plugin-marker artifact for the modern `plugins{}` DSL (confirmed by
decompiling `agcp-1.9.6.300.jar`: Gradle cannot resolve
`com.huawei.agconnect` as a plugin id from any repository, Huawei's own
included) - it only ships the classic buildscript-classpath form, which a
module's own `android/build.gradle.kts` genuinely cannot register itself
(Gradle requires `buildscript{}` to be the very first block in a script,
before `nxModules` - and hence which modules are even enabled - is known).
For this one case, a module drops
`modules/<name>/android/settings.properties`
(`repository=`/`buildscriptClasspath=`/`catalogPluginAlias=`/
`catalogPluginId=` keys) instead, scanned generically by two engine files
that must run before `nxModules` exists:

- `settings.gradle.kts` scans every module DIRECTORY present on disk (not
  the current project's enabled module list - unknown yet) for this file,
  using `repository=` to populate `dependencyResolutionManagement.repositories`
  (needed for the plain `com.huawei.hms:iap` dependency itself) and
  `catalogPluginAlias=`/`catalogPluginId=` to backfill an otherwise-empty
  `libs` version catalog. That catalog exists only because AGConnect's own
  `GradleVersionTool` determines the Android Gradle Plugin version by first
  scanning buildscript classpath dependencies for a classic
  `com.android.tools.build:gradle` entry (this project has none, since AGP
  is applied through the `plugins{}` DSL), then falling back to reading
  `libs.plugins.android.application` from a catalog literally named `libs` -
  failing outright (`Catalog named libs doesn't exist` / `No value present`)
  if neither exists. This project keeps no real catalog of its own; the
  entry exists purely to satisfy that lookup.
- The same scan, using `repository=`/`buildscriptClasspath=`, runs again in
  the **root project's** own `build.gradle.kts` (template + every real
  project copy) - not `:app`'s - because a `buildscript{}` block placed in
  `:app`'s own script (which was the first thing tried here) disables
  Gradle's type-safe accessor generation for the rest of that file's
  `android{}`/`androidComponents{}` DSL entirely (`namespace`, `compileSdk`,
  `packaging`, `signingConfig`, `variant.sources`, ... all stop resolving).
  Subprojects inherit the root project's buildscript classpath, so
  `apply(plugin = "com.huawei.agconnect")` - called from
  `store_app_gallery`'s own `android/build.gradle.kts`, itself applied into
  `:app` - still resolves it correctly.

With that classpath in place, `store_app_gallery/android/build.gradle.kts`
does the rest entirely on its own: copies the current project's
`android/agconnect-services.json` into the shared `:app` module directory
(the plugin reads it synchronously at configuration time, so the copy must
happen first) - throwing a clear `GradleException` naming where to put it
if the project has none - then calls `apply(plugin = "com.huawei.agconnect")`
and declares the `com.huawei.hms:iap` dependency. `com.huawei.hms:iap`'s
own manifest also sets `android:allowBackup="false"`; the shared
`android/app/src/main/AndroidManifest.xml` carries
`tools:replace="android:allowBackup"` on its `<application>` tag
unconditionally (inert when no enabled module conflicts on that attribute)
so the manifest merger has a winner to pick instead of failing the build -
along with Samsung's `com.samsung.android.iap.permission.BILLING`
permission (not auto-merged from its `.aar` the way Play Billing's/HMS
IAP's own manifests are, per Samsung's integration guide). These two are
the one remaining exception to "a module owns its Android files": a
manifest merge conflict, and `tools:replace` specifically, can only be
resolved from the actual application module's manifest, and neither is a
plugin registered late enough to reach through a module's own
`android/build.gradle.kts` the way a dependency can - going further (real
per-module Gradle subprojects, each with its own manifest that merges in
automatically the way a normal AAR dependency's does) would remove even
this, at the cost of a much bigger structural change to how a project's
Android build is put together.

Verified end-to-end with a synthetic placeholder `agconnect-services.json`
(structurally valid, no real Huawei backend behind it) - `nx.py build -p
android --project projects/samples --abi arm64-v8a` with `store`/
`store_google_play`/`store_app_gallery` enabled together produces a real
signed-and-packaged APK, and disabling `store_app_gallery` again (with
`store_galaxy_store` enabled and no vendored `.aar` present) still fails
with exactly `store_galaxy_store`'s own clear `GradleException`, not a
generic script error - confirming the whole generic per-module chain
degrades the same way a missing desktop SDK's `FATAL_ERROR` already does.
A real project still needs its own genuine `agconnect-services.json` from
AppGallery Connect (which itself needs a verified Huawei Developer account
with Merchant Service enabled) for HMS IAP to do anything at runtime - that
account-verification step is a real environment limitation this repository
can't shortcut, the same kind of gap `store_microsoft`'s package-identity
requirement already documents above.

## Adding a new backend

Copy `modules/store_steam`'s shape: `NxStore<Name>.cmake` (if it vendors an
SDK) + `<name>_module.cpp` (provide whichever of the five services its SDK
supports, `on_attach` reads `store_<name>.ini`) + `<name>_config.h/.cpp` +
`manifest.json` + `tests/`. See that module's own comments for the concrete
worked example, and the CMake infra above for what's already shared. Skip
the CMake vendoring step and the config file entirely if the backend needs
neither - `modules/store_microsoft` is the worked example for that case.
