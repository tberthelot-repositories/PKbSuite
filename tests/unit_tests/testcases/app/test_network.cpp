#include "test_network.h"

#include <QUrl>
#include <QtTest>

#include "utils/misc.h"

void TestNetwork::initTestCase() {}

void TestNetwork::testSSL() {
    auto data = Utils::Misc::downloadUrl(QUrl("https://www.github.com"));

    QVERIFY(data.size() > 0);
}
