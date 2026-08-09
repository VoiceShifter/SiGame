#ifndef PACKVALIDATORSCREEN_H
#define PACKVALIDATORSCREEN_H

#include <QWidget>

class QPushButton;
class QTextEdit;

class PackValidatorScreen : public QWidget
{
      Q_OBJECT

    public:
      explicit PackValidatorScreen(QWidget *parent = nullptr);

    signals:
      void cancelled();

    private slots:
      void choosePack();

    private:
      void validatePack(const QString &path);

      QPushButton *m_chooseButton{};
      QTextEdit *m_logEdit{};
};

#endif // PACKVALIDATORSCREEN_H
