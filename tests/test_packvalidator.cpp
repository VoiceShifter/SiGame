#include "packvalidator.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class PackValidatorTest : public QObject
{
      Q_OBJECT

    private:
      static void writeFile(const QString &path,
                            const QByteArray &contents = {})
      {
            QFile file(path);
            QVERIFY2(file.open(QIODevice::WriteOnly),
                     qPrintable(file.errorString()));
            QCOMPARE(file.write(contents), contents.size());
      }

      static void createPack(const QDir &pack, const QByteArray &content,
                             bool createMedia = true)
      {
            QVERIFY(pack.mkpath(QStringLiteral("Audio")));
            QVERIFY(pack.mkpath(QStringLiteral("Images")));
            QVERIFY(pack.mkpath(QStringLiteral("Video")));
            writeFile(pack.filePath(QStringLiteral("quality.marker")));
            writeFile(pack.filePath(QStringLiteral("content.xml")), content);
            if (createMedia)
            {
                  writeFile(pack.filePath(QStringLiteral("Images/q.png")),
                            QByteArrayLiteral("image"));
            }
      }

      static QByteArray validContent()
      {
            return QByteArrayLiteral(
                  "<package name=\"Test pack\"><files>"
                  "<file name=\"Images/q.png\"/></files><rounds>"
                  "<round name=\"Round\"><themes><theme name=\"Theme\">"
                  "<questions><question price=\"100\"><params>"
                  "<param name=\"question\" type=\"content\">"
                  "<item type=\"image\" isRef=\"true\">q.png</item>"
                  "</param></params><right><answer>Answer</answer></right>"
                  "</question></questions></theme></themes></round>"
                  "</rounds></package>");
      }

    private slots:
      void acceptsValidPack()
      {
            QTemporaryDir temporaryDirectory;
            QVERIFY(temporaryDirectory.isValid());
            const QDir pack(temporaryDirectory.path());
            createPack(pack, validContent());

            const PackValidationResult result =
                  PackValidator::validate(pack.path());

            QVERIFY2(result.isValid(),
                     qPrintable(result.errors.join(QLatin1Char('\n'))));
            QCOMPARE(result.roundCount, 1);
            QCOMPARE(result.themeCount, 1);
            QCOMPARE(result.questionCount, 1);
      }

      void reportsMissingQuestionMedia()
      {
            QTemporaryDir temporaryDirectory;
            QVERIFY(temporaryDirectory.isValid());
            const QDir pack(temporaryDirectory.path());
            createPack(pack, validContent(), false);

            const PackValidationResult result =
                  PackValidator::validate(pack.path());

            QVERIFY(!result.isValid());
            QVERIFY(result.errors.join(QLatin1Char('\n'))
                          .contains(QStringLiteral("Images/q.png")));
      }

      void reportsUnsupportedTypes()
      {
            QTemporaryDir temporaryDirectory;
            QVERIFY(temporaryDirectory.isValid());
            const QDir pack(temporaryDirectory.path());
            QByteArray content = validContent();
            content.replace("price=\"100\"", "price=\"100\" type=\"mystery\"");
            content.replace("<params>",
                            "<params><param name=\"answerType\">mystery"
                            "</param>");
            content.replace("type=\"image\"", "type=\"document\"");
            createPack(pack, content);

            const PackValidationResult result =
                  PackValidator::validate(pack.path());
            const QString errors = result.errors.join(QLatin1Char('\n'));

            QVERIFY(errors.contains(QStringLiteral("question type")));
            QVERIFY(errors.contains(QStringLiteral("answer type")));
            QVERIFY(errors.contains(QStringLiteral("media type")));
      }

      void reportsMalformedXml()
      {
            QTemporaryDir temporaryDirectory;
            QVERIFY(temporaryDirectory.isValid());
            const QDir pack(temporaryDirectory.path());
            createPack(pack, QByteArrayLiteral("<package><rounds>"));

            const PackValidationResult result =
                  PackValidator::validate(pack.path());

            QVERIFY(!result.isValid());
            QVERIFY(result.errors.join(QLatin1Char('\n'))
                          .contains(QStringLiteral("XML parse error")));
      }

      void writesErrorsFile()
      {
            PackValidationResult result;
            result.errors.push_back(QStringLiteral("Broken question"));
            QString path;
            QString error;

            QVERIFY2(PackValidator::writeErrors(result, &path, &error),
                     qPrintable(error));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            QCOMPARE(file.readAll(), QByteArray("Broken question\n"));
            file.close();
            QVERIFY(QFile::remove(path));
      }
};

QTEST_APPLESS_MAIN(PackValidatorTest)

#include "test_packvalidator.moc"
