#include "../../include/hubsight/preferences/preference_store.h"

#include <QFile>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

namespace HubSight::Preferences {
namespace {

QString joinedPath(const QString &prefix, const QString &key) {
  return prefix.isEmpty() ? key : prefix + QLatin1Char('.') + key;
}

void collectDiff(const QJsonObject &before, const QJsonObject &after,
                 const QString &prefix, QVector<PreferenceChange> *changes) {
  QSet<QString> keySet;
  for (const QString &key : before.keys()) {
    keySet.insert(key);
  }
  for (const QString &key : after.keys()) {
    keySet.insert(key);
  }

  QStringList keys = keySet.values();
  keys.sort();
  for (const QString &key : keys) {
    const QString path = joinedPath(prefix, key);
    const bool hadValue = before.contains(key);
    const bool hasValue = after.contains(key);
    const QJsonValue oldValue = hadValue ? before.value(key) : QJsonValue();
    const QJsonValue newValue = hasValue ? after.value(key) : QJsonValue();

    if (!hadValue && hasValue && newValue.isObject()) {
      const qsizetype beforeCount = changes->size();
      collectDiff({}, newValue.toObject(), path, changes);
      if (changes->size() == beforeCount) {
        PreferenceChange change;
        change.path = path;
        change.value = newValue;
        change.type = PreferenceChange::Type::Set;
        changes->append(change);
      }
      continue;
    }
    if (hadValue && !hasValue && oldValue.isObject()) {
      const qsizetype beforeCount = changes->size();
      collectDiff(oldValue.toObject(), {}, path, changes);
      if (changes->size() == beforeCount) {
        PreferenceChange change;
        change.path = path;
        change.previousValue = oldValue;
        change.type = PreferenceChange::Type::Removed;
        changes->append(change);
      }
      continue;
    }
    if (hadValue && hasValue && oldValue.isObject() && newValue.isObject()) {
      collectDiff(oldValue.toObject(), newValue.toObject(), path, changes);
      continue;
    }
    if (hadValue && hasValue && oldValue == newValue) {
      continue;
    }

    PreferenceChange change;
    change.path = path;
    change.previousValue = oldValue;
    change.value = newValue;
    change.type = hasValue ? PreferenceChange::Type::Set
                           : PreferenceChange::Type::Removed;
    changes->append(change);
  }
}

} // namespace

PreferenceStore::Transaction::Transaction(PreferenceStore *store,
                                          QJsonObject values,
                                          quint64 baseRevision, bool draft)
    : m_store(store), m_values(std::move(values)), m_baseRevision(baseRevision),
      m_draft(draft) {}

PreferenceStore::Transaction::~Transaction() {
  if (m_active && m_store) {
    rollback();
  }
}

QJsonValue PreferenceStore::Transaction::get(const QString &path,
                                             const QJsonValue &fallback) const {
  QStringList segments;
  if (!PreferenceStore::splitPath(path, &segments)) {
    return fallback;
  }
  bool found = false;
  const QJsonValue value = PreferenceStore::valueAt(m_values, segments, &found);
  return found ? value : fallback;
}

bool PreferenceStore::Transaction::contains(const QString &path) const {
  QStringList segments;
  if (!PreferenceStore::splitPath(path, &segments)) {
    return false;
  }
  bool found = false;
  PreferenceStore::valueAt(m_values, segments, &found);
  return found;
}

bool PreferenceStore::Transaction::set(const QString &path,
                                       const QJsonValue &value) {
  if (!m_active) {
    m_lastError = QStringLiteral("The preference transaction is not active.");
    return false;
  }
  if (value.isUndefined()) {
    m_lastError = QStringLiteral(
        "Undefined is not a JSON value; use remove() to delete a preference.");
    return false;
  }

  QStringList segments;
  if (!PreferenceStore::splitPath(path, &segments, &m_lastError)) {
    return false;
  }
  if (!PreferenceStore::setAt(&m_values, segments, value)) {
    m_lastError = QStringLiteral("Unable to set preference path.");
    return false;
  }
  m_lastError.clear();
  return true;
}

bool PreferenceStore::Transaction::remove(const QString &path) {
  if (!m_active) {
    m_lastError = QStringLiteral("The preference transaction is not active.");
    return false;
  }

  QStringList segments;
  if (!PreferenceStore::splitPath(path, &segments, &m_lastError)) {
    return false;
  }
  const bool removed = PreferenceStore::removeAt(&m_values, segments);
  if (!removed) {
    m_lastError = QStringLiteral("Preference path does not exist.");
    return false;
  }
  m_lastError.clear();
  return true;
}

PreferenceStore::TransactionResult PreferenceStore::Transaction::commit() {
  if (!m_active || !m_store) {
    m_lastError = QStringLiteral("The preference transaction is not active.");
    return TransactionResult::Invalid;
  }
  return m_store->commitTransaction(*this);
}

void PreferenceStore::Transaction::rollback() {
  if (!m_active) {
    return;
  }
  if (m_store) {
    m_store->rollbackTransaction(*this);
  } else {
    m_active = false;
  }
}

bool PreferenceStore::Transaction::isActive() const { return m_active; }

quint64 PreferenceStore::Transaction::baseRevision() const {
  return m_baseRevision;
}

QString PreferenceStore::Transaction::lastError() const { return m_lastError; }

PreferenceStore::PreferenceStore(QObject *parent) : QObject(parent) {
  qRegisterMetaType<PreferenceError>();
  qRegisterMetaType<PreferenceChange>();
  qRegisterMetaType<PreferenceChange::Type>();
  qRegisterMetaType<TransactionResult>();
  qRegisterMetaType<ReadView>();
  qRegisterMetaType<QVector<PreferenceChange>>();
}

PreferenceStore::~PreferenceStore() = default;

QJsonValue PreferenceStore::get(const QString &path, const QJsonValue &fallback,
                                ReadView view) const {
  QStringList segments;
  if (!splitPath(path, &segments)) {
    return fallback;
  }
  bool found = false;
  const QJsonValue value = valueAt(valuesFor(view), segments, &found);
  return found ? value : fallback;
}

bool PreferenceStore::contains(const QString &path, ReadView view) const {
  QStringList segments;
  if (!splitPath(path, &segments)) {
    return false;
  }
  bool found = false;
  valueAt(valuesFor(view), segments, &found);
  return found;
}

bool PreferenceStore::set(const QString &path, const QJsonValue &value) {
  if (m_draft) {
    const QJsonObject before = m_draft->m_values;
    if (!m_draft->set(path, value)) {
      setError(QStringLiteral("INVALID_PREFERENCE"), m_draft->lastError());
      return false;
    }
    emitDraftDiff(before, m_draft->m_values);
    clearError();
    return true;
  }

  std::unique_ptr<Transaction> transaction = beginTransaction();
  if (!transaction->set(path, value)) {
    setError(QStringLiteral("INVALID_PREFERENCE"), transaction->lastError());
    return false;
  }
  const TransactionResult result = transaction->commit();
  return result == TransactionResult::Committed ||
         result == TransactionResult::NoChanges;
}

bool PreferenceStore::remove(const QString &path) {
  if (m_draft) {
    const QJsonObject before = m_draft->m_values;
    if (!m_draft->remove(path)) {
      setError(QStringLiteral("PREFERENCE_NOT_FOUND"), m_draft->lastError());
      return false;
    }
    emitDraftDiff(before, m_draft->m_values);
    clearError();
    return true;
  }

  std::unique_ptr<Transaction> transaction = beginTransaction();
  if (!transaction->remove(path)) {
    setError(QStringLiteral("PREFERENCE_NOT_FOUND"), transaction->lastError());
    return false;
  }
  const TransactionResult result = transaction->commit();
  return result == TransactionResult::Committed ||
         result == TransactionResult::NoChanges;
}

bool PreferenceStore::beginDraft() {
  if (m_draft) {
    setError(QStringLiteral("DRAFT_ALREADY_OPEN"),
             QStringLiteral("A preference draft is already active."));
    return false;
  }
  m_draft = std::unique_ptr<Transaction>(
      new Transaction(this, m_values, m_revision, true));
  clearError();
  return true;
}

bool PreferenceStore::hasDraft() const { return m_draft != nullptr; }

PreferenceStore::TransactionResult PreferenceStore::commitDraft() {
  if (!m_draft) {
    setError(QStringLiteral("NO_DRAFT"),
             QStringLiteral("There is no active preference draft."));
    return TransactionResult::Invalid;
  }
  const TransactionResult result = commitTransaction(*m_draft);
  if (result == TransactionResult::Committed ||
      result == TransactionResult::NoChanges) {
    m_draft.reset();
  }
  return result;
}

void PreferenceStore::discardDraft() {
  if (!m_draft) {
    return;
  }
  m_draft->m_active = false;
  m_draft.reset();
  emit draftDiscarded();
  clearError();
}

std::unique_ptr<PreferenceStore::Transaction>
PreferenceStore::beginTransaction() {
  return std::unique_ptr<Transaction>(
      new Transaction(this, m_values, m_revision, false));
}

quint64 PreferenceStore::revision() const { return m_revision; }

QJsonObject PreferenceStore::committedObject() const { return m_values; }

QJsonObject PreferenceStore::draftObject() const {
  return m_draft ? m_draft->m_values : QJsonObject{};
}

QByteArray PreferenceStore::toJson(QJsonDocument::JsonFormat format,
                                   ReadView view) const {
  return QJsonDocument(valuesFor(view)).toJson(format);
}

bool PreferenceStore::loadJson(const QByteArray &json, QString *errorMessage) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (document.isNull() || !document.isObject()) {
    const QString message =
        document.isNull()
            ? QStringLiteral("Invalid preference JSON: %1.")
                  .arg(parseError.errorString())
            : QStringLiteral("Preference JSON root must be an object.");
    if (errorMessage) {
      *errorMessage = message;
    }
    setError(QStringLiteral("INVALID_JSON"), message);
    return false;
  }
  if (!replaceCommitted(document.object(), errorMessage)) {
    return false;
  }
  clearError();
  return true;
}

