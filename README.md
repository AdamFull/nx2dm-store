# store module

The neutral storefront interface. `modules/store` itself talks to no
storefront - it only declares five independently-optional services
(`store_service.h`) and exposes the neutral `host.store_*` Luau surface
(`store_scripting.cpp`) that forwards to whichever backend module is active.
A storefront integration is a separate backend module - `modules/store_steam`,
`modules/store_egs`, `modules/store_gog`, `modules/store_stove`,
`modules/store_microsoft` and `modules/store_google_play` are built and
verified so far (the mobile stores still to come - Huawei App Gallery,
Samsung Galaxy Store, Amazon Appstore, Apple's App Store - follow
`store_google_play`'s recipe rather than the desktop backends'; see
"Adding a new backend" below).

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
worked example for every other mobile store still to come (Huawei App
Gallery, Samsung Galaxy Store, Amazon Appstore - none of them ship a native
SDK either): a hand-written shim
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
than re-inventing it. A module contributes Kotlin/Java sources and a
Gradle dependency the same conditional way it contributes C++
(`android/app/build.gradle.kts`'s per-module source-dir loop and a `nxModules`-gated
`implementation(...)` line) - `store_google_play`'s shim is never
referenced from any unconditionally-compiled file, so a build without the
module carries none of Billing Library's code, permissions, or dependency
weight. Like `store_gog`, it can't check base-app ownership (the Play
Store itself already gates who can install/run the APK) and like
`store_stove`'s achievements, one operation has no honest mapping: Play
Billing's consumable/durable split doesn't exist in `StoreIap::purchase()`,
so every product is treated as a durable entitlement (`acknowledgePurchase`,
never `consumeAsync`) - a consumable-currency product isn't served by this
backend.

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

A backend can also expose capabilities that don't belong in this neutral
interface at all - Steam's Workshop, leaderboards, and explicit overlay
control (`modules/store_steam/store_steam_workshop.h`,
`store_steam_leaderboards.h`, `store_steam_overlay.h`) are the worked
example. These aren't registered through `ServiceRegistry` (nothing outside
that one backend module will ever look them up) and get their own
`host.store_steam_*` Luau surface from a separate scripting file
(`store_steam_scripting.h/.cpp`), keeping `store/store_scripting.cpp` itself
strictly neutral.

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
`store_microsoft` and `store_google_play` are the exceptions - neither has
a config file, because neither has developer-supplied credentials to
configure at runtime in the first place: `StoreContext::GetDefault()` and
`BillingClient.newBuilder(context)` both take no per-developer id/secret,
resolving everything from the process's own package identity (and, for
`store_google_play`, the installed Play Store client) instead.

## SDK delivery convention

A storefront SDK is developer-provided and **never committed** - vendors
either gate the download behind a partner account (Steamworks, EOS, GOG
Galaxy, Stove) or there's nothing to vendor at all (Microsoft Store's
`Windows.Services.Store` ships with the Windows SDK already on the machine;
Google Play Billing is a plain public Maven dependency, no partner account
or download either). Each backend that does need a vendored SDK follows
`modules/live2d`'s precedent for a big vendor SDK with its own idiosyncratic
shape:

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

A mobile backend with no native SDK at all (`store_google_play`, and every
other mobile store still to come) skips vendoring entirely: a Gradle
dependency and a hand-written JNI shim replace `NxStore<Name>.cmake` - see
`store_google_play`'s own paragraph above, and `engine/core/foundation/
platform/android_jni.h` for the reusable JNI toolkit every one of them
will share.

## Adding a new backend

Copy `modules/store_steam`'s shape: `NxStore<Name>.cmake` (if it vendors an
SDK) + `<name>_module.cpp` (provide whichever of the five services its SDK
supports, `on_attach` reads `store_<name>.ini`) + `<name>_config.h/.cpp` +
`manifest.json` + `tests/`. See that module's own comments for the concrete
worked example, and the CMake infra above for what's already shared. Skip
the CMake vendoring step and the config file entirely if the backend needs
neither - `modules/store_microsoft` is the worked example for that case.
