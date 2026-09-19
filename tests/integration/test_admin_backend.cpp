#include <hubsight/admin/hubsight_admin.h>

#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <memory>

using namespace HubSight::Admin;

namespace {

QString environmentValue(const char *name) {
  return QString::fromUtf8(qgetenv(name)).trimmed();
}

QUrl integrationGateway() {
  return QUrl(environmentValue("HUBSIGHT_ADMIN_INTEGRATION_GATEWAY_URL"));
}

} // namespace

class AdminBackendIntegrationTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    qRegisterMetaType<AdminError>();
    qRegisterMetaType<AdminUser>();
    qRegisterMetaType<SystemStatus>();
  }

  void statusEndpoint() {
    const QUrl gateway = integrationGateway();
    const QString apiKey =
        environmentValue("HUBSIGHT_ADMIN_INTEGRATION_API_KEY");
    if (!gateway.isValid() || gateway.host().isEmpty() || apiKey.isEmpty()) {
      QSKIP(
          "Set HUBSIGHT_ADMIN_INTEGRATION_GATEWAY_URL and "
          "HUBSIGHT_ADMIN_INTEGRATION_API_KEY to run the backend smoke test.");
    }

    AdminClient client;
    QVERIFY2(client.setGatewayUrl(gateway),
             "The configured integration gateway URL was rejected by the SDK.");
    client.setApiKey(apiKey);

    QSignalSpy statusSpy(client.system(), &SystemClient::statusReceived);
    QSignalSpy errorSpy(&client, &AdminClient::errorOccurred);
    QSignalSpy completionSpy(&client, &AdminClient::requestCompleted);
    client.system()->fetchStatus();

    QTRY_VERIFY_WITH_TIMEOUT(statusSpy.count() > 0 || errorSpy.count() > 0,
                             30000);
    if (errorSpy.count() > 0) {
      const AdminError error = errorSpy.at(0).at(0).value<AdminError>();
      const QString message =
          QStringLiteral("Admin status request failed with %1 (%2).")
              .arg(error.serverCode, error.operation);
      QFAIL(qPrintable(message));
    }
    QCOMPARE(statusSpy.count(), 1);
    QCOMPARE(completionSpy.count(), 1);
    QVERIFY(completionSpy.at(0).at(1).value<HttpProtocol>() !=
            HttpProtocol::Unknown);
    QVERIFY(statusSpy.at(0).at(0).value<SystemStatus>().adminApiEnabled ||
            !statusSpy.at(0).at(0).value<SystemStatus>().apiVersion.isEmpty());
  }

  void optionalAuthenticationFlow() {
    const QUrl gateway = integrationGateway();
    const QString apiKey =
        environmentValue("HUBSIGHT_ADMIN_INTEGRATION_API_KEY");
    const QString username =
        environmentValue("HUBSIGHT_ADMIN_INTEGRATION_USERNAME");
    const QString password =
        environmentValue("HUBSIGHT_ADMIN_INTEGRATION_PASSWORD");
    if (!gateway.isValid() || gateway.host().isEmpty() || apiKey.isEmpty() ||
        username.isEmpty() || password.isEmpty()) {
      QSKIP(
          "Set gateway, API key, username, and password environment variables "
          "to run the optional JWT integration test.");
    }

    AdminApplicationClient client(std::make_shared<InMemorySecureStorage>());
    client.setAutoConnectRealtime(false);
    QVERIFY2(client.configure(gateway, apiKey),
             "The configured integration gateway could not be applied.");

    QSignalSpy authenticatedSpy(&client,
                                &AdminApplicationClient::authenticated);
    QSignalSpy twoFactorSpy(&client,
                            &AdminApplicationClient::twoFactorRequired);
    QSignalSpy errorSpy(&client, &AdminApplicationClient::errorOccurred);
    client.signIn(username, password);

    QTRY_VERIFY_WITH_TIMEOUT(authenticatedSpy.count() > 0 ||
                                 twoFactorSpy.count() > 0 ||
                                 errorSpy.count() > 0,
                             30000);
    if (twoFactorSpy.count() > 0) {
      QSKIP("The integration account requires 2FA; provide a test account or "
            "complete the challenge in an application flow.");
    }
    if (errorSpy.count() > 0) {
      const AdminError error = errorSpy.at(0).at(0).value<AdminError>();
      const QString message =
          QStringLiteral("Admin authentication failed with %1 (%2).")
              .arg(error.serverCode, error.operation);
      QFAIL(qPrintable(message));
    }

    QCOMPARE(authenticatedSpy.count(), 1);
    QVERIFY(authenticatedSpy.at(0).at(0).value<AdminUser>().isValid());
  }
};

QTEST_MAIN(AdminBackendIntegrationTest)
#include "test_admin_backend.moc"
