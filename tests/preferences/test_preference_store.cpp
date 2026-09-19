#include <hubsight/preferences/preference_store.h>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace HubSight::Preferences;

class PreferenceStoreTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    qRegisterMetaType<PreferenceError>();
    qRegisterMetaType<PreferenceChange>();
    qRegisterMetaType<PreferenceChange::Type>();
    qRegisterMetaType<PreferenceStore::TransactionResult>();
    qRegisterMetaType<QVector<PreferenceChange>>();
  }

  void dotNotationReadsWritesAndDeletes() {
    PreferenceStore store;
    QSignalSpy changedSpy(&store, &PreferenceStore::changed);

    QVERIFY(store.set("window.geometry.width", 1280));
    QCOMPARE(store.get("window.geometry.width").toInt(), 1280);
    QCOMPARE(store.get(QStringLiteral("window.geometry.height"), 720).toInt(),
             720);
    QVERIFY(store.contains(QStringLiteral("window.geometry.width")));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).value<PreferenceChange>().path,
             QStringLiteral("window.geometry.width"));

    QVERIFY(store.set(QStringLiteral("window.geometry.height"), 720));
    QVERIFY(store.remove(QStringLiteral("window.geometry.width")));
    QVERIFY(!store.contains(QStringLiteral("window.geometry.width")));
    QCOMPARE(changedSpy.count(), 3);
    QCOMPARE(changedSpy.at(2).at(0).value<PreferenceChange>().type,
             PreferenceChange::Type::Removed);

    QVERIFY(!store.set(QStringLiteral("window..invalid"), true));
    QCOMPARE(store.lastError(),
             QStringLiteral("Preference paths cannot contain empty "
                            "dot-notation segments."));
  }

  void draftIsInvisibleUntilCommit() {
    PreferenceStore store;
    QSignalSpy changedSpy(&store, &PreferenceStore::changed);
    QSignalSpy draftSpy(&store, &PreferenceStore::draftChanged);
    QSignalSpy committedSpy(&store, &PreferenceStore::transactionCommitted);

    QVERIFY(
        store.set(QStringLiteral("appearance.theme"), QStringLiteral("dark")));
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(store.beginDraft());
    QVERIFY(
        store.set(QStringLiteral("appearance.theme"), QStringLiteral("light")));
    QVERIFY(store.set(QStringLiteral("appearance.font.scale"), 1.25));

    QCOMPARE(store.get(QStringLiteral("appearance.theme")).toString(),
             QStringLiteral("light"));
    QCOMPARE(store
                 .get(QStringLiteral("appearance.theme"), {},
                      PreferenceStore::ReadView::Committed)
                 .toString(),
             QStringLiteral("dark"));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(draftSpy.count(), 2);

    QCOMPARE(store.commitDraft(),
             PreferenceStore::TransactionResult::Committed);
    QCOMPARE(changedSpy.count(), 3);
    QCOMPARE(committedSpy.count(), 2);
    QCOMPARE(store.get(QStringLiteral("appearance.font.scale")).toDouble(),
             1.25);
    QVERIFY(!store.hasDraft());
  }

  void transactionsAreAtomicAndDetectConflicts() {
    PreferenceStore store;
    QSignalSpy changedSpy(&store, &PreferenceStore::changed);
    QSignalSpy rollbackSpy(&store, &PreferenceStore::transactionRolledBack);

    auto transaction = store.beginTransaction();
    QVERIFY(transaction->set(QStringLiteral("server.host"),
                             QStringLiteral("gateway.example.com")));
    QVERIFY(transaction->set(QStringLiteral("server.port"), 443));
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(transaction->commit(),
             PreferenceStore::TransactionResult::Committed);
    QCOMPARE(changedSpy.count(), 2);

    auto rollback = store.beginTransaction();
    QVERIFY(rollback->set(QStringLiteral("temporary.value"), true));
    rollback->rollback();
    QCOMPARE(rollbackSpy.count(), 1);
    QVERIFY(!store.contains(QStringLiteral("temporary.value")));

    auto first = store.beginTransaction();
    auto stale = store.beginTransaction();
    QVERIFY(first->set(QStringLiteral("revision.value"), 1));
    QCOMPARE(first->commit(), PreferenceStore::TransactionResult::Committed);
    QVERIFY(stale->set(QStringLiteral("revision.value"), 2));
    QCOMPARE(stale->commit(), PreferenceStore::TransactionResult::Conflict);
    QCOMPARE(store.get(QStringLiteral("revision.value")).toInt(), 1);
  }

  void jsonRoundTripAndAtomicFilePersistence() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("preferences.json"));

    PreferenceStore store;
    store.setStoragePath(path);
    store.setAutoPersist(true);
    QVERIFY(
        store.set(QStringLiteral("account.locale"), QStringLiteral("vi-VN")));
    QVERIFY(QFile::exists(path));

    PreferenceStore loaded;
    QString error;
    QVERIFY2(loaded.loadFile(path, &error), qPrintable(error));
    QCOMPARE(loaded.get(QStringLiteral("account.locale")).toString(),
             QStringLiteral("vi-VN"));
    QCOMPARE(loaded.storagePath(), path);

    const QByteArray compact = loaded.toJson();
    QVERIFY(compact.contains("account"));
    QVERIFY(compact.contains("vi-VN"));

    QVERIFY(!loaded.loadJson(QByteArrayLiteral("[]"), &error));
    QCOMPARE(loaded.get(QStringLiteral("account.locale")).toString(),
             QStringLiteral("vi-VN"));
  }

  void invalidDraftAndPersistenceConfigurationAreSafe() {
    PreferenceStore store;
    QCOMPARE(store.commitDraft(), PreferenceStore::TransactionResult::Invalid);
    QVERIFY(!store.lastError().isEmpty());

    store.setAutoPersist(true);
    QVERIFY(!store.set(QStringLiteral("app.enabled"), true));
    QCOMPARE(store.lastError(),
             QStringLiteral("Auto-persist is enabled but no preference "
                            "storage path is configured."));
    QVERIFY(!store.contains(QStringLiteral("app.enabled")));

    QVERIFY(store.beginDraft());
    QVERIFY(store.set(QStringLiteral("app.enabled"), true));
    store.discardDraft();
    QVERIFY(!store.contains(QStringLiteral("app.enabled")));
  }
};

QTEST_MAIN(PreferenceStoreTest)
#include "test_preference_store.moc"