bool PreferenceStore::loadFile(const QString &path, QString *errorMessage) {
  const QString normalizedPath = path.trimmed();
  if (normalizedPath.isEmpty()) {
    const QString message =
        QStringLiteral("A preference file path is required.");
    if (errorMessage) {
      *errorMessage = message;
    }
    setError(QStringLiteral("INVALID_FILE_PATH"), message);
    return false;
  }

  QFile file(normalizedPath);
  if (!file.open(QIODevice::ReadOnly)) {
    const QString message = QStringLiteral("Unable to read preference file: %1")
                                .arg(file.errorString());
    if (errorMessage) {
      *errorMessage = message;
    }
    setError(QStringLiteral("FILE_READ_FAILED"), message);
    return false;
  }
  if (!loadJson(file.readAll(), errorMessage)) {
    return false;
  }
  m_storagePath = normalizedPath;
  return true;
}

bool PreferenceStore::saveFile(const QString &path, QString *errorMessage) {
  const QString normalizedPath =
      path.trimmed().isEmpty() ? m_storagePath : path.trimmed();
  if (normalizedPath.isEmpty()) {
    const QString message =
        QStringLiteral("A preference file path is required.");
    if (errorMessage) {
      *errorMessage = message;
    }
    setError(QStringLiteral("INVALID_FILE_PATH"), message);
    return false;
  }
  if (!writeFile(normalizedPath, m_values, errorMessage)) {
    return false;
  }
  m_storagePath = normalizedPath;
  clearError();
  return true;
}

