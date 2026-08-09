#include "packvalidatorscreen.h"

#include "packvalidator.h"
#include "wasmpackimporter.h"

#include <QDir>
#include <QFileDialog>
#include <QPushButton>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

PackValidatorScreen::PackValidatorScreen(QWidget *parent) : QWidget(parent)
{
      auto *layout = new QVBoxLayout(this);
      m_chooseButton = new QPushButton(tr("Choose pack folder"), this);
      m_logEdit = new QTextEdit(this);
      m_logEdit->setReadOnly(true);
      m_logEdit->setPlaceholderText(
            tr("Choose a game pack folder to start validation."));
      layout->addWidget(m_chooseButton);
      layout->addWidget(m_logEdit, 1);

      connect(m_chooseButton, &QPushButton::clicked, this,
              &PackValidatorScreen::choosePack);
}

void PackValidatorScreen::choosePack()
{
#ifdef Q_OS_WASM
      WasmPackImporter::choosePackDirectory(
            this, [this](const QString &path, bool failed)
            {
                  if (failed)
                  {
                        m_logEdit->setPlainText(
                              tr("The selected pack could not be imported by "
                                 "the browser."));
                  }
                  else if (!path.isEmpty())
                  {
                        validatePack(path);
                  }
            });
#else
      const QString path = QFileDialog::getExistingDirectory(
            this, tr("Choose pack folder"), QDir::homePath());
      if (!path.isEmpty())
      {
            validatePack(path);
      }
#endif
}

void PackValidatorScreen::validatePack(const QString &path)
{
      PackValidationResult result = PackValidator::validate(path);
      QString errorsPath;
      QString writeError;
      if (PackValidator::writeErrors(result, &errorsPath, &writeError))
      {
            result.logLines.push_back(
                  tr("Errors file: %1").arg(QDir::toNativeSeparators(errorsPath)));
      }
      else
      {
            result.logLines.push_back(tr("ERROR: %1").arg(writeError));
      }
      m_logEdit->setPlainText(result.logLines.join(QLatin1Char('\n')));
      m_logEdit->moveCursor(QTextCursor::Start);
}
