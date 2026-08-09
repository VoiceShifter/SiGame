#ifndef PACKVALIDATOR_H
#define PACKVALIDATOR_H

#include <QString>
#include <QStringList>

struct PackValidationResult
{
      QStringList logLines;
      QStringList errors;
      int roundCount{};
      int themeCount{};
      int questionCount{};

      bool isValid() const { return errors.isEmpty(); }
};

class PackValidator
{
    public:
      static PackValidationResult validate(const QString &packPath);
      static bool writeErrors(const PackValidationResult &result,
                              QString *filePath,
                              QString *errorMessage = nullptr);
};

#endif // PACKVALIDATOR_H