void PreferenceStore::setStoragePath(const QString &path) {
  m_storagePath = path.trimmed();
}

QString PreferenceStore::storagePath() const { return m_storagePath; }

void PreferenceStore::setAutoPersist(bool enabled) { m_autoPersist = enabled; }

bool PreferenceStore::autoPersist() const { return m_autoPersist; }

QString PreferenceStore::lastError() const { return m_lastError; }

bool PreferenceStore::splitPath(const QString &path, QStringList *segments,
                                QString *errorMessage) {
  if (path.isEmpty() || path.trimmed() != path) {
    if (errorMessage) {
      *errorMessage = QStringLiteral(
          "Preference paths must be non-empty and must not have surrounding "
          "whitespace.");
    }
    return false;
  }

  const QStringList parts = path.split(QLatin1Char('.'), Qt::KeepEmptyParts);
  for (const QString &part : parts) {
    if (part.isEmpty()) {
      if (errorMessage) {
        *errorMessage = QStringLiteral(
            "Preference paths cannot contain empty dot-notation segments.");
      }
      return false;
    }
  }
  if (segments) {
    *segments = parts;
  }
  return true;
}

QJsonValue PreferenceStore::valueAt(const QJsonObject &object,
                                    const QStringList &segments, bool *found) {
  QJsonValue current(object);
  for (const QString &segment : segments) {
    if (!current.isObject() || !current.toObject().contains(segment)) {
      if (found) {
        *found = false;
      }
      return {};
    }
    current = current.toObject().value(segment);
  }
  if (found) {
    *found = true;
  }
  return current;
}

bool PreferenceStore::setAt(QJsonObject *object, const QStringList &segments,
                            const QJsonValue &value) {
  if (!object || segments.isEmpty() || value.isUndefined()) {
    return false;
  }
  const QString &segment = segments.constFirst();
  if (segments.size() == 1) {
    object->insert(segment, value);
    return true;
  }

  QJsonObject child = object->value(segment).toObject();
  const QStringList remaining = segments.mid(1);
  if (!setAt(&child, remaining, value)) {
    return false;
  }
  object->insert(segment, child);
  return true;
}

bool PreferenceStore::removeAt(QJsonObject *object,
                               const QStringList &segments) {
  if (!object || segments.isEmpty()) {
    return false;
  }
  const QString &segment = segments.constFirst();
  if (!object->contains(segment)) {
    return false;
  }
  if (segments.size() == 1) {
    object->remove(segment);
    return true;
  }

  QJsonObject child = object->value(segment).toObject();
  if (child.isEmpty() && !object->value(segment).isObject()) {
    return false;
  }
  if (!removeAt(&child, segments.mid(1))) {
    return false;
  }
  object->insert(segment, child);
  return true;
}

