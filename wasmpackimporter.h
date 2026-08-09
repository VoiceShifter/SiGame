#ifndef WASMPACKIMPORTER_H
#define WASMPACKIMPORTER_H

#include <QString>

#include <functional>

class QObject;

namespace WasmPackImporter
{
using Completion = std::function<void(const QString &path, bool failed)>;

void choosePackDirectory(QObject *context, Completion completion);
void chooseProfileImage(QObject *context, Completion completion);
}

#endif // WASMPACKIMPORTER_H
