#pragma once

#include "preferences_export.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <memory>

namespace HubSight::Preferences {

struct HUBSIGHT_PREFERENCES_EXPORT PreferenceError {
  QString code;
  QString message;
};

struct HUBSIGHT_PREFERENCES_EXPORT PreferenceChange {
  enum class Type {
    Set,
    Removed,
  };

  QString path;
  QJsonValue value;
  QJsonValue previousValue;
  Type type = Type::Set;
};

// JSON preference store with dot-notation access, draft editing, optimistic
// transactions, atomic change events, and optional atomic JSON-file
// persistence.
//
// Example:
//   PreferenceStore preferences;
//   preferences.set("window.geometry.width", 1280);
//   const QJsonValue width = preferences.get("window.geometry.width");
//
// A draft is isolated from committed state until commitDraft() succeeds. For
// multi-writer code, beginTransaction() captures a revision and returns
// Conflict when another transaction commits first.
class HUBSIGHT_PREFERENCES_EXPORT PreferenceStore final : public QObject {
  Q_OBJECT

public:
  enum class TransactionResult {
    Committed,
    NoChanges,
    Conflict,
    Invalid,
    PersistenceFailed,
  };
  Q_ENUM(TransactionResult)

  enum class ReadView {
    Committed,
    DraftIfPresent,
  };
  Q_ENUM(ReadView)

  class HUBSIGHT_PREFERENCES_EXPORT Transaction final {
  public:
    ~Transaction();

    QJsonValue get(const QString &path,
                   const QJsonValue &fallback = QJsonValue()) const;
    bool contains(const QString &path) const;
    bool set(const QString &path, const QJsonValue &value);
    bool remove(const QString &path);

    TransactionResult commit();
    void rollback();
    bool isActive() const;
    quint64 baseRevision() const;
    QString lastError() const;

  private:
    friend class PreferenceStore;
    Transaction(PreferenceStore *store, QJsonObject values,
                quint64 baseRevision, bool draft);

    PreferenceStore *m_store = nullptr;
    QJsonObject m_values;
    quint64 m_baseRevision = 0;
    QString m_lastError;
    bool m_active = true;
    bool m_draft = false;
  };

  explicit PreferenceStore(QObject *parent = nullptr);
  ~PreferenceStore() override;

  QJsonValue get(const QString &path, const QJsonValue &fallback = QJsonValue(),
                 ReadView view = ReadView::DraftIfPresent) const;
  QJsonValue get(const char *path, const QJsonValue &fallback = QJsonValue(),
                 ReadView view = ReadView::DraftIfPresent) const {
    return get(QString::fromUtf8(path), fallback, view);
  }
  bool contains(const QString &path,
                ReadView view = ReadView::DraftIfPresent) const;
  bool contains(const char *path,
                ReadView view = ReadView::DraftIfPresent) const {
    return contains(QString::fromUtf8(path), view);
  }

  // If a draft is active, these methods edit the draft. Otherwise each call is
  // an implicit one-operation transaction and publishes its change atomically.
  bool set(const QString &path, const QJsonValue &value);
  bool set(const char *path, const QJsonValue &value) {
    return set(QString::fromUtf8(path), value);
  }
  bool remove(const QString &path);
  bool remove(const char *path) { return remove(QString::fromUtf8(path)); }

  bool beginDraft();
  bool hasDraft() const;
  TransactionResult commitDraft();
  void discardDraft();

  // Transactions are isolated snapshots. The caller must commit or rollback;
  // destroying an active transaction rolls it back without publishing changes.
  std::unique_ptr<Transaction> beginTransaction();

  quint64 revision() const;
  QJsonObject committedObject() const;
  QJsonObject draftObject() const;

  QByteArray toJson(QJsonDocument::JsonFormat format = QJsonDocument::Compact,
                    ReadView view = ReadView::Committed) const;
  bool loadJson(const QByteArray &json, QString *errorMessage = nullptr);
  bool loadFile(const QString &path, QString *errorMessage = nullptr);
  bool saveFile(const QString &path = {}, QString *errorMessage = nullptr);

  void setStoragePath(const QString &path);
  QString storagePath() const;
  void setAutoPersist(bool enabled);
  bool autoPersist() const;

  QString lastError() const;

signals:
  // Emitted only for committed changes. Draft edits use draftChanged().
  void changed(HubSight::Preferences::PreferenceChange change);
  void transactionCommitted(
      quint64 revision,
      QVector<HubSight::Preferences::PreferenceChange> changes);
  void transactionRolledBack(QString reason);
  void draftChanged(QStringList paths);
  void draftDiscarded();
  void errorOccurred(HubSight::Preferences::PreferenceError error);

private:
  friend class Transaction;

  static bool splitPath(const QString &path, QStringList *segments,
                        QString *errorMessage = nullptr);
  static QJsonValue valueAt(const QJsonObject &object,
                            const QStringList &segments, bool *found);
  static bool setAt(QJsonObject *object, const QStringList &segments,
                    const QJsonValue &value);
  static bool removeAt(QJsonObject *object, const QStringList &segments);
  static QVector<PreferenceChange> diff(const QJsonObject &before,
                                        const QJsonObject &after);

  void setError(const QString &code, const QString &message);
  void clearError();
  void emitDraftDiff(const QJsonObject &before, const QJsonObject &after);
  TransactionResult commitTransaction(Transaction &transaction);
  void rollbackTransaction(Transaction &transaction);
  bool writeFile(const QString &path, const QJsonObject &object,
                 QString *errorMessage) const;
  bool replaceCommitted(const QJsonObject &object, QString *errorMessage);
  QJsonObject valuesFor(ReadView view) const;

  QJsonObject m_values;
  std::unique_ptr<Transaction> m_draft;
  quint64 m_revision = 0;
  QString m_storagePath;
  bool m_autoPersist = false;
  QString m_lastError;
};

} // namespace HubSight::Preferences

Q_DECLARE_METATYPE(HubSight::Preferences::PreferenceError)
Q_DECLARE_METATYPE(HubSight::Preferences::PreferenceChange)
Q_DECLARE_METATYPE(HubSight::Preferences::PreferenceChange::Type)
Q_DECLARE_METATYPE(HubSight::Preferences::PreferenceStore::TransactionResult)
Q_DECLARE_METATYPE(HubSight::Preferences::PreferenceStore::ReadView)
Q_DECLARE_METATYPE(QVector<HubSight::Preferences::PreferenceChange>)
