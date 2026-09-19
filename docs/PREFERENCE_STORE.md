# PreferenceStore

`PreferenceStore` is an independent Qt application-preferences module. It is
JSON-based and has no dependency on HubSight Admin API, HTTP, WebSocket,
authentication, realtime, or internet connectivity.

It is intended for non-secret application settings such as window state, UI
options, feature flags, and local user preferences. It is not a replacement for
secure credential storage: JWT refresh tokens, API keys, passwords, `.hscfg`
PINs, and other credentials must never be placed in this store.

The standalone CMake target is:

```cmake
HubSight::Preferences
```

The target only links against `Qt6::Core`.

## Basic usage

```cpp
#include <hubsight/preferences.h>

using namespace HubSight::Preferences;

PreferenceStore preferences;
preferences.set(QStringLiteral("window.geometry.width"), 1280);
preferences.set(QStringLiteral("window.geometry.height"), 720);
preferences.set(QStringLiteral("appearance.theme"), QStringLiteral("dark"));

const int width = preferences
                      .get(QStringLiteral("window.geometry.width"), 1024)
                      .toInt();

preferences.remove(QStringLiteral("appearance.theme"));
```

Dot-notation paths must be non-empty and cannot contain empty segments. A set
operation creates missing intermediate JSON objects. Setting a value below an
existing scalar replaces that scalar with an object.

Supported values are Qt JSON values: object, array, string, number, boolean, and
null. `Undefined` is rejected; use `remove()` to delete a key.

## Draft state

Draft state is useful for settings dialogs or multi-step UI editing:

```cpp
preferences.beginDraft();
preferences.set(QStringLiteral("appearance.theme"), QStringLiteral("light"));
preferences.set(QStringLiteral("appearance.font.scale"), 1.10);

// Default reads use the draft. Committed reads remain unchanged.
const auto draftTheme = preferences.get(QStringLiteral("appearance.theme"));
const auto savedTheme = preferences.get(
    QStringLiteral("appearance.theme"), {},
    PreferenceStore::ReadView::Committed);

// Publish all draft changes atomically, or discard them:
preferences.commitDraft();
// preferences.discardDraft();
```

Draft mutations emit `draftChanged(QStringList paths)`. They do not emit
`changed()` until `commitDraft()` succeeds. A draft commit performs optimistic
revision checking and returns `Conflict` if another transaction committed first.

## ACID-style transactions

For related changes that must be committed as one unit:

```cpp
auto transaction = preferences.beginTransaction();
transaction->set(QStringLiteral("server.host"), QStringLiteral("gateway"));
transaction->set(QStringLiteral("server.port"), 443);

const auto result = transaction->commit();
if (result != PreferenceStore::TransactionResult::Committed &&
    result != PreferenceStore::TransactionResult::NoChanges) {
    transaction->rollback();
}
```

The transaction model provides:

- **Atomicity:** no committed event is emitted until the complete transaction is
  accepted; listeners receive leaf-level changes after the root is swapped.
- **Consistency:** invalid paths and undefined JSON values are rejected without
  modifying the store.
- **Isolation:** a transaction works on a snapshot and detects stale revisions
  at commit time.
- **Durability:** when `autoPersist` and a storage path are configured, commits
  use `QSaveFile` and atomically replace the JSON file. Without persistence, the
  store is intentionally in-memory and does not claim crash durability.

An active transaction is rolled back automatically when destroyed. Explicit
`rollback()` is preferred when the control flow makes the decision clear.

## Change events

```cpp
QObject::connect(
    &preferences, &PreferenceStore::changed,
    [](PreferenceChange change) {
        qInfo() << change.path
                << (change.type == PreferenceChange::Type::Removed
                        ? QStringLiteral("removed")
                        : QStringLiteral("changed"));
    });

QObject::connect(
    &preferences, &PreferenceStore::transactionCommitted,
    [](quint64 revision, QVector<PreferenceChange> changes) {
        qInfo() << "Preference revision" << revision
                << "changed paths" << changes.size();
    });
```

`changed` is emitted once per changed leaf after a successful commit. A delete
is represented by `PreferenceChange::Type::Removed`. `transactionCommitted`
contains the complete batch and revision. `transactionRolledBack`,
`draftChanged`, and `draftDiscarded` are available for UI/state coordination.

## JSON file persistence

```cpp
PreferenceStore preferences;
preferences.setStoragePath(QStringLiteral("/path/to/preferences.json"));
preferences.setAutoPersist(true);
preferences.set(QStringLiteral("ui.sidebar.visible"), true);

// Or load/save explicitly:
preferences.loadFile(QStringLiteral("/path/to/preferences.json"));
preferences.saveFile();
```

File writes are performed through `QSaveFile`, so a failed write does not replace
the previous file. `loadFile()` accepts a JSON object root only and leaves the
current committed state unchanged when parsing fails.

Preference files may contain personal application settings. Applications should
choose an appropriate user-private directory and filesystem permissions. Do not
store credentials in this store.
