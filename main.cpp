#include "mainwindow.h"

#include <QApplication>
#ifdef Q_OS_WASM
#include <QDebug>
#include <QFontDatabase>
#include <QStringList>
#endif

namespace
{
#ifdef Q_OS_WASM
void registerEmojiFont()
{
      const int fontId = QFontDatabase::addApplicationFont(
            QStringLiteral(":/Fonts/NotoColorEmoji.ttf"));
      if (fontId < 0)
      {
            qWarning() << "Unable to load the embedded emoji font";
            return;
      }
      const QStringList families =
            QFontDatabase::applicationFontFamilies(fontId);
      for (const QString &family : families)
      {
            QFontDatabase::addApplicationEmojiFontFamily(family);
            QFontDatabase::addApplicationFallbackFontFamily(
                  QChar::Script_Common, family);
      }
}
#endif
} // namespace

int main(int argc, char *argv[])
{
      QApplication a(argc, argv);
#ifdef Q_OS_WASM
      registerEmojiFont();
#endif
      MainWindow w;
      w.show();
      return a.exec();
}