QVector<PreferenceChange> PreferenceStore::diff(const QJsonObject &before,
                                                const QJsonObject &after) {
  QVector<PreferenceChange> changes;
  collectDiff(before, after, {}, &changes);
  return changes;
}

void PreferenceStore::setError(const QString &code, const QString &message) {
  m_lastError = message;
  emit errorOccurred(PreferenceError{code, message});
}

void PreferenceStore::clearError() { m_lastError.clear(); }

void PreferenceStore::emitDraftDiff(const QJsonObject &before,
                                    const QJsonObject &after) {
  const QVector<PreferenceChange> changes = diff(before, after);
  if (changes.isEmpty()) {
    return;
  }
  QStringList paths;
  paths.reserve(changes.size());
  for (const PreferenceChange &change : changes) {
    paths.append(change.path);
  }
  emit draftChanged(paths);
}

PreferenceStore::TransactionResult
PreferenceStore::commitTransaction(Transaction &transaction) {
  if (!transaction.m_active || transaction.m_store != this) {
    transaction.m_lastError =
        QStringLiteral("The preference transaction is not active.");
    return TransactionResult::Invalid;
  }
  if (transaction.m_baseRevision != m_revision) {
    transaction.m_lastError = QStringLiteral(
        "The preference transaction is stale because another commit changed "
        "the store.");
    setError(QStringLiteral("TRANSACTION_CONFLICT"), transaction.m_lastError);
    return TransactionResult::Conflict;
  }

  const QVector<PreferenceChange> changes =
      diff(m_values, transaction.m_values);
  if (changes.isEmpty()) {
    transaction.m_active = false;
    transaction.m_lastError.clear();
    emit transactionCommitted(m_revision, {});
    return TransactionResult::NoChanges;
  }

  if (m_autoPersist && m_storagePath.isEmpty()) {
    transaction.m_lastError =
        QStringLiteral("Auto-persist is enabled but no preference storage path "
                       "is configured.");
    setError(QStringLiteral("PERSISTENCE_FAILED"), transaction.m_lastError);
    return TransactionResult::PersistenceFailed;
  }
  if (m_autoPersist && !writeFile(m_storagePath, transaction.m_values,
                                  &transaction.m_lastError)) {
    setError(QStringLiteral("PERSISTENCE_FAILED"), transaction.m_lastError);
    return TransactionResult::PersistenceFailed;
  }

  m_values = transaction.m_values;
  ++m_revision;
  transaction.m_active = false;
  transaction.m_lastError.clear();
  for (const PreferenceChange &change : changes) {
    emit changed(change);
  }
  emit transactionCommitted(m_revision, changes);
  clearError();
  return TransactionResult::Committed;
}

void PreferenceStore::rollbackTransaction(Transaction &transaction) {
  if (!transaction.m_active) {
    return;
  }
  transaction.m_active = false;
  if (!transaction.m_draft) {
    emit transactionRolledBack(QStringLiteral("Transaction rolled back."));
  }
}

bool PreferenceStore::writeFile(const QString &path, const QJsonObject &object,
                                QString *errorMessage) const {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    const QString message =
        QStringLiteral("Unable to open preference file for writing: %1")
            .arg(file.errorString());
    if (errorMessage) {
      *errorMessage = message;
    }
    return false;
  }

  const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
  if (file.write(data) != data.size()) {
    const QString message =
        QStringLiteral("Unable to write preference file: %1")
            .arg(file.errorString());
    if (errorMessage) {
      *errorMessage = message;
    }
    return false;
  }
  if (!file.commit()) {
    const QString message =
        QStringLiteral("Unable to atomically commit preference file: %1")
            .arg(file.errorString());
    if (errorMessage) {
      *errorMessage = message;
    }
    return false;
  }
  return true;
}

bool PreferenceStore::replaceCommitted(const QJsonObject &object,
                                       QString *errorMessage) {
  if (m_draft) {
    const QString message = QStringLiteral(
        "Cannot replace committed preferences while a draft is active.");
    if (errorMessage) {
      *errorMessage = message;
    }
    setError(QStringLiteral("DRAFT_ACTIVE"), message);
    return false;
  }

  const QVector<PreferenceChange> changes = diff(m_values, object);
  m_values = object;
  if (!changes.isEmpty()) {
    ++m_revision;
    for (const PreferenceChange &change : changes) {
      emit changed(change);
    }
  }
  emit transactionCommitted(m_revision, changes);
  return true;
}

QJsonObject PreferenceStore::valuesFor(ReadView view) const {
  if (view == ReadView::DraftIfPresent && m_draft) {
    return m_draft->m_values;
  }
  return m_values;
}

} // namespace HubSight::Preferences
